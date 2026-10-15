#include "obelisk/Dialect/Schedule/ScheduleFields.h"
//===- LowerUnitConditionals.cpp - Lower assertions and conditionals ---===//

#include "ContainerElementDescriptor.h"
#include "LowerUnit.h"

#include "mlir/Analysis/Liveness.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SaveAndRestore.h"

#include <limits>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

StringAttr UnitLowering::getLexicalScope(Operation *op) {
  for (Operation *ancestor = op->getParentOp(); ancestor;
       ancestor = ancestor->getParentOp()) {
    if (auto block = dyn_cast<semantic::SVBlockStatementOp>(ancestor)) {
      if (StringAttr path = block.getBlockPathAttr())
        return path;
      continue;
    }
    if (auto path = ancestor->getAttrOfType<StringAttr>("hierarchical_name"))
      return path;
  }
  if (outlinedLexicalScope)
    return outlinedLexicalScope;
  return sim::get<sim::metadata::HierarchicalName>(function);
}

LogicalResult UnitLowering::emitDefaultAssertionFailure(Operation *source,
                                                        Location location,
                                                        StringRef description) {
  // IEEE 1800-2023 20.10: the default fail action of an assert, assume, or
  // expect statement, or of wait_order, is a call to $error. Its message
  // names the severity, the file and line, the scope, and the simulation time.
  std::string file = "<unknown>";
  unsigned line = 0;
  if (auto source = location->findInstanceOf<FileLineColLoc>()) {
    file = source.getFilename().str();
    line = source.getLine();
  }
  StringAttr scope = getLexicalScope(source);
  auto escape = [](std::string text) {
    for (size_t position = 0;
         (position = text.find('%', position)) != std::string::npos;
         position += 2)
      text.insert(position, 1, '%');
    return text;
  };
  std::string message =
      escape((Twine("ERROR: ") + file + ":" + Twine(line) + ": " +
              (scope ? scope.getValue() : StringRef("<unknown>")))
                 .str()) +
      ": simulation time %0t: " + escape((description + " failed.").str());

  FailureOr<Value> time = currentTimeInUnits(location);
  if (failed(time))
    return failure();
  Value context = function.getBody().front().getArgument(0);
  sim::SimErrorOp::create(builder, location, context);
  Value descriptor = arith::ConstantOp::create(
      builder, location, builder.getI32Type(),
      builder.getI32IntegerAttr(static_cast<int32_t>(0x80000002u)));
  Value item =
      sim::SimBytesConstantOp::create(builder, location, message).getResult();
  auto timeMultiplier = sim::get<sim::metadata::DelayScale>(function);
  sim::SimDisplayOp::create(
      builder, location, context, descriptor, ValueRange{item, *time}, true,
      sim::Radix::Decimal, builder.getDenseI32ArrayAttr({0, 0}), scope,
      StringAttr{}, timeMultiplier, designTimePrecisionExponent());
  return success();
}

LogicalResult UnitLowering::lowerImmediateAssertion(
    semantic::SVImmediateAssertionStatementOp op) {
  Location location = getSemanticLocation(op);
  if (sim::has<sim::metadata::DefaultAssertionFailure>(op))
    return emitDefaultAssertionFailure(op, location, "immediate assertion");
  SmallVector<Operation *> children = getChildren(op);
  size_t expected = 1 + static_cast<size_t>(op.getHasPassAction()) +
                    static_cast<size_t>(op.getHasFailAction());
  if (children.size() != expected) {
    emitError(location) << "malformed immediate assertion inventory";
    return failure();
  }

  Block *controlMerge = nullptr;
  Value actionState;
  bool deferredEvaluator = sim::has<sim::metadata::DeferredEvaluator>(op);
  bool attemptControlled =
      !deferredEvaluator && sim::has<sim::metadata::AssertionControlled>(op);
  bool actionControlled =
      !deferredEvaluator &&
      sim::has<sim::metadata::AssertionActionControlled>(op);
  IntegerAttr assertionID;
  if (attemptControlled || actionControlled) {
    assertionID = sim::get<sim::metadata::AssertionControlTargetId>(op);
    if (!assertionID || !assertionID.getValue().isStrictlyPositive()) {
      emitError(location) << "immediate assertion has no prepared control ID";
      return failure();
    }
  }
  Value context = function.getBody().front().getArgument(0);
  if (attemptControlled) {
    Value enabled = sim::SimAssertionEnabledOp::create(
        builder, location, builder.getI1Type(), context, assertionID);
    Block *evaluate = addBlock();
    controlMerge = addBlock();
    cf::CondBranchOp::create(builder, location, enabled, evaluate, ValueRange{},
                             controlMerge, ValueRange{});
    setCurrent(evaluate);
  }
  if (actionControlled)
    actionState = sim::SimAssertionActionStateOp::create(
        builder, location, builder.getI32Type(), context, assertionID);

  // IEEE 1800-2023 9.2.2.2.1: what an immediate assertion's action blocks
  // read adds nothing to the implicit sensitivity of always_comb, nor by
  // 9.2.2.3 of always_latch. Their writes still leave it.
  bool insensitiveActions =
      function.getEntryKind() == sim::EntryKind::AlwaysComb ||
      function.getEntryKind() == sim::EntryKind::AlwaysLatch;
  auto withoutActionSensitivity =
      [&](function_ref<LogicalResult()> lower) -> LogicalResult {
    if (!insensitiveActions)
      return lower();
    llvm::SaveAndRestore<llvm::SetVector<Value> *> dependencies(
        observedDependencies, nullptr);
    llvm::SaveAndRestore<bool> excluded(sensitivityExcluded, true);
    return lower();
  };

  auto actionEnabled = [&](bool passed) -> Value {
    if (!actionState)
      return {};
    int32_t mask = passed ? 1 : 4;
    Value selected = arith::AndIOp::create(
        builder, location, actionState,
        arith::ConstantOp::create(builder, location, builder.getI32Type(),
                                  builder.getI32IntegerAttr(mask)));
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI32Type(), builder.getI32IntegerAttr(0));
    return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                 selected, zero);
  };

  if (op.getIsDeferred()) {
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string siteIdentity =
        (function.getSymName() + ".$deferred_assert." + Twine(node) + "." +
         Twine(node ? 0 : nextForkOrdinal))
            .str();
    uint64_t siteID = stableCodeUnitID(siteIdentity);

    FailureOr<Value> value = lowerExpression(children.front());
    if (failed(value))
      return failure();
    FailureOr<Value> condition = truthValue(*value, location);
    if (failed(condition))
      return failure();

    Block *pass = addBlock();
    Block *fail = addBlock();
    Block *merge = addBlock();
    cf::CondBranchOp::create(builder, location, *condition, pass, ValueRange{},
                             fail, ValueRange{});

    auto scheduleReport = [&](bool passed) -> LogicalResult {
      auto enqueue = sim::SimDeferredEnqueueOp::create(
          builder, location, builder.getI64IntegerAttr(siteID));
      if (auto targetID = sim::get<sim::metadata::AssertionControlTargetId>(op))
        sim::set<sim::metadata::AssertionControlTargetId>(enqueue, targetID);
      Value ticket = enqueue;

      llvm::StringMap<Value> previousValues;
      llvm::StringSet<> newlyBoundValues;
      llvm::StringMap<Operation *> referencedNodes;
      Operation *selectedAction = nullptr;
      if (passed && op.getHasPassAction())
        selectedAction = children[1];
      else if (!passed && op.getHasFailAction())
        selectedAction = children[1 + op.getHasPassAction()];
      if (selectedAction)
        selectedAction->walk([&](semantic::SVNamedValueExpressionOp named) {
          for (Operation *ancestor = named; ancestor != selectedAction;
               ancestor = ancestor->getParentOp()) {
            auto assignment =
                dyn_cast_or_null<semantic::SVAssignmentExpressionOp>(
                    ancestor->getParentOp());
            if (!assignment)
              continue;
            SmallVector<Operation *> operands = getChildren(assignment);
            Operation *operand = ancestor;
            while (operand->getParentOp() != assignment)
              operand = operand->getParentOp();
            if (!operands.empty() && operands.front() == operand)
              return;
            break;
          }
          // Input actuals are values in a deferred report. Output/inout/ref
          // actuals remain references so the matured action can update their
          // live destinations. Prepared direct calls carry one formal record
          // per argument; conservatively preserve references for an unresolved
          // output-bearing call.
          for (Operation *ancestor = named; ancestor != selectedAction;
               ancestor = ancestor->getParentOp()) {
            auto call = dyn_cast_or_null<semantic::SVCallExpressionOp>(
                ancestor->getParentOp());
            if (!call || !call.getHasOutputArguments())
              continue;
            auto formals = sim::get<sim::metadata::CalleeFormals>(call);
            if (!formals)
              return;
            SmallVector<Operation *> arguments = getChildren(call);
            Operation *argument = ancestor;
            while (argument->getParentOp() != call)
              argument = argument->getParentOp();
            auto found = llvm::find(arguments, argument);
            if (found == arguments.end())
              return;
            size_t index = static_cast<size_t>(found - arguments.begin());
            if (index >= formals.size())
              return;
            auto formal = cast<sim::CalleeFormalAttr>(formals[index]);
            if (formal.getDirection() != sim::FormalDirection::In)
              return;
            break;
          }
          StringRef path = named.getReferencedPath();
          if (!path.empty() && !referencedNodes.contains(path))
            referencedNodes[path] = named;
        });
      LogicalResult snapshotted = withoutActionSensitivity([&] {
        for (const auto &entry : referencedNodes) {
          StringRef path = entry.getKey();
          Value old = values.lookup(path);
          if (old)
            previousValues[path] = old;
          else
            newlyBoundValues.insert(path);
          FailureOr<Value> snapshot =
              lowerReferencedValue(entry.getValue(), path, /*lvalue=*/false);
          if (failed(snapshot))
            return failure();
          values[path] = *snapshot;
        }
        return success();
      });
      if (failed(snapshotted))
        return failure();

      std::string identity =
          (Twine(siteIdentity) + (passed ? ".pass" : ".fail")).str();
      auto previousCodeUnit = sim::get<sim::metadata::ForkCodeUnitId>(op);
      BoolAttr previousDeferred = op.getIsDeferredAttr();
      sim::mark<sim::metadata::DeferredEvaluator>(op);
      sim::set<sim::metadata::DeferredResult>(op, builder.getBoolAttr(passed));
      sim::set<sim::metadata::ForkCodeUnitId>(
          op, builder.getI64IntegerAttr(stableCodeUnitID(identity)));
      op->setAttr("is_deferred", builder.getBoolAttr(false));
      FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
          outlineForkBranch(op, node, passed ? 0 : 1,
                            /*captureReferences=*/false);
      op->setAttr("is_deferred", previousDeferred);
      sim::remove<sim::metadata::DeferredEvaluator>(op);
      sim::remove<sim::metadata::DeferredResult>(op);
      if (previousCodeUnit)
        sim::set<sim::metadata::ForkCodeUnitId>(op, previousCodeUnit);
      else
        sim::remove<sim::metadata::ForkCodeUnitId>(op);
      for (const auto &entry : newlyBoundValues)
        values.erase(entry.getKey());
      for (const auto &entry : previousValues)
        values[entry.getKey()] = entry.getValue();
      if (failed(callback))
        return failure();

      sim::SimFuncOp evaluator = callback->first;
      Value reportActionEnabled = actionEnabled(passed);
      SmallVector<Type> inputTypes(evaluator.getFunctionType().getInputs());
      size_t originalInputCount = inputTypes.size();
      if (reportActionEnabled)
        inputTypes.push_back(builder.getI1Type());
      inputTypes.push_back(builder.getI64Type());
      evaluator.setFunctionType(
          FunctionType::get(function.getContext(), inputTypes, TypeRange{}));
      Block &entry = evaluator.getBody().front();
      BlockArgument capturedActionEnabled;
      if (reportActionEnabled)
        capturedActionEnabled =
            entry.addArgument(builder.getI1Type(), location);
      BlockArgument reportTicket =
          entry.addArgument(builder.getI64Type(), location);
      SmallVector<Attribute> argumentAttrs;
      if (ArrayAttr attrs = evaluator.getArgAttrsAttr())
        llvm::append_range(argumentAttrs, attrs);
      while (argumentAttrs.size() < originalInputCount)
        argumentAttrs.push_back(builder.getDictionaryAttr({}));
      DictionaryAttr formal = builder.getDictionaryAttr(
          {sim::named<sim::metadata::CaptureKind>(sim::CaptureKindAttr::get(
              function.getContext(), sim::CaptureKind::Formal))});
      while (argumentAttrs.size() < inputTypes.size())
        argumentAttrs.push_back(formal);
      evaluator.setArgAttrsAttr(builder.getArrayAttr(argumentAttrs));
      if (reportActionEnabled)
        callback->second.push_back(reportActionEnabled);
      callback->second.push_back(ticket);

      Block *body = entry.splitBlock(entry.begin());
      Block *stale = new Block;
      evaluator.getBody().push_back(stale);
      OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
      Value current = sim::SimDeferredMatureOp::create(
          entryBuilder, location, entryBuilder.getI1Type(), reportTicket);
      if (capturedActionEnabled)
        current = arith::AndIOp::create(entryBuilder, location, current,
                                        capturedActionEnabled);
      cf::CondBranchOp::create(entryBuilder, location, current, body, stale);
      OpBuilder staleBuilder = OpBuilder::atBlockEnd(stale);
      sim::SimReturnOp::create(staleBuilder, location, ValueRange{});

      sim::EventRegion region = op.getIsFinal() ? sim::EventRegion::Postponed
                                                : sim::EventRegion::Observed;
      evaluator->setAttr("home_region", sim::EventRegionAttr::get(
                                            function.getContext(), region));
      evaluator->setAttr(
          "domain", sim::ExecutionDomainAttr::get(
                        function.getContext(), sim::ExecutionDomain::Design));
      // A report is canceled by its ticket rules, not by treating its
      // evaluator as an ordinary child of every currently active named scope.
      ::obelisk::schedule::set<::obelisk::schedule::Field::DetachedControls>(
          evaluator, builder.getUnitAttr());
      sim::SimSpawnOp::create(builder, location, evaluator.getSymNameAttr(),
                              callback->second, ArrayAttr{}, ArrayAttr{});
      return success();
    };

    setCurrent(pass);
    if (failed(scheduleReport(true)))
      return failure();
    emitBranch(merge);
    setCurrent(fail);
    if (failed(scheduleReport(false)))
      return failure();
    emitBranch(merge);
    setCurrent(merge);
    if (controlMerge) {
      emitBranch(controlMerge);
      setCurrent(controlMerge);
    }
    return success();
  }

  FailureOr<Value> condition;
  if (auto result = sim::get<sim::metadata::DeferredResult>(op))
    condition = arith::ConstantOp::create(
                    builder, location, builder.getBoolAttr(result.getValue()))
                    .getResult();
  else {
    FailureOr<Value> value = lowerExpression(children.front());
    if (failed(value))
      return failure();
    condition = truthValue(*value, location);
    if (failed(condition))
      return failure();
  }

  bool reactiveActions =
      sim::has<sim::metadata::DeferredEvaluator>(op) && !op.getIsFinal();
  auto lowerAction = [&](Operation *action, unsigned branch) -> LogicalResult {
    if (!reactiveActions)
      return lowerStatement(action);
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string identity =
        (function.getSymName() + ".$reactive_assert_action." + Twine(node) +
         "." + Twine(branch))
            .str();
    auto previousCodeUnit = sim::get<sim::metadata::ForkCodeUnitId>(action);
    sim::set<sim::metadata::ForkCodeUnitId>(
        action, builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlineForkBranch(action, node, branch, /*captureReferences=*/true);
    if (previousCodeUnit)
      sim::set<sim::metadata::ForkCodeUnitId>(action, previousCodeUnit);
    else
      sim::remove<sim::metadata::ForkCodeUnitId>(action);
    if (failed(callback))
      return failure();
    callback->first->setAttr(
        "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Reactive));
    sim::SimSpawnOp::create(builder, getSemanticLocation(action),
                            callback->first.getSymNameAttr(), callback->second,
                            ArrayAttr{}, ArrayAttr{});
    return success();
  };
  auto lowerDefaultFailure = [&]() -> LogicalResult {
    if (!reactiveActions)
      return emitDefaultAssertionFailure(op, location, "immediate assertion");
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string identity =
        (function.getSymName() + ".$reactive_assert_default." + Twine(node))
            .str();
    auto previousCodeUnit = sim::get<sim::metadata::ForkCodeUnitId>(op);
    sim::mark<sim::metadata::DefaultAssertionFailure>(op);
    sim::set<sim::metadata::ForkCodeUnitId>(
        op, builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlineForkBranch(op, node, 3, /*captureReferences=*/true);
    sim::remove<sim::metadata::DefaultAssertionFailure>(op);
    if (previousCodeUnit)
      sim::set<sim::metadata::ForkCodeUnitId>(op, previousCodeUnit);
    else
      sim::remove<sim::metadata::ForkCodeUnitId>(op);
    if (failed(callback))
      return failure();
    callback->first->setAttr(
        "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Reactive));
    callback->first->setAttr(
        "domain", sim::ExecutionDomainAttr::get(function.getContext(),
                                                sim::ExecutionDomain::Design));
    sim::SimSpawnOp::create(builder, location, callback->first.getSymNameAttr(),
                            callback->second, ArrayAttr{}, ArrayAttr{});
    return success();
  };

  Block *passBlock = addBlock();
  Block *failBlock = addBlock();
  Block *mergeBlock = addBlock();
  cf::CondBranchOp::create(builder, location, *condition, passBlock,
                           ValueRange{}, failBlock, ValueRange{});

  size_t nextChild = 1;
  setCurrent(passBlock);
  bool cover =
      op.getAssertionKind() == semantic::SVAssertionKind::CoverProperty ||
      op.getAssertionKind() == semantic::SVAssertionKind::CoverSequence;
  if (op.getHasPassAction()) {
    Operation *action = children[nextChild++];
    if (Value enabled = actionEnabled(true)) {
      Block *execute = addBlock();
      cf::CondBranchOp::create(builder, location, enabled, execute,
                               ValueRange{}, mergeBlock, ValueRange{});
      setCurrent(execute);
    }
    if (failed(
            withoutActionSensitivity([&] { return lowerAction(action, 0); })))
      return failure();
  }
  emitBranch(mergeBlock);

  setCurrent(failBlock);
  if (!cover) {
    if (op.getHasFailAction()) {
      Operation *action = children[nextChild++];
      if (Value enabled = actionEnabled(false)) {
        Block *execute = addBlock();
        cf::CondBranchOp::create(builder, location, enabled, execute,
                                 ValueRange{}, mergeBlock, ValueRange{});
        setCurrent(execute);
      }
      if (failed(
              withoutActionSensitivity([&] { return lowerAction(action, 1); })))
        return failure();
    } else if (op.getAssertionKind() == semantic::SVAssertionKind::Assert ||
               op.getAssertionKind() == semantic::SVAssertionKind::Assume) {
      if (Value enabled = actionEnabled(false)) {
        Block *execute = addBlock();
        cf::CondBranchOp::create(builder, location, enabled, execute,
                                 ValueRange{}, mergeBlock, ValueRange{});
        setCurrent(execute);
      }
      if (failed(lowerDefaultFailure()))
        return failure();
    }
  }
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  if (controlMerge) {
    emitBranch(controlMerge);
    setCurrent(controlMerge);
  }
  return success();
}

LogicalResult
UnitLowering::lowerQualifiedConditional(semantic::SVConditionalStatementOp op) {
  struct Branch {
    SmallVector<Operation *> expressions;
    SmallVector<Operation *> patterns;
    SmallVector<bool> hasPattern;
    Operation *body = nullptr;
    llvm::StringMap<Value> captures;
  };
  Location location = getSemanticLocation(op);
  SmallVector<Branch> branches;
  Operation *finalElse = nullptr;
  semantic::SVConditionalStatementOp cursor = op;
  while (cursor) {
    SmallVector<Operation *> children = getChildren(cursor);
    ArrayRef<int64_t> flags = cursor.getConditionPatternFlags();
    if (flags.size() != cursor.getConditionCount() ||
        cursor.getConditionCount() == 0) {
      emitError(getSemanticLocation(cursor))
          << "malformed qualified conditional inventory";
      return failure();
    }
    size_t conditionChildren = cursor.getConditionCount();
    for (int64_t flag : flags)
      conditionChildren += static_cast<size_t>(flag);
    size_t statementCount = 1 + cursor.getHasElse();
    if (children.size() != conditionChildren + statementCount) {
      emitError(getSemanticLocation(cursor))
          << "malformed qualified conditional statements";
      return failure();
    }
    Branch branch;
    size_t next = 0;
    for (int64_t flag : flags) {
      branch.expressions.push_back(children[next++]);
      branch.hasPattern.push_back(flag != 0);
      branch.patterns.push_back(flag ? children[next++] : nullptr);
    }
    branch.body = children[conditionChildren];
    branches.push_back(std::move(branch));
    if (!cursor.getHasElse())
      break;
    Operation *otherwise = children[conditionChildren + 1];
    if (auto nested = dyn_cast<semantic::SVConditionalStatementOp>(otherwise);
        nested &&
        nested.getCheckKind() == semantic::SVUniquePriorityCheck::None) {
      cursor = nested;
      continue;
    }
    finalElse = otherwise;
    break;
  }

  auto evaluateBranch = [&](Branch &branch, Block *matchedDestination,
                            Block *unmatchedDestination) -> LogicalResult {
    // Hoist this branch's capture storage ahead of its short-circuit CFG.
    // The allocation still executes once per dynamic statement execution, but
    // now dominates both later conditions and the deferred unique/unique0
    // dispatch body even when an earlier condition fails.
    SmallVector<semantic::SVVariablePatternOp> capturePatterns;
    for (Operation *pattern : branch.patterns)
      if (pattern)
        pattern->walk([&](semantic::SVVariablePatternOp variable) {
          capturePatterns.push_back(variable);
        });
    for (semantic::SVVariablePatternOp variable : capturePatterns) {
      StringRef path = variable.getReferencedPath();
      Value initial = localDefaults.lookup(path);
      if (path.empty() || !initial) {
        emitError(getSemanticLocation(variable))
            << "pattern variable has no activation-local binding";
        return failure();
      }
      if (branch.captures.count(path))
        continue;
      Value destination = sim::SimRefAllocOp::create(
          builder, getSemanticLocation(variable),
          sim::RefType::get(function.getContext(), initial.getType()), initial);
      recordImplicitEventActivationLocal(destination);
      branch.captures[path] = destination;
      values[path] = destination;
      lvalues[path] = destination;
    }

    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    for (size_t index = 0; index < branch.expressions.size(); ++index) {
      Operation *expression = branch.expressions[index];
      FailureOr<Value> value = lowerExpression(expression);
      if (failed(value))
        return failure();
      FailureOr<Value> matched;
      if (branch.hasPattern[index])
        matched =
            lowerPattern(*value, branch.patterns[index],
                         semantic::SVCaseCondition::Normal, &branch.captures);
      else
        matched = truthValue(*value, getSemanticLocation(expression));
      if (failed(matched))
        return failure();
      if (index + 1 == branch.expressions.size()) {
        cf::CondBranchOp::create(builder, getSemanticLocation(expression),
                                 *matched, matchedDestination,
                                 ValueRange{trueValue}, unmatchedDestination,
                                 ValueRange{falseValue});
      } else {
        Block *nextCondition = addBlock();
        cf::CondBranchOp::create(builder, getSemanticLocation(expression),
                                 *matched, nextCondition, ValueRange{},
                                 unmatchedDestination, ValueRange{falseValue});
        setCurrent(nextCondition);
      }
    }
    return success();
  };

  Block *mergeBlock = addBlock();
  bool inspectAll =
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique ||
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique0;
  if (!inspectAll) {
    for (Branch &branch : branches) {
      Block *bodyBlock = addBlock();
      Block *nextBranch = addBlock();
      bodyBlock->addArgument(builder.getI1Type(), location);
      nextBranch->addArgument(builder.getI1Type(), location);
      if (failed(evaluateBranch(branch, bodyBlock, nextBranch)))
        return failure();
      setCurrent(bodyBlock);
      for (auto &capture : branch.captures)
        values[capture.getKey()] = capture.getValue();
      if (failed(lowerStatement(branch.body)))
        return failure();
      emitBranch(mergeBlock);
      setCurrent(nextBranch);
    }
    if (!finalElse)
      emitQualifierWarning(op.getOperation(), location, op.getCheckKind(), "if",
                           "no match");
    else if (failed(lowerStatement(finalElse)))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(mergeBlock);
    return success();
  }

  auto i32 = builder.getI32Type();
  Value matchCount = arith::ConstantOp::create(builder, location, i32,
                                               builder.getI32IntegerAttr(0));
  Value selected = arith::ConstantOp::create(builder, location, i32,
                                             builder.getI32IntegerAttr(-1));
  for (auto [branchIndex, branch] : llvm::enumerate(branches)) {
    Block *groupDone = addBlock();
    groupDone->addArgument(builder.getI1Type(), location);
    if (failed(evaluateBranch(branch, groupDone, groupDone)))
      return failure();
    setCurrent(groupDone);
    Value groupMatched = groupDone->getArgument(0);
    Value one = arith::ConstantOp::create(builder, location, i32,
                                          builder.getI32IntegerAttr(1));
    Value incremented =
        arith::AddIOp::create(builder, location, matchCount, one);
    matchCount = arith::SelectOp::create(builder, location, groupMatched,
                                         incremented, matchCount);
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noSelection = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::slt, selected, zero);
    Value choose =
        arith::AndIOp::create(builder, location, groupMatched, noSelection);
    Value indexValue = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(branchIndex));
    selected = arith::SelectOp::create(builder, location, choose, indexValue,
                                       selected);
    if (branchIndex + 1 != branches.size()) {
      Block *nextBranch = addBlock();
      nextBranch->addArgument(i32, location);
      nextBranch->addArgument(i32, location);
      cf::BranchOp::create(builder, location, nextBranch,
                           ValueRange{matchCount, selected});
      setCurrent(nextBranch);
      matchCount = nextBranch->getArgument(0);
      selected = nextBranch->getArgument(1);
    }
  }
  Value one = arith::ConstantOp::create(builder, location, i32,
                                        builder.getI32IntegerAttr(1));
  Value overlap = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, matchCount, one);
  Block *overlapWarning = addBlock();
  Block *checkNoMatch = addBlock();
  Block *dispatch = addBlock();
  cf::CondBranchOp::create(builder, location, overlap, overlapWarning,
                           ValueRange{}, checkNoMatch, ValueRange{});
  setCurrent(overlapWarning);
  emitQualifierWarning(op.getOperation(), location, op.getCheckKind(), "if",
                       "multiple matches");
  emitBranch(dispatch);
  setCurrent(checkNoMatch);
  if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique &&
      !finalElse) {
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noMatch = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, matchCount, zero);
    Block *noMatchWarning = addBlock();
    cf::CondBranchOp::create(builder, location, noMatch, noMatchWarning,
                             ValueRange{}, dispatch, ValueRange{});
    setCurrent(noMatchWarning);
    emitQualifierWarning(op.getOperation(), location, op.getCheckKind(), "if",
                         "no match");
    emitBranch(dispatch);
  } else {
    emitBranch(dispatch);
  }
  setCurrent(dispatch);
  for (auto [branchIndex, branch] : llvm::enumerate(branches)) {
    Value indexValue = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(branchIndex));
    Value isSelected = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, selected, indexValue);
    Block *bodyBlock = addBlock();
    Block *nextDispatch = addBlock();
    cf::CondBranchOp::create(builder, location, isSelected, bodyBlock,
                             ValueRange{}, nextDispatch, ValueRange{});
    setCurrent(bodyBlock);
    for (auto &capture : branch.captures)
      values[capture.getKey()] = capture.getValue();
    if (failed(lowerStatement(branch.body)))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(nextDispatch);
  }
  if (finalElse && failed(lowerStatement(finalElse)))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

