//===- SimulationProcessWrapperLowering.cpp - Native process wrappers -===//

#include "SimulationProcessWrapperLowering.h"
#include "SimulationProcessRuntimeABI.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/SmallPtrSet.h"

using namespace mlir;

namespace obelisk::detail {

LogicalResult materializeEvalCheckpointHandoffGlobals(ModuleOp module) {
  OpBuilder builder(module.getContext());
  Location location = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(module.getContext());
  Type i32 = builder.getI32Type();
  auto ensureInteger = [&](StringRef name, IntegerAttr initial) {
    if (LLVM::GlobalOp global = module.lookupSymbol<LLVM::GlobalOp>(name))
      return global.getGlobalType() == i32;
    builder.setInsertionPointToStart(module.getBody());
    LLVM::GlobalOp::create(builder, location, i32, false,
                           LLVM::Linkage::Internal, name, initial, 4);
    return true;
  };
  auto ensurePointer = [&](StringRef name) {
    if (LLVM::GlobalOp global = module.lookupSymbol<LLVM::GlobalOp>(name))
      return global.getGlobalType() == pointer;
    builder.setInsertionPointToStart(module.getBody());
    auto global = LLVM::GlobalOp::create(builder, location, pointer, false,
                                         LLVM::Linkage::Internal, name,
                                         Attribute{}, 8);
    Block *initializer = new Block;
    global.getInitializerRegion().push_back(initializer);
    builder.setInsertionPointToStart(initializer);
    LLVM::ReturnOp::create(builder, location,
                           LLVM::ZeroOp::create(builder, location, pointer));
    return true;
  };
  if (!ensureInteger(evalCheckpointActorName,
                     builder.getI32IntegerAttr(UINT32_MAX)) ||
      !ensureInteger(evalCheckpointContinuationName,
                     builder.getI32IntegerAttr(0)) ||
      !ensurePointer(evalCheckpointCallbackName) ||
      !ensurePointer(evalCheckpointMutableStateName))
    return module.emitError("eval checkpoint handoff symbol has wrong type");
  return success();
}

void publishAction(OpBuilder &builder, Location location, Value instance,
                   uint32_t actionKind, uint32_t suspendKind,
                   uint32_t continuation, uint32_t flags, Value payload,
                   uint64_t auxiliary) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Value action =
      loadAt(builder, location, instance, kInstanceActionField, pointer, 0);
  storeAt(builder, location, action, kActionKindField,
          llvmConstant(builder, location, i32, actionKind), 4);
  storeAt(builder, location, action, kActionSuspendKindField,
          llvmConstant(builder, location, i32, suspendKind), 4);
  storeAt(builder, location, action, kActionContinuationField,
          llvmConstant(builder, location, i32, continuation), 4);
  storeAt(builder, location, action, kActionFlagsField,
          llvmConstant(builder, location, i32, flags), 4);
  storeAt(builder, location, action, kActionPayloadField, payload, 8);
  storeAt(builder, location, action, kActionAuxiliaryField,
          llvmConstant(builder, location, i64, auxiliary), 8);
}

