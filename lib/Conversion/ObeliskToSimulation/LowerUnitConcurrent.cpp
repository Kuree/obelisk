//===- LowerUnitConcurrent.cpp - AOT concurrent assertion monitors -------===//
//
// Compiles the bounded single-clock SVA slice into ordinary simulation SSA.
// Runtime state is a compact bitset carried across clock suspensions; the
// runtime has no temporal interpreter and no solver dependency.
//
//===----------------------------------------------------------------------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"

#include <optional>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

/// One deterministic bounded sequence. Each clock age contains the boolean
/// expressions that must all hold at that age. Empty ages represent ## gaps.
struct FixedSequence {
  SmallVector<SmallVector<Operation *, 2>, 8> ages;
};

/// Return the substituted body of a nonrecursive assertion invocation. The
/// remaining children are the source actual/default and local-initializer
/// inventory retained for semantic tooling; they are deliberately not
/// evaluated by the monitor compiler.
static FailureOr<Operation *> getExpandedAssertionBody(
    semantic::SVAssertionInstanceExpressionOp instance) {
  if (instance.getIsRecursiveProperty() || !instance.getHasExpandedBody() ||
      instance.getLocalVariableCount() != 0)
    return failure();

  size_t initializedLocals = llvm::count_if(
      instance.getLocalVariableHasInitializer(),
      [](int64_t hasInitializer) { return hasInitializer != 0; });
  SmallVector<Operation *> children = getChildren(instance);
  size_t expectedChildren =
      1 + instance.getArgumentCount() + initializedLocals;
  if (children.size() != expectedChildren)
    return failure();
  return children.front();
}

static FailureOr<FixedSequence> compileFixedSequence(Operation *operation) {
  if (auto instance =
          dyn_cast<semantic::SVAssertionInstanceExpressionOp>(operation)) {
    FailureOr<Operation *> body = getExpandedAssertionBody(instance);
    if (failed(body))
      return failure();
    return compileFixedSequence(*body);
  }

  if (auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(simple);
    if (simple.getIsNull() || children.size() != 1)
      return failure();
    uint64_t repetitions = 1;
    if (simple.getHasRepetition()) {
      if (simple.getRepetitionKind() !=
              semantic::SVSequenceRepetitionKind::Consecutive ||
          simple.getRepetitionIsUnbounded() || !simple.getRepetitionMin() ||
          !simple.getRepetitionMax() ||
          *simple.getRepetitionMin() != *simple.getRepetitionMax() ||
          *simple.getRepetitionMin() <= 0)
        return failure();
      repetitions = static_cast<uint64_t>(*simple.getRepetitionMin());
    }
    FixedSequence nested;
    if (isa<semantic::SVAssertionInstanceExpressionOp>(children.front())) {
      FailureOr<FixedSequence> compiled =
          compileFixedSequence(children.front());
      if (failed(compiled))
        return failure();
      nested = std::move(*compiled);
    } else {
      nested.ages.resize(1);
      nested.ages.front().push_back(children.front());
    }
    if (nested.ages.empty() || repetitions > 63 / nested.ages.size())
      return failure();
    FixedSequence result;
    result.ages.reserve(repetitions * nested.ages.size());
    for (uint64_t index = 0; index < repetitions; ++index)
      llvm::append_range(result.ages, nested.ages);
    return result;
  }

  if (auto concat = dyn_cast<semantic::SVSequenceConcatExprOp>(operation)) {
    SmallVector<Operation *> children = getChildren(concat);
    ArrayAttr delays = concat.getDelays();
    if (children.empty() || delays.size() != children.size())
      return failure();
    FixedSequence result;
    for (auto [index, child] : llvm::enumerate(children)) {
      auto delay = dyn_cast<DictionaryAttr>(delays[index]);
      auto minimum = delay ? delay.getAs<IntegerAttr>("min") : IntegerAttr{};
      auto maximum = delay ? delay.getAs<IntegerAttr>("max") : IntegerAttr{};
      auto unbounded =
          delay ? delay.getAs<BoolAttr>("is_unbounded") : BoolAttr{};
      if (!minimum || !maximum || !unbounded || unbounded.getValue() ||
          minimum.getInt() < 0 || minimum.getInt() != maximum.getInt())
        return failure();
      FailureOr<FixedSequence> nested = compileFixedSequence(child);
      if (failed(nested))
        return failure();
      uint64_t offset = minimum.getInt();
      if (result.ages.empty())
        result.ages.resize(1);
      uint64_t start = result.ages.size() - 1 + offset;
      uint64_t required = start + nested->ages.size();
      if (required > 63)
        return failure();
      result.ages.resize(std::max<uint64_t>(result.ages.size(), required));
      for (auto [age, predicates] : llvm::enumerate(nested->ages))
        llvm::append_range(result.ages[start + age], predicates);
    }
    return result;
  }

  return failure();
}

