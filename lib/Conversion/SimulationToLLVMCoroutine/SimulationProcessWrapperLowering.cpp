//===- SimulationProcessWrapperLowering.cpp - Native process wrappers -===//

#include "SimulationProcessWrapperLowering.h"
#include "SimulationProcessRuntimeABI.h"

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

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

} // namespace obelisk::detail