LogicalResult makeNativeWrappers(ModuleOp module, LLVM::LLVMFuncOp ramp,
                                 StringRef baseName, bool directActivation) {
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
  copyNativePartition(ramp, requirements);
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
  copyNativePartition(ramp, execute);
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
      loadAt(builder, location, instance, kInstanceContextField, pointer, 0);
  Value currentContext = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, runtimeContext, currentContext, 8);
  if (directActivation) {
    cf::BranchOp::create(builder, location, start);
  } else {
    Value handle = loadAt(builder, location, instance,
                          kInstanceNativeHandleField, pointer, 0);
    Value bits = LLVM::PtrToIntOp::create(builder, location,
                                          builder.getI64Type(), handle);
    Value isNull = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, bits,
        llvmConstant(builder, location, builder.getI64Type(), 0));
    cf::CondBranchOp::create(builder, location, isNull, start, ValueRange{},
                             resume, ValueRange{});
    builder.setInsertionPointToStart(resume);
    // The start path joins here after publishing its newly created handle.
    Value resumeHandle = loadAt(builder, location, instance,
                                kInstanceNativeHandleField, pointer, 0);
    LLVM::CoroResumeOp::create(builder, location, resumeHandle);
    cf::BranchOp::create(builder, location, done);
  }
  if (directActivation)
    resume->erase();
  builder.setInsertionPointToStart(start);
  Value modeExecute = llvmConstant(builder, location, i32, 1);
  Value nullOut = LLVM::ZeroOp::create(builder, location, pointer);
  LLVM::CallOp::create(builder, location, TypeRange{}, SymbolRefAttr::get(ramp),
                       ValueRange{instance, modeExecute, nullOut, nullOut});
  cf::BranchOp::create(builder, location, directActivation ? done : resume);
  builder.setInsertionPointToStart(done);
  Value status =
      loadAt(builder, location, instance, kInstanceStatusField, i32, 4);
  LLVM::ReturnOp::create(builder, location, status);

  builder.setInsertionPointAfter(execute);
  auto destroy = LLVM::LLVMFuncOp::create(
      builder, location, makeName(".__obelisk_native_destroy"),
      LLVM::LLVMFunctionType::get(voidType, {pointer}, false));
  copyNativePartition(ramp, destroy);
  Block *destroyEntry = destroy.addEntryBlock(builder);
  if (directActivation) {
    builder.setInsertionPointToStart(destroyEntry);
    LLVM::ReturnOp::create(builder, location, ValueRange{});
    return success();
  }
  Block *destroyCall = new Block;
  Block *destroyDone = new Block;
  destroy.getBody().push_back(destroyCall);
  destroy.getBody().push_back(destroyDone);
  builder.setInsertionPointToStart(destroyEntry);
  Value destroyInstance = destroyEntry->getArgument(0);
  Value destroyHandle = loadAt(builder, location, destroyInstance,
                               kInstanceNativeHandleField, pointer, 0);
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
  storeAt(builder, location, destroyInstance, kInstanceNativeHandleField,
          LLVM::ZeroOp::create(builder, location, pointer), 0);
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
  copyNativePartition(body, requirements);
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
  copyNativePartition(body, execute);
  Block *executeEntry = execute.addEntryBlock(builder);
  builder.setInsertionPointToStart(executeEntry);
  Value instance = executeEntry->getArgument(0);
  Value runtimeContext =
      loadAt(builder, location, instance, kInstanceContextField, pointer, 0);
  Value currentContext = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, runtimeContext, currentContext, 8);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameField, pointer, 0);
  SmallVector<Value> arguments;
  size_t physicalArgument = 0;
  Block &bodyEntry = body.getBody().front();
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (!slot.hasValueStorage()) {
      arguments.push_back(loadAt(builder, location, instance,
                                 kInstanceContextField, pointer, 0));
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
  storeAt(builder, location, instance, kInstanceContinuationField,
          llvmConstant(builder, location, i32, 0), 4);
  publishAction(builder, location, instance, OBELISK_RT_FRAGMENT_TERMINATE,
                OBELISK_RT_SUSPEND_NONE, 0, OBELISK_RT_FRAGMENT_FLAGS_NONE,
                llvmConstant(builder, location, i64, 0), 0);
  LLVM::ReturnOp::create(builder, location, call.getResult(0));

  builder.setInsertionPointAfter(execute);
  auto destroy = LLVM::LLVMFuncOp::create(
      builder, location, (baseName + ".__obelisk_native_destroy").str(),
      LLVM::LLVMFunctionType::get(voidType, {pointer}, false));
  copyNativePartition(body, destroy);
  Block *destroyEntry = destroy.addEntryBlock(builder);
  builder.setInsertionPointToStart(destroyEntry);
  LLVM::ReturnOp::create(builder, location, ValueRange{});
  return success();
}