static Operation *unwrapAssertionInstance(Operation *operation) {
  while (true) {
    if (auto instance =
            dyn_cast<semantic::SVAssertionInstanceExpressionOp>(operation)) {
      FailureOr<Operation *> body = getExpandedAssertionBody(instance);
      if (failed(body))
        return nullptr;
      operation = *body;
      continue;
    }
    auto simple = dyn_cast<semantic::SVSimpleAssertionExprOp>(operation);
    SmallVector<Operation *> children = simple ? getChildren(simple)
                                               : SmallVector<Operation *>{};
    if (simple && !simple.getIsNull() && !simple.getHasRepetition() &&
        children.size() == 1 &&
        isa<semantic::SVAssertionInstanceExpressionOp>(children.front())) {
      operation = children.front();
      continue;
    }
    return operation;
  }
}

} // namespace

LogicalResult UnitLowering::lowerSequenceEndpointMonitor(
    ArrayRef<Operation *> roots) {
  if (roots.size() != 1)
    return function.emitError(
               "sequence endpoint monitor requires one assertion instance"),
           failure();
  auto instance =
      dyn_cast<semantic::SVAssertionInstanceExpressionOp>(roots.front());
  if (!instance)
    return emitError(getSemanticLocation(roots.front()))
               << "sequence endpoint monitor has no assertion instance",
           failure();
  FailureOr<Operation *> expanded = getExpandedAssertionBody(instance);
  if (failed(expanded))
    return emitError(getSemanticLocation(instance))
               << "sequence endpoint monitor requires a nonrecursive "
                  "expanded instance without local variables",
           failure();
  auto clocking = dyn_cast<semantic::SVClockingAssertionExprOp>(*expanded);
  SmallVector<Operation *> clocked = clocking ? getChildren(clocking)
                                              : SmallVector<Operation *>{};
  if (!clocking || clocked.size() != 2)
    return emitError(getSemanticLocation(*expanded))
               << "sequence endpoint monitor requires an explicit clock",
           failure();
  auto clock = dyn_cast<semantic::SVSignalEventControlOp>(clocked.front());
  if (!clock || clock.getHasIff() || getChildren(clock).size() != 1 ||
      !isAddressableExpression(getChildren(clock).front()))
    return emitError(getSemanticLocation(clocked.front()))
               << "sequence endpoint monitor requires one direct signal "
                  "edge clock without iff",
           failure();
  FailureOr<FixedSequence> compiled = compileFixedSequence(clocked.back());
  if (failed(compiled) || compiled->ages.empty() ||
      compiled->ages.size() > 63)
    return emitError(getSemanticLocation(clocked.back()))
               << "sequence endpoint monitor supports boolean terms, fixed "
                  "## delays, and fixed consecutive repetition up to 63 "
                  "cycles",
           failure();

  auto endpointPath =
      function->getAttrOfType<StringAttr>(sequenceEndpointPathAttrName);
  Value endpoint = endpointPath ? values.lookup(endpointPath.getValue())
                                : Value{};
  if (!endpoint || !isa<sim::EventType>(endpoint.getType()))
    return function.emitError(
               "sequence endpoint monitor has no endpoint event capture"),
           failure();
  function->removeAttr(sequenceEndpointMonitorAttrName);
  function->removeAttr(sequenceEndpointPathAttrName);

  Location location = getSemanticLocation(instance);
  function->setAttr("home_region",
                    sim::EventRegionAttr::get(function.getContext(),
                                              sim::EventRegion::Observed));
  Type stateType = builder.getI64Type();
  Value zero = arith::ConstantOp::create(builder, location, stateType,
                                         builder.getI64IntegerAttr(0));
  Value stateStorage;
  if (compiled->ages.size() > 1)
    stateStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);

  Block *wait = addBlock();
  Block *sample = addBlock();
  emitBranch(wait);
  setCurrent(wait);
  if (failed(emitEventSuspend(clock, sample)))
    return failure();
  wait->getTerminator()->setAttr(
      "resume_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Observed));
  setCurrent(sample);

  bool savedSampleAssertionValues = sampleAssertionValues;
  sampleAssertionValues = true;
  Operation *savedSampledClock = activeSampledClock;
  activeSampledClock = clock;
  llvm::scope_exit restoreSampling([&] {
    sampleAssertionValues = savedSampleAssertionValues;
    activeSampledClock = savedSampledClock;
  });

  llvm::DenseMap<Operation *, Value> predicateCache;
  auto evaluateAge = [&](ArrayRef<Operation *> predicates) -> FailureOr<Value> {
    Value result = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    for (Operation *predicate : predicates) {
      Value truth;
      if (auto found = predicateCache.find(predicate);
          found != predicateCache.end()) {
        truth = found->second;
      } else {
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value))
          return failure();
        FailureOr<Value> converted =
            truthValue(*value, getSemanticLocation(predicate));
        if (failed(converted))
          return failure();
        truth = *converted;
        predicateCache[predicate] = truth;
      }
      result = arith::AndIOp::create(builder, location, result, truth);
    }
    return result;
  };
  auto triggerIf = [&](Value condition) {
    Block *trigger = addBlock();
    Block *continuation = addBlock();
    cf::CondBranchOp::create(builder, location, condition, trigger,
                             ValueRange{}, continuation, ValueRange{});
    setCurrent(trigger);
    sim::SimEventTriggerOp::create(builder, location, endpoint, Value{},
                                   builder.getBoolAttr(false),
                                   sim::EventSiteAttr{});
    emitBranch(continuation);
    setCurrent(continuation);
  };

  Value state = stateStorage
                    ? sim::SimRefLoadOp::create(builder, location, stateType,
                                                stateStorage)
                    : zero;
  Value nextState = zero;
  for (uint64_t age = 1; age < compiled->ages.size(); ++age) {
    Value mask = arith::ConstantOp::create(
        builder, location, stateType,
        builder.getI64IntegerAttr(uint64_t{1} << age));
    Value presentBits = arith::AndIOp::create(builder, location, state, mask);
    Value active = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, presentBits, zero);
    FailureOr<Value> matches = evaluateAge(compiled->ages[age]);
    if (failed(matches))
      return failure();
    Value advances = arith::AndIOp::create(builder, location, active, *matches);
    if (age + 1 == compiled->ages.size()) {
      triggerIf(advances);
    } else {
      Value nextMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
      Value advancedBit =
          arith::SelectOp::create(builder, location, advances, nextMask, zero);
      nextState =
          arith::OrIOp::create(builder, location, nextState, advancedBit);
    }
  }

  FailureOr<Value> starts = evaluateAge(compiled->ages.front());
  if (failed(starts))
    return failure();
  if (compiled->ages.size() == 1) {
    triggerIf(*starts);
  } else {
    Value nextMask = arith::ConstantOp::create(
        builder, location, stateType, builder.getI64IntegerAttr(2));
    Value started =
        arith::SelectOp::create(builder, location, *starts, nextMask, zero);
    nextState = arith::OrIOp::create(builder, location, nextState, started);
  }
  if (stateStorage)
    sim::SimRefStoreOp::create(builder, location, nextState, stateStorage);
  cf::BranchOp::create(builder, location, wait);
  return success();
}

