//===- SimulationToLLVMCoroutine.cpp - Native process coroutines ---------===//

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"

#include "SimulationToLLVMCoroutinePrivate.h"
#include "SimulationAOTPlanning.h"
#include "SimulationNBALowering.h"

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
using detail::materializeNativeSchedulerGlobals;
using detail::makeProcessDescriptor;
using detail::makeByteArrayGlobal;
using detail::makeConstantGlobal;
using detail::managedClassDescriptorName;
using detail::managedMethodThunkName;
using detail::managedRootRangePushCheckAttr;
using detail::managedRootRangeRecordAttr;
using detail::markCleanStaticNBAsInGuardedBodies;
using detail::markLikelyTrue;
using detail::materializeDPIThunks;
using detail::materializeManagedMethodThunks;
using detail::materializeGeneratedNBAAccumulators;
using detail::materializeNativeObserverThunks;
using detail::nativeStateWidth;
using detail::nativeTwoStateBlockUnknownsAttr;
using detail::notifySignal;
using detail::NativeCallResultLowering;
using detail::NativeReturnLowering;
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

constexpr uint64_t kNoOffset = std::numeric_limits<uint64_t>::max();

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

uint32_t suspensionKind(Operation *operation) {
  return TypeSwitch<Operation *, uint32_t>(operation)
      .Case<sim::SimSuspendDelayOp>([](auto) { return 1; })
      .Case<sim::SimSuspendChangeOp>([](auto) { return 2; })
      .Case<sim::SimSuspendLevelOp>([](auto) { return 2; })
      .Case<sim::SimSuspendEdgeOp>([](auto) { return 3; })
      .Case<sim::SimSuspendEdgeIffOp>([](auto) { return 3; })
      .Case<sim::SimSuspendAnyOp>([](auto) { return 3; })
      .Case<sim::SimSuspendEventOp>([](auto) { return 4; })
      .Case<sim::SimSuspendAwaitOp>([](auto) { return 5; })
      .Case<sim::SimSuspendJoinOp>([](auto) { return 6; })
      .Case<sim::SimSuspendForeverOp>([](auto) { return 7; })
      .Case<sim::SimSuspendChildrenOp>([](auto) { return 9; })
      .Case<sim::SimSuspendObserveOp>([](auto) { return 10; })
      .Default([](Operation *) { return 0; });
}

LogicalResult convertNativeAggregateType(Type type,
                                         SmallVectorImpl<Type> &results) {
  std::optional<unsigned> width = nativeStateWidth(type);
  if (!width)
    return failure();
  Type plane = IntegerType::get(type.getContext(), *width);
  results.push_back(plane);
  if (containsLogic(type))
    results.push_back(plane);
  return success();
}

