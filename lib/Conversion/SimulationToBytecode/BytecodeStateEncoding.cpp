//===- BytecodeStateEncoding.cpp - State handle instruction selection ----===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"
#include "talos/Dialect/Schedule/ScheduleFields.h"

using namespace mlir;

namespace talos::bytecode {

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
    write32(bytes, 0, TALOS_RT_DESCRIPTOR_EVENT);
    write64(bytes, 8, UINT64_MAX);
    write64(bytes, 16, UINT64_MAX);
    emit({Constant, 0, destination, 0, 0, 0, 0, addRawConstant(bytes)});
    return success();
  }
  if (auto op = dyn_cast<sim::SimContextStorageOp>(operation))
    return encodeHandle(plan, op.getResult(), op.getId(), state.storage,
                        TALOS_RT_DESCRIPTOR_STORAGE);
  if (auto op = dyn_cast<sim::SimContextNetOp>(operation))
    return encodeHandle(plan, op.getResult(), op.getId(), state.nets,
                        TALOS_RT_DESCRIPTOR_NET);
  if (auto op = dyn_cast<sim::SimContextDriverOp>(operation))
    return encodeHandle(plan, op.getResult(), op.getId(), state.drivers,
                        TALOS_RT_DESCRIPTOR_DRIVER);
  if (auto op = dyn_cast<sim::SimContextEventOp>(operation)) {
    emit({MakeHandle, 0, reg(plan, op.getResult()), TALOS_RT_DESCRIPTOR_EVENT,
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
    return normalizeAggregateRegister(plan, op.getResult());
  }
  if (auto op = dyn_cast<sim::SimNetReadOp>(operation)) {
    emit({LoadState, 0, reg(plan, op.getResult()), reg(plan, op.getNet())});
    return normalizeAggregateRegister(plan, op.getResult());
  }
  if (auto op = dyn_cast<sim::SimDriverReadOp>(operation)) {
    emit({LoadState, 0, reg(plan, op.getResult()), reg(plan, op.getDriver())});
    return normalizeAggregateRegister(plan, op.getResult());
  }
  if (auto op = dyn_cast<sim::SimRefStoreOp>(operation)) {
    uint32_t reference = reg(plan, op.getReference());
    uint32_t value = reg(plan, op.getValue());
    uint8_t valueKind = plan.layouts[value].kind;
    // Managed object words use the dedicated change-publication path in the
    // interpreter; packed continuous-value retention does not apply to them.
    bool supportsContinuousStore = valueKind == Bits || valueKind == Logic ||
                                   valueKind == String || valueKind == Real32 ||
                                   valueKind == Real64;
    sim::EntryKind entryKind = plan.function.getEntryKind();
    bool continuous = supportsContinuousStore &&
                      (sim::has<sim::metadata::ContinuousStore>(op) ||
                       entryKind == sim::EntryKind::Continuous ||
                       entryKind == sim::EntryKind::PortInput ||
                       entryKind == sim::EntryKind::PortOutput);
    emit({StoreState,
          static_cast<uint16_t>(
              continuous ? TALOS_RT_DB_STORE_STATE_CONTINUOUS : 0),
          0, reference, value});
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
         static_cast<uint16_t>(op.getIsAssign() ? TALOS_RT_DB_OVERRIDE_ASSIGN
                                                : TALOS_RT_DB_OVERRIDE_FORCE),
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
    uint16_t flags = op.getIsAssign() ? TALOS_RT_DB_OVERRIDE_ASSIGN
                                      : TALOS_RT_DB_OVERRIDE_FORCE;
    flags |= TALOS_RT_DB_OVERRIDE_DYNAMIC;
    if (op.getClaim())
      flags |= TALOS_RT_DB_OVERRIDE_CLAIM;
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
         static_cast<uint16_t>(op.getIsAssign() ? TALOS_RT_DB_OVERRIDE_ASSIGN
                                                : TALOS_RT_DB_OVERRIDE_FORCE),
         0, reg(plan, op.getTarget())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimDriverDriveOp>(operation)) {
    uint16_t flags = ::talos::schedule::has<
                         ::talos::schedule::Field::DeferNetResolution>(op)
                         ? TALOS_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION
                         : 0;
    emit({StoreState, flags, 0, reg(plan, op.getDriver()),
          reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimDriverDriveDelayedNetOp>(operation)) {
    uint16_t flags = op.getDeferResolution()
                         ? TALOS_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION
                         : 0;
    emit({StoreState, flags, 0, reg(plan, op.getDriver()),
          reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimDriverDriveChangedOp>(operation)) {
    uint16_t flags = TALOS_RT_DB_STORE_STATE_CHANGED;
    if (::talos::schedule::has<
            ::talos::schedule::Field::DeferNetResolution>(op))
      flags |= TALOS_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION;
    emit({StoreState, flags, reg(plan, op.getChanged()),
          reg(plan, op.getDriver()), reg(plan, op.getValue())});
    return success();
  }
  return std::nullopt;
}

} // namespace talos::bytecode
