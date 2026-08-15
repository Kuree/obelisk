//===- SimulationProcessWrapperLowering.cpp - Native process wrappers -===//

#include "SimulationProcessWrapperLowering.h"
#include "SimulationProcessRuntimeABI.h"

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/SmallPtrSet.h"

using namespace mlir;

namespace obelisk::detail {

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

LogicalResult makeDirectFragmentWrapper(
    ModuleOp module, sim::SimFuncOp body, sim::SimFuncOp actor,
    StringRef wrapperName, uint32_t actorSlot, uint32_t continuation,
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
  if (body->hasAttr("obelisk.eval.inductive_two_state") ||
      body->hasAttr("obelisk.eval.selected_two_state") ||
      body->hasAttr("obelisk.eval.four_state_source"))
    wrapper->setAttr(sim::metadata::evalTwoStateVariant, builder.getUnitAttr());
  if (body->hasAttr(sim::metadata::evalPathGuardedTwoState))
    wrapper->setAttr(sim::metadata::evalPathGuardedTwoState,
                     builder.getUnitAttr());
  if (body->hasAttr(sim::metadata::evalPathGuardedKnownPreserving))
    wrapper->setAttr(sim::metadata::evalPathGuardedKnownPreserving,
                     builder.getUnitAttr());
  if (auto unsupportedOwner = body->getAttrOfType<StringAttr>(
          sim::metadata::evalUnsupportedCheckpointOwner))
    wrapper->setAttr(sim::metadata::evalUnsupportedCheckpointOwner,
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
      mayTerminate |=
          operation->getName().getDialectNamespace() == "obelisk_rt";
      mayTerminate |=
          isa<sim::SimFinishOp, sim::SimStopOp, sim::SimFatalOp,
              sim::SimErrorOp, sim::SimTerminationRequestedOp,
              sim::SimStatusCheckOp, sim::SimDisplayOp, sim::SimFileOpenMCDOp,
              sim::SimFileOpenOp, sim::SimFileCloseOp, sim::SimFileFlushOp,
              sim::SimFileGetcOp, sim::SimFileUngetcOp, sim::SimFileGetlineOp,
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
    wrapper->setAttr(sim::metadata::evalMayTerminate, builder.getUnitAttr());
    if (body->hasAttr("obelisk.eval.inherited_two_state_checkpoint") ||
        actor->hasAttr("obelisk.eval.inherited_two_state_checkpoint"))
      wrapper->setAttr(sim::metadata::evalCheckpointSafe,
                       builder.getUnitAttr());
  }
  Block *entry = wrapper.addEntryBlock(builder);
  if (body->hasAttr("obelisk.eval.raw_captures")) {
    if (!mayTerminate)
      wrapper->setAttr(sim::metadata::evalInfallible, builder.getUnitAttr());
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
    bool returnsStatus = false;
    body.walk([&](sim::SimStatusCheckOp) { returnsStatus = true; });
    auto call = func::CallOp::create(
        builder, location, body.getSymName(),
        returnsStatus ? TypeRange{i32} : TypeRange{}, arguments);
    call->setAttr("obelisk.eval.direct_call", builder.getUnitAttr());
    LLVM::ReturnOp::create(
        builder, location,
        returnsStatus
            ? call.getResult(0)
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
          SymbolRefAttr::get(
              context, "obelisk_rt_v1_scheduler_direct_fragment_enter"),
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
  Value currentContext = LLVM::AddressOfOp::create(
      builder, location, pointer, "__obelisk_current_context");
  LLVM::StoreOp::create(builder, location, entry->getArgument(0),
                        currentContext, 8);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameOffset, pointer, 8);
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
    Type valueType =
        convertProcessType(actorEntry.getArgument(physicalArgument++).getType(),
                           context);
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
  func::CallOp::create(builder, location, body.getSymName(), TypeRange{},
                       arguments);
  Value leaveStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(
              context, "obelisk_rt_v1_scheduler_direct_fragment_leave"),
          ValueRange{entry->getArgument(0),
                     llvmConstant(builder, location, i32, actorSlot)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, leaveStatus);
  return success();
}

} // namespace obelisk::detail
