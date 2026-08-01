//===- SimulationManagedRootInstrumentation.cpp - Precise GC roots ------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Analysis/Liveness.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/SetVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMINSTRUMENTMANAGEDROOTSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace detail {
namespace {

bool managedOperationMayCollect(Operation *operation) {
  return isa<
      sim::SimClassAllocOp, sim::SimClassCopyOp, sim::SimWeakCreateOp,
      sim::SimReferencePathIndexOp, sim::SimReferencePathAssocOp,
      sim::SimContainerCreateLikeOp, sim::SimContainerCreateOp,
      sim::SimContainerCloneOp, sim::SimContainerWriteOp, sim::SimQueueInsertOp,
      sim::SimAssocCreateOp, sim::SimAssocWriteOp, sim::SimAssocSetDefaultOp,
      sim::SimAssocTraverseOp, sim::SimArgumentRefStoreOp,
      sim::SimReferencePathNBAEnqueueOp, sim::SimGCSafepointOp,
      sim::SimStringLiteralOp, sim::SimStringFromPackedOp,
      sim::SimStringConcatOp, sim::SimStringRepeatOp, sim::SimStringPutcOp,
      sim::SimStringSubstrOp, sim::SimStringCaseConvertOp,
      sim::SimStringFormatIntegerOp, sim::SimStringFormatRealOp,
      sim::SimStringScanFieldOp,
      sim::SimFileGetlineStringOp, sim::SimFileErrorStringOp,
      sim::SimPlusargValueOp, sim::SimCallOp,
      sim::SimClassDirectCallOp,
      sim::SimClassVirtualCallOp, sim::SimDPICallOp>(operation);
}

} // namespace