LogicalResult
makeDirectFragmentWrapper(ModuleOp module, sim::SimFuncOp body,
                          sim::SimFuncOp actor, StringRef wrapperName,
                          uint32_t actorSlot, uint32_t continuation,
                          const SimulationProcessFrameAnalysis &analysis) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToEnd(module.getBody());
  Location location = body.getLoc();
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();

  auto wrapper = LLVM::LLVMFuncOp::create(
      builder, location, wrapperName,
      LLVM::LLVMFunctionType::get(i32, {pointer}, false));
  copyNativePartition(body, wrapper);
  if (::obelisk::schedule::has<
          ::obelisk::schedule::Field::EvalInductiveTwoState>(body) ||
      ::obelisk::schedule::has<
          ::obelisk::schedule::Field::EvalSelectedTwoState>(body) ||
      ::obelisk::schedule::has<::obelisk::schedule::Field::EvalFourStateSource>(
          body))
    ::obelisk::schedule::set<schedule::Field::EvalTwoStateWrapper>(
        wrapper, builder.getUnitAttr());
  if (::obelisk::schedule::has<schedule::metadata::evalPathGuardedTwoState>(
          body))
    ::obelisk::schedule::set<schedule::metadata::evalPathGuardedTwoState>(
        wrapper, builder.getUnitAttr());
  if (::obelisk::schedule::has<
          schedule::metadata::evalPathGuardedKnownPreserving>(body))
    ::obelisk::schedule::set<
        schedule::metadata::evalPathGuardedKnownPreserving>(
        wrapper, builder.getUnitAttr());
  if (auto unsupportedOwner = ::obelisk::schedule::get<
          schedule::metadata::evalUnsupportedCheckpointOwner>(body))
    ::obelisk::schedule::set<
        schedule::metadata::evalUnsupportedCheckpointOwner>(wrapper,
                                                            unsupportedOwner);
  bool mayTerminate = false;
  llvm::SmallPtrSet<Operation *, 8> visited;
  auto design = body->getParentOfType<sim::SimDesignOp>();
  std::function<void(sim::SimFuncOp)> inspect = [&](sim::SimFuncOp function) {
    if (!function || !visited.insert(function.getOperation()).second)
      return;
    function.walk([&](Operation *operation) {
      // Direct-fragment materialization runs after Simulation-to-Runtime
      // conversion, so checkpoint-producing operations may already have
      // crossed the typed runtime dialect boundary.  Treat any such operation
      // conservatively: a generated hot owner must have a closed, runtime-free
      // call graph before it can bypass coordinator status handling.
      mayTerminate |= isa_and_nonnull<runtime::ObeliskRuntimeDialect>(
          operation->getDialect());
      mayTerminate |=
          isa<sim::SimFinishOp, sim::SimStopOp, sim::SimFatalOp,
              sim::SimProgramExitOp, sim::SimErrorOp,
              sim::SimTerminationRequestedOp, sim::SimStatusCheckOp,
              sim::SimDisplayOp, sim::SimFileOpenMCDOp, sim::SimFileOpenOp,
              sim::SimFileCloseOp, sim::SimFileFlushOp, sim::SimFileGetcOp,
              sim::SimFileUngetcOp, sim::SimFileGetlineOp,
              sim::SimFileReadPackedOp, sim::SimFileEofOp, sim::SimFileSeekOp,
              sim::SimFileTellOp, sim::SimFileRewindOp, sim::SimDumpOpenOp,
              sim::SimDumpOpenStringOp, sim::SimDumpTimescaleOp,
              sim::SimDumpVarsOp, sim::SimDumpAllOp, sim::SimDumpControlOp,
              sim::SimDumpLimitOp, sim::SimDumpFlushOp, sim::SimDumpPortsOp,
              sim::SimDumpPortsControlOp>(operation);
      if (auto call = dyn_cast<sim::SimCallOp>(operation))
        inspect(design.lookupSymbol<sim::SimFuncOp>(call.getCallee()));
    });
  };
  inspect(body);
  inspect(actor);
  if (mayTerminate) {
    ::obelisk::schedule::set<schedule::metadata::evalMayTerminate>(
        wrapper, builder.getUnitAttr());
    if (::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalInheritedTwoStateCheckpoint>(
            body) ||
        ::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalInheritedTwoStateCheckpoint>(actor))
      ::obelisk::schedule::set<schedule::metadata::evalCheckpointSafe>(
          wrapper, builder.getUnitAttr());
  }
  Block *entry = wrapper.addEntryBlock(builder);
  if (::obelisk::schedule::has<::obelisk::schedule::Field::EvalRawCaptures>(
          body)) {
    if (!mayTerminate)
      ::obelisk::schedule::set<schedule::metadata::evalInfallible>(
          wrapper, builder.getUnitAttr());
    wrapper->setAttr(
        "passthrough",
        builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
    builder.setInsertionPointToStart(entry);
    SmallVector<Value> arguments;
    for (Type input : body.getFunctionType().getInputs()) {
      Type converted = convertProcessType(input, context);
      arguments.push_back(
          isa<sim::ContextType>(input)
              ? entry->getArgument(0)
              : LLVM::PoisonOp::create(builder, location, converted)
                    .getResult());
    }
    bool returnsStatus = body.getFunctionType().getNumResults() != 0;
    body.walk([&](sim::SimStatusCheckOp) { returnsStatus = true; });
    auto call = schedule::NativeExecuteOp::create(
        builder, location, returnsStatus ? TypeRange{i32} : TypeRange{},
        SymbolRefAttr::get(
            body->getParentOfType<sim::SimDesignOp>().getSymNameAttr(),
            {FlatSymbolRefAttr::get(context, body.getSymName())}),
        arguments, returnsStatus ? builder.getUnitAttr() : UnitAttr{});
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalDirectCall>(
        call, builder.getUnitAttr());
    LLVM::ReturnOp::create(
        builder, location,
        returnsStatus ? call.getResult(0)
                      : llvmConstant(builder, location, i32, OBELISK_RT_OK));
    return success();
  }
  Block *invoke = new Block;
  Block *failed = new Block;
  wrapper.getBody().push_back(invoke);
  wrapper.getBody().push_back(failed);
  builder.setInsertionPointToStart(entry);
  Value instanceAddress = entryAlloca(builder, location, pointer, 1, 8);
  Value enterStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context,
                             "obelisk_rt_v1_scheduler_direct_fragment_enter"),
          ValueRange{entry->getArgument(0),
                     llvmConstant(builder, location, i32, actorSlot),
                     llvmConstant(builder, location, i32, continuation),
                     instanceAddress})
          .getResult();
  Value entered = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, enterStatus,
      llvmConstant(builder, location, i32, OBELISK_RT_OK));
  cf::CondBranchOp::create(builder, location, entered, invoke, ValueRange{},
                           failed, ValueRange{});

  builder.setInsertionPointToStart(failed);
  LLVM::ReturnOp::create(builder, location, enterStatus);

  builder.setInsertionPointToStart(invoke);
  Value instance =
      LLVM::LoadOp::create(builder, location, pointer, instanceAddress, 8);
  Value currentContext = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, entry->getArgument(0),
                        currentContext, 8);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameField, pointer, 0);
  SmallVector<Value> arguments;
  size_t physicalArgument = 0;
  Block &actorEntry = actor.getBody().front();
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (!slot.hasValueStorage()) {
      arguments.push_back(entry->getArgument(0));
      ++physicalArgument;
      continue;
    }
    if (physicalArgument >= actorEntry.getNumArguments())
      return actor.emitError("direct fragment capture layout is truncated");
    Type valueType = convertProcessType(
        actorEntry.getArgument(physicalArgument++).getType(), context);
    arguments.push_back(loadAt(builder, location, frame, slot.valueOffset,
                               valueType, slot.alignment));
    if (slot.hasSecondaryStorage()) {
      if (physicalArgument >= actorEntry.getNumArguments())
        return actor.emitError(
            "direct fragment secondary capture layout is truncated");
      Type secondaryType = convertProcessType(
          actorEntry.getArgument(physicalArgument++).getType(), context);
      arguments.push_back(loadAt(builder, location, frame,
                                 slot.getSecondaryOffset(), secondaryType,
                                 slot.alignment));
    }
  }
  if (physicalArgument != actorEntry.getNumArguments())
    return actor.emitError(
        "direct fragment capture layout disagrees with actor entry");
  if (arguments.size() != body.getFunctionType().getNumInputs()) {
    const ProcessSuspension *selected = nullptr;
    for (const ProcessSuspension &suspension : analysis.getSuspensions())
      if (suspension.continuationID == continuation) {
        selected = &suspension;
        break;
      }
    if (!selected)
      return actor.emitError("direct fragment continuation is unknown");
    physicalArgument = 0;
    for (const ProcessFrameValue &slot :
         analysis.getContinuationLayout(continuation)) {
      if (physicalArgument >= selected->continuation->getNumArguments())
        return actor.emitError(
            "direct fragment continuation layout is truncated");
      Type valueType = convertProcessType(
          selected->continuation->getArgument(physicalArgument++).getType(),
          context);
      arguments.push_back(loadAt(builder, location, frame, slot.valueOffset,
                                 valueType, slot.alignment));
      if (slot.hasSecondaryStorage()) {
        if (physicalArgument >= selected->continuation->getNumArguments())
          return actor.emitError(
              "direct fragment continuation secondary layout is truncated");
        Type secondaryType = convertProcessType(
            selected->continuation->getArgument(physicalArgument++).getType(),
            context);
        arguments.push_back(loadAt(builder, location, frame,
                                   slot.getSecondaryOffset(), secondaryType,
                                   slot.alignment));
      }
    }
    if (physicalArgument != selected->continuation->getNumArguments())
      return actor.emitError(
          "direct fragment continuation layout disagrees with actor entry");
  }
  if (arguments.size() != body.getFunctionType().getNumInputs())
    return actor.emitError(
        "direct fragment continuation layout disagrees with body entry");
  schedule::NativeExecuteOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(
          body->getParentOfType<sim::SimDesignOp>().getSymNameAttr(),
          {FlatSymbolRefAttr::get(context, body.getSymName())}),
      arguments, UnitAttr{});
  Value leaveStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context,
                             "obelisk_rt_v1_scheduler_direct_fragment_leave"),
          ValueRange{entry->getArgument(0),
                     llvmConstant(builder, location, i32, actorSlot)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, leaveStatus);
  return success();
}

