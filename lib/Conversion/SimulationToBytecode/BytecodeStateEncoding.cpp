//===- BytecodeStateEncoding.cpp - State handle instruction selection ----===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

using namespace mlir;

namespace obelisk::bytecode {

std::optional<LogicalResult>
Encoder::encodeStateOperation(FunctionPlan &plan, Operation *operation) {
  if (isa<sim::SimEventNullOp>(operation)) {
    uint32_t destination = reg(plan, operation->getResult(0));
    const Layout &layout = plan.layouts[destination];
    if (layout.kind != Handle || layout.size != 32)
      return operation->emitOpError(
          "event null requires the canonical handle layout");
    SmallVector<uint8_t, 32> bytes(layout.size, 0);
    write32(bytes, 0, OBELISK_RT_DESCRIPTOR_EVENT);
    write64(bytes, 8, UINT64_MAX);
    write64(bytes, 16, UINT64_MAX);
    emit({Constant, 0, destination, 0, 0, 0, 0, addRawConstant(bytes)});
    return success();
  }
  if (auto op = dyn_cast<sim::SimContextStorageOp>(operation))
    return encodeHandle(plan, op.getResult(), op.getId(), state.storage,
                        OBELISK_RT_DESCRIPTOR_STORAGE);
  if (auto op = dyn_cast<sim::SimContextNetOp>(operation))
    return encodeHandle(plan, op.getResult(), op.getId(), state.nets,
                        OBELISK_RT_DESCRIPTOR_NET);
  if (auto op = dyn_cast<sim::SimContextDriverOp>(operation))
    return encodeHandle(plan, op.getResult(), op.getId(), state.drivers,
                        OBELISK_RT_DESCRIPTOR_DRIVER);
  if (auto op = dyn_cast<sim::SimContextEventOp>(operation)) {
    emit({MakeHandle, 0, reg(plan, op.getResult()),
          OBELISK_RT_DESCRIPTOR_EVENT, 0, 0, 0, op.getId()});
    return success();
  }
  if (auto op = dyn_cast<sim::SimRefExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(),
                              op.getLowBit(), Value{});
  if (auto op = dyn_cast<sim::SimNetExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(),
                              op.getLowBit(), Value{});
  if (auto op = dyn_cast<sim::SimDriverExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(),
                              op.getLowBit(), Value{});
  if (auto op = dyn_cast<sim::SimRefDynExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(), 0,
                              op.getLowBit());
  if (auto op = dyn_cast<sim::SimDriverDynExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(), 0,
                              op.getLowBit());
  if (auto op = dyn_cast<sim::SimRefSubelementOp>(operation))
    return encodeSubelementView(plan, op.getResult(), op.getInput(),
                                op.getIndices(), op.getOperation());
  if (auto op = dyn_cast<sim::SimDriverSubelementOp>(operation))
    return encodeSubelementView(plan, op.getResult(), op.getInput(),
                                op.getIndices(), op.getOperation());
  if (auto op = dyn_cast<sim::SimRefArrayElementOp>(operation))
    return encodeArrayView(plan, op.getResult(), op.getInput(), op.getIndex(),
                           op.getOperation());
  if (auto op = dyn_cast<sim::SimDriverArrayElementOp>(operation))
    return encodeArrayView(plan, op.getResult(), op.getInput(), op.getIndex(),
                           op.getOperation());
  if (auto op = dyn_cast<sim::SimRefLoadOp>(operation)) {
    emit({LoadState, 0, reg(plan, op.getResult()),
          reg(plan, op.getReference())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimNetReadOp>(operation)) {
    emit({LoadState, 0, reg(plan, op.getResult()), reg(plan, op.getNet())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimRefStoreOp>(operation)) {
    emit({StoreState, 0, 0, reg(plan, op.getReference()),
          reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimOverrideOp>(operation)) {
    emit({OverrideState,
          op.getIsAssign() ? OBELISK_RT_DB_OVERRIDE_ASSIGN
                           : OBELISK_RT_DB_OVERRIDE_FORCE,
          0, reg(plan, op.getTarget()), reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimReleaseOverrideOp>(operation)) {
    emit({ReleaseState,
          op.getIsAssign() ? OBELISK_RT_DB_OVERRIDE_ASSIGN
                           : OBELISK_RT_DB_OVERRIDE_FORCE,
          0, reg(plan, op.getTarget())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimDriverDriveOp>(operation)) {
    emit({StoreState, 0, 0, reg(plan, op.getDriver()),
          reg(plan, op.getValue())});
    return success();
  }
  return std::nullopt;
}

} // namespace obelisk::bytecode