LogicalResult instrumentManagedRoots(ModuleOp module) {
  SmallVector<sim::SimFuncOp> functions;
  module.walk([&](sim::SimFuncOp function) {
    if (!function.isExternal())
      functions.push_back(function);
  });
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  Type rootType =
      LLVM::LLVMStructType::getLiteral(context, {pointer, i64, pointer, i64});
  for (sim::SimFuncOp function : functions) {
    Liveness liveness(function);
    struct ManagedSSAValue {
      Value value;
      uint64_t bitOffset;
    };
    SmallVector<ManagedSSAValue> handles;
    SmallVector<Operation *> collectionPoints;
    auto collectHandles = [&](Value value) -> LogicalResult {
      SmallVector<uint64_t, 2> offsets;
      if (isa<sim::ManagedRefType, sim::ArgumentRefType>(value.getType()))
        offsets.push_back(0);
      else if (!sim::getManagedHandleOffsets(value.getType(), offsets))
        return failure();
      for (uint64_t offset : offsets)
        handles.push_back({value, offset});
      return success();
    };
    for (Block &block : function.getBody()) {
      for (BlockArgument argument : block.getArguments())
        if (failed(collectHandles(argument)))
          return function.emitError(
              "block argument has no fixed managed root layout");
      for (Operation &operation : block) {
        if (managedOperationMayCollect(&operation))
          collectionPoints.push_back(&operation);
        for (Value result : operation.getResults())
          if (failed(collectHandles(result)))
            return operation.emitError(
                "result has no fixed managed root layout");
      }
    }
    if (handles.empty() || collectionPoints.empty())
      continue;
    llvm::erase_if(handles, [&](const ManagedSSAValue &handle) {
      return llvm::none_of(collectionPoints, [&](Operation *point) {
        const LivenessBlockInfo *blockInfo =
            liveness.getLiveness(point->getBlock());
        if (!blockInfo)
          return false;
        Liveness::ValueSetT live = blockInfo->currentlyLiveValues(point);
        return live.contains(handle.value) &&
               handle.value.getDefiningOp() != point;
      });
    });
    if (handles.empty())
      continue;

    OpBuilder builder(context);
    Block &entry = function.getBody().front();
    builder.setInsertionPointToStart(&entry);
    Location location = function.getLoc();
    Value rootCount = llvmConstant(builder, location, i64, handles.size());
    SmallVector<Value> slots;
    slots.reserve(handles.size());
    Value slotsBase = LLVM::AllocaOp::create(builder, location, pointer,
                                             pointer, rootCount, 8);
    for (size_t index = 0; index != handles.size(); ++index)
      slots.push_back(
          byteGEP(builder, location, slotsBase, index * sizeof(void *)));
    Value one = llvmConstant(builder, location, i64, 1);
    Value record =
        LLVM::AllocaOp::create(builder, location, pointer, rootType, one, 8);
    record.getDefiningOp()->setAttr(managedRootRangeRecordAttr,
                                    builder.getUnitAttr());

    // Each coroutine activation owns roots only while it is running on the
    // current worker lane. Pop before suspension and reacquire on the resume
    // block so a continuation may migrate to another host thread.
    llvm::SetVector<Block *> activationBlocks;
    activationBlocks.insert(&entry);
    function.walk([&](Operation *operation) {
      if (sim::isSuspensionOp(operation) &&
          operation->getNumSuccessors() != 0)
        activationBlocks.insert(operation->getSuccessor(0));
    });
    DenseMap<Block *, Operation *> activationEnds;
    for (Block *block : activationBlocks) {
      if (block == &entry)
        builder.setInsertionPointAfter(record.getDefiningOp());
      else
        builder.setInsertionPointToStart(block);
      Value contextAddress = LLVM::AddressOfOp::create(
          builder, location, pointer, "__obelisk_current_context");
      Value runtimeContext =
          LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
      Value lane =
          LLVM::CallOp::create(
              builder, location, TypeRange{pointer},
              SymbolRefAttr::get(context, "obelisk_rt_v1_gc_current_lane"),
              runtimeContext)
              .getResult();
      Operation *last = lane.getDefiningOp();
      for (Value slot : slots)
        LLVM::StoreOp::create(builder, location,
                              LLVM::ZeroOp::create(builder, location, pointer),
                              slot, 8);
      LLVM::StoreOp::create(builder, location,
                            LLVM::ZeroOp::create(builder, location, rootType),
                            record, 8);
      Value status =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context,
                                 "obelisk_rt_v1_gc_managed_root_range_push"),
              ValueRange{lane, record, slotsBase, rootCount})
              .getResult();
      Operation *check =
          reportManagedStatus(builder, location, runtimeContext, status);
      check->setAttr(managedRootRangePushCheckAttr, builder.getUnitAttr());
      last = check;
      activationEnds[block] = last;
    }

    for (auto [handle, slot] : llvm::zip_equal(handles, slots)) {
      if (auto argument = dyn_cast<BlockArgument>(handle.value)) {
        Block *owner = argument.getOwner();
        if (Operation *activationEnd = activationEnds.lookup(owner))
          builder.setInsertionPointAfter(activationEnd);
        else
          builder.setInsertionPointToStart(owner);
      } else {
        builder.setInsertionPointAfter(cast<OpResult>(handle.value).getOwner());
      }
      sim::SimClassRootBindOp::create(
          builder, handle.value.getLoc(), handle.value, slot,
          builder.getI64IntegerAttr(handle.bitOffset));
    }

    // A root slot outlives its SSA value and is reused on loop backedges.
    // Clear values that are dead at each collection-capable operation so weak
    // reachability follows SSA liveness and a later collection never observes
    // a pointer into an already reclaimed span.
    for (Operation *collectionPoint : collectionPoints) {
      const LivenessBlockInfo *blockInfo =
          liveness.getLiveness(collectionPoint->getBlock());
      if (!blockInfo)
        continue;
      Liveness::ValueSetT live =
          blockInfo->currentlyLiveValues(collectionPoint);
      builder.setInsertionPoint(collectionPoint);
      for (auto [handle, slot] : llvm::zip_equal(handles, slots)) {
        if (live.contains(handle.value) &&
            handle.value.getDefiningOp() != collectionPoint)
          continue;
        LLVM::StoreOp::create(
            builder, collectionPoint->getLoc(),
            LLVM::ZeroOp::create(builder, collectionPoint->getLoc(), pointer),
            slot, 8);
      }
    }

    SmallVector<Operation *> exits;
    function.walk([&](Operation *operation) {
      if (isa<sim::SimReturnOp>(operation) ||
          sim::isSuspensionOp(operation))
        exits.push_back(operation);
    });
    for (Operation *exit : exits) {
      builder.setInsertionPoint(exit);
      emitManagedRootRangePop(builder, exit->getLoc(), function);
    }
  }
  return success();
}

} // namespace detail

namespace {

class ObeliskSimInstrumentManagedRootsPass
    : public impl::ObeliskSimInstrumentManagedRootsPassBase<
          ObeliskSimInstrumentManagedRootsPass> {
public:
  void runOnOperation() override {
    if (failed(detail::instrumentManagedRoots(getOperation())))
      signalPassFailure();
  }
};

} // namespace
} // namespace obelisk
