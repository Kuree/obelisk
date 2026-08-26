//===- BytecodeContainerEncoding.cpp - Container instruction selection ---===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

using namespace mlir;

namespace obelisk::bytecode {

std::optional<LogicalResult>
Encoder::encodeContainerOperation(FunctionPlan &plan, Operation *operation) {
  if (auto op = dyn_cast<sim::SimBoxPackOp>(operation)) {
    emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimBoxCastOp>(operation)) {
    emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimBoxIsTypeOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicBoxIsType,
        {reg(plan, op.getInput()), emitU64Constant(plan, op.getTypeId())},
        {reg(plan, op.getResult())});
  if (auto op = dyn_cast<sim::SimContainerSizeOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerSize, {op.getContainer()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimContainerCreateLikeOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerCreateLike,
                         {op.getPreferred(), op.getFallback(), op.getSize()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimContainerCreateOp>(operation)) {
    SmallVector<uint8_t> traceSlots;
    for (auto [offset, kind] :
         llvm::zip_equal(op.getTraceOffsets(), op.getTraceKinds())) {
      append64(traceSlots, static_cast<uint64_t>(offset));
      append32(traceSlots, static_cast<uint32_t>(kind));
      append32(traceSlots, 0);
    }
    return emitIntrinsicRegisters(plan, kIntrinsicContainerCreate,
                                  {emitU64Constant(plan, op.getContainerKind()),
                                   emitU64Constant(plan, op.getTypeId()),
                                   emitU64Constant(plan, op.getElementKind()),
                                   emitU64Constant(plan, op.getElementFlags()),
                                   emitU64Constant(plan, op.getValueSize()),
                                   emitU64Constant(plan, op.getAlignment()),
                                   emitU64Constant(plan, op.getBitWidth()),
                                   emitBytesConstant(plan, traceSlots),
                                   reg(plan, op.getSize()),
                                   emitU64Constant(plan, op.getBound())},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimContainerCloneOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerClone, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimContainerImportFixedOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getInput().getType(), dataLayout);
    std::optional<uint32_t> width = simulationWidth(op.getInput().getType());
    if (failed(storage) || !width)
      return op.emitOpError("fixed array has no bytecode layout");
    return emitIntrinsicRegisters(
        plan, kIntrinsicContainerImportFixed,
        {reg(plan, op.getContainer()), reg(plan, op.getInput()),
         emitU64Constant(plan, storage->planeSize),
         emitU64Constant(plan, *width),
         emitU64Constant(plan, storage->fourState),
         emitU64Constant(plan, op.getElementSpan()),
         emitU64Constant(
             plan, sim::getAggregateNumElements(op.getInput().getType()))},
        {});
  }
  if (auto op = dyn_cast<sim::SimContainerExportFixedOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getResult().getType(), dataLayout);
    std::optional<uint32_t> width = simulationWidth(op.getResult().getType());
    if (failed(storage) || !width)
      return op.emitOpError("fixed array has no bytecode layout");
    return emitIntrinsicRegisters(
        plan, kIntrinsicContainerExportFixed,
        {reg(plan, op.getContainer()),
         emitU64Constant(plan, storage->planeSize),
         emitU64Constant(plan, *width),
         emitU64Constant(plan, storage->fourState),
         emitU64Constant(plan, op.getElementSpan()),
         emitU64Constant(
             plan, sim::getAggregateNumElements(op.getResult().getType()))},
        {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimContainerExportBitstreamOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getResult().getType(), dataLayout);
    Type containerType = op.getContainer().getType();
    Type element =
        isa<sim::DynamicArrayType>(containerType)
            ? cast<sim::DynamicArrayType>(containerType).getElementType()
            : cast<sim::QueueType>(containerType).getElementType();
    FailureOr<ManagedValueStorage> elementStorage =
        getManagedValueStorage(element, dataLayout);
    std::optional<uint32_t> width = simulationWidth(op.getResult().getType());
    std::optional<uint32_t> elementWidth = simulationWidth(element);
    if (failed(storage) || failed(elementStorage) || !width || !elementWidth ||
        *elementWidth == 0 || *width % *elementWidth != 0)
      return op.emitOpError("bit-stream export has no bytecode layout");
    requiresContainerBitstreamFeature = true;
    return emitIntrinsicRegisters(
        plan, kIntrinsicContainerExportBitstream,
        {reg(plan, op.getContainer()),
         emitU64Constant(plan, storage->planeSize),
         emitU64Constant(plan, *width),
         emitU64Constant(plan, storage->fourState),
         emitU64Constant(plan, *elementWidth),
         emitU64Constant(plan, *width / *elementWidth),
         emitU64Constant(plan, elementStorage->planeSize),
         emitU64Constant(plan, elementStorage->fourState)},
        {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimContainerSwapOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerSwap,
                         {op.getContainer(), op.getLeft(), op.getRight()}, {});
  if (auto op = dyn_cast<sim::SimContainerDeleteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerDelete, {op.getContainer()},
                         {});
  if (auto op = dyn_cast<sim::SimQueueDeleteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicQueueDelete,
                         {op.getQueue(), op.getIndex()}, {});
  if (auto op = dyn_cast<sim::SimQueueInsertOp>(operation))
    return emitIntrinsic(plan, kIntrinsicQueueInsert,
                         {op.getQueue(), op.getIndex(), op.getValue()}, {});
  if (auto op = dyn_cast<sim::SimMailboxCreateOp>(operation)) {
    SmallVector<uint8_t> traceSlots;
    for (auto [offset, kind] :
         llvm::zip_equal(op.getTraceOffsets(), op.getTraceKinds())) {
      append64(traceSlots, static_cast<uint64_t>(offset));
      append32(traceSlots, static_cast<uint32_t>(kind));
      append32(traceSlots, 0);
    }
    return emitIntrinsicRegisters(plan, kIntrinsicMailboxCreate,
                                  {emitU64Constant(plan, op.getTypeId()),
                                   emitU64Constant(plan, op.getElementKind()),
                                   emitU64Constant(plan, op.getElementFlags()),
                                   emitU64Constant(plan, op.getValueSize()),
                                   emitU64Constant(plan, op.getAlignment()),
                                   emitU64Constant(plan, op.getBitWidth()),
                                   emitBytesConstant(plan, traceSlots),
                                   reg(plan, op.getBound())},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimMailboxNumOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMailboxNum, {op.getMailbox()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimMailboxTryPutOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMailboxTryPut,
                         {op.getMailbox(), op.getValue()}, {op.getSuccess()});
  if (auto op = dyn_cast<sim::SimMailboxTryPeekOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMailboxTryPeek, {op.getMailbox()},
                         {op.getSuccess(), op.getValue()});
  if (auto op = dyn_cast<sim::SimMailboxTryGetOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMailboxTryGet, {op.getMailbox()},
                         {op.getSuccess(), op.getValue()});
  if (auto op = dyn_cast<sim::SimSemaphoreCreateOp>(operation))
    return emitIntrinsic(plan, kIntrinsicSemaphoreCreate, {op.getKeys()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimSemaphorePutOp>(operation))
    return emitIntrinsic(plan, kIntrinsicSemaphorePut,
                         {op.getSemaphore(), op.getKeys()}, {});
  if (auto op = dyn_cast<sim::SimSemaphoreTryGetOp>(operation))
    return emitIntrinsic(plan, kIntrinsicSemaphoreTryGet,
                         {op.getSemaphore(), op.getKeys()}, {op.getSuccess()});
  if (auto op = dyn_cast<sim::SimRandomNextOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomNext, {}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimRandomStateOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomGetState, {},
                         {op.getState(), op.getIncrement()});
  if (auto op = dyn_cast<sim::SimRandomSetStateOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomSetState,
                         {op.getState(), op.getIncrement()}, {});
  if (auto op = dyn_cast<sim::SimSampledReadOp>(operation))
    return emitIntrinsic(plan, kIntrinsicSampledRead, {op.getSource()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimSampledHistoryOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicSampledHistory,
        {emitU64Constant(plan, op.getId()),
         emitU64Constant(plan, op.getDepth()), reg(plan, op.getGate()),
         reg(plan, op.getCurrent())},
        {reg(plan, op.getResult())});
  if (auto op = dyn_cast<sim::SimClockedSampleUpdateOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicClockedSampleUpdate,
        {emitU64Constant(plan, op.getId()),
         emitU64Constant(plan, op.getDepth()), reg(plan, op.getGate()),
         reg(plan, op.getCurrent())},
        {});
  if (auto op = dyn_cast<sim::SimClockedSampleReadOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicClockedSampleRead,
        {emitU64Constant(plan, op.getId()),
         emitU64Constant(plan, op.getDepth()),
         emitU64Constant(plan, op.getAge())},
        {reg(plan, op.getResult())});
  if (auto op = dyn_cast<sim::SimRandomSeedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomSeed, {op.getSeed()}, {});
  if (auto op = dyn_cast<sim::SimRandomBoundedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomBounded, {op.getBound()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimRandomDistributionOp>(operation)) {
    uint32_t distribution = emitU64Constant(plan, op.getDistribution());
    return emitIntrinsicRegisters(
        plan, kIntrinsicRandomDistribution,
        {distribution, reg(plan, op.getSeed()), reg(plan, op.getFirst()),
         reg(plan, op.getSecond())},
        {reg(plan, op.getResult()), reg(plan, op.getNextSeed())});
  }
  if (auto op = dyn_cast<sim::SimStochasticQueueOp>(operation)) {
    uint32_t action = emitU64Constant(plan, op.getAction());
    uint32_t unitScale = emitU64Constant(plan, op.getUnitScale());
    return emitIntrinsicRegisters(
        plan, kIntrinsicStochasticQueue,
        {action, reg(plan, op.getId()), reg(plan, op.getFirst()),
         reg(plan, op.getSecond()), unitScale},
        {reg(plan, op.getPrimary()), reg(plan, op.getSecondary()),
         reg(plan, op.getStatus())});
  }
  if (auto op = dyn_cast<sim::SimRandomCycleNextOp>(operation)) {
    uint32_t width = emitU64Constant(plan, op.getWidth());
    return emitIntrinsicRegisters(
        plan, kIntrinsicRandomCycleNext,
        {reg(plan, op.getKey()), reg(plan, op.getPosition()), width},
        {reg(plan, op.getNextPosition()), reg(plan, op.getValue())});
  }
  if (auto op = dyn_cast<sim::SimRandomSolveOp>(operation)) {
    StringRef program = op.getProgram();
    SmallVector<uint32_t> inputs{
        emitBytesConstant(
            plan,
            ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(program.data()),
                              program.size())),
        reg(plan, op.getStart()),
        reg(plan, op.getMutableMask()),
        reg(plan, op.getConstraintMask()),
        reg(plan, op.getMaxAttempts()),
        reg(plan, op.getRngState()),
        reg(plan, op.getRngIncrement())};
    for (Value capture : op.getCaptures())
      inputs.push_back(reg(plan, capture));
    return emitIntrinsicRegisters(plan, kIntrinsicRandomSolveState, inputs,
                                  {reg(plan, op.getAssignment()),
                                   reg(plan, op.getSuccess()),
                                   reg(plan, op.getNextRngState())});
  }
  if (auto op = dyn_cast<sim::SimRandomSolveWideOp>(operation)) {
    StringRef program = op.getProgram();
    SmallVector<uint32_t> inputs{
        emitBytesConstant(
            plan,
            ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(program.data()),
                              program.size())),
        reg(plan, op.getStart()),
        reg(plan, op.getMutableMask()),
        reg(plan, op.getConstraintMask()),
        reg(plan, op.getMaxAttempts()),
        reg(plan, op.getRngState()),
        reg(plan, op.getRngIncrement())};
    for (Value capture : op.getCaptures())
      inputs.push_back(reg(plan, capture));
    return emitIntrinsicRegisters(plan, kIntrinsicRandomSolveWideState, inputs,
                                  {reg(plan, op.getAssignment()),
                                   reg(plan, op.getSuccess()),
                                   reg(plan, op.getNextRngState())});
  }
  if (auto op = dyn_cast<sim::SimContainerReadOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerRead,
                         {op.getContainer(), op.getIndex()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimContainerWriteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerWrite,
                         {op.getContainer(), op.getIndex(), op.getValue()}, {});
  if (auto op = dyn_cast<sim::SimAssocCreateOp>(operation)) {
    SmallVector<uint8_t> traceSlots;
    for (auto [offset, kind] :
         llvm::zip_equal(op.getTraceOffsets(), op.getTraceKinds())) {
      append64(traceSlots, static_cast<uint64_t>(offset));
      append32(traceSlots, static_cast<uint32_t>(kind));
      append32(traceSlots, 0);
    }
    return emitIntrinsicRegisters(plan, kIntrinsicAssocCreate,
                                  {emitU64Constant(plan, op.getTypeId()),
                                   emitU64Constant(plan, op.getElementKind()),
                                   emitU64Constant(plan, op.getElementFlags()),
                                   emitU64Constant(plan, op.getValueSize()),
                                   emitU64Constant(plan, op.getAlignment()),
                                   emitU64Constant(plan, op.getBitWidth()),
                                   emitBytesConstant(plan, traceSlots),
                                   emitU64Constant(plan, op.getKeyKind()),
                                   emitU64Constant(plan, op.getKeyWidth())},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimAssocReadOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocRead,
                         {op.getArray(), op.getKey()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimAssocWriteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocWrite,
                         {op.getArray(), op.getKey(), op.getValue()}, {});
  if (auto op = dyn_cast<sim::SimAssocExistsOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocExists,
                         {op.getArray(), op.getKey()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimAssocDeleteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocDelete,
                         {op.getArray(), op.getKey()}, {});
  if (auto op = dyn_cast<sim::SimAssocSetDefaultOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocDefault,
                         {op.getArray(), op.getValue()}, {});
  if (auto op = dyn_cast<sim::SimAssocTraverseOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicAssocTraverse,
        {reg(plan, op.getArray()), reg(plan, op.getKey()),
         emitU64Constant(plan, static_cast<uint64_t>(static_cast<int64_t>(
                                   static_cast<int32_t>(op.getDirection())))),
         emitU64Constant(plan, op.getEndpoint() ? 1 : 0)},
        {reg(plan, op.getResultKey()), reg(plan, op.getSuccess())});
  return std::nullopt;
}

} // namespace obelisk::bytecode
