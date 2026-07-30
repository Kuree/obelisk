//===- SimulationToLLVMCoroutine.cpp - Native process coroutines ---------===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"

#include "SimulationToLLVMCoroutinePrivate.h"
#include "SimulationAOTPlanning.h"
#include "SimulationNBALowering.h"
#include "SimulationPackedLowering.h"
#include "SimulationProcessActivationLowering.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/NetConnectivityAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Analysis/StaticSpecializationAnalysis.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Conversion/SimulationToRuntime.h"
#include "obelisk/Conversion/SimulationToStandard.h"
#include "obelisk/Conversion/SimulationTimeLowering.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Analysis/Liveness.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/LLVMCommon/LoweringOptions.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/MathToLLVM/MathToLLVM.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <type_traits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKSIMPROCESSESTOLLVMCOROUTINESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using detail::alignUp;
using detail::annotateStaticDriverNets;
using detail::assumeCleanSpecializationAttr;
using detail::byteGEP;
using detail::buildNativeStaticActorRootPlan;
using detail::buildNativeStaticFanoutPlan;
using detail::buildNativeStaticNBAPlan;
using detail::castIntegerWidth;
using detail::containsLogic;
using detail::convertProcessType;
using detail::declareNativeRuntimeABI;
using detail::DirectStaticStateRange;
using detail::entryAlloca;
using detail::emitManagedRootRangePop;
using detail::getOrDeclareLLVMFunction;
using detail::insertValue;
using detail::insertAutomaticOwnerReleases;
using detail::instrumentManagedRoots;
using detail::llvmConstant;
using detail::loadAt;
using detail::loadStatePlane;
using detail::lowerNativeDPICalls;
using detail::lowerNativeFunctionBody;
using detail::lowerPackedSimulationOperations;
using detail::materializeNativeSchedulerGlobals;
using detail::makeProcessActivationHelper;
using detail::makeProcessDescriptor;
using detail::makeProcessSpawnHelper;
using detail::makeSchedulerMain;
using detail::makeByteArrayGlobal;
using detail::makeConstantGlobal;
using detail::managedClassDescriptorName;
using detail::managedMethodThunkName;
using detail::managedRootRangePushCheckAttr;
using detail::managedRootRangeRecordAttr;
using detail::markCleanStaticNBAsInGuardedBodies;
using detail::markLikelyTrue;
using detail::makeNativeAOTPlan;
using detail::materializeDPIThunks;
using detail::materializeManagedMethodThunks;
using detail::materializeGeneratedNBAAccumulators;
using detail::materializeNativeObserverThunks;
using detail::nativeStateWidth;
using detail::nativeTwoStateBlockUnknownsAttr;
using detail::notifySignal;
using detail::NativeCallResultLowering;
using detail::NativeReturnLowering;
using detail::NativeSchedulePlan;
using detail::NativeStateLayout;
using detail::NativeStaticFanoutPlan;
using detail::NativeStaticNBAPlan;
using detail::populateAggregateToLLVMConversionPatterns;
using detail::populateControlToLLVMConversionPatterns;
using detail::populateContextRuntimeToLLVMConversionPattern;
using detail::populateDriverToLLVMConversionPatterns;
using detail::populateEventToLLVMConversionPatterns;
using detail::populateFunctionTypeConversionPatterns;
using detail::populateManagedToLLVMConversionPatterns;
using detail::populateNBAToLLVMConversionPatterns;
using detail::populateNativeHandleConversionPatterns;
using detail::populateOverrideToLLVMConversionPatterns;
using detail::populateReferenceLifetimeToLLVMConversionPatterns;
using detail::populateSchedulerToLLVMConversionPatterns;
using detail::populateStateReadWriteToLLVMConversionPatterns;
using detail::populateSuspensionTypeConversionPatterns;
using detail::prepareManagedLowering;
using detail::recordStaticSpecializationCFGBlocks;
using detail::resolveCFGConstantInteger;
using detail::resolveDirectStaticStateRange;
using detail::reportManagedStatus;
using detail::releaseNativeAutomaticState;
using detail::resizeSignedIndexToI64;
using detail::serializeComputedObserverWait;
using detail::serializeRuntimeWait;
using detail::stableProcessID;
using detail::staticNBASpecializationGuard;
using detail::staticSpecializationGuard;
using detail::storeAt;
using detail::storeStatePlane;
using detail::threadProcessStateThroughCFG;
using detail::threadRuntimeStatuses;
using detail::validateProcessABI;
using detail::ReferenceArgumentMap;

constexpr uint64_t kInstanceAllocationOffset =
    offsetof(obelisk_rt_process_instance_v1, allocation);
constexpr uint64_t kInstanceFrameOffset =
    offsetof(obelisk_rt_process_instance_v1, frame);
constexpr uint64_t kInstanceScratchOffset =
    offsetof(obelisk_rt_process_instance_v1, scratch_offset);
constexpr uint64_t kInstanceNativeHandleOffset =
    offsetof(obelisk_rt_process_instance_v1, native_handle);
constexpr uint64_t kInstanceContinuationOffset =
    offsetof(obelisk_rt_process_instance_v1, continuation);
constexpr uint64_t kInstanceStatusOffset =
    offsetof(obelisk_rt_process_instance_v1, status);
constexpr uint64_t kInstanceContextOffset =
    offsetof(obelisk_rt_process_instance_v1, context);
constexpr uint64_t kInstanceActionOffset =
    offsetof(obelisk_rt_process_instance_v1, action);
constexpr uint64_t kActionKindOffset =
    offsetof(obelisk_rt_fragment_action_v1, kind);
constexpr uint64_t kActionSuspendKindOffset =
    offsetof(obelisk_rt_fragment_action_v1, suspend_kind);
constexpr uint64_t kActionContinuationOffset =
    offsetof(obelisk_rt_fragment_action_v1, continuation);
constexpr uint64_t kActionFlagsOffset =
    offsetof(obelisk_rt_fragment_action_v1, flags);
constexpr uint64_t kActionPayloadOffset =
    offsetof(obelisk_rt_fragment_action_v1, payload);
constexpr uint64_t kActionAuxiliaryOffset =
    offsetof(obelisk_rt_fragment_action_v1, auxiliary);

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
      .Case<sim::SimSuspendJoinOp>(
          [](auto) { return OBELISK_RT_SUSPEND_JOIN; })
      .Case<sim::SimSuspendForeverOp>(
          [](auto) { return OBELISK_RT_SUSPEND_FOREVER; })
      .Case<sim::SimSuspendChildrenOp>(
          [](auto) { return OBELISK_RT_SUSPEND_CHILDREN; })
      .Case<sim::SimSuspendObserveOp>(
          [](auto) { return OBELISK_RT_SUSPEND_OBSERVER; })
      .Default([](Operation *) { return OBELISK_RT_SUSPEND_NONE; });
}

