//===- TargetBackend.cpp - Target-independent code generation ------------===//
//
// The pipeline every target shares: MLIR lowering, LLVM translation, the VPI
// and state-synchronization lifecycle, optimization, and writing an object or
// bitcode. Only target initialization, the target machine and the link differ,
// and those are dispatched through TargetBackend.
//
//===----------------------------------------------------------------------===//

#include "TargetBackend.h"

#include "BackendUtils.h"
#include "NativeBackend.h"
#include "WasmBackend.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Conversion/Passes.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#include <memory>
#include <optional>
#include <system_error>

using namespace llvm;
using namespace mlir;

namespace obelisk::driver {

CodeGenOptLevel getCodeGenOptLevel(uint32_t level) {
  switch (level) {
  case 0:
    return CodeGenOptLevel::None;
  case 1:
    return CodeGenOptLevel::Less;
  case 2:
    return CodeGenOptLevel::Default;
  default:
    return CodeGenOptLevel::Aggressive;
  }
}

FailureOr<SmallString<256>> makeTemporaryBeside(StringRef output,
                                                StringRef suffix) {
  SmallString<256> pattern(output);
  pattern.append(".tmp-%%%%%%");
  pattern.append(suffix);
  int descriptor = -1;
  SmallString<256> path;
  if (sys::fs::createUniqueFile(pattern, descriptor, path))
    return failure();
  sys::Process::SafelyCloseFileDescriptor(descriptor);
  return path;
}

LogicalResult atomicallyReplace(StringRef temporary, StringRef output) {
  std::error_code error = sys::fs::rename(temporary, output);
  if (error) {
    errs() << "obelisk: error: could not publish '" << output
           << "': " << error.message() << '\n';
    return failure();
  }
  return success();
}

std::optional<std::string>
TargetBackend::findSupportTree(StringRef executablePath) const {
  SmallString<256> executable(executablePath);
  if (sys::fs::make_absolute(executable))
    return std::nullopt;
  sys::path::remove_filename(executable);
  SmallVector<SmallString<256>, 2> candidates;
  SmallString<256> installed(executable);
  sys::path::append(installed, "..", "lib", "obelisk", "targets");
  sys::path::append(installed, getTriple());
  candidates.push_back(installed);
  SmallString<256> buildTree(executable);
  sys::path::append(buildTree, "..", "..", "lib", "obelisk");
  sys::path::append(buildTree, "targets", getTriple());
  candidates.push_back(buildTree);
  for (SmallString<256> &candidate : candidates) {
    sys::path::remove_dots(candidate, true);
    if (sys::fs::exists(Twine(candidate) + "/.complete"))
      return candidate.str().str();
  }
  return std::nullopt;
}

std::unique_ptr<TargetBackend> createTargetBackend(TargetKind target) {
  switch (target) {
  case TargetKind::Native:
#if OBELISK_HAS_NATIVE_BACKEND
    return createNativeBackend();
#else
    return nullptr;
#endif
  case TargetKind::Wasm:
#if OBELISK_HAS_WASM_BACKEND
    return createWasmBackend();
#else
    return nullptr;
#endif
  }
  return nullptr;
}

namespace {

OptimizationLevel getLLVMOptLevel(uint32_t level) {
  switch (level) {
  case 0:
    return OptimizationLevel::O0;
  case 1:
    return OptimizationLevel::O1;
  case 2:
    return OptimizationLevel::O2;
  default:
    return OptimizationLevel::O3;
  }
}

LogicalResult addMinimalMain(ModuleOp module) {
  if (module.lookupSymbol("main"))
    return success();
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToEnd(module.getBody());
  Location location = module.getLoc();
  mlir::Type i32 = builder.getI32Type();
  auto main = LLVM::LLVMFuncOp::create(
      builder, location, "main", LLVM::LLVMFunctionType::get(i32, {}, false));
  Block *entry = main.addEntryBlock(builder);
  builder.setInsertionPointToStart(entry);
  mlir::Value zero = LLVM::ConstantOp::create(builder, location, i32,
                                              builder.getI32IntegerAttr(0));
  LLVM::ReturnOp::create(builder, location, zero);
  return success();
}

LogicalResult lowerToLLVM(ModuleOp module, TargetMachine &targetMachine,
                          StringRef triple, bool bytecode, StringRef vpi,
                          obelisk::sim::NativeSchedulerMode nativeScheduler,
                          bool &requiresStateSync) {
  if (bytecode && nativeScheduler == obelisk::sim::NativeSchedulerMode::Auto)
    nativeScheduler = obelisk::sim::NativeSchedulerMode::Generic;
  module->setAttr("llvm.target_triple",
                  StringAttr::get(module.getContext(), triple));
  module->setAttr(
      "llvm.data_layout",
      StringAttr::get(
          module.getContext(),
          targetMachine.createDataLayout().getStringRepresentation()));
  mlir::PassManager manager(module.getContext());
  bool hasLanguageOverride = false;
  module.walk([&](mlir::Operation *operation) {
    if (mlir::isa<obelisk::sim::SimOverrideOp,
                  obelisk::sim::SimReleaseOverrideOp>(operation))
      hasLanguageOverride = true;
  });
  requiresStateSync = vpi != "off" || hasLanguageOverride;
  module->setAttr("obelisk.native_scheduler",
                  obelisk::sim::NativeSchedulerModeAttr::get(
                      module.getContext(), nativeScheduler));
  // Hybrid AOT keeps bytecode available as the canonical implementation for
  // fragments that cannot be scheduled statically and for writable VPI
  // transition stages. The shared process frame lets those fragments return
  // to native execution at a continuation boundary without copying state.
  bool evalScheduler =
      nativeScheduler == obelisk::sim::NativeSchedulerMode::Eval;
  bool needsHybridBytecode =
      nativeScheduler != obelisk::sim::NativeSchedulerMode::Generic;
  bool needsSampledStatePlan = false;
  module.walk([&](obelisk::sim::SimSampledReadOp) {
    needsSampledStatePlan = true;
  });
  bool needsWaveformMetadata = false;
  module.walk([&](mlir::Operation *operation) {
    needsWaveformMetadata |=
        mlir::isa<obelisk::sim::SimDumpOpenOp,
                  obelisk::sim::SimDumpOpenStringOp,
                  obelisk::sim::SimDumpTimescaleOp, obelisk::sim::SimDumpVarsOp,
                  obelisk::sim::SimDumpAllOp, obelisk::sim::SimDumpControlOp,
                  obelisk::sim::SimDumpLimitOp, obelisk::sim::SimDumpFlushOp>(
            operation);
  });
  bool needsDesignEncoding =
      bytecode || needsHybridBytecode || vpi != "off" || hasLanguageOverride ||
      needsWaveformMetadata;
  requiresStateSync |= needsSampledStatePlan && !needsDesignEncoding;
  if (needsDesignEncoding) {
    EncodeObeliskSimToBytecodePassOptions options;
    options.vpi = vpi.str();
    options.requireBytecode = bytecode;
    manager.addPass(createEncodeObeliskSimToBytecodePass(options));
  } else if (needsSampledStatePlan) {
    SmallVector<obelisk::sim::SimDesignOp> designs;
    module.walk([&](obelisk::sim::SimDesignOp design) {
      designs.push_back(design);
    });
    if (designs.size() != 1)
      return module.emitError(
          "sampled-state planning requires exactly one simulation design");
    FailureOr<SimulationSampledStatePlan> plan =
        planSimulationSampledState(designs.front());
    if (failed(plan))
      return failure();
    OpBuilder builder(module.getContext());
    module->setAttr("obelisk.execution.flags",
                    builder.getI32IntegerAttr(plan->executionFlags));
    module->setAttr("obelisk.execution.state_bits",
                    builder.getI64IntegerAttr(plan->stateBitCount));
    SmallVector<int64_t> sampledRanges;
    sampledRanges.reserve(plan->ranges.size() * 2);
    for (const SimulationSampledRange &range : plan->ranges) {
      sampledRanges.push_back(static_cast<int64_t>(range.bitOffset));
      sampledRanges.push_back(static_cast<int64_t>(range.bitWidth));
    }
    module->setAttr("obelisk.execution.sampled_ranges",
                    builder.getDenseI64ArrayAttr(sampledRanges));
  }
  // Native region bodies may diverge from their already-frozen bytecode
  // fallback after this point. Keep the generic scheduler as an untouched
  // oracle and apply AOT-only next-state rewrites only when the hybrid image
  // provides the deoptimization implementation.
  if (needsHybridBytecode || evalScheduler)
    manager.addPass(createObeliskSimOptimizeNativeRegionsPass());
  manager.addPass(createConvertObeliskSimProcessesToLLVMCoroutinesPass());
  if (failed(manager.run(module)))
    return failure();
  return addMinimalMain(module);
}

LogicalResult optimizeLLVMModule(llvm::Module &module,
                                 TargetMachine &targetMachine,
                                 uint32_t optLevel) {
  if (verifyModule(module, &errs())) {
    errs() << "obelisk: error: invalid LLVM IR before native optimization\n";
    return failure();
  }
  PassBuilder builder(&targetMachine);
  LoopAnalysisManager loopAnalyses;
  FunctionAnalysisManager functionAnalyses;
  CGSCCAnalysisManager cgsccAnalyses;
  llvm::ModuleAnalysisManager moduleAnalyses;
  builder.registerModuleAnalyses(moduleAnalyses);
  builder.registerCGSCCAnalyses(cgsccAnalyses);
  builder.registerFunctionAnalyses(functionAnalyses);
  builder.registerLoopAnalyses(loopAnalyses);
  builder.crossRegisterProxies(loopAnalyses, functionAnalyses, cgsccAnalyses,
                               moduleAnalyses);
  OptimizationLevel level = getLLVMOptLevel(optLevel);
  ModulePassManager passes = optLevel == 0
                                 ? builder.buildO0DefaultPipeline(level)
                                 : builder.buildPerModuleDefaultPipeline(level);
  passes.run(module, moduleAnalyses);
  if (verifyModule(module, &errs())) {
    errs() << "obelisk: error: invalid LLVM IR after native optimization\n";
    return failure();
  }
  return success();
}

LogicalResult
addVPIStartupLifecycle(llvm::Module &module, StringRef vpi,
                       ArrayRef<SharedLibraryInput> sharedLibraryInputs,
                       bool requiresStateSync) {
  bool enableVPI = vpi != "off";
  if (!enableVPI && !requiresStateSync)
    return success();
  llvm::Function *main = module.getFunction("main");
  llvm::GlobalVariable *current =
      module.getNamedGlobal("__obelisk_current_context");
  llvm::GlobalVariable *stateValue =
      module.getNamedGlobal("__obelisk_state_value");
  llvm::GlobalVariable *stateUnknown =
      module.getNamedGlobal("__obelisk_state_unknown");
  if (!main || !current || !stateValue || !stateUnknown) {
    errs() << "obelisk: error: native state synchronization requires a "
              "generated scheduler main\n";
    return failure();
  }
  llvm::CallBase *spawn = nullptr;
  llvm::CallBase *destroy = nullptr;
  for (llvm::BasicBlock &block : *main)
    for (llvm::Instruction &instruction : block)
      if (auto *call = dyn_cast<llvm::CallBase>(&instruction))
        if (llvm::Function *callee = call->getCalledFunction()) {
          if (callee->getName().ends_with(".__obelisk_spawn"))
            spawn = call;
          else if (callee->getName() == "obelisk_rt_v1_context_destroy")
            destroy = call;
        }
  if (!spawn || (enableVPI && !destroy)) {
    errs() << "obelisk: error: generated scheduler lifecycle is incomplete\n";
    return failure();
  }

  llvm::LLVMContext &context = module.getContext();
  llvm::Type *pointer = llvm::PointerType::get(context, 0);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  SmallVector<llvm::Constant *> names;
  unsigned stringIndex = 0;
  for (const SharedLibraryInput &input : sharedLibraryInputs) {
    if (!input.hasVPIStartup)
      continue;
    llvm::Constant *bytes =
        llvm::ConstantDataArray::getString(context, input.loaderName, true);
    auto *global = new llvm::GlobalVariable(
        module, bytes->getType(), true, llvm::GlobalValue::PrivateLinkage,
        bytes, (Twine("__obelisk_vpi_module_") + Twine(stringIndex++)).str());
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    names.push_back(global);
  }
  llvm::Constant *nameArrayPointer =
      llvm::ConstantPointerNull::get(cast<llvm::PointerType>(pointer));
  if (!names.empty()) {
    llvm::ArrayType *arrayType = llvm::ArrayType::get(pointer, names.size());
    auto *array = new llvm::GlobalVariable(
        module, arrayType, true, llvm::GlobalValue::PrivateLinkage,
        llvm::ConstantArray::get(arrayType, names),
        "__obelisk_vpi_startup_modules");
    llvm::Constant *zero = llvm::ConstantInt::get(i64, 0);
    SmallVector<llvm::Constant *, 2> indices{zero, zero};
    nameArrayPointer =
        llvm::ConstantExpr::getInBoundsGetElementPtr(arrayType, array, indices);
  }
  llvm::FunctionCallee fail = module.getOrInsertFunction(
      "obelisk_rt_v1_scheduler_fail",
      llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                              {pointer, llvm::Type::getInt32Ty(context)},
                              false));
  llvm::IRBuilder<> beforeSpawn(spawn);
  llvm::Value *runtimeContext =
      beforeSpawn.CreateLoad(pointer, current, "obelisk.vpi.context");
  auto *valueArray = dyn_cast<llvm::ArrayType>(stateValue->getValueType());
  auto *unknownArray = dyn_cast<llvm::ArrayType>(stateUnknown->getValueType());
  if (!valueArray || !unknownArray ||
      valueArray->getNumElements() != unknownArray->getNumElements()) {
    errs() << "obelisk: error: generated native state planes disagree\n";
    return failure();
  }
  llvm::GlobalVariable *execution =
      module.getNamedGlobal("__obelisk_execution_descriptor_v1");
  auto *executionInitializer =
      execution ? dyn_cast<llvm::ConstantStruct>(execution->getInitializer())
                : nullptr;
  auto *stateBitCount =
      executionInitializer && executionInitializer->getNumOperands() > 7
          ? dyn_cast<llvm::ConstantInt>(executionInitializer->getOperand(7))
          : nullptr;
  if (!stateBitCount ||
      stateBitCount->getZExtValue() > valueArray->getNumElements() * 8) {
    errs() << "obelisk: error: execution descriptor disagrees with generated "
              "native state planes\n";
    return failure();
  }
  if (requiresStateSync) {
    llvm::FunctionCallee sync = module.getOrInsertFunction(
        "obelisk_rt_v1_native_state_sync",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context),
                                {pointer, pointer, pointer, i64}, false));
    llvm::Value *syncStatus = beforeSpawn.CreateCall(
        sync,
        {runtimeContext, stateValue, stateUnknown,
         llvm::ConstantInt::get(i64, stateBitCount->getZExtValue())},
        "obelisk.state.sync");
    beforeSpawn.CreateCall(fail, {runtimeContext, syncStatus});
  }
  if (enableVPI) {
    llvm::FunctionCallee startup = module.getOrInsertFunction(
        "obelisk_rt_v1_vpi_startup",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context),
                                {pointer, pointer, i64}, false));
    llvm::Value *status =
        beforeSpawn.CreateCall(startup,
                               {runtimeContext, nameArrayPointer,
                                llvm::ConstantInt::get(i64, names.size())},
                               "obelisk.vpi.startup");
    beforeSpawn.CreateCall(fail, {runtimeContext, status});
    llvm::FunctionCallee shutdown = module.getOrInsertFunction(
        "obelisk_rt_v1_vpi_shutdown",
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {pointer},
                                false));
    llvm::IRBuilder<> beforeDestroy(destroy);
    beforeDestroy.CreateCall(shutdown, {destroy->getArgOperand(0)});
  }
  return success();
}

