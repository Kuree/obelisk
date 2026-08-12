//===- SimulationProcessCoroutineLowering.cpp - Native process lowering ===//

#include "SimulationProcessCoroutineLowering.h"
#include "SimulationProcessRuntimeABI.h"
#include "SimulationProcessWrapperLowering.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Conversion/SimulationTimeLowering.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/SetVector.h"

#include <cstddef>
#include <limits>

using namespace mlir;

namespace obelisk::detail {

namespace {

uint32_t suspensionKind(Operation *operation) {
  return TypeSwitch<Operation *, uint32_t>(operation)
      .Case<sim::SimSuspendDelayOp>(
          [](auto) { return OBELISK_RT_SUSPEND_DELAY; })
      .Case<sim::SimSuspendChangeOp, sim::SimSuspendLevelOp>(
          [](auto) { return OBELISK_RT_SUSPEND_CHANGE; })
      .Case<sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
            sim::SimSuspendAnyOp>([](auto) { return OBELISK_RT_SUSPEND_EDGE; })
      .Case<sim::SimSuspendEventOp>(
          [](auto) { return OBELISK_RT_SUSPEND_EVENT; })
      .Case<sim::SimSuspendAwaitOp>(
          [](auto) { return OBELISK_RT_SUSPEND_AWAIT; })
      .Case<sim::SimSuspendJoinOp>([](auto) { return OBELISK_RT_SUSPEND_JOIN; })
      .Case<sim::SimSuspendForeverOp>(
          [](auto) { return OBELISK_RT_SUSPEND_FOREVER; })
      .Case<sim::SimSuspendChildrenOp>(
          [](auto) { return OBELISK_RT_SUSPEND_CHILDREN; })
      .Case<sim::SimSuspendObserveOp>(
          [](auto) { return OBELISK_RT_SUSPEND_OBSERVER; })
      .Default([](Operation *) { return OBELISK_RT_SUSPEND_NONE; });
}

void addFrameAttributes(LLVM::LLVMFuncOp ramp,
                        const SimulationProcessFrameAnalysis &analysis,
                        OpBuilder &builder) {
  ramp->setAttr("obelisk.frame.size",
                builder.getI64IntegerAttr(analysis.getFrameSize()));
  ramp->setAttr("obelisk.frame.alignment",
                builder.getI64IntegerAttr(analysis.getFrameAlignment()));
  ramp->setAttr("obelisk.frame.checksum",
                builder.getI64IntegerAttr(analysis.getChecksum()));
  SmallVector<int32_t> continuationIDs;
  for (uint32_t continuation : analysis.getContinuations())
    continuationIDs.push_back(static_cast<int32_t>(continuation));
  ramp->setAttr("obelisk.frame.continuations",
                builder.getDenseI32ArrayAttr(continuationIDs));
  SmallVector<Attribute> fields;
  for (const ProcessFrameField &field : analysis.getFields()) {
    NamedAttrList attributes;
    attributes.set(
        "kind", builder.getI32IntegerAttr(static_cast<uint32_t>(field.kind)));
    attributes.set(
        "flags", builder.getI32IntegerAttr(static_cast<uint32_t>(field.flags)));
    attributes.set("offset", builder.getI64IntegerAttr(field.offset));
    attributes.set("size", builder.getI64IntegerAttr(field.size));
    attributes.set("alignment", builder.getI32IntegerAttr(field.alignment));
    attributes.set("reserved", builder.getI32IntegerAttr(field.reserved));
    fields.push_back(attributes.getDictionary(builder.getContext()));
  }
  ramp->setAttr("obelisk.frame.fields", builder.getArrayAttr(fields));
}

struct RampBlocks {
  Block *suspendReturn;
  Block *terminate;
  Block *cleanup;
  DenseMap<Block *, Block *> shims;
};

Block *makeCoroutineReturnBlock(Region &region, Location location,
                                Value handle) {
  Block *block = new Block;
  region.push_back(block);
  OpBuilder builder(block, block->begin());
  Value unwind = llvmConstant(builder, location, builder.getI1Type(), 0);
  Value none = LLVM::NoneTokenOp::create(builder, location);
  LLVM::CoroEndOp::create(builder, location, builder.getI1Type(), handle,
                          unwind, none);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return block;
}

LogicalResult storeFrameValue(OpBuilder &builder, Location location,
                              Value frame, Value value, uint64_t offset,
                              uint32_t alignment) {
  if (!isa<IntegerType, Float64Type, LLVM::LLVMPointerType>(value.getType()))
    return failure();
  storeAt(builder, location, frame, offset, value, alignment);
  return success();
}

LogicalResult
lowerSuspendTerminator(Operation *operation, Value instance, Value handle,
                       const SimulationProcessFrameAnalysis &analysis,
                       const RampBlocks &blocks) {
  IRRewriter builder(operation->getContext());
  builder.setInsertionPoint(operation);
  Location location = operation->getLoc();
  auto branch = cast<BranchOpInterface>(operation);
  auto continuationAttr =
      operation->getAttrOfType<IntegerAttr>("obelisk.coro.continuation");
  auto waitOffsetAttr =
      operation->getAttrOfType<IntegerAttr>("obelisk.coro.wait_offset");
  auto waitSizeAttr =
      operation->getAttrOfType<IntegerAttr>("obelisk.coro.wait_size");
  if (!continuationAttr || !waitOffsetAttr || !waitSizeAttr)
    return operation->emitError("missing coroutine frame analysis metadata");
  uint32_t continuationID = continuationAttr.getInt();
  Block *continuation = operation->getSuccessor(0);
  ArrayRef<ProcessFrameValue> layout =
      analysis.getContinuationLayout(continuationID);
  SmallVector<Value> operands(
      branch.getSuccessorOperands(0).getForwardedOperands().begin(),
      branch.getSuccessorOperands(0).getForwardedOperands().end());
  Value frame = loadAt(builder, location, instance, kInstanceFrameOffset,
                       LLVM::LLVMPointerType::get(builder.getContext()), 8);
  size_t physical = 0;
  for (const ProcessFrameValue &slot : layout) {
    if (physical >= operands.size() ||
        failed(storeFrameValue(builder, location, frame, operands[physical++],
                               slot.valueOffset, slot.alignment)))
      return operation->emitError("cannot store continuation value in frame");
    if (slot.hasSecondaryStorage()) {
      if (physical >= operands.size() ||
          failed(storeFrameValue(builder, location, frame, operands[physical++],
                                 slot.getSecondaryOffset(), slot.alignment)))
        return operation->emitError(
            "cannot store secondary continuation value in frame");
    }
  }
  if (physical != operands.size())
    return operation->emitError(
        "converted continuation arity disagrees with frame analysis");

  if (auto control = dyn_cast<sim::SimProcessControlOp>(operation)) {
    Type i32 = builder.getI32Type();
    Type i64 = builder.getI64Type();
    Value disposition = entryAlloca(builder, location, i32, 1, 4);
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32,
                                       OBELISK_RT_PROCESS_CONTROL_CONTINUE),
                          disposition, 4);
    auto [context, lane] = managedContextAndLane(builder, location);
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(builder.getContext(),
                               "obelisk_rt_v1_process_control"),
            ValueRange{context, control.getProcess(),
                       llvmConstant(builder, location, i32,
                                    static_cast<uint32_t>(control.getKind())),
                       disposition})
            .getResult();

