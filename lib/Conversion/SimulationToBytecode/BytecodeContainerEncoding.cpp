//===- BytecodeContainerEncoding.cpp - Container instruction selection ---===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

using namespace mlir;

namespace obelisk::bytecode {

std::optional<LogicalResult>
Encoder::encodeContainerOperation(FunctionPlan &plan, Operation *operation) {
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
  if (auto op = dyn_cast<sim::SimContainerDeleteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerDelete, {op.getContainer()},
                         {});
  if (auto op = dyn_cast<sim::SimQueueDeleteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicQueueDelete,
                         {op.getQueue(), op.getIndex()}, {});
  if (auto op = dyn_cast<sim::SimQueueInsertOp>(operation))
    return emitIntrinsic(plan, kIntrinsicQueueInsert,
                         {op.getQueue(), op.getIndex(), op.getValue()}, {});
  if (auto op = dyn_cast<sim::SimRandomNextOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomNext, {}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimRandomSeedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomSeed, {op.getSeed()}, {});
  if (auto op = dyn_cast<sim::SimRandomBoundedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomBounded, {op.getBound()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimRandomDistributionOp>(operation)) {
    uint32_t distribution = emitU64Constant(plan, op.getDistribution());
    return emitIntrinsicRegisters(
        plan, kIntrinsicRandomDistribution,
        {distribution, reg(plan, op.getFirst()), reg(plan, op.getSecond())},
        {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimRandomSolveOp>(operation)) {
    StringRef program = op.getProgram();
    SmallVector<uint32_t> inputs{
        emitBytesConstant(
            plan,
            ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(program.data()),
                              program.size())),
        reg(plan, op.getStart()), reg(plan, op.getMutableMask()),
        reg(plan, op.getConstraintMask()), reg(plan, op.getMaxAttempts())};
    for (Value capture : op.getCaptures())
      inputs.push_back(reg(plan, capture));
    return emitIntrinsicRegisters(
        plan, kIntrinsicRandomSolve, inputs,
        {reg(plan, op.getAssignment()), reg(plan, op.getSuccess())});
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