void publishAction(OpBuilder &builder, Location location, Value instance,
                   uint32_t actionKind, uint32_t suspendKind,
                   uint32_t continuation, uint32_t flags, Value payload,
                   uint64_t auxiliary) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Value action =
      loadAt(builder, location, instance, kInstanceActionOffset, pointer, 8);
  storeAt(builder, location, action, kActionKindOffset,
          llvmConstant(builder, location, i32, actionKind), 4);
  storeAt(builder, location, action, kActionSuspendKindOffset,
          llvmConstant(builder, location, i32, suspendKind), 4);
  storeAt(builder, location, action, kActionContinuationOffset,
          llvmConstant(builder, location, i32, continuation), 4);
  storeAt(builder, location, action, kActionFlagsOffset,
          llvmConstant(builder, location, i32, flags), 4);
  storeAt(builder, location, action, kActionPayloadOffset, payload, 8);
  storeAt(builder, location, action, kActionAuxiliaryOffset,
          llvmConstant(builder, location, i64, auxiliary), 8);
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
    if (failed(serializeComputedObserverWait(
            operation, wait, waitSize, builder, observerBindings)))
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

LogicalResult makeNativeWrappers(ModuleOp module, LLVM::LLVMFuncOp ramp,
                                 StringRef baseName) {
  OpBuilder builder(ramp);
  builder.setInsertionPointAfter(ramp);
  Location location = ramp.getLoc();
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  auto makeName = [&](StringRef suffix) { return (baseName + suffix).str(); };

  auto requirements = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_requirements"),
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *requirementsEntry = requirements.addEntryBlock(builder);
  builder.setInsertionPointToStart(requirementsEntry);
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value mode = llvmConstant(builder, location, i32, 0);
  auto requirementsCall = LLVM::CallOp::create(
      builder, location, TypeRange{}, SymbolRefAttr::get(ramp),
      ValueRange{null, mode, requirementsEntry->getArgument(0),
                 requirementsEntry->getArgument(1)});
  (void)requirementsCall;
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, 0));

  builder.setInsertionPointAfter(requirements);
  auto execute = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_execute"),
      LLVM::LLVMFunctionType::get(i32, {pointer}, false));
  Block *executeEntry = execute.addEntryBlock(builder);
  Block *start = new Block;
  Block *resume = new Block;
  Block *done = new Block;
  execute.getBody().push_back(start);
  execute.getBody().push_back(resume);
  execute.getBody().push_back(done);
  builder.setInsertionPointToStart(executeEntry);
  Value instance = executeEntry->getArgument(0);
  Value runtimeContext =
      loadAt(builder, location, instance, kInstanceContextOffset, pointer, 8);
  Value currentContext = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, runtimeContext, currentContext, 8);
  Value handle = loadAt(builder, location, instance,
                        kInstanceNativeHandleOffset, pointer, 8);
  Value bits =
      LLVM::PtrToIntOp::create(builder, location, builder.getI64Type(), handle);
  Value isNull = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, bits,
      llvmConstant(builder, location, builder.getI64Type(), 0));
  cf::CondBranchOp::create(builder, location, isNull, start, ValueRange{},
                           resume, ValueRange{});
  builder.setInsertionPointToStart(start);
  Value modeExecute = llvmConstant(builder, location, i32, 1);
  Value nullOut = LLVM::ZeroOp::create(builder, location, pointer);
  LLVM::CallOp::create(builder, location, TypeRange{}, SymbolRefAttr::get(ramp),
                       ValueRange{instance, modeExecute, nullOut, nullOut});
  cf::BranchOp::create(builder, location, done);
  builder.setInsertionPointToStart(resume);
  LLVM::CoroResumeOp::create(builder, location, handle);
  cf::BranchOp::create(builder, location, done);
  builder.setInsertionPointToStart(done);
  Value status =
      loadAt(builder, location, instance, kInstanceStatusOffset, i32, 4);
  LLVM::ReturnOp::create(builder, location, status);

  builder.setInsertionPointAfter(execute);
  auto destroy = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_destroy"),
      LLVM::LLVMFunctionType::get(voidType, {pointer}, false));
  Block *destroyEntry = destroy.addEntryBlock(builder);
  Block *destroyCall = new Block;
  Block *destroyDone = new Block;
  destroy.getBody().push_back(destroyCall);
  destroy.getBody().push_back(destroyDone);
  builder.setInsertionPointToStart(destroyEntry);
  Value destroyInstance = destroyEntry->getArgument(0);
  Value destroyHandle = loadAt(builder, location, destroyInstance,
                               kInstanceNativeHandleOffset, pointer, 8);
  Value destroyBits = LLVM::PtrToIntOp::create(
      builder, location, builder.getI64Type(), destroyHandle);
  Value destroyIsNull = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, destroyBits,
      llvmConstant(builder, location, builder.getI64Type(), 0));
  cf::CondBranchOp::create(builder, location, destroyIsNull, destroyDone,
                           ValueRange{}, destroyCall, ValueRange{});
  builder.setInsertionPointToStart(destroyCall);
  LLVM::CallIntrinsicOp::create(builder, location,
                                builder.getStringAttr("llvm.coro.destroy"),
                                destroyHandle);
  storeAt(builder, location, destroyInstance, kInstanceNativeHandleOffset,
          LLVM::ZeroOp::create(builder, location, pointer), 8);
  cf::BranchOp::create(builder, location, destroyDone);
  builder.setInsertionPointToStart(destroyDone);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return success();
}