LogicalResult
UnitLowering::lowerConditional(semantic::SVConditionalStatementOp op) {
  if (op.getCheckKind() != semantic::SVUniquePriorityCheck::None)
    return lowerQualifiedConditional(op);
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  ArrayRef<int64_t> patternFlags = op.getConditionPatternFlags();
  if (op.getConditionCount() == 0 ||
      patternFlags.size() != op.getConditionCount()) {
    emitError(location) << "malformed conditional inventory";
    return failure();
  }
  size_t conditionChildren = op.getConditionCount();
  for (int64_t flag : patternFlags)
    conditionChildren += static_cast<size_t>(flag);
  size_t statementCount = 1 + op.getHasElse();
  if (children.size() != conditionChildren + statementCount) {
    emitError(location) << "malformed conditional expression inventory";
    return failure();
  }
  ArrayRef<Operation *> conditions =
      ArrayRef<Operation *>(children).take_front(conditionChildren);
  ArrayRef<Operation *> statements =
      ArrayRef<Operation *>(children).take_back(statementCount);

  // Elaboration can prove a parameter-controlled branch dead even when its
  // body contains a selection that has no legal runtime representation at
  // this parameter value. IEEE 1800-2017 12.4 treats an integral condition as
  // true exactly when it contains a known one bit. Select the sole unpatterned
  // arm here so unreachable source contributes neither lowering work nor IR.
  if (op.getConditionCount() == 1 && patternFlags.front() == 0) {
    Operation *expression = conditions.front();
    std::optional<StringRef> spelling = getConstantSpelling(expression);
    if (spelling) {
      auto semanticType = expression->getAttrOfType<TypeAttr>("semantic_type");
      std::optional<uint64_t> width =
          semanticType ? getSemanticPackedWidth(semanticType.getValue())
                       : std::nullopt;
      if (width && *width <= std::numeric_limits<unsigned>::max()) {
        FailureOr<ParsedConstant> parsed =
            parseSVInteger(*spelling, static_cast<unsigned>(*width),
                           getSemanticLocation(expression));
        if (failed(parsed))
          return failure();
        bool selected = !(parsed->value & ~parsed->unknown).isZero();
        if (selected)
          return lowerStatement(statements.front());
        if (op.getHasElse())
          return lowerStatement(statements.back());
        return success();
      }
    }
  }

  Block *thenBlock = addBlock();
  Block *elseBlock = addBlock();
  Block *mergeBlock = addBlock();
  size_t childIndex = 0;
  for (size_t conditionIndex = 0; conditionIndex < op.getConditionCount();
       ++conditionIndex) {
    Operation *expression = conditions[childIndex++];
    FailureOr<Value> conditionValue = lowerExpression(expression);
    if (failed(conditionValue))
      return failure();
    FailureOr<Value> condition;
    if (patternFlags[conditionIndex]) {
      Operation *pattern = conditions[childIndex++];
      condition = lowerPattern(*conditionValue, pattern,
                               semantic::SVCaseCondition::Normal);
    } else {
      condition = truthValue(*conditionValue, getSemanticLocation(expression));
    }
    if (failed(condition))
      return failure();
    if (conditionIndex + 1 == op.getConditionCount()) {
      cf::CondBranchOp::create(builder, getSemanticLocation(expression),
                               *condition, thenBlock, ValueRange{}, elseBlock,
                               ValueRange{});
    } else {
      Block *nextCondition = addBlock();
      cf::CondBranchOp::create(builder, getSemanticLocation(expression),
                               *condition, nextCondition, ValueRange{},
                               elseBlock, ValueRange{});
      setCurrent(nextCondition);
    }
  }
  setCurrent(thenBlock);
  if (failed(lowerStatement(statements[0])))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(elseBlock);
  if (op.getHasElse() && failed(lowerStatement(statements[1])))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

FailureOr<Value> UnitLowering::lowerPattern(Value input, Operation *pattern,
                                            semantic::SVCaseCondition condition,
                                            llvm::StringMap<Value> *captures) {
  Location location = getSemanticLocation(pattern);
  auto trueValue = [&]() -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(true));
  };
  if (isa<semantic::SVWildcardPatternOp>(pattern))
    return trueValue();
  if (auto constant = dyn_cast<semantic::SVConstantPatternOp>(pattern)) {
    SmallVector<Operation *> children = getChildren(pattern);
    if (children.size() != 1) {
      emitError(location) << "constant pattern must contain one expression";
      return failure();
    }
    return lowerCaseLabel(input, input.getType(), children.front(),
                          children.front(), condition);
  }
  if (auto variable = dyn_cast<semantic::SVVariablePatternOp>(pattern)) {
    StringRef path = variable.getReferencedPath();
    if (path.empty()) {
      emitError(location) << "pattern variable has no resolved binding";
      return failure();
    }
    Value initial = localDefaults.lookup(path);
    if (!initial || initial.getType() != input.getType()) {
      emitError(location)
          << "pattern variable has no compatible activation-local binding";
      return failure();
    }
    Value destination = captures ? captures->lookup(path) : Value{};
    if (destination) {
      auto reference = dyn_cast<sim::RefType>(destination.getType());
      if (!reference || reference.getElementType() != input.getType()) {
        emitError(location)
            << "pattern capture storage has an incompatible type";
        return failure();
      }
      sim::SimRefStoreOp::create(builder, location, input, destination);
    } else {
      destination = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), input.getType()), input);
      recordImplicitEventActivationLocal(destination);
    }
    values[path] = destination;
    lvalues[path] = destination;
    if (captures)
      (*captures)[path] = destination;
    return trueValue();
  }
  if (auto tagged = dyn_cast<semantic::SVTaggedPatternOp>(pattern)) {
    auto ordinalAttr = tagged.getFieldOrdinalAttr();
    if (!ordinalAttr || ordinalAttr.getValue().isNegative()) {
      emitError(location) << "tagged pattern has no valid field ordinal";
      return failure();
    }
    uint64_t ordinal = ordinalAttr.getValue().getZExtValue();
    Type fieldType =
        ordinal <= std::numeric_limits<unsigned>::max()
            ? sim::getAggregateElementType(input.getType(), ordinal)
            : Type{};
    if (!fieldType) {
      emitError(location) << "tagged pattern field ordinal is out of range";
      return failure();
    }
    Value active = sim::SimUnionIsActiveOp::create(
        builder, location, builder.getI1Type(), input, ordinal);
    SmallVector<Operation *> children = getChildren(pattern);
    if (children.empty())
      return active;
    if (children.size() != 1) {
      emitError(location) << "tagged pattern has malformed value inventory";
      return failure();
    }
    Value field = sim::SimUnionExtractOp::create(builder, location, fieldType,
                                                 input, ordinal);
    FailureOr<Value> nested =
        lowerPattern(field, children.front(), condition, captures);
    if (failed(nested))
      return failure();
    return arith::AndIOp::create(builder, location, active, *nested)
        .getResult();
  }
  if (auto structure = dyn_cast<semantic::SVStructurePatternOp>(pattern)) {
    ArrayRef<int64_t> ordinals = structure.getFieldOrdinals();
    SmallVector<Operation *> children = getChildren(pattern);
    if (ordinals.size() != children.size()) {
      emitError(location) << "malformed structure pattern inventory";
      return failure();
    }
    if (!isa<sim::PackedStructType, sim::UnpackedStructType>(input.getType())) {
      emitError(location) << "structure pattern input is not a fixed struct";
      return failure();
    }
    Value matched = trueValue();
    for (auto [ordinal, child] : llvm::zip_equal(ordinals, children)) {
      if (ordinal < 0) {
        emitError(location) << "structure pattern field ordinal is negative";
        return failure();
      }
      Type fieldType = sim::getAggregateElementType(
          input.getType(), static_cast<unsigned>(ordinal));
      if (!fieldType) {
        emitError(location)
            << "structure pattern field ordinal is out of range";
        return failure();
      }
      Value field = sim::SimAggregateExtractOp::create(
          builder, getSemanticLocation(child), fieldType, input, ordinal);
      FailureOr<Value> nested = lowerPattern(field, child, condition, captures);
      if (failed(nested))
        return failure();
      matched = arith::AndIOp::create(builder, getSemanticLocation(child),
                                      matched, *nested);
    }
    return matched;
  }
  emitError(location) << "unsupported executable pattern kind";
  return failure();
}