    Region *region = operation->getParentRegion();
    Block *dispatch = new Block;
    Block *continueAction = new Block;
    Block *suspendAction = new Block;
    Block *killAction = new Block;
    Block *yield = new Block;
    Block *failed = new Block;
    failed->addArgument(i32, location);
    region->push_back(dispatch);
    region->push_back(continueAction);
    region->push_back(suspendAction);
    region->push_back(killAction);
    region->push_back(yield);
    region->push_back(failed);

    Value succeeded = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, status,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    LLVM::CondBrOp::create(builder, location, succeeded, dispatch, failed,
                           status);

    builder.setInsertionPointToStart(dispatch);
    Value selected =
        LLVM::LoadOp::create(builder, location, i32, disposition, 4);
    SmallVector<APInt> cases{
        APInt(32, OBELISK_RT_PROCESS_CONTROL_CONTINUE),
        APInt(32, OBELISK_RT_PROCESS_CONTROL_SUSPEND_CURRENT),
        APInt(32, OBELISK_RT_PROCESS_CONTROL_KILL_CURRENT)};
    SmallVector<Block *> destinations{continueAction, suspendAction,
                                      killAction};
    SmallVector<ValueRange> destinationOperands(3);
    LLVM::SwitchOp::create(
        builder, location, selected, failed,
        ValueRange{
            llvmConstant(builder, location, i32, OBELISK_RT_INVALID_ARGUMENT)},
        cases, destinations, destinationOperands, ArrayRef<int32_t>{});

    builder.setInsertionPointToStart(continueAction);
    LLVM::BrOp::create(builder, location, blocks.shims.lookup(continuation));

    builder.setInsertionPointToStart(suspendAction);
    publishAction(builder, location, instance,
                  OBELISK_RT_FRAGMENT_PROCESS_SUSPEND, OBELISK_RT_SUSPEND_NONE,
                  continuationID, OBELISK_RT_FRAGMENT_FLAGS_NONE,
                  llvmConstant(builder, location, i64, 0), 0);
    LLVM::BrOp::create(builder, location, yield);

    builder.setInsertionPointToStart(killAction);
    publishAction(builder, location, instance, OBELISK_RT_FRAGMENT_TERMINATE,
                  OBELISK_RT_SUSPEND_NONE, 0, OBELISK_RT_FRAGMENT_FLAGS_NONE,
                  llvmConstant(builder, location, i64, 0), 0);
    LLVM::BrOp::create(builder, location, yield);

    builder.setInsertionPointToStart(failed);
    LLVM::CallOp::create(builder, location, TypeRange{},
                         SymbolRefAttr::get(builder.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{context, failed->getArgument(0)});
    if (!blocks.terminate)
      return control.emitOpError("has no coroutine termination block");
    LLVM::BrOp::create(builder, location, blocks.terminate);

    builder.setInsertionPointToStart(yield);
    Value final = llvmConstant(builder, location, builder.getI1Type(), 0);
    Value save = LLVM::CoroSaveOp::create(
        builder, location, LLVM::LLVMTokenType::get(builder.getContext()),
        handle);
    Value state = LLVM::CoroSuspendOp::create(builder, location,
                                              builder.getI8Type(), save, final);
    SmallVector<Block *> resumeDestinations{blocks.shims.lookup(continuation),
                                            blocks.cleanup};
    SmallVector<ValueRange> resumeOperands(2);
    SmallVector<APInt> resumeCases{APInt(8, 0), APInt(8, 1)};
    LLVM::SwitchOp::create(builder, location, state, blocks.suspendReturn,
                           ValueRange{}, resumeCases, resumeDestinations,
                           resumeOperands, ArrayRef<int32_t>{});
    builder.eraseOp(operation);
    return success();
  }