LogicalResult
makePlainNativeWrappers(ModuleOp module, func::FuncOp body, StringRef baseName,
                        const SimulationProcessFrameAnalysis &analysis) {
  OpBuilder builder(body);
  builder.setInsertionPointAfter(body);
  Location location = body.getLoc();
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Type voidType = LLVM::LLVMVoidType::get(context);

  auto requirements = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_requirements").str(),
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *requirementsEntry = requirements.addEntryBlock(builder);
  builder.setInsertionPointToStart(requirementsEntry);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, 0),
                        requirementsEntry->getArgument(0), 8);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, 1),
                        requirementsEntry->getArgument(1), 8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, 0));

  builder.setInsertionPointAfter(requirements);
  auto execute = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_execute").str(),
      LLVM::LLVMFunctionType::get(i32, {pointer}, false));
  Block *executeEntry = execute.addEntryBlock(builder);
  builder.setInsertionPointToStart(executeEntry);
  Value instance = executeEntry->getArgument(0);
  Value runtimeContext =
      loadAt(builder, location, instance, kInstanceContextOffset, pointer, 8);
  Value currentContext = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, runtimeContext, currentContext, 8);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
  SmallVector<Value> arguments;
  size_t physicalArgument = 0;
  Block &bodyEntry = body.getBody().front();
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (!slot.hasValueStorage()) {
      arguments.push_back(loadAt(builder, location, instance,
                                 kInstanceContextOffset, pointer, 8));
      ++physicalArgument;
      continue;
    }
    Type valueType = bodyEntry.getArgument(physicalArgument++).getType();
    arguments.push_back(loadAt(builder, location, frame, slot.valueOffset,
                               valueType, slot.alignment));
    if (slot.hasSecondaryStorage()) {
      Type secondaryType = bodyEntry.getArgument(physicalArgument++).getType();
      arguments.push_back(loadAt(builder, location, frame,
                                 slot.getSecondaryOffset(), secondaryType,
                                 slot.alignment));
    }
  }
  if (arguments.size() != bodyEntry.getNumArguments())
    return body.emitError(
        "converted entry arity disagrees with canonical capture layout");
  auto call = func::CallOp::create(builder, location, body.getSymName(),
                                   TypeRange{i32}, arguments);
  storeAt(builder, location, instance, kInstanceContinuationOffset,
          llvmConstant(builder, location, i32, 0), 4);
  publishAction(builder, location, instance, OBELISK_RT_FRAGMENT_TERMINATE,
                OBELISK_RT_SUSPEND_NONE, 0, OBELISK_RT_FRAGMENT_FLAGS_NONE,
                llvmConstant(builder, location, i64, 0), 0);
  LLVM::ReturnOp::create(builder, location, call.getResult(0));

  builder.setInsertionPointAfter(execute);
  auto destroy = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_destroy").str(),
      LLVM::LLVMFunctionType::get(voidType, {pointer}, false));
  Block *destroyEntry = destroy.addEntryBlock(builder);
  builder.setInsertionPointToStart(destroyEntry);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return success();
}

LogicalResult
lowerPlainNativeProcess(sim::SimFuncOp function,
                        const SimulationProcessFrameAnalysis &analysis) {
  if (!function.getResultTypes().empty())
    return function.emitError("simulation process cannot return values");
  if (failed(lowerSimulationTimeOperations(function)))
    return failure();
  ModuleOp module = function->getParentOfType<ModuleOp>();
  Location location = function.getLoc();
  MLIRContext *context = function.getContext();
  std::string baseName = function.getSymName().str();
  uint64_t stableID = function.getCodeUnitId().value_or(
      stableProcessID(baseName) &
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  context->getOrLoadDialect<func::FuncDialect>();
  SmallVector<Type> inputTypes;
  for (BlockArgument argument : function.getBody().front().getArguments())
    inputTypes.push_back(convertProcessType(argument.getType(), context));
  OpBuilder builder(function);
  auto body = func::FuncOp::create(
      builder, location, baseName,
      FunctionType::get(context, inputTypes, TypeRange{builder.getI32Type()}));
  body->setAttr("obelisk.entry_kind",
                builder.getI32IntegerAttr(
                    static_cast<uint32_t>(function.getEntryKind())));
  body->setAttr("obelisk.native_scratch_size", builder.getI64IntegerAttr(0));
  body.getBody().takeBody(function.getBody());
  function.erase();
  for (Block &block : body.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(convertProcessType(argument.getType(), context));
  if (failed(lowerNativeDPICalls(body)))
    return failure();

  IRRewriter rewriter(context);
  if (failed(lowerNativeFunctionBody(
          body, NativeReturnLowering::SuccessStatus,
          NativeCallResultLowering::Preserve)))
    return failure();

  SmallVector<sim::SimStatusCheckOp> checks;
  body.walk([&](sim::SimStatusCheckOp check) { checks.push_back(check); });
  for (sim::SimStatusCheckOp check : checks) {
    Block *source = check->getBlock();
    Block *continuation = source->splitBlock(std::next(check->getIterator()));
    Block *failure = new Block;
    body.getBody().push_back(failure);
    rewriter.setInsertionPoint(check);
    Value ok = runtime::RTStatusIsOp::create(
        rewriter, check.getLoc(), rewriter.getI1Type(), check.getStatus(), 0);
    cf::CondBranchOp::create(rewriter, check.getLoc(), ok, continuation,
                             ValueRange{}, failure, ValueRange{});
    Value status = check.getStatus();
    bool pushFailure = check->hasAttr(managedRootRangePushCheckAttr);
    rewriter.eraseOp(check);
    rewriter.setInsertionPointToStart(failure);
    Value bits = runtime::RTStatusToBitsOp::create(
        rewriter, body.getLoc(), rewriter.getI32Type(), status);
    if (!pushFailure)
      emitManagedRootRangePop(rewriter, body.getLoc(), body);
    func::ReturnOp::create(rewriter, body.getLoc(), bits);
  }

  if (failed(makePlainNativeWrappers(module, body, baseName, analysis)))
    return failure();
  return makeProcessDescriptor(module, location, baseName, stableID, analysis);
}

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
    canTerminate |= isa<sim::SimReturnOp, sim::SimStatusCheckOp>(operation);
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
          llvmConstant(builder, location, i32,
                       OBELISK_RT_INVALID_CONTINUATION),
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

LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function) {
  if (failed(lowerSimulationTimeOperations(function)))
    return failure();
  Location location = function.getLoc();
  std::string symbolName = function.getSymName().str();
  FunctionType functionType = function.getFunctionType();
  SmallVector<Type> inputTypes;
  SmallVector<Type> resultTypes;
  for (Type type : functionType.getInputs())
    inputTypes.push_back(convertProcessType(type, function.getContext()));
  for (Type type : functionType.getResults())
    resultTypes.push_back(convertProcessType(type, function.getContext()));
  uint32_t entryKind = static_cast<uint32_t>(function.getEntryKind());
  bool observer = function.getEntryKind() == sim::EntryKind::Observer;
  auto observerWidth =
      function->getAttrOfType<IntegerAttr>("obelisk_sim.observer_width");
  auto observerFourState =
      function->getAttrOfType<BoolAttr>("obelisk_sim.observer_four_state");
  function.getContext()->getOrLoadDialect<func::FuncDialect>();
  OpBuilder builder(function.getContext());
  builder.setInsertionPoint(function);
  auto replacement =
      func::FuncOp::create(builder, location, builder.getStringAttr(symbolName),
                           TypeAttr::get(FunctionType::get(
                               function.getContext(), inputTypes, resultTypes)),
                           StringAttr{}, ArrayAttr{}, ArrayAttr{}, UnitAttr{});
  replacement->setAttr("obelisk.entry_kind",
                       builder.getI32IntegerAttr(entryKind));
  replacement->setAttr("obelisk.native_scratch_size",
                       builder.getI64IntegerAttr(0));
  replacement.getBody().takeBody(function.getBody());
  function.erase();
  for (Block &block : replacement.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(
          convertProcessType(argument.getType(), replacement.getContext()));
  if (failed(lowerNativeDPICalls(replacement)))
    return failure();
  if (failed(lowerNativeFunctionBody(
          replacement, NativeReturnLowering::Preserve,
          NativeCallResultLowering::ConvertProcessTypes)))
    return failure();
  if (observer) {
    if (!observerWidth || !observerFourState)
      return replacement.emitError(
          "observer entry is missing native descriptor metadata");
    replacement->setAttr(
        "obelisk.observer_width",
        builder.getI32IntegerAttr(observerWidth.getValue().getZExtValue()));
    replacement->setAttr("obelisk.observer_four_state",
                         builder.getBoolAttr(observerFourState.getValue()));
  }
  return success();
}

FailureOr<NativeStateLayout> buildNativeStateLayout(ModuleOp module) {
  FailureOr<analysis::NativeStateLayoutAnalysis> analyzed =
      analysis::NativeStateLayoutAnalysis::compute(module);
  if (failed(analyzed))
    return failure();
  NativeStateLayout layout;
  static_cast<analysis::NativeStateLayoutAnalysis &>(layout) =
      std::move(*analyzed);
  return layout;
}

LogicalResult
specializeNativeAOTCaptures(ModuleOp module,
                            const analysis::NativeAOTAnalysis &eligibility) {
  sim::SimFuncOp root;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      root = function;
  });
  if (!root)
    return module.emitError(
        "cannot specialize AOT captures without a root initializer");

  WalkResult specialized = root.walk([&](sim::SimSpawnOp spawn) {
    sim::SimDesignOp design = spawn->getParentOfType<sim::SimDesignOp>();
    sim::SimFuncOp target =
        design ? design.lookupSymbol<sim::SimFuncOp>(spawn.getCallee())
               : nullptr;
    if (!target ||
        !eligibility.getActorSlots().contains(target.getOperation()))
      return WalkResult::advance();
    Block &entry = target.getBody().front();
    if (spawn.getNumOperands() != entry.getNumArguments()) {
      spawn.emitOpError("AOT capture specialization found an invalid arity");
      return WalkResult::interrupt();
    }
    if (entry.getNumArguments() == 0 ||
        !isa<sim::ContextType>(entry.getArgument(0).getType())) {
      target.emitOpError(
          "AOT capture specialization requires a context entry capture");
      return WalkResult::interrupt();
    }

    for (unsigned index = 1; index != entry.getNumArguments(); ++index) {
      Operation *producer = spawn.getOperand(index).getDefiningOp();
      if (!producer ||
          !isa<sim::SimContextStorageOp, sim::SimContextNetOp,
               sim::SimContextDriverOp, sim::SimContextEventOp>(producer))
        continue;
      if (producer->getNumOperands() != 1 ||
          producer->getOperand(0) != spawn.getOperand(0) ||
          producer->getNumResults() != 1 ||
          producer->getResult(0) != spawn.getOperand(index))
        continue;

      SmallVector<OpOperand *> uses;
      for (OpOperand &use : entry.getArgument(index).getUses())
        uses.push_back(&use);
      DenseMap<Block *, Value> specializedByBlock;
      for (OpOperand *use : uses) {
        Block *block = use->getOwner()->getBlock();
        auto [position, inserted] =
            specializedByBlock.try_emplace(block, Value{});
        if (inserted) {
          OpBuilder builder(target.getContext());
          builder.setInsertionPointToStart(block);
          IRMapping mapping;
          mapping.map(spawn.getOperand(0), entry.getArgument(0));
          position->second = builder.clone(*producer, mapping)->getResult(0);
        }
        use->set(position->second);
      }
    }
    return WalkResult::advance();
  });
  return specialized.wasInterrupted() ? failure() : success();
}

