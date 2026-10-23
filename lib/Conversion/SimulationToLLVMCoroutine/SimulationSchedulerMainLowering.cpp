//===- SimulationSchedulerMainLowering.cpp - Native scheduler entry ----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"

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
                                bool useAOT, bool directEval,
                                bool hasObserver) {
  if (module.lookupSymbol("main"))
    return success();
  sim::SimFuncOp root;
  bool multipleRoots = false;
  ::obelisk::detail::walkNativeFunctions<sim::SimFuncOp>(
      module, [&](sim::SimFuncOp function) {
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
  bool hasCoverage = runtime::has<runtime::CoverageSchemaBlob>(module);
  // A functional schema is also required for covergroup methods used directly
  // by the language.  Its presence therefore does not imply that the user
  // requested persistent coverage output.  The preparation pipeline adds the
  // metrics attribute for explicit --coverage selection and for coverage
  // database system calls, which are the two opt-in persistence paths.
  uint32_t coveragePersistenceMask = 0;
  if (auto metrics = cov::get<cov::Metrics>(module)) {
    for (Attribute attribute : metrics) {
      auto metric = dyn_cast<StringAttr>(attribute);
      if (!metric)
        continue;
      coveragePersistenceMask |=
          llvm::StringSwitch<uint32_t>(metric.getValue())
              .Case("line", OBELISK_RT_COVERAGE_PERSIST_LINE)
              .Case("toggle", OBELISK_RT_COVERAGE_PERSIST_TOGGLE)
              .Case("functional", OBELISK_RT_COVERAGE_PERSIST_FUNCTIONAL)
              .Case("assertion", OBELISK_RT_COVERAGE_PERSIST_ASSERTION)
              .Case("fsm", OBELISK_RT_COVERAGE_PERSIST_FSM)
              .Default(0);
    }
  }
  bool shouldDumpCoverage = hasCoverage && coveragePersistenceMask != 0;
  bool hasDesignBytecode = false;
  if (runtime::has<runtime::ExecutionFlagSet>(module)) {
    runtime::ExecutionFlags flags = runtime::getExecutionFlags(module);
    hasDesignBytecode = runtime::bitEnumContainsAll(
        flags, runtime::ExecutionFlags::HasBytecode);
  }
  // Observer capability is a source-level scheduling fact retained by the
  // plan even after evaluator functions acquire their native signatures.
  // IEEE 1800-2023 4.6(a), 9.4.2: observers may require the canonical
  // source-order publication image even after generated stores run ahead.
  // Keep that image for observation-capable designs. A closed eval design
  // without those readers can let all tiers address the generated storage.
  // A DPI export runs in its importing caller's call chain (IEEE 1800-2023
  // 35.5.3) and must see that caller's stores, which the shared storage
  // already holds; it needs no separate image.
  bool sharedNativeState = hasExecution && useAOT && directEval &&
                           stateLayout.bitCount && !hasObserver &&
                           !hasCoverage;
  uint64_t linePointCount = 0;
  uint64_t toggleBitCount = 0;
  if (hasCoverage) {
    if (auto count = cov::get<cov::LinePointCount>(module))
      linePointCount = count.getValue().getZExtValue();
    if (auto count = cov::get<cov::ToggleBitCount>(module))
      toggleBitCount = count.getValue().getZExtValue();
  }
  bool requiresNativeStateSync =
      stateLayout.bitCount && (hasDesignBytecode || toggleBitCount != 0 ||
                               stateLayout.directContinuous);
  bool bindStateSpecialization =
      (requiresNativeStateSync || sharedNativeState) &&
      !stateLayout.guardedHandles.empty();
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
  auto initializers =
      module.lookupSymbol<LLVM::GlobalOp>("__obelisk_state_initializers_v1");
  if (!initializers)
    return module.emitError("native state initializers are missing");
  uint64_t fillCount =
      cast<LLVM::LLVMArrayType>(initializers.getGlobalType()).getNumElements() /
      4;
  auto initialized = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, "obelisk_rt_v1_native_state_initialize"),
      ValueRange{runtimeContext,
                 LLVM::AddressOfOp::create(builder, location, pointer,
                                           "__obelisk_state_value"),
                 LLVM::AddressOfOp::create(builder, location, pointer,
                                           "__obelisk_state_unknown"),
                 llvmConstant(builder, location, i64, stateLayout.bitCount),
                 LLVM::AddressOfOp::create(builder, location, pointer,
                                           initializers.getSymName()),
                 llvmConstant(builder, location, i64, fillCount)});
  Block *initializedReady = new Block, *initializeFailed = new Block;
  main.getBody().push_back(initializedReady);
  main.getBody().push_back(initializeFailed);
  LLVM::CondBrOp::create(
      builder, location,
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                            initialized.getResult(),
                            llvmConstant(builder, location, i32, 0)),
      initializedReady, initializeFailed);
  builder.setInsertionPointToStart(initializeFailed);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_context_destroy"),
      runtimeContext);
  LLVM::ReturnOp::create(builder, location, initialized.getResult());
  builder.setInsertionPointToStart(initializedReady);
  if (sharedNativeState) {
    Value value = LLVM::AddressOfOp::create(builder, location, pointer,
                                            "__obelisk_state_value");
    Value unknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                              "__obelisk_state_unknown");
    Value status = LLVM::CallOp::create(
                       builder, location, TypeRange{i32},
                       SymbolRefAttr::get(
                           context, "obelisk_rt_v1_native_state_bind_shared"),
                       ValueRange{runtimeContext, value, unknown,
                                  llvmConstant(builder, location, i64,
                                               stateLayout.bitCount)})
                       .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status});
  }
  runtime::Features features = runtime::getFeatures(module);
  auto needs = [&](runtime::Features feature) {
    return runtime::bitEnumContainsAll(features, feature);
  };
  bool requiresDynamicScanFeature = needs(runtime::Features::DynamicScan);
  bool requiresContainerBitstreamFeature =
      needs(runtime::Features::ContainerBitstream);
  bool requiresRecursiveBitstreamFeature =
      needs(runtime::Features::RecursiveBitstream);
  bool requiresClassBitstreamFeature = needs(runtime::Features::ClassBitstream);
  bool requiresClassBitstreamBytecodeFeature =
      needs(runtime::Features::ClassBitstreamBytecode);
  bool requiresDPIExportBytecodeFeature =
      needs(runtime::Features::DPIExportBytecode);
  bool requiresDPIImportBytecodeFeature =
      needs(runtime::Features::DPIImportBytecode);
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
  if (requiresRecursiveBitstreamFeature)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_recursive_bitstream_link_anchor"),
        ValueRange{});
  if (requiresClassBitstreamFeature)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_class_bitstream_link_anchor"),
        ValueRange{});
  if (requiresClassBitstreamBytecodeFeature)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(
            context, "obelisk_rt_v1_class_bitstream_bytecode_link_anchor"),
        ValueRange{});
  if (requiresDPIExportBytecodeFeature)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_dpi_export_bytecode_link_anchor"),
        ValueRange{});
  if (requiresDPIImportBytecodeFeature)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_dpi_import_bytecode_link_anchor"),
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
  // Element defaults must precede the first container of their type, which
  // the root initializer may already create.
  if (ArrayAttr defaults = native::get<native::ElementDefaults>(module))
    for (auto entry : defaults.getAsRange<native::ElementDefaultAttr>()) {
      ArrayRef<int8_t> image = entry.getImage().asArrayRef();
      std::string name =
          "__obelisk_element_default_" + std::to_string(entry.getTypeId());
      if (!module.lookupSymbol(name)) {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(module.getBody());
        LLVM::GlobalOp::create(
            builder, location,
            LLVM::LLVMArrayType::get(builder.getI8Type(), image.size()),
            /*isConstant=*/true, LLVM::Linkage::Internal, name,
            builder.getStringAttr(StringRef(
                reinterpret_cast<const char *>(image.data()), image.size())),
            1);
      }
      Value status =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context,
                                 "obelisk_rt_v1_element_type_define_default"),
              ValueRange{
                  runtimeContext,
                  LLVM::ConstantOp::create(
                      builder, location, i64,
                      builder.getI64IntegerAttr(
                          static_cast<int64_t>(entry.getTypeId()))),
                  LLVM::AddressOfOp::create(builder, location, pointer, name),
                  LLVM::ConstantOp::create(
                      builder, location, i64,
                      builder.getI64IntegerAttr(
                          static_cast<int64_t>(image.size())))})
              .getResult();
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
          ValueRange{runtimeContext, status});
    }
  if (requiresClassBitstreamFeature) {
    Value status = LLVM::CallOp::create(
                       builder, location, TypeRange{i32},
                       SymbolRefAttr::get(
                           context, "obelisk_rt_v1_class_bitstream_finalize"),
                       ValueRange{runtimeContext})
                       .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status});
  }
  SmallVector<LLVM::LLVMFuncOp> dpiThunks;
  ::obelisk::detail::walkNativeFunctions<LLVM::LLVMFuncOp>(
      module, [&](LLVM::LLVMFuncOp function) {
        if (dpi::has<dpi::ImportId>(function))
          dpiThunks.push_back(function);
      });
  llvm::sort(dpiThunks, [](LLVM::LLVMFuncOp lhs, LLVM::LLVMFuncOp rhs) {
    return dpi::get<dpi::ImportId>(lhs).getInt() <
           dpi::get<dpi::ImportId>(rhs).getInt();
  });
  for (LLVM::LLVMFuncOp thunk : dpiThunks) {
    auto importID = dpi::get<dpi::ImportId>(thunk);
    auto abiHash = dpi::get<dpi::ABIHash>(thunk);
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
  auto emitBoundRegistration = [&](OpBuilder &builder, Value runtimeContext,
                                   const NativeStateLayout::Bound &bound)
      -> LogicalResult {
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
      if (hasDesignBytecode && !sharedNativeState) {
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
    return success();
  };
  // Registration is ordered initialization, not a scheduling boundary. Keep
  // large designs from producing a single basic block with tens of thousands
  // of calls (and quadratic register-allocation work). The helpers preserve
  // each bound's registration, managed roots and status reporting in place.
  constexpr size_t registrationBatchSize = 256;
  if (stateLayout.bounds.size() <= registrationBatchSize) {
    for (const NativeStateLayout::Bound &bound : stateLayout.bounds)
      if (mlir::failed(emitBoundRegistration(builder, runtimeContext, bound)))
        return failure();
  } else {
    // Large designs register from two constant tables in one loop: bounds
    // {handle, end of its roots, bit offset, width} and managed roots {byte
    // offset, kind mask}. The order of runtime calls and their status checks
    // is the same as the unrolled form, without a call sequence per bound.
    auto boundType =
        LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, i64});
    auto rootType = LLVM::LLVMStructType::getLiteral(context, {i64, i32, i32});
    SmallVector<Attribute> boundRows, rootRows;
    boundRows.reserve(stateLayout.bounds.size());
    for (const NativeStateLayout::Bound &bound : stateLayout.bounds) {
      for (const sim::ManagedHandleSlot &root : bound.managedRootSlots) {
        uint64_t bit = bound.offset + root.bitOffset;
        if (bit & 7)
          return module.emitError("managed static root is not byte aligned");
        rootRows.push_back(
            integerStructAttr(rootType, {bit / 8, root.kindMask, 0}));
      }
      boundRows.push_back(
          integerStructAttr(boundType, {bound.handleID, rootRows.size(),
                                        bound.offset, bound.width}));
    }
    constexpr StringLiteral boundsName = "__obelisk_static_state_bounds_v1";
    constexpr StringLiteral rootsName = "__obelisk_static_state_roots_v1";
    makeConstantGlobal(module, location,
                       LLVM::LLVMArrayType::get(boundType, boundRows.size()),
                       boundsName, LLVM::Linkage::Internal, 8,
                       ArrayAttr::get(context, boundRows));
    makeConstantGlobal(module, location,
                       LLVM::LLVMArrayType::get(rootType, rootRows.size()),
                       rootsName, LLVM::Linkage::Internal, 8,
                       ArrayAttr::get(context, rootRows));
    LLVM::LLVMFuncOp helper;
    {
      OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToEnd(module.getBody());
      helper = LLVM::LLVMFuncOp::create(
          builder, location, "__obelisk_register_static_state",
          LLVM::LLVMFunctionType::get(voidType, {pointer}, false),
          LLVM::Linkage::Internal);
      helper.setNoInline(true);
      Block *entry = helper.addEntryBlock(builder);
      Value helperContext = entry->getArgument(0);
      Region &body = helper.getBody();
      auto *boundHeader = new Block, *boundBody = new Block,
           *rootHeader = new Block, *rootBody = new Block, *exit = new Block;
      for (Block *block : {boundHeader, boundBody, rootHeader, rootBody, exit})
        body.push_back(block);
      Value bound = boundHeader->addArgument(i64, location);
      Value firstRoot = boundHeader->addArgument(i64, location);
      Value root = rootHeader->addArgument(i64, location);
      Value nextBound = rootHeader->addArgument(i64, location);
      Value rootEnd = rootHeader->addArgument(i64, location);
      auto fail = [&](Value status) {
        LLVM::CallOp::create(
            builder, location, TypeRange{},
            SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
            ValueRange{helperContext, status});
      };
      auto field = [&](StringRef table, Type rowType, Value row, int32_t index,
                       Type type) -> Value {
        Value address = LLVM::GEPOp::create(
            builder, location, pointer, rowType,
            LLVM::AddressOfOp::create(builder, location, pointer, table),
            ArrayRef<LLVM::GEPArg>{row, index});
        return LLVM::LoadOp::create(builder, location, type, address,
                                    type == i64 ? 8 : 4);
      };

      builder.setInsertionPointToStart(entry);
      Value zero = llvmConstant(builder, location, i64, 0);
      LLVM::BrOp::create(builder, location, ValueRange{zero, zero},
                         boundHeader);

      builder.setInsertionPointToStart(boundHeader);
      Value more = LLVM::ICmpOp::create(
          builder, location, LLVM::ICmpPredicate::ult, bound,
          llvmConstant(builder, location, i64, boundRows.size()));
      LLVM::CondBrOp::create(builder, location, more, boundBody, exit);

      builder.setInsertionPointToStart(boundBody);
      Value status =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context,
                                 "obelisk_rt_v1_native_state_register_static"),
              ValueRange{helperContext,
                         field(boundsName, boundType, bound, 0, i32),
                         field(boundsName, boundType, bound, 2, i64),
                         field(boundsName, boundType, bound, 3, i64)})
              .getResult();
      fail(status);
      Value end = LLVM::ZExtOp::create(
          builder, location, i64, field(boundsName, boundType, bound, 1, i32));
      Value following = LLVM::AddOp::create(
          builder, location, bound, llvmConstant(builder, location, i64, 1));
      LLVM::BrOp::create(builder, location,
                         ValueRange{firstRoot, following, end}, rootHeader);

      builder.setInsertionPointToStart(rootHeader);
      Value rootsLeft = LLVM::ICmpOp::create(
          builder, location, LLVM::ICmpPredicate::ult, root, rootEnd);
      LLVM::CondBrOp::create(builder, location, rootsLeft, rootBody,
                             ValueRange{}, boundHeader,
                             ValueRange{nextBound, rootEnd});

      builder.setInsertionPointToStart(rootBody);
      Value byte = field(rootsName, rootType, root, 0, i64);
      Value kind = field(rootsName, rootType, root, 1, i32);
      Value slot = LLVM::GEPOp::create(
          builder, location, pointer, builder.getI8Type(),
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    "__obelisk_state_value"),
          ArrayRef<LLVM::GEPArg>{byte});
      fail(LLVM::CallOp::create(
               builder, location, TypeRange{i32},
               SymbolRefAttr::get(
                   context, "obelisk_rt_v1_gc_candidate_static_root_register"),
               ValueRange{helperContext, slot, kind})
               .getResult());
      if (hasDesignBytecode && !sharedNativeState) {
        Value bit = LLVM::ShlOp::create(
            builder, location, byte, llvmConstant(builder, location, i64, 3));
        fail(
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(
                    context, "obelisk_rt_v1_gc_design_candidate_root_register"),
                ValueRange{helperContext, bit, kind})
                .getResult());
      }
      Value nextRoot = LLVM::AddOp::create(
          builder, location, root, llvmConstant(builder, location, i64, 1));
      LLVM::BrOp::create(builder, location,
                         ValueRange{nextRoot, nextBound, rootEnd}, rootHeader);

      builder.setInsertionPointToStart(exit);
      LLVM::ReturnOp::create(builder, location, ValueRange{});
    }
    LLVM::CallOp::create(builder, location, TypeRange{},
                         SymbolRefAttr::get(context, helper.getSymName()),
                         ValueRange{runtimeContext});
  }
  if (requiresNativeStateSync) {
    Value stateValue = LLVM::AddressOfOp::create(builder, location, pointer,
                                                 "__obelisk_state_value");
    Value stateUnknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_state_unknown");
    Value status =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_native_state_sync"),
            ValueRange{
                runtimeContext, stateValue, stateUnknown,
                llvmConstant(builder, location, i64, stateLayout.bitCount)})
            .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status});
  }
  if (stateLayout.directContinuous) {
    SmallVector<Value> arguments{runtimeContext};
    for (StringRef name :
         {"__obelisk_continuous_value", "__obelisk_continuous_unknown",
          "__obelisk_continuous_mask"})
      arguments.push_back(
          LLVM::AddressOfOp::create(builder, location, pointer, name));
    Value status = LLVM::CallOp::create(
                       builder, location, TypeRange{i32},
                       "obelisk_rt_v1_native_state_bind_continuous", arguments)
                       .getResult();
    LLVM::CallOp::create(builder, location, TypeRange{},
                         "obelisk_rt_v1_scheduler_fail",
                         ValueRange{runtimeContext, status});
  }
  if (!stateLayout.unknownSummaryRoots.empty()) {
    auto address = [&](StringRef name) -> Value {
      return LLVM::AddressOfOp::create(builder, location, pointer, name);
    };
    Value status =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            "obelisk_rt_v1_native_state_bind_unknown_summary",
            ValueRange{
                runtimeContext, address("__obelisk_state_unknown"),
                llvmConstant(builder, location, i64, stateLayout.bitCount),
                address(unknownSummaryName), address(unknownSummaryRootsName),
                llvmConstant(builder, location, i64,
                             stateLayout.unknownSummaryRoots.size()),
                address(unknownSummaryPatternsName),
                llvmConstant(builder, location, i64,
                             stateLayout.getUnknownSummaryPatternCount())})
            .getResult();
    LLVM::CallOp::create(builder, location, TypeRange{},
                         "obelisk_rt_v1_scheduler_fail",
                         ValueRange{runtimeContext, status});
  }
  if (bindStateSpecialization) {
    Value fast = LLVM::AddressOfOp::create(
        builder, location, pointer, "__obelisk_state_specialization_fast_v1");
    Value status =
        LLVM::CallOp::create(builder, location, TypeRange{i32},
                             "obelisk_rt_v1_native_state_bind_specialization",
                             ValueRange{runtimeContext, fast})
            .getResult();
    LLVM::CallOp::create(builder, location, TypeRange{},
                         "obelisk_rt_v1_scheduler_fail",
                         ValueRange{runtimeContext, status});
  }
  if (hasCoverage) {
    struct ToggleBinding {
      uint64_t firstBit;
      uint64_t bitCount;
      uint64_t stateHandle;
    };
    SmallVector<ToggleBinding> toggleBindings;
    struct FsmBinding {
      uint64_t machine;
      uint64_t shift;
      uint64_t bitCount;
      uint64_t stateHandle;
    };
    SmallVector<FsmBinding> fsmBindings;
    auto collectToggleBindings = [&](Operation *operation, uint64_t id,
                                     const auto &handles) -> LogicalResult {
      if (auto values = cov::get<cov::FsmBindings>(operation)) {
        auto handle = handles.find(id);
        if (handle == handles.end())
          return operation->emitError(
              "FSM state vector has no native state handle");
        for (Attribute value : values) {
          auto binding = cast<cov::FsmBindingAttr>(value);
          uint64_t stateHandle =
              obelisk_rt_stable_handle_offset(handle->second, binding.getLow());
          if (stateHandle == UINT64_MAX)
            return operation->emitError(
                "FSM coverage binding cannot be represented by a stable state "
                "handle");
          fsmBindings.push_back({binding.getMachine(), binding.getShift(),
                                 binding.getWidth(), stateHandle});
        }
      }
      auto values = cov::get<cov::ToggleBindings>(operation);
      if (!values)
        return success();
      auto handle = handles.find(id);
      if (handle == handles.end())
        return operation->emitError(
            "covered declaration has no native state handle");
      for (Attribute value : values) {
        auto binding = dyn_cast<cov::ToggleBindingAttr>(value);
        if (!binding || binding.getLow() < 0 || binding.getWidth() == 0)
          return operation->emitError("has invalid toggle coverage binding");
        uint64_t firstBit = binding.getBase();
        uint64_t bitCount = binding.getWidth();
        if (firstBit > toggleBitCount || bitCount > toggleBitCount - firstBit)
          return operation->emitError(
              "toggle coverage binding exceeds the prepared inventory");
        uint64_t stateHandle =
            obelisk_rt_stable_handle_offset(handle->second, binding.getLow());
        if (stateHandle == UINT64_MAX)
          return operation->emitError(
              "toggle coverage binding cannot be represented by a stable "
              "state handle");
        toggleBindings.push_back({firstBit, bitCount, stateHandle});
      }
      return success();
    };
    WalkResult collected = module.walk([&](Operation *operation) -> WalkResult {
      LogicalResult result = success();
      if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation))
        result = collectToggleBindings(operation, storage.getId(),
                                       stateLayout.storage);
      else if (auto net = dyn_cast<sim::SimNetDeclOp>(operation))
        result =
            collectToggleBindings(operation, net.getId(), stateLayout.nets);
      return mlir::failed(result) ? WalkResult::interrupt()
                                  : WalkResult::advance();
    });
    if (collected.wasInterrupted())
      return failure();
    llvm::sort(toggleBindings,
               [](const ToggleBinding &lhs, const ToggleBinding &rhs) {
                 return lhs.firstBit < rhs.firstBit;
               });
    Value null = LLVM::ZeroOp::create(builder, location, pointer);
    Value initialValue = null;
    Value initialUnknown = null;
    if (toggleBitCount) {
      auto valueBytes = cov::get<cov::ToggleInitialValue>(module);
      auto unknownBytes = cov::get<cov::ToggleInitialUnknown>(module);
      const uint64_t expectedBytes = (toggleBitCount + 7) / 8;
      if (!valueBytes || !unknownBytes ||
          static_cast<uint64_t>(valueBytes.size()) != expectedBytes ||
          static_cast<uint64_t>(unknownBytes.size()) != expectedBytes)
        return module.emitError(
            "toggle coverage inventory has invalid initial shadow planes");
      auto asString = [](DenseI8ArrayAttr bytes) {
        ArrayRef<int8_t> data = bytes.asArrayRef();
        return StringRef(reinterpret_cast<const char *>(data.data()),
                         data.size());
      };
      LLVM::GlobalOp valueGlobal = makeByteArrayGlobal(
          module, location, "__obelisk_coverage_initial_value_v1",
          asString(valueBytes));
      LLVM::GlobalOp unknownGlobal = makeByteArrayGlobal(
          module, location, "__obelisk_coverage_initial_unknown_v1",
          asString(unknownBytes));
      initialValue = LLVM::AddressOfOp::create(builder, location, pointer,
                                               valueGlobal.getSymName());
      initialUnknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                                 unknownGlobal.getSymName());
    }
    Value status =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_coverage_finalize"),
            ValueRange{runtimeContext,
                       llvmConstant(builder, location, i64, linePointCount),
                       llvmConstant(builder, location, i64, toggleBitCount),
                       initialValue, initialUnknown,
                       llvmConstant(builder, location, i32,
                                    coveragePersistenceMask)})
            .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, status});
    for (const ToggleBinding &binding : toggleBindings) {
      Value bindStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, "obelisk_rt_v1_coverage_toggle_bind"),
              ValueRange{
                  runtimeContext,
                  llvmConstant(builder, location, i64, binding.firstBit),
                  llvmConstant(builder, location, i64, binding.bitCount),
                  llvmConstant(builder, location, i64, binding.stateHandle)})
              .getResult();
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
          ValueRange{runtimeContext, bindStatus});
    }
    for (const FsmBinding &binding : fsmBindings) {
      Value bindStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, "obelisk_rt_v1_coverage_fsm_bind"),
              ValueRange{
                  runtimeContext,
                  llvmConstant(builder, location, i64, binding.machine),
                  llvmConstant(builder, location, i64, binding.shift),
                  llvmConstant(builder, location, i64, binding.bitCount),
                  llvmConstant(builder, location, i64, binding.stateHandle)})
              .getResult();
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
          ValueRange{runtimeContext, bindStatus});
    }
    Value sealStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_coverage_toggle_seal"),
            ValueRange{runtimeContext})
            .getResult();
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
        ValueRange{runtimeContext, sealStatus});
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
  // Report the simulation result before snapshot I/O can replace the
  // thread-local diagnostic.  If both fail, the simulation remains the exit
  // status and each failure is described by the operation that produced it.
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_report_status"),
      ValueRange{runtimeContext, run.getResult()});
  Value finalStatus = run.getResult();
  Value coverageStatus;
  if (shouldDumpCoverage) {
    coverageStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_coverage_snapshot"),
            ValueRange{runtimeContext, run.getResult(), finalTime.getResult()})
            .getResult();
    Value runSucceeded = LLVM::ICmpOp::create(
        builder, location, LLVM::ICmpPredicate::eq, run.getResult(),
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    finalStatus = LLVM::SelectOp::create(builder, location, runSucceeded,
                                         coverageStatus, run.getResult());
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_report_status"),
        ValueRange{runtimeContext, coverageStatus});
  }
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, "obelisk_rt_v1_context_destroy"),
      runtimeContext);
  LLVM::ReturnOp::create(builder, location, finalStatus);

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
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_initialize", i32,
                           {pointer, pointer, pointer, i64, pointer, i64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_destroy", voidType,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_context_configure_argv", i32,
                           {pointer, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_register_static",
                           i32, {pointer, i32, i64, i64});
  if (requiresNativeStateSync)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_sync", i32,
                             {pointer, pointer, pointer, i64});
  if (sharedNativeState)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_bind_shared",
                             i32, {pointer, pointer, pointer, i64});
  if (bindStateSpecialization)
    getOrDeclareLLVMFunction(module,
                             "obelisk_rt_v1_native_state_bind_specialization",
                             i32, {pointer, pointer});
  if (stateLayout.directContinuous)
    getOrDeclareLLVMFunction(module,
                             "obelisk_rt_v1_native_state_bind_continuous", i32,
                             {pointer, pointer, pointer, pointer});
  if (!stateLayout.unknownSummaryRoots.empty())
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_native_state_bind_unknown_summary", i32,
        {pointer, pointer, i64, pointer, pointer, i64, pointer, i64});
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
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_element_type_define_default",
                           i32, {pointer, i64, pointer, i64});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_context_register_import_signature",
                           i32, {pointer, i32, i64, pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_fail", voidType,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                           {pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_report_status",
                           voidType, {pointer, i32});
  if (hasCoverage) {
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_coverage_finalize", i32,
                             {pointer, i64, i64, pointer, pointer, i32});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_coverage_toggle_bind", i32,
                             {pointer, i64, i64, i64});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_coverage_fsm_bind", i32,
                             {pointer, i64, i64, i64, i64});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_coverage_toggle_seal", i32,
                             {pointer});
  }
  if (shouldDumpCoverage)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_coverage_snapshot", i32,
                             {pointer, i32, i64});
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
  if (requiresRecursiveBitstreamFeature)
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_recursive_bitstream_link_anchor", voidType, {});
  if (requiresClassBitstreamFeature)
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_class_bitstream_link_anchor", voidType, {});
  if (requiresClassBitstreamFeature)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_class_bitstream_finalize",
                             i32, {pointer});
  if (requiresClassBitstreamBytecodeFeature)
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_class_bitstream_bytecode_link_anchor", voidType,
        {});
  if (requiresDPIExportBytecodeFeature)
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_dpi_export_bytecode_link_anchor", voidType, {});
  if (requiresDPIImportBytecodeFeature)
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_dpi_import_bytecode_link_anchor", voidType, {});
  return success();
}

} // namespace obelisk::detail
