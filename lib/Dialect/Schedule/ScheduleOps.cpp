#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/TypeSwitch.h"
using namespace mlir;
#define GET_OP_CLASSES
#include "obelisk/Dialect/Schedule/ScheduleOps.cpp.inc"
namespace obelisk::schedule {
static LogicalResult verifyNativeUnpackedWord(Operation *op, Type input,
                                              Type result) {
  auto array = dyn_cast<sim::UnpackedArrayType>(input);
  if (!array || !sim::getPackedWidth(array.getElementType()) ||
      !isa<IntegerType, sim::LogicType>(result))
    return op->emitOpError(
        "requires an unpacked array of packed elements and a "
        "packed word result");
  if (auto integer = dyn_cast<IntegerType>(result);
      integer && !integer.isSignless())
    return op->emitOpError("requires a signless integer word");
  auto width = sim::getPackedWidth(result);
  bool logic =
      isa<sim::LogicType>(sim::getPackedScalarType(array.getElementType()));
  auto span = sim::getProvenanceSpan(array);
  if (!width || *width < 2 || *width > 64 || !span || *width > *span ||
      *span > INT64_MAX || logic != isa<sim::LogicType>(result))
    return op->emitOpError(
        "requires a contained native word with the same state domain");
  return success();
}
LogicalResult NativeWordViewOp::verify() {
  return verifyNativeUnpackedWord(*this, getInput().getType().getElementType(),
                                  getResult().getType().getElementType());
}
LogicalResult NativeWordExtractOp::verify() {
  return verifyNativeUnpackedWord(*this, getInput().getType(),
                                  getResult().getType());
}
LogicalResult NativeWordReverseOp::verify() {
  Type type = getInput().getType();
  if (getResult().getType() != type)
    return emitOpError("requires matching operand and result types");
  auto width = sim::getPackedWidth(type);
  if (!isa<IntegerType, sim::LogicType>(type) || !width || *width < 2 ||
      *width > 64)
    return emitOpError("requires a native integer or logic word");
  if (auto integer = dyn_cast<IntegerType>(type);
      integer && !integer.isSignless())
    return emitOpError("requires a signless integer word");
  if (getLaneWidth() < 1 || getLaneWidth() > *width || *width % getLaneWidth())
    return emitOpError("requires a lane width dividing the native word width");
  return success();
}
LogicalResult NativeWordResizeLanesOp::verify() {
  auto wordWidth = [](Type type) -> unsigned {
    if (auto integer = dyn_cast<IntegerType>(type))
      return integer.isSignless() ? integer.getWidth() : 0;
    if (auto logic = dyn_cast<sim::LogicType>(type))
      return logic.getWidth();
    return 0;
  };
  unsigned input = wordWidth(getInput().getType());
  unsigned result = wordWidth(getResult().getType());
  unsigned from = getInputLaneWidth(), to = getResultLaneWidth();
  if (input < 2 || input > 64 || result < 2 || result > 64 ||
      isa<sim::LogicType>(getInput().getType()) !=
          isa<sim::LogicType>(getResult().getType()))
    return emitOpError("requires native words with the same state domain");
  if (!from || !to || input % from || result % to || input / from < 2 ||
      input / from != result / to)
    return emitOpError("requires matching counts of complete lanes");
  return success();
}
LogicalResult NativeWordTableOp::verify() {
  auto laneWidth = [](Type type) -> unsigned {
    if (auto integer = dyn_cast<IntegerType>(type))
      return integer.isSignless() ? integer.getWidth() : 0;
    if (auto logic = dyn_cast<sim::LogicType>(type))
      return logic.getWidth();
    return 0;
  };
  unsigned width = laneWidth(getResult().getType());
  if (width < 2 || width > 64)
    return emitOpError("requires a native integer or logic word result");
  unsigned planes = 0;
  for (Value input : getInputs()) {
    if (laneWidth(input.getType()) != width)
      return emitOpError("requires matching native word operand widths");
    planes += isa<sim::LogicType>(input.getType()) ? 2 : 1;
  }
  if (planes > 6 || getValueTable().getBitWidth() != (1u << planes) ||
      getUnknownTable().getBitWidth() != (1u << planes))
    return emitOpError(
        "requires complete truth tables for at most six input planes");
  if (isa<IntegerType>(getResult().getType()) && !getUnknownTable().isZero())
    return emitOpError("integer results cannot have unknown bits");
  return success();
}
void NativeWordRegionOp::getSuccessorRegions(
    RegionBranchPoint point, SmallVectorImpl<RegionSuccessor> &regions) {
  if (point.isParent())
    regions.emplace_back(&getBody());
  else
    regions.emplace_back(getOperation(), getOperation()->getResults());
}

void NativeWordRegionOp::getRegionInvocationBounds(
    ArrayRef<Attribute>, SmallVectorImpl<InvocationBounds> &bounds) {
  bounds.emplace_back(1, 1);
}

MutableOperandRange
NativeWordYieldOp::getMutableSuccessorOperands(RegionSuccessor) {
  return MutableOperandRange(getOperation(), 0, 0);
}

LogicalResult NativeWordRegionOp::verify() {
  if (getWidth() < 2 || getWidth() > 64)
    return emitOpError("requires a native word width between 2 and 64");
  if (getLaneCount() && (getLaneCount() < 2 || getLaneCount() > getWidth() ||
                         getWidth() % getLaneCount()))
    return emitOpError("requires a lane count dividing the maximum word width");
  if (getBody().empty() || getBody().front().getNumArguments())
    return emitOpError("requires a nonempty CFG without entry arguments");
  for (Block &block : getBody()) {
    for (Operation &op : block) {
      if (op.getNumRegions())
        return emitOpError("does not permit nested regions");
      if (isa<CallOpInterface>(op))
        return emitOpError("does not permit calls");
      if (auto store = dyn_cast<sim::SimRefStoreOp>(op)) {
        auto width = sim::getPackedWidth(store.getValue().getType());
        if (!width || *width < 2 || *width > getWidth() ||
            (getLaneCount() ? *width % getLaneCount() != 0
                            : *width != getWidth()))
          return emitOpError("requires native-width packed stores");
      } else if (!isa<sim::SimRefLoadOp>(&op) && !isMemoryEffectFree(&op)) {
        return emitOpError(
            "permits only packed state accesses and pure computation");
      }
    }
    Operation *end = block.getTerminator();
    if (!isa<NativeWordYieldOp>(end) && !isa<BranchOpInterface>(end))
      return emitOpError("requires CFG branches or word.yield");
  }
  return success();
}
static LogicalResult verifyTransferABI(Operation *op, Type signature,
                                       uint32_t source, uint32_t destination,
                                       ArrayRef<int64_t> offsets) {
  auto type = dyn_cast<FunctionType>(signature);
  if (!type || type.getNumResults() || type.getNumInputs() < 3 ||
      !isa<sim::ContextType>(type.getInput(0)) ||
      offsets.size() != type.getNumInputs() || offsets.front() != -1)
    return op->emitOpError("requires a context and canonical storage captures");
  if (source == 0 || destination == 0 || source >= type.getNumInputs() ||
      destination >= type.getNumInputs() ||
      type.getInput(source) != type.getInput(destination))
    return op->emitOpError("requires matching source and destination captures");
  llvm::DenseSet<int64_t> occupied;
  for (unsigned index = 1; index < type.getNumInputs(); ++index) {
    auto reference = dyn_cast<sim::RefType>(type.getInput(index));
    if (!reference || !sim::getPackedWidth(reference.getElementType()) ||
        offsets[index] < 0 || offsets[index] % 8 != 0 ||
        !occupied.insert(offsets[index]).second)
      return op->emitOpError("requires distinct aligned storage capture slots");
  }
  return success();
}
LogicalResult TransferKernelOp::verify() {
  return verifyTransferABI(*this, getSignature(), getSource(), getDestination(),
                           getCaptureOffsets());
}
LogicalResult TransferActivationOp::verify() {
  return verifyTransferABI(*this, getSignature(), getSource(), getDestination(),
                           getCaptureOffsets());
}
LogicalResult
TransferActivationOp::verifySymbolUses(SymbolTableCollection &symbols) {
  if (auto name = getKernelAttr()) {
    auto kernel =
        symbols.lookupNearestSymbolFrom<TransferKernelOp>(*this, name);
    if (!kernel || kernel.getSignature() != getSignature() ||
        kernel.getSource() != getSource() ||
        kernel.getDestination() != getDestination() ||
        kernel.getCaptureOffsets() != getCaptureOffsets())
      return emitOpError(
          "requires a transfer kernel with the same capture ABI");
  }
  if (!symbols.lookupNearestSymbolFrom<sim::SimFuncOp>(*this, getActorAttr()))
    return emitOpError("requires an original simulation process actor");
  return success();
}
LogicalResult NativeKnownValueOp::verify() {
  Type type = getInput().getType();
  if (!isa<IntegerType>(type) && !sim::getPackedWidth(type) &&
      (!isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type) ||
       !sim::getFixedBitStreamPlan(type)))
    return emitOpError("requires a fixed native bit value");
  return success();
}
LogicalResult NativeReadyRegionOp::verify() {
  if (getBody().empty() || getBody().front().getNumArguments())
    return emitOpError("requires a nonempty CFG without entry arguments");
  for (Block &block : getBody()) {
    for (Operation &op : block)
      if (op.getNumRegions())
        return emitOpError("does not permit nested regions");
    Operation *end = block.getTerminator();
    if (!isa<NativeReadyYieldOp>(end) &&
        (!isa<BranchOpInterface>(end) || !isMemoryEffectFree(end)))
      return emitOpError("requires non-suspending branches or ready.yield");
  }
  return success();
}
LogicalResult NativeNBABatchOp::verify() {
  if (getBody().empty() || getBody().front().getNumArguments())
    return emitOpError("requires one block without arguments");
  Block &body = getBody().front();
  if (body.empty() || !isa<NativeNBAYieldOp>(body.back()))
    return emitOpError("requires an nba.yield terminator");
  int64_t previous = -1;
  for (Operation &operation : body.without_terminator()) {
    auto commit = dyn_cast<NativeNBACommitOp>(operation);
    if (!commit)
      return emitOpError("body permits only nba.commit actions");
    if (static_cast<int64_t>(commit.getRoot()) <= previous)
      return emitOpError("requires distinct roots in increasing order");
    previous = commit.getRoot();
    if (bool(getLayoutAttr()) != bool(commit.getLayoutAttr()))
      return emitOpError("requires layouts on both batch and every commit");
  }
  if (previous == -1)
    return emitOpError("requires at least one commit");
  if (auto layout = getLayoutAttr())
    if (!layout.getBitCount() || !layout.getRootCount() ||
        uint64_t(previous) >= layout.getRootCount())
      return emitOpError("requires a nonempty layout covering every root");
  return success();
}
LogicalResult NativeNBACommitOp::verify() {
  if (getRootAttr().getValue().isNegative() || getRoot() >= UINT32_MAX)
    return emitOpError("root must be between 0 and UINT32_MAX - 1");
  if (auto layout = getLayoutAttr()) {
    auto batch = (*this)->getParentOfType<NativeNBABatchOp>();
    auto parent = batch ? batch.getLayoutAttr() : NBABatchLayoutAttr{};
    if (!parent || !layout.getWidth() ||
        layout.getOffset() > parent.getBitCount() ||
        layout.getWidth() > parent.getBitCount() - layout.getOffset())
      return emitOpError("commit range must lie within the batch state layout");
    if ((!layout.getAccumulator() && layout.getSlots().empty()) ||
        (layout.getAccumulator() && layout.getWidth() > 256))
      return emitOpError(
          "requires slots or an accumulator no wider than 256 bits");
    for (auto attr : layout.getSlots()) {
      auto slot = dyn_cast<NBASlotLayoutAttr>(attr);
      if (!slot || !slot.getWidth() || slot.getWidth() > 64)
        return emitOpError("requires fixed slots between 1 and 64 bits");
      if (auto offset = slot.getConstantOffset()) {
        const APInt &value = offset.getValue();
        if (value.isNegative() || value.getActiveBits() > 64 ||
            value.getZExtValue() > layout.getWidth() ||
            slot.getWidth() > layout.getWidth() - value.getZExtValue())
          return emitOpError("constant slot target must lie within its root");
      }
    }
    for (auto attr : layout.getConsumers()) {
      auto consumer = dyn_cast<NBAConsumerAttr>(attr);
      if (!consumer || !consumer.getWidth() ||
          consumer.getLow() >= layout.getWidth() ||
          consumer.getWidth() > layout.getWidth() - consumer.getLow() ||
          consumer.getBit() >= parent.getCapacity())
        return emitOpError("requires consumers within root and ready capacity");
    }
  }
  return success();
}
LogicalResult NativeTransitionOp::verify() {
  if (getStaticStateAttr().getValue().isNegative() ||
      getStaticState() > UINT32_MAX)
    return emitOpError(
        "static state must fit an unsigned 32-bit root identity");
  if (getWidth() < 1 || getWidth() > 64)
    return emitOpError("transition width must be between 1 and 64");
  if (auto owner = getSourceOwnerAttr()) {
    auto code = owner.getCodeUnit();
    auto continuation = owner.getContinuation();
    if (!code || !continuation || code.getValue().getBitWidth() > 64 ||
        continuation.getValue().getBitWidth() > 64 ||
        continuation.getUInt() > UINT32_MAX)
      return emitOpError("requires a canonical code-unit/continuation owner");
  }
  return success();
}
LogicalResult NativeReadyUpdateOp::verify() {
  if (getCapacity() < 1 || getCapacity() > UINT32_MAX)
    return emitOpError("ready capacity must be between 1 and UINT32_MAX");
  if (getWordAttr().getValue().isNegative() ||
      getWord() >= (getCapacity() + 63) / 64)
    return emitOpError("ready word must name a leaf within capacity");
  return success();
}
void NativeReadyUpdateOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(MemoryEffects::Read::get(),
                       &getOperation()->getOpOperand(0),
                       SideEffects::DefaultResource::get());
  effects.emplace_back(MemoryEffects::Write::get(),
                       &getOperation()->getOpOperand(0),
                       SideEffects::DefaultResource::get());
  effects.emplace_back(MemoryEffects::Read::get(),
                       sim::SchedulerResource::get());
  effects.emplace_back(MemoryEffects::Write::get(),
                       sim::SchedulerResource::get());
}
LogicalResult NativeReadyCommitOp::verify() {
  if (getCapacity() < 1 || getCapacity() > UINT32_MAX)
    return emitOpError("ready capacity must be between 1 and UINT32_MAX");
  if (getWordAttr().getValue().isNegative() ||
      getWord() >= (getCapacity() + 63) / 64)
    return emitOpError("ready word must name a leaf within capacity");
  return success();
}
LogicalResult NativeReadyReadOp::verify() {
  if (getCapacity() < 1 || getCapacity() > UINT32_MAX)
    return emitOpError("ready capacity must be between 1 and UINT32_MAX");
  if (getWordAttr().getValue().isNegative() ||
      getWord() >= (getCapacity() + 63) / 64)
    return emitOpError("ready word must name a leaf within capacity");
  return success();
}
void NativeReadyReadOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(MemoryEffects::Read::get(),
                       &getOperation()->getOpOperand(0),
                       SideEffects::DefaultResource::get());
  effects.emplace_back(MemoryEffects::Read::get(),
                       sim::SchedulerResource::get());
}
void NativeReadyCommitOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  effects.emplace_back(MemoryEffects::Read::get(),
                       &getOperation()->getOpOperand(0),
                       SideEffects::DefaultResource::get());
  effects.emplace_back(MemoryEffects::Write::get(),
                       &getOperation()->getOpOperand(0),
                       SideEffects::DefaultResource::get());
  effects.emplace_back(MemoryEffects::Read::get(),
                       sim::SchedulerResource::get());
  effects.emplace_back(MemoryEffects::Write::get(),
                       sim::SchedulerResource::get());
}
SuccessorOperands NativeSuspendDelayOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendDelayOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands NativeSuspendChangeOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendChangeOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands NativeSuspendEdgeOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendEdgeOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands NativeSuspendEdgeIffOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendEdgeIffOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands NativeSuspendLevelOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendLevelOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands NativeSuspendAnyOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendAnyOp::verify() {
  if (getEdges().empty() || getEdges().size() > getNumOperands())
    return emitOpError("edge inventory exceeds the watched operand prefix");
  for (int32_t edge : getEdges())
    if (!sim::symbolizeEdgeKind(edge))
      return emitOpError("invalid edge kind");
  for (Value handle : getWatched())
    if (!handle.getType().isInteger(64))
      return emitOpError("watched handles must be i64");
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
Operation::operand_range NativeSuspendAnyOp::getWatched() {
  return getValues().take_front(
      std::min<size_t>(getEdges().size(), getNumOperands()));
}
Operation::operand_range NativeSuspendAnyOp::getContinuationOperands() {
  return getValues().drop_front(
      std::min<size_t>(getEdges().size(), getNumOperands()));
}
MutableOperandRange NativeSuspendAnyOp::getContinuationOperandsMutable() {
  unsigned watchedCount = std::min<size_t>(getEdges().size(), getNumOperands());
  return MutableOperandRange(getOperation(), watchedCount,
                             getNumOperands() - watchedCount);
}
SuccessorOperands
NativeSuspendClockSetOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendClockSetOp::verify() {
  if (getEdges().empty() || getEdges().size() > 64 ||
      getEdges().size() > getNumOperands() ||
      getConditionCountAttr().getValue().isNegative() ||
      getConditionCount() > getNumOperands() - getEdges().size() ||
      getConditionIndices().size() != getEdges().size())
    return emitOpError("invalid clock and condition inventory");
  for (int32_t index : getConditionIndices())
    if (index < -1 ||
        (index >= 0 && static_cast<uint32_t>(index) >= getConditionCount()))
      return emitOpError("condition index exceeds the condition inventory");
  for (Value handle :
       getValues().take_front(getEdges().size() + getConditionCount()))
    if (!handle.getType().isInteger(64))
      return emitOpError("clock handles must be i64");
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
Operation::operand_range NativeSuspendClockSetOp::getPrimaries() {
  size_t count = std::min<size_t>(getEdges().size(), getNumOperands());
  return getValues().take_front(count);
}
Operation::operand_range NativeSuspendClockSetOp::getConditions() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t count = getConditionCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getConditionCount(),
                                          getNumOperands() - primaryCount);
  return getValues().slice(primaryCount, count);
}
Operation::operand_range NativeSuspendClockSetOp::getContinuationOperands() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t conditionCount =
      getConditionCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getConditionCount(),
                               getNumOperands() - primaryCount);
  return getValues().drop_front(primaryCount + conditionCount);
}
MutableOperandRange NativeSuspendClockSetOp::getContinuationOperandsMutable() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  size_t conditionCount =
      getConditionCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getConditionCount(),
                               getNumOperands() - primaryCount);
  size_t begin = primaryCount + conditionCount;
  return MutableOperandRange(getOperation(), begin, getNumOperands() - begin);
}
SuccessorOperands NativeSuspendEventOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendEventOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands
NativeSuspendEventOrderOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendEventOrderOp::verify() {
  if (getEventCountAttr().getValue().isNegative() || getEventCount() == 0 ||
      getEventCount() > getNumOperands())
    return emitOpError("event count exceeds the operand inventory");
  for (Value handle : getEvents())
    if (!handle.getType().isInteger(64))
      return emitOpError("event handles must be i64");
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
Operation::operand_range NativeSuspendEventOrderOp::getEvents() {
  return getValues().take_front(
      std::min<size_t>(getEventCount(), getNumOperands()));
}
Operation::operand_range NativeSuspendEventOrderOp::getContinuationOperands() {
  return getValues().drop_front(
      std::min<size_t>(getEventCount(), getNumOperands()));
}
MutableOperandRange
NativeSuspendEventOrderOp::getContinuationOperandsMutable() {
  unsigned eventCount = std::min<size_t>(getEventCount(), getNumOperands());
  return MutableOperandRange(getOperation(), eventCount,
                             getNumOperands() - eventCount);
}
SuccessorOperands NativeSuspendMailboxOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendMailboxOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands
NativeSuspendSemaphoreOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendSemaphoreOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands NativeSuspendObserveOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendObserveOp::verify() {
  auto planes = get<Field::NativeInitialPlaneCounts>(*this);
  auto conditionBegin = get<Field::NativeConditionOperandBegin>(*this);
  auto continuationBegin = get<Field::NativeContinuationOperandBegin>(*this);
  if (getEdges().empty() || !planes || planes.size() != getEdges().size() ||
      !conditionBegin || !continuationBegin ||
      getConditionCountAttr().getValue().isNegative() ||
      getConditionIndices().size() != getEdges().size())
    return emitOpError("requires a complete physical observer inventory");
  uint64_t expected = getEdges().size();
  for (int32_t count : planes.asArrayRef()) {
    if (count < 1 || count > 2)
      return emitOpError("observer initial value requires one or two planes");
    expected += count;
  }
  if (conditionBegin.getValue().isNegative() ||
      continuationBegin.getValue().isNegative() ||
      conditionBegin.getValue().getZExtValue() != expected ||
      continuationBegin.getValue().getZExtValue() !=
          expected + getConditionCount() ||
      expected + getConditionCount() > getNumOperands())
    return emitOpError(
        "observer segments do not match the physical operand inventory");
  for (int32_t index : getConditionIndices())
    if (index < -1 ||
        (index >= 0 && static_cast<uint32_t>(index) >= getConditionCount()))
      return emitOpError("condition index exceeds the observer inventory");
  for (Value value : getPrimaries())
    if (!value.getDefiningOp<NativeObserverOp>())
      return emitOpError("primary must be a schedule observer");
  for (Value value : getConditions())
    if (!value.getDefiningOp<NativeObserverOp>())
      return emitOpError("condition must be a schedule observer");
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
Operation::operand_range NativeSuspendObserveOp::getPrimaries() {
  size_t count = std::min<size_t>(getEdges().size(), getNumOperands());
  return getValues().take_front(count);
}
Operation::operand_range NativeSuspendObserveOp::getInitialValues() {
  size_t primaryCount = std::min<size_t>(getEdges().size(), getNumOperands());
  auto end = get<Field::NativeConditionOperandBegin>(*this);
  size_t limit =
      end ? std::min<uint64_t>(end.getValue().getZExtValue(), getNumOperands())
          : primaryCount;
  return getValues().slice(primaryCount,
                           limit >= primaryCount ? limit - primaryCount : 0);
}
namespace {
size_t observerSegmentBegin(Operation *operation, Field field) {
  auto value = get<IntegerAttr>(operation, field);
  return value && !value.getValue().isNegative()
             ? std::min<uint64_t>(value.getValue().getZExtValue(),
                                  operation->getNumOperands())
             : 0;
}
} // namespace
Operation::operand_range NativeSuspendObserveOp::getConditions() {
  size_t begin =
      observerSegmentBegin(*this, Field::NativeConditionOperandBegin);
  size_t count =
      getConditionCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getConditionCount(), getNumOperands() - begin);
  return getValues().slice(begin, count);
}
Operation::operand_range NativeSuspendObserveOp::getContinuationOperands() {
  return getValues().drop_front(
      observerSegmentBegin(*this, Field::NativeContinuationOperandBegin));
}
MutableOperandRange NativeSuspendObserveOp::getContinuationOperandsMutable() {
  size_t begin =
      observerSegmentBegin(*this, Field::NativeContinuationOperandBegin);
  return MutableOperandRange(getOperation(), begin, getNumOperands() - begin);
}
SuccessorOperands NativeSuspendForeverOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendForeverOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands NativeSuspendAwaitOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendAwaitOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
SuccessorOperands NativeSuspendJoinOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendJoinOp::verify() {
  if (getProcessCountAttr().getValue().isNegative() ||
      getProcessCount() > getNumOperands())
    return emitOpError("process count exceeds the operand inventory");
  for (Value handle : getProcesses())
    if (!handle.getType().isInteger(64))
      return emitOpError("process handles must be i64");
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
Operation::operand_range NativeSuspendJoinOp::getProcesses() {
  size_t count = getProcessCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getProcessCount(), getNumOperands());
  return getValues().take_front(count);
}
Operation::operand_range NativeSuspendJoinOp::getContinuationOperands() {
  size_t count = getProcessCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getProcessCount(), getNumOperands());
  return getValues().drop_front(count);
}
MutableOperandRange NativeSuspendJoinOp::getContinuationOperandsMutable() {
  unsigned count =
      getProcessCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getProcessCount(), getNumOperands());
  return MutableOperandRange(getOperation(), count, getNumOperands() - count);
}
SuccessorOperands
NativeSuspendChildrenOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeSuspendChildrenOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion())
    return emitOpError("continuation must remain in the same native region");
  if (getContinuationOperands().getTypes() !=
      getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation arguments must match their physical operand types");
  return success();
}
LogicalResult NativeSpawnOp::verifySymbolUses(SymbolTableCollection &symbols) {
  auto callee = symbols.lookupNearestSymbolFrom<FunctionOpInterface>(
      *this, getCalleeAttr());
  if (!callee)
    return emitOpError("requires a defined native process callee");
  if (getOperandTypes() != callee.getArgumentTypes())
    return emitOpError("captures must match the prepared callee signature");
  return success();
}
LogicalResult NativeSpawnOp::verify() { return success(); }
} // namespace obelisk::schedule