LogicalResult UnitLowering::lowerConcurrentAssertion(
    semantic::SVConcurrentAssertionStatementOp op) {
  Location location = getSemanticLocation(op);
  if (op->hasAttr("obelisk_sim.default_assertion_failure")) {
    emitDefaultAssertionFailure(location, "concurrent assertion");
    return success();
  }
  SmallVector<Operation *> children = getChildren(op);

  bool cover =
      op.getAssertionKind() == semantic::SVAssertionKind::CoverProperty ||
      op.getAssertionKind() == semantic::SVAssertionKind::CoverSequence;
  bool assertion = op.getAssertionKind() == semantic::SVAssertionKind::Assert ||
                   op.getAssertionKind() == semantic::SVAssertionKind::Assume;
  bool observable = assertion || cover;
  if (!observable &&
      op.getAssertionKind() != semantic::SVAssertionKind::Restrict) {
    emitError(location)
        << "expect statements are not executable by the bounded concurrent "
           "monitor";
    return failure();
  }

  // A source assertion label arrives as a synthetic named block. Preserve its
  // prepared stable identity for future assertion-control selection without
  // turning it into a procedural control activation across clock waits.
  if (auto block =
          dyn_cast_or_null<semantic::SVBlockStatementOp>(op->getParentOp())) {
    if (auto path = block.getBlockPathAttr())
      function->setAttr("obelisk_sim.assertion_path", path);
    if (auto target =
            block->getAttrOfType<IntegerAttr>("obelisk_sim.control_target_id"))
      function->setAttr("obelisk_sim.assertion_target_id", target);
  }

  size_t prefix = 0;
  Operation *disable = nullptr;
  if (op.getHasDefaultDisable()) {
    if (children.empty())
      return op.emitError("missing resolved default disable expression"),
             failure();
    disable = children[prefix++];
  }
  Operation *defaultClock = nullptr;
  if (op.getDefaultClockingSymbolAttr()) {
    if (prefix >= children.size() ||
        !isa<semantic::SVSignalEventControlOp, semantic::SVEventListControlOp>(
            children[prefix]))
      return op.emitError("missing resolved default clock event"), failure();
    defaultClock = children[prefix++];
  }
  size_t expected = prefix + 1 + static_cast<size_t>(op.getHasPassAction()) +
                    static_cast<size_t>(op.getHasFailAction());
  if (children.size() != expected)
    return op.emitError("malformed concurrent assertion inventory"), failure();

  Operation *property = unwrapAssertionInstance(children[prefix]);
  if (!property)
    return op.emitError("recursive, local-variable, or malformed assertion "
                        "instances are not executable by the bounded AOT "
                        "monitor"),
           failure();

  Operation *clock = defaultClock;
  if (auto clocking = dyn_cast<semantic::SVClockingAssertionExprOp>(property)) {
    SmallVector<Operation *> clocked = getChildren(clocking);
    if (clocked.size() != 2)
      return clocking.emitError("malformed clocked assertion"), failure();
    clock = clocked.front();
    property = unwrapAssertionInstance(clocked.back());
  }
  if (!clock)
    return op.emitError("concurrent assertion has no resolved clock"),
           failure();
  auto clockEvent = dyn_cast<semantic::SVSignalEventControlOp>(clock);
  if (!clockEvent || clockEvent.getHasIff() ||
      getChildren(clockEvent).size() != 1 ||
      !isAddressableExpression(getChildren(clockEvent).front()))
    return emitError(getSemanticLocation(clock))
               << "AOT concurrent monitors currently require one direct "
                  "signal edge clock without iff",
           failure();

  if (auto disabled =
          dyn_cast_or_null<semantic::SVDisableIffAssertionExprOp>(property)) {
    SmallVector<Operation *> nested = getChildren(disabled);
    if (nested.size() != 2)
      return disabled.emitError("malformed disable iff assertion"), failure();
    disable = nested.front();
    property = unwrapAssertionInstance(nested.back());
  }
  if (!property)
    return op.emitError("disable iff wraps an unsupported assertion instance"),
           failure();

  semantic::SVBinaryAssertionExprOp implication;
  FixedSequence sequence;
  Operation *antecedent = nullptr;
  bool nonoverlapped = false;
  if (auto binary =
          dyn_cast_or_null<semantic::SVBinaryAssertionExprOp>(property);
      binary &&
      (binary.getOperatorKind() ==
           semantic::SVAssertionBinaryOperator::OverlappedImplication ||
       binary.getOperatorKind() ==
           semantic::SVAssertionBinaryOperator::NonOverlappedImplication)) {
    SmallVector<Operation *> operands = getChildren(binary);
    if (operands.size() != 2)
      return binary.emitError("malformed implication"), failure();
    FailureOr<FixedSequence> lhs = compileFixedSequence(operands.front());
    FailureOr<FixedSequence> rhs = compileFixedSequence(operands.back());
    if (failed(lhs) || lhs->ages.size() != 1 || lhs->ages.front().size() != 1 ||
        failed(rhs))
      return binary.emitError(
                 "AOT implication antecedents must be one boolean term and "
                 "consequents must have bounded fixed delays"),
             failure();
    implication = binary;
    antecedent = lhs->ages.front().front();
    sequence = std::move(*rhs);
    nonoverlapped =
        binary.getOperatorKind() ==
        semantic::SVAssertionBinaryOperator::NonOverlappedImplication;
  } else {
    FailureOr<FixedSequence> compiled = compileFixedSequence(property);
    if (failed(compiled))
      return emitError(getSemanticLocation(property))
                 << "AOT concurrent monitors currently support boolean "
                    "terms, fixed ## delays, and fixed consecutive "
                    "repetition up to 63 cycles",
             failure();
    sequence = std::move(*compiled);
  }
  if (sequence.ages.empty() || sequence.ages.size() > 63)
    return op.emitError("concurrent monitor horizon must be 1..63 cycles"),
           failure();
  // IEEE 1800 evaluates sampled predicates and monitor state in Observed.
  function->setAttr("home_region",
                    sim::EventRegionAttr::get(function.getContext(),
                                              sim::EventRegion::Observed));

  Type stateType = builder.getI64Type();
  Value zero = arith::ConstantOp::create(builder, location, stateType,
                                         builder.getI64IntegerAttr(0));
  bool needsState =
      disable || sequence.ages.size() > 1 || (implication && nonoverlapped);
  Value stateStorage;
  if (needsState)
    stateStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);

  Value disableEpoch;
  Value initialDisable;
  if (disable) {
    // `disable iff` is unsampled and asynchronous. Bind its two-state truth
    // value as a computed observer: every false-to-true transition wakes a
    // cold Observed actor which clears live attempts and advances an epoch.
    // Reactive reports capture that epoch and become no-ops when cancellation
    // overtakes an already queued callback in the same time slot.
    FailureOr<Value> current = lowerExpression(disable);
    if (failed(current))
      return failure();
    FailureOr<Value> truth = truthValue(*current, getSemanticLocation(disable));
    if (failed(truth))
      return failure();
    initialDisable = *truth;
    FailureOr<Value> observer = bindObserver(disable);
    if (failed(observer))
      return emitError(getSemanticLocation(disable))
                 << "disable iff expression has no asynchronous observer; "
                    "its operands are not executable",
             failure();
    auto observerBinding = observer->getDefiningOp<sim::SimObserverBindOp>();
    if (!observerBinding)
      return emitError(getSemanticLocation(disable))
                 << "disable iff expression has no observer binding",
             failure();
    for (Value capture : observerBinding.getValues())
      if (isa<sim::RefType>(capture.getType()) &&
          !isStaticallyAllocatedOverrideTarget(capture))
        return emitError(getSemanticLocation(disable))
                   << "disable iff cannot asynchronously observe an "
                      "automatic variable",
               failure();
    auto design = function->getParentOfType<sim::SimDesignOp>();
    sim::SimFuncOp observerEvaluator =
        design ? design.lookupSymbol<sim::SimFuncOp>(
                     observerBinding.getEvaluator())
               : sim::SimFuncOp{};
    if (!observerEvaluator)
      return emitError(getSemanticLocation(disable))
                 << "disable iff observer evaluator is missing",
             failure();
    observerEvaluator->setAttr("obelisk_sim.concurrent_cancel_observer",
                               builder.getUnitAttr());
    observerEvaluator->setAttr("obelisk_sim.detached_controls",
                               builder.getUnitAttr());
    disableEpoch = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);

    if (!design)
      return function.emitError(
                 "concurrent disable outlining requires a simulation design"),
             failure();
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    std::string symbol =
        (function.getSymName() + ".$concurrent_cancel." + Twine(node)).str();
    std::string identity =
        (function.getSymName() + ".$concurrent_disable." + Twine(node)).str();
    uint64_t codeUnitID = stableCodeUnitID(identity);
    uint64_t scopeID = 0;
    std::string parentHierarchy = function.getSymName().str();
    uint64_t parentID = function.getCodeUnitId().value_or(0);
    for (sim::SimCodeUnitDeclOp declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>()) {
      if (declaration.getId() != parentID)
        continue;
      scopeID = declaration.getScopeId();
      parentHierarchy = declaration.getHierarchicalName().str();
      break;
    }
    std::string hierarchy =
        (Twine(parentHierarchy) + ".$concurrent_disable." + Twine(node)).str();

    Value context = function.getBody().front().getArgument(0);
    SmallVector<Value> captures{context};
    llvm::append_range(captures, observerBinding.getValues());
    unsigned initialDisableIndex = captures.size();
    captures.push_back(initialDisable);
    unsigned stateStorageIndex = captures.size();
    captures.push_back(stateStorage);
    unsigned disableEpochIndex = captures.size();
    captures.push_back(disableEpoch);
    SmallVector<Type> inputs;
    for (Value capture : captures)
      inputs.push_back(capture.getType());
    SmallVector<DictionaryAttr> argumentAttrs;
    for (auto [index, capture] : llvm::enumerate(captures)) {
      SmallVector<NamedAttribute> metadata;
      if (auto argument = dyn_cast<BlockArgument>(capture);
          argument && argument.getOwner() == &function.getBody().front()) {
        if (DictionaryAttr source = function.getArgAttrDict(
                argument.getArgNumber()))
          llvm::append_range(metadata, source);
      }
      if (metadata.empty())
        metadata.push_back(builder.getNamedAttr(
            "obelisk_sim.capture_kind",
            sim::CaptureKindAttr::get(function.getContext(),
                                      index == 0 ? sim::CaptureKind::Context
                                                 : sim::CaptureKind::Formal)));
      if (isa<sim::RefType>(capture.getType()) &&
          !isStaticallyAllocatedOverrideTarget(capture))
        metadata.push_back(builder.getNamedAttr(
            "obelisk_sim.automatic_reference_capture", builder.getUnitAttr()));
      argumentAttrs.push_back(builder.getDictionaryAttr(metadata));
    }

    OpBuilder outlineBuilder(function);
    outlineBuilder.setInsertionPoint(function);
    sim::SimCodeUnitDeclOp::create(
        outlineBuilder, getSemanticLocation(disable), codeUnitID, scopeID,
        sim::EntryKind::Fork, outlineBuilder.getStringAttr(hierarchy),
        outlineBuilder.getStringAttr("concurrent assertion disable observer"),
        outlineBuilder.getUnitAttr());
    SmallVector<NamedAttribute> attributes{
        outlineBuilder.getNamedAttr(bindingsAttrName,
                                    outlineBuilder.getArrayAttr({})),
        outlineBuilder.getNamedAttr(
            "code_unit_id", outlineBuilder.getI64IntegerAttr(codeUnitID)),
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            "home_region",
            sim::EventRegionAttr::get(function.getContext(),
                                      sim::EventRegion::Reactive)),
        outlineBuilder.getNamedAttr(
            "domain", sim::ExecutionDomainAttr::get(
                          function.getContext(), sim::ExecutionDomain::Design)),
        outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                    outlineBuilder.getStringAttr(hierarchy)),
    };
    sim::SimFuncOp cancel = sim::SimFuncOp::create(
        outlineBuilder, getSemanticLocation(disable), symbol,
        FunctionType::get(function.getContext(), inputs, TypeRange{}),
        sim::EntryKind::Fork, attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(cancel, SymbolTable::Visibility::Private);
    cancel->setAttr("obelisk_sim.concurrent_cancel", builder.getUnitAttr());
    cancel->setAttr("obelisk_sim.detached_controls", builder.getUnitAttr());
    cancel->setAttr("obelisk_sim.priority_signal_resume",
                    builder.getUnitAttr());

    Block &entry = cancel.getBody().front();
    Block *waitDisable = new Block;
    Block *cancelLiveAttempts = new Block;
    waitDisable->addArgument(builder.getI1Type(), getSemanticLocation(disable));
    cancel.getBody().push_back(waitDisable);
    cancel.getBody().push_back(cancelLiveAttempts);
    OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
    cf::BranchOp::create(entryBuilder, getSemanticLocation(disable),
                         waitDisable,
                         ValueRange{entry.getArgument(initialDisableIndex)});
    OpBuilder waitBuilder = OpBuilder::atBlockEnd(waitDisable);
    SmallVector<Value> reboundValues;
    for (unsigned index = 1; index != initialDisableIndex; ++index)
      reboundValues.push_back(entry.getArgument(index));
    auto reboundObserver = sim::SimObserverBindOp::create(
        waitBuilder, getSemanticLocation(disable),
        observerBinding.getResult().getType(),
        observerBinding.getEvaluatorAttr(), reboundValues,
        observerBinding.getCaptureCountAttr());
    SmallVector<Value> observed{reboundObserver.getResult(),
                                waitDisable->getArgument(0)};
    auto cancelWait = sim::SimSuspendObserveOp::create(
        waitBuilder, getSemanticLocation(disable), observed, 0,
        ArrayRef<int32_t>{static_cast<int32_t>(sim::EdgeKind::Posedge)},
        ArrayRef<int32_t>{-1}, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr::get(function.getContext(),
                                  sim::EventRegion::Reactive),
        cancelLiveAttempts);
    cancelWait->setAttr("obelisk_sim.concurrent_cancel_level_true",
                        builder.getUnitAttr());

    OpBuilder cancelBuilder = OpBuilder::atBlockEnd(cancelLiveAttempts);
    Value cancelZero = arith::ConstantOp::create(
        cancelBuilder, getSemanticLocation(disable), stateType,
        cancelBuilder.getI64IntegerAttr(0));
    sim::SimRefStoreOp::create(cancelBuilder, getSemanticLocation(disable),
                               cancelZero,
                               entry.getArgument(stateStorageIndex));
    Value epoch =
        sim::SimRefLoadOp::create(cancelBuilder, getSemanticLocation(disable),
                                  stateType,
                                  entry.getArgument(disableEpochIndex));
    Value one = arith::ConstantOp::create(
        cancelBuilder, getSemanticLocation(disable), stateType,
        cancelBuilder.getI64IntegerAttr(1));
    Value nextEpoch = arith::AddIOp::create(
        cancelBuilder, getSemanticLocation(disable), epoch, one);
    sim::SimRefStoreOp::create(cancelBuilder, getSemanticLocation(disable),
                               nextEpoch,
                               entry.getArgument(disableEpochIndex));
    Value currentTrue = arith::ConstantOp::create(
        cancelBuilder, getSemanticLocation(disable), builder.getI1Type(),
        builder.getBoolAttr(true));
    cf::BranchOp::create(cancelBuilder, getSemanticLocation(disable),
                         waitDisable, ValueRange{currentTrue});

    sim::SimSpawnOp::create(builder, getSemanticLocation(disable),
                            cancel.getSymNameAttr(), captures, ArrayAttr{},
                            ArrayAttr{});
  }

  struct ReportCallback {
    sim::SimFuncOp function;
    SmallVector<Value> captures;
    Location location;
  };
  std::optional<ReportCallback> passReport;
  std::optional<ReportCallback> failReport;
  auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
  uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
  auto outlineReport =
      [&](bool passed, std::optional<ReportCallback> &report) -> LogicalResult {
    if (!observable || (!passed && cover))
      return success();
    Operation *selected = nullptr;
    bool defaultFailure = false;
    if (passed && op.getHasPassAction())
      selected = children[prefix + 1];
    else if (!passed && op.getHasFailAction())
      selected = children[prefix + 1 + op.getHasPassAction()];
    else if (!passed && assertion)
      defaultFailure = true;
    if (!selected && !defaultFailure)
      return success();
    if (selected && isa<semantic::SVEmptyStatementOp>(selected))
      return success();

    Operation *outlined = defaultFailure ? op.getOperation() : selected;
    unsigned ordinal = passed ? 0 : (defaultFailure ? 2 : 1);
    std::string identity = (function.getSymName() + ".$concurrent_report." +
                            Twine(node) + "." + Twine(ordinal))
                               .str();
    Attribute previousCodeUnit =
        outlined->getAttr("obelisk_sim.fork_code_unit_id");
    outlined->setAttr("obelisk_sim.fork_code_unit_id",
                      builder.getI64IntegerAttr(stableCodeUnitID(identity)));
    if (defaultFailure)
      op->setAttr("obelisk_sim.default_assertion_failure",
                  builder.getUnitAttr());
    FailureOr<std::pair<sim::SimFuncOp, SmallVector<Value>>> callback =
        outlineForkBranch(outlined, node, ordinal,
                          /*captureReferences=*/true);
    if (defaultFailure)
      op->removeAttr("obelisk_sim.default_assertion_failure");
    if (previousCodeUnit)
      outlined->setAttr("obelisk_sim.fork_code_unit_id", previousCodeUnit);
    else
      outlined->removeAttr("obelisk_sim.fork_code_unit_id");
    if (failed(callback))
      return failure();

    callback->first->setAttr(
        "home_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Reactive));
    callback->first->setAttr(
        "domain", sim::ExecutionDomainAttr::get(function.getContext(),
                                                sim::ExecutionDomain::Design));
    callback->first->setAttr("obelisk_sim.concurrent_report",
                             builder.getUnitAttr());
    callback->first->setAttr("obelisk_sim.detached_controls",
                             builder.getUnitAttr());
    if (disableEpoch) {
      sim::SimFuncOp evaluator = callback->first;
      SmallVector<Type> inputTypes(evaluator.getFunctionType().getInputs());
      inputTypes.push_back(disableEpoch.getType());
      inputTypes.push_back(stateType);
      evaluator.setFunctionType(
          FunctionType::get(function.getContext(), inputTypes, TypeRange{}));
      Block &entry = evaluator.getBody().front();
      BlockArgument epochReference =
          entry.addArgument(disableEpoch.getType(), location);
      BlockArgument expectedEpoch = entry.addArgument(stateType, location);
      SmallVector<Attribute> argumentAttrs;
      if (ArrayAttr attrs = evaluator.getArgAttrsAttr())
        llvm::append_range(argumentAttrs, attrs);
      while (argumentAttrs.size() + 2 < inputTypes.size())
        argumentAttrs.push_back(builder.getDictionaryAttr({}));
      argumentAttrs.push_back(builder.getDictionaryAttr({
          builder.getNamedAttr(
              "obelisk_sim.capture_kind",
              sim::CaptureKindAttr::get(function.getContext(),
                                        sim::CaptureKind::Formal)),
          builder.getNamedAttr("obelisk_sim.automatic_reference_capture",
                               builder.getUnitAttr()),
      }));
      argumentAttrs.push_back(builder.getDictionaryAttr({builder.getNamedAttr(
          "obelisk_sim.capture_kind",
          sim::CaptureKindAttr::get(function.getContext(),
                                    sim::CaptureKind::Formal))}));
      evaluator.setArgAttrsAttr(builder.getArrayAttr(argumentAttrs));

      Block *body = entry.splitBlock(entry.begin());
      Block *canceled = new Block;
      evaluator.getBody().push_back(canceled);
      OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
      Value currentEpoch = sim::SimRefLoadOp::create(entryBuilder, location,
                                                     stateType, epochReference);
      Value current = arith::CmpIOp::create(entryBuilder, location,
                                            arith::CmpIPredicate::eq,
                                            currentEpoch, expectedEpoch);
      cf::CondBranchOp::create(entryBuilder, location, current, body, canceled);
      OpBuilder canceledBuilder = OpBuilder::atBlockEnd(canceled);
      sim::SimReturnOp::create(canceledBuilder, location, ValueRange{});
      callback->second.push_back(disableEpoch);
    }
    report.emplace(ReportCallback{callback->first, std::move(callback->second),
                                  getSemanticLocation(outlined)});
    return success();
  };
  if (failed(outlineReport(true, passReport)) ||
      failed(outlineReport(false, failReport)))
    return failure();
  auto scheduleResult = [&](bool passed) {
    std::optional<ReportCallback> &report = passed ? passReport : failReport;
    if (!report)
      return;
    SmallVector<Value> captures(report->captures);
    if (disableEpoch)
      captures.push_back(sim::SimRefLoadOp::create(builder, report->location,
                                                   stateType, disableEpoch));
    sim::SimSpawnOp::create(builder, report->location,
                            report->function.getSymNameAttr(), captures,
                            ArrayAttr{}, ArrayAttr{});
  };

  Block *wait = addBlock();
  Block *sample = addBlock();
  emitBranch(wait);
  setCurrent(wait);
  if (failed(emitEventSuspend(clock, sample)))
    return failure();
  // The clock occurrence samples in Preponed; bounded evaluation and monitor
  // bookkeeping resume in Observed.
  wait->getTerminator()->setAttr(
      "resume_region", sim::EventRegionAttr::get(function.getContext(),
                                                 sim::EventRegion::Observed));
  setCurrent(sample);
  Value state = zero;
  if (stateStorage)
    state =
        sim::SimRefLoadOp::create(builder, location, stateType, stateStorage);

  if (disable) {
    FailureOr<Value> currentDisable = lowerExpression(disable);
    if (failed(currentDisable))
      return failure();
    FailureOr<Value> disabled =
        truthValue(*currentDisable, getSemanticLocation(disable));
    if (failed(disabled))
      return failure();
    Block *cancelSample = addBlock();
    Block *evaluateSample = addBlock();
    cf::CondBranchOp::create(builder, getSemanticLocation(disable), *disabled,
                             cancelSample, ValueRange{}, evaluateSample,
                             ValueRange{});
    setCurrent(cancelSample);
    sim::SimRefStoreOp::create(builder, getSemanticLocation(disable), zero,
                               stateStorage);
    Value epoch = sim::SimRefLoadOp::create(
        builder, getSemanticLocation(disable), stateType, disableEpoch);
    Value one =
        arith::ConstantOp::create(builder, getSemanticLocation(disable),
                                  stateType, builder.getI64IntegerAttr(1));
    Value nextEpoch = arith::AddIOp::create(
        builder, getSemanticLocation(disable), epoch, one);
    sim::SimRefStoreOp::create(builder, getSemanticLocation(disable), nextEpoch,
                               disableEpoch);
    cf::BranchOp::create(builder, getSemanticLocation(disable), wait);
    setCurrent(evaluateSample);
  }

  bool savedSampleAssertionValues = sampleAssertionValues;
  sampleAssertionValues = true;
  Operation *savedSampledClock = activeSampledClock;
  activeSampledClock = clock;
  llvm::scope_exit restoreSampling([&] {
    sampleAssertionValues = savedSampleAssertionValues;
    activeSampledClock = savedSampledClock;
  });

  llvm::DenseMap<Operation *, Value> predicateCache;
  auto conditionalResult = [&](Value condition, bool passed) -> LogicalResult {
    if (!observable)
      return success();
    Block *report = addBlock();
    Block *continuation = addBlock();
    cf::CondBranchOp::create(builder, location, condition, report, ValueRange{},
                             continuation, ValueRange{});
    setCurrent(report);
    scheduleResult(passed);
    emitBranch(continuation);
    setCurrent(continuation);
    return success();
  };
  auto evaluateAge = [&](ArrayRef<Operation *> predicates) -> FailureOr<Value> {
    Value result = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    for (Operation *predicate : predicates) {
      Value truth;
      auto found = predicateCache.find(predicate);
      if (found != predicateCache.end()) {
        truth = found->second;
      } else {
        FailureOr<Value> value = lowerExpression(predicate);
        if (failed(value))
          return failure();
        FailureOr<Value> converted =
            truthValue(*value, getSemanticLocation(predicate));
        if (failed(converted))
          return failure();
        truth = *converted;
        predicateCache[predicate] = truth;
      }
      result = arith::AndIOp::create(builder, location, result, truth);
    }
    return result;
  };

  Value nextState = zero;
  uint64_t firstActiveAge = implication && nonoverlapped ? 0 : 1;
  for (uint64_t age = firstActiveAge; age < sequence.ages.size(); ++age) {
    Value mask = arith::ConstantOp::create(
        builder, location, stateType,
        builder.getI64IntegerAttr(uint64_t{1} << age));
    Value presentBits = arith::AndIOp::create(builder, location, state, mask);
    Value active = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, presentBits, zero);
    FailureOr<Value> matches = evaluateAge(sequence.ages[age]);
    if (failed(matches))
      return failure();
    Value fails = arith::AndIOp::create(
        builder, location, active,
        arith::XOrIOp::create(
            builder, location, *matches,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true))));
    if (failed(conditionalResult(fails, false)))
      return failure();
    Value advances = arith::AndIOp::create(builder, location, active, *matches);
    if (age + 1 == sequence.ages.size()) {
      if (failed(conditionalResult(advances, true)))
        return failure();
    } else {
      Value nextMask = arith::ConstantOp::create(
          builder, location, stateType,
          builder.getI64IntegerAttr(uint64_t{1} << (age + 1)));
      Value advancedBit =
          arith::SelectOp::create(builder, location, advances, nextMask, zero);
      nextState =
          arith::OrIOp::create(builder, location, nextState, advancedBit);
    }
  }

  FailureOr<Value> starts = implication ? [&]() -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(antecedent);
    if (failed(value))
      return failure();
    return truthValue(*value, getSemanticLocation(antecedent));
  }()
      : evaluateAge(sequence.ages.front());
  if (failed(starts))
    return failure();

  if (implication) {
    Value vacuous = arith::XOrIOp::create(
        builder, location, *starts,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    // A property assertion succeeds vacuously when its antecedent is false,
    // but a cover directive records only a nonvacuous match.
    if (!cover && failed(conditionalResult(vacuous, true)))
      return failure();
    if (nonoverlapped) {
      Value firstMask = arith::ConstantOp::create(builder, location, stateType,
                                                  builder.getI64IntegerAttr(1));
      Value started =
          arith::SelectOp::create(builder, location, *starts, firstMask, zero);
      nextState = arith::OrIOp::create(builder, location, nextState, started);
    } else {
      FailureOr<Value> first = evaluateAge(sequence.ages.front());
      if (failed(first))
        return failure();
      Value matched = arith::AndIOp::create(builder, location, *starts, *first);
      Value failedStart = arith::AndIOp::create(
          builder, location, *starts,
          arith::XOrIOp::create(
              builder, location, *first,
              arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(true))));
      if (failed(conditionalResult(failedStart, false)))
        return failure();
      if (sequence.ages.size() == 1) {
        if (failed(conditionalResult(matched, true)))
          return failure();
      } else {
        Value nextMask = arith::ConstantOp::create(
            builder, location, stateType, builder.getI64IntegerAttr(2));
        Value started =
            arith::SelectOp::create(builder, location, matched, nextMask, zero);
        nextState = arith::OrIOp::create(builder, location, nextState, started);
      }
    }
  } else {
    // The age-zero attempt is evaluated directly every clock. Older attempts
    // are represented by the state bits handled above.
    Value failedStart = arith::XOrIOp::create(
        builder, location, *starts,
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    if (failed(conditionalResult(failedStart, false)))
      return failure();
    if (sequence.ages.size() == 1) {
      if (failed(conditionalResult(*starts, true)))
        return failure();
    } else {
      Value nextMask = arith::ConstantOp::create(builder, location, stateType,
                                                 builder.getI64IntegerAttr(2));
      Value started =
          arith::SelectOp::create(builder, location, *starts, nextMask, zero);
      nextState = arith::OrIOp::create(builder, location, nextState, started);
    }
  }

  if (stateStorage)
    sim::SimRefStoreOp::create(builder, location, nextState, stateStorage);
  cf::BranchOp::create(builder, location, wait);
  return success();
}

} // namespace obelisk::simlowering
