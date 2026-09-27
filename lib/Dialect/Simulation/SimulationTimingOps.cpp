//===- SimulationTimingOps.cpp - Time, suspension, and output op verifiers ===//
//
// Verifiers for time and event operations, the suspension ops and their
// branch/continuation interfaces, and the display and file operations.
//
//===----------------------------------------------------------------------===//

#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/OutputItemFlags.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk::sim {

Operation::operand_range SimCovergroupClockEventRegisterOp::getPrimaries() {
  return getValues().take_front(
      std::min<size_t>(getEdges().size(), getValues().size()));
}

Operation::operand_range SimCovergroupClockEventRegisterOp::getConditions() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getValues().size());
  size_t begin = std::min<size_t>(getValues().size(), primaryCount * 2);
  size_t available = getValues().size() - begin;
  size_t conditionCount =
      getConditionCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getConditionCount(), available);
  return getValues().slice(begin, conditionCount);
}

Operation::operand_range SimCovergroupClockEventRegisterOp::getInitialValues() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getValues().size());
  size_t available = getValues().size() - primaryCount;
  return getValues().slice(primaryCount, std::min(primaryCount, available));
}

Value SimCovergroupClockEventRegisterOp::getSampler() {
  size_t index = getPrimaries().size() + getInitialValues().size() +
                 getConditions().size();
  return index < getValues().size() ? getValues()[index] : Value{};
}

LogicalResult SimCovergroupClockEventRegisterOp::verify() {
  if (getConditionCountAttr().getValue().isNegative())
    return emitOpError("condition count must be nonnegative");
  if (getPrimaries().empty() || getPrimaries().size() > 64)
    return emitOpError("requires between one and 64 event primaries");
  if (getInitialValues().size() != getPrimaries().size() ||
      getConditions().size() != static_cast<uint64_t>(getConditionCount()) ||
      getValues().size() != getPrimaries().size() + getInitialValues().size() +
                                getConditions().size() + 1)
    return emitOpError(
        "operand inventory must end with exactly one sampler observer");
  if (getEdges().size() != getPrimaries().size() ||
      getConditionIndices().size() != getPrimaries().size())
    return emitOpError("requires one edge and condition index per primary");

  SmallVector<bool> usedConditions(getConditions().size(), false);
  int32_t nextCondition = 0;
  for (auto [index, primary, initial, edge, conditionIndex] :
       llvm::enumerate(getPrimaries(), getInitialValues(), getEdges(),
                       getConditionIndices())) {
    auto observer = dyn_cast<ObserverType>(primary.getType());
    if (!observer)
      return emitOpError("event primaries must be observer handles");
    if (initial.getType() != observer.getResultType())
      return emitOpError() << "initial value #" << index
                           << " does not match its primary observer result";
    if (edge < static_cast<int32_t>(EdgeKind::Change) ||
        edge > static_cast<int32_t>(EdgeKind::Both))
      return emitOpError("contains an invalid event edge");
    if (conditionIndex < -1 ||
        (conditionIndex >= 0 &&
         static_cast<uint64_t>(conditionIndex) >= getConditions().size()))
      return emitOpError("contains an invalid condition index");
    if (conditionIndex >= 0) {
      if (conditionIndex != nextCondition++)
        return emitOpError(
            "condition indices must be in canonical ascending order");
      if (usedConditions[conditionIndex])
        return emitOpError("a condition may belong to only one event");
      usedConditions[conditionIndex] = true;
    }
  }
  if (llvm::any_of(getConditions(), [&](Value condition) {
        auto observer = dyn_cast<ObserverType>(condition.getType());
        return !observer || !observer.getResultType().isSignlessInteger(1);
      }))
    return emitOpError("event conditions must be i1 observer handles");
  if (llvm::is_contained(usedConditions, false))
    return emitOpError("contains an unreferenced condition handle");

  Value sampler = getSampler();
  auto observer =
      sampler ? dyn_cast<ObserverType>(sampler.getType()) : ObserverType{};
  auto binding = sampler ? sampler.getDefiningOp<SimObserverBindOp>()
                         : SimObserverBindOp{};
  if (!observer || !observer.getResultType().isSignlessInteger(1) || !binding ||
      !binding.getDependencies().empty())
    return emitOpError(
        "sampler must be a dependency-free i1 observer.bind token");
  if (binding.getCaptures().empty() ||
      binding.getCaptures().front() != getHandle())
    return emitOpError(
        "sampler observer must capture the registered covergroup handle first");
  auto evaluator = SymbolTable::lookupNearestSymbolFrom<SimFuncOp>(
      binding, binding.getEvaluatorAttr());
  if (!evaluator || evaluator.getEntryKind() != EntryKind::Observer ||
      SymbolTable::getSymbolVisibility(evaluator) !=
          SymbolTable::Visibility::Private ||
      !evaluator->hasAttr("simulation.covergroup_event_sample_evaluator"))
    return emitOpError(
        "sampler must name a private covergroup event sample evaluator");
  if (getStrobe() !=
      evaluator->hasAttr("simulation.covergroup_strobe_sample_evaluator"))
    return emitOpError(
        "strobe policy must match the covergroup sample evaluator");
  if (getStrobe() && failed(verifyPostponedReadOnly(evaluator)))
    return failure();

  SimFuncOp function = (*this)->getParentOfType<SimFuncOp>();
  if (!function || function.getEntryKind() != EntryKind::Fork ||
      SymbolTable::getSymbolVisibility(function) !=
          SymbolTable::Visibility::Private ||
      !function->hasAttr("internal") ||
      !::obelisk::schedule::has<::obelisk::schedule::Field::DetachedControls>(
          function) ||
      !::obelisk::schedule::has<::obelisk::schedule::Field::PrimeOnSpawn>(
          function) ||
      !::obelisk::schedule::has<
          ::obelisk::schedule::Field::CovergroupClockingSampler>(function) ||
      function->hasAttr("simulation.multiclock_sequence_coordinator") ||
      function->hasAttr("simulation.timing_check_coordinator"))
    return emitOpError(
        "requires a private detached primed covergroup clocking owner");
  unsigned registrations = 0;
  function.walk([&](SimCovergroupClockEventRegisterOp) { ++registrations; });
  if (registrations != 1)
    return emitOpError(
        "owner must contain exactly one clock-event registration");
  return success();
}