LogicalResult makeRuntimeCheckpointWrapper(ModuleOp module,
                                           sim::SimFuncOp actor,
                                           StringRef wrapperName,
                                           uint32_t actorSlot,
                                           uint32_t continuation) {
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToEnd(module.getBody());
  Location location = actor.getLoc();
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  auto functionType = LLVM::LLVMFunctionType::get(i32, {pointer}, false);

  if (failed(materializeEvalCheckpointHandoffGlobals(module)))
    return failure();

  // This wrapper can survive a conservative late handoff from Eval to the
  // legacy AOT scheduler.  Declare its runtime dependency at the point where
  // the reference is created instead of relying on accepted-Eval plan
  // materialization to do so later.
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_execute_aot_actor",
                           i32, {pointer, i32});

  std::string callbackName = (Twine(wrapperName) + ".checkpoint").str();
  auto callback =
      LLVM::LLVMFuncOp::create(builder, location, callbackName, functionType);
  copyNativePartition(actor, callback);
  callback.setPrivate();
  Block *callbackEntry = callback.addEntryBlock(builder);
  builder.setInsertionPointToStart(callbackEntry);
  Value callbackStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context,
                             "obelisk_rt_v1_scheduler_execute_aot_actor"),
          ValueRange{callbackEntry->getArgument(0),
                     llvmConstant(builder, location, i32, actorSlot)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, callbackStatus);

  builder.setInsertionPointAfter(callback);
  auto wrapper =
      LLVM::LLVMFuncOp::create(builder, location, wrapperName, functionType);
  copyNativePartition(actor, wrapper);
  wrapper.setPrivate();
  // IEEE 1800-2017 Clauses 31.7 and 31.9.1 require the generic coordinator to
  // observe a primary publication and a transport monitor to register its
  // delayed commit before generated periodic execution can advance time.
  // Publish an exact cold checkpoint without placing process creation or
  // scheduler calls in the generated evaluator's hot closure.
  ::obelisk::schedule::set<schedule::metadata::evalMayTerminate>(
      wrapper, builder.getUnitAttr());
  ::obelisk::schedule::set<schedule::metadata::evalCheckpointSafe>(
      wrapper, builder.getUnitAttr());
  wrapper->setAttr("passthrough", builder.getArrayAttr(
                                      {builder.getStringAttr("alwaysinline")}));
  Block *entry = wrapper.addEntryBlock(builder);
  builder.setInsertionPointToStart(entry);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i32, actorSlot),
                        LLVM::AddressOfOp::create(builder, location, pointer,
                                                  evalCheckpointActorName),
                        4);
  LLVM::StoreOp::create(
      builder, location, llvmConstant(builder, location, i32, continuation),
      LLVM::AddressOfOp::create(builder, location, pointer,
                                evalCheckpointContinuationName),
      4);
  LLVM::StoreOp::create(
      builder, location,
      LLVM::AddressOfOp::create(builder, location, pointer, callbackName),
      LLVM::AddressOfOp::create(builder, location, pointer,
                                evalCheckpointCallbackName),
      8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32,
                                      OBELISK_RT_AOT_GENERATED_CHECKPOINT));
  return success();
}

} // namespace obelisk::detail
