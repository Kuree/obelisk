//===- BytecodeStateEncoding.cpp - State handle instruction selection ----===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

using namespace mlir;

namespace obelisk::bytecode {

static constexpr StringLiteral continuousStoreAttrName =
    "obelisk_sim.continuous_store";

std::optional<LogicalResult>
Encoder::encodeStateOperation(FunctionPlan &plan, Operation *operation) {
  if (auto op = dyn_cast<sim::SimEventCreateOp>(operation))
    return emitIntrinsic(plan, kIntrinsicEventCreate, {}, {op.getResult()});
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
    emit({MakeHandle, 0, reg(plan, op.getResult()), OBELISK_RT_DESCRIPTOR_EVENT,
          0, 0, 0, op.getId()});
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
  if (auto op = dyn_cast<sim::SimDriverReadOp>(operation)) {
    emit({LoadState, 0, reg(plan, op.getResult()), reg(plan, op.getDriver())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimRefStoreOp>(operation)) {
    sim::EntryKind entryKind = plan.function.getEntryKind();
    bool continuous = !isa<sim::EventType>(op.getValue().getType()) &&
                      (op->hasAttr(continuousStoreAttrName) ||
                       entryKind == sim::EntryKind::Continuous ||
                       entryKind == sim::EntryKind::PortInput ||
                       entryKind == sim::EntryKind::PortOutput);
    emit({StoreState,
          static_cast<uint16_t>(
              continuous ? OBELISK_RT_DB_STORE_STATE_CONTINUOUS : 0),
          0, reg(plan, op.getReference()), reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimNetWriteOp>(operation)) {
    emit({StoreState, 0, 0, reg(plan, op.getNet()), reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimOverrideOp>(operation)) {
    if (isa<sim::ManagedRefType>(op.getTarget().getType())) {
      FailureOr<ManagedValueStorage> storage =
          getManagedValueStorage(op.getValue().getType(), dataLayout);
      if (failed(storage))
        return op.emitOpError("managed override value has no field layout");
      uint64_t flags =
          (op.getIsAssign() ? 1u : 0u) | (storage->fourState ? 8u : 0u);
      return emitIntrinsicRegisters(
          plan, kIntrinsicManagedOverride,
          {reg(plan, op.getTarget()), reg(plan, op.getValue()),
           emitU64Constant(plan, storage->planeSize),
           emitU64Constant(plan, flags), emitU64Constant(plan, 0)},
          {});
    }
    emit(
        {OverrideState,
         static_cast<uint16_t>(op.getIsAssign() ? OBELISK_RT_DB_OVERRIDE_ASSIGN
                                                : OBELISK_RT_DB_OVERRIDE_FORCE),
         0, reg(plan, op.getTarget()), reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimDynamicOverrideOp>(operation)) {
    if (isa<sim::ManagedRefType>(op.getTarget().getType())) {
      FailureOr<ManagedValueStorage> storage =
          getManagedValueStorage(op.getValue().getType(), dataLayout);
      if (failed(storage))
        return op.emitOpError("managed override value has no field layout");
      uint64_t flags = (op.getIsAssign() ? 1u : 0u) | 2u |
                       (op.getClaim() ? 4u : 0u) |
                       (storage->fourState ? 8u : 0u);
      return emitIntrinsicRegisters(
          plan, kIntrinsicManagedOverride,
          {reg(plan, op.getTarget()), reg(plan, op.getValue()),
           emitU64Constant(plan, storage->planeSize),
           emitU64Constant(plan, flags), reg(plan, op.getOwner())},
          {});
    }
    uint16_t flags = op.getIsAssign() ? OBELISK_RT_DB_OVERRIDE_ASSIGN
                                      : OBELISK_RT_DB_OVERRIDE_FORCE;
    flags |= OBELISK_RT_DB_OVERRIDE_DYNAMIC;
    if (op.getClaim())
      flags |= OBELISK_RT_DB_OVERRIDE_CLAIM;
    emit({OverrideState, flags, 0, reg(plan, op.getTarget()),
          reg(plan, op.getValue()), reg(plan, op.getOwner())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimReleaseOverrideOp>(operation)) {
    if (auto reference =
            dyn_cast<sim::ManagedRefType>(op.getTarget().getType())) {
      FailureOr<ManagedValueStorage> storage =
          getManagedValueStorage(reference.getElementType(), dataLayout);
      if (failed(storage))
        return op.emitOpError("managed override target has no field layout");
      uint64_t flags =
          (op.getIsAssign() ? 1u : 0u) | (storage->fourState ? 8u : 0u);
      return emitIntrinsicRegisters(plan, kIntrinsicManagedReleaseOverride,
                                    {reg(plan, op.getTarget()),
                                     emitU64Constant(plan, storage->planeSize),
                                     emitU64Constant(plan, flags)},
                                    {});
    }
    emit(
        {ReleaseState,
         static_cast<uint16_t>(op.getIsAssign() ? OBELISK_RT_DB_OVERRIDE_ASSIGN
                                                : OBELISK_RT_DB_OVERRIDE_FORCE),
         0, reg(plan, op.getTarget())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimDriverDriveOp>(operation)) {
    uint16_t flags = op->hasAttr("obelisk_sim.defer_net_resolution")
                         ? OBELISK_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION
                         : 0;
    emit({StoreState, flags, 0, reg(plan, op.getDriver()),
          reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimDriverDriveDelayedNetOp>(operation)) {
    uint16_t flags = op.getDeferResolution()
                         ? OBELISK_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION
                         : 0;
    emit({StoreState, flags, 0, reg(plan, op.getDriver()),
          reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimDriverDriveChangedOp>(operation)) {
    uint16_t flags = OBELISK_RT_DB_STORE_STATE_CHANGED;
    if (op->hasAttr("obelisk_sim.defer_net_resolution"))
      flags |= OBELISK_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION;
    emit({StoreState, flags, reg(plan, op.getChanged()),
          reg(plan, op.getDriver()), reg(plan, op.getValue())});
    return success();
  }
  return std::nullopt;
}

} // namespace obelisk::bytecode
