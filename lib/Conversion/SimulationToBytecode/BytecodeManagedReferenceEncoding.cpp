//===- BytecodeManagedReferenceEncoding.cpp - Managed reference encoding -===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

using namespace mlir;

namespace obelisk::bytecode {

std::optional<LogicalResult>
Encoder::encodeManagedReferenceOperation(FunctionPlan &plan,
                                         Operation *operation) {
  if (isa<sim::SimManagedNullOp>(operation)) {
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
  if (auto op = dyn_cast<sim::SimReferencePathIndexOp>(operation))
    return emitIntrinsic(
        plan, kIntrinsicReferencePathIndex,
        {op.getContainer(), op.getIndex(), op.getOwnerReference()},
        {op.getResult()});
  if (auto op = dyn_cast<sim::SimReferencePathAssocOp>(operation))
    return emitIntrinsic(plan, kIntrinsicReferencePathAssoc,
                         {op.getArray(), op.getKey(), op.getOwnerReference()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimArgumentRefFromPathOp>(operation))
    return emitIntrinsic(plan, kIntrinsicArgumentRefFromPath, {op.getInput()},
                         {op.getResult()});
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