namespace obelisk::schedule {
SuccessorOperands NativeProcessControlOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeProcessControlOp::verify() {
  if (getContinuation()->getParent() != (*this)->getParentRegion() ||
      getContinuationOperands().getTypes() !=
          getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation must match the native region and operand types");
  return success();
}
SuccessorOperands
NativeControlBoundaryOp::getSuccessorOperands(unsigned index) {
  assert(index < 2 && "control boundary has two successors");
  return index == 0 ? SuccessorOperands(getResumeOperandsMutable())
                    : SuccessorOperands(MutableOperandRange(
                          getOperation(), getNumOperands(), 0));
}
LogicalResult NativeControlBoundaryOp::verify() {
  auto function = getOperation()->getParentOfType<sim::SimFuncOp>();
  if (!function)
    return emitOpError("must be nested in simulation.func");
  if (function.getEntryKind() == sim::EntryKind::Function ||
      function.getEntryKind() == sim::EntryKind::Observer)
    return emitOpError("requires a suspendable process entry");
  if (getResume() == getBody())
    return emitOpError("resume and body successors must be distinct");
  if (getResume()->getParent() != &function.getBody() ||
      getBody()->getParent() != &function.getBody())
    return emitOpError("successors must be blocks in the same function");
  if (getResume() == &function.getBody().front() ||
      getBody() == &function.getBody().front())
    return emitOpError("successors must not target the entry block");
  if (getResumeOperands().getTypes() != getResume()->getArgumentTypes())
    return emitOpError(
        "resume operand types must match resume block arguments");
  if (!getBody()->getArguments().empty())
    return emitOpError("body successor must not have block arguments");
  return success();
}
SuccessorOperands NativeTaskCallOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeTaskCallOp::verify() {
  if (getArgumentCountAttr().getValue().isNegative() ||
      getArgumentCount() > getValues().size())
    return emitOpError("argument count exceeds the native operand inventory");
  if (getContinuation()->getParent() != (*this)->getParentRegion() ||
      getContinuationOperands().getTypes() !=
          getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation must match the native region and operand types");
  return success();
}
Operation::operand_range NativeTaskCallOp::getArguments() {
  size_t count = getArgumentCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getArgumentCount(), getNumOperands());
  return getValues().take_front(count);
}
Operation::operand_range NativeTaskCallOp::getContinuationOperands() {
  size_t count = getArgumentCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getArgumentCount(), getNumOperands());
  return getValues().drop_front(count);
}
MutableOperandRange NativeTaskCallOp::getContinuationOperandsMutable() {
  unsigned count =
      getArgumentCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getArgumentCount(), getNumOperands());
  return MutableOperandRange(getOperation(), count, getNumOperands() - count);
}
LogicalResult
NativeTaskCallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto callee = symbolTable.lookupNearestSymbolFrom<sim::SimFuncOp>(
      getOperation(), getCalleeAttr());
  if (!callee || callee.getEntryKind() != sim::EntryKind::Task)
    return emitOpError("callee must name a sibling task entry");
  if (getArguments().getTypes() != callee.getFunctionType().getInputs())
    return emitOpError("argument types must match the task signature");
  if (!callee.getFunctionType().getResults().empty())
    return emitOpError("task entry must not return SSA results");
  return success();
}
SuccessorOperands
NativeClassVirtualTaskCallOp::getSuccessorOperands(unsigned index) {
  assert(index == 0);
  return SuccessorOperands(getContinuationOperandsMutable());
}
LogicalResult NativeClassVirtualTaskCallOp::verify() {
  if (getArgumentCountAttr().getValue().isNegative() ||
      getArgumentCount() > getValues().size())
    return emitOpError("argument count exceeds the native operand inventory");
  if (getContinuation()->getParent() != (*this)->getParentRegion() ||
      getContinuationOperands().getTypes() !=
          getContinuation()->getArgumentTypes())
    return emitOpError(
        "continuation must match the native region and operand types");
  return success();
}
Operation::operand_range NativeClassVirtualTaskCallOp::getArguments() {
  size_t count =
      getArgumentCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getArgumentCount(), getValues().size());
  return getValues().take_front(count);
}
Operation::operand_range
NativeClassVirtualTaskCallOp::getContinuationOperands() {
  return getValues().drop_front(getArguments().size());
}
MutableOperandRange
NativeClassVirtualTaskCallOp::getContinuationOperandsMutable() {
  unsigned count =
      getArgumentCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getArgumentCount(), getValues().size());
  return MutableOperandRange(getOperation(), 1 + count,
                             getValues().size() - count);
}
LogicalResult NativeClassVirtualTaskCallOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  auto method = symbolTable.lookupNearestSymbolFrom<NativeMethodOp>(
      getOperation(), getMethodAttr());
  if (!method || !method.getIsVirtual() || !method.getIsTask() ||
      !method.getSlotAttr() || *method.getSlot() != getSlot() ||
      !method.getSignatureIdAttr() ||
      *method.getSignatureId() != getSignatureId())
    return emitOpError("method must name a compatible virtual task slot");
  return success();
}
} // namespace obelisk::schedule