LogicalResult SimCovergroupBlockEventRegisterOp::verify() {
  if (getTargetIds().empty() || getTargetIds().size() > UINT32_MAX)
    return emitOpError("requires between one and UINT32_MAX block events");
  if (getTargetIds().size() != getEventKinds().size())
    return emitOpError("requires one boundary kind per target ID");
  for (auto [target, kind] : llvm::zip_equal(getTargetIds(), getEventKinds())) {
    if (target <= 0)
      return emitOpError("block-event target IDs must be positive");
    if (kind < 0 || kind > 1)
      return emitOpError("block-event kind must be begin (0) or end (1)");
  }
  auto observer = dyn_cast<ObserverType>(getSampler().getType());
  auto binding = getSampler().getDefiningOp<SimObserverBindOp>();
  if (!observer || !observer.getResultType().isSignlessInteger(1) || !binding ||
      !binding.getDependencies().empty())
    return emitOpError(
        "sampler must be a dependency-free i1 observer.bind token");
  if (binding.getCaptures().empty() ||
      binding.getCaptures().front() != getHandle())
    return emitOpError(
        "sampler observer must capture the registered covergroup handle first");
  auto evaluator = SymbolTable::lookupNearestSymbolFrom<SimFuncOp>(
      binding, binding.getEvaluatorAttr());
  if (!evaluator || evaluator.getEntryKind() != EntryKind::Observer ||
      SymbolTable::getSymbolVisibility(evaluator) !=
          SymbolTable::Visibility::Private ||
      !evaluator->hasAttr(
          "simulation.covergroup_block_event_sample_evaluator"))
    return emitOpError(
        "sampler must name a private covergroup block-event sample evaluator");
  return success();
}

LogicalResult SimCovergroupBlockEventFireOp::verify() {
  if (getTargetIdAttr().getValue().isNegative() ||
      getTargetIdAttr().getValue().isZero())
    return emitOpError("block-event target ID must be positive");
  return success();
}

LogicalResult SimTimeConstantOp::verify() {
  if (getValueAttr().getValue().isNegative())
    return emitOpError("simulation time must be nonnegative");
  return success();
}

LogicalResult SimTimeScaleOp::verify() {
  if (!getInput().getType().isSignlessInteger(64))
    return emitOpError("input must be a normalized signless i64");
  if (getScaleAttr().getValue().isNegative() ||
      getScaleAttr().getValue().isZero())
    return emitOpError("tick scale must be positive");
  return success();
}

LogicalResult SimTimeToRealOp::verify() {
  if (!getScaleAttr().getValue().isStrictlyPositive())
    return emitOpError("tick scale must be positive");
  return success();
}

LogicalResult SimTimeFromRealOp::verify() {
  if (!getScaleAttr().getValue().isStrictlyPositive())
    return emitOpError("tick scale must be positive");
  if (!getQuantumAttr().getValue().isStrictlyPositive())
    return emitOpError("tick quantum must be positive");
  if (!getScaleAttr().getValue().urem(getQuantumAttr().getValue()).isZero())
    return emitOpError("tick quantum must divide the tick scale");
  return success();
}

LogicalResult SimEventTriggerOp::verify() {
  if (getDelay() && !getNonblocking())
    return emitOpError("a delayed named-event trigger must be nonblocking");
  if (getReplaceable() && !getNonblocking())
    return emitOpError("a replaceable named-event timer must be nonblocking");
  return success();
}

