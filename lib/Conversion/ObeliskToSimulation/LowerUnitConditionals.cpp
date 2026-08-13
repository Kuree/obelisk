//===- LowerUnitConditionals.cpp - Lower assertions and conditionals ---===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringMap.h"

#include <limits>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

void UnitLowering::emitDefaultAssertionFailure(Location location,
                                               StringRef description) {
  std::string file = "<unknown>";
  unsigned line = 0;
  if (auto source = location->findInstanceOf<FileLineColLoc>()) {
    file = source.getFilename().str();
    line = source.getLine();
  }
  std::string message = (Twine("ERROR: ") + file + ":" + Twine(line) + ": " +
                         description + " failed.")
                            .str();
  for (size_t position = 0;
       (position = message.find('%', position)) != std::string::npos;
       position += 2)
    message.insert(position, 1, '%');

  Value context = function.getBody().front().getArgument(0);
  Value descriptor = arith::ConstantOp::create(
      builder, location, builder.getI32Type(),
      builder.getI32IntegerAttr(static_cast<int32_t>(0x80000002u)));
  Value item =
      sim::SimBytesConstantOp::create(builder, location, message).getResult();
  auto timeMultiplier =
      function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
  StringAttr scope =
      function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
  sim::SimDisplayOp::create(builder, location, context, descriptor,
                            ValueRange{item}, true, 10,
                            builder.getDenseI32ArrayAttr({0}), scope,
                            StringAttr{}, timeMultiplier, IntegerAttr{});
}