LLVM::GlobalOp
makeStatePlane(ModuleOp module, StringRef name, uint64_t bytes, bool unknown,
               ArrayRef<NativeStateLayout::Driver> highImpedanceDrivers = {},
               ArrayRef<NativeStateLayout::Net> highImpedanceNets = {}) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  Location location = module.getLoc();
  Type i8 = builder.getI8Type();
  Type array = LLVM::LLVMArrayType::get(i8, bytes);
  auto global =
      LLVM::GlobalOp::create(builder, location, array, false,
                             LLVM::Linkage::Internal, name, Attribute{}, 8);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  Value value = LLVM::ZeroOp::create(builder, location, array);
  SmallVector<uint8_t> initial(bytes, unknown ? UINT8_MAX : 0);
  if (!unknown)
    for (const NativeStateLayout::Driver &driver : highImpedanceDrivers)
      for (unsigned bit = 0; bit < driver.width; ++bit) {
        uint64_t absolute = driver.offset + bit;
        initial[absolute / 8] |= static_cast<uint8_t>(1u << (absolute % 8));
      }
  if (!unknown)
    for (const NativeStateLayout::Net &net : highImpedanceNets) {
      if (!net.fourState)
        continue;
      for (unsigned bit = 0; bit < net.width; ++bit) {
        uint64_t absolute = net.offset + bit;
        initial[absolute / 8] |= static_cast<uint8_t>(1u << (absolute % 8));
      }
    }
  for (auto [index, byte] : llvm::enumerate(initial))
    if (byte != 0)
      value = LLVM::InsertValueOp::create(
          builder, location, value, llvmConstant(builder, location, i8, byte),
          ArrayRef<int64_t>{static_cast<int64_t>(index)});
  LLVM::ReturnOp::create(builder, location, value);
  return global;
}

} // namespace


namespace detail {

std::optional<uint64_t> resolveCFGConstantInteger(Value value,
                                                  DenseSet<Value> &active) {
  if (auto constant = value.getDefiningOp<arith::ConstantOp>())
    if (auto integer = dyn_cast<IntegerAttr>(constant.getValue()))
      return integer.getValue().getZExtValue();
  auto argument = dyn_cast<BlockArgument>(value);
  if (!argument || !active.insert(value).second)
    return std::nullopt;
  std::optional<uint64_t> resolved;
  Block *block = argument.getOwner();
  for (Block *predecessor : block->getPredecessors()) {
    Operation *terminator = predecessor->getTerminator();
    auto branch = dyn_cast<BranchOpInterface>(terminator);
    if (!branch) {
      active.erase(value);
      return std::nullopt;
    }
    for (unsigned successor = 0; successor != terminator->getNumSuccessors();
         ++successor) {
      if (terminator->getSuccessor(successor) != block)
        continue;
      SuccessorOperands operands = branch.getSuccessorOperands(successor);
      unsigned index = argument.getArgNumber();
      if (index >= operands.size() || operands.isOperandProduced(index)) {
        active.erase(value);
        return std::nullopt;
      }
      std::optional<uint64_t> incoming =
          resolveCFGConstantInteger(operands[index], active);
      if (!incoming || (resolved && *resolved != *incoming)) {
        active.erase(value);
        return std::nullopt;
      }
      resolved = incoming;
    }
  }
  active.erase(value);
  return resolved;
}

std::optional<uint64_t> resolveCFGConstantInteger(Value value) {
  DenseSet<Value> active;
  return resolveCFGConstantInteger(value, active);
}

} // namespace detail