void UnitLowering::emitQualifierWarning(
    Operation *statement, Location location,
    semantic::SVUniquePriorityCheck qualifier, StringRef statementKind,
    StringRef reason) {
  StringRef qualifierName;
  switch (qualifier) {
  case semantic::SVUniquePriorityCheck::Unique:
    qualifierName = "unique";
    break;
  case semantic::SVUniquePriorityCheck::Unique0:
    qualifierName = "unique0";
    break;
  case semantic::SVUniquePriorityCheck::Priority:
    qualifierName = "priority";
    break;
  case semantic::SVUniquePriorityCheck::None:
    return;
  }
  Block *controlResume = nullptr;
  if (sim::has<sim::metadata::AssertionControlled>(statement)) {
    IntegerAttr assertionID =
        sim::get<sim::metadata::AssertionControlTargetId>(statement);
    assert(assertionID && assertionID.getValue().isStrictlyPositive() &&
           "controlled qualifier report must have a prepared identity");
    Value context = function.getBody().front().getArgument(0);
    Value enabled = sim::SimAssertionEnabledOp::create(
        builder, location, builder.getI1Type(), context, assertionID);
    Block *report = addBlock();
    controlResume = addBlock();
    cf::CondBranchOp::create(builder, location, enabled, report, ValueRange{},
                             controlResume, ValueRange{});
    setCurrent(report);
  }
  std::string file = "<unknown>";
  unsigned line = 0;
  unsigned column = 0;
  if (auto fileLocation = location->findInstanceOf<FileLineColLoc>()) {
    file = fileLocation.getFilename().str();
    line = fileLocation.getLine();
    column = fileLocation.getColumn();
  }
  std::string message = (Twine(file) + ":" + Twine(line) + ":" + Twine(column) +
                         ": warning: " + qualifierName + " " + statementKind +
                         " violation: " + reason)
                            .str();
  Value text =
      sim::SimBytesConstantOp::create(builder, location, message).getResult();
  Value descriptor = arith::ConstantOp::create(
      builder, location, builder.getI32Type(),
      builder.getI32IntegerAttr(static_cast<int32_t>(0x80000002u)));
  StringAttr scope = sim::get<sim::metadata::HierarchicalName>(function);
  IntegerAttr multiplier = sim::get<sim::metadata::DelayScale>(function);
  sim::SimDisplayOp::create(
      builder, location, function.getBody().front().getArgument(0), descriptor,
      ValueRange{text}, true, sim::Radix::Decimal, ArrayRef<int32_t>{0}, scope,
      StringAttr{}, multiplier, IntegerAttr{});
  if (controlResume) {
    emitBranch(controlResume);
    setCurrent(controlResume);
  }
}

FailureOr<Value>
UnitLowering::lowerCaseLabel(Value selector, Type selectorType,
                             Operation *selectorNode, Operation *label,
                             semantic::SVCaseCondition condition) {
  Location location = getSemanticLocation(label);
  bool selectorString = isa<sim::StringType>(selectorType);
  bool selectorFloat = isa<FloatType>(selectorType);
  Type selectorScalar = selectorString || selectorFloat
                            ? Type{}
                            : sim::getPackedScalarType(selectorType);
  bool selectorLogic = selectorScalar && isa<sim::LogicType>(selectorScalar);
  auto compareValue =
      [&](Value candidate, sim::CompareKind logicKind,
          arith::CmpIPredicate integerKind) -> FailureOr<Value> {
    // Table 11-1 gives real operands the relational and logical-equality
    // operators, so an ordered comparison carries both plain `case` labels and
    // the endpoints of a `case ... inside` range.
    if (selectorFloat)
      return compareFloatMember(selector, candidate, integerKind, location);
    FailureOr<Value> normalized =
        convert(candidate, selectorType, false, location);
    if (failed(normalized))
      return failure();
    if (selectorString) {
      Value comparison = sim::SimStringCompareOp::create(
          builder, location, builder.getI32Type(), selector, *normalized,
          builder.getBoolAttr(false));
      arith::CmpIPredicate predicate = arith::CmpIPredicate::eq;
      switch (integerKind) {
      case arith::CmpIPredicate::sge:
      case arith::CmpIPredicate::uge:
        predicate = arith::CmpIPredicate::sge;
        break;
      case arith::CmpIPredicate::sle:
      case arith::CmpIPredicate::ule:
        predicate = arith::CmpIPredicate::sle;
        break;
      default:
        break;
      }
      return arith::CmpIOp::create(builder, location, predicate, comparison,
                                   arith::ConstantOp::create(
                                       builder, location, builder.getI32Type(),
                                       builder.getI32IntegerAttr(0)))
          .getResult();
    }
    FailureOr<Value> scalarCandidate = toPackedScalar(*normalized, location);
    FailureOr<Value> scalarSelector = toPackedScalar(selector, location);
    if (failed(scalarCandidate) || failed(scalarSelector) ||
        (*scalarCandidate).getType() != (*scalarSelector).getType())
      return failure();
    if (selectorLogic) {
      bool integerResult = logicKind == sim::CompareKind::CaseEq ||
                           logicKind == sim::CompareKind::CaseZEq ||
                           logicKind == sim::CompareKind::CaseXZEq;
      return sim::SimLogicCompareOp::create(
                 builder, location,
                 integerResult
                     ? Type(builder.getI1Type())
                     : Type(sim::LogicType::get(function.getContext(), 1)),
                 logicKind, *scalarSelector, *scalarCandidate)
          .getResult();
    }
    return arith::CmpIOp::create(builder, location, integerKind,
                                 *scalarSelector, *scalarCandidate)
        .getResult();
  };

  if (condition != semantic::SVCaseCondition::Inside) {
    FailureOr<Value> candidate = lowerContextDeterminedExpression(label);
    if (failed(candidate))
      return failure();
    sim::CompareKind logicKind = sim::CompareKind::CaseEq;
    if (condition == semantic::SVCaseCondition::WildcardJustZ)
      logicKind = sim::CompareKind::CaseZEq;
    else if (condition == semantic::SVCaseCondition::WildcardXOrZ)
      logicKind = sim::CompareKind::CaseXZEq;
    return compareValue(*candidate, logicKind, arith::CmpIPredicate::eq);
  }

  auto combineLogic = [&](Value lhs, Value rhs) -> Value {
    if (selectorLogic)
      return sim::SimLogicLogicalOp::create(
          builder, location, sim::LogicType::get(function.getContext(), 1),
          sim::LogicalKind::And, lhs, rhs);
    return arith::AndIOp::create(builder, location, lhs, rhs);
  };
  Value result;
  if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(label)) {
    if (range.getRangeKind() != semantic::SVValueRangeKind::Simple) {
      emitError(location) << "case inside tolerance ranges are not executable";
      return failure();
    }
    SmallVector<Operation *> endpoints = getChildren(label);
    if (endpoints.size() != 2) {
      emitError(location) << "malformed case inside range inventory";
      return failure();
    }
    bool signedSelector = isSignedNode(selectorNode);
    if (!isUnboundedEndpoint(endpoints[0])) {
      FailureOr<Value> lower = lowerExpression(endpoints[0]);
      if (failed(lower))
        return failure();
      FailureOr<Value> above = compareValue(
          *lower,
          signedSelector ? sim::CompareKind::SGE : sim::CompareKind::UGE,
          signedSelector ? arith::CmpIPredicate::sge
                         : arith::CmpIPredicate::uge);
      if (failed(above))
        return failure();
      result = *above;
    }
    if (!isUnboundedEndpoint(endpoints[1])) {
      FailureOr<Value> upper = lowerExpression(endpoints[1]);
      if (failed(upper))
        return failure();
      FailureOr<Value> below = compareValue(
          *upper,
          signedSelector ? sim::CompareKind::SLE : sim::CompareKind::ULE,
          signedSelector ? arith::CmpIPredicate::sle
                         : arith::CmpIPredicate::ule);
      if (failed(below))
        return failure();
      result = result ? combineLogic(result, *below) : *below;
    }
  } else {
    FailureOr<Value> candidate = lowerExpression(label);
    if (failed(candidate))
      return failure();
    // 11.4.13 and 12.5.4 descend through every unpacked dimension to a
    // singular member before comparison. A multidimensional subarray is
    // never converted as though it were one packed label.
    std::function<LogicalResult(Value)> addCandidate = [&](Value value) {
      if (auto array = dyn_cast<sim::UnpackedArrayType>(value.getType())) {
        unsigned count = sim::getAggregateNumElements(array);
        for (unsigned index = 0; index < count; ++index) {
          Value element = sim::SimAggregateExtractOp::create(
              builder, location, array.getElementType(), value, index);
          if (failed(addCandidate(element)))
            return failure();
        }
        return success();
      }
      FailureOr<Value> equal = compareValue(value, sim::CompareKind::WildEq,
                                            arith::CmpIPredicate::eq);
      if (failed(equal))
        return failure();
      if (!result)
        result = *equal;
      else if (selectorLogic)
        result = sim::SimLogicLogicalOp::create(
            builder, location, sim::LogicType::get(function.getContext(), 1),
            sim::LogicalKind::Or, result, *equal);
      else
        result = arith::OrIOp::create(builder, location, result, *equal);
      return success();
    };
    if (failed(addCandidate(*candidate)))
      return failure();
    if (!result)
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
  }
  if (!result) {
    emitError(location) << "case inside item has no comparable value";
    return failure();
  }
  return truthValue(result, location);
}