bool hasNoLogic(Operation *operation) {
  for (Type type : operation->getOperandTypes())
    if (containsLogic(type))
      return false;
  for (Type type : operation->getResultTypes())
    if (containsLogic(type))
      return false;
  for (Region &region : operation->getRegions())
    for (Block &block : region)
      for (BlockArgument argument : block.getArguments())
        if (containsLogic(argument.getType()))
          return false;
  return true;
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
  storeAt(builder, location, action, 0,
          llvmConstant(builder, location, i32, actionKind), 4);
  storeAt(builder, location, action, 4,
          llvmConstant(builder, location, i32, suspendKind), 4);
  storeAt(builder, location, action, 8,
          llvmConstant(builder, location, i32, continuation), 4);
  storeAt(builder, location, action, 12,
          llvmConstant(builder, location, i32, flags), 4);
  storeAt(builder, location, action, 16, payload, 8);
  storeAt(builder, location, action, 24,
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
    publishAction(builder, location, instance, 3, 0, continuationID, 0,
                  activation, 0);
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
  publishAction(builder, location, instance, 1, kind, continuationID,
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
    if (slot.valueOffset == kNoOffset) {
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
  publishAction(builder, location, instance, 2, 0, 0, 0,
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
    if (slot.valueOffset == kNoOffset) {
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
          llvmConstant(builder, location, i32, 12), 4);
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
    publishAction(builder, location, instance, 2, 0, 0, 0,
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

struct NativeSchedulePlan {
  uint32_t initialRank = UINT32_MAX;
  SmallVector<std::pair<uint32_t, uint32_t>> continuations;
  SmallVector<uint32_t> bytecodeContinuations;
  std::optional<uint32_t> actorSlot;
};

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

std::optional<DirectStaticStateRange>
resolveDirectStaticStateRange(Value handle, unsigned width,
                              const NativeStateLayout *layout) {
  if (!layout || width == 0 || width > 64)
    return std::nullopt;
  std::optional<uint64_t> value = resolveCFGConstantInteger(handle);
  if (!value)
    return std::nullopt;
  obelisk_rt_stable_handle_v1 decoded{};
  if (!obelisk_rt_stable_handle_decode(*value, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset < 0)
    return std::nullopt;
  bool direct = layout->directHandles.contains(decoded.id);
  bool guarded = layout->guardedHandles.contains(decoded.id);
  if (!direct && !guarded)
    return std::nullopt;
  auto bound = llvm::find_if(layout->bounds, [&](const auto &candidate) {
    return candidate.handleID == decoded.id;
  });
  // Direct accesses are authorized either by the specialization policy or by
  // a fully static VPI-off wide-NBA plan, which records its roots in the same
  // direct-handle inventory before conversion.
  if (bound == layout->bounds.end() ||
      static_cast<uint64_t>(decoded.offset) > bound->width ||
      width > bound->width - static_cast<uint64_t>(decoded.offset))
    return std::nullopt;
  return DirectStaticStateRange{bound->offset +
                                    static_cast<uint64_t>(decoded.offset),
                                static_cast<uint64_t>(decoded.offset),
                                decoded.id, guarded};
}

struct DirectPackedPlane {
  Value address;
  IntegerType spanType;
  Value span;
  unsigned bitOffset;
};

DirectPackedPlane loadDirectPackedPlane(OpBuilder &builder, Location location,
                                        StringRef globalName,
                                        uint64_t bitOffset, unsigned width) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  IntegerType i8 = builder.getI8Type();
  IntegerType i64 = builder.getI64Type();
  uint64_t firstByte = bitOffset / 8;
  unsigned firstBit = static_cast<unsigned>(bitOffset % 8);
  unsigned byteCount = (firstBit + width + 7) / 8;
  IntegerType spanType = builder.getIntegerType(byteCount * 8);
  Value base =
      LLVM::AddressOfOp::create(builder, location, pointer, globalName);
  Value address = LLVM::GEPOp::create(
      builder, location, pointer, i8, base,
      ValueRange{llvmConstant(builder, location, i64, firstByte)});
  Value span = LLVM::LoadOp::create(builder, location, spanType, address, 1);
  return {address, spanType, span, firstBit};
}

Value extractDirectPackedPlane(OpBuilder &builder, Location location,
                               const DirectPackedPlane &plane,
                               IntegerType resultType) {
  Value shifted = plane.span;
  if (plane.bitOffset != 0)
    shifted = arith::ShRUIOp::create(
        builder, location, shifted,
        llvmConstant(builder, location, plane.spanType, plane.bitOffset));
  if (shifted.getType() == resultType)
    return shifted;
  return LLVM::TruncOp::create(builder, location, resultType, shifted);
}

Value storeDirectPackedPlane(OpBuilder &builder, Location location, Value input,
                             StringRef globalName, uint64_t bitOffset) {
  IntegerType inputType = cast<IntegerType>(input.getType());
  DirectPackedPlane plane = loadDirectPackedPlane(
      builder, location, globalName, bitOffset, inputType.getWidth());
  APInt fieldMask =
      APInt::getBitsSet(plane.spanType.getWidth(), plane.bitOffset,
                        plane.bitOffset + inputType.getWidth());
  Value mask = arith::ConstantOp::create(
      builder, location, plane.spanType,
      builder.getIntegerAttr(plane.spanType, fieldMask));
  Value preserved = arith::AndIOp::create(
      builder, location, plane.span,
      arith::XOrIOp::create(
          builder, location, mask,
          arith::ConstantOp::create(
              builder, location, plane.spanType,
              builder.getIntegerAttr(
                  plane.spanType,
                  APInt::getAllOnes(plane.spanType.getWidth())))));
  Value extended =
      inputType == plane.spanType
          ? input
          : LLVM::ZExtOp::create(builder, location, plane.spanType, input);
  if (plane.bitOffset != 0)
    extended = arith::ShLIOp::create(
        builder, location, extended,
        llvmConstant(builder, location, plane.spanType, plane.bitOffset));
  Value updated = arith::OrIOp::create(
      builder, location, preserved,
      arith::AndIOp::create(builder, location, extended, mask));
  LLVM::StoreOp::create(builder, location, updated, plane.address, 1);
  Value old = extractDirectPackedPlane(builder, location, plane, inputType);
  return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne, old,
                               input);
}

} // namespace detail

namespace detail {

Value loadStatePlane(ConversionPatternRewriter &rewriter, Location location,
                     Value handle, IntegerType resultType, StringRef globalName,
                     bool unknownFallback, uint64_t stateBitCount,
                     const NativeStateLayout *directLayout,
                     Value guardedPermission, bool assumeClean) {
  std::optional<DirectStaticStateRange> range = resolveDirectStaticStateRange(
      handle, resultType.getWidth(), directLayout);
  if (range && (!range->guarded || assumeClean))
    return extractDirectPackedPlane(
        rewriter, location,
        loadDirectPackedPlane(rewriter, location, globalName, range->offset,
                              resultType.getWidth()),
        resultType);

  auto emitGeneric = [&]() -> Value {
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    IntegerType i32 = rewriter.getI32Type();
    IntegerType i64 = rewriter.getI64Type();
    Value base =
        LLVM::AddressOfOp::create(rewriter, location, pointer, globalName);
    Value out = entryAlloca(rewriter, location, resultType, 1, 1);
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    LLVM::CallOp::create(
        rewriter, location, TypeRange{i32},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_native_state_load_plane"),
        ValueRange{
            context, base, llvmConstant(rewriter, location, i64, stateBitCount),
            handle,
            llvmConstant(rewriter, location, i64, resultType.getWidth()),
            llvmConstant(rewriter, location, i32,
                         globalName == "__obelisk_state_unknown" ? 1 : 0),
            llvmConstant(rewriter, location, i32, unknownFallback ? 1 : 0),
            out});
    return LLVM::LoadOp::create(rewriter, location, resultType, out, 1);
  };
  if (!range || !range->guarded)
    return emitGeneric();

  Block *head = rewriter.getInsertionBlock();
  Block *continuation = rewriter.splitBlock(head, rewriter.getInsertionPoint());
  BlockArgument result = continuation->addArgument(resultType, location);
  Region *region = head->getParent();
  Block *directBlock =
      rewriter.createBlock(region, continuation->getIterator());
  Block *genericBlock =
      rewriter.createBlock(region, continuation->getIterator());
  recordStaticSpecializationCFGBlocks(rewriter, head, 3);

  rewriter.setInsertionPointToEnd(head);
  Value useDirect =
      guardedPermission
          ? guardedPermission
          : staticSpecializationGuard(rewriter, location, range->staticID,
                                      OBELISK_RT_STATIC_ROOT_READ);
  markLikelyTrue(cf::CondBranchOp::create(rewriter, location, useDirect,
                                          directBlock, ValueRange{},
                                          genericBlock, ValueRange{}));

  rewriter.setInsertionPointToEnd(directBlock);
  Value direct = extractDirectPackedPlane(
      rewriter, location,
      loadDirectPackedPlane(rewriter, location, globalName, range->offset,
                            resultType.getWidth()),
      resultType);
  cf::BranchOp::create(rewriter, location, continuation, ValueRange{direct});

  rewriter.setInsertionPointToEnd(genericBlock);
  Value generic = emitGeneric();
  cf::BranchOp::create(rewriter, location, continuation, ValueRange{generic});

  rewriter.setInsertionPointToStart(continuation);
  return result;
}

Value storeStatePlane(ConversionPatternRewriter &rewriter, Location location,
                      Value handle, Value input, StringRef globalName,
                      uint64_t stateBitCount,
                      const NativeStateLayout *directLayout,
                      Value guardedPermission, bool assumeClean) {
  IntegerType inputType = cast<IntegerType>(input.getType());
  std::optional<DirectStaticStateRange> range =
      resolveDirectStaticStateRange(handle, inputType.getWidth(), directLayout);
  if (range && (!range->guarded || assumeClean))
    return storeDirectPackedPlane(rewriter, location, input, globalName,
                                  range->offset);

  auto emitGeneric = [&]() -> Value {
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    IntegerType i8 = rewriter.getI8Type();
    IntegerType i32 = rewriter.getI32Type();
    IntegerType i64 = rewriter.getI64Type();
    Value base =
        LLVM::AddressOfOp::create(rewriter, location, pointer, globalName);
    Value in = entryAlloca(rewriter, location, inputType, 1, 1);
    LLVM::StoreOp::create(rewriter, location, input, in, 1);
    Value changed = entryAlloca(rewriter, location, i8, 1, 1);
    LLVM::StoreOp::create(rewriter, location,
                          llvmConstant(rewriter, location, i8, 0), changed, 1);
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    LLVM::CallOp::create(
        rewriter, location, TypeRange{i32},
        SymbolRefAttr::get(rewriter.getContext(),
                           "obelisk_rt_v1_native_state_store_plane"),
        ValueRange{
            context, base, llvmConstant(rewriter, location, i64, stateBitCount),
            handle, llvmConstant(rewriter, location, i64, inputType.getWidth()),
            llvmConstant(rewriter, location, i32,
                         globalName == "__obelisk_state_unknown" ? 1 : 0),
            in, changed});
    Value changedByte =
        LLVM::LoadOp::create(rewriter, location, i8, changed, 1);
    return arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::ne,
                                 changedByte,
                                 llvmConstant(rewriter, location, i8, 0));
  };
  if (!range || !range->guarded)
    return emitGeneric();

  Block *head = rewriter.getInsertionBlock();
  Block *continuation = rewriter.splitBlock(head, rewriter.getInsertionPoint());
  BlockArgument changed =
      continuation->addArgument(rewriter.getI1Type(), location);
  Region *region = head->getParent();
  Block *directBlock =
      rewriter.createBlock(region, continuation->getIterator());
  Block *genericBlock =
      rewriter.createBlock(region, continuation->getIterator());
  recordStaticSpecializationCFGBlocks(rewriter, head, 3);

  rewriter.setInsertionPointToEnd(head);
  Value useDirect =
      guardedPermission
          ? guardedPermission
          : staticSpecializationGuard(rewriter, location, range->staticID,
                                      OBELISK_RT_STATIC_ROOT_WRITE);
  markLikelyTrue(cf::CondBranchOp::create(rewriter, location, useDirect,
                                          directBlock, ValueRange{},
                                          genericBlock, ValueRange{}));

  rewriter.setInsertionPointToEnd(directBlock);
  Value directChanged = storeDirectPackedPlane(rewriter, location, input,
                                               globalName, range->offset);
  cf::BranchOp::create(rewriter, location, continuation,
                       ValueRange{directChanged});

  rewriter.setInsertionPointToEnd(genericBlock);
  Value genericChanged = emitGeneric();
  cf::BranchOp::create(rewriter, location, continuation,
                       ValueRange{genericChanged});

  rewriter.setInsertionPointToStart(continuation);
  return changed;
}

} // namespace detail

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

LogicalResult
makeProcessActivationHelper(ModuleOp module, sim::SimFuncOp function,
                            const SimulationProcessFrameAnalysis &analysis) {
  if (function.getEntryKind() != sim::EntryKind::Task)
    return success();
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = function.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  SmallVector<Type> arguments;
  for (BlockArgument argument : function.getBody().front().getArguments())
    arguments.push_back(convertProcessType(argument.getType(), context));
  std::string helperName =
      (function.getSymName() + ".__obelisk_activate").str();
  if (module.lookupSymbol(helperName))
    return success();
  builder.setInsertionPointAfter(function);
  auto helper = LLVM::LLVMFuncOp::create(
      builder, location, helperName,
      LLVM::LLVMFunctionType::get(i64, arguments, false));
  Block *entry = helper.addEntryBlock(builder);
  Block *created = new Block;
  Block *failed = new Block;
  helper.getBody().push_back(created);
  helper.getBody().push_back(failed);
  builder.setInsertionPointToStart(entry);
  Value one = llvmConstant(builder, location, i64, 1);
  Value outInstance =
      LLVM::AllocaOp::create(builder, location, pointer, pointer, one, 8);
  LLVM::StoreOp::create(builder, location,
                        LLVM::ZeroOp::create(builder, location, pointer),
                        outInstance, 8);
  Value descriptor = LLVM::AddressOfOp::create(
      builder, location, pointer,
      (function.getSymName() + ".__obelisk_process_descriptor").str());
  Value status =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_process_instance_create"),
          ValueRange{descriptor, outInstance})
          .getResult();
  Value succeeded =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq, status,
                            llvmConstant(builder, location, i32, 0));
  LLVM::CondBrOp::create(builder, location, succeeded, created, failed);

  builder.setInsertionPointToStart(failed);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{entry->getArgument(0), status});
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i64, 0));

  builder.setInsertionPointToStart(created);
  Value instance =
      LLVM::LoadOp::create(builder, location, pointer, outInstance, 8);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
  size_t physicalArgument = 0;
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (slot.valueOffset == kNoOffset) {
      ++physicalArgument;
      continue;
    }
    if (physicalArgument >= entry->getNumArguments())
      return helper.emitError(
          "activation capture layout has too few arguments");
    storeAt(builder, location, frame, slot.valueOffset,
            entry->getArgument(physicalArgument++), slot.alignment);
    if (slot.hasSecondaryStorage()) {
      if (physicalArgument >= entry->getNumArguments())
        return helper.emitError(
            "activation capture is missing its secondary value");
      storeAt(builder, location, frame, slot.getSecondaryOffset(),
              entry->getArgument(physicalArgument++), slot.alignment);
    }
  }
  if (physicalArgument != entry->getNumArguments())
    return helper.emitError("activation capture layout has excess arguments");
  Value encoded = LLVM::PtrToIntOp::create(builder, location, i64, instance);
  LLVM::ReturnOp::create(builder, location, encoded);
  return success();
}

