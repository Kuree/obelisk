//===- SimulationProcessActivationLowering.cpp - Activation helpers ---===//

#include "SimulationProcessActivationLowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"

#include <cstddef>

using namespace mlir;

namespace obelisk::detail {
namespace {

constexpr uint64_t kInstanceFrameOffset =
    offsetof(obelisk_rt_process_instance_v1, frame);

} // namespace

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
          SymbolRefAttr::get(
              context,
              "obelisk_rt_v1_process_instance_create_for_context"),
          ValueRange{entry->getArgument(0), descriptor, outInstance})
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
    if (!slot.hasValueStorage()) {
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
      SymbolRefAttr::get(context,
                         "obelisk_rt_v1_process_instance_create_for_context"),
      ValueRange{entry->getArgument(0), descriptor, outInstance});
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
    if (!slot.hasValueStorage()) {
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
  sim::EntryKind entryKind = function.getEntryKind();
  bool startup = sim::isStartupEntryKind(entryKind) ||
                 (entryKind == sim::EntryKind::Initial &&
                  function.getHomeRegion() == sim::EventRegion::Active);
  uint32_t scheduleFlags = OBELISK_RT_SCHEDULE_HOME(homeRegion) |
                           (entryKind == sim::EntryKind::Final
                                ? OBELISK_RT_SCHEDULE_FINAL
                                : 0) |
                           (entryKind == sim::EntryKind::Initial
                                ? OBELISK_RT_SCHEDULE_INITIAL
                                : 0) |
                           (startup
                                ? OBELISK_RT_SCHEDULE_STARTUP
                                : 0) |
                           (function->hasAttr("obelisk_sim.detached_controls")
                                ? OBELISK_RT_SCHEDULE_DETACHED_CONTROLS
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

  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_process_instance_create_for_context", i32,
      {pointer, pointer, pointer});
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
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_deferred_enqueue", i64,
                           {pointer, i64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_deferred_enqueue_for_assertion", i64,
      {pointer, i64, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_deferred_mature", i32,
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

} // namespace obelisk::detail