namespace obelisk::schedule {
LogicalResult
NativeExecuteOp::verifySymbolUses(SymbolTableCollection &symbols) {
  auto callee = symbols.lookupNearestSymbolFrom<FunctionOpInterface>(
      *this, getCalleeAttr());
  if (!callee)
    return emitOpError("requires a defined scheduled function");
  auto nativeType = [&](Type type) -> Type {
    if (isa<sim::ContextType>(type))
      return LLVM::LLVMPointerType::get(getContext());
    if (isa<sim::TimeType>(type))
      return IntegerType::get(getContext(), 64);
    return type;
  };
  SmallVector<Type> inputs, results;
  for (Type type : callee.getArgumentTypes())
    inputs.push_back(nativeType(type));
  for (Type type : callee.getResultTypes())
    results.push_back(nativeType(type));
  if (getStatusResult() && results.empty())
    results.push_back(IntegerType::get(getContext(), 32));
  if (getOperandTypes() != TypeRange(inputs) ||
      getResultTypes() != TypeRange(results))
    return emitOpError(
        "operands and results must match the planned native body ABI");
  return success();
}
} // namespace obelisk::schedule

namespace obelisk::schedule {
LogicalResult NativeMethodOp::verify() {
  if (!isa<FunctionType>(getFunctionType()))
    return emitOpError("requires a semantic function signature");
  if (getImplementationAttr() &&
      (!getNativeTypeAttr() ||
       !isa<FunctionType>(getNativeTypeAttr().getValue())))
    return emitOpError("requires a prepared native function signature");
  return success();
}
LogicalResult NativeMethodOp::verifySymbolUses(SymbolTableCollection &symbols) {
  if (!symbols.lookupNearestSymbolFrom<sim::SimClassDeclOp>(*this,
                                                            getOwnerAttr()))
    return emitOpError("requires a declared class owner");
  if (getImplementationAttr()) {
    auto callee = symbols.lookupNearestSymbolFrom<FunctionOpInterface>(
        *this, getImplementationAttr());
    if (!callee || callee.getFunctionType() != getNativeTypeAttr().getValue())
      return emitOpError(
          "implementation must match the prepared native signature");
  }
  return success();
}
LogicalResult NativeObserverOp::verify() {
  auto id = get<Field::NativeObserverId>(*this);
  auto width = get<Field::NativeObserverWidth>(*this);
  auto fourState = get<Field::NativeObserverFourState>(*this);
  auto kinds = get<Field::NativeDependencyKinds>(*this);
  auto widths = get<Field::NativeDependencyWidths>(*this);
  auto indices = get<Field::NativeDependencyCaptureIndices>(*this);
  if (!id || !width || width.getValue().isNegative() ||
      width.getValue().isZero() || !fourState || !kinds || !widths || !indices)
    return emitOpError(
        "requires typed observer identity, result, and dependency metadata");
  if (getCaptureCount() <= getNumOperands() &&
      (kinds.size() != getNumOperands() - getCaptureCount() ||
       widths.size() != kinds.size() || indices.size() != kinds.size()))
    return emitOpError(
        "dependency metadata must match the dependency operands");
  if (getCaptureCountAttr().getValue().isNegative() ||
      getCaptureCount() > getNumOperands())
    return emitOpError("capture count exceeds the operand inventory");
  return success();
}
} // namespace obelisk::schedule

