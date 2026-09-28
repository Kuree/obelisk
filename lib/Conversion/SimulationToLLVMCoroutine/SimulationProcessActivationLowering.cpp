#include "obelisk/Dialect/Schedule/ScheduleFields.h"
//===- SimulationProcessActivationLowering.cpp - Activation helpers ---===//

#include "SimulationProcessActivationLowering.h"
#include "SimulationProcessRuntimeABI.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"

#include <cstddef>

using namespace mlir;

namespace obelisk::detail {
// Bootstrap helpers may still be nested in a simulation design. Match module
// lookup scope and retain the first symbol on a collision, leaving duplicate
// symbol diagnostics intact instead of allowing automatic renaming.
static void indexModuleSymbol(ModuleOp module, SymbolTable &symbols,
                              Operation *symbol) {
  if (symbol->getParentOp() == module &&
      !symbols.lookup(SymbolTable::getSymbolName(symbol)))
    symbols.insert(symbol);
}

LogicalResult
makeProcessActivationHelper(ModuleOp module, SymbolTable &symbols,
                            sim::SimFuncOp function,
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
  ArrayRef<int64_t> transferredReferences;
  if (auto references = function->getAttrOfType<DenseI64ArrayAttr>(
          nativeTransferredReferencesAttr))
    transferredReferences = references.asArrayRef();
  std::string checkedHelperName =
      (function.getSymName() + ".__obelisk_activate_checked").str();
  std::string helperName =
      (function.getSymName() + ".__obelisk_activate").str();
  if (symbols.lookup(checkedHelperName) || symbols.lookup(helperName))
    return success();
  builder.setInsertionPointAfter(function);
  SmallVector<Type> checkedArguments(arguments);
  checkedArguments.push_back(pointer);
  auto checkedHelper = LLVM::LLVMFuncOp::create(
      builder, location, checkedHelperName,
      LLVM::LLVMFunctionType::get(i32, checkedArguments, false),
      LLVM::Linkage::Internal);
  copyNativePartition(function, checkedHelper);
  indexModuleSymbol(module, symbols, checkedHelper);
  Block *entry = checkedHelper.addEntryBlock(builder);
  Block *created = new Block;
  Block *failed = new Block;
  checkedHelper.getBody().push_back(created);
  checkedHelper.getBody().push_back(failed);
  builder.setInsertionPointToStart(entry);
  Value one = llvmConstant(builder, location, i64, 1);
  Value outActivation = entry->getArguments().back();
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, 0), outActivation,
                        8);
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
              context, "obelisk_rt_v1_process_instance_create_for_context"),
          ValueRange{entry->getArgument(0), descriptor, outInstance})
          .getResult();
  Value succeeded =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq, status,
                            llvmConstant(builder, location, i32, 0));
  LLVM::CondBrOp::create(builder, location, succeeded, created, failed);

  builder.setInsertionPointToStart(failed);
  LLVM::ReturnOp::create(builder, location, status);

  builder.setInsertionPointToStart(created);
  Value instance =
      LLVM::LoadOp::create(builder, location, pointer, outInstance, 8);
  Value frame =
      loadAt(builder, location, instance, kInstanceFrameField, pointer, 0);
  size_t physicalArgument = 0;
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (!slot.hasValueStorage()) {
      ++physicalArgument;
      continue;
    }
    if (physicalArgument + 1 >= entry->getNumArguments())
      return checkedHelper.emitError(
          "activation capture layout has too few arguments");
    storeAt(builder, location, frame, slot.valueOffset,
            entry->getArgument(physicalArgument++), slot.alignment);
    if (slot.hasSecondaryStorage()) {
      if (physicalArgument + 1 >= entry->getNumArguments())
        return checkedHelper.emitError(
            "activation capture is missing its secondary value");
      storeAt(builder, location, frame, slot.getSecondaryOffset(),
              entry->getArgument(physicalArgument++), slot.alignment);
    }
  }
  if (physicalArgument + 1 != entry->getNumArguments())
    return checkedHelper.emitError(
        "activation capture layout has excess arguments");
  Value encoded = LLVM::PtrToIntOp::create(builder, location, i64, instance);
  LLVM::StoreOp::create(builder, location, encoded, outActivation, 8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, 0));

  builder.setInsertionPointAfter(checkedHelper);
  auto helper = LLVM::LLVMFuncOp::create(
      builder, location, helperName,
      LLVM::LLVMFunctionType::get(i64, arguments, false));
  copyNativePartition(function, helper);
  indexModuleSymbol(module, symbols, helper);
  Block *wrapperEntry = helper.addEntryBlock(builder);
  Block *wrapperSucceeded = new Block;
  Block *wrapperFailed = new Block;
  helper.getBody().push_back(wrapperSucceeded);
  helper.getBody().push_back(wrapperFailed);
  builder.setInsertionPointToStart(wrapperEntry);
  Value wrapperOne = llvmConstant(builder, location, i64, 1);
  Value wrapperOut =
      LLVM::AllocaOp::create(builder, location, pointer, i64, wrapperOne, 8);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, 0), wrapperOut, 8);
  SmallVector<Value> callArguments(wrapperEntry->getArguments());
  callArguments.push_back(wrapperOut);
  Value wrapperStatus =
      LLVM::CallOp::create(builder, location, TypeRange{i32},
                           SymbolRefAttr::get(context, checkedHelperName),
                           callArguments)
          .getResult();
  Value wrapperOK = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, wrapperStatus,
      llvmConstant(builder, location, i32, 0));
  LLVM::CondBrOp::create(builder, location, wrapperOK, wrapperSucceeded,
                         wrapperFailed);

  builder.setInsertionPointToStart(wrapperFailed);
  for (int64_t index : transferredReferences) {
    if (index < 0 ||
        static_cast<uint64_t>(index) >= wrapperEntry->getNumArguments())
      return helper.emitError("has an invalid transferred-reference index");
    LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context, "obelisk_rt_v1_native_state_release"),
        ValueRange{wrapperEntry->getArgument(0),
                   wrapperEntry->getArgument(static_cast<unsigned>(index)),
                   llvmConstant(builder, location, i32, 0)});
  }
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
      ValueRange{wrapperEntry->getArgument(0), wrapperStatus});
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i64, 0));

  builder.setInsertionPointToStart(wrapperSucceeded);
  LLVM::ReturnOp::create(
      builder, location,
      LLVM::LoadOp::create(builder, location, i64, wrapperOut, 8));
  return success();
}

