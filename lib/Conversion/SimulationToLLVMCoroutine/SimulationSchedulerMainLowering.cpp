//===- SimulationSchedulerMainLowering.cpp - Native scheduler entry ----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {

// Reserved names for the final-simulation-time channel. The accessors are
// exported so a standalone host can read the value after main returns; the
// storage itself stays internal.
static constexpr llvm::StringLiteral kFinalTimeName = "__obelisk_final_time";
static constexpr llvm::StringLiteral kFinalTimeAccessorName =
    "obelisk_final_time";
static constexpr llvm::StringLiteral kTimePrecisionAccessorName =
    "obelisk_time_precision_fs";

LogicalResult makeSchedulerMain(ModuleOp module,
                                const NativeStateLayout &stateLayout,
                                bool useAOT, bool directEval) {
  if (module.lookupSymbol("main"))
    return success();
  sim::SimFuncOp root;
  bool multipleRoots = false;
  module.walk([&](sim::SimFuncOp function) {
    if (function.getEntryKind() != sim::EntryKind::RootInitializer)
      return;
    multipleRoots |= static_cast<bool>(root);
    if (!root)
      root = function;
  });
  if (multipleRoots)
    return module.emitError("design has multiple root processes");
  if (!root)
    return success();
  // The time precision travels as femtoseconds per tick, matching the design
  // attribute, so a host can render ticks without knowing anything else.
  int64_t precisionFs = 1;
  module.walk([&](sim::SimDesignOp design) {
    if (auto attr = design->getAttrOfType<IntegerAttr>("time_precision_fs"))
      precisionFs = attr.getInt();
  });
  std::string rootSpawnName = root.getSymName().str();
  rootSpawnName += ".__obelisk_spawn";
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
  builder.setInsertionPointToStart(entry);
  (void)directEval;
  Block *ready = new Block;
  Block *failed = new Block;
  main.getBody().push_back(ready);
  main.getBody().push_back(failed);
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
  bool requiresDynamicScanFeature =
      module->hasAttr("obelisk.feature.dynamic_scan");
  bool requiresContainerBitstreamFeature =
      module->hasAttr("obelisk.feature.container_bitstream");
  bool requiresDPIExportBytecodeFeature =
      module->hasAttr("obelisk.feature.dpi_export_bytecode");
  if (requiresDynamicScanFeature)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_dynamic_scan_link_anchor"),
        ValueRange{});
  if (requiresContainerBitstreamFeature)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_container_bitstream_link_anchor"),
        ValueRange{});
  if (requiresDPIExportBytecodeFeature)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_dpi_export_bytecode_link_anchor"),
        ValueRange{});
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
    for (const sim::ManagedHandleSlot &root : bound.managedRootSlots) {
      uint64_t rootOffset = root.bitOffset;
      if ((bound.offset + rootOffset) & 7)
        return module.emitError("managed static root is not byte aligned");
      Value state = LLVM::AddressOfOp::create(builder, location, pointer,
                                              "__obelisk_state_value");
      Value slot =
          byteGEP(builder, location, state, (bound.offset + rootOffset) / 8);
      SmallVector<Value> rootArguments{
          runtimeContext, slot,
          llvmConstant(builder, location, i32, root.kindMask)};
      Value rootStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(
                  context, "obelisk_rt_v1_gc_candidate_static_root_register"),
              rootArguments)
              .getResult();
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
          ValueRange{runtimeContext, rootStatus});
      if (hasDesignBytecode) {
        SmallVector<Value> designRootArguments{
            runtimeContext,
            llvmConstant(builder, location, i64, bound.offset + rootOffset),
            llvmConstant(builder, location, i32, root.kindMask)};
        Value designRootStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(
                    context, "obelisk_rt_v1_gc_design_candidate_root_register"),
                designRootArguments)
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
  LLVM::CallOp::create(builder, location, TypeRange{i64},
                       SymbolRefAttr::get(context, rootSpawnName),
                       runtimeContext);
  auto run = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, useAOT ? "obelisk_rt_v1_scheduler_run_aot"
                                         : "obelisk_rt_v1_scheduler_run"),
      runtimeContext);
  // Capture the design's own notion of elapsed time while the context that
  // holds it still exists. A host has no other channel: once main returns, the
  // context is gone and the module exposes nothing but its exports.
  auto finalTime = LLVM::CallOp::create(
      builder, location, TypeRange{i64},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_time"),
      runtimeContext);
  LLVM::StoreOp::create(
      builder, location, finalTime.getResult(),
      LLVM::AddressOfOp::create(builder, location, pointer, kFinalTimeName));
  // Nothing after this point can report: the context holding the diagnostic is
  // destroyed on the next line and the process exits with the status.
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_report_status"),
      ValueRange{runtimeContext, run.getResult()});
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_context_destroy"),
      runtimeContext);
  LLVM::ReturnOp::create(builder, location, run.getResult());

  // Storage for the captured time, plus the two accessors a host calls after
  // main has returned. The precision is a compile-time constant, but it has to
  // travel with the module: without it the raw tick count cannot be rendered
  // as a time.
  builder.setInsertionPointToEnd(module.getBody());
  LLVM::GlobalOp::create(builder, location, i64, /*isConstant=*/false,
                         LLVM::Linkage::Internal, kFinalTimeName,
                         builder.getI64IntegerAttr(0));
  auto defineAccessor = [&](StringRef name, function_ref<Value()> emitValue) {
    auto accessor = LLVM::LLVMFuncOp::create(
        builder, location, name, LLVM::LLVMFunctionType::get(i64, {}, false));
    OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(accessor.addEntryBlock(builder));
    LLVM::ReturnOp::create(builder, location, emitValue());
  };
  defineAccessor(kFinalTimeAccessorName, [&]() -> Value {
    Value address =
        LLVM::AddressOfOp::create(builder, location, pointer, kFinalTimeName);
    return LLVM::LoadOp::create(builder, location, i64, address);
  });
  defineAccessor(kTimePrecisionAccessorName, [&]() -> Value {
    return LLVM::ConstantOp::create(builder, location, i64,
                                    builder.getI64IntegerAttr(precisionFs));
  });
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_time", i64,
                           {pointer});

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
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_gc_candidate_static_root_register",
                           i32, {pointer, pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_design_root_register", i32,
                           {pointer, i64});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_gc_design_candidate_root_register",
                           i32, {pointer, i64, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_class_register", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_context_register_import_signature",
                           i32, {pointer, i32, i64, pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_fail", voidType,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_report_status",
                           voidType, {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_install_aot", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run_aot", i32,
                           {pointer});
  if (requiresDynamicScanFeature)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dynamic_scan_link_anchor",
                             voidType, {});
  if (requiresContainerBitstreamFeature)
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_container_bitstream_link_anchor", voidType, {});
  if (requiresDPIExportBytecodeFeature)
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_dpi_export_bytecode_link_anchor", voidType, {});
  return success();
}

} // namespace obelisk::detail
