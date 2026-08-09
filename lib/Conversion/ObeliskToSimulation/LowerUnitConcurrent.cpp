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
      uint64_t offset = index == 0 ? 0 : minimum.getInt();
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
  if (disable) {
    emitError(getSemanticLocation(disable))
        << "disable iff requires asynchronous monitor cancellation; it is "
           "not lowered as a sampled property term";
    return failure();
  }

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
    sim::SimSpawnOp::create(builder, report->location,
                            report->function.getSymNameAttr(), report->captures,
                            ArrayAttr{}, ArrayAttr{});
  };

  Type stateType = builder.getI64Type();
  Value zero = arith::ConstantOp::create(builder, location, stateType,
                                         builder.getI64IntegerAttr(0));
  bool needsState = sequence.ages.size() > 1 || (implication && nonoverlapped);
  Value stateStorage;
  if (needsState)
    stateStorage = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), stateType),
        zero);
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

  sampleAssertionValues = true;
  Operation *savedSampledClock = activeSampledClock;
  activeSampledClock = clock;
  llvm::scope_exit restoreSampling([&] {
    sampleAssertionValues = false;
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