namespace detail {

void notifySignal(OpBuilder &builder, Location location, Value handle,
                  uint64_t width, Value oldValue, Value oldUnknown,
                  Value newValue, Value newUnknown,
                  std::optional<DirectStaticStateRange> directRange) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Value address = LLVM::AddressOfOp::create(builder, location, pointer,
                                            "__obelisk_current_context");
  Value context = LLVM::LoadOp::create(builder, location, pointer, address, 8);
  if (directRange && width <= 64) {
    auto scalar = [&](Value value) -> Value {
      if (!value)
        return llvmConstant(builder, location, i64, uint64_t{0});
      if (value.getType() == i64)
        return value;
      return LLVM::ZExtOp::create(builder, location, i64, value);
    };
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(builder.getContext(),
                           "obelisk_rt_v1_scheduler_static_transition"),
        ValueRange{
            context,
            llvmConstant(builder, location, i32, directRange->staticID),
            llvmConstant(builder, location, i64, directRange->localOffset),
            llvmConstant(builder, location, i64, width), scalar(oldValue),
            scalar(oldUnknown), scalar(newValue), scalar(newUnknown)});
    return;
  }
  auto save = [&](Value value) {
    if (!value)
      return LLVM::ZeroOp::create(builder, location, pointer).getResult();
    Value storage = entryAlloca(builder, location, value.getType(), 1, 1);
    LLVM::StoreOp::create(builder, location, value, storage, 1);
    return storage;
  };
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(),
                         "obelisk_rt_v1_scheduler_signal_transition"),
      ValueRange{context, handle, llvmConstant(builder, location, i64, width),
                 save(oldValue), save(oldUnknown), save(newValue),
                 save(newUnknown)});
}

} // namespace detail

