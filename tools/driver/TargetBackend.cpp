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
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Coroutines/CoroCleanup.h"
#include "llvm/Transforms/Coroutines/CoroEarly.h"
#include "llvm/Transforms/Coroutines/CoroSplit.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <chrono>
#include <memory>
#include <mutex>
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

struct NativePartition {
  std::string id;
  SmallVector<std::string> members;
  SmallVector<std::string> exports;
};

struct NativePartitionPlan {
  SmallVector<NativePartition> partitions;
};

FailureOr<std::optional<NativePartitionPlan>>
readNativePartitionPlan(ModuleOp module) {
  auto manifest = module->getAttrOfType<ArrayAttr>(
      obelisk::sim::metadata::nativePhysicalPartitionManifest);
  if (!manifest)
    return std::optional<NativePartitionPlan>{};
  NativePartitionPlan plan;
  for (mlir::Attribute entryAttr : manifest) {
    auto entry = dyn_cast<DictionaryAttr>(entryAttr);
    auto id = entry ? entry.getAs<StringAttr>("id") : StringAttr{};
    auto members = entry ? entry.getAs<ArrayAttr>("members") : ArrayAttr{};
    auto exports = entry ? entry.getAs<ArrayAttr>("exports") : ArrayAttr{};
    if (!id || !members || !exports)
      return module.emitError("has malformed physical partition manifest"),
             failure();
    NativePartition &partition = plan.partitions.emplace_back();
    partition.id = id.getValue().str();
    for (mlir::Attribute memberAttr : members) {
      auto member = dyn_cast<FlatSymbolRefAttr>(memberAttr);
      if (!member)
        return module.emitError("has malformed physical partition member"),
               failure();
      partition.members.push_back(member.getValue().str());
    }
    for (mlir::Attribute exportAttr : exports) {
      auto exportSymbol = dyn_cast<FlatSymbolRefAttr>(exportAttr);
      if (!exportSymbol)
        return module.emitError("has malformed physical partition export"),
               failure();
      partition.exports.push_back(exportSymbol.getValue().str());
    }
  }
  if (plan.partitions.empty())
    return std::optional<NativePartitionPlan>{};
  return std::optional<NativePartitionPlan>(std::move(plan));
}

bool shouldSplitNativeModule(const llvm::Module &module,
                             const NativePartitionPlan &plan) {
  if (plan.partitions.size() < 2)
    return false;
  uint64_t functionCount = 0;
  uint64_t instructionCount = 0;
  for (const llvm::Function &function : module) {
    if (function.isDeclaration())
      continue;
    ++functionCount;
    instructionCount += function.getInstructionCount();
  }
  return functionCount >= 128 || instructionCount >= 100000;
}

struct NativeModuleSplitPlan {
  llvm::DenseMap<const llvm::GlobalValue *, unsigned> assignments;
  SmallVector<unsigned> groups;
  llvm::DenseSet<unsigned> nativeObjectGroups;
};

uint64_t estimateNativeGlobalWeight(const llvm::GlobalVariable &global) {
  if (!global.hasInitializer())
    return 1;
  uint64_t weight = 1;
  SmallVector<const llvm::Constant *> worklist{global.getInitializer()};
  llvm::SmallPtrSet<const llvm::Constant *, 32> visited;
  while (!worklist.empty()) {
    const llvm::Constant *constant = worklist.pop_back_val();
    if (!visited.insert(constant).second)
      continue;
    if (auto *data = dyn_cast<llvm::ConstantDataSequential>(constant))
      weight += std::max<uint64_t>(1, (data->getNumElements() + 31) / 32);
    else
      weight += std::max<unsigned>(1, constant->getNumOperands());
    for (const llvm::Use &operand : constant->operands())
      if (auto *child = dyn_cast<llvm::Constant>(operand.get()))
        worklist.push_back(child);
  }
  return weight;
}