LogicalResult UnitLowering::lowerCase(semantic::SVCaseStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.empty()) {
    emitError(location) << "case statement has no selector";
    return failure();
  }
  size_t itemCount = op.getItemCount();
  bool hasDefault = op.getHasDefault();
  ArrayRef<int64_t> labelCounts = op.getItemLabelCounts();
  if (labelCounts.size() != itemCount) {
    unsupported(op) << " (missing case item boundaries)";
    return failure();
  }

  // The importer emits the selector, then every item's label expressions in
  // item order, then every item body followed by the default body. Split the
  // inventory by those counts rather than by inspecting each child's kind.
  int64_t totalLabels = 0;
  for (int64_t count : labelCounts) {
    if (count <= 0) {
      unsupported(op) << " (invalid case item boundaries)";
      return failure();
    }
    totalLabels += count;
  }
  size_t statementCount = itemCount + (hasDefault ? 1 : 0);
  if (children.size() !=
      1 + static_cast<size_t>(totalLabels) + statementCount) {
    unsupported(op) << " (malformed case item inventory)";
    return failure();
  }
  ArrayRef<Operation *> labels =
      ArrayRef<Operation *>(children).slice(1, totalLabels);
  ArrayRef<Operation *> statements =
      ArrayRef<Operation *>(children).take_back(statementCount);
  // IEEE 1800-2017 6.23: a type reference shall only be compared with another
  // type reference, and such comparisons are constant expressions. A case over
  // one is therefore settled here: 12.5 runs the first item a label of which
  // names a matching type (6.22.1), and the default otherwise.
  if (std::optional<int64_t> selectorIdentity =
          getTypeReferenceIdentity(children.front())) {
    Operation *chosen = hasDefault ? statements.back() : nullptr;
    size_t nextLabel = 0;
    bool matched = false;
    for (size_t item = 0; item < itemCount; ++item) {
      ArrayRef<Operation *> itemLabels =
          labels.slice(nextLabel, static_cast<size_t>(labelCounts[item]));
      nextLabel += static_cast<size_t>(labelCounts[item]);
      for (Operation *label : itemLabels) {
        std::optional<int64_t> labelIdentity = getTypeReferenceIdentity(label);
        if (!labelIdentity) {
          unsupported(op) << " (type reference case label)";
          return failure();
        }
        if (matched || *labelIdentity != *selectorIdentity)
          continue;
        chosen = statements[item];
        matched = true;
      }
    }
    return chosen ? lowerStatement(chosen) : success();
  }

  FailureOr<Value> selector =
      lowerContextDeterminedExpression(children.front());
  if (failed(selector))
    return failure();
  // IEEE 1800-2017 12.5 compares the case expression against each item with
  // an exact match. Table 11-1 leaves `===` undefined for real and shortreal
  // while `==` accepts any type, so a floating-point selector matches with
  // ordinary equality -- a real value has no x or z bits for `===` to add.
  if (!sim::getPackedScalarType((*selector).getType()) &&
      !isa<sim::StringType, FloatType>((*selector).getType())) {
    emitError(location) << "case selector is not an executable packed value";
    return failure();
  }

  Block *mergeBlock = addBlock();

  bool inspectAll =
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique ||
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique0;
  bool disjointConstants =
      inspectAll && op.getConditionKind() == semantic::SVCaseCondition::Normal;
  // IEEE 1800-2023 12.5.3 counts matching items, not labels within one item.
  // Exact equal-width constant labels from different items cannot both match
  // the same selector, including X/Z. Keep dynamic, wildcard, real, string,
  // and width-changing cases on the general checked path.
  auto selectorWidth = sim::getPackedWidth((*selector).getType());
  DenseMap<std::pair<APInt, APInt>, size_t> constantItems;
  size_t nextConstant = 0;
  for (size_t item = 0; disjointConstants && item < itemCount; ++item)
    for (int64_t index = 0; index < labelCounts[item]; ++index) {
      Operation *label = labels[nextConstant++];
      auto labelType = getNormalizedSemanticType(label);
      auto spelling = getConstantSpelling(label);
      if (isa<semantic::SVConversionExpressionOp>(label) || failed(labelType) ||
          !spelling || !selectorWidth ||
          sim::getPackedWidth(*labelType) != selectorWidth) {
        disjointConstants = false;
        break;
      }
      auto value =
          parseSVInteger(*spelling, *selectorWidth, getSemanticLocation(label));
      if (failed(value) ||
          (!value->unknown.isZero() &&
           (isa<IntegerType>(sim::getPackedScalarType(*labelType)) ||
            isa<IntegerType>(
                sim::getPackedScalarType((*selector).getType()))))) {
        disjointConstants = false;
        break;
      }
      auto [found, inserted] = constantItems.try_emplace(
          std::make_pair(value->value, value->unknown), item);
      if (!inserted && found->second != item) {
        disjointConstants = false;
        break;
      }
    }
  inspectAll &= !disjointConstants;
  if (!inspectAll) {
    size_t nextLabel = 0;
    for (size_t item = 0; item < itemCount; ++item) {
      size_t labelCount = static_cast<size_t>(labelCounts[item]);
      ArrayRef<Operation *> itemLabels = labels.slice(nextLabel, labelCount);
      nextLabel += labelCount;
      Block *itemBlock = addBlock();
      Block *nextItemBlock = addBlock();
      for (auto [labelIndex, label] : llvm::enumerate(itemLabels)) {
        FailureOr<Value> matched =
            lowerCaseLabel(*selector, (*selector).getType(), children.front(),
                           label, op.getConditionKind());
        if (failed(matched))
          return failure();
        if (labelIndex + 1 == itemLabels.size()) {
          cf::CondBranchOp::create(builder, getSemanticLocation(label),
                                   *matched, itemBlock, ValueRange{},
                                   nextItemBlock, ValueRange{});
        } else {
          Block *nextLabelBlock = addBlock();
          cf::CondBranchOp::create(builder, getSemanticLocation(label),
                                   *matched, itemBlock, ValueRange{},
                                   nextLabelBlock, ValueRange{});
          setCurrent(nextLabelBlock);
        }
      }
      setCurrent(itemBlock);
      if (failed(lowerStatement(statements[item])))
        return failure();
      emitBranch(mergeBlock);
      setCurrent(nextItemBlock);
    }
    if ((op.getCheckKind() == semantic::SVUniquePriorityCheck::Priority ||
         (disjointConstants &&
          op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique)) &&
        !hasDefault)
      emitQualifierWarning(op.getOperation(), location, op.getCheckKind(),
                           "case", "no match");
    if (hasDefault && failed(lowerStatement(statements.back())))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(mergeBlock);
    return success();
  }

  auto i32 = builder.getI32Type();
  Value matchCount = arith::ConstantOp::create(builder, location, i32,
                                               builder.getI32IntegerAttr(0));
  Value selected = arith::ConstantOp::create(builder, location, i32,
                                             builder.getI32IntegerAttr(-1));
  size_t nextLabel = 0;
  for (size_t item = 0; item < itemCount; ++item) {
    size_t labelCount = static_cast<size_t>(labelCounts[item]);
    ArrayRef<Operation *> itemLabels = labels.slice(nextLabel, labelCount);
    nextLabel += labelCount;
    Block *groupDone = addBlock();
    groupDone->addArgument(builder.getI1Type(), location);
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    for (auto [labelIndex, label] : llvm::enumerate(itemLabels)) {
      FailureOr<Value> matched =
          lowerCaseLabel(*selector, (*selector).getType(), children.front(),
                         label, op.getConditionKind());
      if (failed(matched))
        return failure();
      if (labelIndex + 1 == itemLabels.size()) {
        cf::CondBranchOp::create(builder, getSemanticLocation(label), *matched,
                                 groupDone, ValueRange{trueValue}, groupDone,
                                 ValueRange{falseValue});
      } else {
        Block *nextLabelBlock = addBlock();
        cf::CondBranchOp::create(builder, getSemanticLocation(label), *matched,
                                 groupDone, ValueRange{trueValue},
                                 nextLabelBlock, ValueRange{});
        setCurrent(nextLabelBlock);
      }
    }
    setCurrent(groupDone);
    Value groupMatched = groupDone->getArgument(0);
    Value one = arith::ConstantOp::create(builder, location, i32,
                                          builder.getI32IntegerAttr(1));
    Value incremented =
        arith::AddIOp::create(builder, location, matchCount, one);
    matchCount = arith::SelectOp::create(builder, location, groupMatched,
                                         incremented, matchCount);
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noSelection = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::slt, selected, zero);
    Value selectThis =
        arith::AndIOp::create(builder, location, groupMatched, noSelection);
    Value itemIndex = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(item));
    selected = arith::SelectOp::create(builder, location, selectThis, itemIndex,
                                       selected);
    if (item + 1 != itemCount) {
      Block *nextGroup = addBlock();
      nextGroup->addArgument(i32, location);
      nextGroup->addArgument(i32, location);
      cf::BranchOp::create(builder, location, nextGroup,
                           ValueRange{matchCount, selected});
      setCurrent(nextGroup);
      matchCount = nextGroup->getArgument(0);
      selected = nextGroup->getArgument(1);
    }
  }

  Value one = arith::ConstantOp::create(builder, location, i32,
                                        builder.getI32IntegerAttr(1));
  Value overlap = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, matchCount, one);
  Block *overlapWarning = addBlock();
  Block *checkNoMatch = addBlock();
  Block *dispatch = addBlock();
  cf::CondBranchOp::create(builder, location, overlap, overlapWarning,
                           ValueRange{}, checkNoMatch, ValueRange{});
  setCurrent(overlapWarning);
  emitQualifierWarning(op.getOperation(), location, op.getCheckKind(), "case",
                       "multiple matches");
  emitBranch(dispatch);
  setCurrent(checkNoMatch);
  if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique &&
      !hasDefault) {
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noMatch = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, matchCount, zero);
    Block *noMatchWarning = addBlock();
    cf::CondBranchOp::create(builder, location, noMatch, noMatchWarning,
                             ValueRange{}, dispatch, ValueRange{});
    setCurrent(noMatchWarning);
    emitQualifierWarning(op.getOperation(), location, op.getCheckKind(), "case",
                         "no match");
    emitBranch(dispatch);
  } else {
    emitBranch(dispatch);
  }

  setCurrent(dispatch);
  for (size_t item = 0; item < itemCount; ++item) {
    Value itemIndex = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(item));
    Value isSelected = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, selected, itemIndex);
    Block *itemBlock = addBlock();
    Block *nextDispatch = addBlock();
    cf::CondBranchOp::create(builder, location, isSelected, itemBlock,
                             ValueRange{}, nextDispatch, ValueRange{});
    setCurrent(itemBlock);
    if (failed(lowerStatement(statements[item])))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(nextDispatch);
  }
  if (hasDefault && failed(lowerStatement(statements.back())))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

Value UnitLowering::emitRandomBounded(Value bound, Location location) {
  auto type = cast<IntegerType>(bound.getType());
  unsigned width = type.getWidth();
  auto i64 = builder.getI64Type();
  Value context = function.getBody().front().getArgument(0);
  if (width <= 64) {
    Value range = bound;
    if (width < 64)
      range = arith::ExtUIOp::create(builder, location, i64, bound);
    Value draw =
        sim::SimRandomBoundedOp::create(builder, location, i64, context, range);
    return width == 64 ? draw
                       : arith::TruncIOp::create(builder, location, type, draw);
  }

  auto constant = [&](uint64_t value) -> Value {
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getIntegerAttr(type, value));
  };
  // Saturate bound-1's leading bit into a mask of its significant bits. This
  // leaves an exact power-of-two proposal domain containing [0, bound), even
  // when a live bound is much smaller than its declared width. The compiler
  // folds the mask for constant weights; no target big-integer helper is
  // needed.
  Value zero = constant(0);
  Value mask = arith::SubIOp::create(builder, location, bound, constant(1));
  for (uint64_t shift = 1; shift < width; shift *= 2) {
    Value suffix =
        arith::ShRUIOp::create(builder, location, mask, constant(shift));
    mask = arith::OrIOp::create(builder, location, mask, suffix);
  }

  Block *drawBlock = addBlock();
  Block *acceptedBlock = addBlock();
  Value accepted = acceptedBlock->addArgument(type, location);
  emitBranch(drawBlock);
  setCurrent(drawBlock);
  Value draw = zero;
  for (uint64_t offset = 0; offset < width; offset += 64) {
    Value word = sim::SimRandomNextOp::create(builder, location, i64, context);
    Value extended = arith::ExtUIOp::create(builder, location, type, word);
    if (offset)
      extended =
          arith::ShLIOp::create(builder, location, extended, constant(offset));
    draw = arith::OrIOp::create(builder, location, draw, extended);
  }
  draw = arith::AndIOp::create(builder, location, draw, mask);
  Value valid = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::ult, draw, bound);
  // Every accepted rank has exactly one pattern in the masked proposal domain.
  // Rejecting its unused tail is unbiased, with rejection probability below
  // 1/2.
  cf::CondBranchOp::create(builder, location, valid, acceptedBlock,
                           ValueRange{draw}, drawBlock, ValueRange{});
  setCurrent(acceptedBlock);
  return accepted;
}

LogicalResult UnitLowering::lowerRandCase(semantic::SVRandCaseStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  size_t itemCount = op.getItemCount();
  // The importer emits every item's weight expression in item order, then
  // every item body. The verifier already pairs the two halves.
  if (itemCount == 0 || children.size() != 2 * itemCount) {
    emitError(location) << "malformed randcase item inventory";
    return failure();
  }
  ArrayRef<Operation *> weights =
      ArrayRef<Operation *>(children).take_front(itemCount);
  ArrayRef<Operation *> statements =
      ArrayRef<Operation *>(children).take_back(itemCount);

  unsigned sumWidth = 0;
  for (Operation *node : weights) {
    FailureOr<Type> type = getNormalizedSemanticType(node);
    std::optional<unsigned> width =
        succeeded(type) ? sim::getPackedWidth(*type) : std::nullopt;
    if (!width || *width == 0) {
      emitError(getSemanticLocation(node))
          << "randcase weights require integral values";
      return failure();
    }
    sumWidth = std::max(sumWidth, *width);
  }
  Type sumType = builder.getIntegerType(sumWidth);
  Value zero = arith::ConstantOp::create(builder, location, sumType,
                                         builder.getIntegerAttr(sumType, 0));

  // IEEE 1800-2023 18.16 evaluates self-determined weights at most once.
  // Summands are unsigned and their sum has the maximum operand precision.
  // Preserve that precision, including overflow, when selecting a rank.
  SmallVector<Value> bounds;
  bounds.reserve(itemCount);
  Value total = zero;
  for (Operation *node : weights) {
    Location weightLocation = getSemanticLocation(node);
    FailureOr<Value> value = lowerExpression(node);
    if (failed(value))
      return failure();
    FailureOr<Value> weight =
        convert(*value, sumType, false, weightLocation, false);
    if (failed(weight))
      return failure();
    total = arith::AddIOp::create(builder, weightLocation, total, *weight);
    bounds.push_back(total);
  }

  Block *mergeBlock = addBlock();
  Block *selectBlock = addBlock();
  // An all-zero weight list selects no branch at all.
  Value anyWeight = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ne, total, zero);
  cf::CondBranchOp::create(builder, location, anyWeight, selectBlock,
                           ValueRange{}, mergeBlock, ValueRange{});
  setCurrent(selectBlock);

  // The draw shares the process random number generator with $urandom and
  // lands in [0, total), so it always falls inside the final item's bound and
  // that item needs no test of its own.
  Value draw = emitRandomBounded(total, location);
  for (size_t item = 0; item + 1 < itemCount; ++item) {
    Block *itemBlock = addBlock();
    Block *nextItemBlock = addBlock();
    Value selected = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, draw, bounds[item]);
    cf::CondBranchOp::create(builder, location, selected, itemBlock,
                             ValueRange{}, nextItemBlock, ValueRange{});
    setCurrent(itemBlock);
    if (failed(lowerStatement(statements[item])))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(nextItemBlock);
  }
  if (failed(lowerStatement(statements.back())))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

// Only the grammar controls contain syntactic production occurrences. Code
// blocks may contain another randsequence with its own production inventory.
static void
collectRandSequenceItems(Operation *node,
                         SmallVectorImpl<semantic::SVProdItemOp> &items) {
  if (auto item = dyn_cast<semantic::SVProdItemOp>(node)) {
    items.push_back(item);
    return;
  }
  if (isa<semantic::SVCodeBlockProdOp>(node))
    return;
  for (Operation *child : getChildren(node))
    collectRandSequenceItems(child, items);
}

LogicalResult
UnitLowering::copyRandSequenceOutputs(RandSequenceProductionReturn &production,
                                      Location location) {
  for (auto &output : production.copyOuts) {
    FailureOr<Value> value = loadReference(output.formalReference, location);
    if (failed(value))
      return failure();
    FailureOr<Value> converted =
        convert(*value, output.destination.type, output.formalSigned, location,
                output.destinationSigned);
    if (failed(converted) ||
        failed(writeCapturedLValue(output.destination, *converted,
                                   output.destinationSigned, false, location)))
      return failure();
  }
  return success();
}

LogicalResult
UnitLowering::lowerRandSequence(semantic::SVRandSequenceStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  RandSequenceContext context;
  context.breakTarget = addBlock();
  context.controlDepth = controlScopes.size();
  context.loopDepth = loopTargets.size();
  context.productionDepth = randSequenceProductionReturns.size();
  for (Operation *child : children) {
    auto production = dyn_cast<semantic::SVFrozenRandSeqProductionOp>(child);
    if (!production) {
      emitError(getSemanticLocation(child))
          << "randsequence contains a non-production child";
      return failure();
    }
    StringRef identity = production.getReferencedSymbol().getLeafReference();
    if (!context.productions.try_emplace(identity, production).second) {
      emitError(getSemanticLocation(production))
          << "duplicate frozen randsequence production " << identity;
      return failure();
    }
  }
  auto productionCount = op->getAttrOfType<IntegerAttr>("production_count");
  if (!productionCount || productionCount.getInt() < 0 ||
      context.productions.size() !=
          static_cast<uint64_t>(productionCount.getInt())) {
    emitError(location) << "malformed frozen randsequence production inventory";
    return failure();
  }

  // Resolve every production reference before emitting any CFG, and find
  // the productions on a cycle. 18.17 permits recursion; those productions
  // get a single body with activation frames instead of being expanded.
  llvm::StringMap<SmallVector<StringRef>> successors;
  for (auto &entry : context.productions) {
    SmallVector<semantic::SVProdItemOp> items;
    collectRandSequenceItems(entry.second, items);
    SmallVector<StringRef> &targets = successors[entry.getKey()];
    for (semantic::SVProdItemOp item : items) {
      auto reference = item->getAttrOfType<SymbolRefAttr>("target");
      if (!reference) {
        emitError(getSemanticLocation(item))
            << "randsequence production item has no target";
        return failure();
      }
      auto target =
          context.productions.find(reference.getLeafReference().getValue());
      if (target == context.productions.end()) {
        emitError(getSemanticLocation(item))
            << "randsequence references unknown production " << reference;
        return failure();
      }
      targets.push_back(target->getKey());
    }
  }
  llvm::StringMap<llvm::StringSet<>> reachable;
  for (auto &entry : context.productions) {
    llvm::StringSet<> &seen = reachable[entry.getKey()];
    SmallVector<StringRef> worklist(successors[entry.getKey()]);
    while (!worklist.empty()) {
      StringRef next = worklist.pop_back_val();
      if (seen.insert(next).second)
        llvm::append_range(worklist, successors[next]);
    }
  }
  SmallVector<StringRef> identities;
  for (auto &entry : context.productions)
    identities.push_back(entry.getKey());
  llvm::sort(identities);
  for (StringRef identity : identities) {
    if (!reachable[identity].contains(identity) ||
        context.recursiveComponents.count(identity))
      continue;
    unsigned component = context.recursiveComponents.size() + 1;
    for (StringRef other : identities)
      if (reachable[identity].contains(other) &&
          reachable[other].contains(identity))
        context.recursiveComponents[other] = component;
  }
  auto first = op->getAttrOfType<SymbolRefAttr>("first_production");
  if (!first) {
    emitError(location) << "randsequence has no first production";
    return failure();
  }
  if (!context.productions.count(first.getLeafReference())) {
    emitError(location) << "randsequence cannot resolve its first production";
    return failure();
  }

  bool recursive = !context.recursiveComponents.empty();
  Block *firstBlock = nullptr;
  context.frames.push_back({0, {}});
  if (recursive) {
    firstBlock = addBlock();
    context.slotBlock = firstBlock;
    cf::BranchOp::create(builder, location, firstBlock);
    setCurrent(firstBlock);
    Block *start = addBlock();
    cf::BranchOp::create(builder, location, start);
    setCurrent(start);
  }
  randSequenceContexts.push_back(std::move(context));
  llvm::scope_exit popContext([&] { randSequenceContexts.pop_back(); });
  if (recursive)
    randSequenceContexts.back().returnStack =
        createRandSequenceStack(builder.getI64Type(), location);
  auto initial =
      randSequenceContexts.back().productions.find(first.getLeafReference());
  if (failed(lowerRandSequenceProduction(initial->second, {})))
    return failure();
  Block *exit = randSequenceContexts.back().breakTarget;
  emitBranch(exit);

  if (recursive) {
    // Emit each recursive body once, in the statement's own scope; a body
    // may call productions whose bodies are not emitted yet.
    while (!randSequenceContexts.back().pendingBodies.empty()) {
      std::string identity =
          randSequenceContexts.back().pendingBodies.pop_back_val();
      RecursiveProductionBody &body =
          randSequenceContexts.back().recursiveBodies.find(identity)->second;
      Block *last = &function.getBody().back();
      randSequenceContexts.back().currentFrame = body.frame;
      setCurrent(body.entry);
      LogicalResult lowered =
          lowerRandSequenceProduction(body.production, {}, {}, &body);
      randSequenceContexts.back().currentFrame = 0;
      if (failed(lowered))
        return failure();
      for (Block &block : llvm::make_range(std::next(last->getIterator()),
                                           function.getBody().end()))
        randSequenceContexts.back().blockFrames[&block] = body.frame;
    }
    RandSequenceContext &context = randSequenceContexts.back();
    Type i64 = builder.getI64Type();
    for (auto &entry : context.recursiveBodies) {
      RecursiveProductionBody &body = entry.second;
      context.blockFrames[body.entry] = body.frame;
      context.blockFrames[body.exit] = body.frame;
      // The exit pops its caller's resume index and returns there.
      setCurrent(body.exit);
      Value size = sim::SimContainerSizeOp::create(builder, location, i64,
                                                   context.returnStack);
      Value one = arith::ConstantOp::create(builder, location, i64,
                                            builder.getI64IntegerAttr(1));
      Value last = arith::SubIOp::create(builder, location, size, one);
      Value index = sim::SimContainerReadOp::create(builder, location, i64,
                                                    context.returnStack, last);
      sim::SimQueueDeleteOp::create(builder, location, context.returnStack,
                                    last);
      SmallVector<APInt> cases;
      SmallVector<ValueRange> operands;
      for (size_t ordinal = 0; ordinal < body.resumes.size(); ++ordinal) {
        cases.push_back(APInt(64, ordinal));
        operands.push_back(ValueRange{});
      }
      cf::SwitchOp::create(builder, location, index, body.resumes.front(),
                           ValueRange{}, cases, body.resumes, operands);
    }
    if (failed(repairRecursiveRandSequence(firstBlock, location)))
      return failure();
  }
  setCurrent(exit);
  return success();
}