LogicalResult UnitLowering::lowerImmediateAssertion(
    semantic::SVImmediateAssertionStatementOp op) {
  Location location = getSemanticLocation(op);
  if (op->hasAttr("obelisk_sim.default_assertion_failure")) {
    emitDefaultAssertionFailure(location);
    return success();
  }
  SmallVector<Operation *> children = getChildren(op);
  size_t expected = 1 + static_cast<size_t>(op.getHasPassAction()) +
                    static_cast<size_t>(op.getHasFailAction());
  if (children.size() != expected) {
    emitError(location) << "malformed immediate assertion inventory";
    return failure();
  }

  Block *controlMerge = nullptr;
  Value actionState;
  bool deferredEvaluator = op->hasAttr("obelisk_sim.deferred_evaluator");
  bool attemptControlled =
      !deferredEvaluator && op->hasAttr("obelisk_sim.assertion_controlled");
  bool actionControlled =
      !deferredEvaluator &&
      op->hasAttr("obelisk_sim.assertion_action_controlled");
  IntegerAttr assertionID;
  if (attemptControlled || actionControlled) {
    assertionID = op->getAttrOfType<IntegerAttr>(
        "obelisk_sim.assertion_control_target_id");
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

  auto actionEnabled = [&](bool passed) -> Value {
    if (!actionState)
      return {};
    int32_t mask = passed ? 1 : 4;
    Value selected = arith::AndIOp::create(
        builder, location, actionState,
        arith::ConstantOp::create(builder, location, builder.getI32Type(),
                                  builder.getI32IntegerAttr(mask)));
    Value zero = arith::ConstantOp::create(builder, location,
                                           builder.getI32Type(),
                                           builder.getI32IntegerAttr(0));
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
      if (auto targetID = op->getAttrOfType<IntegerAttr>(
              "obelisk_sim.assertion_control_target_id"))
        enqueue->setAttr("obelisk_sim.assertion_control_target_id", targetID);
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
            auto formals =
                call->getAttrOfType<ArrayAttr>(calleeFormalsAttrName);
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
            auto formal = cast<DictionaryAttr>(formals[index]);
            auto direction = static_cast<semantic::SVArgumentDirection>(
                formal.getAs<IntegerAttr>("direction").getInt());
            if (direction != semantic::SVArgumentDirection::In)
              return;
            break;
          }
          StringRef path = named.getReferencedPath();
          if (!path.empty() && !referencedNodes.contains(path))
            referencedNodes[path] = named;
        });
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

      std::string identity =
          (Twine(siteIdentity) + (passed ? ".pass" : ".fail")).str();
      Attribute previousCodeUnit = op->getAttr("obelisk_sim.fork_code_unit_id");
      BoolAttr previousDeferred = op.getIsDeferredAttr();
      op->setAttr("obelisk_sim.deferred_evaluator", builder.getUnitAttr());
      op->setAttr("obelisk_sim.deferred_result", builder.getBoolAttr(passed));
      op->setAttr("obelisk_sim.fork_code_unit_id",
                  builder.getI64IntegerAttr(stableCodeUnitID(identity)));
      op->setAttr("is_deferred", builder.getBoolAttr(false));
      FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
          outlineForkBranch(op, node, passed ? 0 : 1,
                            /*captureReferences=*/false);
      op->setAttr("is_deferred", previousDeferred);
      op->removeAttr("obelisk_sim.deferred_evaluator");
      op->removeAttr("obelisk_sim.deferred_result");
      if (previousCodeUnit)
        op->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
      else
        op->removeAttr("obelisk_sim.fork_code_unit_id");
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
      DictionaryAttr formal = builder.getDictionaryAttr({builder.getNamedAttr(
          "obelisk_sim.capture_kind",
          sim::CaptureKindAttr::get(function.getContext(),
                                    sim::CaptureKind::Formal))});
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
      evaluator->setAttr("obelisk_sim.detached_controls",
                         builder.getUnitAttr());
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
  if (auto result = op->getAttrOfType<BoolAttr>("obelisk_sim.deferred_result"))
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
      op->hasAttr("obelisk_sim.deferred_evaluator") && !op.getIsFinal();
  auto lowerAction = [&](Operation *action, unsigned branch) -> LogicalResult {
    if (!reactiveActions)
      return lowerStatement(action);
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string identity =
        (function.getSymName() + ".$reactive_assert_action." + Twine(node) +
         "." + Twine(branch))
            .str();
    Attribute previousCodeUnit =
        action->getAttr("obelisk_sim.fork_code_unit_id");
    action->setAttr("obelisk_sim.fork_code_unit_id",
                    builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlineForkBranch(action, node, branch, /*captureReferences=*/true);
    if (previousCodeUnit)
      action->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
    else
      action->removeAttr("obelisk_sim.fork_code_unit_id");
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
    if (!reactiveActions) {
      emitDefaultAssertionFailure(location);
      return success();
    }
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string identity =
        (function.getSymName() + ".$reactive_assert_default." + Twine(node))
            .str();
    Attribute previousCodeUnit = op->getAttr("obelisk_sim.fork_code_unit_id");
    op->setAttr("obelisk_sim.default_assertion_failure", builder.getUnitAttr());
    op->setAttr("obelisk_sim.fork_code_unit_id",
                builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlineForkBranch(op, node, 3, /*captureReferences=*/true);
    op->removeAttr("obelisk_sim.default_assertion_failure");
    if (previousCodeUnit)
      op->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
    else
      op->removeAttr("obelisk_sim.fork_code_unit_id");
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
    if (failed(lowerAction(action, 0)))
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
      if (failed(lowerAction(action, 1)))
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
      emitQualifierWarning(location, op.getCheckKind(), "if", "no match");
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
  emitQualifierWarning(location, op.getCheckKind(), "if", "multiple matches");
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
    emitQualifierWarning(location, op.getCheckKind(), "if", "no match");
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
    Location location, semantic::SVUniquePriorityCheck qualifier,
    StringRef statementKind, StringRef reason) {
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
  StringAttr scope =
      function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
  IntegerAttr multiplier =
      function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
  sim::SimDisplayOp::create(
      builder, location, function.getBody().front().getArgument(0), descriptor,
      ValueRange{text}, true, 10, ArrayRef<int32_t>{0}, scope, StringAttr{},
      multiplier, IntegerAttr{});
}

FailureOr<Value>
UnitLowering::lowerCaseLabel(Value selector, Type selectorType,
                             Operation *selectorNode, Operation *label,
                             semantic::SVCaseCondition condition) {
  Location location = getSemanticLocation(label);
  bool selectorString = isa<sim::StringType>(selectorType);
  bool selectorLogic =
      !selectorString &&
      isa<sim::LogicType>(sim::getPackedScalarType(selectorType));
  auto compareValue =
      [&](Value candidate, sim::CompareKind logicKind,
          arith::CmpIPredicate integerKind) -> FailureOr<Value> {
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
    if (auto array = dyn_cast<sim::UnpackedArrayType>((*candidate).getType())) {
      unsigned count = sim::getAggregateNumElements(array);
      for (unsigned index = 0; index < count; ++index) {
        Value element = sim::SimAggregateExtractOp::create(
            builder, location, array.getElementType(), *candidate, index);
        FailureOr<Value> equal = compareValue(element, sim::CompareKind::WildEq,
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
      }
    } else {
      FailureOr<Value> equal = compareValue(
          *candidate, sim::CompareKind::WildEq, arith::CmpIPredicate::eq);
      if (failed(equal))
        return failure();
      result = *equal;
    }
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
  FailureOr<Value> selector =
      lowerContextDeterminedExpression(children.front());
  if (failed(selector))
    return failure();
  if (!sim::getPackedScalarType((*selector).getType()) &&
      !isa<sim::StringType>((*selector).getType())) {
    emitError(location) << "case selector is not an executable packed value";
    return failure();
  }

  Block *mergeBlock = addBlock();

  bool inspectAll =
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique ||
      op.getCheckKind() == semantic::SVUniquePriorityCheck::Unique0;
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
    if (op.getCheckKind() == semantic::SVUniquePriorityCheck::Priority &&
        !hasDefault)
      emitQualifierWarning(location, op.getCheckKind(), "case", "no match");
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
  emitQualifierWarning(location, op.getCheckKind(), "case", "multiple matches");
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
    emitQualifierWarning(location, op.getCheckKind(), "case", "no match");
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

  auto i64 = builder.getI64Type();
  Value zero = arith::ConstantOp::create(builder, location, i64,
                                         builder.getI64IntegerAttr(0));

  // IEEE 1800-2017 18.16 evaluates every weight exactly once, in source order.
  // Accumulate the running total alongside so each item keeps the upper bound
  // of its own share of the distribution.
  SmallVector<Value> bounds;
  bounds.reserve(itemCount);
  Value total = zero;
  for (Operation *node : weights) {
    Location weightLocation = getSemanticLocation(node);
    FailureOr<Value> value = lowerExpression(node);
    if (failed(value))
      return failure();
    bool isSigned = isSignedNode(node);
    FailureOr<Value> weight = convert(*value, i64, isSigned, weightLocation);
    if (failed(weight))
      return failure();
    // A weight is a count, so a negative one contributes nothing. Clamping
    // keeps it from reappearing as a dominant unsigned magnitude.
    if (isSigned) {
      Value negative =
          arith::CmpIOp::create(builder, weightLocation,
                                arith::CmpIPredicate::slt, *weight, zero);
      weight = arith::SelectOp::create(builder, weightLocation, negative, zero,
                                       *weight)
                   .getResult();
    }
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
  Value context = function.getBody().front().getArgument(0);
  Value draw = sim::SimRandomBoundedOp::create(builder, location, i64, context,
                                               total);
  for (size_t item = 0; item + 1 < itemCount; ++item) {
    Block *itemBlock = addBlock();
    Block *nextItemBlock = addBlock();
    Value selected =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                              draw, bounds[item]);
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

LogicalResult
UnitLowering::lowerRandSequence(semantic::SVRandSequenceStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  RandSequenceContext context;
  context.breakTarget = addBlock();
  context.controlDepth = controlScopes.size();
  for (Operation *child : children) {
    auto production =
        dyn_cast<semantic::SVFrozenRandSeqProductionOp>(child);
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

  // Recursive productions are legal in 18.17 and require activation-owned
  // frames. Do not approximate them by a depth cutoff or static unrolling;
  // reject the currently unoutlined boundary before emitting any CFG.
  llvm::StringMap<int> visitState;
  std::function<LogicalResult(StringRef)> visit = [&](StringRef identity) {
    int &state = visitState[identity];
    if (state == 1) {
      emitError(location)
          << "recursive randsequence production requires activation-frame "
             "lowering";
      return failure();
    }
    if (state == 2)
      return success();
    auto found = context.productions.find(identity);
    if (found == context.productions.end()) {
      emitError(location) << "randsequence references unknown production "
                          << identity;
      return failure();
    }
    state = 1;
    WalkResult result = found->second->walk([&](semantic::SVProdItemOp item) {
      auto reference = item->getAttrOfType<SymbolRefAttr>("target");
      if (!reference) {
        emitError(getSemanticLocation(item))
            << "randsequence production item has no target";
        return WalkResult::interrupt();
      }
      auto target =
          context.productions.find(reference.getLeafReference().getValue());
      if (target == context.productions.end()) {
        emitError(getSemanticLocation(item))
            << "randsequence references unknown production "
            << reference;
        return WalkResult::interrupt();
      }
      if (failed(visit(target->getKey())))
        return WalkResult::interrupt();
      return WalkResult::advance();
    });
    if (result.wasInterrupted())
      return failure();
    state = 2;
    return success();
  };
  auto first = op->getAttrOfType<SymbolRefAttr>("first_production");
  if (!first) {
    emitError(location) << "randsequence has no first production";
    return failure();
  }
  if (!context.productions.count(first.getLeafReference())) {
    emitError(location) << "randsequence cannot resolve its first production";
    return failure();
  }
  if (failed(visit(first.getLeafReference())))
    return failure();

  randSequenceContexts.push_back(std::move(context));
  auto initial =
      randSequenceContexts.back().productions.find(first.getLeafReference());
  if (failed(lowerRandSequenceProduction(initial->second, {}))) {
    randSequenceContexts.pop_back();
    return failure();
  }
  Block *exit = randSequenceContexts.back().breakTarget;
  emitBranch(exit);
  setCurrent(exit);
  randSequenceContexts.pop_back();
  return success();
}

LogicalResult UnitLowering::lowerRandSequenceProduction(
    semantic::SVFrozenRandSeqProductionOp production,
    ArrayRef<Operation *> actuals) {
  Location location = getSemanticLocation(production);
  auto productionType = production->getAttrOfType<TypeAttr>("semantic_type");
  if (!productionType) {
    emitError(location) << "randsequence production has no semantic type";
    return failure();
  }
  if (!isa<semantic::VoidType>(productionType.getValue())) {
    emitError(location)
        << "value-returning randsequence productions are outside the current "
           "executable boundary";
    return failure();
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
    auto defaultCount =
        formal ? formal.getAs<IntegerAttr>("default_operand_count")
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

  SmallVector<Value> explicitValues;
  explicitValues.reserve(actuals.size());
  for (Operation *actual : actuals) {
    FailureOr<Value> value = lowerExpression(actual);
    if (failed(value))
      return failure();
    explicitValues.push_back(*value);
  }
  for (auto [index, attribute] : llvm::enumerate(formals)) {
    auto formal = cast<DictionaryAttr>(attribute);
    auto direction = formal.getAs<IntegerAttr>("direction");
    auto path = formal.getAs<StringAttr>("referenced_path");
    auto semanticType = formal.getAs<TypeAttr>("semantic_type");
    if (!direction ||
        direction.getInt() != static_cast<int64_t>(
                                  semantic::SVArgumentDirection::In) ||
        !path || !semanticType) {
      emitError(location)
          << "randsequence currently requires typed input formals";
      return failure();
    }
    FailureOr<Type> type =
        normalizeSemanticType(semanticType.getValue(), location);
    if (failed(type))
      return failure();
    Value initial;
    bool sourceSigned = false;
    if (index < explicitValues.size()) {
      initial = explicitValues[index];
      sourceSigned = isSignedNode(actuals[index]);
    } else if (defaultExpressions[index]) {
      FailureOr<Value> value = lowerExpression(defaultExpressions[index]);
      if (failed(value))
        return failure();
      initial = *value;
      sourceSigned = isSignedNode(defaultExpressions[index]);
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
    savedBindings.push_back({bindingPath, values.lookup(bindingPath),
                             lvalues.lookup(bindingPath),
                             values.count(bindingPath) != 0,
                             lvalues.count(bindingPath) != 0});
    Value reference = sim::SimRefAllocOp::create(
        builder, location,
        sim::RefType::get(function.getContext(), *type), *converted);
    values[bindingPath] = reference;
    lvalues[bindingPath] = reference;
  }

  ArrayRef<int64_t> itemCounts = production.getRuleItemCounts();
  ArrayRef<int64_t> hasWeights = production.getRuleHasWeights();
  ArrayRef<int64_t> hasWeightCodeBlocks =
      production.getRuleHasWeightCodeBlocks();
  ArrayRef<int64_t> isRandJoin = production.getRuleIsRandJoin();
  ArrayRef<int64_t> hasRandJoinExpressions =
      production.getRuleHasRandJoinExpressions();
  size_t ruleCount = production.getRuleCount();
  if (itemCounts.size() != ruleCount || hasWeights.size() != ruleCount ||
      hasWeightCodeBlocks.size() != ruleCount ||
      isRandJoin.size() != ruleCount ||
      hasRandJoinExpressions.size() != ruleCount) {
    emitError(location) << "malformed randsequence rule metadata";
    return failure();
  }

  struct Rule {
    Operation *weight = nullptr;
    SmallVector<Operation *> items;
    Operation *weightCodeBlock = nullptr;
    bool randJoin = false;
    Operation *randJoinBias = nullptr;
  };
  SmallVector<Rule> rules;
  rules.reserve(ruleCount);
  for (size_t index = 0; index < ruleCount; ++index) {
    if (itemCounts[index] < 0 ||
        (hasWeights[index] != 0 && hasWeights[index] != 1) ||
        (hasWeightCodeBlocks[index] != 0 &&
         hasWeightCodeBlocks[index] != 1) ||
        (isRandJoin[index] != 0 && isRandJoin[index] != 1) ||
        (hasRandJoinExpressions[index] != 0 &&
         hasRandJoinExpressions[index] != 1)) {
      emitError(location) << "invalid randsequence rule metadata";
      return failure();
    }
    Rule rule;
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
    llvm::append_range(
        rule.items,
        ArrayRef<Operation *>(children).slice(nextChild, itemCounts[index]));
    nextChild += itemCounts[index];
    if (hasWeightCodeBlocks[index]) {
      if (nextChild == children.size())
        return emitError(location)
               << "missing randsequence weight code block";
      rule.weightCodeBlock = children[nextChild++];
    }
    rules.push_back(std::move(rule));
  }
  if (nextChild != children.size()) {
    emitError(location) << "excess randsequence production operands";
    return failure();
  }

  Block *productionReturn = addBlock();
  randSequenceProductionReturns.push_back(
      {productionReturn, controlScopes.size()});
  llvm::scope_exit popReturn(
      [&] { randSequenceProductionReturns.pop_back(); });
  auto lowerRandJoin = [&](const Rule &rule) -> LogicalResult {
    if (rule.items.size() < 2)
      return emitError(location)
             << "rand join requires at least two production items";

    SmallVector<Operation *> streams;
    streams.reserve(rule.items.size());
    for (Operation *operand : rule.items) {
      auto item = dyn_cast<semantic::SVProdItemOp>(operand);
      auto targetRef = item
                           ? item->getAttrOfType<SymbolRefAttr>("target")
                           : SymbolRefAttr{};
      auto target = targetRef
                        ? randSequenceContexts.back().productions.find(
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
      FailureOr<Value> bias =
          convert(*lowered, builder.getF64Type(),
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
      Value atLeastZero = arith::CmpFOp::create(
          builder, getSemanticLocation(rule.randJoinBias),
          arith::CmpFPredicate::OGE, *bias, zero);
      Value atMostOne = arith::CmpFOp::create(
          builder, getSemanticLocation(rule.randJoinBias),
          arith::CmpFPredicate::OLE, *bias, one);
      Value valid = arith::AndIOp::create(
          builder, getSemanticLocation(rule.randJoinBias), atLeastZero,
          atMostOne);
      Block *accepted = addBlock();
      Block *invalid = addBlock();
      cf::CondBranchOp::create(builder,
                               getSemanticLocation(rule.randJoinBias), valid,
                               accepted, invalid);
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
    SmallVector<Value> initial(streams.size(),
                               arith::ConstantOp::create(
                                   builder, location, i1,
                                   builder.getBoolAttr(true)));
    initial.push_back(arith::ConstantOp::create(
        builder, location, i64,
        builder.getI64IntegerAttr(streams.size())));
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
    cf::CondBranchOp::create(builder, location, last, choose,
                             ValueRange{zero}, randomize, ValueRange{});

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
        nextActive.push_back(
            operand == index
                ? Value(arith::ConstantOp::create(
                      builder, location, i1, builder.getBoolAttr(false)))
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
  auto lowerExplicitWeight = [&](Operation *node, Type i64,
                                 Value zero) -> FailureOr<Value> {
    FailureOr<Type> sourceType = getNormalizedSemanticType(node);
    std::optional<unsigned> width =
        succeeded(sourceType) ? sim::getPackedWidth(*sourceType) : std::nullopt;
    if (!width || *width > 64) {
      emitError(getSemanticLocation(node))
          << "randsequence weights wider than 64 bits are outside the "
             "runtime RNG boundary";
      return failure();
    }
    FailureOr<Value> lowered = lowerExpression(node);
    if (failed(lowered))
      return failure();
    FailureOr<Value> converted =
        convert(*lowered, i64, isSignedNode(node), getSemanticLocation(node));
    if (failed(converted))
      return failure();
    if (isSignedNode(node)) {
      Value negative = arith::CmpIOp::create(
          builder, getSemanticLocation(node), arith::CmpIPredicate::slt,
          *converted, zero);
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
      Type i64 = builder.getI64Type();
      Value zero = arith::ConstantOp::create(builder, location, i64,
                                             builder.getI64IntegerAttr(0));
      if (failed(lowerExplicitWeight(rules.front().weight, i64, zero)))
        return failure();
    }
    if (failed(lowerRule(rules.front())))
      return failure();
  } else {
    Type i64 = builder.getI64Type();
    Value zero = arith::ConstantOp::create(builder, location, i64,
                                           builder.getI64IntegerAttr(0));
    Value one = arith::ConstantOp::create(builder, location, i64,
                                          builder.getI64IntegerAttr(1));
    SmallVector<Value> bounds;
    Value total = zero;
    for (const Rule &rule : rules) {
      Value weight = one;
      if (rule.weight) {
        FailureOr<Value> loweredWeight =
            lowerExplicitWeight(rule.weight, i64, zero);
        if (failed(loweredWeight))
          return failure();
        weight = *loweredWeight;
      }
      Value next = arith::AddIOp::create(builder, location, total, weight);
      Value overflow = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, next, total);
      Block *invalid = addBlock();
      Block *valid = addBlock();
      cf::CondBranchOp::create(builder, location, overflow, invalid, valid);
      setCurrent(invalid);
      if (failed(emitRuntimeFatal(location,
                                  "randsequence weight sum overflowed 64 bits")))
        return failure();
      setCurrent(valid);
      total = next;
      bounds.push_back(total);
    }
    Block *select = addBlock();
    Value any = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::ne, total, zero);
    cf::CondBranchOp::create(builder, location, any, select,
                             productionReturn);
    setCurrent(select);
    Value context = function.getBody().front().getArgument(0);
    Value draw = sim::SimRandomBoundedOp::create(builder, location, i64,
                                                 context, total);
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
  setCurrent(productionReturn);
  return success();
}

LogicalResult UnitLowering::lowerRandSequenceProductionItem(
    semantic::SVProdItemOp item) {
  if (randSequenceContexts.empty())
    return emitError(getSemanticLocation(item))
           << "randsequence production item has no active grammar";
  auto target = item->getAttrOfType<SymbolRefAttr>("target");
  if (!target)
    return emitError(getSemanticLocation(item))
           << "randsequence production item has no target";
  auto found = randSequenceContexts.back().productions.find(
      target.getLeafReference());
  if (found == randSequenceContexts.back().productions.end())
    return emitError(getSemanticLocation(item))
           << "randsequence cannot resolve production " << target;
  SmallVector<Operation *> actuals = getChildren(item);
  auto argumentCount = item->getAttrOfType<IntegerAttr>("argument_count");
  if (!argumentCount || argumentCount.getInt() < 0 ||
      actuals.size() != static_cast<uint64_t>(argumentCount.getInt()))
    return emitError(getSemanticLocation(item))
           << "malformed randsequence actual argument inventory";
  return lowerRandSequenceProduction(found->second, actuals);
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
        (hasElse.getValue() &&
         !isa<semantic::SVProdItemOp>(children[2])))
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
    if (hasElse.getValue() &&
        failed(lowerRandSequenceProductionItem(
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
    FailureOr<Value> count = convert(*lowered, i64,
                                     isSignedNode(children.front()), location);
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
    Value more = arith::CmpIOp::create(builder, location,
                                       arith::CmpIPredicate::ult, index, *count);
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
    ArrayRef<int64_t> expressionCounts =
        expressionCountsAttr ? expressionCountsAttr.asArrayRef()
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
      if (expressionCount <= 0 ||
          next + expressionCount >= children.size())
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
        FailureOr<Value> matches = lowerCaseLabel(
            *selector, *selectorType, selectorNode, label,
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
  return emitError(location) << "unsupported randsequence production node "
                             << node->getName();
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
      emitQualifierWarning(location, op.getCheckKind(), "case", "no match");
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
  emitQualifierWarning(location, op.getCheckKind(), "case", "multiple matches");
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
    emitQualifierWarning(location, op.getCheckKind(), "case", "no match");
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