OpFoldResult SimTimeConstantOp::fold(FoldAdaptor adaptor) {
  return adaptor.getValueAttr();
}

OpFoldResult SimTimeAddOp::fold(FoldAdaptor adaptor) {
  auto lhs = dyn_cast_or_null<IntegerAttr>(adaptor.getLhs());
  auto rhs = dyn_cast_or_null<IntegerAttr>(adaptor.getRhs());
  if (lhs && lhs.getValue().isZero())
    return getRhs();
  if (rhs && rhs.getValue().isZero())
    return getLhs();
  if (!lhs || !rhs)
    return {};
  bool overflow = false;
  APInt sum = lhs.getValue().sadd_ov(rhs.getValue(), overflow);
  if (overflow || sum.isNegative())
    return {};
  return IntegerAttr::get(lhs.getType(), sum);
}

LogicalResult verifyContinuation(Operation *op, ValueRange continuationOperands,
                                 Block *continuation) {
  if (!continuation)
    return op->emitOpError("requires a continuation successor");
  if (continuationOperands.getTypes() != continuation->getArgumentTypes())
    return op->emitOpError(
        "continuation operand types must match successor block arguments");
  auto function = op->getParentOfType<SimFuncOp>();
  if (!function || continuation->getParent() != &function.getBody())
    return op->emitOpError("continuation must be a block in the same function");
  if (continuation == &function.getBody().front())
    return op->emitOpError("continuation must not target the entry block");
  if (auto resume =
          op->template getAttrOfType<EventRegionAttr>("resume_region")) {
    EventRegion region = resume.getValue();
    if (region != EventRegion::Active && region != EventRegion::Observed &&
        region != EventRegion::Reactive && region != EventRegion::Postponed)
      return op->emitOpError(
          "resume region must be an executable process home region");
  }
  return success();
}

template <typename SuspendOp>
static SuccessorOperands makeContinuationSuccessorOperands(SuspendOp op,
                                                           unsigned index) {
  assert(index == 0 && "suspension operations have one successor");
  return SuccessorOperands(op.getContinuationOperandsMutable());
}

SuccessorOperands SimSuspendDelayOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendChangeOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendEdgeOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendEdgeIffOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendLevelOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendAnyOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendClockSetOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendEventOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendEventOrderOp::getSuccessorOperands(unsigned index) {
  assert(index == 0 && "ordered event suspension has one successor");
  return SuccessorOperands(getContinuationOperandsMutable());
}
SuccessorOperands SimSuspendMailboxOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendSemaphoreOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendObserveOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendForeverOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendAwaitOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendJoinOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimSuspendChildrenOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimTaskCallOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands
SimClassVirtualTaskCallOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}
SuccessorOperands SimProcessControlOp::getSuccessorOperands(unsigned index) {
  return makeContinuationSuccessorOperands(*this, index);
}

LogicalResult SimProcessControlOp::verify() {
  auto function = getOperation()->getParentOfType<SimFuncOp>();
  if (!function)
    return emitOpError("must be nested in simulation.func");
  if (function.getEntryKind() == EntryKind::Observer)
    return emitOpError("is not permitted in an observer entry");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

LogicalResult SimSuspendDelayOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendChangeOp::verify() {
  if (!isa<RefType, NetType, DriverType, ManagedWatchType>(
          getWatched().getType()))
    return emitOpError(
        "watched value must be a ref, net, driver, or managed-watch handle");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendEdgeOp::verify() {
  if (!isa<RefType, NetType>(getWatched().getType()))
    return emitOpError("watched value must be a ref or net handle");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendEdgeIffOp::verify() {
  if (!isa<RefType, NetType>(getWatched().getType()))
    return emitOpError("watched value must be a ref or net handle");
  if (!isa<RefType, NetType>(getCondition().getType()))
    return emitOpError("condition must be a ref or net handle");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendLevelOp::verify() {
  if (!isa<RefType, NetType>(getWatched().getType()))
    return emitOpError("watched value must be a ref or net handle");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendAnyOp::verify() {
  if (getEdges().size() > getNumOperands())
    return emitOpError("edge inventory exceeds the operand inventory");
  if (getWatched().empty())
    return emitOpError("requires at least one watched handle");
  if (getEdges().size() != getWatched().size())
    return emitOpError("requires one edge kind per watched handle");
  for (auto [watched, edge] : llvm::zip(getWatched(), getEdges())) {
    if (!isa<RefType, NetType, DriverType, ManagedWatchType>(watched.getType()))
      return emitOpError(
          "watched values must be ref, net, driver, or managed-watch handles");
    if (isa<ManagedWatchType>(watched.getType()) &&
        edge != static_cast<int32_t>(EdgeKind::Change))
      return emitOpError("managed watches only support change events");
    if (edge < static_cast<int32_t>(EdgeKind::Change) ||
        edge > static_cast<int32_t>(EdgeKind::Both))
      return emitOpError("contains an invalid edge kind");
  }
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

Operation::operand_range SimSuspendAnyOp::getWatched() {
  return getValues().take_front(
      std::min<size_t>(getEdges().size(), getNumOperands()));
}

Operation::operand_range SimSuspendAnyOp::getContinuationOperands() {
  return getValues().drop_front(
      std::min<size_t>(getEdges().size(), getNumOperands()));
}

MutableOperandRange SimSuspendAnyOp::getContinuationOperandsMutable() {
  unsigned watchedCount = std::min<size_t>(getEdges().size(), getNumOperands());
  return MutableOperandRange(getOperation(), watchedCount,
                             getNumOperands() - watchedCount);
}

Operation::operand_range SimSuspendClockSetOp::getPrimaries() {
  size_t count = std::min<size_t>(getEdges().size(), getNumOperands());
  return getValues().take_front(count);
}

Operation::operand_range SimSuspendClockSetOp::getConditions() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t count = getConditionCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getConditionCount(),
                                          getNumOperands() - primaryCount);
  return getValues().slice(primaryCount, count);
}

Operation::operand_range SimSuspendClockSetOp::getContinuationOperands() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t conditionCount =
      getConditionCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getConditionCount(),
                               getNumOperands() - primaryCount);
  return getValues().drop_front(primaryCount + conditionCount);
}

MutableOperandRange SimSuspendClockSetOp::getContinuationOperandsMutable() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t conditionCount =
      getConditionCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getConditionCount(),
                               getNumOperands() - primaryCount);
  size_t begin = primaryCount + conditionCount;
  return MutableOperandRange(getOperation(), begin, getNumOperands() - begin);
}