LogicalResult UnitLowering::lowerRandSequenceProduction(
    semantic::SVFrozenRandSeqProductionOp production,
    ArrayRef<Operation *> actuals, Value resultReference,
    RecursiveProductionBody *body) {
  Location location = getSemanticLocation(production);
  if (!body && randSequenceContexts.back().recursiveComponents.count(
                   production.getReferencedSymbol().getLeafReference()))
    return lowerRecursiveProductionCall(production, actuals, resultReference);
  auto productionType = production->getAttrOfType<TypeAttr>("semantic_type");
  if (!productionType) {
    emitError(location) << "randsequence production has no semantic type";
    return failure();
  }
  bool returnsValue = !isa<semantic::VoidType>(productionType.getValue());
  Type resultType;
  if (returnsValue) {
    FailureOr<Type> normalized =
        normalizeSemanticType(productionType.getValue(), location, function);
    if (failed(normalized))
      return failure();
    resultType = *normalized;
  } else if (resultReference) {
    return emitError(location) << "void production cannot bind a result";
  }

  ArrayAttr formals = production.getFormalArguments();
  if (formals.size() != production.getArgumentCount() ||
      actuals.size() > formals.size()) {
    emitError(location) << "malformed randsequence formal inventory";
    return failure();
  }
  SmallVector<Operation *> children = getChildren(production);
  SmallVector<Operation *> defaultExpressions(formals.size(), nullptr);
  size_t nextChild = 0;
  for (auto [index, attribute] : llvm::enumerate(formals)) {
    auto formal = dyn_cast<DictionaryAttr>(attribute);
    auto defaultCount = formal
                            ? formal.getAs<IntegerAttr>("default_operand_count")
                            : IntegerAttr{};
    if (!formal || !defaultCount || defaultCount.getInt() < 0 ||
        defaultCount.getInt() > 1 ||
        nextChild + defaultCount.getInt() > children.size()) {
      emitError(location) << "malformed randsequence formal default inventory";
      return failure();
    }
    if (defaultCount.getInt() == 1)
      defaultExpressions[index] = children[nextChild++];
  }

  struct SavedBinding {
    std::string path;
    Value value;
    Value lvalue;
    bool hadValue;
    bool hadLValue;
  };
  SmallVector<SavedBinding> savedBindings;
  llvm::scope_exit restoreBindings([&] {
    for (const SavedBinding &saved : llvm::reverse(savedBindings)) {
      if (saved.hadValue)
        values[saved.path] = saved.value;
      else
        values.erase(saved.path);
      if (saved.hadLValue)
        lvalues[saved.path] = saved.lvalue;
      else
        lvalues.erase(saved.path);
    }
  });

  struct Actual {
    Value value{};
    Value reference{};
    std::optional<CapturedLValue> destination;
    bool sourceSigned = false;
  };
  SmallVector<Actual> explicitValues;
  SmallVector<RandSequenceProductionReturn::CopyOut> copyOuts;
  // A recursive body binds its formals to its slots; each call stores the
  // actuals there and copies results back on return.
  if (body)
    for (auto [index, attribute] : llvm::enumerate(formals)) {
      auto path =
          cast<DictionaryAttr>(attribute).getAs<StringAttr>("referenced_path");
      std::string bindingPath = path.getValue().str();
      savedBindings.push_back(
          {bindingPath, values.lookup(bindingPath), lvalues.lookup(bindingPath),
           values.count(bindingPath) != 0, lvalues.count(bindingPath) != 0});
      values[bindingPath] = body->formalSlots[index];
      lvalues[bindingPath] = body->formalSlots[index];
    }
  // Evaluate actual values and capture actual addresses in the caller's
  // scope before binding any callee formal. Outputs do not read the actual.
  for (auto [index, attribute] :
       llvm::enumerate(body ? ArrayRef<Attribute>{} : formals.getValue())) {
    auto formal = dyn_cast<DictionaryAttr>(attribute);
    auto direction =
        formal ? formal.getAs<IntegerAttr>("direction") : IntegerAttr{};
    if (!direction || direction.getInt() < 0 || direction.getInt() > 3)
      return emitError(location) << "malformed randsequence formal direction";
    Operation *actual = index < actuals.size() ? actuals[index] : nullptr;
    if (!actual || isa<semantic::SVEmptyArgumentExpressionOp>(actual))
      actual = defaultExpressions[index];
    if (!actual)
      return emitError(location) << "randsequence production argument " << index
                                 << " has no actual or default value";
    if (direction.getInt() !=
        static_cast<int64_t>(semantic::SVArgumentDirection::In)) {
      if (isa<semantic::SVAssignmentExpressionOp>(actual)) {
        auto children = getChildren(actual);
        if (children.size() == 2 &&
            isa<semantic::SVEmptyArgumentExpressionOp>(children.back()))
          actual = children.front();
      }
    }
    Actual argument;
    argument.sourceSigned = isSignedNode(actual);
    if (!isa<semantic::SVEmptyArgumentExpressionOp>(actual)) {
      if (direction.getInt() ==
          static_cast<int64_t>(semantic::SVArgumentDirection::In)) {
        FailureOr<Value> value = lowerExpression(actual);
        if (failed(value))
          return failure();
        argument.value = cloneSequentialValue(*value, location);
      } else if (direction.getInt() ==
                 static_cast<int64_t>(semantic::SVArgumentDirection::Ref)) {
        FailureOr<Value> reference = lowerExpression(actual, /*lvalue=*/true);
        if (failed(reference))
          return failure();
        argument.reference = *reference;
      } else {
        FailureOr<CapturedLValue> destination = captureLValue(actual, location);
        if (failed(destination))
          return failure();
        argument.destination = *destination;
        if (direction.getInt() ==
            static_cast<int64_t>(semantic::SVArgumentDirection::InOut)) {
          FailureOr<Value> value = loadCapturedLValue(*destination, location);
          if (failed(value))
            return failure();
          argument.value = cloneSequentialValue(*value, location);
        }
      }
    }
    explicitValues.push_back(std::move(argument));
  }
  for (auto [index, attribute] :
       llvm::enumerate(body ? ArrayRef<Attribute>{} : formals.getValue())) {
    auto formal = cast<DictionaryAttr>(attribute);
    auto direction = formal.getAs<IntegerAttr>("direction");
    auto path = formal.getAs<StringAttr>("referenced_path");
    auto semanticType = formal.getAs<TypeAttr>("semantic_type");
    if (!direction || direction.getInt() < 0 || direction.getInt() > 3 ||
        !path || !semanticType) {
      emitError(location) << "randsequence requires typed formal arguments";
      return failure();
    }
    FailureOr<Type> type =
        normalizeSemanticType(semanticType.getValue(), location, function);
    if (failed(type))
      return failure();
    bool formalSigned = isSignedSemanticType(semanticType.getValue());
    Actual argument = explicitValues[index];
    if (direction.getInt() ==
        static_cast<int64_t>(semantic::SVArgumentDirection::Ref)) {
      if (!argument.reference ||
          getReferenceElementType(argument.reference) != *type) {
        return emitError(location)
               << "randsequence ref actual must be an equivalent variable "
                  "reference";
      }
      FailureOr<Value> reference =
          isa<sim::RefType>(argument.reference.getType())
              ? FailureOr<Value>(argument.reference)
              : toArgumentReference(argument.reference, *type, location);
      if (failed(reference))
        return emitError(location)
               << "randsequence ref actual is not addressable";
      std::string bindingPath = path.getValue().str();
      savedBindings.push_back(
          {bindingPath, values.lookup(bindingPath), lvalues.lookup(bindingPath),
           values.count(bindingPath) != 0, lvalues.count(bindingPath) != 0});
      values[bindingPath] = *reference;
      lvalues[bindingPath] = *reference;
      continue;
    }
    Value initial;
    bool sourceSigned = false;
    if (direction.getInt() ==
        static_cast<int64_t>(semantic::SVArgumentDirection::Out)) {
      initial = createDefaultValue(builder, location, *type);
      if (!initial)
        return emitError(location) << "unsupported randsequence output type";
    } else if (argument.value) {
      initial = argument.value;
      sourceSigned = argument.sourceSigned;
    } else {
      emitError(location) << "randsequence production argument " << index
                          << " has no actual or default value";
      return failure();
    }
    FailureOr<Value> converted =
        convert(initial, *type, sourceSigned, location);
    if (failed(converted))
      return failure();
    std::string bindingPath = path.getValue().str();
    savedBindings.push_back(
        {bindingPath, values.lookup(bindingPath), lvalues.lookup(bindingPath),
         values.count(bindingPath) != 0, lvalues.count(bindingPath) != 0});
    Value reference = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), *type),
        *converted);
    values[bindingPath] = reference;
    lvalues[bindingPath] = reference;
    if (argument.destination)
      copyOuts.push_back({*argument.destination, reference, formalSigned,
                          argument.sourceSigned});
  }

  if (body)
    resultReference = body->resultSlot;
  if (returnsValue) {
    Value undefined = createDefaultValue(builder, location, resultType);
    if (!undefined)
      return emitError(location) << "unsupported randsequence return type";
    if (body) {
      sim::SimRefStoreOp::create(builder, location, undefined, resultReference);
    } else if (!resultReference) {
      resultReference = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), resultType), undefined);
    } else if (getReferenceElementType(resultReference) != resultType ||
               failed(storeReference(resultReference, undefined, location))) {
      return emitError(location) << "randsequence return binding type mismatch";
    }
  }

  ArrayRef<int64_t> itemCounts = production.getRuleItemCounts();
  ArrayRef<int64_t> hasWeights = production.getRuleHasWeights();
  ArrayRef<int64_t> hasWeightCodeBlocks =
      production.getRuleHasWeightCodeBlocks();
  ArrayRef<int64_t> isRandJoin = production.getRuleIsRandJoin();
  ArrayRef<int64_t> hasRandJoinExpressions =
      production.getRuleHasRandJoinExpressions();
  ArrayAttr ruleVariables = production.getRuleVariables();
  size_t ruleCount = production.getRuleCount();
  if (itemCounts.size() != ruleCount || hasWeights.size() != ruleCount ||
      hasWeightCodeBlocks.size() != ruleCount ||
      isRandJoin.size() != ruleCount ||
      hasRandJoinExpressions.size() != ruleCount ||
      ruleVariables.size() != ruleCount) {
    emitError(location) << "malformed randsequence rule metadata";
    return failure();
  }

  struct Rule {
    Operation *weight = nullptr;
    SmallVector<Operation *> items;
    Operation *weightCodeBlock = nullptr;
    bool randJoin = false;
    Operation *randJoinBias = nullptr;
    ArrayAttr variables;
  };
  SmallVector<Rule> rules;
  rules.reserve(ruleCount);
  for (size_t index = 0; index < ruleCount; ++index) {
    if (itemCounts[index] < 0 ||
        (hasWeights[index] != 0 && hasWeights[index] != 1) ||
        (hasWeightCodeBlocks[index] != 0 && hasWeightCodeBlocks[index] != 1) ||
        (isRandJoin[index] != 0 && isRandJoin[index] != 1) ||
        (hasRandJoinExpressions[index] != 0 &&
         hasRandJoinExpressions[index] != 1)) {
      emitError(location) << "invalid randsequence rule metadata";
      return failure();
    }
    Rule rule;
    rule.variables = dyn_cast<ArrayAttr>(ruleVariables[index]);
    if (!rule.variables)
      return emitError(location) << "malformed randsequence rule variables";
    rule.randJoin = isRandJoin[index];
    if (hasWeights[index]) {
      if (nextChild == children.size())
        return emitError(location) << "missing randsequence rule weight";
      rule.weight = children[nextChild++];
    }
    if (hasRandJoinExpressions[index]) {
      if (nextChild == children.size())
        return emitError(location) << "missing rand join expression";
      rule.randJoinBias = children[nextChild++];
    }
    if (nextChild + itemCounts[index] > children.size()) {
      emitError(location) << "truncated randsequence production list";
      return failure();
    }
    llvm::append_range(rule.items, ArrayRef<Operation *>(children).slice(
                                       nextChild, itemCounts[index]));
    nextChild += itemCounts[index];
    if (hasWeightCodeBlocks[index]) {
      if (nextChild == children.size())
        return emitError(location) << "missing randsequence weight code block";
      rule.weightCodeBlock = children[nextChild++];
    }
    rules.push_back(std::move(rule));
  }
  if (nextChild != children.size()) {
    emitError(location) << "excess randsequence production operands";
    return failure();
  }

  Block *productionReturn = body ? body->exit : addBlock();
  randSequenceProductionReturns.push_back(
      {productionReturn, controlScopes.size(), resultReference,
       std::move(copyOuts)});
  llvm::scope_exit popReturn([&] { randSequenceProductionReturns.pop_back(); });
  auto lowerRandJoin = [&](const Rule &rule) -> LogicalResult {
    if (rule.items.size() < 2)
      return emitError(location)
             << "rand join requires at least two production items";

    SmallVector<Operation *> streams;
    streams.reserve(rule.items.size());
    for (Operation *operand : rule.items) {
      auto item = dyn_cast<semantic::SVProdItemOp>(operand);
      auto targetRef =
          item ? item->getAttrOfType<SymbolRefAttr>("target") : SymbolRefAttr{};
      auto target = targetRef ? randSequenceContexts.back().productions.find(
                                    targetRef.getLeafReference())
                              : randSequenceContexts.back().productions.end();
      auto argumentCount =
          item ? item->getAttrOfType<IntegerAttr>("argument_count")
               : IntegerAttr{};
      if (!item || !targetRef ||
          target == randSequenceContexts.back().productions.end() ||
          !argumentCount || argumentCount.getInt() != 0 ||
          !getChildren(item).empty() ||
          target->second.getArgumentCount() != 0 ||
          target->second.getRuleCount() != 1 ||
          target->second.getRuleItemCounts().size() != 1 ||
          target->second.getRuleItemCounts()[0] != 1 ||
          target->second.getRuleHasWeights().size() != 1 ||
          target->second.getRuleHasWeights()[0] ||
          target->second.getRuleHasWeightCodeBlocks().size() != 1 ||
          target->second.getRuleHasWeightCodeBlocks()[0] ||
          target->second.getRuleIsRandJoin().size() != 1 ||
          target->second.getRuleIsRandJoin()[0] ||
          target->second.getRuleHasRandJoinExpressions().size() != 1 ||
          target->second.getRuleHasRandJoinExpressions()[0] ||
          !target->second.getFormalArguments().empty()) {
        return emitError(getSemanticLocation(operand))
               << "rand join currently requires deterministic single-item "
                  "operand productions of equal depth-one length";
      }
      auto targetType =
          target->second->getAttrOfType<TypeAttr>("semantic_type");
      SmallVector<Operation *> targetChildren = getChildren(target->second);
      if (!targetType || !isa<semantic::VoidType>(targetType.getValue()) ||
          targetChildren.size() != 1) {
        return emitError(getSemanticLocation(operand))
               << "rand join currently requires void single-item operand "
                  "productions";
      }
      streams.push_back(targetChildren.front());
    }

    // The bias is observable even when every active stream has the same
    // length, although equal lengths make it irrelevant to selection. Check
    // its required real [0.0, 1.0] domain before consuming any RNG state.
    if (rule.randJoinBias) {
      FailureOr<Value> lowered = lowerExpression(rule.randJoinBias);
      if (failed(lowered))
        return failure();
      FailureOr<Value> bias = convert(*lowered, builder.getF64Type(),
                                      isSignedNode(rule.randJoinBias),
                                      getSemanticLocation(rule.randJoinBias));
      if (failed(bias))
        return failure();
      Value zero = arith::ConstantOp::create(
          builder, getSemanticLocation(rule.randJoinBias),
          builder.getF64FloatAttr(0.0));
      Value one = arith::ConstantOp::create(
          builder, getSemanticLocation(rule.randJoinBias),
          builder.getF64FloatAttr(1.0));
      Value atLeastZero =
          arith::CmpFOp::create(builder, getSemanticLocation(rule.randJoinBias),
                                arith::CmpFPredicate::OGE, *bias, zero);
      Value atMostOne =
          arith::CmpFOp::create(builder, getSemanticLocation(rule.randJoinBias),
                                arith::CmpFPredicate::OLE, *bias, one);
      Value valid =
          arith::AndIOp::create(builder, getSemanticLocation(rule.randJoinBias),
                                atLeastZero, atMostOne);
      Block *accepted = addBlock();
      Block *invalid = addBlock();
      cf::CondBranchOp::create(builder, getSemanticLocation(rule.randJoinBias),
                               valid, accepted, invalid);
      setCurrent(invalid);
      if (failed(emitRuntimeFatal(
              getSemanticLocation(rule.randJoinBias),
              "rand join bias is outside the real range [0.0, 1.0]")))
        return failure();
      setCurrent(accepted);
    }

    Type i64 = builder.getI64Type();
    Type i1 = builder.getI1Type();
    Block *header = addBlock();
    for (size_t index = 0; index < streams.size(); ++index)
      header->addArgument(i1, location);
    header->addArgument(i64, location);
    Block *dispatch = addBlock();
    Block *exit = addBlock();
    SmallVector<Value> initial(
        streams.size(), arith::ConstantOp::create(builder, location, i1,
                                                  builder.getBoolAttr(true)));
    initial.push_back(arith::ConstantOp::create(
        builder, location, i64, builder.getI64IntegerAttr(streams.size())));
    cf::BranchOp::create(builder, location, header, initial);

    setCurrent(header);
    Value remaining = header->getArgument(streams.size());
    Value zero = arith::ConstantOp::create(builder, location, i64,
                                           builder.getI64IntegerAttr(0));
    Value more = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, remaining, zero);
    cf::CondBranchOp::create(builder, location, more, dispatch, exit);

    setCurrent(dispatch);
    Value one = arith::ConstantOp::create(builder, location, i64,
                                          builder.getI64IntegerAttr(1));
    Value last = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, remaining, one);
    Block *randomize = addBlock();
    Block *choose = addBlock();
    choose->addArgument(i64, location);
    cf::CondBranchOp::create(builder, location, last, choose, ValueRange{zero},
                             randomize, ValueRange{});

    setCurrent(randomize);
    Value context = function.getBody().front().getArgument(0);
    Value draw = sim::SimRandomBoundedOp::create(builder, location, i64,
                                                 context, remaining);
    cf::BranchOp::create(builder, location, choose, ValueRange{draw});

    setCurrent(choose);
    draw = choose->getArgument(0);
    Value rank = zero;
    Value nextRemaining =
        arith::SubIOp::create(builder, location, remaining, one);
    for (size_t index = 0; index < streams.size(); ++index) {
      Value active = header->getArgument(index);
      Value rankMatches = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, draw, rank);
      Value selected =
          arith::AndIOp::create(builder, location, active, rankMatches);
      Block *selectedBlock = addBlock();
      Block *nextStream = addBlock();
      cf::CondBranchOp::create(builder, location, selected, selectedBlock,
                               nextStream);
      setCurrent(selectedBlock);

      Block *streamReturn = addBlock();
      randSequenceProductionReturns.push_back(
          {streamReturn, controlScopes.size()});
      if (failed(lowerRandSequenceNode(streams[index]))) {
        randSequenceProductionReturns.pop_back();
        return failure();
      }
      emitBranch(streamReturn);
      setCurrent(streamReturn);
      randSequenceProductionReturns.pop_back();
      SmallVector<Value> nextActive;
      nextActive.reserve(streams.size() + 1);
      for (size_t operand = 0; operand < streams.size(); ++operand)
        nextActive.push_back(operand == index ? Value(arith::ConstantOp::create(
                                                    builder, location, i1,
                                                    builder.getBoolAttr(false)))
                                              : header->getArgument(operand));
      nextActive.push_back(nextRemaining);
      cf::BranchOp::create(builder, location, header, nextActive);

      setCurrent(nextStream);
      Value nextRank = arith::AddIOp::create(builder, location, rank, one);
      rank = arith::SelectOp::create(builder, location, active, nextRank, rank);
    }
    // The bounded draw and active-count invariant make this block
    // unreachable, but keep the CFG structurally total.
    cf::BranchOp::create(builder, location, exit);
    setCurrent(exit);
    return success();
  };
  auto lowerRule = [&](const Rule &rule) -> LogicalResult {
    // 18.17.7 allocates an implicit result variable per returning production
    // in this rule. Multiple syntactic occurrences use a [1:N] array; a
    // repeated occurrence keeps one slot and replaces its value each time.
    llvm::StringMap<SmallVector<semantic::SVProdItemOp>> returningItems;
    llvm::StringMap<Type> returnTypes;
    SmallVector<semantic::SVProdItemOp> items;
    for (Operation *item : rule.items)
      collectRandSequenceItems(item, items);
    for (semantic::SVProdItemOp item : items) {
      auto targetRef = item->getAttrOfType<SymbolRefAttr>("target");
      auto target = targetRef ? randSequenceContexts.back().productions.find(
                                    targetRef.getLeafReference())
                              : randSequenceContexts.back().productions.end();
      if (target == randSequenceContexts.back().productions.end())
        return emitError(location) << "unknown rule production";
      auto semanticType =
          target->second->getAttrOfType<TypeAttr>("semantic_type");
      if (!semanticType)
        return emitError(location) << "rule production has no return type";
      if (isa<semantic::VoidType>(semanticType.getValue()))
        continue;
      auto name = target->second->getAttrOfType<StringAttr>("production_name");
      StringRef productionName = name ? name.getValue() : target->getKey();
      FailureOr<Type> type =
          normalizeSemanticType(semanticType.getValue(), location, function);
      if (failed(type))
        return failure();
      returningItems[productionName].push_back(item);
      returnTypes[productionName] = *type;
    }

    SmallVector<SavedBinding> savedRuleBindings;
    llvm::scope_exit restoreRuleBindings([&] {
      for (const SavedBinding &saved : llvm::reverse(savedRuleBindings)) {
        if (saved.hadValue)
          values[saved.path] = saved.value;
        else
          values.erase(saved.path);
        if (saved.hadLValue)
          lvalues[saved.path] = saved.lvalue;
        else
          lvalues.erase(saved.path);
      }
    });
    llvm::DenseMap<Operation *, Value> resultReferences;
    llvm::StringSet<> boundNames;
    for (Attribute attribute : rule.variables) {
      auto variable = dyn_cast<DictionaryAttr>(attribute);
      auto name = variable ? variable.getAs<StringAttr>("name") : StringAttr{};
      auto path = variable ? variable.getAs<StringAttr>("referenced_path")
                           : StringAttr{};
      auto semanticType =
          variable ? variable.getAs<TypeAttr>("semantic_type") : TypeAttr{};
      if (!name || !path || !semanticType ||
          !returningItems.count(name.getValue()) ||
          !boundNames.insert(name.getValue()).second)
        return emitError(location)
               << "malformed rule return-variable inventory";
      FailureOr<Type> type =
          normalizeSemanticType(semanticType.getValue(), location, function);
      if (failed(type))
        return failure();
      auto &occurrences = returningItems[name.getValue()];
      Type returnType = returnTypes[name.getValue()];
      if (occurrences.size() == 1) {
        if (*type != returnType)
          return emitError(location) << "rule return variable has wrong type";
      } else {
        auto array = dyn_cast<sim::UnpackedArrayType>(*type);
        if (!array || array.getElementType() != returnType ||
            array.getLeft() != 1 ||
            array.getRight() != static_cast<int64_t>(occurrences.size()))
          return emitError(location) << "rule return array must use [1:N]";
      }
      Value undefined = createDefaultValue(builder, location, *type);
      if (!undefined)
        return emitError(location) << "unsupported rule return-variable type";
      // A recursive body keeps the variable in a slot of its frame, which a
      // reentering call saves; its value restarts at each rule execution.
      Value reference;
      if (body) {
        reference = allocateRandSequenceSlot(*type, location);
        if (!reference)
          return emitError(location) << "unsupported rule return-variable type";
        RandSequenceContext &context = randSequenceContexts.back();
        context.frames[context.currentFrame].slots.push_back(reference);
        sim::SimRefStoreOp::create(builder, location, undefined, reference);
      } else {
        reference = sim::SimRefAllocOp::create(
            builder, location, sim::RefType::get(function.getContext(), *type),
            undefined);
      }
      std::string bindingPath = path.getValue().str();
      savedRuleBindings.push_back(
          {bindingPath, values.lookup(bindingPath), lvalues.lookup(bindingPath),
           values.count(bindingPath) != 0, lvalues.count(bindingPath) != 0});
      values[bindingPath] = reference;
      lvalues[bindingPath] = reference;
      for (auto [index, item] : llvm::enumerate(occurrences)) {
        Value destination = reference;
        if (occurrences.size() > 1)
          destination = sim::SimRefSubelementOp::create(
              builder, location,
              sim::RefType::get(function.getContext(), returnType), reference,
              builder.getDenseI64ArrayAttr({static_cast<int64_t>(index)}));
        resultReferences[item] = destination;
      }
    }
    if (boundNames.size() != returningItems.size())
      return emitError(location) << "missing rule return-variable binding";
    randSequenceResultReferences.push_back(std::move(resultReferences));
    llvm::scope_exit popResultReferences(
        [&] { randSequenceResultReferences.pop_back(); });
    if (rule.randJoin) {
      if (failed(lowerRandJoin(rule)))
        return failure();
    } else {
      for (Operation *item : rule.items)
        if (failed(lowerRandSequenceNode(item)))
          return failure();
    }
    if (rule.weightCodeBlock &&
        failed(lowerRandSequenceNode(rule.weightCodeBlock)))
      return failure();
    emitBranch(productionReturn);
    return success();
  };
  // Unlike randcase's fixed-precision sum (18.16), production probabilities
  // in 18.17.1 are proportional to every nonnegative weight. Size the total
  // from the operand widths and number of alternatives before evaluating any
  // weight, so overflow cannot discard a legal production or fail generation.
  unsigned maximumWeightWidth = 1;
  for (const Rule &rule : rules) {
    if (!rule.weight)
      continue;
    FailureOr<Type> type = getNormalizedSemanticType(rule.weight);
    std::optional<unsigned> width =
        succeeded(type) ? sim::getPackedWidth(*type) : std::nullopt;
    if (!width || !*width)
      return emitError(getSemanticLocation(rule.weight))
             << "randsequence weights require integral values";
    maximumWeightWidth = std::max(maximumWeightWidth, *width);
  }
  unsigned carryWidth = llvm::Log2_64_Ceil(std::max<size_t>(1, rules.size()));
  if (maximumWeightWidth > IntegerType::kMaxWidth - carryWidth)
    return emitError(location) << "randsequence weight sum exceeds IR width";
  Type weightType =
      builder.getIntegerType(std::max(64u, maximumWeightWidth + carryWidth));
  Value zero;
  if (rules.size() > 1 || (rules.size() == 1 && rules.front().weight))
    zero = arith::ConstantOp::create(builder, location, weightType,
                                     builder.getIntegerAttr(weightType, 0));
  auto lowerExplicitWeight = [&](Operation *node,
                                 Value zero) -> FailureOr<Value> {
    Location weightLocation = getSemanticLocation(node);
    FailureOr<Value> lowered = lowerExpression(node);
    if (failed(lowered))
      return failure();
    // Packed four-state vectors are checked as one logic scalar.
    FailureOr<Value> scalar = toPackedScalar(*lowered, weightLocation);
    if (failed(scalar))
      return failure();
    if (isa<sim::LogicType>(scalar->getType())) {
      // An X/Z value is not a nonnegative integral weight. Check before
      // four-state-to-two-state conversion can turn unknown bits into zero.
      Value selfEqual = sim::SimLogicCompareOp::create(
          builder, weightLocation,
          sim::LogicType::get(function.getContext(), 1), sim::CompareKind::Eq,
          *scalar, *scalar);
      Value known = sim::SimLogicIsTrueOp::create(
          builder, weightLocation, builder.getI1Type(), selfEqual);
      Block *valid = addBlock();
      Block *invalid = addBlock();
      cf::CondBranchOp::create(builder, weightLocation, known, valid, invalid);
      setCurrent(invalid);
      if (failed(emitRuntimeFatal(weightLocation,
                                  "randsequence weight contains X or Z bits")))
        return failure();
      setCurrent(valid);
    }
    FailureOr<Value> converted =
        convert(*lowered, weightType, isSignedNode(node), weightLocation);
    if (failed(converted))
      return failure();
    if (isSignedNode(node)) {
      Value negative =
          arith::CmpIOp::create(builder, getSemanticLocation(node),
                                arith::CmpIPredicate::slt, *converted, zero);
      Block *invalid = addBlock();
      Block *valid = addBlock();
      cf::CondBranchOp::create(builder, getSemanticLocation(node), negative,
                               invalid, valid);
      setCurrent(invalid);
      if (failed(emitRuntimeFatal(
              getSemanticLocation(node),
              "randsequence weight evaluated to a negative value")))
        return failure();
      setCurrent(valid);
    }
    return *converted;
  };

  if (rules.empty()) {
    emitBranch(productionReturn);
  } else if (rules.size() == 1) {
    if (rules.front().weight) {
      if (failed(lowerExplicitWeight(rules.front().weight, zero)))
        return failure();
    }
    if (failed(lowerRule(rules.front())))
      return failure();
  } else {
    Value one = arith::ConstantOp::create(
        builder, location, weightType, builder.getIntegerAttr(weightType, 1));
    SmallVector<Value> bounds;
    Value total = zero;
    for (const Rule &rule : rules) {
      Value weight = one;
      if (rule.weight) {
        FailureOr<Value> loweredWeight = lowerExplicitWeight(rule.weight, zero);
        if (failed(loweredWeight))
          return failure();
        weight = *loweredWeight;
      }
      Value next = arith::AddIOp::create(builder, location, total, weight);
      total = next;
      bounds.push_back(total);
    }
    Block *select = addBlock();
    Value any = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::ne, total, zero);
    cf::CondBranchOp::create(builder, location, any, select, productionReturn);
    setCurrent(select);
    Value draw = emitRandomBounded(total, location);
    for (size_t index = 0; index + 1 < rules.size(); ++index) {
      Block *selected = addBlock();
      Block *nextRule = addBlock();
      Value choose = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, draw, bounds[index]);
      cf::CondBranchOp::create(builder, location, choose, selected, nextRule);
      setCurrent(selected);
      if (failed(lowerRule(rules[index])))
        return failure();
      setCurrent(nextRule);
    }
    if (failed(lowerRule(rules.back())))
      return failure();
  }
  // Every path of a recursive body has branched to its exit.
  if (body)
    return success();
  setCurrent(productionReturn);
  return copyRandSequenceOutputs(randSequenceProductionReturns.back(),
                                 location);
}