  if (auto task = dyn_cast<sim::SimClassVirtualTaskCallOp>(operation)) {
    Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
    Type i32 = builder.getI32Type();
    Type i64 = builder.getI64Type();
    auto sizesAttr =
        task->getAttrOfType<DenseI64ArrayAttr>(nativeMethodArgumentSizesAttr);
    auto rootsAttr =
        task->getAttrOfType<DenseI64ArrayAttr>(nativeMethodArgumentRootsAttr);
    auto referencesAttr =
        task->getAttrOfType<DenseI64ArrayAttr>(nativeTransferredReferencesAttr);
    if (!sizesAttr ||
        static_cast<uint64_t>(sizesAttr.size()) != task.getArguments().size() ||
        !rootsAttr || (rootsAttr.size() % 4) != 0 || !referencesAttr)
      return task.emitOpError("has malformed native argument metadata");
    Type argumentType =
        LLVM::LLVMStructType::getLiteral(builder.getContext(), {pointer, i64});
    Value argumentArray = LLVM::ZeroOp::create(builder, location, pointer);
    SmallVector<Value> argumentStorage;
    if (!task.getArguments().empty()) {
      argumentArray = entryAlloca(builder, location, argumentType,
                                  task.getArguments().size(), 8);
      for (auto [index, argument] : llvm::enumerate(task.getArguments())) {
        if (sizesAttr[index] <= 0)
          return task.emitOpError("has an invalid native argument size");
        Value data = entryAlloca(builder, location, argument.getType(), 1, 8);
        argumentStorage.push_back(data);
        LLVM::StoreOp::create(builder, location, argument, data, 1);
        Value record = LLVM::ZeroOp::create(builder, location, argumentType);
        record = insertValue(builder, location, record, data, 0);
        record = insertValue(
            builder, location, record,
            llvmConstant(builder, location, i64, sizesAttr[index]), 1);
        LLVM::StoreOp::create(
            builder, location, record,
            byteGEP(builder, location, argumentArray, index * 16), 8);
      }
    }
    Value outActivation = entryAlloca(builder, location, i64, 1, 8);
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i64, 0),
                          outActivation, 8);
    storeAt(builder, location, instance, kInstanceContinuationOffset,
            llvmConstant(builder, location, i32, continuationID), 4);
    auto [context, lane] = managedContextAndLane(builder, location);
    Block *dispatch = new Block;
    Block *activated = new Block;
    Block *failed = new Block;
    failed->addArgument(i32, location);
    operation->getParentRegion()->push_back(dispatch);
    operation->getParentRegion()->push_back(activated);
    operation->getParentRegion()->push_back(failed);

    Value rootRecord;
    if (!rootsAttr.empty()) {
      Type rootType = LLVM::LLVMStructType::getLiteral(
          builder.getContext(), {pointer, i64, pointer, i64});
      uint64_t rootCount = rootsAttr.size() / 4;
      Value count = llvmConstant(builder, location, i64, rootCount);
      Value rootSlots = entryAlloca(builder, location, i64, rootCount, 8);
      rootRecord = entryAlloca(builder, location, rootType, 1, 8);
      LLVM::StoreOp::create(builder, location,
                            LLVM::ZeroOp::create(builder, location, rootType),
                            rootRecord, 8);
      for (uint64_t index = 0; index != rootCount; ++index) {
        int64_t argumentIndex = rootsAttr[index * 4];
        int64_t byteOffset = rootsAttr[index * 4 + 1];
        int64_t kindMask = rootsAttr[index * 4 + 2];
        int64_t conditional = rootsAttr[index * 4 + 3];
        if (argumentIndex < 0 ||
            static_cast<uint64_t>(argumentIndex) >= argumentStorage.size() ||
            byteOffset < 0 || sizesAttr[argumentIndex] < 0 ||
            kindMask <= 0 ||
            static_cast<uint64_t>(kindMask) >
                OBELISK_RT_MANAGED_ROOT_KIND_ALL ||
            (conditional != 0 && conditional != 1) ||
            static_cast<uint64_t>(byteOffset) >
                static_cast<uint64_t>(sizesAttr[argumentIndex]) ||
            sizeof(void *) > static_cast<uint64_t>(sizesAttr[argumentIndex]) -
                                  static_cast<uint64_t>(byteOffset))
          return task.emitOpError("has an invalid native argument root");
        Value root = LLVM::LoadOp::create(
            builder, location, i64,
            byteGEP(builder, location, argumentStorage[argumentIndex],
                    byteOffset),
            1);
        if (conditional)
          root = LLVM::CallOp::create(
                     builder, location, TypeRange{i64},
                     SymbolRefAttr::get(builder.getContext(),
                                        "obelisk_rt_v1_gc_candidate_root"),
                     ValueRange{context, root,
                                llvmConstant(builder, location, i32,
                                             kindMask)})
                     .getResult();
        LLVM::StoreOp::create(
            builder, location, root,
            byteGEP(builder, location, rootSlots, index * sizeof(void *)), 8);
      }
      Value pushStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(builder.getContext(),
                                 "obelisk_rt_v1_gc_managed_root_range_push"),
              ValueRange{lane, rootRecord, rootSlots, count})
              .getResult();
      Value pushed = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, pushStatus,
          llvmConstant(builder, location, i32, OBELISK_RT_OK));
      LLVM::CondBrOp::create(builder, location, pushed, dispatch, failed,
                             pushStatus);
    } else {
      LLVM::BrOp::create(builder, location, dispatch);
    }

    builder.setInsertionPointToStart(dispatch);
    SmallVector<Value> activationArguments{
        lane, managedObjectPointer(builder, location, task.getReceiver())};
    StringRef activationName = "obelisk_rt_v1_method_task_activate";
    auto method =
        SymbolTable::lookupNearestSymbolFrom<sim::SimClassMethodDeclOp>(
            task, task.getMethodAttr());
    auto owner =
        method ? SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                     method, method.getOwnerAttr())
               : sim::SimClassDeclOp{};
    if (!method || !owner)
      return task.emitOpError("virtual task descriptor is missing");
    if (owner.getIsInterface()) {
      if (!method.getInterfaceOrdinalAttr())
        return task.emitOpError("interface task has no dispatch ordinal");
      activationName = "obelisk_rt_v1_interface_method_task_activate";
      activationArguments.push_back(
          llvmConstant(builder, location, i64, owner.getId()));
      activationArguments.push_back(
          llvmConstant(builder, location, i64, *method.getInterfaceOrdinal()));
    } else {
      activationArguments.push_back(
          llvmConstant(builder, location, i64, task.getSlot()));
    }
    activationArguments.push_back(
        llvmConstant(builder, location, i64, task.getSignatureId()));
    activationArguments.push_back(argumentArray);
    activationArguments.push_back(
        llvmConstant(builder, location, i32, task.getArguments().size()));
    activationArguments.push_back(outActivation);
    Value status = LLVM::CallOp::create(
                       builder, location, TypeRange{i32},
                       SymbolRefAttr::get(builder.getContext(), activationName),
                       activationArguments)
                       .getResult();
    if (rootRecord) {
      Value popStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(builder.getContext(),
                                 "obelisk_rt_v1_gc_managed_root_range_pop"),
              ValueRange{lane, rootRecord})
              .getResult();
      LLVM::CallOp::create(builder, location, TypeRange{},
          SymbolRefAttr::get(builder.getContext(),
                             "obelisk_rt_v1_scheduler_fail"),
          ValueRange{context, popStatus});
    }
    Value succeeded = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, status,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    LLVM::CondBrOp::create(builder, location, succeeded, activated, failed,
                           status);

    builder.setInsertionPointToStart(failed);
    for (int64_t index : referencesAttr.asArrayRef()) {
      if (index < 0 || static_cast<uint64_t>(index) >= task.getValues().size())
        return task.emitOpError("has an invalid transferred-reference index");
      Value releaseStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(builder.getContext(),
                                 "obelisk_rt_v1_native_state_release"),
              ValueRange{context, task.getValues()[index],
                         llvmConstant(builder, location, i32, 0)})
              .getResult();
      LLVM::CallOp::create(builder, location, TypeRange{},
          SymbolRefAttr::get(builder.getContext(),
                             "obelisk_rt_v1_scheduler_fail"),
          ValueRange{context, releaseStatus});
    }
    LLVM::CallOp::create(builder, location, TypeRange{},
        SymbolRefAttr::get(builder.getContext(),
                           "obelisk_rt_v1_scheduler_fail"),
        ValueRange{context, failed->getArgument(0)});
    if (!blocks.terminate)
      return task.emitOpError("has no coroutine termination block");
    LLVM::BrOp::create(builder, location, blocks.terminate);

    builder.setInsertionPointToStart(activated);
    Value activation =
        LLVM::LoadOp::create(builder, location, i64, outActivation, 8);
    publishAction(builder, location, instance, OBELISK_RT_FRAGMENT_TASK_CALL,
                  OBELISK_RT_SUSPEND_NONE, continuationID,
                  OBELISK_RT_FRAGMENT_FLAGS_NONE, activation, 0);
    Value final = llvmConstant(builder, location, builder.getI1Type(), 0);
    Value save = LLVM::CoroSaveOp::create(
        builder, location, LLVM::LLVMTokenType::get(builder.getContext()),
        handle);
    Value state = LLVM::CoroSuspendOp::create(builder, location,
                                              builder.getI8Type(), save, final);
    SmallVector<Block *> destinations{blocks.shims.lookup(continuation),
                                      blocks.cleanup};
    SmallVector<ValueRange> destinationOperands(2);
    SmallVector<APInt> caseValues{APInt(8, 0), APInt(8, 1)};
    LLVM::SwitchOp::create(builder, location, state, blocks.suspendReturn,
                           ValueRange{}, caseValues, destinations,
                           destinationOperands, ArrayRef<int32_t>{});
    builder.eraseOp(operation);
    return success();
  }

  if (auto task = dyn_cast<sim::SimTaskCallOp>(operation)) {
    Type i32 = builder.getI32Type();
    Type i64 = builder.getI64Type();
    storeAt(builder, location, instance, kInstanceContinuationOffset,
            llvmConstant(builder, location, i32, continuationID), 4);
    std::string helper = (task.getCallee() + ".__obelisk_activate").str();
    Value activation =
        LLVM::CallOp::create(builder, location, TypeRange{i64},
                             SymbolRefAttr::get(builder.getContext(), helper),
                             task.getArguments())
            .getResult();
    publishAction(builder, location, instance, OBELISK_RT_FRAGMENT_TASK_CALL,
                  OBELISK_RT_SUSPEND_NONE, continuationID,
                  OBELISK_RT_FRAGMENT_FLAGS_NONE, activation, 0);
    Value final = llvmConstant(builder, location, builder.getI1Type(), 0);
    Value save = LLVM::CoroSaveOp::create(
        builder, location, LLVM::LLVMTokenType::get(builder.getContext()),
        handle);
    Value state = LLVM::CoroSuspendOp::create(builder, location,
                                              builder.getI8Type(), save, final);
    SmallVector<Block *> destinations{blocks.shims.lookup(continuation),
                                      blocks.cleanup};
    SmallVector<ValueRange> destinationOperands(2);
    SmallVector<APInt> caseValues{APInt(8, 0), APInt(8, 1)};
    LLVM::SwitchOp::create(builder, location, state, blocks.suspendReturn,
                           ValueRange{}, caseValues, destinations,
                           destinationOperands, ArrayRef<int32_t>{});
    builder.eraseOp(operation);
    return success();
  }

  uint64_t waitOffset = waitOffsetAttr.getInt();
  uint64_t waitSize = waitSizeAttr.getInt();
  uint32_t kind = suspensionKind(operation);
  uint32_t count = sim::getWaitEntryCount(operation);
  Value wait = byteGEP(builder, location, frame, waitOffset);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  SmallVector<Operation *> observerBindings;
  if (isa<sim::SimSuspendObserveOp>(operation)) {
    if (failed(serializeComputedObserverWait(operation, wait, waitSize, builder,
                                             observerBindings)))
      return failure();
  } else {
    if (failed(serializeRuntimeWait(operation, wait, kind, count, builder)))
      return failure();
  }

  storeAt(builder, location, instance, kInstanceContinuationOffset,
          llvmConstant(builder, location, i32, continuationID), 4);
  uint32_t actionFlags = getRuntimeResumeActionFlags(operation);
  if (actionFlags == UINT32_MAX)
    return operation->emitError("has no executable resume region");
  publishAction(builder, location, instance, OBELISK_RT_FRAGMENT_SUSPEND, kind,
                continuationID,
                OBELISK_RT_ACTION_FRAME_WAIT_RECORD | actionFlags,
                llvmConstant(builder, location, i64, waitOffset), waitSize);

  Value final = llvmConstant(builder, location, builder.getI1Type(), 0);
  Value save = LLVM::CoroSaveOp::create(
      builder, location, LLVM::LLVMTokenType::get(builder.getContext()),
      handle);
  Value state = LLVM::CoroSuspendOp::create(builder, location,
                                            builder.getI8Type(), save, final);
  SmallVector<Block *> destinations{blocks.shims.lookup(continuation),
                                    blocks.cleanup};
  SmallVector<ValueRange> destinationOperands(2);
  SmallVector<APInt> caseValues{APInt(8, 0), APInt(8, 1)};
  LLVM::SwitchOp::create(builder, location, state, blocks.suspendReturn,
                         ValueRange{}, caseValues, destinations,
                         destinationOperands, ArrayRef<int32_t>{});
  builder.eraseOp(operation);
  for (Operation *binding : observerBindings)
    if (binding->use_empty())
      builder.eraseOp(binding);
  return success();
}