LogicalResult writeObject(llvm::Module &module, TargetMachine &targetMachine,
                          StringRef path, StringRef description) {
  std::error_code error;
  raw_fd_ostream output(path, error, sys::fs::OF_None);
  if (error) {
    errs() << "obelisk: error: could not create object '" << path
           << "': " << error.message() << '\n';
    return failure();
  }
  legacy::PassManager codegen;
  if (targetMachine.addPassesToEmitFile(codegen, output, nullptr,
                                        CodeGenFileType::ObjectFile)) {
    errs() << "obelisk: error: " << description
           << " target cannot emit objects\n";
    return failure();
  }
  codegen.run(module);
  output.flush();
  if (output.has_error()) {
    errs() << "obelisk: error: failed while writing object '" << path
           << "': " << output.error().message() << '\n';
    output.clear_error();
    return failure();
  }
  return success();
}

LogicalResult writeBitcode(llvm::Module &module, StringRef path) {
  std::error_code error;
  raw_fd_ostream output(path, error, sys::fs::OF_None);
  if (error) {
    errs() << "obelisk: error: could not create bitcode '" << path
           << "': " << error.message() << '\n';
    return failure();
  }
  ProfileSummaryInfo profileSummary(module);
  ModuleSummaryIndex index =
      buildModuleSummaryIndex(module, nullptr, &profileSummary);
  WriteBitcodeToFile(module, output, false, &index, true);
  output.flush();
  if (output.has_error()) {
    errs() << "obelisk: error: failed while writing bitcode '" << path
           << "': " << output.error().message() << '\n';
    output.clear_error();
    return failure();
  }
  return success();
}

} // namespace

