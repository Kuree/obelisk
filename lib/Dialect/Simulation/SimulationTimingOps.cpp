//===- SimulationTimingOps.cpp - Time, suspension, and output op verifiers ===//
//
// Verifiers for time and event operations, the suspension ops and their
// branch/continuation interfaces, and the display and file operations.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
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

LogicalResult verifyContinuation(Operation *op,
                                        ValueRange continuationOperands,
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
SuccessorOperands SimSuspendEventOp::getSuccessorOperands(unsigned index) {
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

LogicalResult SimSuspendDelayOp::verify() {
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}
LogicalResult SimSuspendChangeOp::verify() {
  if (!isa<RefType, NetType>(getWatched().getType()))
    return emitOpError("watched value must be a ref or net handle");
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
  if (getEdge() == EdgeKind::Change)
    return emitOpError("primary event must request an edge");
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
    if (!isa<RefType, NetType>(watched.getType()))
      return emitOpError("watched values must be ref or net handles");
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
LogicalResult SimSuspendEventOp::verify() {
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
  if ((*this)->hasAttr("obelisk_sim.concurrent_cancel_level_true")) {
    auto function = (*this)->getParentOfType<SimFuncOp>();
    if (!function || !function->hasAttr("internal") ||
        !function->hasAttr("obelisk_sim.concurrent_cancel") ||
        !function->hasAttr("obelisk_sim.detached_controls") ||
        !function->hasAttr("obelisk_sim.priority_signal_resume") ||
        function.getEntryKind() != EntryKind::Fork ||
        function.getHomeRegion() != EventRegion::Reactive)
      return emitOpError(
          "concurrent cancel level-true suspension requires an internal "
          "detached priority concurrent-cancel fork in the reactive region");
    if (getPrimaries().size() != 1 || !getConditions().empty() ||
        getConditionIndices().front() != -1 ||
        getEdges().front() != static_cast<int32_t>(EdgeKind::Posedge))
      return emitOpError(
          "concurrent cancel level-true suspension requires one i1 truth "
          "primary with posedge and no condition");
    auto result = dyn_cast<IntegerType>(
        cast<ObserverType>(getPrimaries().front().getType()).getResultType());
    if (!result || result.getWidth() != 1)
      return emitOpError(
          "concurrent cancel level-true suspension requires one i1 truth "
          "primary with posedge and no condition");
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
  if (auto converted = (*this)->getAttrOfType<IntegerAttr>(
          "obelisk.coro.condition_operand_begin")) {
    size_t begin = std::min<uint64_t>(converted.getValue().getZExtValue(),
                                      getNumOperands());
    size_t count =
        getConditionCountAttr().getValue().isNegative()
            ? 0
            : std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
    return getValues().slice(begin, count);
  }
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t begin = std::min<size_t>(getNumOperands(), primaryCount * 2);
  size_t count =
      getConditionCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
  return getValues().slice(begin, count);
}

Operation::operand_range SimSuspendObserveOp::getContinuationOperands() {
  if (auto converted = (*this)->getAttrOfType<IntegerAttr>(
          "obelisk.coro.continuation_operand_begin")) {
    size_t begin = std::min<uint64_t>(converted.getValue().getZExtValue(),
                                      getNumOperands());
    return getValues().drop_front(begin);
  }
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t begin = std::min<size_t>(getNumOperands(), primaryCount * 2);
  if (!getConditionCountAttr().getValue().isNegative())
    begin += std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
  return getValues().drop_front(begin);
}

MutableOperandRange SimSuspendObserveOp::getContinuationOperandsMutable() {
  if (auto converted = (*this)->getAttrOfType<IntegerAttr>(
          "obelisk.coro.continuation_operand_begin")) {
    size_t begin = std::min<uint64_t>(converted.getValue().getZExtValue(),
                                      getNumOperands());
    return MutableOperandRange(getOperation(), begin, getNumOperands() - begin);
  }
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
    return emitOpError("must be nested in obelisk_sim.func");
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

LogicalResult SimDisplayOp::verify() {
  int64_t radix = getDefaultRadix();
  if (radix != 2 && radix != 8 && radix != 10 && radix != 16)
    return emitOpError("default radix must be 2, 8, 10, or 16");
  if (IntegerAttr multiplier = getTimeMultiplierAttr())
    if (!multiplier.getValue().isStrictlyPositive())
      return emitOpError("time multiplier must be positive");
  unsigned itemIndex = 0;
  for (int32_t flags : getItemFlags()) {
    if ((flags & 2) != 0) {
      if (flags != 2)
        return emitOpError("omitted display items cannot carry other flags");
      continue;
    }
    if (itemIndex == getItems().size())
      return emitOpError("item flags require more display operands");
    Value item = getItems()[itemIndex++];
    if (!isa<BytesType, StringType, DynamicArrayType, QueueType, AssocArrayType,
             IntegerType, LogicType>(item.getType()) &&
        !item.getType().isF64())
      return emitOpError(
          "items must be literal bytes, packed integers, or f64 reals; "
          "managed strings and containers are also accepted");
    if ((flags & ~31) != 0)
      return emitOpError("display item flags contain an unknown bit");
    if ((flags & 16) != 0 &&
        !isa<DynamicArrayType, QueueType, AssocArrayType>(item.getType()))
      return emitOpError("container display flags require a container operand");
    if ((flags & 4) != 0 && !item.getType().isF64())
      return emitOpError("real display items must have f64 operands");
    if ((flags & 4) == 0 && item.getType().isF64())
      return emitOpError("f64 display operands must be marked real");
    if ((flags & 5) == 5)
      return emitOpError("real display items cannot be marked signed");
    if (isa<BytesType>(item.getType()) && flags != 0)
      return emitOpError("literal byte items cannot be signed");
    if (isa<StringType>(item.getType()) && flags != 8)
      return emitOpError(
          "managed string display items require the string flag");
    if (isa<DynamicArrayType, QueueType, AssocArrayType>(item.getType()) &&
        flags != 16)
      return emitOpError(
          "managed container display items require the container flag");
  }
  if (itemIndex != getItems().size())
    return emitOpError("requires one flag entry per display item");
  return success();
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


} // namespace obelisk::sim