Expected<NativeModuleSplitPlan>
planNativeModuleSplit(llvm::Module &module, const NativePartitionPlan &plan,
                      unsigned maxGroups) {
  // The semantic manifest supplies stable ownership and dependency identity,
  // but an owner can contain a huge generated class or tens of thousands of
  // constants.  Treating every owner as an indivisible physical shard leaves
  // most ThinLTO workers idle behind a few outliers.  Form stable owner-local
  // definition units and use deterministic longest-processing-time packing.
  // ThinLTO's global index remains responsible for cross-unit importing and
  // whole-program optimization.
  maxGroups = std::max(2u, maxGroups);
  llvm::DenseMap<const llvm::GlobalValue *, StringRef> ownerByValue;
  for (const NativePartition &partition : plan.partitions) {
    for (StringRef member : partition.members) {
      llvm::GlobalValue *value = module.getNamedValue(member);
      if (!value)
        return createStringError(inconvertibleErrorCode(),
                                 "physical partition member is missing: %s",
                                 member.str().c_str());
      auto [entry, inserted] = ownerByValue.try_emplace(value, partition.id);
      if (!inserted && entry->second != partition.id)
        return createStringError(
            inconvertibleErrorCode(),
            "physical partition member has multiple owners: %s",
            member.str().c_str());
    }
    for (StringRef symbol : partition.exports)
      if (!module.getNamedValue(symbol))
        return createStringError(inconvertibleErrorCode(),
                                 "physical partition export is missing: %s",
                                 symbol.str().c_str());
  }

  // LLVM's coroutine split creates resume/destroy/cleanup functions after the
  // physical manifest was frozen. Recover the ramp owner so their stable unit
  // identity does not depend on late module order.
  for (llvm::Function &function : module) {
    if (function.isDeclaration() || ownerByValue.count(&function))
      continue;
    StringRef name = function.getName();
    for (StringRef marker : {".resume", ".destroy", ".cleanup"}) {
      size_t position = name.rfind(marker);
      if (position == StringRef::npos)
        continue;
      llvm::Function *ramp = module.getFunction(name.take_front(position));
      if (ramp && ownerByValue.count(ramp)) {
        ownerByValue[&function] = ownerByValue.lookup(ramp);
        break;
      }
    }
  }

  struct SplitUnit {
    std::string key;
    const llvm::GlobalValue *value = nullptr;
    uint64_t weight = 1;
    bool nativeObject = false;
  };
  SmallVector<SplitUnit> units;
  for (llvm::GlobalValue &value : module.global_values()) {
    if (value.isDeclaration())
      continue;
    StringRef owner = ownerByValue.lookup(&value);
    if (owner.empty())
      owner = "primary";
    SplitUnit &unit = units.emplace_back();
    unit.key = owner.str();
    unit.key.push_back('\0');
    unit.key.append(value.getName());
    unit.value = &value;
    if (auto *function = dyn_cast<llvm::Function>(&value)) {
      unit.weight = std::max<uint64_t>(1, function->getInstructionCount());
      // Keep only genuinely exceptional bodies out of ThinLTO. Medium-sized
      // generated functions still benefit from importing small helpers, and
      // the wide-packed RMW patterns that previously poisoned instruction
      // selection are removed before translation.
      unit.nativeObject = function->getInstructionCount() > 20000;
    } else if (auto *global = dyn_cast<llvm::GlobalVariable>(&value))
      unit.weight = estimateNativeGlobalWeight(*global);
  }
  llvm::sort(units, [](const SplitUnit &lhs, const SplitUnit &rhs) {
    if (lhs.nativeObject != rhs.nativeObject)
      return lhs.nativeObject > rhs.nativeObject;
    return lhs.weight != rhs.weight ? lhs.weight > rhs.weight
                                    : lhs.key < rhs.key;
  });
  unsigned groupCount = std::min<unsigned>(maxGroups, units.size());
  SmallVector<uint64_t> groupWeights(groupCount, 0);
  llvm::DenseMap<const llvm::GlobalValue *, unsigned> assignments;
  llvm::DenseSet<unsigned> nativeObjectGroups;
  unsigned nativeUnitCount = llvm::count_if(
      units, [](const SplitUnit &unit) { return unit.nativeObject; });
  unsigned nativeGroupCount = 0;
  if (nativeUnitCount == units.size())
    nativeGroupCount = groupCount;
  else if (nativeUnitCount != 0)
    // Reserve at most one third of the ready queue for direct-O3 outliers.
    // Packing those definitions across a hardware-thread-sized set keeps all
    // cores busy without consuming every group and disabling ThinLTO for the
    // ordinary importable body of the design.
    nativeGroupCount =
        std::min(nativeUnitCount, std::max(1u, groupCount / 3));
  for (unsigned group = 0; group != nativeGroupCount; ++group)
    nativeObjectGroups.insert(group);

  auto assignRange = [&](unsigned begin, unsigned end, unsigned firstGroup,
                         unsigned endGroup) {
    for (unsigned index = begin; index != end; ++index) {
      unsigned group = firstGroup;
      for (unsigned candidate = firstGroup + 1; candidate != endGroup;
           ++candidate)
        if (groupWeights[candidate] < groupWeights[group])
          group = candidate;
      assignments[units[index].value] = group;
      groupWeights[group] += units[index].weight;
    }
  };
  if (nativeUnitCount != 0)
    assignRange(0, nativeUnitCount, 0, nativeGroupCount);
  if (nativeUnitCount != units.size())
    assignRange(nativeUnitCount, units.size(), nativeGroupCount, groupCount);

  // Physical balancing can cut any semantic owner boundary. Cross-object
  // definitions therefore cannot retain local linkage. Give every local
  // definition a deterministic hidden name before cloning. ThinLTO may still
  // internalize these after it has seen the complete program.
  SmallVector<llvm::GlobalValue *> localDefinitions;
  for (llvm::GlobalValue &value : module.global_values())
    if (!value.isDeclaration() && value.hasLocalLinkage())
      localDefinitions.push_back(&value);
  llvm::sort(localDefinitions,
             [](const llvm::GlobalValue *lhs, const llvm::GlobalValue *rhs) {
               return lhs->getName() < rhs->getName();
             });
  for (llvm::GlobalValue *value : localDefinitions) {
    std::string originalName = value->getName().str();
    if (!value->hasLocalLinkage())
      continue;
    std::string seed = module.getModuleIdentifier();
    seed.push_back('\0');
    seed.append(originalName);
    std::string base = (Twine("__obelisk_partition_") +
                        utohexstr(xxHash64(seed)) + "_" + originalName)
                           .str();
    std::string name = base;
    for (unsigned collision = 0; module.getNamedValue(name); ++collision)
      name = (Twine(base) + "_" + Twine(collision + 1)).str();
    value->setName(name);
    value->setLinkage(llvm::GlobalValue::ExternalLinkage);
    value->setVisibility(llvm::GlobalValue::HiddenVisibility);
  }

  SmallVector<unsigned> groups;
  for (unsigned group = 0; group != groupCount; ++group) {
    bool nonempty = llvm::any_of(
        assignments, [&](const auto &entry) { return entry.second == group; });
    if (!nonempty)
      continue;
    groups.push_back(group);
  }
  return NativeModuleSplitPlan{std::move(assignments), std::move(groups),
                               std::move(nativeObjectGroups)};
}