LogicalResult emitTargetOutput(ModuleOp module,
                               const NativeOutputOptions &options) {
  std::unique_ptr<TargetBackend> backend = createTargetBackend(options.target);
  if (!backend) {
    errs() << "obelisk: error: this build does not include the requested "
              "code-generation target\n";
    return failure();
  }

  std::string targetError;
  std::unique_ptr<TargetMachine> targetMachine =
      backend->createTargetMachine(targetError, options.optLevel);
  if (!targetMachine) {
    errs() << "obelisk: error: could not create " << backend->getDescription()
           << " target: " << targetError << '\n';
    return failure();
  }
  bool requiresStateSync = false;
  std::optional<obelisk::sim::NativeSchedulerMode> nativeScheduler =
      obelisk::sim::symbolizeNativeSchedulerMode(options.nativeScheduler);
  if (!nativeScheduler) {
    errs() << "obelisk: error: invalid native scheduler mode\n";
    return failure();
  }
  // Reject unprofitable auto candidates before bytecode materialization.  A
  // profitable candidate remains Auto: coroutine lowering has the physical
  // state layout, exact fanout, and direct fragments needed to decide whether
  // the generated periodic eval form is actually materializable.  Converting
  // Auto to Eval here would incorrectly make that later proof mandatory for
  // ordinary non-periodic designs.
  if (*nativeScheduler == obelisk::sim::NativeSchedulerMode::Auto &&
      !options.bytecode) {
    obelisk::analysis::NativeAOTAnalysis aot =
        obelisk::analysis::NativeAOTAnalysis::compute(module);
    if (!aot.isEligible() || !aot.isAOTCostEffective())
      *nativeScheduler = obelisk::sim::NativeSchedulerMode::Generic;
    // A structural periodic candidate is only a cheap pipeline-shaping hint.
    // Keep Auto through coroutine lowering, where physical aliases, exact
    // fanout, and direct-fragment coverage can be proved together. A false
    // positive must remain eligible for the generic/AOT fallback.
  }
  if (failed(lowerToLLVM(module, *targetMachine, backend->getTriple(),
                         options.bytecode, options.vpi, *nativeScheduler,
                         requiresStateSync)))
    return failure();

  registerLLVMDialectTranslation(*module.getContext());
  registerBuiltinDialectTranslation(*module.getContext());
  llvm::LLVMContext llvmContext;
  std::unique_ptr<llvm::Module> llvmModule =
      translateModuleToLLVMIR(module, llvmContext, "obelisk");
  if (!llvmModule) {
    errs() << "obelisk: error: LLVM dialect translation failed\n";
    return failure();
  }
  llvmModule->setTargetTriple(Triple(backend->getTriple()));
  llvmModule->setDataLayout(targetMachine->createDataLayout());
  if (failed(addVPIStartupLifecycle(*llvmModule, options.vpi,
                                    options.sharedLibraryInputs,
                                    requiresStateSync)))
    return failure();
  if (failed(optimizeLLVMModule(*llvmModule, *targetMachine, options.optLevel)))
    return failure();

  bool fullLTO = options.kind == NativeOutputKind::Executable &&
                 !options.noLTO && backend->usesFullLTO(options.optLevel);
  if (fullLTO) {
    // LLD's explicit --lto=full mode selects LLVM's unified LTO pipeline.
    // Match Clang -flto=full -funified-lto bitcode so every module in the
    // optimized link carries the required pipeline marker.
    llvmModule->addModuleFlag(llvm::Module::Error, "EnableSplitLTOUnit", 1);
    llvmModule->addModuleFlag(llvm::Module::Error, "UnifiedLTO", 1);
  }

  if (options.kind == NativeOutputKind::LLVMIR) {
    std::error_code error;
    raw_fd_ostream output(options.outputPath, error, sys::fs::OF_Text);
    if (error) {
      errs() << "obelisk: error: could not open LLVM IR output '"
             << options.outputPath << "': " << error.message() << '\n';
      return failure();
    }
    llvmModule->print(output, nullptr);
    output.flush();
    if (output.has_error()) {
      errs() << "obelisk: error: failed while writing LLVM IR output '"
             << options.outputPath << "': " << output.error().message() << '\n';
      output.clear_error();
      return failure();
    }
    return success();
  }

  FailureOr<SmallString<256>> moduleTemporary =
      makeTemporaryBeside(options.outputPath, fullLTO ? ".bc" : ".o");
  if (failed(moduleTemporary)) {
    errs() << "obelisk: error: could not create temporary target module\n";
    return failure();
  }
  LogicalResult wroteModule =
      fullLTO ? writeBitcode(*llvmModule, *moduleTemporary)
              : writeObject(*llvmModule, *targetMachine, *moduleTemporary,
                            backend->getDescription());
  if (failed(wroteModule)) {
    sys::fs::remove(*moduleTemporary);
    return failure();
  }
  if (options.kind == NativeOutputKind::Object) {
    if (failed(atomicallyReplace(*moduleTemporary, options.outputPath))) {
      sys::fs::remove(*moduleTemporary);
      return failure();
    }
    return success();
  }

  std::optional<std::string> support =
      backend->findSupportTree(options.executablePath);
  if (!support) {
    errs() << "obelisk: error: " << backend->getDescription()
           << " link support tree was not found relative to '"
           << options.executablePath << "'\n";
    sys::fs::remove(*moduleTemporary);
    return failure();
  }
  LogicalResult linked = backend->linkExecutable(
      *moduleTemporary, options.outputPath, *support, options);
  sys::fs::remove(*moduleTemporary);
  return linked;
}

} // namespace obelisk::driver
