//===- SimulationWaitLowering.cpp - Runtime wait-record lowering ----------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TypeSwitch.h"

#include <limits>

using namespace mlir;

namespace obelisk::detail {

LogicalResult
serializeRuntimeWait(Operation *operation, Value wait, uint32_t kind,
                     uint32_t count, OpBuilder &builder,
                     SmallVectorImpl<Operation *> &observerBindings) {
  constexpr uint64_t waitHeaderSize = sizeof(obelisk_rt_wait_record_v1);
  constexpr uint64_t waitEntrySize = sizeof(obelisk_rt_wait_entry_v1);
  constexpr uint32_t noEdge = std::numeric_limits<uint32_t>::max();
  Location location = operation->getLoc();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();

  storeAt(builder, location, wait, 0,
          llvmConstant(builder, location, i32, OBELISK_RT_VERSION), 4);
  storeAt(builder, location, wait, 4,
          llvmConstant(builder, location, i32, kind), 4);
  uint32_t waitFlags = 0;
  if (auto join = dyn_cast<sim::SimSuspendJoinOp>(operation))
    waitFlags = static_cast<uint32_t>(join.getKind());
  else if (isa<sim::SimSuspendLevelOp>(operation))
    waitFlags = OBELISK_RT_WAIT_LEVEL_TRUE;
  else if (isa<sim::SimSuspendEdgeIffOp>(operation))
    waitFlags = OBELISK_RT_WAIT_EDGE_IFF;
  else if (isa<sim::SimSuspendClockSetOp>(operation)) {
    waitFlags = OBELISK_RT_WAIT_CLOCK_OCCURRENCE;
    if (operation->hasAttr("slot_final"))
      waitFlags |= OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL;
    auto clocks = cast<sim::SimSuspendClockSetOp>(operation);
    if (llvm::any_of(clocks.getConditions(), [](Value condition) {
          return getConvertedObserverBinding(condition) != nullptr;
        }))
      waitFlags |= OBELISK_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS;
  } else if (auto mailbox = dyn_cast<sim::SimSuspendMailboxOp>(operation))
    waitFlags = static_cast<uint32_t>(mailbox.getKind());
  if ((operation->hasAttr(sim::metadata::topLevelWildcardWait) ||
      operation->hasAttr(sim::metadata::proceduralEventWait)) &&
      isa<sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
          sim::SimSuspendEdgeIffOp, sim::SimSuspendAnyOp>(operation))
    waitFlags |= OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF;
  storeAt(builder, location, wait, 8,
          llvmConstant(builder, location, i32, waitFlags), 4);
  storeAt(builder, location, wait, 12,
          llvmConstant(builder, location, i32, count), 4);
  Value payload = llvmConstant(builder, location, i64, 0);
  if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation))
    payload = asI64(builder, location, delay.getDelay());
  else if (auto semaphore = dyn_cast<sim::SimSuspendSemaphoreOp>(operation))
    payload = asI64(builder, location, semaphore.getKeys());
  else if (auto clocks = dyn_cast<sim::SimSuspendClockSetOp>(operation))
    payload = llvmConstant(builder, location, i64, clocks.getOccurrenceSite());
  storeAt(builder, location, wait, 16, payload, 8);
  uint64_t auxiliary = 0;
  if (auto clocks = dyn_cast<sim::SimSuspendClockSetOp>(operation))
    for (auto [index, condition] :
         llvm::enumerate(clocks.getConditionIndices()))
      if (condition >= 0)
        auxiliary |= uint64_t{1} << index;
  storeAt(builder, location, wait, 24,
          llvmConstant(builder, location, i64, auxiliary), 8);

  SmallVector<Value> watched;
  SmallVector<uint32_t> watchedEdges;
  TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendChangeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        watchedEdges.push_back(static_cast<uint32_t>(sim::EdgeKind::Change));
      })
      .Case<sim::SimSuspendLevelOp>([&](auto op) {
        watched.push_back(op.getWatched());
        watchedEdges.push_back(static_cast<uint32_t>(sim::EdgeKind::Change));
      })
      .Case<sim::SimSuspendEdgeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        watchedEdges.push_back(static_cast<uint32_t>(op.getEdge()));
      })
      .Case<sim::SimSuspendEdgeIffOp>([&](auto op) {
        watched.push_back(op.getWatched());
        watchedEdges.push_back(static_cast<uint32_t>(op.getEdge()));
        watched.push_back(op.getCondition());
        watchedEdges.push_back(noEdge);
      })
      .Case<sim::SimSuspendAnyOp>([&](auto op) {
        llvm::append_range(watched, op.getWatched());
        for (int32_t edge : op.getEdges())
          watchedEdges.push_back(static_cast<uint32_t>(edge));
      })
      .Case<sim::SimSuspendClockSetOp>([&](auto op) {
        llvm::append_range(watched, op.getPrimaries());
        for (int32_t edge : op.getEdges())
          watchedEdges.push_back(static_cast<uint32_t>(edge));
        llvm::append_range(watched, op.getConditions());
        auto predicates = op->template getAttrOfType<DenseI32ArrayAttr>(
            "condition_predicates");
        for (auto [index, condition] : llvm::enumerate(op.getConditions())) {
          uint32_t predicate = predicates ? predicates[index] : 0;
          watchedEdges.push_back(
              getConvertedObserverBinding(condition)
                  ? OBELISK_RT_WAIT_CONDITION_OBSERVER + predicate
                  : (predicates
                         ? OBELISK_RT_WAIT_CONDITION_PREDICATE + predicate
                         : noEdge));
        }
      })
      .Case<sim::SimSuspendEventOp>([&](auto op) {
        watched.push_back(op.getEvent());
        watchedEdges.push_back(noEdge);
      })
      .Case<sim::SimSuspendEventOrderOp>([&](auto op) {
        llvm::append_range(watched, op.getEvents());
        watchedEdges.append(op.getEvents().size(), noEdge);
      })
      .Case<sim::SimSuspendMailboxOp>([&](auto op) {
        watched.push_back(op.getMailbox());
        watchedEdges.push_back(noEdge);
      })
      .Case<sim::SimSuspendSemaphoreOp>([&](auto op) {
        watched.push_back(op.getSemaphore());
        watchedEdges.push_back(noEdge);
      })
      .Case<sim::SimSuspendAwaitOp>([&](auto op) {
        watched.push_back(op.getProcess());
        watchedEdges.push_back(noEdge);
      })
      .Case<sim::SimSuspendJoinOp>([&](auto op) {
        llvm::append_range(watched, op.getProcesses());
        watchedEdges.append(op.getProcesses().size(), noEdge);
      });
  if (watched.size() != watchedEdges.size())
    return operation->emitError("wait handle and edge inventories disagree");
  auto waitWidths =
      operation->getAttrOfType<DenseI32ArrayAttr>("obelisk.coro.wait_widths");
  if (!watched.empty() &&
      (!waitWidths || static_cast<size_t>(waitWidths.size()) != watched.size()))
    return operation->emitError("wait handle and width inventories disagree");
  uint64_t captureCursor = 0;
  uint64_t capturesOffset = waitHeaderSize + uint64_t{count} * waitEntrySize;
  for (auto [index, value] : llvm::enumerate(watched)) {
    uint64_t entryOffset = waitHeaderSize + index * waitEntrySize;
    Operation *binding = getConvertedObserverBinding(value);
    if (binding) {
      auto observerID =
          binding->getAttrOfType<IntegerAttr>("obelisk.coro.observer_id");
      auto captures = getConvertedObserverCaptures(binding);
      if (!observerID || captures.size() > UINT32_MAX)
        return binding->emitOpError("has malformed clock-condition metadata");
      storeAt(builder, location, wait, entryOffset,
              llvmConstant(builder, location, i64,
                           observerID.getValue().getZExtValue()),
              8);
      storeAt(builder, location, wait, entryOffset + 12,
              llvmConstant(builder, location, i32,
                           static_cast<uint32_t>(captures.size())),
              4);
      for (Value capture : captures) {
        uint64_t offset =
            capturesOffset +
            captureCursor++ * sizeof(obelisk_rt_computed_capture_v1);
        storeAt(builder, location, wait, offset,
                asI64(builder, location, capture), 8);
        for (uint64_t word = 1; word != 4; ++word)
          storeAt(builder, location, wait, offset + word * 8,
                  llvmConstant(builder, location, i64, 0), 8);
      }
      if (!llvm::is_contained(observerBindings, binding))
        observerBindings.push_back(binding);
    } else {
      storeAt(builder, location, wait, entryOffset,
              asI64(builder, location, value), 8);
      storeAt(builder, location, wait, entryOffset + 12,
              llvmConstant(builder, location, i32,
                           static_cast<uint32_t>(waitWidths[index])),
              4);
    }
    storeAt(builder, location, wait, entryOffset + 8,
            llvmConstant(builder, location, i32, watchedEdges[index]), 4);
  }
  return success();
}

} // namespace obelisk::detail