LogicalResult SimSuspendClockSetOp::verify() {
  if (getConditionCountAttr().getValue().isNegative())
    return emitOpError("condition count must be nonnegative");
  if (getPrimaries().empty() || getPrimaries().size() > 64)
    return emitOpError("requires between one and 64 clock primaries");
  if (getConditions().size() != static_cast<uint64_t>(getConditionCount()))
    return emitOpError("condition count exceeds the operand inventory");
  auto conditionPredicates =
      (*this)->getAttrOfType<DenseI32ArrayAttr>("condition_predicates");
  if (conditionPredicates &&
      static_cast<size_t>(conditionPredicates.size()) != getConditions().size())
    return emitOpError("requires one predicate per clock condition");
  if (getEdges().size() != getPrimaries().size() ||
      getConditionIndices().size() != getPrimaries().size())
    return emitOpError("requires one edge and condition index per primary");
  if (!getOccurrenceSiteAttr().getValue().isStrictlyPositive() ||
      getOccurrenceSite() > UINT32_MAX)
    return emitOpError("requires a positive 32-bit occurrence site");
  SmallVector<bool> usedConditions(getConditions().size(), false);
  int32_t nextCondition = 0;
  for (auto [primary, edge, conditionIndex] :
       llvm::zip_equal(getPrimaries(), getEdges(), getConditionIndices())) {
    if (!isa<RefType, NetType, DriverType>(primary.getType()))
      return emitOpError("clock primaries must be direct signal handles");
    bool standardEdge = edge >= static_cast<int32_t>(EdgeKind::Change) &&
                        edge <= static_cast<int32_t>(EdgeKind::Both);
    bool customEdge = (edge & ~0x3f) == 0x100 && (edge & 0x3f) != 0;
    if (!standardEdge && !customEdge)
      return emitOpError("contains an invalid edge kind");
    if (conditionIndex < -1 ||
        (conditionIndex >= 0 &&
         static_cast<uint64_t>(conditionIndex) >= getConditions().size()))
      return emitOpError("contains an invalid condition index");
    if (conditionIndex >= 0) {
      if (conditionIndex != nextCondition++)
        return emitOpError(
            "condition indices must be in canonical ascending order");
      if (usedConditions[conditionIndex])
        return emitOpError("a condition may belong to only one clock");
      usedConditions[conditionIndex] = true;
    }
  }
  for (Value condition : getConditions()) {
    if (isa<RefType, NetType, DriverType>(condition.getType()))
      continue;
    auto observer = dyn_cast<ObserverType>(condition.getType());
    if (!observer || getPackedWidth(observer.getResultType()) != 1)
      return emitOpError(
          "clock conditions must be direct signal handles or one-bit "
          "observer tokens");
    if (!condition.getDefiningOp<SimObserverBindOp>())
      return emitOpError("clock condition observer must be produced by "
                         "observer.bind");
  }
  if (conditionPredicates)
    for (int32_t predicate : conditionPredicates.asArrayRef())
      if (predicate < 0 || predicate > 9)
        return emitOpError("contains an invalid clock condition predicate");
  if (llvm::is_contained(usedConditions, false))
    return emitOpError("contains an unreferenced condition handle");
  auto function = (*this)->getParentOfType<SimFuncOp>();
  bool assertionCoordinator =
      function &&
      function->hasAttr("simulation.multiclock_sequence_coordinator") &&
      function.getHomeRegion() == EventRegion::Observed;
  bool timingCheckCoordinator =
      function && function->hasAttr("simulation.timing_check_coordinator") &&
      function.getHomeRegion() == EventRegion::Observed;
  if (getSlotFinalAttr() && !timingCheckCoordinator)
    return emitOpError("slot_final is reserved for a timing-check coordinator");
  if (!function || (!assertionCoordinator && !timingCheckCoordinator) ||
      SymbolTable::getSymbolVisibility(function) !=
          SymbolTable::Visibility::Private ||
      function.getEntryKind() != EntryKind::Always ||
      function.getDomain() != ExecutionDomain::Design)
    return emitOpError(
        "requires a private design-domain assertion or timing-check "
        "coordinator");
  unsigned clockWaits = 0;
  function.walk([&](SimSuspendClockSetOp) { ++clockWaits; });
  if (clockWaits != 1)
    return emitOpError("coordinator must contain exactly one clock-set wait");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

LogicalResult SimClockOccurrenceConsumeOp::verify() {
  if (!getOccurrenceSiteAttr().getValue().isStrictlyPositive() ||
      getOccurrenceSite() > UINT32_MAX)
    return emitOpError("requires a positive 32-bit occurrence site");
  auto function = getOperation()->getParentOfType<SimFuncOp>();
  if (!function)
    return emitOpError("must be nested in simulation.func");
  return success();
}
LogicalResult SimNoChangeUpdateOp::verify() {
  if (!getOccurrenceSiteAttr().getValue().isStrictlyPositive() ||
      getOccurrenceSite() > UINT32_MAX)
    return emitOpError("requires a positive 32-bit occurrence site");
  if (!getOperation()->getParentOfType<SimFuncOp>())
    return emitOpError("must be nested in simulation.func");
  return success();
}
LogicalResult SimSuspendEventOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
Operation::operand_range SimSuspendEventOrderOp::getEvents() {
  return getValues().take_front(
      std::min<size_t>(getEventCount(), getNumOperands()));
}

Operation::operand_range SimSuspendEventOrderOp::getContinuationOperands() {
  return getValues().drop_front(
      std::min<size_t>(getEventCount(), getNumOperands()));
}

MutableOperandRange SimSuspendEventOrderOp::getContinuationOperandsMutable() {
  unsigned eventCount = std::min<size_t>(getEventCount(), getNumOperands());
  return MutableOperandRange(getOperation(), eventCount,
                             getNumOperands() - eventCount);
}

LogicalResult SimSuspendEventOrderOp::verify() {
  if (getEventCount() <= 0)
    return emitOpError("requires at least one event handle");
  if (static_cast<uint64_t>(getEventCount()) > getNumOperands())
    return emitOpError("event inventory exceeds the operand inventory");
  if (llvm::any_of(getEvents(), [](Value event) {
        return !isa<EventType>(event.getType());
      }))
    return emitOpError("ordered values must be event handles");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendMailboxOp::verify() {
  if (llvm::none_of(getContinuationOperands(),
                    [&](Value value) { return value == getMailbox(); }))
    return emitOpError("requires the mailbox as a continuation operand to "
                       "preserve its GC root");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendSemaphoreOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendObserveOp::verify() {
  if (getConditionCountAttr().getValue().isNegative())
    return emitOpError("condition count must be nonnegative");
  if (getPrimaries().empty())
    return emitOpError("requires at least one primary observer");
  if (getConditions().size() != static_cast<uint64_t>(getConditionCount()))
    return emitOpError("condition count exceeds the operand inventory");
  if (getPrimaries().size() != getInitialValues().size() ||
      getPrimaries().size() != getEdges().size() ||
      getPrimaries().size() != getConditionIndices().size())
    return emitOpError(
        "requires one initial value, edge, and condition index per primary");
  for (Value primary : getPrimaries())
    if (!isa<ObserverType>(primary.getType()))
      return emitOpError("primary operands must be observer handles");
  for (Value condition : getConditions())
    if (!isa<ObserverType>(condition.getType()))
      return emitOpError("condition operands must be observer handles");
  SmallVector<bool> usedConditions(getConditions().size(), false);
  for (auto [index, primary, initial, edge, conditionIndex] :
       llvm::enumerate(getPrimaries(), getInitialValues(), getEdges(),
                       getConditionIndices())) {
    auto observer = cast<ObserverType>(primary.getType());
    if (initial.getType() != observer.getResultType())
      return emitOpError() << "initial value #" << index
                           << " does not match its primary observer result";
    if (edge < static_cast<int32_t>(EdgeKind::Change) ||
        edge > static_cast<int32_t>(EdgeKind::Both))
      return emitOpError("contains an invalid edge kind");
    if (conditionIndex < -1 ||
        (conditionIndex >= 0 &&
         static_cast<uint64_t>(conditionIndex) >= getConditions().size()))
      return emitOpError("contains an invalid condition observer index");
    if (conditionIndex >= 0) {
      if (usedConditions[conditionIndex])
        return emitOpError(
            "a condition observer may belong to only one primary clause");
      usedConditions[conditionIndex] = true;
      Type result =
          cast<ObserverType>(getConditions()[conditionIndex].getType())
              .getResultType();
      auto integer = dyn_cast<IntegerType>(result);
      if (!integer || integer.getWidth() != 1)
        return emitOpError("condition observers must return i1");
    }
  }
  if (llvm::is_contained(usedConditions, false))
    return emitOpError("contains an unreferenced condition observer");
  bool concurrentCancel = ::obelisk::schedule::has<
      ::obelisk::schedule::Field::ConcurrentCancelLevelTrue>((*this));
  bool concurrentAbort = ::obelisk::schedule::has<
      ::obelisk::schedule::Field::ConcurrentAbortLevelTrue>((*this));
  if (concurrentCancel && concurrentAbort)
    return emitOpError(
        "cannot be both a concurrent-cancel and concurrent-abort suspension");
  if (concurrentCancel || concurrentAbort) {
    auto function = (*this)->getParentOfType<SimFuncOp>();
    if (!function || !function->hasAttr("internal") ||
        !(concurrentCancel
              ? ::obelisk::schedule::has<
                    ::obelisk::schedule::Field::ConcurrentCancel>(function)
              : ::obelisk::schedule::has<
                    ::obelisk::schedule::Field::ConcurrentAbort>(function)) ||
        !::obelisk::schedule::has<::obelisk::schedule::Field::DetachedControls>(
            function) ||
        !::obelisk::schedule::has<
            ::obelisk::schedule::Field::PrioritySignalResume>(function) ||
        function.getEntryKind() != EntryKind::Fork ||
        function.getHomeRegion() != EventRegion::Reactive)
      return emitOpError() << "concurrent "
                           << (concurrentCancel ? "cancel" : "abort")
                           << " level-true suspension requires an internal "
                              "detached priority concurrent-"
                           << (concurrentCancel ? "cancel" : "abort")
                           << " fork in the reactive region";
    if (getPrimaries().size() != 1 || !getConditions().empty() ||
        getConditionIndices().front() != -1 ||
        getEdges().front() != static_cast<int32_t>(EdgeKind::Posedge))
      return emitOpError() << "concurrent "
                           << (concurrentCancel ? "cancel" : "abort")
                           << " level-true suspension requires one i1 truth "
                              "primary with posedge and no condition";
    auto result = dyn_cast<IntegerType>(
        cast<ObserverType>(getPrimaries().front().getType()).getResultType());
    if (!result || result.getWidth() != 1)
      return emitOpError() << "concurrent "
                           << (concurrentCancel ? "cancel" : "abort")
                           << " level-true suspension requires one i1 truth "
                              "primary with posedge and no condition";
  }
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

Operation::operand_range SimSuspendObserveOp::getPrimaries() {
  size_t count = std::min<size_t>(getEdges().size(), getNumOperands());
  return getValues().take_front(count);
}

Operation::operand_range SimSuspendObserveOp::getInitialValues() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t remaining = getNumOperands() - primaryCount;
  return getValues().slice(primaryCount, std::min(primaryCount, remaining));
}

Operation::operand_range SimSuspendObserveOp::getConditions() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t begin = std::min<size_t>(getNumOperands(), primaryCount * 2);
  size_t count =
      getConditionCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
  return getValues().slice(begin, count);
}

Operation::operand_range SimSuspendObserveOp::getContinuationOperands() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t begin = std::min<size_t>(getNumOperands(), primaryCount * 2);
  if (!getConditionCountAttr().getValue().isNegative())
    begin += std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
  return getValues().drop_front(begin);
}

MutableOperandRange SimSuspendObserveOp::getContinuationOperandsMutable() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t begin = std::min<size_t>(getNumOperands(), primaryCount * 2);
  if (!getConditionCountAttr().getValue().isNegative())
    begin += std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
  return MutableOperandRange(getOperation(), begin, getNumOperands() - begin);
}
LogicalResult SimSuspendForeverOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendAwaitOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendJoinOp::verify() {
  if (getProcessCountAttr().getValue().isNegative() || getProcessCount() == 0)
    return emitOpError("requires at least one child process");
  if (static_cast<uint64_t>(getProcessCount()) > getNumOperands())
    return emitOpError("process count exceeds the operand inventory");
  for (Value process : getProcesses())
    if (!isa<ProcessType>(process.getType()))
      return emitOpError("process prefix must contain only process handles");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

LogicalResult SimSuspendChildrenOp::verify() {
  auto function = getOperation()->getParentOfType<SimFuncOp>();
  if (!function)
    return emitOpError("must be nested in simulation.func");
  if (function.getEntryKind() == EntryKind::Function)
    return emitOpError("is not permitted in a zero-time function entry");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

Operation::operand_range SimSuspendJoinOp::getProcesses() {
  size_t count = getProcessCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getProcessCount(), getNumOperands());
  return getValues().take_front(count);
}

Operation::operand_range SimSuspendJoinOp::getContinuationOperands() {
  size_t count = getProcessCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getProcessCount(), getNumOperands());
  return getValues().drop_front(count);
}