LogicalResult
makeProcessSpawnHelper(ModuleOp module, SymbolTable &symbols,
                       sim::SimFuncOp function,
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
  if (symbols.lookup(helperName))
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
      LLVM::GlobalOp global = makeConstantGlobal(
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
      copyNativePartition(function, global);
      indexModuleSymbol(module, symbols, global);
    };
    makeArray(continuationName, 0);
    makeArray(rankName, 1);
  }
  if (!schedule.bytecodeContinuations.empty()) {
    auto arrayType =
        LLVM::LLVMArrayType::get(i32, schedule.bytecodeContinuations.size());
    LLVM::GlobalOp global = makeConstantGlobal(
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
    copyNativePartition(function, global);
    indexModuleSymbol(module, symbols, global);
  }
  uint32_t homeRegion = getRuntimeEventRegion(function.getHomeRegion());
  if (homeRegion == UINT32_MAX)
    return function.emitOpError("has no executable runtime home region");
  sim::EntryKind entryKind = function.getEntryKind();
  bool primeOnSpawn =
      ::obelisk::schedule::has<::obelisk::schedule::Field::PrimeOnSpawn>(
          function);
  if (primeOnSpawn &&
      (!function->hasAttr("internal") ||
       !::obelisk::schedule::has<::obelisk::schedule::Field::DetachedControls>(
           function) ||
       entryKind != sim::EntryKind::Fork))
    return function.emitError(
        "prime-on-spawn is reserved for internal detached waiters");
  bool startup = sim::isStartupEntryKind(entryKind) ||
                 (entryKind == sim::EntryKind::Initial &&
                  function.getHomeRegion() == sim::EventRegion::Active);
  bool prioritySignalResume = ::obelisk::schedule::has<
      ::obelisk::schedule::Field::PrioritySignalResume>(function);
  bool concurrentSignalObserver =
      ::obelisk::schedule::has<::obelisk::schedule::Field::ConcurrentCancel>(
          function) ||
      ::obelisk::schedule::has<::obelisk::schedule::Field::ConcurrentAbort>(
          function);
  if (prioritySignalResume &&
      (!function->hasAttr("internal") || !concurrentSignalObserver ||
       !::obelisk::schedule::has<::obelisk::schedule::Field::DetachedControls>(
           function) ||
       entryKind != sim::EntryKind::Fork ||
       function.getHomeRegion() != sim::EventRegion::Reactive))
    return function.emitError(
        "priority signal resume is reserved for internal concurrent "
        "cancellation or abort observers");
  uint32_t scheduleFlags =
      OBELISK_RT_SCHEDULE_HOME(homeRegion) |
      (entryKind == sim::EntryKind::Final ? OBELISK_RT_SCHEDULE_FINAL : 0) |
      (entryKind == sim::EntryKind::Initial ? OBELISK_RT_SCHEDULE_INITIAL : 0) |
      (startup ? OBELISK_RT_SCHEDULE_STARTUP : 0) |
      (::obelisk::schedule::has<::obelisk::schedule::Field::DetachedControls>(
           function)
           ? OBELISK_RT_SCHEDULE_DETACHED_CONTROLS
           : 0) |
      (prioritySignalResume ? OBELISK_RT_SCHEDULE_PRIORITY_SIGNAL : 0) |
      (entryKind == sim::EntryKind::RootInitializer ? OBELISK_RT_SCHEDULE_ROOT
                                                    : 0);
  // Entry captures form a prefix of the canonical frame. Marshal only that
  // prefix; never copy continuation lanes, waits, or native scratch.
  uint64_t captureSize = 0;
  SmallVector<int64_t> offsets;
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (!slot.hasValueStorage())
      continue;
    offsets.push_back(slot.valueOffset);
    captureSize = std::max(captureSize, slot.valueOffset + slot.storageSize);
    if (slot.hasSecondaryStorage()) {
      offsets.push_back(slot.getSecondaryOffset());
      captureSize =
          std::max(captureSize, slot.getSecondaryOffset() + slot.storageSize);
    }
  }
  auto owner =
      ::obelisk::schedule::get<::obelisk::schedule::Field::ProgramOwnerId>(
          function);
  uint32_t options = (primeOnSpawn ? OBELISK_RT_SPAWN_PRIME : 0) |
                     (owner ? OBELISK_RT_SPAWN_PROGRAM_OWNER : 0);
  auto planType = LLVM::LLVMStructType::getLiteral(
      context, {pointer, i64, pointer, pointer, pointer, i64, i32, i32, i32,
                i32, i32, i32});
  std::string planName =
      (function.getSymName() + ".__obelisk_spawn_plan").str();
  auto plan = makeConstantGlobal(
      module, location, planType, planName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &initializer) {
        Value value = LLVM::ZeroOp::create(initializer, location, planType);
        auto address = [&](unsigned field, StringRef name) {
          value = insertValue(
              initializer, location, value,
              LLVM::AddressOfOp::create(initializer, location, pointer, name),
              field);
        };
        auto integer = [&](unsigned field, Type type, uint64_t number) {
          value = insertValue(initializer, location, value,
                              llvmConstant(initializer, location, type, number),
                              field);
        };
        address(
            0, (function.getSymName() + ".__obelisk_process_descriptor").str());
        integer(1, i64, captureSize);
        if (!schedule.continuations.empty()) {
          address(2, continuationName);
          address(3, rankName);
        }
        if (!schedule.bytecodeContinuations.empty())
          address(4, bytecodeContinuationName);
        integer(5, i64, owner ? owner.getValue().getZExtValue() : 0);
        integer(6, i32, scheduleFlags);
        integer(7, i32, schedule.actorSlot.value_or(UINT32_MAX));
        integer(8, i32, schedule.initialRank);
        integer(9, i32, schedule.continuations.size());
        integer(10, i32, schedule.bytecodeContinuations.size());
        integer(11, i32, options);
        return value;
      });
  copyNativePartition(function, plan);
  indexModuleSymbol(module, symbols, plan);

  builder.setInsertionPointAfter(function);
  auto helper = LLVM::LLVMFuncOp::create(
      builder, location, helperName,
      LLVM::LLVMFunctionType::get(i64, arguments, false));
  copyNativePartition(function, helper);
  // Retain the layout for root-spawn table materialization. Dynamic callers
  // use the typed marshalling body below.
  helper->setAttr("obelisk.spawn_capture_offsets",
                  builder.getDenseI64ArrayAttr(offsets));
  helper->setAttr("obelisk.spawn_capture_size",
                  builder.getI64IntegerAttr(captureSize));
  indexModuleSymbol(module, symbols, helper);
  Block *entry = helper.addEntryBlock(builder);
  builder.setInsertionPointToStart(entry);
  Value captures = LLVM::ZeroOp::create(builder, location, pointer);
  if (captureSize) {
    Type i8 = builder.getI8Type();
    captures = LLVM::AllocaOp::create(
        builder, location, pointer, i8,
        llvmConstant(builder, location, i64, captureSize),
        analysis.getFrameAlignment());
    LLVM::MemsetOp::create(
        builder, location, captures, llvmConstant(builder, location, i8, 0),
        llvmConstant(builder, location, i64, captureSize), false);
  }
  size_t physicalArgument = 0;
  for (const ProcessFrameValue &slot : analysis.getEntryCaptureLayout()) {
    if (!slot.hasValueStorage()) {
      ++physicalArgument;
      continue;
    }
    if (physicalArgument >= entry->getNumArguments())
      return helper.emitError("spawn capture layout has too few arguments");
    storeAt(builder, location, captures, slot.valueOffset,
            entry->getArgument(physicalArgument++), slot.alignment);
    if (slot.hasSecondaryStorage()) {
      if (physicalArgument >= entry->getNumArguments())
        return helper.emitError("spawn capture is missing its secondary value");
      storeAt(builder, location, captures, slot.getSecondaryOffset(),
              entry->getArgument(physicalArgument++), slot.alignment);
    }
  }
  if (physicalArgument != entry->getNumArguments())
    return helper.emitError("spawn capture layout has excess arguments");
  Value planAddress =
      LLVM::AddressOfOp::create(builder, location, pointer, planName);
  Value token = LLVM::CallOp::create(
                    builder, location, TypeRange{i64},
                    SymbolRefAttr::get(context, "obelisk_rt_v1_process_spawn"),
                    ValueRange{entry->getArgument(0), planAddress, captures})
                    .getResult();
  LLVM::ReturnOp::create(builder, location, token);
  return success();
}