LogicalResult
UnitLowering::lowerRandSequenceProductionItem(semantic::SVProdItemOp item) {
  if (randSequenceContexts.empty())
    return emitError(getSemanticLocation(item))
           << "randsequence production item has no active grammar";
  auto target = item->getAttrOfType<SymbolRefAttr>("target");
  if (!target)
    return emitError(getSemanticLocation(item))
           << "randsequence production item has no target";
  auto found =
      randSequenceContexts.back().productions.find(target.getLeafReference());
  if (found == randSequenceContexts.back().productions.end())
    return emitError(getSemanticLocation(item))
           << "randsequence cannot resolve production " << target;
  SmallVector<Operation *> actuals = getChildren(item);
  auto argumentCount = item->getAttrOfType<IntegerAttr>("argument_count");
  if (!argumentCount || argumentCount.getInt() < 0 ||
      actuals.size() != static_cast<uint64_t>(argumentCount.getInt()))
    return emitError(getSemanticLocation(item))
           << "malformed randsequence actual argument inventory";
  Value resultReference =
      randSequenceResultReferences.empty()
          ? Value{}
          : randSequenceResultReferences.back().lookup(item);
  auto type = found->second->getAttrOfType<TypeAttr>("semantic_type");
  if (type && !isa<semantic::VoidType>(type.getValue()) && !resultReference)
    return emitError(getSemanticLocation(item))
           << "returning production item has no rule variable";
  return lowerRandSequenceProduction(found->second, actuals, resultReference);
}