MutableOperandRange SimSuspendJoinOp::getContinuationOperandsMutable() {
  unsigned count =
      getProcessCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getProcessCount(), getNumOperands());
  return MutableOperandRange(getOperation(), count, getNumOperands() - count);
}

static LogicalResult verifyOutputItems(Operation *operation, ValueRange items,
                                       ArrayRef<int32_t> itemFlags, Radix radix,
                                       IntegerAttr timeMultiplier,
                                       bool allowDesignatedFormat) {
  if (!symbolizeRadix(static_cast<uint32_t>(radix)))
    return operation->emitOpError("default radix must be 2, 8, 10, or 16");
  if (timeMultiplier && !timeMultiplier.getValue().isStrictlyPositive())
    return operation->emitOpError("time multiplier must be positive");
  unsigned itemIndex = 0;
  for (auto [logicalIndex, flags] : llvm::enumerate(itemFlags)) {
    if ((flags & OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT) != 0 &&
        (!allowDesignatedFormat || logicalIndex != 0))
      return operation->emitOpError(
          "designated format must be the first string output-format item");
    if ((flags & OBELISK_RT_OUTPUT_ITEM_OMITTED) != 0) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_OMITTED)
        return operation->emitOpError(
            "omitted display items cannot carry other flags");
      continue;
    }
    if (itemIndex == items.size())
      return operation->emitOpError("item flags require more display operands");
    Value item = items[itemIndex++];
    if ((flags & OBELISK_RT_OUTPUT_ITEM_ENUM) != 0) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_ENUM &&
          flags !=
              (OBELISK_RT_OUTPUT_ITEM_ENUM | OBELISK_RT_OUTPUT_ITEM_SIGNED))
        return operation->emitOpError(
            "enum display items may only also carry the signed flag");
      if (!isa<IntegerType, LogicType>(item.getType()) ||
          itemIndex == items.size() ||
          !isa<StringType>(items[itemIndex].getType()))
        return operation->emitOpError(
            "enum display items require a packed value and mnemonic string");
      ++itemIndex;
      continue;
    }
    if ((flags & OBELISK_RT_OUTPUT_ITEM_NET) != 0) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_NET &&
          flags != (OBELISK_RT_OUTPUT_ITEM_NET | OBELISK_RT_OUTPUT_ITEM_SIGNED))
        return operation->emitOpError(
            "net display items may only also carry the signed flag");
      if (!isa<IntegerType, LogicType>(item.getType()) ||
          itemIndex == items.size() ||
          !isa<NetType>(items[itemIndex].getType()))
        return operation->emitOpError(
            "net display items require a packed value and net handle");
      ++itemIndex;
      continue;
    }
    if ((flags & OBELISK_RT_OUTPUT_ITEM_RAW_AGGREGATE) != 0) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_RAW_AGGREGATE)
        return operation->emitOpError(
            "raw aggregate display items cannot carry other flags");
      if (!isa<StringType>(item.getType()) || itemIndex + 1 >= items.size() ||
          !isa<StringType>(items[itemIndex].getType()) ||
          !isa<StringType>(items[itemIndex + 1].getType()))
        return operation->emitOpError(
            "raw aggregate display items require pattern, two-state, and "
            "four-state strings");
      itemIndex += 2;
      continue;
    }
    if (!isa<BytesType, StringType, DynamicArrayType, QueueType, AssocArrayType,
             ClassHandleType, VirtualInterfaceType, ProcessType, IntegerType,
             LogicType>(item.getType()) &&
        !item.getType().isF64())
      return operation->emitOpError(
          "items must be literal bytes, packed integers, or f64 reals; "
          "managed strings, containers, class handles, and virtual-interface "
          "and process handles are also accepted");
    if ((flags & ~OBELISK_RT_OUTPUT_ITEM_ALL) != 0)
      return operation->emitOpError(
          "display item flags contain an unknown bit");
    if ((flags & OBELISK_RT_OUTPUT_ITEM_CONTAINER) != 0 &&
        !isa<DynamicArrayType, QueueType, AssocArrayType>(item.getType()))
      return operation->emitOpError(
          "container display flags require a container operand");
    if ((flags & OBELISK_RT_OUTPUT_ITEM_CLASS) != 0 &&
        !isa<ClassHandleType>(item.getType()))
      return operation->emitOpError(
          "class-handle display flags require a class-handle operand");
    if ((flags & OBELISK_RT_OUTPUT_ITEM_VIRTUAL_INTERFACE) != 0 &&
        !isa<VirtualInterfaceType>(item.getType()))
      return operation->emitOpError(
          "virtual-interface display flags require a virtual-interface "
          "operand");
    if ((flags & OBELISK_RT_OUTPUT_ITEM_PROCESS) != 0 &&
        !isa<ProcessType>(item.getType()))
      return operation->emitOpError(
          "process display flags require a process-handle operand");
    if ((flags & OBELISK_RT_OUTPUT_ITEM_REAL) != 0 && !item.getType().isF64())
      return operation->emitOpError(
          "real display items must have f64 operands");
    if ((flags & OBELISK_RT_OUTPUT_ITEM_REAL) == 0 && item.getType().isF64())
      return operation->emitOpError("f64 display operands must be marked real");
    if ((flags & OBELISK_RT_OUTPUT_ITEM_REAL_TIME) != 0 &&
        (flags & OBELISK_RT_OUTPUT_ITEM_REAL) == 0)
      return operation->emitOpError(
          "realtime display items must also be marked real");
    if ((flags &
         (OBELISK_RT_OUTPUT_ITEM_REAL | OBELISK_RT_OUTPUT_ITEM_SIGNED)) ==
        (OBELISK_RT_OUTPUT_ITEM_REAL | OBELISK_RT_OUTPUT_ITEM_SIGNED))
      return operation->emitOpError(
          "real display items cannot be marked signed");
    if (isa<BytesType>(item.getType()) &&
        flags == OBELISK_RT_OUTPUT_ITEM_SIGNED)
      return operation->emitOpError("literal byte items cannot be signed");
    if (isa<BytesType>(item.getType()) && flags != 0 &&
        (!allowDesignatedFormat ||
         flags != OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT))
      return operation->emitOpError(
          "only a string output-format literal may carry the designated "
          "format flag");
    if (isa<StringType>(item.getType())) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_STRING &&
          flags != (OBELISK_RT_OUTPUT_ITEM_STRING |
                    OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT) &&
          flags !=
              (OBELISK_RT_OUTPUT_ITEM_STRING | OBELISK_RT_OUTPUT_ITEM_FORMAT))
        return operation->emitOpError(
            "managed string items require the string flag and may also carry "
            "the format flag");
    } else if (isa<DynamicArrayType, QueueType, AssocArrayType>(
                   item.getType())) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_CONTAINER)
        return operation->emitOpError(
            "managed container items require only the container flag");
    } else if (isa<ClassHandleType>(item.getType())) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_CLASS)
        return operation->emitOpError(
            "class-handle items require only the class-handle flag");
    } else if (isa<VirtualInterfaceType>(item.getType())) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_VIRTUAL_INTERFACE)
        return operation->emitOpError(
            "virtual-interface items require only the virtual-interface "
            "flag");
    } else if (isa<ProcessType>(item.getType())) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_PROCESS)
        return operation->emitOpError(
            "process-handle items require only the process flag");
    } else if (item.getType().isF64()) {
      if (flags != OBELISK_RT_OUTPUT_ITEM_REAL &&
          flags !=
              (OBELISK_RT_OUTPUT_ITEM_REAL | OBELISK_RT_OUTPUT_ITEM_REAL_TIME))
        return operation->emitOpError(
            "f64 items require the real flag and may carry the realtime flag");
    } else if (!isa<BytesType>(item.getType()) && flags != 0 &&
               flags != OBELISK_RT_OUTPUT_ITEM_SIGNED) {
      return operation->emitOpError(
          "packed integer items may only carry the signed flag");
    }
  }
  if (itemIndex != items.size())
    return operation->emitOpError("requires one flag entry per display item");
  return success();
}

LogicalResult SimDisplayOp::verify() {
  return verifyOutputItems(*this, getItems(), getItemFlags(), getDefaultRadix(),
                           getTimeMultiplierAttr(), false);
}

LogicalResult SimStringOutputFormatOp::verify() {
  return verifyOutputItems(*this, getItems(), getItemFlags(), getDefaultRadix(),
                           getTimeMultiplierAttr(), true);
}

static LogicalResult verifyPackedFileResult(Operation *operation, Type type) {
  auto integer = dyn_cast<IntegerType>(type);
  if (!integer || integer.getWidth() == 0)
    return operation->emitOpError(
        "packed data result must be a nonzero-width integer");
  return success();
}

LogicalResult SimFileGetlineOp::verify() {
  return verifyPackedFileResult(*this, getData().getType());
}

LogicalResult SimFileReadPackedOp::verify() {
  return verifyPackedFileResult(*this, getData().getType());
}

LogicalResult SimFileReadMemTokenOp::verify() {
  if (getData().getType().getWidth() == 0)
    return emitOpError("data must have nonzero width");
  if (getRadix() != Radix::Binary && getRadix() != Radix::Hex)
    return emitOpError("radix must be 2 or 16");
  return success();
}

} // namespace obelisk::sim