LogicalResult
makeProcessSpawnHelper(ModuleOp module, sim::SimFuncOp function,
                       const SimulationProcessFrameAnalysis &analysis,
                       const NativeSchedulePlan &schedule) {
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = function.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  SmallVector<Type> arguments;
  for (BlockArgument argument : function.getBody().front().getArguments())
    arguments.push_back(convertProcessType(argument.getType(), context));
  std::string helperName = (function.getSymName() + ".__obelisk_spawn").str();
  if (module.lookupSymbol(helperName))
    return success();
  std::string continuationName =
      (function.getSymName() + ".__obelisk_schedule_continuations").str();
  std::string rankName =
      (function.getSymName() + ".__obelisk_schedule_ranks").str();
  std::string bytecodeContinuationName =
      (function.getSymName() + ".__obelisk_bytecode_continuations").str();
  if (!schedule.continuations.empty()) {
    auto arrayType =
        LLVM::LLVMArrayType::get(i32, schedule.continuations.size());
    auto makeArray = [&](StringRef name, unsigned element) {
      makeConstantGlobal(
          module, location, arrayType, name, LLVM::Linkage::Internal, 4,
          [&](OpBuilder &initializer) {
            Value array =
                LLVM::ZeroOp::create(initializer, location, arrayType);
            for (auto [index, continuation] :
                 llvm::enumerate(schedule.continuations))
              array = LLVM::InsertValueOp::create(
                  initializer, location, array,
                  llvmConstant(initializer, location, i32,
                               element == 0 ? continuation.first
                                            : continuation.second),
                  ArrayRef<int64_t>{static_cast<int64_t>(index)});
            return array;
          });
    };
    makeArray(continuationName, 0);
    makeArray(rankName, 1);
  }
  if (!schedule.bytecodeContinuations.empty()) {
    auto arrayType =
        LLVM::LLVMArrayType::get(i32, schedule.bytecodeContinuations.size());
    makeConstantGlobal(
        module, location, arrayType, bytecodeContinuationName,
        LLVM::Linkage::Internal, 4, [&](OpBuilder &initializer) {
          Value array = LLVM::ZeroOp::create(initializer, location, arrayType);
          for (auto [index, continuation] :
               llvm::enumerate(schedule.bytecodeContinuations))
            array = LLVM::InsertValueOp::create(
                initializer, location, array,
                llvmConstant(initializer, location, i32, continuation),
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          return array;
        });
  }
  builder.setInsertionPointAfter(function);
  auto helper = LLVM::LLVMFuncOp::create(
      builder, location, helperName,
      LLVM::LLVMFunctionType::get(i64, arguments, false));
  Block *entry = helper.addEntryBlock(builder);
  Block *created = new Block;
  Block *createFailed = new Block;
  Block *added = new Block;
  Block *addFailed = new Block;
  helper.getBody().push_back(created);
  helper.getBody().push_back(createFailed);
  helper.getBody().push_back(added);
  helper.getBody().push_back(addFailed);
  builder.setInsertionPointToStart(entry);
  Value one = llvmConstant(builder, location, i64, 1);
  Value outInstance =
      LLVM::AllocaOp::create(builder, location, pointer, pointer, one, 8);
  LLVM::StoreOp::create(builder, location,
                        LLVM::ZeroOp::create(builder, location, pointer),
                        outInstance, 8);
  Value descriptor = LLVM::AddressOfOp::create(
      builder, location, pointer,
      (function.getSymName() + ".__obelisk_process_descriptor").str());
  auto create = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, "obelisk_rt_v1_process_instance_create"),
      ValueRange{descriptor, outInstance});
  Value createSucceeded = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, create.getResult(),
      llvmConstant(builder, location, i32, 0));
  LLVM::CondBrOp::create(builder, location, createSucceeded, created,
                         createFailed);

  builder.setInsertionPointToStart(createFailed);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{entry->getArgument(0), create.getResult()});
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i64, 0));

  builder.setInsertionPointToStart(created);
  Value instance =
      LLVM::LoadOp::create(builder, location, pointer, outInstance, 8);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
  size_t physicalArgument = 0;
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (slot.valueOffset == kNoOffset) {
      ++physicalArgument;
      continue;
    }
    if (physicalArgument >= entry->getNumArguments())
      return helper.emitError("spawn capture layout has too few arguments");
    storeAt(builder, location, frame, slot.valueOffset,
            entry->getArgument(physicalArgument++), slot.alignment);
    if (slot.hasSecondaryStorage()) {
      if (physicalArgument >= entry->getNumArguments())
        return helper.emitError("spawn capture is missing its secondary value");
      storeAt(builder, location, frame, slot.getSecondaryOffset(),
              entry->getArgument(physicalArgument++), slot.alignment);
    }
  }
  if (physicalArgument != entry->getNumArguments())
    return helper.emitError("spawn capture layout has excess arguments");
  uint32_t homeRegion = getRuntimeEventRegion(function.getHomeRegion());
  if (homeRegion == UINT32_MAX)
    return function.emitOpError("has no executable runtime home region");
  uint32_t scheduleFlags = OBELISK_RT_SCHEDULE_HOME(homeRegion) |
                           (function.getEntryKind() == sim::EntryKind::Final
                                ? OBELISK_RT_SCHEDULE_FINAL
                                : 0);
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value continuationAddress = null;
  Value rankAddress = null;
  if (!schedule.continuations.empty()) {
    continuationAddress =
        LLVM::AddressOfOp::create(builder, location, pointer, continuationName);
    rankAddress =
        LLVM::AddressOfOp::create(builder, location, pointer, rankName);
  }
  SmallVector<Value> addArguments{
      entry->getArgument(0), instance,
      llvmConstant(builder, location, i32, scheduleFlags)};
  StringRef addName = "obelisk_rt_v1_scheduler_add_planned";
  if (schedule.actorSlot) {
    addName = "obelisk_rt_v1_scheduler_add_aot";
    addArguments.push_back(
        llvmConstant(builder, location, i32, *schedule.actorSlot));
  }
  llvm::append_range(
      addArguments,
      ValueRange{
          llvmConstant(builder, location, i32, schedule.initialRank),
          continuationAddress, rankAddress,
          llvmConstant(builder, location, i32, schedule.continuations.size())});
  if (schedule.actorSlot) {
    Value bytecodeContinuations = null;
    if (!schedule.bytecodeContinuations.empty())
      bytecodeContinuations = LLVM::AddressOfOp::create(
          builder, location, pointer, bytecodeContinuationName);
    addArguments.push_back(bytecodeContinuations);
    addArguments.push_back(llvmConstant(builder, location, i32,
                                        schedule.bytecodeContinuations.size()));
  }
  auto add =
      LLVM::CallOp::create(builder, location, TypeRange{i32},
                           SymbolRefAttr::get(context, addName), addArguments);
  Value addSucceeded = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, add.getResult(),
      llvmConstant(builder, location, i32, 0));
  LLVM::CondBrOp::create(builder, location, addSucceeded, added, addFailed);

  builder.setInsertionPointToStart(addFailed);
  LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, "obelisk_rt_v1_process_instance_destroy"),
      instance);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{entry->getArgument(0), add.getResult()});
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i64, 0));

  builder.setInsertionPointToStart(added);
  Value token =
      LLVM::CallOp::create(
          builder, location, TypeRange{i64},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_process_token"),
          ValueRange{entry->getArgument(0), instance})
          .getResult();
  LLVM::ReturnOp::create(builder, location, token);

  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_instance_create", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_add_planned", i32,
                           {pointer, pointer, i32, i32, pointer, pointer, i32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_add_aot", i32,
      {pointer, pointer, i32, i32, i32, pointer, pointer, i32, pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_process_token", i64,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_fail",
                           LLVM::LLVMVoidType::get(context), {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_disable_children",
                           i32, {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_enter", i32,
                           {pointer, i64, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_leave", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_disable", i32,
                           {pointer, i64, i64, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_once", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_deferred_once", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_monitor_register", i32,
                           {pointer, i64, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_monitor_control", i32,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_monitor_current", i32,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_instance_destroy",
                           i32, {pointer});
  return success();
}

LogicalResult makeNativeAOTPlan(
    ModuleOp module, uint32_t actorCount,
    ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    ArrayRef<obelisk_rt_static_actor_root> actorRoots, bool enableDirectState,
    bool enableStaticNBA, bool enableStaticControl, bool enableStaticFanout,
    bool enableCleanSuperstep, bool fullyStatic, bool rootSlotZero,
    const analysis::SimulationVPIAnalysis &vpi) {
  if (actorCount == 0 || executableNodes.empty())
    return module.emitError("AOT schedule has no executable actor nodes");
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  ArrayRef<obelisk_rt_static_nba_root> nbaRoots = staticNBAPlan.roots;
  ArrayRef<obelisk_rt_static_nba_site> nbaSites = staticNBAPlan.sites;
  ArrayRef<obelisk_rt_static_fanout_entry> fanoutEntries =
      staticFanoutPlan.exact
          ? ArrayRef<obelisk_rt_static_fanout_entry>(staticFanoutPlan.entries)
          : ArrayRef<obelisk_rt_static_fanout_entry>{};
  // Static time/region control and exact actor fanout are independent of VPI
  // reads. Writable VPI hands dirty roots and the affected event slot to the
  // existing guarded state/control paths; the exact dependency table remains
  // valid and can continue to wake native actors without subscriptions.
  bool staticControlEnabled =
      enableStaticControl && fullyStatic && vpi.hasComputeGraph();
  bool staticFanoutEnabled = enableStaticFanout && staticFanoutPlan.exact &&
                             fullyStatic && vpi.hasComputeGraph();
  bool guardedFanoutEnabled = !staticFanoutEnabled && staticFanoutPlan.exact &&
                              fullyStatic && vpi.hasComputeGraph();
  bool guardedSpecializationEnabled =
      vpi.allowsWrite() && (enableDirectState || enableStaticNBA);
  bool cleanSuperstepEnabled = enableCleanSuperstep && staticControlEnabled &&
                               staticFanoutPlan.exact && fullyStatic;
  uint64_t graphLayoutChecksum = 0;
  if (auto image =
          module->getAttrOfType<DenseI8ArrayAttr>("obelisk.bytecode.image")) {
    ArrayRef<int8_t> bytes = image.asArrayRef();
    if (bytes.size() < 40)
      return module.emitError("embedded bytecode checksum is truncated");
    for (unsigned byte = 0; byte != 8; ++byte)
      graphLayoutChecksum |= uint64_t{static_cast<uint8_t>(bytes[32 + byte])}
                             << (byte * 8);
  }
  Type stateType = LLVM::LLVMArrayType::get(pointer, actorCount);
  constexpr StringLiteral stateName = "__obelisk_aot_schedule_state_v1";
  constexpr StringLiteral nodesName = "__obelisk_aot_schedule_nodes_v1";
  constexpr StringLiteral nbaRootsName = "__obelisk_aot_nba_roots_v1";
  constexpr StringLiteral nbaSitesName = "__obelisk_aot_nba_sites_v1";
  constexpr StringLiteral fanoutName = "__obelisk_aot_static_fanout_v1";
  constexpr StringLiteral actorRootsName =
      "__obelisk_aot_static_actor_roots_v1";
  constexpr StringLiteral bindName = "__obelisk_aot_schedule_bind_v1";
  constexpr StringLiteral runName = "__obelisk_aot_schedule_run_v1";
  constexpr StringLiteral snapshotName = "__obelisk_aot_schedule_snapshot_v1";
  constexpr StringLiteral nbaCommitName = "__obelisk_aot_static_nba_commit_v1";
  constexpr StringLiteral planName = "__obelisk_aot_schedule_plan_v1";

  builder.setInsertionPointToStart(module.getBody());
  auto state = LLVM::GlobalOp::create(builder, location, stateType, false,
                                      LLVM::Linkage::Internal, stateName,
                                      Attribute{}, 8);
  Block *initializer = new Block;
  state.getInitializerRegion().push_back(initializer);
  builder.setInsertionPointToStart(initializer);
  LLVM::ReturnOp::create(builder, location,
                         LLVM::ZeroOp::create(builder, location, stateType));

  Type nodeType = LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32});
  Type nodesType = LLVM::LLVMArrayType::get(nodeType, executableNodes.size());
  makeConstantGlobal(
      module, location, nodesType, nodesName, LLVM::Linkage::Internal, 4,
      [&](OpBuilder &initializerBuilder) {
        Value nodes =
            LLVM::ZeroOp::create(initializerBuilder, location, nodesType);
        for (auto [index, node] : llvm::enumerate(executableNodes)) {
          Value value =
              LLVM::ZeroOp::create(initializerBuilder, location, nodeType);
          value = insertValue(
              initializerBuilder, location, value,
              llvmConstant(initializerBuilder, location, i32, node.actor_slot),
              0);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.continuation),
                              1);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.fusion_group),
                              2);
          nodes = LLVM::InsertValueOp::create(
              initializerBuilder, location, nodes, value,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return nodes;
      });

  Type nbaRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, pointer});
  if (!nbaRoots.empty()) {
    Type rootsType = LLVM::LLVMArrayType::get(nbaRootType, nbaRoots.size());
    makeConstantGlobal(
        module, location, rootsType, nbaRootsName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value roots =
              LLVM::ZeroOp::create(initializerBuilder, location, rootsType);
          for (auto [index, root] : llvm::enumerate(nbaRoots)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.commit_node),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, root.bit_width),
                2);
            Value accumulator =
                index < staticNBAPlan.generatedAccumulators.size() &&
                        !staticNBAPlan.generatedAccumulators[index].empty()
                    ? LLVM::AddressOfOp::create(
                          initializerBuilder, location, pointer,
                          staticNBAPlan.generatedAccumulators[index])
                          .getResult()
                    : LLVM::ZeroOp::create(initializerBuilder, location,
                                           pointer)
                          .getResult();
            value = insertValue(initializerBuilder, location, value,
                                accumulator, 3);
            roots = LLVM::InsertValueOp::create(
                initializerBuilder, location, roots, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return roots;
        });
  }
  Type nbaSiteType = LLVM::LLVMStructType::getLiteral(context, {i64, i32, i32});
  if (!nbaSites.empty()) {
    Type sitesType = LLVM::LLVMArrayType::get(nbaSiteType, nbaSites.size());
    makeConstantGlobal(
        module, location, sitesType, nbaSitesName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value sites =
              LLVM::ZeroOp::create(initializerBuilder, location, sitesType);
          for (auto [index, site] : llvm::enumerate(nbaSites)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaSiteType);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, site.site), 0);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.root), 1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.storage),
                2);
            sites = LLVM::InsertValueOp::create(
                initializerBuilder, location, sites, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return sites;
        });
  }
  Type fanoutType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32, i32, i64, i64});
  if (!fanoutEntries.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(fanoutType, fanoutEntries.size());
    makeConstantGlobal(
        module, location, entriesType, fanoutName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(fanoutEntries)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, fanoutType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                1);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.continuation),
                                2);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.edge), 3);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, entry.low_bit),
                4);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             entry.bit_width),
                                5);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }
  Type actorRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32, i32});
  if (!actorRoots.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(actorRootType, actorRoots.size());
    makeConstantGlobal(
        module, location, entriesType, actorRootsName, LLVM::Linkage::Internal,
        4, [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(actorRoots)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               actorRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.flags),
                2);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }

  builder.setInsertionPointToEnd(module.getBody());
  auto bind = LLVM::LLVMFuncOp::create(
      builder, location, bindName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  Block *bindEntry = bind.addEntryBlock(builder);
  builder.setInsertionPointToStart(bindEntry);
  Value slot =
      LLVM::ZExtOp::create(builder, location, i64, bindEntry->getArgument(2));
  Value actorAddress =
      LLVM::GEPOp::create(builder, location, pointer, pointer,
                          bindEntry->getArgument(0), ValueRange{slot});
  LLVM::StoreOp::create(builder, location, bindEntry->getArgument(3),
                        actorAddress, 8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, OBELISK_RT_OK));

  builder.setInsertionPointToEnd(module.getBody());
  auto run = LLVM::LLVMFuncOp::create(
      builder, location, runName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *runEntry = run.addEntryBlock(builder);
  builder.setInsertionPointToStart(runEntry);
  Value nodes =
      LLVM::AddressOfOp::create(builder, location, pointer, nodesName);
  Value runStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(
              context, fullyStatic ? "obelisk_rt_v1_scheduler_run_aot_nodes"
                                   : "obelisk_rt_v1_scheduler_run"),
          fullyStatic ? ValueRange{runEntry->getArgument(1), nodes,
                                   llvmConstant(builder, location, i32,
                                                executableNodes.size())}
                      : ValueRange{runEntry->getArgument(1)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, runStatus);

  builder.setInsertionPointToEnd(module.getBody());
  auto snapshot = LLVM::LLVMFuncOp::create(
      builder, location, snapshotName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, pointer}, false));
  Block *snapshotEntry = snapshot.addEntryBlock(builder);
  builder.setInsertionPointToStart(snapshotEntry);
  Value snapshotStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_snapshot_aot"),
          ValueRange{snapshotEntry->getArgument(1),
                     snapshotEntry->getArgument(2)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, snapshotStatus);

  builder.setInsertionPointToEnd(module.getBody());
  auto nbaCommit = LLVM::LLVMFuncOp::create(
      builder, location, nbaCommitName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  Block *nbaCommitEntry = nbaCommit.addEntryBlock(builder);
  builder.setInsertionPointToStart(nbaCommitEntry);
  Value nbaCommitStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_static_nba_commit_roots"),
          ValueRange{nbaCommitEntry->getArgument(1),
                     llvmConstant(builder, location, i32, nbaRoots.size()),
                     nbaCommitEntry->getArgument(2),
                     nbaCommitEntry->getArgument(3)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, nbaCommitStatus);

  auto planType = LLVM::LLVMStructType::getLiteral(
      context,
      {i32, i64,     pointer, i64,     i32,     i32,     pointer, pointer,
       i64, pointer, pointer, pointer, pointer, i32,     i32,     pointer,
       i64, pointer, i64,     pointer, i64,     pointer, pointer});
  makeConstantGlobal(
      module, location, planType, planName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &initializerBuilder) {
        Value value =
            LLVM::ZeroOp::create(initializerBuilder, location, planType);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32,
                                     sizeof(obelisk_rt_native_schedule_plan)),
                        0);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         graphLayoutChecksum),
                            1);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, stateName),
                        2);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         uint64_t{actorCount} * sizeof(void *)),
                            3);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, actorCount), 4);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(
                initializerBuilder, location, i32,
                (fullyStatic ? OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC : 0) |
                    (rootSlotZero ? OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO
                                  : 0) |
                    (staticControlEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL
                         : 0) |
                    (staticFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT
                         : 0) |
                    (enableDirectState ? OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE
                                       : 0) |
                    (enableStaticNBA ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA
                                     : 0) |
                    (fullyStatic ? OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS
                                 : 0) |
                    (guardedFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT
                         : 0) |
                    (guardedSpecializationEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION
                         : 0) |
                    (cleanSuperstepEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP
                         : 0)),
            5);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(initializerBuilder,
                                                      location, pointer,
                                                      "__obelisk_state_value"),
                            6);
        value = insertValue(
            initializerBuilder, location, value,
            LLVM::AddressOfOp::create(initializerBuilder, location, pointer,
                                      "__obelisk_state_unknown"),
            7);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         stateLayout.bitCount),
                            8);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, bindName),
                        9);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(
                                initializerBuilder, location, pointer, runName),
                            10);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, snapshotName),
                        11);
        Value rootsAddress =
            nbaRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaRootsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, rootsAddress, 12);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, nbaRoots.size()),
            13);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 14);
        Value sitesAddress =
            nbaSites.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaSitesName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, sitesAddress, 15);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, nbaSites.size()),
            16);
        Value fanoutAddress =
            fanoutEntries.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, fanoutName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, fanoutAddress, 17);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         fanoutEntries.size()),
                            18);
        Value actorRootsAddress =
            actorRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, actorRootsName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value,
                            actorRootsAddress, 19);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, actorRoots.size()),
            20);
        Value commitAddress =
            enableStaticNBA
                ? LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaCommitName)
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, commitAddress, 21);
        Value specializationFast =
            guardedSpecializationEnabled
                ? LLVM::AddressOfOp::create(
                      initializerBuilder, location, pointer,
                      "__obelisk_static_specialization_fast_v1")
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        return insertValue(initializerBuilder, location, value,
                           specializationFast, 22);
      });
  if (fullyStatic)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run_aot_nodes",
                             i32, {pointer, pointer, i32});
  else
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                             {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_snapshot_aot", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_root", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_roots", i32,
                           {pointer, i32, i32, pointer});
  return success();
}