namespace obelisk::schedule {
uint32_t getNativeWaitEntryCount(Operation *operation) {
  return TypeSwitch<Operation *, uint32_t>(operation)
      .Case<NativeSuspendChangeOp, NativeSuspendLevelOp, NativeSuspendEdgeOp,
            NativeSuspendEventOp, NativeSuspendMailboxOp,
            NativeSuspendSemaphoreOp, NativeSuspendAwaitOp>(
          [](auto) { return 1; })
      .Case<NativeSuspendEdgeIffOp>([](auto) { return 2; })
      .Case<NativeSuspendAnyOp>(
          [](auto op) { return static_cast<uint32_t>(op.getWatched().size()); })
      .Case<NativeSuspendClockSetOp>([](auto op) {
        return static_cast<uint32_t>(op.getPrimaries().size() +
                                     op.getConditions().size());
      })
      .Case<NativeSuspendEventOrderOp>(
          [](auto op) { return static_cast<uint32_t>(op.getEvents().size()); })
      .Case<NativeSuspendJoinOp>([](auto op) {
        return static_cast<uint32_t>(op.getProcesses().size());
      })
      .Default([](Operation *) { return 0; });
}

} // namespace obelisk::schedule

namespace obelisk::schedule {
LogicalResult NativeScratchOp::verify() {
  if (getSizeAttr().getValue().isNegative())
    return emitOpError("scratch byte count must be nonnegative");
  for (Operation *user : getResult().getUsers()) {
    if (!isa<runtime::RTFileReadOp, runtime::RTFileReadMemTokenOp,
             runtime::RTPackedFromBytesOp>(user) ||
        user->getParentRegion() != (*this)->getParentRegion())
      return emitOpError(
          "scratch consumers must remain in the native activation region");
  }
  return success();
}
} // namespace obelisk::schedule