std::unique_ptr<llvm::Module>
cloneNativeModulePartition(llvm::Module &module,
                           const NativeModuleSplitPlan &plan, unsigned group) {
  ValueToValueMapTy mapping;
  std::unique_ptr<llvm::Module> result =
      CloneModule(module, mapping, [&](const llvm::GlobalValue *value) {
        auto found = plan.assignments.find(value);
        return found != plan.assignments.end() && found->second == group;
      });
  // CloneModule keeps a declaration for every definition rejected by the
  // predicate. A UVM module has tens of thousands of symbols, so retaining
  // that complete inventory in every shard makes the ThinLTO combined index
  // scale as O(shards * whole-program symbols). Keep only declarations that
  // the selected definitions actually reference.
  for (llvm::GlobalValue &value :
       llvm::make_early_inc_range(result->global_values()))
    if (value.isDeclaration() && value.use_empty())
      value.eraseFromParent();
  result->setModuleIdentifier(
      (Twine(module.getModuleIdentifier()) + ".partition." + Twine(group))
          .str());
  return result;
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
                          uint32_t optLevel, bool planSemanticPartitions,
                          bool timing, bool &requiresStateSync) {
  if (bytecode && nativeScheduler == obelisk::sim::NativeSchedulerMode::Auto)
    nativeScheduler = obelisk::sim::NativeSchedulerMode::Generic;
  module->setAttr("llvm.target_triple",
                  StringAttr::get(module.getContext(), triple));
  module->setAttr(
      "llvm.data_layout",
      StringAttr::get(
          module.getContext(),
          targetMachine.createDataLayout().getStringRepresentation()));
  module->setAttr(
      "obelisk.native.optimization_level",
      IntegerAttr::get(mlir::IntegerType::get(module.getContext(), 32),
                       optLevel));
  mlir::PassManager manager(module.getContext());
  if (timing)
    manager.enableTiming();
  if (timing)
    module->setAttr("obelisk.debug.native_timing",
                    UnitAttr::get(module.getContext()));
  else
    module->removeAttr("obelisk.debug.native_timing");
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
  module.walk(
      [&](obelisk::sim::SimSampledReadOp) { needsSampledStatePlan = true; });
  bool needsWaveformMetadata = false;
  module.walk([&](mlir::Operation *operation) {
    needsWaveformMetadata |= mlir::isa<
        obelisk::sim::SimDumpOpenOp, obelisk::sim::SimDumpOpenStringOp,
        obelisk::sim::SimDumpTimescaleOp, obelisk::sim::SimDumpVarsOp,
        obelisk::sim::SimDumpAllOp, obelisk::sim::SimDumpControlOp,
        obelisk::sim::SimDumpLimitOp, obelisk::sim::SimDumpFlushOp,
        obelisk::sim::SimDumpPortsOp, obelisk::sim::SimDumpPortsControlOp>(
        operation);
  });
  bool needsDesignEncoding = bytecode || needsHybridBytecode || vpi != "off" ||
                             hasLanguageOverride || needsWaveformMetadata;
  requiresStateSync |= needsSampledStatePlan && !needsDesignEncoding;
  if (needsDesignEncoding) {
    // Bytecode and native lowering must observe the same suspension-safe SSA
    // shape. The coroutine pass also runs this canonicalization for native
    // lowering, but bytecode is frozen before that pass starts.
    OpPassManager &designManager = manager.nest<obelisk::sim::SimDesignOp>();
    designManager.nest<obelisk::sim::SimFuncOp>().addPass(
        createObeliskSimThreadProcessCFGPass());
    EncodeObeliskSimToBytecodePassOptions options;
    options.vpi = vpi.str();
    options.requireBytecode = bytecode;
    manager.addPass(createEncodeObeliskSimToBytecodePass(options));
  } else if (needsSampledStatePlan) {
    SmallVector<obelisk::sim::SimDesignOp> designs;
    module.walk(
        [&](obelisk::sim::SimDesignOp design) { designs.push_back(design); });
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
  // Partition metadata is a native ELF object/ThinLTO contract. In
  // particular, wasm64 keeps its current single-module lowering and staged
  // wasm-object runtime even when the source design is large.
  if (planSemanticPartitions)
    manager.nest<obelisk::sim::SimDesignOp>().addPass(
        createObeliskSimPlanNativePartitionsPass());
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

LogicalResult lowerLLVMCoroutines(llvm::Module &module,
                                  TargetMachine &targetMachine,
                                  bool optimizeFrame) {
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
  ModulePassManager passes;
  passes.addPass(CoroEarlyPass());
  CGSCCPassManager coroutinePasses;
  coroutinePasses.addPass(CoroSplitPass(optimizeFrame));
  passes.addPass(
      createModuleToPostOrderCGSCCPassAdaptor(std::move(coroutinePasses)));
  passes.addPass(CoroCleanupPass());
  passes.run(module, moduleAnalyses);
  if (verifyModule(module, &errs())) {
    errs() << "obelisk: error: invalid LLVM IR after coroutine lowering\n";
    return failure();
  }
  for (const llvm::Function &function : module)
    if (function.isIntrinsic() &&
        function.getName().starts_with("llvm.coro.") && !function.use_empty()) {
      errs() << "obelisk: error: coroutine lowering left live intrinsic '"
             << function.getName() << "'\n";
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
  // A forced-bytecode executable uses native code only for scheduler glue,
  // runtime entry points, and optional foreign-call thunks. The SystemVerilog
  // bodies have already gone through the requested simulation optimization
  // pipeline before being frozen into bytecode. Running LLVM's aggressive
  // optimizer over their unreachable native fallback copies dominates large
  // library builds without changing bytecode execution, so keep the host
  // shell deliberately cheap.
  uint32_t hostOptLevel = options.bytecode ? 0 : options.optLevel;
  std::unique_ptr<TargetMachine> targetMachine =
      backend->createTargetMachine(targetError, hostOptLevel);
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
  if (failed(lowerToLLVM(
          module, *targetMachine, backend->getTriple(), options.bytecode,
          options.vpi, *nativeScheduler, options.optLevel,
          backend->supportsSemanticPartitions() && !options.bytecode,
          options.timing, requiresStateSync)))
    return failure();
  auto lastBackendTiming = std::chrono::steady_clock::now();
  auto markBackendTiming = [&](StringRef name) {
    if (!options.timing)
      return;
    auto now = std::chrono::steady_clock::now();
    double seconds =
        std::chrono::duration<double>(now - lastBackendTiming).count();
    errs() << "obelisk backend timing: " << name << ": " << seconds << " s\n";
    lastBackendTiming = now;
  };

  std::optional<NativePartitionPlan> nativePartitionPlan;
  if (backend->supportsSemanticPartitions() && !options.bytecode &&
      options.kind == NativeOutputKind::Executable) {
    FailureOr<std::optional<NativePartitionPlan>> plan =
        readNativePartitionPlan(module);
    if (failed(plan))
      return failure();
    nativePartitionPlan = std::move(*plan);
  }

  registerLLVMDialectTranslation(*module.getContext());
  registerBuiltinDialectTranslation(*module.getContext());
  llvm::LLVMContext llvmContext;
  std::unique_ptr<llvm::Module> llvmModule =
      translateModuleToLLVMIR(module, llvmContext, "obelisk");
  if (!llvmModule) {
    errs() << "obelisk: error: LLVM dialect translation failed\n";
    return failure();
  }
  markBackendTiming("MLIR to LLVM translation");
  // Translation has fully consumed the lowered MLIR. Release its operation
  // storage before building LLVM partitions or entering LTO; otherwise large
  // designs retain both complete IR representations through the peak-memory
  // backend phase.
  module.getBody()->clear();
  llvmModule->setTargetTriple(Triple(backend->getTriple()));
  llvmModule->setDataLayout(targetMachine->createDataLayout());
  if (failed(addVPIStartupLifecycle(*llvmModule, options.vpi,
                                    options.sharedLibraryInputs,
                                    requiresStateSync)))
    return failure();
  markBackendTiming("VPI lifecycle materialization");
  bool splitModule = nativePartitionPlan &&
                     shouldSplitNativeModule(*llvmModule, *nativePartitionPlan);
  bool thinLTO = splitModule && options.optLevel != 0 && !options.noLTO;
  if (!splitModule &&
      failed(optimizeLLVMModule(*llvmModule, *targetMachine, hostOptLevel)))
    return failure();

  bool fullLTO = options.kind == NativeOutputKind::Executable &&
                 !options.bytecode && !options.noLTO &&
                 backend->usesFullLTO(options.optLevel) && !thinLTO;
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

  SmallVector<SmallString<256>, 4> moduleTemporaries;
  SmallVector<std::string> modulePaths;
  auto removeTemporaries = [&] {
    for (const SmallString<256> &path : moduleTemporaries)
      sys::fs::remove(path);
  };
  if (splitModule) {
    // LPT balancing removes the generated-function outliers before this
    // point, so two physical groups per requested hardware thread absorb the
    // remaining backend variance without multiplying serial cloning work.
    // ThinLTO, not this planner, owns cross-shard importing and optimization.
    unsigned physicalGroups = std::min<uint32_t>(
        256, std::max<uint32_t>(2, options.compileThreads * 2));
    Expected<NativeModuleSplitPlan> splitPlan = planNativeModuleSplit(
        *llvmModule, *nativePartitionPlan, physicalGroups);
    if (!splitPlan) {
      logAllUnhandledErrors(splitPlan.takeError(), errs(), "obelisk: error: ");
      return failure();
    }
    if (thinLTO) {
      std::vector<std::string> partitionBitcode(splitPlan->groups.size());
      std::vector<double> partitionPreLinkSeconds(splitPlan->groups.size(),
                                                  0.0);
      std::vector<std::string> partitionOutputPaths(splitPlan->groups.size());
      std::vector<std::unique_ptr<TargetMachine>> partitionTargets;
      partitionTargets.reserve(splitPlan->groups.size());
      for (auto [index, group] : llvm::enumerate(splitPlan->groups)) {
        std::unique_ptr<llvm::Module> partition =
            cloneNativeModulePartition(*llvmModule, *splitPlan, group);
        bool nativeObject = splitPlan->nativeObjectGroups.contains(group);
        if (!nativeObject) {
          partition->addModuleFlag(llvm::Module::Error, "EnableSplitLTOUnit",
                                   1);
          partition->addModuleFlag(llvm::Module::Error, "UnifiedLTO", 1);
        }
        SmallVector<char, 0> storage;
        raw_svector_ostream stream(storage);
        WriteBitcodeToFile(*partition, stream);
        partitionBitcode[index].assign(storage.begin(), storage.end());
        FailureOr<SmallString<256>> temporary = makeTemporaryBeside(
            options.outputPath,
            (Twine(".part-") + Twine(index) +
             (nativeObject ? ".o" : ".bc"))
                .str());
        if (failed(temporary)) {
          errs() << "obelisk: error: could not create ThinLTO partition "
                    "temporary\n";
          removeTemporaries();
          return failure();
        }
        moduleTemporaries.push_back(std::move(*temporary));
        partitionOutputPaths[index] = moduleTemporaries.back().str().str();
        std::string error;
        std::unique_ptr<TargetMachine> workerTarget =
            backend->createTargetMachine(error, hostOptLevel);
        if (!workerTarget) {
          errs() << "obelisk: error: could not create ThinLTO pre-link "
                    "target: "
                 << error << '\n';
          removeTemporaries();
          return failure();
        }
        partitionTargets.push_back(std::move(workerTarget));
      }
      llvmModule.reset();
      markBackendTiming("partition serialization and ThinLTO target creation");

      ThreadPoolStrategy strategy =
          hardware_concurrency(std::max<uint32_t>(1, options.compileThreads));
      strategy.Limit = true;
      DefaultThreadPool pool(strategy);
      std::vector<std::shared_future<bool>> futures;
      futures.reserve(partitionBitcode.size());
      std::mutex diagnosticMutex;
      for (size_t index = 0; index != partitionBitcode.size(); ++index)
        futures.push_back(pool.async([&, index] {
          auto workerStart = std::chrono::steady_clock::now();
          llvm::LLVMContext workerContext;
          MemoryBufferRef buffer(partitionBitcode[index],
                                 partitionOutputPaths[index]);
          Expected<std::unique_ptr<llvm::Module>> parsed =
              parseBitcodeFile(buffer, workerContext);
          if (!parsed) {
            std::lock_guard<std::mutex> lock(diagnosticMutex);
            logAllUnhandledErrors(parsed.takeError(), errs(),
                                  "obelisk: error: ThinLTO partition parse "
                                  "failed: ");
            return false;
          }
          // Legalize coroutine state machines before either emitting the
          // ThinLTO summary or compiling an isolated oversized definition.
          // Ordinary shards receive their sole O3/IPO pipeline from LLD;
          // direct shards receive the same local O3 pipeline here once.
          if (failed(lowerLLVMCoroutines(**parsed, *partitionTargets[index],
                                         hostOptLevel != 0)))
            return false;
          bool nativeObject =
              splitPlan->nativeObjectGroups.contains(splitPlan->groups[index]);
          if (nativeObject &&
              failed(optimizeLLVMModule(**parsed, *partitionTargets[index],
                                        hostOptLevel)))
            return false;
          LogicalResult wrotePartition =
              nativeObject
                  ? writeObject(**parsed, *partitionTargets[index],
                                partitionOutputPaths[index],
                                backend->getDescription())
                  : writeBitcode(**parsed, partitionOutputPaths[index]);
          if (failed(wrotePartition)) {
            return false;
          }
          partitionPreLinkSeconds[index] =
              std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                            workerStart)
                  .count();
          return true;
        }));
      pool.wait();
      for (const std::shared_future<bool> &future : futures)
        if (!future.get()) {
          removeTemporaries();
          return failure();
        }
      modulePaths.clear();
      llvm::append_range(modulePaths, partitionOutputPaths);
      if (options.timing)
        for (auto [index, seconds] : llvm::enumerate(partitionPreLinkSeconds))
          errs() << "obelisk backend timing: partition " << index
                 << " coroutine legalization and "
                 << (splitPlan->nativeObjectGroups.contains(
                         splitPlan->groups[index])
                         ? "direct O3 object"
                         : "ThinLTO bitcode")
                 << ": " << seconds << " s\n";
      markBackendTiming("parallel coroutine legalization and shard emission");
    } else {
      // CloneModule retains the source LLVMContext. Clone and serialize one
      // partition at a time, then parse each into a private context so
      // optimization and codegen are safe to run concurrently without ever
      // retaining every clone together.
      std::vector<std::string> partitionBitcode(splitPlan->groups.size());
      std::vector<double> partitionCodegenSeconds(splitPlan->groups.size(),
                                                  0.0);
      std::vector<std::unique_ptr<TargetMachine>> partitionTargets;
      partitionTargets.reserve(splitPlan->groups.size());
      for (auto [index, group] : llvm::enumerate(splitPlan->groups)) {
        std::unique_ptr<llvm::Module> partition =
            cloneNativeModulePartition(*llvmModule, *splitPlan, group);
        SmallVector<char, 0> storage;
        raw_svector_ostream stream(storage);
        WriteBitcodeToFile(*partition, stream);
        partitionBitcode[index].assign(storage.begin(), storage.end());
        FailureOr<SmallString<256>> temporary = makeTemporaryBeside(
            options.outputPath, (Twine(".part-") + Twine(index) + ".o").str());
        if (failed(temporary)) {
          errs() << "obelisk: error: could not create partition temporary\n";
          removeTemporaries();
          return failure();
        }
        moduleTemporaries.push_back(std::move(*temporary));
        modulePaths.push_back(moduleTemporaries.back().str().str());
        std::string error;
        std::unique_ptr<TargetMachine> workerTarget =
            backend->createTargetMachine(error, hostOptLevel);
        if (!workerTarget) {
          errs() << "obelisk: error: could not create partition target: "
                 << error << '\n';
          removeTemporaries();
          return failure();
        }
        partitionTargets.push_back(std::move(workerTarget));
      }
      llvmModule.reset();
      markBackendTiming("partition serialization and target creation");

      ThreadPoolStrategy strategy =
          hardware_concurrency(std::max<uint32_t>(1, options.compileThreads));
      strategy.Limit = true;
      DefaultThreadPool pool(strategy);
      std::vector<std::shared_future<bool>> futures;
      futures.reserve(partitionBitcode.size());
      std::mutex diagnosticMutex;
      for (size_t index = 0; index != partitionBitcode.size(); ++index)
        futures.push_back(pool.async([&, index] {
          auto workerStart = std::chrono::steady_clock::now();
          llvm::LLVMContext workerContext;
          MemoryBufferRef buffer(partitionBitcode[index], modulePaths[index]);
          Expected<std::unique_ptr<llvm::Module>> parsed =
              parseBitcodeFile(buffer, workerContext);
          if (!parsed) {
            std::lock_guard<std::mutex> lock(diagnosticMutex);
            logAllUnhandledErrors(parsed.takeError(), errs(),
                                  "obelisk: error: partition parse failed: ");
            return false;
          }
          if (failed(optimizeLLVMModule(**parsed, *partitionTargets[index],
                                        hostOptLevel)) ||
              failed(writeObject(**parsed, *partitionTargets[index],
                                 modulePaths[index],
                                 backend->getDescription())))
            return false;
          partitionCodegenSeconds[index] =
              std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                            workerStart)
                  .count();
          return true;
        }));
      pool.wait();
      for (const std::shared_future<bool> &future : futures)
        if (!future.get()) {
          removeTemporaries();
          return failure();
        }
      if (options.timing)
        for (auto [index, seconds] : llvm::enumerate(partitionCodegenSeconds))
          errs() << "obelisk backend timing: partition " << index
                 << " optimization and codegen: " << seconds << " s\n";
      markBackendTiming("parallel partition optimization and codegen");
    }
  } else {
    FailureOr<SmallString<256>> moduleTemporary =
        makeTemporaryBeside(options.outputPath, fullLTO ? ".bc" : ".o");
    if (failed(moduleTemporary)) {
      errs() << "obelisk: error: could not create temporary target module\n";
      return failure();
    }
    moduleTemporaries.push_back(std::move(*moduleTemporary));
    LogicalResult wroteModule =
        fullLTO
            ? writeBitcode(*llvmModule, moduleTemporaries.back())
            : writeObject(*llvmModule, *targetMachine, moduleTemporaries.back(),
                          backend->getDescription());
    if (failed(wroteModule)) {
      removeTemporaries();
      return failure();
    }
    modulePaths.push_back(moduleTemporaries.back().str().str());
  }
  if (options.kind == NativeOutputKind::Object) {
    if (moduleTemporaries.size() != 1 ||
        failed(
            atomicallyReplace(moduleTemporaries.front(), options.outputPath))) {
      removeTemporaries();
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
    removeTemporaries();
    return failure();
  }
  LogicalResult linked = backend->linkExecutable(
      modulePaths, options.outputPath, *support, options, thinLTO);
  markBackendTiming("native link");
  removeTemporaries();
  return linked;
}

} // namespace obelisk::driver