LogicalResult makeSchedulerMain(ModuleOp module,
                                const NativeStateLayout &stateLayout,
                                bool useAOT) {
  if (module.lookupSymbol("main"))
    return success();
  sim::SimFuncOp root;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getSymName() == "__obelisk_root")
      root = function;
  });
  if (!root)
    return success();
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  builder.setInsertionPointToEnd(module.getBody());
  Location location = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  auto main = LLVM::LLVMFuncOp::create(
      builder, location, "main",
      LLVM::LLVMFunctionType::get(i32, {i32, pointer}, false));
  Block *entry = main.addEntryBlock(builder);
  Block *ready = new Block;
  Block *failed = new Block;
  main.getBody().push_back(ready);
  main.getBody().push_back(failed);
  builder.setInsertionPointToStart(entry);
  Value one = llvmConstant(builder, location, i64, 1);
  Value outContext =
      LLVM::AllocaOp::create(builder, location, pointer, pointer, one, 8);
  LLVM::StoreOp::create(builder, location,
                        LLVM::ZeroOp::create(builder, location, pointer),
                        outContext, 8);
  constexpr StringLiteral executionName = "__obelisk_execution_descriptor_v1";
  bool hasExecution = module.lookupSymbol(executionName) != nullptr;
  bool hasDesignBytecode = false;
  if (auto flags =
          module->getAttrOfType<IntegerAttr>("obelisk.execution.flags"))
    hasDesignBytecode = (flags.getValue().getZExtValue() &
                         OBELISK_RT_EXECUTION_HAS_BYTECODE) != 0;
  if (hasExecution) {
    Value execution =
        LLVM::AddressOfOp::create(builder, location, pointer, executionName);
    auto create = LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context, "obelisk_rt_v1_context_create_for_design"),
        ValueRange{execution, outContext});
    Value succeeded = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, create.getResult(),
        llvmConstant(builder, location, i32, 0));
    LLVM::CondBrOp::create(builder, location, succeeded, ready, failed,
                           create.getResult());
  } else {
    auto create = LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context, "obelisk_rt_v1_context_create"),
        outContext);
    Value succeeded = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, create.getResult(),
        llvmConstant(builder, location, i32, 0));
    LLVM::CondBrOp::create(builder, location, succeeded, ready, failed,
                           create.getResult());
  }
  failed->addArgument(i32, location);
  builder.setInsertionPointToStart(failed);
  LLVM::ReturnOp::create(builder, location, failed->getArgument(0));

  builder.setInsertionPointToStart(ready);
  Value runtimeContext =
      LLVM::LoadOp::create(builder, location, pointer, outContext, 8);
  Value configureStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_context_configure_argv"),
          ValueRange{runtimeContext, entry->getArgument(0),
                     entry->getArgument(1)})
          .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{runtimeContext, configureStatus});
  Value currentAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, runtimeContext, currentAddress, 8);
  SmallVector<sim::SimClassDeclOp> managedClasses;
  module.walk([&](sim::SimClassDeclOp declaration) {
    managedClasses.push_back(declaration);
  });
  llvm::sort(managedClasses,
             [](auto lhs, auto rhs) { return lhs.getId() < rhs.getId(); });
  for (sim::SimClassDeclOp declaration : managedClasses) {
    Value descriptor = LLVM::AddressOfOp::create(
        builder, location, pointer,
        managedClassDescriptorName(
            FlatSymbolRefAttr::get(context, declaration.getSymName())));
    Value status =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_class_register"),
            ValueRange{runtimeContext, descriptor})
            .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status});
  }
  SmallVector<LLVM::LLVMFuncOp> dpiThunks;
  module.walk([&](LLVM::LLVMFuncOp function) {
    if (function->hasAttr("obelisk.dpi.import_id"))
      dpiThunks.push_back(function);
  });
  llvm::sort(dpiThunks, [](LLVM::LLVMFuncOp lhs, LLVM::LLVMFuncOp rhs) {
    return lhs->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id").getInt() <
           rhs->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id").getInt();
  });
  for (LLVM::LLVMFuncOp thunk : dpiThunks) {
    auto importID = thunk->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id");
    auto abiHash = thunk->getAttrOfType<IntegerAttr>("obelisk.dpi.abi_hash");
    if (!abiHash)
      return thunk.emitError("DPI thunk is missing its ABI signature hash");
    Value callback = LLVM::AddressOfOp::create(builder, location, pointer,
                                               thunk.getSymName());
    Value userData = LLVM::ZeroOp::create(builder, location, pointer);
    Value status =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(
                context, "obelisk_rt_v1_context_register_import_signature"),
            ValueRange{runtimeContext,
                       llvmConstant(builder, location, i32,
                                    importID.getValue().getZExtValue()),
                       llvmConstant(builder, location, i64,
                                    abiHash.getValue().getZExtValue()),
                       callback, userData})
            .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status});
  }
  for (const NativeStateLayout::Bound &bound : stateLayout.bounds) {
    auto status = LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_native_state_register_static"),
        ValueRange{runtimeContext,
                   llvmConstant(builder, location, i32, bound.handleID),
                   llvmConstant(builder, location, i64, bound.offset),
                   llvmConstant(builder, location, i64, bound.width)});
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status.getResult()});
    for (uint64_t rootOffset : bound.managedRootOffsets) {
      if ((bound.offset + rootOffset) & 7)
        return module.emitError("managed static root is not byte aligned");
      Value state = LLVM::AddressOfOp::create(builder, location, pointer,
                                              "__obelisk_state_value");
      Value slot =
          byteGEP(builder, location, state, (bound.offset + rootOffset) / 8);
      Value rootStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context,
                                 "obelisk_rt_v1_gc_static_root_register"),
              ValueRange{runtimeContext, slot})
              .getResult();
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
          ValueRange{runtimeContext, rootStatus});
      if (hasDesignBytecode) {
        Value designRootStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(context,
                                   "obelisk_rt_v1_gc_design_root_register"),
                ValueRange{runtimeContext,
                           llvmConstant(builder, location, i64,
                                        bound.offset + rootOffset)})
                .getResult();
        LLVM::CallOp::create(
            builder, location, TypeRange{},
            SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
            ValueRange{runtimeContext, designRootStatus});
      }
    }
  }
  if (useAOT) {
    Value plan = LLVM::AddressOfOp::create(builder, location, pointer,
                                           "__obelisk_aot_schedule_plan_v1");
    Value installStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_install_aot"),
            ValueRange{runtimeContext, plan})
            .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, installStatus});
  }
  LLVM::CallOp::create(
      builder, location, TypeRange{i64},
      SymbolRefAttr::get(context, "__obelisk_root.__obelisk_spawn"),
      runtimeContext);
  auto run = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, useAOT ? "obelisk_rt_v1_scheduler_run_aot"
                                         : "obelisk_rt_v1_scheduler_run"),
      runtimeContext);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_context_destroy"),
      runtimeContext);
  LLVM::ReturnOp::create(builder, location, run.getResult());

  if (hasExecution)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_create_for_design",
                             i32, {pointer, pointer});
  else
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_create", i32,
                             {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_destroy", voidType,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_configure_argv", i32,
                           {pointer, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_register_static",
                           i32, {pointer, i32, i64, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_static_root_register", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_design_root_register", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_class_register", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_context_register_import_signature",
                           i32, {pointer, i32, i64, pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_fail", voidType,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_install_aot", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run_aot", i32,
                           {pointer});
  return success();
}

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
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_signal",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_signal_transition",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_static_transition",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_real_transition",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_specialization_guard",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       IntegerType::get(context, 32), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_specialization_guard",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event", LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event_after",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event_triggered",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_handle_offset",
      IntegerType::get(context, 64),
      {IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_nba", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_static_nba",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_claim", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_static_nba_packed",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_stage_wide",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_string_nba",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_managed_nba",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_fail",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_alloc", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_alloc_with_roots",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_retain",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_release",
                           IntegerType::get(context, 32),
                           {LLVM::LLVMPointerType::get(context),
                            IntegerType::get(context, 64),
                            IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_load_plane",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_store_plane",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_override", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_release_override",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_argument_ref_load", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_argument_ref_store", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  Type managedPointer = LLVM::LLVMPointerType::get(context);
  Type managedI32 = IntegerType::get(context, 32);
  Type managedI64 = IntegerType::get(context, 64);
  Type managedF64 = Float64Type::get(context);
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_current_lane",
                           managedPointer, {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_root_push", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_root_pop", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_gc_root_range_push", managedI32,
      {managedPointer, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_root_range_pop",
                           managedI32, {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_gc_managed_root_range_push", managedI32,
      {managedPointer, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_managed_root_range_pop",
                           managedI32, {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_safepoint", managedI32,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_allocate", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_reference_path_index_create", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer, managedI64,
       managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_size", managedI64,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_create_like",
                           managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_container_create_typed", managedI32,
      {managedPointer, managedI32, managedI64, managedI32, managedI32,
       managedI64, managedI64, managedI64, managedPointer, managedI64,
       managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_clone", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_delete", managedI32,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_queue_delete_index",
                           managedI32, {managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_queue_insert", managedI32,
                           {managedPointer, managedPointer, managedI64,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_bounded", managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_next", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_seed", managedI32,
                           {managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_create", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_covergroup_set_enabled",
                           managedI32,
                           {managedPointer, managedI64, managedI32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_covergroup_sample_enabled",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_bin_hit", managedI32,
      {managedPointer, managedI64, managedI32, managedI32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_sample", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_covergroup_instance_query",
                           managedI32,
                           {managedPointer, managedI64, managedPointer,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_type_query", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64, managedPointer,
       managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_container_read", managedI32,
      {managedPointer, managedI64, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_write", managedI32,
                           {managedPointer, managedPointer, managedI64,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_assoc_create_typed", managedI32,
      {managedPointer, managedI64, managedI32, managedI32, managedI64,
       managedI64, managedI64, managedPointer, managedI64, managedI32,
       managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_assoc_read_checked", managedI32,
      {managedPointer, managedPointer, managedPointer, managedI64,
       managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_assoc_write_checked", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer,
       managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assoc_exists", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assoc_delete", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assoc_set_default_checked",
                           managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedI64, managedPointer, managedI64});
  for (StringRef name :
       {"obelisk_rt_v1_assoc_first", "obelisk_rt_v1_assoc_last",
        "obelisk_rt_v1_assoc_next", "obelisk_rt_v1_assoc_prev"})
    getOrDeclareLLVMFunction(
        module, name, managedI32,
        {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_reference_path_assoc_create", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer,
       managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_shallow_copy", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_read", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_write", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_read_planes", managedI32,
      {managedPointer, managedI64, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_write_planes", managedI32,
      {managedPointer, managedI64, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_field_load",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_field_store",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_is_instance",
                           managedI32, {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_id", managedI64,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_cast", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_method_invoke", managedI32,
                           {managedPointer, managedPointer, managedI64,
                            managedI64, managedPointer, managedI32,
                            managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_weak_create", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_weak_get", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_weak_clear", managedI32,
                           {managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_create", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_from_packed",
                           managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_to_packed", managedI32,
      {managedI64, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_concat_many", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_repeat", managedI32,
      {managedPointer, managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_length", managedI64,
                           {managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_getc", managedI32,
                           {managedI64, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_putc", managedI32,
      {managedPointer, managedI64, managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_substr", managedI32,
      {managedPointer, managedI64, managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_compare", managedI32,
                           {managedI64, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_compare_insensitive",
                           managedI32, {managedI64, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_case_convert", managedI32,
      {managedPointer, managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_parse_integer",
                           managedI32,
                           {managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_parse_real",
                           managedI32, {managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_format_integer", managedI32,
      {managedPointer, managedI64, managedI32, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_format_real",
                           managedI32,
                           {managedPointer, managedF64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_file_open_string_mcd",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_file_open_string", managedI32,
      {managedPointer, managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_file_getline_string",
                           managedI32,
                           {managedPointer, managedPointer, managedI32,
                            managedPointer, managedPointer});
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

  // Consume the whole-design X/Z proof in the AOT path after suspension
  // threading has reached its final SSA shape. Signatures and canonical frames
  // remain two-plane ABI objects, but proven block arguments, call results,
  // and local producers expose a constant-zero unknown plane to LLVM.
  DenseSet<Value> nativeTwoStateValues;
  DenseSet<Operation *> nativeTwoStateOperations;
  WalkResult stateDomainsComputed = module.walk([&](sim::SimDesignOp design) {
    FailureOr<StateDomainAnalysis> stateDomains =
        StateDomainAnalysis::compute(design);
    if (failed(stateDomains))
      return WalkResult::interrupt();
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      if (function.isExternal())
        continue;
      for (Block &block : function.getBody()) {
        for (BlockArgument argument : block.getArguments())
          if (isa<sim::LogicType>(argument.getType()) &&
              stateDomains->isTwoState(argument))
            nativeTwoStateValues.insert(argument);
        for (Operation &operation : block)
          for (Value result : operation.getResults())
            if (isa<sim::LogicType>(result.getType()) &&
                stateDomains->isTwoState(result))
              nativeTwoStateValues.insert(result);
      }
    }
    return WalkResult::advance();
  });
  if (stateDomainsComputed.wasInterrupted())
    return failure();
  for (Value value : nativeTwoStateValues) {
    auto result = dyn_cast<OpResult>(value);
    if (!result || result.getOwner()->getNumResults() != 1)
      continue;
    nativeTwoStateOperations.insert(result.getOwner());
  }

  // Record the net driven by each operation before dialect conversion starts
  // rewriting function signatures and their block arguments.  Conversion
  // patterns should inspect stable operation metadata instead of chasing the
  // source SSA graph while it is being replaced.
  annotateStaticDriverNets(module, *stateLayout);

  SimulationToStandardTypeConverter packedConverter;
  addSimulationPackedAggregateTypeConversions(packedConverter);
  packedConverter.addConversion(
      [](sim::UnpackedArrayType type, SmallVectorImpl<Type> &results) {
        return convertNativeAggregateType(type, results);
      });
  packedConverter.addConversion(
      [](sim::UnpackedStructType type, SmallVectorImpl<Type> &results) {
        return convertNativeAggregateType(type, results);
      });
  packedConverter.addConversion(
      [](sim::UnpackedUnionType type, SmallVectorImpl<Type> &results) {
        return convertNativeAggregateType(type, results);
      });
  addSimulationToRuntimeTypeConversions(packedConverter);
  packedConverter.addConversion([context](Type type) -> std::optional<Type> {
    if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
            sim::ProcessType, sim::ControlType, sim::ObserverType,
            sim::CovergroupHandleType>(type))
      return IntegerType::get(context, 64);
    return std::nullopt;
  });
  packedConverter.addConversion([context](Type type) -> std::optional<Type> {
    if (sim::isManagedHandleType(type))
      return IntegerType::get(context, 64);
    return std::nullopt;
  });
  packedConverter.addConversion([context](sim::ArgumentRefType) -> Type {
    return IntegerType::get(context, 192);
  });
  packedConverter.addConversion(
      [context](sim::ManagedRefType, SmallVectorImpl<Type> &results) {
        results.push_back(IntegerType::get(context, 64));
        results.push_back(IntegerType::get(context, 64));
        return success();
      });
  ReferenceArgumentMap referenceArguments;
  WalkResult lifetimeInputs = module.walk([&](sim::SimFuncOp function) {
    if (function.getBody().empty())
      return WalkResult::advance();
    // Observer captures are borrowed from the persistent computed-wait
    // record. Unlike an ordinary direct call, invoking an observer does not
    // transfer one retained reference per argument, so its return must not
    // consume captured automatic state. The waiting activation owns that
    // state across suspension and releases it on resumption or cancellation.
    if (function.getEntryKind() == sim::EntryKind::Observer)
      return WalkResult::advance();
    unsigned physical = 0;
    for (BlockArgument argument : function.getBody().front().getArguments()) {
      SmallVector<Type> converted;
      if (failed(packedConverter.convertType(argument.getType(), converted)))
        return WalkResult::interrupt();
      if (isa<sim::RefType>(argument.getType())) {
        if (converted.size() != 1)
          return WalkResult::interrupt();
        referenceArguments[function.getOperation()].push_back(physical);
      }
      physical += converted.size();
    }
    return WalkResult::advance();
  });
  if (lifetimeInputs.wasInterrupted())
    return failure();
  // This is transaction-local metadata produced only by the AOT signature
  // pattern below. Never consume a same-named source attribute.
  module.walk([](sim::SimFuncOp function) {
    function->removeAttr(nativeTwoStateBlockUnknownsAttr);
  });
  RewritePatternSet packedPatterns(context);
  populateSimulationToStandardPatterns(packedConverter, packedPatterns,
                                       nativeTwoStateOperations);
  populateSimulationPackedAggregateViewPatterns(packedConverter,
                                                packedPatterns);
  populateSimulationToRuntimePatterns(packedConverter, packedPatterns);
  populateFunctionTypeConversionPatterns(packedPatterns, packedConverter,
                                         nativeTwoStateValues);
  populateAggregateToLLVMConversionPatterns(packedPatterns, packedConverter);
  populateControlToLLVMConversionPatterns(packedPatterns, packedConverter);
  populateEventToLLVMConversionPatterns(packedPatterns, packedConverter);
  populateSuspensionTypeConversionPatterns(packedPatterns, packedConverter);
  populateReferenceLifetimeToLLVMConversionPatterns(packedPatterns,
                                                    packedConverter);
  populateNativeHandleConversionPatterns(
      packedPatterns, packedConverter, stateLayout->storage, stateLayout->nets,
      stateLayout->drivers);
  populateSchedulerToLLVMConversionPatterns(packedPatterns, packedConverter);
  populateStateReadWriteToLLVMConversionPatterns(
      packedPatterns, packedConverter, stateLayout->bitCount,
      staticSpecialization && useAOT && aotEligibility.isFullyEligible()
          ? &*stateLayout
          : nullptr);
  populateOverrideToLLVMConversionPatterns(packedPatterns, packedConverter,
                                           stateLayout->bitCount);
  populateManagedToLLVMConversionPatterns(
      packedPatterns, packedConverter, dataLayout, stateLayout->bitCount);
  populateDriverToLLVMConversionPatterns(packedPatterns, packedConverter,
                                         *stateLayout);
  populateNBAToLLVMConversionPatterns(
      packedPatterns, packedConverter, stateLayout->bitCount,
      staticNBA ? &staticNBAPlan : nullptr, staticNBA, vpi.allowsWrite());
  ConversionTarget packedTarget(*context);
  packedTarget.addIllegalOp<
      sim::SimBytesConstantOp, sim::SimFinishOp, sim::SimStopOp,
      sim::SimFatalOp, sim::SimTerminationRequestedOp, sim::SimTimeNowOp,
      sim::SimDisplayOp, sim::SimFileOpenMCDOp, sim::SimFileOpenOp,
      sim::SimFileCloseOp, sim::SimFileFlushOp, sim::SimFileGetcOp,
      sim::SimFileUngetcOp, sim::SimFileGetlineOp, sim::SimFileReadPackedOp,
      sim::SimFileEofOp, sim::SimFileSeekOp, sim::SimFileTellOp,
      sim::SimFileRewindOp>();
  packedTarget.addIllegalOp<
      sim::SimContextStorageOp, sim::SimContextNetOp, sim::SimContextDriverOp,
      sim::SimContextEventOp, sim::SimRefAllocOp, sim::SimRefReleaseOwnerOp,
      sim::SimRefLoadOp, sim::SimRefStoreOp, sim::SimOverrideOp,
      sim::SimReleaseOverrideOp,
      sim::SimNetExtractOp, sim::SimRefExtractOp, sim::SimRefDynExtractOp,
      sim::SimRefSubelementOp, sim::SimRefArrayElementOp, sim::SimNetReadOp,
      sim::SimDriverDriveOp, sim::SimDriverExtractOp,
      sim::SimDriverDynExtractOp, sim::SimDriverSubelementOp,
      sim::SimDriverArrayElementOp, sim::SimNBAEnqueueOp,
      sim::SimEventTriggerOp, sim::SimEventTriggeredOp, sim::SimEventEqualOp,
      sim::SimDisableChildrenOp, sim::SimControlEnterOp, sim::SimControlLeaveOp,
      sim::SimControlDisableOp, sim::SimStaticOnceOp, sim::SimDeferredOnceOp,
      sim::SimMonitorRegisterOp, sim::SimMonitorControlOp,
      sim::SimMonitorCurrentOp, sim::SimBitsDynExtractOp, sim::SimClassNullOp,
      sim::SimCovergroupNullOp, sim::SimCovergroupCreateOp,
      sim::SimCovergroupSampleEnabledOp, sim::SimCovergroupBinHitOp,
      sim::SimCovergroupStartOp, sim::SimCovergroupStopOp,
      sim::SimCovergroupInstanceQueryOp, sim::SimCovergroupTypeQueryOp,
      sim::SimManagedNullOp, sim::SimManagedIsNullOp, sim::SimEventNullOp,
      sim::SimContainerSizeOp, sim::SimContainerCreateLikeOp,
      sim::SimContainerCreateOp, sim::SimContainerCloneOp,
      sim::SimContainerDeleteOp, sim::SimQueueDeleteOp, sim::SimQueueInsertOp,
      sim::SimContainerReadOp, sim::SimContainerWriteOp, sim::SimAssocCreateOp,
      sim::SimAssocReadOp, sim::SimAssocWriteOp, sim::SimAssocExistsOp,
      sim::SimAssocDeleteOp, sim::SimAssocSetDefaultOp, sim::SimAssocTraverseOp,
      sim::SimRandomNextOp, sim::SimRandomSeedOp, sim::SimRandomBoundedOp,
      sim::SimStringLiteralOp, sim::SimStringFromPackedOp,
      sim::SimStringToPackedOp, sim::SimStringConcatOp, sim::SimStringRepeatOp,
      sim::SimStringLengthOp, sim::SimStringGetcOp, sim::SimStringPutcOp,
      sim::SimStringSubstrOp, sim::SimStringCompareOp,
      sim::SimStringCaseConvertOp, sim::SimStringParseIntegerOp,
      sim::SimStringParseRealOp, sim::SimStringFormatIntegerOp,
      sim::SimStringFormatRealOp, sim::SimFileOpenStringMCDOp,
      sim::SimFileOpenStringOp, sim::SimFileGetlineStringOp,
      sim::SimClassAllocOp, sim::SimClassCopyOp, sim::SimClassIsInstanceOp,
      sim::SimClassIdOp, sim::SimClassCastOp, sim::SimClassFieldRefOp,
      sim::SimClassRootBindOp, sim::SimManagedLoadOp, sim::SimManagedStoreOp,
      sim::SimManagedNBAEnqueueOp, sim::SimReferencePathNBAEnqueueOp,
      sim::SimArgumentRefFromRefOp, sim::SimArgumentRefFromManagedOp,
      sim::SimReferencePathIndexOp, sim::SimReferencePathAssocOp,
      sim::SimArgumentRefFromPathOp, sim::SimArgumentRefLoadOp,
      sim::SimArgumentRefStoreOp, sim::SimClassDirectCallOp,
      sim::SimClassVirtualCallOp, sim::SimWeakCreateOp, sim::SimWeakGetOp,
      sim::SimWeakClearOp, sim::SimGCSafepointOp>();
  packedTarget
      .addIllegalOp<sim::SimAggregateDefaultOp, sim::SimAggregateConstructOp,
                    sim::SimAggregateExtractOp, sim::SimAggregateInsertOp,
                    sim::SimArrayDynExtractOp, sim::SimUnionConstructOp,
                    sim::SimUnionExtractOp, sim::SimUnionIsActiveOp>();
  packedTarget.addLegalDialect<runtime::ObeliskRuntimeDialect>();
  packedTarget.addLegalOp<sim::SimContextRuntimeOp, sim::SimStatusCheckOp>();
  packedTarget.addDynamicallyLegalOp<sim::SimFuncOp>(
      [&](sim::SimFuncOp function) {
        return packedConverter.isSignatureLegal(function.getFunctionType()) &&
               packedConverter.isLegal(&function.getBody());
      });
  packedTarget.addDynamicallyLegalOp<
      sim::SimCallOp, sim::SimDPICallOp, sim::SimSpawnOp, sim::SimReturnOp,
      sim::SimTaskCallOp, sim::SimObserverBindOp, sim::SimPackedFlattenOp,
      sim::SimPackedUnflattenOp, sim::SimSuspendDelayOp,
      sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
      sim::SimSuspendLevelOp, sim::SimSuspendAnyOp, sim::SimSuspendEventOp,
      sim::SimSuspendForeverOp, sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
      sim::SimSuspendChildrenOp, sim::SimSuspendObserveOp>(
      [&](Operation *operation) { return packedConverter.isLegal(operation); });
  packedTarget.addDynamicallyLegalDialect<
      sim::ObeliskSimulationDialect, arith::ArithDialect,
      cf::ControlFlowDialect, func::FuncDialect>([&](Operation *operation) {
    return hasNoLogic(operation) && packedConverter.isLegal(operation);
  });
  packedTarget.addDynamicallyLegalOp<cf::BranchOp, cf::CondBranchOp>(
      [&](Operation *operation) { return packedConverter.isLegal(operation); });
  packedTarget.addDynamicallyLegalOp<ModuleOp>(hasNoLogic);
  packedTarget.markUnknownOpDynamicallyLegal(hasNoLogic);
  if (failed(
          applyFullConversion(module, packedTarget, std::move(packedPatterns))))
    return failure();
  if (failed(materializeDPIThunks(module)))
    return failure();

  // Region signature conversion records the physical unknown-plane block
  // arguments that the whole-design proof made redundant. Replace them only
  // after dialect conversion has finished remapping every original logic use;
  // doing this inside the signature pattern would not update future one-to-N
  // operand adaptors owned by the conversion driver.
  WalkResult specializedBlockArguments =
      module.walk([&](sim::SimFuncOp function) {
        auto mappings = function->getAttrOfType<ArrayAttr>(
            nativeTwoStateBlockUnknownsAttr);
        if (!mappings)
          return WalkResult::advance();
        if (mappings.size() != function.getBody().getBlocks().size()) {
          function.emitOpError(
              "has invalid native two-state block-argument metadata");
          return WalkResult::interrupt();
        }
        OpBuilder builder(context);
        for (auto [block, mapping] :
             llvm::zip_equal(function.getBody(), mappings)) {
          auto indices = dyn_cast<DenseI64ArrayAttr>(mapping);
          if (!indices) {
            function.emitOpError(
                "has malformed native two-state block-argument metadata");
            return WalkResult::interrupt();
          }
          builder.setInsertionPointToStart(&block);
          for (int64_t index : indices.asArrayRef()) {
            if (index < 0 ||
                static_cast<uint64_t>(index) >= block.getNumArguments()) {
              function.emitOpError(
                  "has out-of-range native two-state block argument");
              return WalkResult::interrupt();
            }
            BlockArgument argument =
                block.getArgument(static_cast<unsigned>(index));
            auto type = dyn_cast<IntegerType>(argument.getType());
            if (!type) {
              function.emitOpError(
                  "has non-integer native two-state unknown plane");
              return WalkResult::interrupt();
            }
            Value zero = arith::ConstantOp::create(
                builder, function.getLoc(), type,
                builder.getIntegerAttr(type, APInt::getZero(type.getWidth())));
            argument.replaceAllUsesWith(zero);
          }
        }
        function->removeAttr(nativeTwoStateBlockUnknownsAttr);
        return WalkResult::advance();
      });
  if (specializedBlockArguments.wasInterrupted() ||
      failed(threadRuntimeStatuses(module)) ||
      failed(releaseNativeAutomaticState(module, referenceArguments)))
    return failure();
  if (failed(validateRuntimeToLLVMPreconditions(module, dataLayout)))
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