Value UnitLowering::allocateRandSequenceSlot(Type type, Location location) {
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPoint(
      randSequenceContexts.back().slotBlock->getTerminator());
  Value initial = createDefaultValue(builder, location, type);
  if (!initial)
    return {};
  return sim::SimRefAllocOp::create(
      builder, location, sim::RefType::get(builder.getContext(), type),
      initial);
}

Value UnitLowering::createRandSequenceStack(Type element, Location location) {
  FailureOr<ContainerElementDescriptor> descriptor =
      describeContainerElement(element, location);
  if (failed(descriptor))
    return {};
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPoint(
      randSequenceContexts.back().slotBlock->getTerminator());
  Value zero = arith::ConstantOp::create(
      builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
  return sim::SimContainerCreateOp::create(
      builder, location, sim::QueueType::get(builder.getContext(), element, 0),
      zero, descriptor->typeID, descriptor->kind, descriptor->flags,
      descriptor->valueSize, descriptor->alignment, descriptor->bitWidth,
      builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
      builder.getDenseI32ArrayAttr(descriptor->traceKinds),
      sim::ContainerKind::Queue, UINT64_MAX);
}

FailureOr<UnitLowering::RecursiveProductionBody *>
UnitLowering::getRecursiveProductionBody(
    semantic::SVFrozenRandSeqProductionOp production, Location location) {
  RandSequenceContext &context = randSequenceContexts.back();
  StringRef identity = production.getReferencedSymbol().getLeafReference();
  auto [found, inserted] = context.recursiveBodies.try_emplace(identity);
  RecursiveProductionBody &body = found->second;
  if (!inserted)
    return &body;
  body.production = production;
  body.entry = addBlock();
  body.exit = addBlock();
  body.frame = context.frames.size();
  context.frames.push_back({context.recursiveComponents.lookup(identity), {}});
  for (Attribute attribute : production.getFormalArguments()) {
    auto formal = dyn_cast<DictionaryAttr>(attribute);
    auto direction =
        formal ? formal.getAs<IntegerAttr>("direction") : IntegerAttr{};
    auto semanticType =
        formal ? formal.getAs<TypeAttr>("semantic_type") : TypeAttr{};
    if (!direction || !semanticType)
      return emitError(location)
                 << "randsequence requires typed formal arguments",
             failure();
    // A ref formal aliases a different actual in each activation, which a
    // slot of the single body cannot name.
    if (direction.getInt() ==
        static_cast<int64_t>(semantic::SVArgumentDirection::Ref))
      return emitError(location) << "a recursive randsequence production "
                                    "cannot have a ref argument",
             failure();
    FailureOr<Type> type =
        normalizeSemanticType(semanticType.getValue(), location, function);
    if (failed(type))
      return failure();
    Value slot = allocateRandSequenceSlot(*type, location);
    if (!slot)
      return emitError(location)
                 << "unsupported recursive randsequence argument type "
                 << *type,
             failure();
    body.formalSlots.push_back(slot);
  }
  auto productionType = production->getAttrOfType<TypeAttr>("semantic_type");
  if (productionType && !isa<semantic::VoidType>(productionType.getValue())) {
    FailureOr<Type> type =
        normalizeSemanticType(productionType.getValue(), location, function);
    if (failed(type))
      return failure();
    body.resultSlot = allocateRandSequenceSlot(*type, location);
    if (!body.resultSlot)
      return emitError(location)
                 << "unsupported recursive randsequence return type " << *type,
             failure();
  }
  SmallVector<Value> &slots = context.frames[body.frame].slots;
  llvm::append_range(slots, body.formalSlots);
  if (body.resultSlot)
    slots.push_back(body.resultSlot);
  context.pendingBodies.push_back(identity.str());
  return &body;
}

LogicalResult UnitLowering::lowerRecursiveProductionCall(
    semantic::SVFrozenRandSeqProductionOp production,
    ArrayRef<Operation *> actuals, Value resultReference) {
  Location location = getSemanticLocation(production);
  FailureOr<RecursiveProductionBody *> found =
      getRecursiveProductionBody(production, location);
  if (failed(found))
    return failure();
  RecursiveProductionBody *body = *found;
  ArrayAttr formals = production.getFormalArguments();
  SmallVector<Operation *> children = getChildren(production);
  if (formals.size() != production.getArgumentCount() ||
      actuals.size() > formals.size())
    return emitError(location) << "malformed randsequence formal inventory";
  SmallVector<Operation *> defaultExpressions(formals.size(), nullptr);
  size_t nextChild = 0;
  for (auto [index, attribute] : llvm::enumerate(formals)) {
    auto defaultCount = cast<DictionaryAttr>(attribute).getAs<IntegerAttr>(
        "default_operand_count");
    if (!defaultCount || defaultCount.getInt() < 0 ||
        defaultCount.getInt() > 1 ||
        nextChild + defaultCount.getInt() > children.size())
      return emitError(location)
             << "malformed randsequence formal default inventory";
    if (defaultCount.getInt() == 1)
      defaultExpressions[index] = children[nextChild++];
  }

  // Evaluate the actuals in the caller's activation. Output actuals are
  // written when the call returns.
  struct Output {
    Operation *actual;
    unsigned formal;
    bool formalSigned;
  };
  SmallVector<Value> initial;
  SmallVector<Output> outputs;
  for (auto [index, attribute] : llvm::enumerate(formals)) {
    auto formal = cast<DictionaryAttr>(attribute);
    auto direction = formal.getAs<IntegerAttr>("direction");
    auto semanticType = formal.getAs<TypeAttr>("semantic_type");
    Type type = getReferenceElementType(body->formalSlots[index]);
    Operation *actual = index < actuals.size() ? actuals[index] : nullptr;
    if (!actual || isa<semantic::SVEmptyArgumentExpressionOp>(actual))
      actual = defaultExpressions[index];
    if (!actual)
      return emitError(location) << "randsequence production argument " << index
                                 << " has no actual or default value";
    bool input = direction.getInt() ==
                 static_cast<int64_t>(semantic::SVArgumentDirection::In);
    bool output = direction.getInt() ==
                  static_cast<int64_t>(semantic::SVArgumentDirection::Out);
    if (!input && isa<semantic::SVAssignmentExpressionOp>(actual)) {
      SmallVector<Operation *> assignment = getChildren(actual);
      if (assignment.size() == 2 &&
          isa<semantic::SVEmptyArgumentExpressionOp>(assignment.back()))
        actual = assignment.front();
    }
    if (!input)
      outputs.push_back({actual, static_cast<unsigned>(index),
                         isSignedSemanticType(semanticType.getValue())});
    if (output) {
      initial.push_back(createDefaultValue(builder, location, type));
      continue;
    }
    FailureOr<Value> value = lowerExpression(actual);
    if (failed(value))
      return failure();
    FailureOr<Value> converted =
        convert(*value, type, isSignedNode(actual), location);
    if (failed(converted))
      return failure();
    initial.push_back(cloneSequentialValue(*converted, location));
  }

  // Save the caller's frame if this call can reenter it, then store the
  // actuals and enter the body.
  RandSequenceContext &context = randSequenceContexts.back();
  unsigned callerFrame = context.currentFrame;
  unsigned component = context.recursiveComponents.lookup(
      production.getReferencedSymbol().getLeafReference());
  Block *save = addBlock();
  cf::BranchOp::create(builder, location, save);
  setCurrent(save);
  Block *call = addBlock();
  cf::BranchOp::create(builder, location, call);
  setCurrent(call);
  for (auto [slot, value] : llvm::zip_equal(body->formalSlots, initial))
    sim::SimRefStoreOp::create(builder, location, value, slot);
  Type i64 = builder.getI64Type();
  Value resumeIndex = arith::ConstantOp::create(
      builder, location, i64, builder.getI64IntegerAttr(body->resumes.size()));
  Value depth = sim::SimContainerSizeOp::create(builder, location, i64,
                                                context.returnStack);
  sim::SimQueueInsertOp::create(builder, location, context.returnStack, depth,
                                resumeIndex);
  cf::BranchOp::create(builder, location, body->entry);

  // Read the callee's results before restoring the caller's frame, which
  // may share the callee's slots.
  Block *resume = addBlock();
  body->resumes.push_back(resume);
  setCurrent(resume);
  Value result;
  if (body->resultSlot && resultReference)
    result = sim::SimRefLoadOp::create(
        builder, location, getReferenceElementType(body->resultSlot),
        body->resultSlot);
  SmallVector<Value> outputValues;
  for (const Output &output : outputs) {
    Value slot = body->formalSlots[output.formal];
    outputValues.push_back(sim::SimRefLoadOp::create(
        builder, location, getReferenceElementType(slot), slot));
  }
  Block *restore = addBlock();
  cf::BranchOp::create(builder, location, restore);
  setCurrent(restore);
  Block *after = addBlock();
  cf::BranchOp::create(builder, location, after);
  setCurrent(after);
  context.recursiveCalls.push_back(
      {callerFrame, component, save, resume, restore});
  if (result && failed(storeReference(resultReference, result, location)))
    return failure();
  // Output actuals are addressed when the call returns.
  for (auto [output, value] : llvm::zip_equal(outputs, outputValues)) {
    FailureOr<CapturedLValue> destination =
        captureLValue(output.actual, location);
    if (failed(destination))
      return failure();
    bool destinationSigned = isSignedNode(output.actual);
    FailureOr<Value> converted =
        convert(value, destination->type, output.formalSigned, location,
                destinationSigned);
    if (failed(converted) ||
        failed(writeCapturedLValue(*destination, *converted, destinationSigned,
                                   false, location)))
      return failure();
  }
  return success();
}

LogicalResult UnitLowering::repairRecursiveRandSequence(Block *first,
                                                        Location location) {
  RandSequenceContext &context = randSequenceContexts.back();
  SmallVector<Block *> blocks;
  llvm::DenseMap<Block *, unsigned> blockOrder;
  for (Block &block :
       llvm::make_range(first->getIterator(), function.getBody().end())) {
    blockOrder[&block] = blocks.size();
    blocks.push_back(&block);
  }
  auto frameOf = [&](Block *block) -> std::optional<unsigned> {
    if (!blockOrder.count(block))
      return std::nullopt;
    return context.blockFrames.lookup(block);
  };

  // A recursive call clobbers every value of a body it can reenter. Protect
  // the values live into a resume block of such a call, and every use the
  // return edges leave undominated.
  Liveness liveness(function);
  DominanceInfo dominance(function);
  llvm::SetVector<Value> protect;
  for (const auto &call : context.recursiveCalls) {
    if (call.frame == 0 ||
        context.frames[call.frame].component != call.component)
      continue;
    for (Value value : liveness.getLiveIn(call.resume)) {
      std::optional<unsigned> frame = frameOf(value.getParentBlock());
      if (frame && *frame && context.frames[*frame].component == call.component)
        protect.insert(value);
    }
  }
  for (Block *block : blocks)
    block->walk([&](Operation *operation) {
      for (Value operand : operation->getOperands())
        if (frameOf(operand.getParentBlock()) &&
            !dominance.properlyDominates(operand, operation))
          protect.insert(operand);
    });
  // Allocate slots in program order.
  llvm::DenseMap<Value, std::pair<unsigned, unsigned>> positions;
  for (auto [blockIndex, block] : llvm::enumerate(blocks)) {
    unsigned offset = 0;
    for (BlockArgument argument : block->getArguments())
      positions[argument] = {blockIndex, offset++};
    for (Operation &operation : *block)
      for (Value result : operation.getResults())
        positions[result] = {blockIndex, offset++};
  }
  SmallVector<Value> ordered(protect.begin(), protect.end());
  llvm::sort(ordered, [&](Value left, Value right) {
    return positions.lookup(left) < positions.lookup(right);
  });

  // A pure computation over values no call changes is recomputed at each
  // use. Anything else moves to a slot of its frame.
  std::function<bool(Value)> invariant = [&](Value value) {
    Block *block = value.getParentBlock();
    if (!frameOf(block) || block == context.slotBlock)
      return true;
    Operation *definition = value.getDefiningOp();
    return definition && definition->getNumRegions() == 0 &&
           isMemoryEffectFree(definition) &&
           llvm::all_of(definition->getOperands(), invariant);
  };
  std::function<Value(Value, Operation *)> rematerialize =
      [&](Value value, Operation *user) -> Value {
    Block *block = value.getParentBlock();
    if (!frameOf(block) || block == context.slotBlock)
      return value;
    Operation *definition = value.getDefiningOp();
    IRMapping mapping;
    for (Value operand : definition->getOperands())
      mapping.map(operand, rematerialize(operand, user));
    OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPoint(user);
    Operation *clone = builder.clone(*definition, mapping);
    return clone->getResult(cast<OpResult>(value).getResultNumber());
  };
  for (Value value : ordered) {
    if (invariant(value)) {
      for (OpOperand &use : llvm::make_early_inc_range(value.getUses()))
        use.set(rematerialize(value, use.getOwner()));
      continue;
    }
    Type type = value.getType();
    Value slot;
    if (!isa<sim::RefType, sim::ManagedRefType, sim::ArgumentRefType,
             sim::ReferencePathType, sim::NetType, sim::DriverType>(type))
      slot = allocateRandSequenceSlot(type, location);
    if (!slot)
      return emitError(location)
             << "a recursive randsequence production keeps a value of type "
             << type << " across a production call";
    context.frames[*frameOf(value.getParentBlock())].slots.push_back(slot);
    OpBuilder::InsertionGuard guard(builder);
    if (auto argument = dyn_cast<BlockArgument>(value))
      builder.setInsertionPointToStart(argument.getOwner());
    else
      builder.setInsertionPointAfter(value.getDefiningOp());
    Operation *store =
        sim::SimRefStoreOp::create(builder, location, value, slot);
    for (OpOperand &use : llvm::make_early_inc_range(value.getUses())) {
      if (use.getOwner() == store)
        continue;
      builder.setInsertionPoint(use.getOwner());
      use.set(sim::SimRefLoadOp::create(builder, location, type, slot));
    }
  }

  // Each call that can reenter its caller's body saves that frame's slots
  // and restores them when it returns.
  Type i64 = builder.getI64Type();
  llvm::DenseMap<Value, Value> stacks;
  for (const auto &call : context.recursiveCalls) {
    if (call.frame == 0 ||
        context.frames[call.frame].component != call.component)
      continue;
    ArrayRef<Value> slots = context.frames[call.frame].slots;
    for (Value slot : slots) {
      Value &stack = stacks[slot];
      if (!stack)
        stack =
            createRandSequenceStack(getReferenceElementType(slot), location);
      if (!stack)
        return failure();
    }
    OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPoint(call.save->getTerminator());
    for (Value slot : slots) {
      Value stack = stacks.lookup(slot);
      Value saved = sim::SimRefLoadOp::create(
          builder, location, getReferenceElementType(slot), slot);
      Value depth =
          sim::SimContainerSizeOp::create(builder, location, i64, stack);
      sim::SimQueueInsertOp::create(builder, location, stack, depth, saved);
    }
    builder.setInsertionPoint(call.restore->getTerminator());
    for (Value slot : llvm::reverse(slots)) {
      Value stack = stacks.lookup(slot);
      Value depth =
          sim::SimContainerSizeOp::create(builder, location, i64, stack);
      Value one = arith::ConstantOp::create(builder, location, i64,
                                            builder.getI64IntegerAttr(1));
      Value top = arith::SubIOp::create(builder, location, depth, one);
      Value saved = sim::SimContainerReadOp::create(
          builder, location, getReferenceElementType(slot), stack, top);
      sim::SimQueueDeleteOp::create(builder, location, stack, top);
      sim::SimRefStoreOp::create(builder, location, saved, slot);
    }
  }
  return success();
}

LogicalResult UnitLowering::lowerRandSequenceNode(Operation *node) {
  Location location = getSemanticLocation(node);
  if (auto item = dyn_cast<semantic::SVProdItemOp>(node))
    return lowerRandSequenceProductionItem(item);
  if (auto code = dyn_cast<semantic::SVCodeBlockProdOp>(node)) {
    SmallVector<Operation *> children = getChildren(code);
    if (children.size() != 1)
      return emitError(location) << "malformed randsequence code block";
    return lowerStatement(children.front());
  }
  if (auto conditional = dyn_cast<semantic::SVIfElseProdOp>(node)) {
    SmallVector<Operation *> children = getChildren(conditional);
    auto hasElse = conditional->getAttrOfType<BoolAttr>("has_else");
    if (!hasElse || children.size() != 2u + unsigned(hasElse.getValue()))
      return emitError(location) << "malformed randsequence if production";
    if (!isa<semantic::SVProdItemOp>(children[1]) ||
        (hasElse.getValue() && !isa<semantic::SVProdItemOp>(children[2])))
      return emitError(location) << "malformed randsequence if production";
    FailureOr<Value> condition = lowerExpression(children.front());
    if (failed(condition))
      return failure();
    FailureOr<Value> predicate = truthValue(*condition, location);
    if (failed(predicate))
      return failure();
    Block *ifBlock = addBlock();
    Block *elseBlock = addBlock();
    Block *merge = addBlock();
    cf::CondBranchOp::create(builder, location, *predicate, ifBlock, elseBlock);
    setCurrent(ifBlock);
    if (failed(lowerRandSequenceProductionItem(
            cast<semantic::SVProdItemOp>(children[1]))))
      return failure();
    emitBranch(merge);
    setCurrent(elseBlock);
    if (hasElse.getValue() && failed(lowerRandSequenceProductionItem(
                                  cast<semantic::SVProdItemOp>(children[2]))))
      return failure();
    emitBranch(merge);
    setCurrent(merge);
    return success();
  }
  if (auto repeat = dyn_cast<semantic::SVRepeatProdOp>(node)) {
    SmallVector<Operation *> children = getChildren(repeat);
    if (children.size() != 2 || !isa<semantic::SVProdItemOp>(children[1]))
      return emitError(location) << "malformed randsequence repeat production";
    FailureOr<Type> sourceType = getNormalizedSemanticType(children.front());
    std::optional<unsigned> width =
        succeeded(sourceType) ? sim::getPackedWidth(*sourceType) : std::nullopt;
    if (!width || *width > 64)
      return emitError(location)
             << "randsequence repeat counts wider than 64 bits are outside "
                "the executable boundary";
    FailureOr<Value> lowered = lowerExpression(children.front());
    if (failed(lowered))
      return failure();
    Type i64 = builder.getI64Type();
    FailureOr<Value> count =
        convert(*lowered, i64, isSignedNode(children.front()), location);
    if (failed(count))
      return failure();
    Value zero = arith::ConstantOp::create(builder, location, i64,
                                           builder.getI64IntegerAttr(0));
    if (isSignedNode(children.front())) {
      Value negative = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::slt, *count, zero);
      Block *invalid = addBlock();
      Block *valid = addBlock();
      cf::CondBranchOp::create(builder, location, negative, invalid, valid);
      setCurrent(invalid);
      if (failed(emitRuntimeFatal(
              location,
              "randsequence repeat count evaluated to a negative value")))
        return failure();
      setCurrent(valid);
    }
    Block *header = addBlock();
    header->addArgument(i64, location);
    Block *body = addBlock();
    Block *exit = addBlock();
    cf::BranchOp::create(builder, location, header, ValueRange{zero});
    setCurrent(header);
    Value index = header->getArgument(0);
    Value more = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, index, *count);
    cf::CondBranchOp::create(builder, location, more, body, exit);
    setCurrent(body);
    if (failed(lowerRandSequenceProductionItem(
            cast<semantic::SVProdItemOp>(children[1]))))
      return failure();
    Value one = arith::ConstantOp::create(builder, location, i64,
                                          builder.getI64IntegerAttr(1));
    Value next = arith::AddIOp::create(builder, location, index, one);
    cf::BranchOp::create(builder, location, header, ValueRange{next});
    setCurrent(exit);
    return success();
  }
  if (auto caseProduction = dyn_cast<semantic::SVCaseProdOp>(node)) {
    SmallVector<Operation *> children = getChildren(caseProduction);
    auto expressionCountsAttr =
        caseProduction->getAttrOfType<DenseI64ArrayAttr>(
            "item_expression_counts");
    auto itemCount = caseProduction->getAttrOfType<IntegerAttr>("item_count");
    auto hasDefault = caseProduction->getAttrOfType<BoolAttr>("has_default");
    ArrayRef<int64_t> expressionCounts = expressionCountsAttr
                                             ? expressionCountsAttr.asArrayRef()
                                             : ArrayRef<int64_t>{};
    if (!expressionCountsAttr || !itemCount || itemCount.getInt() < 0 ||
        !hasDefault ||
        expressionCounts.size() != static_cast<uint64_t>(itemCount.getInt()) ||
        children.empty())
      return emitError(location) << "malformed randsequence case production";
    FailureOr<Value> selector = lowerExpression(children.front());
    if (failed(selector))
      return failure();
    FailureOr<Type> selectorType = getNormalizedSemanticType(children.front());
    if (failed(selectorType))
      return failure();
    Operation *selectorNode = children.front();
    size_t next = 1;
    Block *merge = addBlock();
    for (int64_t expressionCount : expressionCounts) {
      if (expressionCount <= 0 || next + expressionCount >= children.size())
        return emitError(location)
               << "malformed randsequence case item inventory";
      SmallVector<Operation *> labels;
      for (int64_t index = 0; index < expressionCount; ++index)
        labels.push_back(children[next++]);
      auto item = dyn_cast<semantic::SVProdItemOp>(children[next++]);
      if (!item)
        return emitError(location)
               << "randsequence case item has no production";
      for (Operation *label : labels) {
        FailureOr<Value> matches =
            lowerCaseLabel(*selector, *selectorType, selectorNode, label,
                           semantic::SVCaseCondition::Normal);
        if (failed(matches))
          return failure();
        Block *selected = addBlock();
        Block *nextLabel = addBlock();
        cf::CondBranchOp::create(builder, getSemanticLocation(label), *matches,
                                 selected, nextLabel);
        setCurrent(selected);
        if (failed(lowerRandSequenceProductionItem(item)))
          return failure();
        emitBranch(merge);
        setCurrent(nextLabel);
      }
    }
    if (hasDefault.getValue()) {
      if (next == children.size() ||
          !isa<semantic::SVProdItemOp>(children[next]))
        return emitError(location)
               << "randsequence case default has no production";
      if (failed(lowerRandSequenceProductionItem(
              cast<semantic::SVProdItemOp>(children[next++]))))
        return failure();
    }
    if (next != children.size())
      return emitError(location)
             << "excess randsequence case production operands";
    emitBranch(merge);
    setCurrent(merge);
    return success();
  }
  return emitError(location)
         << "unsupported randsequence production node " << node->getName();
}