namespace {




LogicalResult prepareSimulationProcessesForLLVMCoroutinesImpl(
    ModuleOp module, const llvm::DataLayout &dataLayout) {
  MLIRContext *context = module.getContext();
  if (failed(prepareManagedLowering(module, dataLayout)))
    return failure();
  FailureOr<NativeStateLayout> stateLayout = buildNativeStateLayout(module);
  if (failed(stateLayout))
    return failure();
  sim::StaticSpecializationAttr staticSpecialization;
  sim::StaticSuperstepAttr staticSuperstep;
  SmallVector<sim::ComputeNBACommitAttr> staticNBACommits;
  sim::SimDesignOp metadataDesign;
  module.walk([&](sim::SimDesignOp design) {
    metadataDesign = design;
    staticSuperstep = design->getAttrOfType<sim::StaticSuperstepAttr>(
        sim::metadata::staticSuperstep);
  });
  analysis::SimulationVPIAnalysis vpi =
      analysis::SimulationVPIAnalysis::compute(metadataDesign);
  if (staticSuperstep &&
      (!metadataDesign ||
       staticSuperstep.getSourceGraph() !=
           metadataDesign.getComputeGraphAttr()))
    return module.emitError(
        "native lowering rejected stale static-superstep metadata");
  if (metadataDesign) {
    FailureOr<analysis::StaticSpecializationAnalysis> analyzed =
        analysis::StaticSpecializationAnalysis::compute(metadataDesign);
    if (failed(analyzed))
      return failure();
    staticSpecialization = analyzed->getPlan();
    llvm::append_range(staticNBACommits, analyzed->getOrderedNBACommits());
    DenseSet<uint64_t> plannedNBARoots;
    for (const auto &[descriptor, root] : analyzed->getRoots()) {
      if (!root.getDirect() && !root.getGuarded() && !root.getNba())
        continue;
      if (root.getWidth() == 0 ||
          ((root.getDirect() || root.getGuarded()) &&
           root.getWidth() > staticSpecialization.getMaxPackedWidth()))
        return module.emitError(
            "native lowering rejected invalid static-specialization root");
      auto handle = stateLayout->storage.find(descriptor);
      if (handle == stateLayout->storage.end())
        return module.emitError(
            "static-specialization root references unknown storage");
      obelisk_rt_stable_handle_v1 decoded{};
      if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
          decoded.offset != 0)
        return module.emitError(
            "static-specialization root has an invalid native handle");
      auto bound = llvm::find_if(stateLayout->bounds, [&](const auto &entry) {
        return entry.handleID == decoded.id;
      });
      if (bound == stateLayout->bounds.end() || bound->width != root.getWidth())
        return module.emitError(
            "static-specialization root disagrees with native state layout");
      if (root.getDirect())
        stateLayout->directHandles.insert(decoded.id);
      if (root.getGuarded())
        stateLayout->guardedHandles.insert(decoded.id);
      if (root.getNba()) {
        plannedNBARoots.insert(descriptor);
        stateLayout->nbaHandles.insert(decoded.id);
      }
    }
    if (analyzed->getNBARoots().size() != plannedNBARoots.size())
      return module.emitError(
          "static-specialization NBA root policies disagree with the "
          "ordered inventory");
  }
  sim::NativeSchedulerMode nativeScheduler = sim::NativeSchedulerMode::Auto;
  if (auto mode = module->getAttrOfType<sim::NativeSchedulerModeAttr>(
          "obelisk.native_scheduler"))
    nativeScheduler = mode.getValue();
  analysis::NativeAOTAnalysis aotEligibility;
  bool useAOT = false;
  DenseMap<Operation *, SmallVector<uint32_t>> aotBytecodeContinuations;
  uint64_t stateBytes = (stateLayout->bitCount + 7) / 8;
  makeStatePlane(module, "__obelisk_state_value", stateBytes, false,
                 stateLayout->driverLayouts, stateLayout->netLayouts);
  makeStatePlane(module, "__obelisk_state_unknown", stateBytes, true);
  materializeNativeSchedulerGlobals(module);
  declareNativeRuntimeABI(module);
  llvm::MapVector<Operation *, std::unique_ptr<SimulationProcessFrameAnalysis>>
      analyses;
  WalkResult analyzed = module.walk([&](sim::SimFuncOp function) {
    bool suspendable = false;
    function.walk(
        [&](Operation *operation) {
          suspendable |= sim::isSuspensionOp(operation);
        });
    bool process = function.getEntryKind() != sim::EntryKind::Function &&
                   function.getEntryKind() != sim::EntryKind::Observer;
    if (failed(insertAutomaticOwnerReleases(function)))
      return WalkResult::interrupt();
    if (suspendable && failed(threadProcessStateThroughCFG(function)))
      return WalkResult::interrupt();
    if (!suspendable && !process)
      return WalkResult::advance();
    auto analysis =
        SimulationProcessFrameAnalysis::create(function, dataLayout);
    if (failed(analysis))
      return WalkResult::interrupt();
    for (const ProcessSuspension &suspension : (*analysis)->getSuspensions()) {
      suspension.operation->setAttr(
          "obelisk.coro.continuation",
          IntegerAttr::get(IntegerType::get(context, 32),
                           suspension.continuationID));
      suspension.operation->setAttr(
          "obelisk.coro.wait_offset",
          IntegerAttr::get(IntegerType::get(context, 64),
                           suspension.waitOffset));
      suspension.operation->setAttr(
          "obelisk.coro.wait_size",
          IntegerAttr::get(IntegerType::get(context, 64), suspension.waitSize));
    }
    analyses.insert({function.getOperation(), std::move(*analysis)});
    return WalkResult::advance();
  });
  if (analyzed.wasInterrupted())
    return failure();
  if (nativeScheduler != sim::NativeSchedulerMode::Generic) {
    aotEligibility = analysis::NativeAOTAnalysis::compute(module);
    useAOT = aotEligibility.isEligible();
    if (nativeScheduler == sim::NativeSchedulerMode::AOT &&
        !aotEligibility.isFullyEligible()) {
      InFlightDiagnostic diagnostic =
          module.emitError("design is ineligible for native AOT scheduling: ");
      if (aotEligibility.getReasons().empty())
        diagnostic << "no statically schedulable process actors";
      else
        llvm::interleaveComma(aotEligibility.getReasons(), diagnostic);
      return failure();
    }
  }
  bool cleanSuperstep = false;
  if (staticSuperstep && useAOT && aotEligibility.isFullyEligible()) {
    ArrayAttr actors = staticSuperstep.getActors();
    if (actors.size() != aotEligibility.getActorSlots().size())
      return module.emitError(
          "native lowering rejected stale static-superstep actor inventory");
    for (auto [slot, attribute] : llvm::enumerate(actors)) {
      auto actor = dyn_cast<FlatSymbolRefAttr>(attribute);
      sim::SimFuncOp function =
          actor ? metadataDesign.lookupSymbol<sim::SimFuncOp>(actor.getValue())
                : nullptr;
      auto planned =
          function
              ? aotEligibility.getActorSlots().find(function.getOperation())
              : aotEligibility.getActorSlots().end();
      if (!function || planned == aotEligibility.getActorSlots().end() ||
          planned->second != slot)
        return module.emitError(
            "native lowering rejected stale static-superstep actor order");
    }
    cleanSuperstep = true;
  }
  if (useAOT && failed(specializeNativeAOTCaptures(module, aotEligibility)))
    return failure();
  bool staticControl = false;
  bool staticFanout = false;
  bool staticFanoutMetadata = false;
  bool directStaticState = false;
  bool staticNBA = false;
  NativeStaticNBAPlan staticNBAPlan;
  NativeStaticFanoutPlan staticFanoutPlan;
  SmallVector<obelisk_rt_static_actor_root> staticActorRoots;
  if (useAOT && aotEligibility.isFullyEligible()) {
    staticControl = vpi.hasComputeGraph();
    staticFanoutMetadata = vpi.hasComputeGraph();
    // Read-only VPI observes the same canonical planes but cannot mutate
    // roots or invalidate the closed-world waiter inventory. It therefore
    // uses the fully static fanout schedule just like VPI-off.
    staticFanout = vpi.preservesStaticDependencies();
    directStaticState = staticSpecialization && vpi.hasComputeGraph() &&
                        (!stateLayout->directHandles.empty() ||
                         !stateLayout->guardedHandles.empty());
    staticNBA = staticSpecialization && !stateLayout->nbaHandles.empty();
  }
  if (staticControl) {
    module.walk([&](Operation *operation) {
      if (llvm::any_of(operation->getOperandTypes(),
                       [](Type type) { return isa<FloatType>(type); }) ||
          llvm::any_of(operation->getResultTypes(),
                       [](Type type) { return isa<FloatType>(type); })) {
        staticControl = false;
        staticFanout = false;
        staticFanoutMetadata = false;
      }
    });
  }
  // State, NBA, and fanout are independent capabilities. Direct access is
  // selected per operation by resolveDirectStaticStateRange; a wide or
  // otherwise generic root does not prevent an independent narrow root from
  // using generated planes.
  if (staticNBA) {
    FailureOr<NativeStaticNBAPlan> plan =
        buildNativeStaticNBAPlan(module, *stateLayout, staticNBACommits, true);
    if (failed(plan))
      return failure();
    staticNBAPlan = std::move(*plan);
    staticNBA = !staticNBAPlan.roots.empty();
    if (failed(materializeGeneratedNBAAccumulators(module, staticNBAPlan)))
      return failure();
    directStaticState |=
        llvm::any_of(staticNBAPlan.generatedAccumulators,
                     [](const std::string &name) { return !name.empty(); });
    for (auto [root, accumulator] : llvm::zip_equal(
             staticNBAPlan.roots, staticNBAPlan.generatedAccumulators))
      if (!accumulator.empty())
        stateLayout->directHandles.insert(root.static_state);
  }
  if (staticFanoutMetadata) {
    FailureOr<NativeStaticFanoutPlan> fanout = buildNativeStaticFanoutPlan(
        module, *stateLayout, aotEligibility.getActorSlots(), true);
    if (failed(fanout))
      return failure();
    staticFanoutPlan = std::move(*fanout);
    staticFanoutMetadata &= staticFanoutPlan.exact;
    staticFanout &= staticFanoutPlan.exact;
    if (staticFanoutPlan.exact) {
      stateLayout->transitionHandlesExact = true;
      for (const obelisk_rt_static_fanout_entry &entry :
           staticFanoutPlan.entries)
        stateLayout->transitionHandles.insert(entry.static_state);
    }
  }
  if (staticSpecialization && useAOT) {
    FailureOr<SmallVector<obelisk_rt_static_actor_root>> dependencies =
        buildNativeStaticActorRootPlan(module, *stateLayout,
                                       aotEligibility.getActorSlots());
    if (failed(dependencies))
      return failure();
    staticActorRoots = std::move(*dependencies);
  }
  FailureOr<analysis::SimulationScheduleAnalysis> scheduleRanks =
      analysis::SimulationScheduleAnalysis::compute(module);
  if (failed(scheduleRanks))
    return failure();
  if (useAOT) {
    for (auto &entry : analyses) {
      auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
      if (!function)
        return failure();
      auto bytecode =
          aotEligibility.getBytecodeFragments().find(entry.first);
      if (bytecode == aotEligibility.getBytecodeFragments().end())
        continue;
      SmallPtrSet<Block *, 8> bytecodeBlocks(bytecode->second.begin(),
                                             bytecode->second.end());
      auto activationRequiresBytecode = [&](Block *start) {
        SmallVector<Block *> pending{start};
        SmallPtrSet<Block *, 16> visited;
        while (!pending.empty()) {
          Block *block = pending.pop_back_val();
          if (!visited.insert(block).second)
            continue;
          if (bytecodeBlocks.contains(block))
            return true;
          Operation *terminator = block->getTerminator();
          if (sim::isSuspensionOp(terminator))
            continue;
          llvm::append_range(pending, terminator->getSuccessors());
        }
        return false;
      };
      SmallVector<uint32_t> &continuations =
          aotBytecodeContinuations[entry.first];
      if (activationRequiresBytecode(&function.getBody().front()))
        continuations.push_back(0);
      for (const ProcessSuspension &suspension : entry.second->getSuspensions())
        if (activationRequiresBytecode(suspension.continuation))
          continuations.push_back(suspension.continuationID);
      llvm::sort(continuations);
      continuations.erase(
          std::unique(continuations.begin(), continuations.end()),
          continuations.end());
    }
  }
  // Root records are native implementation details, not canonical process
  // state. Insert them only after suspension-live semantic values have been
  // threaded and the shared native/bytecode frame has been analyzed. LLVM
  // coroutine lowering preserves these fixed entry allocas across resume.
  if (failed(instrumentManagedRoots(module)))
    return failure();
  bool guardedAOTSpecialization =
      staticSpecialization && useAOT && aotEligibility.isFullyEligible() &&
      vpi.allowsWrite() && (directStaticState || staticNBA);
  // AOT dispatch checks the specialization invariant once per actor
  // activation. Apply that proof to every non-bootstrap actor, including
  // delay/clock processes that are not part of a fused compute body.
  if (guardedAOTSpecialization)
    for (const auto &entry : aotEligibility.getActorSlots()) {
      auto function = dyn_cast<sim::SimFuncOp>(entry.first);
      if (!function ||
          function.getEntryKind() == sim::EntryKind::RootInitializer)
        continue;
      function.walk([&](Operation *nested) {
        if (isa<sim::SimRefLoadOp, sim::SimRefStoreOp>(nested))
          nested->setAttr(assumeCleanSpecializationAttr,
                          UnitAttr::get(context));
      });
    }
  if (failed(markCleanStaticNBAsInGuardedBodies(
          module, guardedAOTSpecialization, staticNBAPlan.siteRoots,
          staticNBAPlan.roots, *stateLayout)))
    return failure();