LogicalResult lowerFinalReturn(sim::SimReturnOp operation,
                               const RampBlocks &blocks) {
  if (!operation.getOperands().empty())
    return operation.emitError("suspendable process cannot return values");
  IRRewriter builder(operation->getContext());
  builder.setInsertionPoint(operation);
  cf::BranchOp::create(builder, operation.getLoc(), blocks.terminate);
  builder.eraseOp(operation);
  return success();
}

} // namespace

LogicalResult
lowerSuspendableProcess(sim::SimFuncOp function,
                        const SimulationProcessFrameAnalysis &analysis) {
  if (failed(lowerSimulationTimeOperations(function)))
    return failure();
  ModuleOp module = function->getParentOfType<ModuleOp>();
  OpBuilder builder(function);
  Location location = function.getLoc();
  MLIRContext *context = function.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  std::string baseName = function.getSymName().str();
  uint64_t stableID = function.getCodeUnitId().value_or(
      stableProcessID(baseName) &
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  std::string rampName = baseName + ".__obelisk_coro_ramp";
  auto ramp = LLVM::LLVMFuncOp::create(
      builder, location, rampName,
      LLVM::LLVMFunctionType::get(LLVM::LLVMVoidType::get(context),
                                  {pointer, i32, pointer, pointer}, false));
  ramp->setAttr(
      "passthrough",
      builder.getArrayAttr({builder.getStringAttr("presplitcoroutine")}));
  addFrameAttributes(ramp, analysis, builder);
  ramp.getBody().takeBody(function.getBody());
  function.erase();

  for (Block &block : ramp.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(convertProcessType(argument.getType(), context));
  if (failed(lowerNativeDPICalls(ramp)))
    return failure();

  if (failed(lowerNativeFunctionBody(ramp, NativeReturnLowering::None,
                                     NativeCallResultLowering::Preserve)))
    return failure();

  Block *oldEntry = &ramp.getBody().front();
  Block *entry = new Block;
  for (Type type : {pointer, i32, pointer, pointer})
    entry->addArgument(type, location);
  ramp.getBody().getBlocks().insert(ramp.getBody().begin(), entry);
  Block *requirements = new Block;
  Block *execute = new Block;
  ramp.getBody().push_back(requirements);
  ramp.getBody().push_back(execute);
  builder.setInsertionPointToStart(entry);
  Value zero32 = llvmConstant(builder, location, i32, 0);
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value id = LLVM::CoroIdOp::create(builder, location,
                                    LLVM::LLVMTokenType::get(context), zero32,
                                    null, null, null);
  Value requirementsMode =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                            entry->getArgument(1), zero32);
  cf::CondBranchOp::create(builder, location, requirementsMode, requirements,
                           ValueRange{}, execute, ValueRange{});

  builder.setInsertionPointToStart(requirements);
  Value size = LLVM::CoroSizeOp::create(builder, location, i64);
  Value alignment = LLVM::CoroAlignOp::create(builder, location, i64);
  LLVM::StoreOp::create(builder, location, size, entry->getArgument(2), 8);
  LLVM::StoreOp::create(builder, location, alignment, entry->getArgument(3), 8);
  LLVM::ReturnOp::create(builder, location, ValueRange{});

  builder.setInsertionPointToStart(execute);
  Value instance = entry->getArgument(0);
  Value allocation = loadAt(builder, location, instance,
                            kInstanceAllocationOffset, pointer, 8);
  Value scratchOffset =
      loadAt(builder, location, instance, kInstanceScratchOffset, i64, 8);
  Value scratch =
      LLVM::GEPOp::create(builder, location, pointer, builder.getI8Type(),
                          allocation, ValueRange{scratchOffset});
  Value handle =
      LLVM::CoroBeginOp::create(builder, location, pointer, id, scratch);
  storeAt(builder, location, instance, kInstanceNativeHandleOffset, handle, 8);

  // Fixed ABI temporaries and managed-root records were deliberately hoisted
  // to the source function entry. The coroutine ramp adds a new dispatch
  // entry which can resume directly at a continuation shim, bypassing that
  // source block. Move the allocas to the ramp's execute block so they
  // dominate initial execution and every resume and become stable coroutine
  // frame storage instead of per-iteration stack growth.
  SmallVector<LLVM::AllocaOp> activationAllocas;
  SmallVector<LLVM::GEPOp> activationAddresses;
  llvm::SetVector<Operation *> allocaConstants;
  for (LLVM::AllocaOp alloca : oldEntry->getOps<LLVM::AllocaOp>()) {
    activationAllocas.push_back(alloca);
    Operation *count = alloca.getArraySize().getDefiningOp();
    if (count && count->getBlock() == oldEntry && isa<LLVM::ConstantOp>(count))
      allocaConstants.insert(count);
  }
  for (LLVM::GEPOp address : oldEntry->getOps<LLVM::GEPOp>())
    if (auto allocation = address.getBase().getDefiningOp<LLVM::AllocaOp>();
        allocation && llvm::is_contained(activationAllocas, allocation))
      activationAddresses.push_back(address);
  for (LLVM::AllocaOp alloca : activationAllocas)
    alloca->moveBefore(execute, execute->begin());
  for (Operation *constant : allocaConstants)
    constant->moveBefore(execute, execute->begin());
  for (LLVM::GEPOp address : activationAddresses)
    address->moveBefore(execute, execute->end());

  RampBlocks blocks;
  blocks.suspendReturn =
      makeCoroutineReturnBlock(ramp.getBody(), location, handle);
  blocks.cleanup = new Block;
  bool canTerminate = false;
  ramp.walk([&](Operation *operation) {
    canTerminate |=
        isa<sim::SimReturnOp, sim::SimStatusCheckOp, sim::SimProcessControlOp>(
            operation);
  });
  blocks.terminate = canTerminate ? new Block : nullptr;
  ramp.getBody().push_back(blocks.cleanup);
  if (blocks.terminate)
    ramp.getBody().push_back(blocks.terminate);

  llvm::SetVector<Block *> continuationBlocks;
  for (Block &block : ramp.getBody())
    if (!block.empty() && sim::isSuspensionOp(block.getTerminator()))
      continuationBlocks.insert(block.getTerminator()->getSuccessor(0));
  for (Block *continuation : continuationBlocks) {
    Block *shim = new Block;
    ramp.getBody().push_back(shim);
    blocks.shims[continuation] = shim;
    builder.setInsertionPointToStart(shim);
    Value frame =
        loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
    SmallVector<Value> loaded;
    size_t argumentIndex = 0;
    uint32_t continuationID = 0;
    for (Block *predecessor : continuation->getPredecessors()) {
      if (auto id = predecessor->getTerminator()->getAttrOfType<IntegerAttr>(
              "obelisk.coro.continuation")) {
        continuationID = id.getInt();
        break;
      }
    }
    if (continuationID == 0)
      return continuation->getParentOp()->emitError(
          "continuation block is missing its stable continuation ID");
    for (const ProcessFrameValue &slot :
         analysis.getContinuationLayout(continuationID)) {
      Type valueType = continuation->getArgument(argumentIndex++).getType();
      loaded.push_back(loadAt(builder, location, frame, slot.valueOffset,
                              valueType, slot.alignment));
      for (uint64_t rootOffset : slot.managedRootOffsets)
        storeAt(builder, location, frame, slot.valueOffset + rootOffset,
                llvmConstant(builder, location, builder.getI64Type(), 0), 8);
      if (slot.hasSecondaryStorage()) {
        Type secondaryType =
            continuation->getArgument(argumentIndex++).getType();
        loaded.push_back(loadAt(builder, location, frame,
                                slot.getSecondaryOffset(), secondaryType,
                                slot.alignment));
      }
    }
    cf::BranchOp::create(builder, location, continuation, loaded);
  }

  builder.setInsertionPointToEnd(execute);
  SmallVector<Value> entryArguments;
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
  ArrayRef<ProcessFrameValue> captureLayout = analysis.getEntryCaptureLayout();
  size_t physicalArgument = 0;
  auto refreshFrameArgument = [&](BlockArgument argument, Type type,
                                  uint64_t offset, uint32_t alignment) {
    SmallVector<OpOperand *> uses;
    for (OpOperand &use : argument.getUses())
      uses.push_back(&use);
    OpBuilder refreshBuilder(context);
    for (OpOperand *use : uses) {
      Location useLocation = use->getOwner()->getLoc();
      refreshBuilder.setInsertionPoint(use->getOwner());
      Value currentFrame = loadAt(refreshBuilder, useLocation, instance,
                                  kInstanceFrameOffset, pointer, 8);
      use->set(loadAt(refreshBuilder, useLocation, currentFrame, offset, type,
                      alignment));
    }
  };
  for (const ProcessFrameValue &slot : captureLayout) {
    if (!slot.hasValueStorage()) {
      entryArguments.push_back(loadAt(builder, location, instance,
                                      kInstanceContextOffset, pointer, 8));
      // The scheduler may supply a different transient context on every
      // invocation.  Do not let LLVM preserve the first context in the
      // coroutine frame: reload it through the runtime-owned instance at each
      // actual use, including uses reached after a resume.
      BlockArgument contextArgument = oldEntry->getArgument(physicalArgument);
      SmallVector<OpOperand *> contextUses;
      for (OpOperand &use : contextArgument.getUses())
        contextUses.push_back(&use);
      OpBuilder refreshBuilder(context);
      for (OpOperand *use : contextUses) {
        refreshBuilder.setInsertionPoint(use->getOwner());
        Value currentContext =
            loadAt(refreshBuilder, use->getOwner()->getLoc(), instance,
                   kInstanceContextOffset, pointer, 8);
        use->set(currentContext);
      }
      ++physicalArgument;
      continue;
    }
    BlockArgument valueArgument = oldEntry->getArgument(physicalArgument++);
    Type valueType = valueArgument.getType();
    entryArguments.push_back(loadAt(builder, location, frame, slot.valueOffset,
                                    valueType, slot.alignment));
    // Captures are canonical-frame state, not coroutine-frame state. Reload
    // them through the instance at every use so LLVM only needs to preserve
    // the instance/control pointer across suspension.
    refreshFrameArgument(valueArgument, valueType, slot.valueOffset,
                         slot.alignment);
    if (slot.hasSecondaryStorage()) {
      BlockArgument secondaryArgument =
          oldEntry->getArgument(physicalArgument++);
      Type secondaryType = secondaryArgument.getType();
      entryArguments.push_back(loadAt(builder, location, frame,
                                      slot.getSecondaryOffset(), secondaryType,
                                      slot.alignment));
      refreshFrameArgument(secondaryArgument, secondaryType,
                           slot.getSecondaryOffset(), slot.alignment);
    }
  }
  if (entryArguments.size() != oldEntry->getNumArguments())
    return ramp.emitError(
        "converted entry arity disagrees with canonical capture layout");
  for (auto [argument, loaded] :
       llvm::zip_equal(oldEntry->getArguments(), entryArguments))
    argument.replaceAllUsesWith(loaded);

  // Dynamic entry dispatch supports bytecode -> native reconstruction at any
  // stable semantic continuation. Ordinary CFG edges remain direct branches.
  Block *dispatch = new Block;
  ramp.getBody().push_back(dispatch);
  cf::BranchOp::create(builder, location, dispatch);
  builder.setInsertionPointToStart(dispatch);
  Value continuationID =
      loadAt(builder, location, instance, kInstanceContinuationOffset, i32, 4);
  SmallVector<std::pair<uint32_t, Block *>> targets;
  targets.emplace_back(0, oldEntry);
  for (uint32_t idValue : analysis.getContinuations()) {
    if (idValue == 0)
      continue;
    Block *target = nullptr;
    for (Block *candidate : continuationBlocks) {
      for (Block *predecessor : candidate->getPredecessors()) {
        Operation *terminator = predecessor->getTerminator();
        auto idAttr =
            terminator->getAttrOfType<IntegerAttr>("obelisk.coro.continuation");
        if (idAttr && idAttr.getInt() == idValue)
          target = blocks.shims.lookup(candidate);
      }
    }
    if (target)
      targets.emplace_back(idValue, target);
  }
  Block *invalid = new Block;
  ramp.getBody().push_back(invalid);
  builder.setInsertionPointToStart(invalid);
  storeAt(builder, location, instance, kInstanceStatusOffset,
          llvmConstant(builder, location, i32, OBELISK_RT_INVALID_CONTINUATION),
          4);
  cf::BranchOp::create(builder, location, blocks.cleanup);
  Block *test = dispatch;
  for (auto [index, target] : llvm::enumerate(targets)) {
    builder.setInsertionPointToEnd(test);
    Value expected = llvmConstant(builder, location, i32, target.first);
    Value equal = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, continuationID, expected);
    Block *next = index + 1 == targets.size() ? invalid : new Block;
    if (next != invalid)
      ramp.getBody().push_back(next);
    ValueRange arguments =
        target.first == 0 ? ValueRange(entryArguments) : ValueRange{};
    cf::CondBranchOp::create(builder, location, equal, target.second, arguments,
                             next, ValueRange{});
    test = next;
  }

  if (blocks.terminate) {
    // LLVM permits exactly one final suspend per switched-resume coroutine.
    // Funnel every semantic process return through this shared block.
    builder.setInsertionPointToStart(blocks.terminate);
    publishAction(builder, location, instance, OBELISK_RT_FRAGMENT_TERMINATE,
                  OBELISK_RT_SUSPEND_NONE, 0, OBELISK_RT_FRAGMENT_FLAGS_NONE,
                  llvmConstant(builder, location, i64, 0), 0);
    Value final = llvmConstant(builder, location, builder.getI1Type(), 1);
    Value save = LLVM::CoroSaveOp::create(
        builder, location, LLVM::LLVMTokenType::get(context), handle);
    Value finalState = LLVM::CoroSuspendOp::create(
        builder, location, builder.getI8Type(), save, final);
    Block *invalidFinalResume = new Block;
    ramp.getBody().push_back(invalidFinalResume);
    builder.setInsertionPointToStart(invalidFinalResume);
    LLVM::UnreachableOp::create(builder, location);
    builder.setInsertionPointToEnd(blocks.terminate);
    SmallVector<Block *> destinations{invalidFinalResume, blocks.cleanup};
    SmallVector<ValueRange> destinationOperands(2);
    SmallVector<APInt> caseValues{APInt(8, 0), APInt(8, 1)};
    LLVM::SwitchOp::create(builder, location, finalState, blocks.suspendReturn,
                           ValueRange{}, caseValues, destinations,
                           destinationOperands, ArrayRef<int32_t>{});
  }

  builder.setInsertionPointToStart(blocks.cleanup);
  storeAt(builder, location, instance, kInstanceNativeHandleOffset,
          LLVM::ZeroOp::create(builder, location, pointer), 8);
  cf::BranchOp::create(builder, location, blocks.suspendReturn);

  SmallVector<Operation *> terminators;
  ramp.walk([&](Operation *operation) {
    if (sim::isSuspensionOp(operation) || isa<sim::SimReturnOp>(operation))
      terminators.push_back(operation);
  });
  for (Operation *operation : terminators) {
    if (sim::isSuspensionOp(operation)) {
      if (failed(lowerSuspendTerminator(operation, instance, handle, analysis,
                                        blocks)))
        return failure();
    } else if (failed(lowerFinalReturn(cast<sim::SimReturnOp>(operation),
                                       blocks))) {
      return failure();
    }
  }

  SmallVector<sim::SimStatusCheckOp> checks;
  ramp.walk([&](sim::SimStatusCheckOp check) { checks.push_back(check); });
  for (sim::SimStatusCheckOp check : checks) {
    Block *source = check->getBlock();
    Block *continuation = source->splitBlock(std::next(check->getIterator()));
    Block *failure = new Block;
    ramp.getBody().push_back(failure);
    builder.setInsertionPoint(check);
    Value ok = runtime::RTStatusIsOp::create(
        builder, check.getLoc(), builder.getI1Type(), check.getStatus(), 0);
    cf::CondBranchOp::create(builder, check.getLoc(), ok, continuation,
                             ValueRange{}, failure, ValueRange{});
    Value status = check.getStatus();
    bool pushFailure = check->hasAttr(managedRootRangePushCheckAttr);
    check.erase();
    builder.setInsertionPointToStart(failure);
    Value bits = runtime::RTStatusToBitsOp::create(
        builder, location, builder.getI32Type(), status);
    storeAt(builder, location, instance, kInstanceStatusOffset, bits, 4);
    if (!pushFailure)
      emitManagedRootRangePop(builder, location, ramp);
    cf::BranchOp::create(builder, location, blocks.terminate);
  }
  if (failed(makeNativeWrappers(module, ramp, baseName)))
    return failure();
  return makeProcessDescriptor(module, location, baseName, stableID, analysis);
}

} // namespace obelisk::detail