void declareProcessSpawnRuntimeABI(ModuleOp module) {
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();

  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_spawn", i64,
                           {pointer, pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_spawn_batch",
                           LLVM::LLVMVoidType::get(context),
                           {pointer, pointer, i32});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_process_instance_create_for_context",
                           i32, {pointer, pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_add_planned", i32,
                           {pointer, pointer, i32, i32, pointer, pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_prime", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_program_register",
                           i32, {pointer, i64, i64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_add_aot", i32,
      {pointer, pointer, i32, i32, i32, pointer, pointer, i32, pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_process_token", i64,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_current", i64,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_status", i32,
                           {pointer, i64, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_random_get", i32,
                           {pointer, i64, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_random_set", i32,
                           {pointer, i64, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_control", i32,
                           {pointer, i64, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_fail",
                           LLVM::LLVMVoidType::get(context), {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_disable_children",
                           i32, {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_enter", i32,
                           {pointer, i64, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_boundary", i32,
                           {pointer, i64, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_leave", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_disable", i32,
                           {pointer, i64, i64, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_control_escape_pending", i32,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_once", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_deferred_once", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_deferred_enqueue", i64,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_deferred_enqueue_for_assertion", i64,
                           {pointer, i64, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_deferred_mature", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assertion_control", i32,
                           {pointer, i32, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assertion_enabled", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assertion_action_state", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assertion_kill_epoch", i64,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_monitor_register_logical",
                           i32, {pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_monitor_control", i32,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_monitor_current", i32,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_process_instance_destroy",
                           i32, {pointer});
}

} // namespace obelisk::detail