  bool enableDirectStaticState =
      staticSpecialization && useAOT && aotEligibility.isFullyEligible();
  if (failed(lowerPackedSimulationOperations(
          module, dataLayout, *stateLayout, enableDirectStaticState,
          staticNBA ? &staticNBAPlan : nullptr, vpi.allowsWrite())))
    return failure();

  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> aotFusionGroups;
  if (useAOT) {
    sim::SimDesignOp design;
    module.walk([&](sim::SimDesignOp candidate) { design = candidate; });
    ArrayAttr fusions =
        design ? design->getAttrOfType<ArrayAttr>(sim::metadata::staticFusion)
               : ArrayAttr{};
    sim::ComputeGraphAttr graph =
        design ? design.getComputeGraphAttr() : nullptr;
    if (fusions && graph) {
      for (Attribute fusionAttribute : fusions) {
        auto fusion = dyn_cast<sim::ComputeFusionAttr>(fusionAttribute);
        if (!fusion)
          return design.emitOpError("has malformed static fusion metadata"),
                 failure();
        for (int64_t fragmentIndex : fusion.getFragments().asArrayRef()) {
          if (fragmentIndex < 0 ||
              static_cast<uint64_t>(fragmentIndex) >= graph.getNodes().size())
            return design.emitOpError(
                       "static fusion references an invalid compute fragment"),
                   failure();
          auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
              graph.getNodes()[static_cast<size_t>(fragmentIndex)]);
          sim::SimFuncOp function = fragment
                                        ? design.lookupSymbol<sim::SimFuncOp>(
                                              fragment.getFunction().getValue())
                                        : nullptr;
          Block *block = function ? analysis::lookupComputeGraphBlock(
                                        function, fragment.getBlock())
                                  : nullptr;
          auto actor =
              function
                  ? aotEligibility.getActorSlots().find(function.getOperation())
                  : aotEligibility.getActorSlots().end();
          sim::ContinuationSiteAttr site;
          if (block) {
            Operation *terminator = block->getTerminator();
            if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(terminator))
              site = suspend.getSiteAttr();
            else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(terminator))
              site = suspend.getSiteAttr();
          }
          if (!fragment || !block || !site)
            return design.emitOpError(
                       "static fusion references a stale AOT continuation"),
                   failure();
          // Fusion metadata describes graph-level opportunities and is built
          // before native AOT actor eligibility is known. Hybrid lowering must
          // retain valid bytecode-only fragments without treating them as
          // stale metadata.
          if (actor == aotEligibility.getActorSlots().end())
            continue;
          auto [entry, inserted] = aotFusionGroups.try_emplace(
              std::pair{actor->second, site.getId()}, fusion.getId());
          if (!inserted && entry->second != fusion.getId())
            return design.emitOpError(
                       "AOT continuation appears in multiple fusion groups"),
                   failure();
        }
      }
    }
  }
  auto fusionGroupFor = [&](uint32_t slot, uint32_t continuation) {
    auto found = aotFusionGroups.find({slot, continuation});
    return found == aotFusionGroups.end() ? UINT32_MAX : found->second;
  };

  SmallVector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>>
      rankedAOTNodes;
  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    NativeSchedulePlan schedule;
    schedule.initialRank =
        scheduleRanks->getEntryRank(entry.first).value_or(0);
    if (useAOT) {
      auto slot = aotEligibility.getActorSlots().find(entry.first);
      if (slot != aotEligibility.getActorSlots().end())
        schedule.actorSlot = slot->second;
    }
    if (schedule.actorSlot) {
      auto bytecode = aotBytecodeContinuations.find(entry.first);
      if (bytecode != aotBytecodeContinuations.end())
        schedule.bytecodeContinuations = bytecode->second;
    }
    DenseMap<uint32_t, uint32_t> continuationRanks;
    for (const ProcessSuspension &suspension : entry.second->getSuspensions()) {
      uint32_t rank =
          scheduleRanks->getBlockRank(suspension.continuation).value_or(0);
      auto [rankIt, inserted] =
          continuationRanks.try_emplace(suspension.continuationID, rank);
      if (!inserted && rankIt->second != rank)
        return suspension.operation->emitError(
            "continuation ID has inconsistent schedule ranks");
    }
    for (auto [continuation, rank] : continuationRanks)
      schedule.continuations.emplace_back(continuation, rank);
    llvm::sort(schedule.continuations, [](const auto &left, const auto &right) {
      return left.first < right.first;
    });
    if (schedule.actorSlot) {
      rankedAOTNodes.emplace_back(
          scheduleRanks->getBlockRank(&function.getBody().front()).value_or(0),
          *schedule.actorSlot, 0, UINT32_MAX);
      for (const ProcessSuspension &suspension : entry.second->getSuspensions())
        rankedAOTNodes.emplace_back(
            scheduleRanks->getBlockRank(suspension.continuation).value_or(0),
            *schedule.actorSlot, suspension.continuationID,
            fusionGroupFor(*schedule.actorSlot, suspension.continuationID));
    }
    if (failed(makeProcessActivationHelper(module, function, *entry.second)))
      return failure();
    if (failed(
            makeProcessSpawnHelper(module, function, *entry.second, schedule)))
      return failure();
  }
  if (useAOT) {
    llvm::SmallDenseSet<uint32_t, 16> entrySlots;
    for (auto [rank, slot, continuation, fusionGroup] : rankedAOTNodes) {
      (void)rank;
      (void)fusionGroup;
      if (slot >= aotEligibility.getActorSlots().size())
        return module.emitError("AOT node references an invalid actor slot");
      if (continuation == 0)
        entrySlots.insert(slot);
    }
    if (entrySlots.size() != aotEligibility.getActorSlots().size())
      return module.emitError(
          "AOT node inventory is missing an actor entry continuation");
    llvm::sort(rankedAOTNodes);
    rankedAOTNodes.erase(
        std::unique(rankedAOTNodes.begin(), rankedAOTNodes.end()),
        rankedAOTNodes.end());
    SmallVector<obelisk_rt_native_schedule_node> executableNodes;
    executableNodes.reserve(rankedAOTNodes.size());
    for (auto [rank, slot, continuation, fusionGroup] : rankedAOTNodes) {
      (void)rank;
      executableNodes.push_back({slot, continuation, fusionGroup});
    }
    bool rootSlotZero =
        llvm::any_of(aotEligibility.getActorSlots(), [](const auto &entry) {
          auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
          return function &&
                 function.getEntryKind() == sim::EntryKind::RootInitializer &&
                 entry.second == 0;
        });
    if (failed(makeNativeAOTPlan(
            module, aotEligibility.getActorSlots().size(), executableNodes,
            *stateLayout, staticNBAPlan, staticFanoutPlan, staticActorRoots,
            directStaticState, staticNBA, staticControl, staticFanout,
            cleanSuperstep, aotEligibility.isFullyEligible(), rootSlotZero,
            vpi)))
      return failure();
  }
  if (failed(makeSchedulerMain(module, *stateLayout, useAOT)))
    return failure();

  SmallVector<sim::SimFuncOp> ordinary;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::Function ||
        function.getEntryKind() == sim::EntryKind::Observer)
      ordinary.push_back(function);
  });
  for (sim::SimFuncOp function : ordinary)
    if (failed(lowerOrdinaryFunction(function)))
      return failure();
  if (failed(materializeManagedMethodThunks(module, dataLayout)))
    return failure();

  for (auto &entry : analyses) {
    auto function = dyn_cast_if_present<sim::SimFuncOp>(entry.first);
    if (!function)
      return failure();
    LogicalResult lowered =
        entry.second->getSuspensions().empty()
            ? lowerPlainNativeProcess(function, *entry.second)
            : lowerSuspendableProcess(function, *entry.second);
    if (failed(lowered))
      return failure();
  }

  SmallVector<sim::SimDesignOp> designs;
  module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
  for (sim::SimDesignOp design : designs) {
    SmallVector<Operation *> nested;
    for (Operation &operation : design.getBody().front())
      nested.push_back(&operation);
    for (Operation *operation : nested) {
      if (isa<sim::SimScopeDeclOp, sim::SimCodeUnitDeclOp,
              sim::SimStorageDeclOp, sim::SimNetDeclOp, sim::SimDriverDeclOp,
              sim::SimNetConnectDeclOp, sim::SimClassDeclOp,
              sim::SimCovergroupDeclOp, sim::SimClassFieldDeclOp,
              sim::SimClassMethodDeclOp>(operation)) {
        operation->erase();
        continue;
      }
      operation->moveBefore(design);
    }
    design.erase();
  }
  return success();
}

