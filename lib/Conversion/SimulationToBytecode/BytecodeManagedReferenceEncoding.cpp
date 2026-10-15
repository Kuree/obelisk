//===- BytecodeManagedReferenceEncoding.cpp - Managed reference encoding -===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

using namespace mlir;

namespace obelisk::bytecode {

std::optional<LogicalResult>
Encoder::encodeManagedReferenceOperation(FunctionPlan &plan,
                                         Operation *operation) {
  if (isa<sim::SimManagedNullOp, sim::SimManagedWatchNullOp>(operation)) {
    uint32_t destination = reg(plan, operation->getResult(0));
    emit({Constant, 0, destination, 0, 0, 0, 0,
          addZeroConstant(plan.layouts[destination])});
    return success();
  }
  if (auto op = dyn_cast<sim::SimManagedIsNullOp>(operation)) {
    uint32_t input = reg(plan, op.getInput());
    uint32_t zero = addZeroConstant(plan.layouts[input]);
    uint32_t constant = temporary(plan, op.getInput().getType());
    if (constant == kInvalidRegister)
      return failure();
    emit({Constant, 0, constant, 0, 0, 0, 0, zero});
    emit({Compare, OBELISK_RT_DB_CMP_EQ, reg(plan, op.getResult()), input,
          constant});
    return success();
  }
  if (auto op = dyn_cast<sim::SimSemaphoreEqualOp>(operation)) {
    emit({Compare, OBELISK_RT_DB_CMP_EQ, reg(plan, op.getEqual()),
          reg(plan, op.getLhs()), reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimMailboxEqualOp>(operation)) {
    emit({Compare, OBELISK_RT_DB_CMP_EQ, reg(plan, op.getEqual()),
          reg(plan, op.getLhs()), reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimReferencePathIndexOp>(operation)) {
    SmallVector<Value> inputs{op.getContainer(), op.getIndex()};
    if (op.getOwnerReference())
      inputs.push_back(op.getOwnerReference());
    return emitIntrinsic(plan, kIntrinsicReferencePathIndex, inputs,
                         {op.getResult()});
  }
  if (auto op = dyn_cast<sim::SimReferencePathStringCharacterOp>(operation))
    return emitIntrinsic(
        plan, kIntrinsicReferencePathStringCharacter,
        {op.getString(), op.getIndex(), op.getOwnerReference()},
        {op.getResult()});
  auto encodeAggregateProjection = [&](auto op) -> LogicalResult {
    using Op = decltype(op);
    uint64_t ownerWidth, ownerSize;
    bool ownerFourState;
    if constexpr (std::is_same_v<Op, sim::SimReferencePathAggregateSliceOp>) {
      ownerWidth = op.getOwnerBitWidth();
      ownerSize = op.getOwnerPlaneSize();
      ownerFourState = op.getOwnerFourState();
    } else {
      Type ownerType = op.getOwnerReference().getType().getElementType();
      auto storage = getManagedValueStorage(ownerType, dataLayout);
      auto width = simulationWidth(ownerType);
      if (failed(storage) || !width)
        return op.emitOpError("aggregate owner has no bytecode layout");
      ownerWidth = *width;
      ownerSize = storage->planeSize;
      ownerFourState = storage->fourState;
    }
    SmallVector<uint8_t> traceSlots;
    for (auto [offset, kind] :
         llvm::zip_equal(op.getTraceOffsets(), op.getTraceKinds())) {
      append64(traceSlots, static_cast<uint64_t>(offset));
      append32(traceSlots, static_cast<uint32_t>(kind));
      append32(traceSlots, 0);
    }
    uint32_t index;
    uint64_t left, right, span;
    if constexpr (std::is_same_v<Op, sim::SimReferencePathAggregateSliceOp>) {
      index = emitU64Constant(plan, op.getBitOffset());
      left = 0;
      right = ownerWidth - 1;
      span = 1;
    } else {
      index = reg(plan, op.getIndex());
      left = op.getLeft();
      right = op.getRight();
      span = op.getElementSpan();
    }
    return emitIntrinsicRegisters(
        plan, kIntrinsicReferencePathAggregateElement,
        {reg(plan, op.getOwnerReference()), index, emitU64Constant(plan, left),
         emitU64Constant(plan, right), emitU64Constant(plan, span),
         emitU64Constant(plan, ownerWidth), emitU64Constant(plan, ownerSize),
         emitU64Constant(plan, ownerFourState),
         emitU64Constant(plan, op.getTypeId()),
         emitU64Constant(plan, static_cast<uint32_t>(op.getElementKind())),
         emitU64Constant(plan, static_cast<uint32_t>(op.getElementFlags())),
         emitU64Constant(plan, op.getValueSize()),
         emitU64Constant(plan, op.getAlignment()),
         emitU64Constant(plan, op.getBitWidth()),
         emitBytesConstant(plan, traceSlots)},
        {reg(plan, op.getResult())});
  };
  if (auto op = dyn_cast<sim::SimReferencePathAggregateElementOp>(operation))
    return encodeAggregateProjection(op);
  if (auto op = dyn_cast<sim::SimReferencePathAggregateSliceOp>(operation))
    return encodeAggregateProjection(op);
  if (auto op = dyn_cast<sim::SimReferencePathAssocOp>(operation))
    return emitIntrinsic(plan, kIntrinsicReferencePathAssoc,
                         {op.getArray(), op.getKey(), op.getOwnerReference()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimArgumentRefFromPathOp>(operation))
    return emitIntrinsic(plan, kIntrinsicArgumentRefFromPath, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimArgumentRefRetypeOp>(operation)) {
    emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimArgumentRefFromRefOp>(operation))
    return emitIntrinsic(plan, kIntrinsicArgumentRefFromRef, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimArgumentRefFromManagedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicArgumentRefFromManaged,
                         {op.getInput()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimArgumentRefLoadOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getResult().getType(), dataLayout);
    std::optional<uint32_t> width = simulationWidth(op.getResult().getType());
    if (failed(storage) || !width)
      return op.emitOpError("argument reference has no bytecode layout");
    uint32_t flags = (storage->fourState ? 1u : 0u) |
                     ((isa<sim::StringType>(op.getResult().getType())
                           ? OBELISK_RT_ARGUMENT_VALUE_STRING
                       : sim::isManagedHandleType(op.getResult().getType())
                           ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                           : OBELISK_RT_ARGUMENT_VALUE_BITS)
                      << 1);
    return emitIntrinsicRegisters(plan, kIntrinsicArgumentRefLoad,
                                  {reg(plan, op.getReference()),
                                   emitU64Constant(plan, storage->planeSize),
                                   emitU64Constant(plan, *width),
                                   emitU64Constant(plan, flags)},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimArgumentRefStoreOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getValue().getType(), dataLayout);
    std::optional<uint32_t> width = simulationWidth(op.getValue().getType());
    if (failed(storage) || !width)
      return op.emitOpError("argument reference has no bytecode layout");
    uint32_t flags = (storage->fourState ? 1u : 0u) |
                     ((isa<sim::StringType>(op.getValue().getType())
                           ? OBELISK_RT_ARGUMENT_VALUE_STRING
                       : sim::isManagedHandleType(op.getValue().getType())
                           ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                           : OBELISK_RT_ARGUMENT_VALUE_BITS)
                      << 1);
    return emitIntrinsicRegisters(
        plan, kIntrinsicArgumentRefStore,
        {reg(plan, op.getReference()), reg(plan, op.getValue()),
         emitU64Constant(plan, storage->planeSize),
         emitU64Constant(plan, *width), emitU64Constant(plan, flags)},
        {});
  }
  if (auto op = dyn_cast<sim::SimManagedWatchOp>(operation)) {
    uint64_t runtimeKind;
    switch (op.getKind()) {
    case sim::ManagedWatchKind::Field:
      runtimeKind = OBELISK_RT_MANAGED_WATCH_FIELD;
      break;
    case sim::ManagedWatchKind::ContainerSize:
      runtimeKind = OBELISK_RT_MANAGED_WATCH_CONTAINER_SIZE;
      break;
    default:
      op.emitOpError("has an unknown managed-watch kind");
      return failure();
    }
    return emitIntrinsicRegisters(
        plan, kIntrinsicManagedWatch,
        {reg(plan, op.getInput()), emitU64Constant(plan, runtimeKind)},
        {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimManagedLoadOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getResult().getType(), dataLayout);
    if (failed(storage))
      return op.emitOpError("managed result has no bytecode field layout");
    uint32_t size = emitU64Constant(plan, storage->planeSize);
    return emitIntrinsicRegisters(plan, kIntrinsicManagedLoad,
                                  {reg(plan, op.getReference()), size},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimManagedStoreOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getValue().getType(), dataLayout);
    if (failed(storage))
      return op.emitOpError("managed value has no bytecode field layout");
    uint32_t size = emitU64Constant(plan, storage->planeSize);
    return emitIntrinsicRegisters(
        plan, kIntrinsicManagedStore,
        {reg(plan, op.getReference()), reg(plan, op.getValue()), size}, {});
  }
  if (auto op = dyn_cast<sim::SimManagedNBAEnqueueOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getValue().getType(), dataLayout);
    if (failed(storage))
      return op.emitOpError("managed NBA value has no bytecode field layout");
    SmallVector<uint32_t> inputs{reg(plan, op.getDestination()),
                                 reg(plan, op.getValue()),
                                 emitU64Constant(plan, storage->planeSize)};
    if (op.getDelay())
      inputs.push_back(reg(plan, op.getDelay()));
    return emitIntrinsicRegisters(plan, kIntrinsicManagedNBA, inputs, {});
  }
  if (auto op = dyn_cast<sim::SimManagedNBAEnqueueMaskedOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getValue().getType(), dataLayout);
    if (failed(storage))
      return op.emitOpError("managed NBA value has no bytecode field layout");
    return emitIntrinsicRegisters(
        plan, kIntrinsicManagedNBAMasked,
        {reg(plan, op.getDestination()), reg(plan, op.getValue()),
         emitU64Constant(plan, storage->planeSize), reg(plan, op.getMask()),
         op.getDelay() ? reg(plan, op.getDelay()) : emitU64Constant(plan, 0)},
        {});
  }
  if (auto op = dyn_cast<sim::SimReferencePathNBAEnqueueOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getValue().getType(), dataLayout);
    if (failed(storage))
      return op.emitOpError(
          "reference-path NBA value has no bytecode field layout");
    SmallVector<uint32_t> inputs{reg(plan, op.getDestination()),
                                 reg(plan, op.getValue()),
                                 emitU64Constant(plan, storage->planeSize)};
    if (op.getDelay())
      inputs.push_back(reg(plan, op.getDelay()));
    return emitIntrinsicRegisters(plan, kIntrinsicManagedNBA, inputs, {});
  }
  if (auto op = dyn_cast<sim::SimWeakGetOp>(operation))
    return emitIntrinsic(plan, kIntrinsicWeakGet, {op.getWeak()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimWeakClearOp>(operation))
    return emitIntrinsic(plan, kIntrinsicWeakClear, {op.getWeak()}, {});
  if (isa<sim::SimGCSafepointOp>(operation))
    return emitIntrinsic(plan, kIntrinsicGCSafepoint, {}, {});
  return std::nullopt;
}

} // namespace obelisk::bytecode