LogicalResult
UnitLowering::lowerPatternCase(semantic::SVPatternCaseStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  size_t itemCount = op.getItemCount();
  ArrayRef<int64_t> filterFlags = op.getItemFilterFlags();
  if (filterFlags.size() != itemCount || children.empty()) {
    emitError(location) << "malformed pattern case inventory";
    return failure();
  }
  size_t filterCount = 0;
  for (int64_t flag : filterFlags) {
    if (flag != 0 && flag != 1) {
      emitError(location) << "invalid pattern case filter inventory";
      return failure();
    }
    filterCount += static_cast<size_t>(flag);
  }
  size_t expressionCount = 1 + itemCount + filterCount;
  size_t statementCount = itemCount + op.getHasDefault();
  if (children.size() != expressionCount + statementCount) {
    emitError(location) << "malformed pattern case item inventory";
    return failure();
  }
  FailureOr<Value> selector = lowerExpression(children.front());
  if (failed(selector))
    return failure();
  SmallVector<Operation *> patterns;
  SmallVector<Operation *> filters(itemCount, nullptr);
  size_t next = 1;
  for (size_t item = 0; item < itemCount; ++item) {
    patterns.push_back(children[next++]);
    if (filterFlags[item])
      filters[item] = children[next++];
  }
  ArrayRef<Operation *> statements =
      ArrayRef<Operation *>(children).take_back(statementCount);
  Block *mergeBlock = addBlock();
  bool inspectAll =
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique ||
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique0;

  if (!inspectAll) {
    for (size_t item = 0; item < itemCount; ++item) {
      llvm::StringMap<Value> captures;
      FailureOr<Value> patternMatched = lowerPattern(
          *selector, patterns[item], op.getConditionKind(), &captures);
      if (failed(patternMatched))
        return failure();
      Block *bodyBlock = addBlock();
      Block *nextItem = addBlock();
      if (Operation *filter = filters[item]) {
        Block *filterBlock = addBlock();
        cf::CondBranchOp::create(builder, getSemanticLocation(patterns[item]),
                                 *patternMatched, filterBlock, ValueRange{},
                                 nextItem, ValueRange{});
        setCurrent(filterBlock);
        FailureOr<Value> filtered = lowerExpression(filter);
        if (failed(filtered))
          return failure();
        FailureOr<Value> filterTruth =
            truthValue(*filtered, getSemanticLocation(filter));
        if (failed(filterTruth))
          return failure();
        cf::CondBranchOp::create(builder, getSemanticLocation(filter),
                                 *filterTruth, bodyBlock, ValueRange{},
                                 nextItem, ValueRange{});
      } else {
        cf::CondBranchOp::create(builder, getSemanticLocation(patterns[item]),
                                 *patternMatched, bodyBlock, ValueRange{},
                                 nextItem, ValueRange{});
      }
      setCurrent(bodyBlock);
      for (auto &capture : captures)
        values[capture.getKey()] = capture.getValue();
      if (failed(lowerStatement(statements[item])))
        return failure();
      emitBranch(mergeBlock);
      setCurrent(nextItem);
    }
    if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Priority &&
        !op.getHasDefault())
      emitQualifierWarning(op.getOperation(), location, op.getCheckKind(),
                           "case", "no match");
    if (op.getHasDefault() && failed(lowerStatement(statements.back())))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(mergeBlock);
    return success();
  }

  auto i32 = builder.getI32Type();
  Value matchCount = arith::ConstantOp::create(builder, location, i32,
                                               builder.getI32IntegerAttr(0));
  Value selected = arith::ConstantOp::create(builder, location, i32,
                                             builder.getI32IntegerAttr(-1));
  SmallVector<llvm::StringMap<Value>> itemCaptures(itemCount);
  for (size_t item = 0; item < itemCount; ++item) {
    FailureOr<Value> patternMatched = lowerPattern(
        *selector, patterns[item], op.getConditionKind(), &itemCaptures[item]);
    if (failed(patternMatched))
      return failure();
    Block *groupDone = addBlock();
    groupDone->addArgument(builder.getI1Type(), location);
    if (Operation *filter = filters[item]) {
      Block *filterBlock = addBlock();
      Value falseValue = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(false));
      cf::CondBranchOp::create(builder, getSemanticLocation(patterns[item]),
                               *patternMatched, filterBlock, ValueRange{},
                               groupDone, ValueRange{falseValue});
      setCurrent(filterBlock);
      FailureOr<Value> filtered = lowerExpression(filter);
      if (failed(filtered))
        return failure();
      FailureOr<Value> filterTruth =
          truthValue(*filtered, getSemanticLocation(filter));
      if (failed(filterTruth))
        return failure();
      cf::BranchOp::create(builder, getSemanticLocation(filter), groupDone,
                           ValueRange{*filterTruth});
    } else {
      cf::BranchOp::create(builder, getSemanticLocation(patterns[item]),
                           groupDone, ValueRange{*patternMatched});
    }
    setCurrent(groupDone);
    Value groupMatched = groupDone->getArgument(0);
    Value one = arith::ConstantOp::create(builder, location, i32,
                                          builder.getI32IntegerAttr(1));
    Value incremented =
        arith::AddIOp::create(builder, location, matchCount, one);
    matchCount = arith::SelectOp::create(builder, location, groupMatched,
                                         incremented, matchCount);
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noSelection = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::slt, selected, zero);
    Value selectThis =
        arith::AndIOp::create(builder, location, groupMatched, noSelection);
    Value itemIndex = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(item));
    selected = arith::SelectOp::create(builder, location, selectThis, itemIndex,
                                       selected);
    if (item + 1 != itemCount) {
      Block *nextGroup = addBlock();
      nextGroup->addArgument(i32, location);
      nextGroup->addArgument(i32, location);
      cf::BranchOp::create(builder, location, nextGroup,
                           ValueRange{matchCount, selected});
      setCurrent(nextGroup);
      matchCount = nextGroup->getArgument(0);
      selected = nextGroup->getArgument(1);
    }
  }
  Value one = arith::ConstantOp::create(builder, location, i32,
                                        builder.getI32IntegerAttr(1));
  Value overlap = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, matchCount, one);
  Block *overlapWarning = addBlock();
  Block *checkNoMatch = addBlock();
  Block *dispatch = addBlock();
  cf::CondBranchOp::create(builder, location, overlap, overlapWarning,
                           ValueRange{}, checkNoMatch, ValueRange{});
  setCurrent(overlapWarning);
  emitQualifierWarning(op.getOperation(), location, op.getCheckKind(), "case",
                       "multiple matches");
  emitBranch(dispatch);
  setCurrent(checkNoMatch);
  if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique &&
      !op.getHasDefault()) {
    Value zero = arith::ConstantOp::create(builder, location, i32,
                                           builder.getI32IntegerAttr(0));
    Value noMatch = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, matchCount, zero);
    Block *noMatchWarning = addBlock();
    cf::CondBranchOp::create(builder, location, noMatch, noMatchWarning,
                             ValueRange{}, dispatch, ValueRange{});
    setCurrent(noMatchWarning);
    emitQualifierWarning(op.getOperation(), location, op.getCheckKind(), "case",
                         "no match");
    emitBranch(dispatch);
  } else {
    emitBranch(dispatch);
  }
  setCurrent(dispatch);
  for (size_t item = 0; item < itemCount; ++item) {
    Value itemIndex = arith::ConstantOp::create(
        builder, location, i32, builder.getI32IntegerAttr(item));
    Value isSelected = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, selected, itemIndex);
    Block *itemBlock = addBlock();
    Block *nextDispatch = addBlock();
    cf::CondBranchOp::create(builder, location, isSelected, itemBlock,
                             ValueRange{}, nextDispatch, ValueRange{});
    setCurrent(itemBlock);
    for (auto &capture : itemCaptures[item])
      values[capture.getKey()] = capture.getValue();
    if (failed(lowerStatement(statements[item])))
      return failure();
    emitBranch(mergeBlock);
    setCurrent(nextDispatch);
  }
  if (op.getHasDefault() && failed(lowerStatement(statements.back())))
    return failure();
  emitBranch(mergeBlock);
  setCurrent(mergeBlock);
  return success();
}

} // namespace obelisk::simlowering