class ConvertObeliskSimProcessesToLLVMCoroutinesPass final
    : public impl::ConvertObeliskSimProcessesToLLVMCoroutinesPassBase<
          ConvertObeliskSimProcessesToLLVMCoroutinesPass> {
public:
  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layoutAttr) {
      module.emitError(
          "coroutine lowering requires an explicit llvm.data_layout");
      return signalPassFailure();
    }
    llvm::Expected<llvm::DataLayout> parsed =
        llvm::DataLayout::parse(layoutAttr.getValue());
    if (!parsed) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
      return signalPassFailure();
    }
    if (!parsed->isLittleEndian() || parsed->getPointerSizeInBits() != 64) {
      module.emitError("coroutine lowering currently requires a 64-bit "
                       "little-endian target");
      return signalPassFailure();
    }
    if (failed(validateProcessABI(module, *parsed)))
      return signalPassFailure();
    if (failed(validateRuntimeToLLVMPreconditions(module, *parsed)))
      return signalPassFailure();
    if (failed(materializeEmbeddedSimulationDesign(module)))
      return signalPassFailure();

    if (failed(prepareSimulationProcessesToLLVMCoroutines(module, *parsed)))
      return signalPassFailure();

    LowerToLLVMOptions options(&getContext());
    options.dataLayout = *parsed;
    LLVMTypeConverter converter(&getContext(), options);
    converter.addConversion([&](Type type) -> std::optional<Type> {
      Type converted = convertProcessType(type, &getContext());
      if (converted != type)
        return converted;
      return std::nullopt;
    });
    addRuntimeToLLVMTypeConversions(converter);
    RewritePatternSet patterns(&getContext());
    populateSimulationCoroutineToLLVMPatterns(converter, patterns);
    if (failed(verify(module)))
      return signalPassFailure();
    ConversionTarget target(getContext());
    target.addLegalDialect<LLVM::LLVMDialect>();
    target.addLegalOp<ModuleOp>();
    target.markUnknownOpDynamicallyLegal(
        [](Operation *operation) { return isa<LLVM::LLVMFuncOp>(operation); });
    if (failed(applyFullConversion(module, target, std::move(patterns)))) {
      signalPassFailure();
      return;
    }
    if (failed(materializeNativeObserverThunks(module))) {
      signalPassFailure();
      return;
    }
    if (failed(verify(module)))
      signalPassFailure();
  }
};

} // namespace

LogicalResult
prepareSimulationProcessesToLLVMCoroutines(ModuleOp module,
                                           const llvm::DataLayout &dataLayout) {
  return prepareSimulationProcessesForLLVMCoroutinesImpl(module, dataLayout);
}

void populateSimulationCoroutineToLLVMPatterns(
    const LLVMTypeConverter &converter, RewritePatternSet &patterns) {
  populateRuntimeToLLVMPatterns(converter, patterns);
  populateContextRuntimeToLLVMConversionPattern(patterns, converter);
  arith::populateArithToLLVMConversionPatterns(converter, patterns);
  cf::populateControlFlowToLLVMConversionPatterns(converter, patterns);
  populateMathToLLVMConversionPatterns(converter, patterns);
  populateFuncToLLVMConversionPatterns(converter, patterns);
}

} // namespace obelisk
