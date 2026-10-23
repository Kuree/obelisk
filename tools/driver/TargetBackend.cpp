//===- TargetBackend.cpp - Target-independent code generation ------------===//
//
// The pipeline every target shares: MLIR lowering, LLVM translation, the VPI
// and state-synchronization lifecycle, optimization, and writing an object or
// bitcode. Only target initialization, the target machine and the link differ,
// and those are dispatched through TargetBackend.
//
//===----------------------------------------------------------------------===//

#include "TargetBackend.h"
#include "obelisk/Conversion/SimulationToSchedule.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Runtime/Runtime.h"

#include "BackendUtils.h"
#include "NativeBackend.h"
#include "NativeExecutionCounts.h"
#include "NativeGlobalInitializers.h"
#include "NativeModulePruning.h"
#include "NativePartitionCost.h"
#include "PassIRDump.h"
#include "WasmBackend.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Conversion/Passes.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/LLVM/ClockedGuardLayout.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"
#include "obelisk/Runtime/OutputItemFlags.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
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
#include "llvm/Support/MathExtras.h"
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
#if defined(__GLIBC__)
#include <malloc.h>
#endif
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

FailureOr<std::string> getSharedLibraryRunpath(StringRef outputPath,
                                               StringRef suppliedDirectory,
                                               bool suppliedPathWasAbsolute,
                                               StringRef origin) {
  SmallString<256> directory(suppliedDirectory);
  if (suppliedPathWasAbsolute) {
    sys::path::remove_dots(directory, true);
    return directory.str().str();
  }
  SmallString<256> image(outputPath);
  if (std::error_code error = sys::fs::make_absolute(image)) {
    errs() << "obelisk: error: could not resolve output directory for "
              "runpath generation: "
           << error.message() << '\n';
    return failure();
  }
  sys::path::remove_dots(image, true);
  sys::path::remove_filename(image);
  if (std::error_code error = sys::fs::make_absolute(directory)) {
    errs() << "obelisk: error: could not resolve shared-library directory '"
           << suppliedDirectory << "': " << error.message() << '\n';
    return failure();
  }
  sys::path::remove_dots(directory, true);
  // Both paths are absolute and normalized: drop their common prefix, then
  // climb out of the rest of the image directory.
  auto from = sys::path::begin(image), fromEnd = sys::path::end(image);
  auto to = sys::path::begin(directory), toEnd = sys::path::end(directory);
  for (; from != fromEnd && to != toEnd && *from == *to; ++from, ++to) {
  }
  std::string runpath = origin.str();
  for (; from != fromEnd; ++from)
    runpath += "/..";
  for (; to != toEnd; ++to)
    runpath += ("/" + *to).str();
  return runpath;
}

ArrayRef<StringLiteral> getRuntimeDPIEntryPoints() {
  static constexpr StringLiteral names[] = {
      "svDpiVersion",       "svGetBitselBit",    "svGetBitselLogic",
      "svPutBitselBit",     "svPutBitselLogic",  "svGetPartselBit",
      "svGetPartselLogic",  "svPutPartselBit",   "svPutPartselLogic",
      "svGetScope",         "svSetScope",        "svGetNameFromScope",
      "svGetScopeFromName", "svPutUserData",     "svGetUserData",
      "svGetCallerInfo",    "svIsDisabledState", "svAckDisabledState",
      "svGetTime",          "svGetTimeUnit",     "svGetTimePrecision"};
  return names;
}

ArrayRef<StringLiteral> getRuntimeVPIEntryPoints() {
  static constexpr StringLiteral names[] = {
      "vpi_chk_error",
      "vpi_compare_objects",
      "vpi_control",
      "vpi_flush",
      "vpi_free_object",
      "vpi_get",
      "vpi_get64",
      "vpi_get_cb_info",
      "vpi_get_data",
      "vpi_get_delays",
      "vpi_get_str",
      "vpi_get_systf_info",
      "vpi_get_time",
      "vpi_get_userdata",
      "vpi_get_value",
      "vpi_get_value_array",
      "vpi_get_vlog_info",
      "vpi_handle",
      "vpi_handle_by_index",
      "vpi_handle_by_multi_index",
      "vpi_handle_by_name",
      "vpi_handle_multi",
      "vpi_iterate",
      "vpi_mcd_name",
      "vpi_printf",
      "vpi_put_userdata",
      "vpi_put_value",
      "vpi_register_cb",
      "vpi_register_systf",
      "vpi_release_handle",
      "vpi_remove_cb",
      "vpi_scan",
      "vpi_vprintf",
      // Obelisk's object-model query for VPI applications.
      "obelisk_rt_v1_vpi_object_model"};
  return names;
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

// Return pages freed by a discarded IR representation to the system before
// the parallel backend phase. glibc keeps them in the freeing thread's arena,
// where partition workers, which allocate from their own arenas, cannot reuse
// them; the whole design's MLIR would otherwise stay resident at peak.
void releaseFreedHeapPages() {
#if defined(__GLIBC__)
  malloc_trim(0);
#endif
}

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
  auto manifest = native::get<native::PhysicalPartitionManifest>(module);
  if (!manifest)
    return std::optional<NativePartitionPlan>{};
  NativePartitionPlan plan;
  for (mlir::Attribute entryAttr : manifest) {
    auto entry = dyn_cast<native::PartitionAttr>(entryAttr);
    if (!entry)
      return module.emitError("has malformed physical partition manifest"),
             failure();
    NativePartition &partition = plan.partitions.emplace_back();
    partition.id = entry.getId().str();
    for (mlir::Attribute memberAttr : entry.getMembers()) {
      auto member = dyn_cast<FlatSymbolRefAttr>(memberAttr);
      if (!member)
        return module.emitError("has malformed physical partition member"),
               failure();
      partition.members.push_back(member.getValue().str());
    }
    for (mlir::Attribute exportAttr : entry.getExports()) {
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

Expected<NativeModuleSplitPlan>
planNativeModuleSplit(llvm::Module &module, const NativePartitionPlan &plan) {
  // The semantic manifest supplies stable ownership and dependency identity,
  // but an owner can contain a huge generated class or tens of thousands of
  // constants.  Treating every owner as an indivisible physical shard leaves
  // most ThinLTO workers idle behind a few outliers.  Form stable owner-local
  // definition units and use deterministic longest-processing-time packing.
  // ThinLTO's global index remains responsible for cross-unit importing and
  // whole-program optimization.
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
  uint64_t instructionCount = 0;
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
      uint64_t instructions = function->getInstructionCount();
      instructionCount += instructions;
      unit.weight = std::max<uint64_t>(1, instructions);
      // Keep only genuinely exceptional bodies out of ThinLTO. Medium-sized
      // generated functions still benefit from importing small helpers, and
      // the wide-packed RMW patterns that previously poisoned instruction
      // selection are removed before translation.
      unit.nativeObject = instructions > 20000;
    } else if (auto *global = dyn_cast<llvm::GlobalVariable>(&value))
      unit.weight = detail::estimateNativeGlobalWeight(*global);
  }
  llvm::sort(units, [](const SplitUnit &lhs, const SplitUnit &rhs) {
    if (lhs.nativeObject != rhs.nativeObject)
      return lhs.nativeObject > rhs.nativeObject;
    return lhs.weight != rhs.weight ? lhs.weight > rhs.weight
                                    : lhs.key < rhs.key;
  });
  // Partition boundaries affect inlining and optimization even with ThinLTO.
  // Derive them from the input rather than the worker count so concurrency
  // changes only scheduling. Keep large designs out of enormous optimization
  // units, while bounding the serial clone/serialization cost.
  unsigned sizeGroups = static_cast<unsigned>(std::min<uint64_t>(
      16, llvm::divideCeil(instructionCount, UINT64_C(100000))));
  unsigned groupCount =
      std::min<unsigned>(std::max(2u, sizeGroups), units.size());
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
    // Spread those definitions without consuming every group and disabling
    // ThinLTO for the ordinary importable body of the design.
    nativeGroupCount = std::min(nativeUnitCount, std::max(1u, groupCount / 3));
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
  // A direct-O3 outlier is outside the ThinLTO index, so nothing can be
  // imported into it: every call it makes to a small helper defined in another
  // group stays an out-of-line call with full argument marshalling. Keep such
  // helpers in the group of the outlier that calls them most; the helper's
  // other callers still reach it through the linker.
  if (nativeUnitCount != 0 && nativeUnitCount != units.size()) {
    llvm::MapVector<const llvm::GlobalValue *, std::pair<unsigned, uint64_t>>
        affinity;
    for (unsigned index = 0; index != nativeUnitCount; ++index) {
      const auto *caller = dyn_cast<llvm::Function>(units[index].value);
      if (!caller)
        continue;
      unsigned group = assignments.lookup(caller);
      llvm::MapVector<const llvm::Function *, uint64_t> callCounts;
      for (const llvm::Instruction &instruction :
           llvm::instructions(*caller)) {
        const auto *call = dyn_cast<llvm::CallBase>(&instruction);
        const llvm::Function *callee =
            call ? call->getCalledFunction() : nullptr;
        if (callee && callee != caller && !callee->isDeclaration() &&
            callee->getInstructionCount() <= 256)
          ++callCounts[callee];
      }
      for (const auto &[callee, count] : callCounts) {
        auto [entry, inserted] = affinity.insert({callee, {group, count}});
        if (!inserted && count > entry->second.second)
          entry->second = {group, count};
      }
    }
    for (const auto &[callee, choice] : affinity) {
      auto found = assignments.find(callee);
      if (found != assignments.end() &&
          !nativeObjectGroups.contains(found->second))
        found->second = choice.first;
    }
  }

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

// Declares, on first use, a global that a cloned partition definition
// references but does not own. CloneModule instead declares every symbol of
// the source module in every partition and deletes the unused ones afterwards,
// which costs O(partitions * whole-program symbols) for a large design.
class PartitionDeclarationMaterializer final : public ValueMaterializer {
public:
  PartitionDeclarationMaterializer(llvm::Module &partition,
                                   ValueToValueMapTy &mapping)
      : partition(partition), mapping(mapping) {}

  llvm::Value *materialize(llvm::Value *value) override {
    auto *global = dyn_cast<llvm::GlobalValue>(value);
    if (!global)
      return nullptr;
    llvm::GlobalValue *declaration;
    if (global->getValueType()->isFunctionTy()) {
      llvm::Function *function = llvm::Function::Create(
          cast<llvm::FunctionType>(global->getValueType()),
          llvm::GlobalValue::ExternalLinkage, global->getAddressSpace(),
          global->getName(), &partition);
      if (auto *source = dyn_cast<llvm::Function>(global)) {
        function->copyAttributesFrom(source);
        // A personality belongs to a definition, not to this declaration.
        function->setPersonalityFn(nullptr);
      }
      declaration = function;
    } else {
      auto *source = dyn_cast<llvm::GlobalVariable>(global);
      auto *variable = new llvm::GlobalVariable(
          partition, global->getValueType(), source && source->isConstant(),
          llvm::GlobalValue::ExternalLinkage, nullptr, global->getName(),
          nullptr, global->getThreadLocalMode(), global->getAddressSpace());
      if (source)
        variable->copyAttributesFrom(source);
      declaration = variable;
    }
    // An alias or another partition's definition is an external reference
    // here; a source declaration keeps its own linkage, e.g. extern_weak.
    declaration->setLinkage(isa<llvm::GlobalObject>(global) &&
                                    global->isDeclaration()
                                ? global->getLinkage()
                                : llvm::GlobalValue::ExternalLinkage);
    mapping[global] = declaration;
    if (auto *object = dyn_cast<llvm::GlobalObject>(global)) {
      SmallVector<std::pair<unsigned, llvm::MDNode *>, 1> attachments;
      object->getAllMetadata(attachments);
      for (auto [kind, node] : attachments)
        cast<llvm::GlobalObject>(declaration)
            ->addMetadata(kind,
                          *MapMetadata(node, mapping, RF_None, nullptr, this));
    }
    return declaration;
  }

private:
  llvm::Module &partition;
  ValueToValueMapTy &mapping;
};

void copyPartitionComdat(llvm::GlobalObject *destination,
                         const llvm::GlobalObject *source) {
  const llvm::Comdat *comdat = source->getComdat();
  if (!comdat)
    return;
  llvm::Comdat *copy =
      destination->getParent()->getOrInsertComdat(comdat->getName());
  copy->setSelectionKind(comdat->getSelectionKind());
  destination->setComdat(copy);
}

// Clones the definitions assigned to `group`, declaring only the globals they
// reference (see PartitionDeclarationMaterializer).
std::unique_ptr<llvm::Module>
cloneNativeModulePartition(llvm::Module &module,
                           const NativeModuleSplitPlan &plan, unsigned group) {
  auto result = std::make_unique<llvm::Module>(
      (Twine(module.getModuleIdentifier()) + ".partition." + Twine(group))
          .str(),
      module.getContext());
  result->setSourceFileName(module.getSourceFileName());
  result->setDataLayout(module.getDataLayout());
  result->setTargetTriple(module.getTargetTriple());
  result->setModuleInlineAsm(module.getModuleInlineAsm());
  ValueToValueMapTy mapping;
  PartitionDeclarationMaterializer materializer(*result, mapping);
  auto selected = [&](const llvm::GlobalValue &value) {
    auto found = plan.assignments.find(&value);
    return found != plan.assignments.end() && found->second == group;
  };

  // Create every owned definition before cloning a body or initializer, so
  // references among them map to definitions rather than declarations.
  SmallVector<const llvm::GlobalVariable *> variables;
  SmallVector<const llvm::Function *> functions;
  SmallVector<const llvm::GlobalAlias *> aliases;
  SmallVector<const llvm::GlobalIFunc *> ifuncs;
  for (const llvm::GlobalVariable &source : module.globals()) {
    if (!selected(source))
      continue;
    auto *variable = new llvm::GlobalVariable(
        *result, source.getValueType(), source.isConstant(),
        source.getLinkage(), nullptr, source.getName(), nullptr,
        source.getThreadLocalMode(), source.getAddressSpace());
    variable->copyAttributesFrom(&source);
    mapping[&source] = variable;
    variables.push_back(&source);
  }
  for (const llvm::Function &source : module) {
    if (!selected(source))
      continue;
    llvm::Function *function = llvm::Function::Create(
        source.getFunctionType(), source.getLinkage(), source.getAddressSpace(),
        source.getName(), result.get());
    function->copyAttributesFrom(&source);
    mapping[&source] = function;
    functions.push_back(&source);
  }
  for (const llvm::GlobalAlias &source : module.aliases()) {
    if (!selected(source))
      continue;
    llvm::GlobalAlias *alias = llvm::GlobalAlias::create(
        source.getValueType(), source.getAddressSpace(), source.getLinkage(),
        source.getName(), result.get());
    alias->copyAttributesFrom(&source);
    mapping[&source] = alias;
    aliases.push_back(&source);
  }
  for (const llvm::GlobalIFunc &source : module.ifuncs()) {
    if (!selected(source))
      continue;
    llvm::GlobalIFunc *ifunc = llvm::GlobalIFunc::create(
        source.getValueType(), source.getAddressSpace(), source.getLinkage(),
        source.getName(), nullptr, result.get());
    ifunc->copyAttributesFrom(&source);
    mapping[&source] = ifunc;
    ifuncs.push_back(&source);
  }

  for (const llvm::GlobalVariable *source : variables) {
    auto *variable = cast<llvm::GlobalVariable>(mapping[source]);
    SmallVector<std::pair<unsigned, llvm::MDNode *>, 1> attachments;
    source->getAllMetadata(attachments);
    for (auto [kind, node] : attachments)
      variable->addMetadata(
          kind, *MapMetadata(node, mapping, RF_None, nullptr, &materializer));
    if (source->hasInitializer())
      variable->setInitializer(MapValue(source->getInitializer(), mapping,
                                        RF_None, nullptr, &materializer));
    copyPartitionComdat(variable, source);
  }
  for (const llvm::Function *source : functions) {
    auto *function = cast<llvm::Function>(mapping[source]);
    auto argument = function->arg_begin();
    for (const llvm::Argument &sourceArgument : source->args()) {
      argument->setName(sourceArgument.getName());
      mapping[&sourceArgument] = &*argument++;
    }
    SmallVector<llvm::ReturnInst *, 8> returns;
    CloneFunctionInto(function, source, mapping,
                      CloneFunctionChangeType::ClonedModule, returns, "",
                      nullptr, nullptr, &materializer);
    if (source->hasPersonalityFn())
      function->setPersonalityFn(MapValue(source->getPersonalityFn(), mapping,
                                          RF_None, nullptr, &materializer));
    copyPartitionComdat(function, source);
  }
  for (const llvm::GlobalAlias *source : aliases)
    if (const llvm::Constant *aliasee = source->getAliasee())
      cast<llvm::GlobalAlias>(mapping[source])
          ->setAliasee(
              MapValue(aliasee, mapping, RF_None, nullptr, &materializer));
  for (const llvm::GlobalIFunc *source : ifuncs)
    if (const llvm::Constant *resolver = source->getResolver())
      cast<llvm::GlobalIFunc>(mapping[source])
          ->setResolver(
              MapValue(resolver, mapping, RF_None, nullptr, &materializer));
  for (const llvm::NamedMDNode &source : module.named_metadata()) {
    llvm::NamedMDNode *node =
        result->getOrInsertNamedMetadata(source.getName());
    for (const llvm::MDNode *operand : source.operands())
      node->addOperand(
          MapMetadata(operand, mapping, RF_None, nullptr, &materializer));
  }
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

static bool literalFormatMayReadNetStrength(StringRef format) {
  size_t offset = 0;
  while ((offset = format.find('%', offset)) != StringRef::npos) {
    ++offset;
    if (offset == format.size())
      return false;
    if (format[offset] == '%') {
      ++offset;
      continue;
    }
    for (; offset != format.size(); ++offset) {
      char character = format[offset];
      if (character == 'v' || character == 'V')
        return true;
      if (llvm::isAlpha(character)) {
        ++offset;
        break;
      }
    }
  }
  return false;
}

static bool formattedOutputMayReadNetStrength(ValueRange items,
                                              ArrayRef<int32_t> itemFlags,
                                              bool literalBytesAreFormats) {
  if (!llvm::any_of(itemFlags, [](int32_t flags) {
        return (flags & OBELISK_RT_OUTPUT_ITEM_NET) != 0;
      }))
    return false;

  unsigned itemIndex = 0;
  for (int32_t flags : itemFlags) {
    if ((flags & OBELISK_RT_OUTPUT_ITEM_OMITTED) != 0)
      continue;
    if (itemIndex == items.size())
      return true;
    mlir::Value item = items[itemIndex++];
    if ((flags & (OBELISK_RT_OUTPUT_ITEM_ENUM | OBELISK_RT_OUTPUT_ITEM_NET)) !=
        0) {
      if (itemIndex == items.size())
        return true;
      ++itemIndex;
      continue;
    }
    bool explicitFormat = (flags & (OBELISK_RT_OUTPUT_ITEM_DESIGNATED_FORMAT |
                                    OBELISK_RT_OUTPUT_ITEM_FORMAT)) != 0;
    if (isa<obelisk::sim::StringType>(item.getType()) && explicitFormat)
      return true;
    if (!isa<obelisk::sim::BytesType>(item.getType()) ||
        (!literalBytesAreFormats && !explicitFormat))
      continue;
    auto constant = item.getDefiningOp<obelisk::sim::SimBytesConstantOp>();
    if (!constant || literalFormatMayReadNetStrength(constant.getValue()))
      return true;
  }
  return false;
}

LogicalResult
lowerToLLVM(ModuleOp module, TargetMachine &targetMachine, StringRef triple,
            bool bytecode, StringRef vpi, StringRef bytecodeScope,
            obelisk::schedule::NativeSchedulerMode nativeScheduler,
            uint32_t optLevel, bool planSemanticPartitions, bool timing,
            bool verifyEach, const PassIRDumpOptions &irDump,
            bool &requiresStateSync, bool &resolveInitialDrivers) {
  if (bytecode &&
      nativeScheduler == obelisk::schedule::NativeSchedulerMode::Auto)
    nativeScheduler = obelisk::schedule::NativeSchedulerMode::Generic;
  module->setAttr("llvm.target_triple",
                  StringAttr::get(module.getContext(), triple));
  module->setAttr(
      "llvm.data_layout",
      StringAttr::get(
          module.getContext(),
          targetMachine.createDataLayout().getStringRepresentation()));
  native::set<native::OptimizationLevel>(
      module, IntegerAttr::get(mlir::IntegerType::get(module.getContext(), 32),
                               optLevel));
  mlir::PassManager manager(module.getContext());
  // Coroutine lowering verifies its final module explicitly. Avoid repeating
  // whole-module verification at every intermediate preparation stage.
  manager.enableVerifier(verifyEach);
  if (timing)
    manager.enableTiming();
  addPassIRDump(manager, irDump);
  if (timing)
    native::mark<native::DebugTiming>(module);
  else
    native::remove<native::DebugTiming>(module);
  // Export bridges must be part of both executable representations. The
  // materializer is a no-op for ordinary designs, preserving their exact IR.
  if (dpi::has<dpi::HasExports>(module) &&
      failed(materializeDPIExportBridges(module)))
    return failure();
  bool hasLanguageOverride = false;
  bool hasDriverNBA = false;
  bool hasDelayedNet = false;
  bool hasInertialDriver = false;
  bool hasPassSwitch = false;
  bool hasClockOccurrenceCondition = false;
  module.walk([&](mlir::Operation *operation) {
    if (mlir::isa<obelisk::sim::SimOverrideOp,
                  obelisk::sim::SimDynamicOverrideOp,
                  obelisk::sim::SimReleaseOverrideOp>(operation))
      hasLanguageOverride = true;
    if (auto enqueue = mlir::dyn_cast<obelisk::sim::SimNBAEnqueueOp>(operation))
      hasDriverNBA |= mlir::isa<obelisk::sim::DriverType>(
          enqueue.getDestination().getType());
    if (auto net = mlir::dyn_cast<obelisk::sim::SimNetDeclOp>(operation))
      hasDelayedNet |= static_cast<bool>(net.getPropagationDelays());
    hasInertialDriver |=
        mlir::isa<obelisk::sim::SimDriverDriveInertialOp,
                  obelisk::sim::SimDriverDriveInertialPathOp,
                  obelisk::sim::SimDriverDriveInertialStrengthPairOp,
                  obelisk::sim::SimDriverDriveInertialPathStrengthPairOp>(
            operation);
    hasPassSwitch |= mlir::isa<obelisk::sim::SimPassSwitchDeclOp>(operation);
    if (auto wait =
            mlir::dyn_cast<obelisk::sim::SimSuspendClockSetOp>(operation))
      hasClockOccurrenceCondition |= !wait.getConditions().empty();
  });
  requiresStateSync = vpi != "off" || hasLanguageOverride || hasDriverNBA ||
                      hasDelayedNet || hasInertialDriver ||
                      hasClockOccurrenceCondition;
  resolveInitialDrivers = hasInertialDriver;
  ::obelisk::schedule::set<::obelisk::schedule::Field::NativeScheduler>(
      module, obelisk::schedule::NativeSchedulerModeAttr::get(
                  module.getContext(), nativeScheduler));
  // Hybrid AOT keeps bytecode available as the canonical implementation for
  // fragments that cannot be scheduled statically and for writable VPI
  // transition stages. The shared process frame lets those fragments return
  // to native execution at a continuation boundary without copying state.
  bool evalScheduler =
      nativeScheduler == obelisk::schedule::NativeSchedulerMode::Eval;
  bool needsHybridBytecode =
      nativeScheduler != obelisk::schedule::NativeSchedulerMode::Generic;
  bool needsSampledStatePlan = false;
  module.walk(
      [&](obelisk::sim::SimSampledReadOp) { needsSampledStatePlan = true; });
  bool needsWaveformMetadata = false;
  bool needsNetDriverTopology = hasPassSwitch;
  module.walk([&](mlir::Operation *operation) {
    needsWaveformMetadata |= mlir::isa<
        obelisk::sim::SimDumpOpenOp, obelisk::sim::SimDumpOpenStringOp,
        obelisk::sim::SimDumpTimescaleOp, obelisk::sim::SimDumpVarsOp,
        obelisk::sim::SimDumpAllOp, obelisk::sim::SimDumpControlOp,
        obelisk::sim::SimDumpLimitOp, obelisk::sim::SimDumpFlushOp,
        obelisk::sim::SimDumpPortsOp, obelisk::sim::SimDumpPortsControlOp>(
        operation);
    needsNetDriverTopology |=
        mlir::isa<obelisk::sim::SimNetCountDriversOp>(operation);
    if (auto display = mlir::dyn_cast<obelisk::sim::SimDisplayOp>(operation))
      needsNetDriverTopology |= formattedOutputMayReadNetStrength(
          display.getItems(), display.getItemFlags(), true);
    if (auto format =
            mlir::dyn_cast<obelisk::sim::SimStringOutputFormatOp>(operation))
      needsNetDriverTopology |= formattedOutputMayReadNetStrength(
          format.getItems(), format.getItemFlags(), false);
  });
  // The query reads raw driver contributions through the runtime. Bind the
  // generated native planes even when no force/VPI feature otherwise needs
  // canonical state synchronization.
  requiresStateSync |= needsNetDriverTopology;
  bool needsDesignEncoding = bytecode || bytecodeScope == "all" ||
                             needsHybridBytecode || vpi != "off" ||
                             hasLanguageOverride || needsWaveformMetadata ||
                             hasDriverNBA || hasDelayedNet ||
                             hasInertialDriver || needsNetDriverTopology;
  bool needsStandaloneStatePlan =
      needsSampledStatePlan || hasClockOccurrenceCondition;
  requiresStateSync |= needsStandaloneStatePlan && !needsDesignEncoding;
  // Both tiers share the polls that bound garbage between collections, so
  // place them before bytecode is frozen.
  manager.addPass(createObeliskSimInsertGCPollsPass());
  // The storage bits expected to hold X/Z in steady state are inferred from
  // the canonical design, which bytecode freezes next; native versioning
  // loads them as a constant X while the unknown summary confirms them.
  if (optLevel > 0) {
    ObeliskSimAnnotateSteadyUnknownPassOptions steadyUnknown;
    steadyUnknown.vpi = vpi.str();
    manager.nest<obelisk::sim::SimDesignOp>().addPass(
        createObeliskSimAnnotateSteadyUnknownPass(steadyUnknown));
  }
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
    options.pruneNative = bytecodeScope == "required" && needsHybridBytecode;
    manager.addPass(createEncodeObeliskSimToBytecodePass(options));
  } else if (needsStandaloneStatePlan) {
    SmallVector<obelisk::sim::SimDesignOp> designs;
    module.walk(
        [&](obelisk::sim::SimDesignOp design) { designs.push_back(design); });
    if (designs.size() != 1)
      return module.emitError(
          "native-state planning requires exactly one simulation design");
    FailureOr<SimulationSampledStatePlan> plan =
        planSimulationSampledState(designs.front());
    if (failed(plan))
      return failure();
    OpBuilder builder(module.getContext());
    runtime::setExecutionFlags(
        module, static_cast<runtime::ExecutionFlags>(plan->executionFlags));
    runtime::set<runtime::StateBits>(
        module, builder.getI64IntegerAttr(plan->stateBitCount));
    SmallVector<int64_t> sampledRanges;
    sampledRanges.reserve(plan->ranges.size() * 2);
    for (const SimulationSampledRange &range : plan->ranges) {
      sampledRanges.push_back(static_cast<int64_t>(range.bitOffset));
      sampledRanges.push_back(static_cast<int64_t>(range.bitWidth));
    }
    runtime::set<runtime::SampledRanges>(
        module, builder.getDenseI64ArrayAttr(sampledRanges));
  }
  // Native region bodies may diverge from their already-frozen bytecode
  // fallback after this point. Keep the generic scheduler as an untouched
  // oracle and apply AOT-only next-state rewrites only when the hybrid image
  // provides the deoptimization implementation.
  if (needsHybridBytecode || evalScheduler)
    manager.nest<sim::SimDesignOp>().addPass(
        createObeliskSimOptimizeNativeRegionsPass());
  // Partition metadata is a native ELF object/ThinLTO contract. In
  // particular, wasm32 keeps its current single-module lowering and staged
  // wasm-object runtime even when the source design is large.
  if (planSemanticPartitions)
    manager.nest<obelisk::sim::SimDesignOp>().addPass(
        createObeliskSimPlanNativePartitionsPass());
  buildSimulationToLLVMCoroutinePipeline(manager);
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
  // Layout passes read the optimized shape of the kernels; see
  // obelisk/LLVM/ClockedGuardLayout.h.
  ClockedGuardLayoutPass().run(module, moduleAnalyses);
  if (verifyModule(module, &errs())) {
    errs() << "obelisk: error: invalid LLVM IR after native optimization\n";
    return failure();
  }
  return success();
}

/// Preserve the native subtarget across serialized bitcode and LTO. A target
/// machine controls the local optimization pipeline, but LLD reconstructs its
/// own target machines when it consumes bitcode. Match Clang's -march=native
/// contract by putting the detected CPU and feature set on every executable
/// function as well.
static void
applyNativeFunctionTargetAttributes(llvm::Module &module,
                                    const TargetMachine &targetMachine) {
  StringRef cpu = targetMachine.getTargetCPU();
  StringRef features = targetMachine.getTargetFeatureString();
  for (llvm::Function &function : module) {
    if (function.isIntrinsic() || function.isDeclaration())
      continue;
    function.addFnAttr("target-cpu", cpu);
    function.addFnAttr("target-features", features);
  }
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
                       bool requiresStateSync, bool resolveInitialDrivers) {
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
  if (!main || !current || !stateValue) {
    errs() << "obelisk: error: native state synchronization requires a "
              "generated scheduler main\n";
    return failure();
  }
  llvm::CallBase *spawn = nullptr;
  llvm::CallBase *report = nullptr;
  llvm::CallBase *destroy = nullptr;
  for (llvm::BasicBlock &block : *main)
    for (llvm::Instruction &instruction : block)
      if (auto *call = dyn_cast<llvm::CallBase>(&instruction))
        if (llvm::Function *callee = call->getCalledFunction()) {
          if (callee->getName().ends_with(".__obelisk_spawn"))
            spawn = call;
          else if (callee->getName() == "obelisk_rt_v1_scheduler_report_status")
            report = call;
        }
  if (report)
    for (llvm::Instruction &instruction : *report->getParent())
      if (auto *call = dyn_cast<llvm::CallBase>(&instruction))
        if (llvm::Function *callee = call->getCalledFunction())
          if (callee->getName() == "obelisk_rt_v1_context_destroy")
            destroy = call;
  auto *returnStatus =
      destroy
          ? dyn_cast<llvm::ReturnInst>(destroy->getParent()->getTerminator())
          : nullptr;
  if (!spawn || (enableVPI && (!report || !destroy || !returnStatus ||
                               report->arg_size() != 2 ||
                               returnStatus->getNumOperands() != 1))) {
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
  auto *unknownArray =
      stateUnknown ? dyn_cast<llvm::ArrayType>(stateUnknown->getValueType())
                   : nullptr;
  if (!valueArray ||
      (stateUnknown && (!unknownArray || valueArray->getNumElements() !=
                                             unknownArray->getNumElements()))) {
    errs() << "obelisk: error: generated native state planes disagree\n";
    return failure();
  }
  llvm::GlobalVariable *execution =
      module.getNamedGlobal("__obelisk_execution_descriptor_v1");
  auto *executionInitializer =
      execution ? dyn_cast<llvm::ConstantStruct>(execution->getInitializer())
                : nullptr;
  // Sampled designs store the descriptor and its extension in one aggregate
  // so `reserved` can be a target-layout-relative offset on wasm32. Peel that
  // storage wrapper before inspecting descriptor fields.
  auto isExecutionDescriptor = [](llvm::ConstantStruct *value) {
    auto *type = value ? dyn_cast<llvm::StructType>(value->getType()) : nullptr;
    return type && type->getNumElements() == 17 &&
           value->getNumOperands() == 17 &&
           type->getElementType(0)->isIntegerTy(32) &&
           type->getElementType(1)->isIntegerTy(32) &&
           type->getElementType(7)->isIntegerTy(64);
  };
  if (!isExecutionDescriptor(executionInitializer) && executionInitializer &&
      executionInitializer->getNumOperands() == 2) {
    auto *nested =
        dyn_cast<llvm::ConstantStruct>(executionInitializer->getOperand(0));
    auto *extension =
        dyn_cast<llvm::ConstantStruct>(executionInitializer->getOperand(1));
    // The extension is deliberately an evolving v1 record.  Its field count
    // is not part of the storage-wrapper contract; the descriptor in element
    // zero is sufficient to identify the generated aggregate.  Requiring a
    // particular extension shape here made adding an in-place v1 field look
    // like a malformed state descriptor.
    if (isExecutionDescriptor(nested) && extension)
      executionInitializer = nested;
  }
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
  auto *executionFlags =
      executionInitializer
          ? dyn_cast<llvm::ConstantInt>(executionInitializer->getOperand(1))
          : nullptr;
  if (!stateUnknown && (!executionFlags || !(executionFlags->getZExtValue() &
                                             OBELISK_RT_EXECUTION_TWO_STATE))) {
    errs()
        << "obelisk: error: four-state execution requires an unknown plane\n";
    return failure();
  }
  llvm::Constant *unknownPointer =
      stateUnknown
          ? static_cast<llvm::Constant *>(stateUnknown)
          : llvm::ConstantPointerNull::get(cast<llvm::PointerType>(pointer));
  if (requiresStateSync) {
    llvm::FunctionCallee sync = module.getOrInsertFunction(
        "obelisk_rt_v1_native_state_sync",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context),
                                {pointer, pointer, pointer, i64}, false));
    llvm::Value *syncStatus = beforeSpawn.CreateCall(
        sync,
        {runtimeContext, stateValue, unknownPointer,
         llvm::ConstantInt::get(i64, stateBitCount->getZExtValue())},
        "obelisk.state.sync");
    beforeSpawn.CreateCall(fail, {runtimeContext, syncStatus});
  }
  if (resolveInitialDrivers) {
    llvm::FunctionCallee resolve = module.getOrInsertFunction(
        "obelisk_rt_v1_scheduler_resolve_initial_drivers",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {pointer},
                                false));
    llvm::Value *resolveStatus = beforeSpawn.CreateCall(
        resolve, {runtimeContext}, "obelisk.initial.drivers");
    beforeSpawn.CreateCall(fail, {runtimeContext, resolveStatus});
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
    llvm::FunctionCallee endCompile = module.getOrInsertFunction(
        "obelisk_rt_v1_vpi_end_compile",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {pointer},
                                false));
    status = beforeSpawn.CreateCall(endCompile, {runtimeContext},
                                    "obelisk.vpi.end_compile");
    beforeSpawn.CreateCall(fail, {runtimeContext, status});
    llvm::FunctionCallee startSimulation = module.getOrInsertFunction(
        "obelisk_rt_v1_vpi_start_simulation",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {pointer},
                                false));
    status = beforeSpawn.CreateCall(startSimulation, {runtimeContext},
                                    "obelisk.vpi.start_simulation");
    beforeSpawn.CreateCall(fail, {runtimeContext, status});
    llvm::FunctionCallee endSimulation = module.getOrInsertFunction(
        "obelisk_rt_v1_vpi_end_simulation",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {pointer},
                                false));
    llvm::FunctionCallee shutdown = module.getOrInsertFunction(
        "obelisk_rt_v1_vpi_shutdown",
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {pointer},
                                false));
    llvm::IRBuilder<> beforeReport(report);
    llvm::Value *endStatus =
        beforeReport.CreateCall(endSimulation, {report->getArgOperand(0)},
                                "obelisk.vpi.end_simulation");
    llvm::Value *runStatus = report->getArgOperand(1);
    llvm::Value *runFailed = beforeReport.CreateICmpNE(
        runStatus, llvm::ConstantInt::get(runStatus->getType(), 0),
        "obelisk.vpi.run_failed");
    llvm::Value *finalStatus = beforeReport.CreateSelect(
        runFailed, runStatus, endStatus, "obelisk.vpi.final_status");
    report->setArgOperand(1, finalStatus);
    returnStatus->setOperand(0, finalStatus);
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

LogicalResult configureTargetLayout(ModuleOp module, TargetKind target) {
  auto backend = createTargetBackend(target);
  if (!backend)
    return module.emitError("requested code-generation target is unavailable");
  std::string error;
  auto machine = backend->createTargetMachine(error, 0);
  if (!machine)
    return module.emitError() << "cannot obtain target data layout: " << error;
  module->setAttr("llvm.target_triple",
                  StringAttr::get(module.getContext(), backend->getTriple()));
  module->setAttr(
      "llvm.data_layout",
      StringAttr::get(module.getContext(),
                      machine->createDataLayout().getStringRepresentation()));
  return success();
}

LogicalResult emitTargetOutput(ModuleOp module,
                               const NativeOutputOptions &options) {
  std::unique_ptr<TargetBackend> backend = createTargetBackend(options.target);
  if (!backend) {
    errs() << "obelisk: error: this build does not include the requested "
              "code-generation target\n";
    return failure();
  }

  std::string targetError;
  bool graphRequiresBytecode = false;
  module.walk([&](obelisk::sim::SimDesignOp design) {
    obelisk::schedule::ComputeGraphAttr graph = design.getComputeGraphAttr();
    if (!graph)
      return;
    for (mlir::Attribute attribute : graph.getNodes())
      if (auto fragment =
              mlir::dyn_cast<obelisk::schedule::ComputeFragmentAttr>(attribute))
        graphRequiresBytecode |=
            fragment.getTier() == obelisk::schedule::ComputeTierKind::Bytecode;
  });
  bool useBytecode = options.bytecode || graphRequiresBytecode;

  // A forced or graph-selected bytecode executable uses native code only for
  // scheduler glue, runtime entry points, and optional foreign-call thunks.
  // The SystemVerilog bodies have already gone through the requested
  // simulation optimization pipeline before being frozen into bytecode.
  // Running LLVM's aggressive optimizer over their unreachable native
  // fallback copies dominates large library builds without changing bytecode
  // execution, so keep the host shell deliberately cheap.
  uint32_t hostOptLevel = useBytecode ? 0 : options.optLevel;
  std::unique_ptr<TargetMachine> targetMachine =
      backend->createTargetMachine(targetError, hostOptLevel);
  if (!targetMachine) {
    errs() << "obelisk: error: could not create " << backend->getDescription()
           << " target: " << targetError << '\n';
    return failure();
  }
  bool requiresStateSync = false;
  bool resolveInitialDrivers = false;
  std::optional<obelisk::schedule::NativeSchedulerMode> nativeScheduler =
      obelisk::schedule::symbolizeNativeSchedulerMode(options.nativeScheduler);
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
  if (*nativeScheduler == obelisk::schedule::NativeSchedulerMode::Auto &&
      useBytecode) {
    *nativeScheduler = obelisk::schedule::NativeSchedulerMode::Generic;
  } else if (*nativeScheduler == obelisk::schedule::NativeSchedulerMode::Auto &&
             !useBytecode) {
    obelisk::analysis::NativeAOTAnalysis aot =
        obelisk::analysis::NativeAOTAnalysis::compute(module);
    if (options.timing) {
      errs() << "obelisk native eligibility: eligible=" << aot.isEligible()
             << " fully_eligible=" << aot.isFullyEligible()
             << " cost_effective=" << aot.isAOTCostEffective()
             << " native_cost=" << aot.getNativeGraphCost()
             << " total_cost=" << aot.getTotalGraphCost()
             << " excluded_actor_cost=" << aot.getExcludedActorCost()
             << " excluded_block_cost=" << aot.getExcludedBlockCost() << '\n';
      for (StringRef reason : aot.getReasons())
        errs() << "obelisk native boundary: " << reason << '\n';
      // A fragment can touch several reasons, so these costs may overlap and
      // do not sum to the withheld total. They identify which boundary holds
      // the most graph cost, which the reason list alone does not.
      for (const obelisk::analysis::NativeAOTBoundaryCost &entry :
           aot.getBoundaryCosts())
        errs() << "obelisk native boundary cost: cost=" << entry.cost
               << " fragments=" << entry.fragments << " actors=" << entry.actors
               << " reason=" << entry.reason << '\n';
    }
    if (!aot.isEligible() || !aot.isAOTCostEffective())
      *nativeScheduler = obelisk::schedule::NativeSchedulerMode::Generic;
    // A structural periodic candidate is only a cheap pipeline-shaping hint.
    // Keep Auto through coroutine lowering, where physical aliases, exact
    // fanout, and direct-fragment coverage can be proved together. A false
    // positive must remain eligible for the generic/AOT fallback.
  }
  bool pruneModel = options.target == TargetKind::Native &&
                    options.kind == NativeOutputKind::Executable &&
                    options.optLevel > 0;
  if (pruneModel)
    native::mark<native::ClosedExecutable>(module);
  else
    native::remove<native::ClosedExecutable>(module);
  if (failed(lowerToLLVM(module, *targetMachine, backend->getTriple(),
                         useBytecode, options.vpi, options.bytecodeScope,
                         *nativeScheduler, options.optLevel,
                         backend->supportsSemanticPartitions() && !useBytecode,
                         options.timing, options.verifyEach, options.irDump,
                         requiresStateSync, resolveInitialDrivers)))
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
  if (backend->supportsSemanticPartitions() && !useBytecode &&
      options.kind == NativeOutputKind::Executable) {
    FailureOr<std::optional<NativePartitionPlan>> plan =
        readNativePartitionPlan(module);
    if (failed(plan))
      return failure();
    nativePartitionPlan = std::move(*plan);
  }
  auto prunePartitionInventory = [&](const llvm::StringSet<> &removed) {
    if (nativePartitionPlan)
      for (NativePartition &partition : nativePartitionPlan->partitions) {
        llvm::erase_if(partition.members, [&](const std::string &name) {
          return removed.contains(name);
        });
        llvm::erase_if(partition.exports, [&](const std::string &name) {
          return removed.contains(name);
        });
      }
  };

  registerLLVMDialectTranslation(*module.getContext());
  registerBuiltinDialectTranslation(*module.getContext());
  llvm::StringSet<> nativeExports;
  for (LLVM::LLVMFuncOp function : module.getOps<LLVM::LLVMFuncOp>()) {
    if (dpi::has<dpi::Export>(function))
      nativeExports.insert(function.getSymName());
  }
  if (pruneModel) {
    // The manifest is an ownership inventory, not a set of live entry points.
    // It has been read above; retain only surviving members in the C++ plan.
    native::remove<native::PhysicalPartitionManifest>(module);
    auto removed = detail::pruneNativeExecutableSymbols(
        module, nativeExports, options.vpi != "off" || requiresStateSync,
        options.verifyEach);
    if (failed(removed))
      return failure();
    prunePartitionInventory(*removed);
    if (options.timing) {
      errs() << "obelisk native pruning: MLIR symbols removed="
             << removed->size() << '\n';
      detail::reportNativePrunedSymbols(*removed, "MLIR");
    }
    markBackendTiming("MLIR symbol dead-code elimination");
  }
  if (failed(materializeDeferredRuntimeByteGlobals(module)))
    return failure();
  markBackendTiming("runtime byte-global materialization");
  if (pruneModel) {
    unsigned changed = detail::balanceNativeGlobalInitializers(
        module, targetMachine->createDataLayout());
    if (options.timing)
      errs() << "obelisk native balanced global initializers: " << changed
             << '\n';
    markBackendTiming("global initializer balancing");
  }
  if (failed(addressNativeStateThroughBases(module)))
    return failure();
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
  releaseFreedHeapPages();
  llvmModule->setTargetTriple(Triple(backend->getTriple()));
  llvmModule->setDataLayout(targetMachine->createDataLayout());
  if (failed(addVPIStartupLifecycle(*llvmModule, options.vpi,
                                    options.sharedLibraryInputs,
                                    requiresStateSync, resolveInitialDrivers)))
    return failure();
  if (options.target == TargetKind::Native)
    applyNativeFunctionTargetAttributes(*llvmModule, *targetMachine);
  markBackendTiming("VPI lifecycle materialization");
  if (options.debugNativeExecutionCounts &&
      !detail::addNativeExecutionCounts(*llvmModule)) {
    errs() << "obelisk: error: execution counts require a generated main\n";
    return failure();
  }
  if (pruneModel) {
    // Partitioning promotes implementation-local definitions to external
    // linkage for cross-object references. Eliminate unreachable model code
    // first, while the complete reference graph is available, rather than
    // optimizing and code-generating bodies the final linker will discard.
    llvm::StringSet<> removed =
        detail::pruneNativeExecutableModel(*llvmModule, nativeExports);
    prunePartitionInventory(removed);
    if (options.timing) {
      errs() << "obelisk native pruning: LLVM symbols removed="
             << removed.size() << '\n';
      detail::reportNativePrunedSymbols(removed, "LLVM");
    }
    markBackendTiming("generated model dead-code elimination");
  }
  if (options.timing) {
    SmallVector<std::pair<uint64_t, StringRef>> functions;
    uint64_t instructions = 0;
    for (const llvm::Function &function : *llvmModule) {
      if (function.isDeclaration())
        continue;
      uint64_t count = function.getInstructionCount();
      instructions += count;
      functions.emplace_back(count, function.getName());
    }
    llvm::sort(functions, [](const auto &lhs, const auto &rhs) {
      return lhs.first != rhs.first ? lhs.first > rhs.first
                                    : lhs.second < rhs.second;
    });
    errs() << "obelisk native LLVM inventory: functions=" << functions.size()
           << " instructions=" << instructions << '\n';
    for (const auto &[count, name] : llvm::ArrayRef(functions).take_front(
             std::min<size_t>(10, functions.size())))
      errs() << "obelisk native LLVM body: instructions=" << count
             << " name=" << name << '\n';
  }
  bool splitModule = nativePartitionPlan &&
                     shouldSplitNativeModule(*llvmModule, *nativePartitionPlan);
  bool thinLTO = splitModule && options.optLevel != 0 && !options.noLTO;
  if (!splitModule &&
      failed(optimizeLLVMModule(*llvmModule, *targetMachine, hostOptLevel)))
    return failure();

  bool fullLTO = options.kind == NativeOutputKind::Executable && !useBytecode &&
                 !options.noLTO && backend->usesFullLTO(options.optLevel) &&
                 !thinLTO;
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
    Expected<NativeModuleSplitPlan> splitPlan =
        planNativeModuleSplit(*llvmModule, *nativePartitionPlan);
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
        // ThinLTO imports source modules in lexical filename order. Put the
        // stable partition index before the random temporary suffix so that
        // fresh builds retain the same import and global definition order.
        FailureOr<SmallString<256>> temporary = makeTemporaryBeside(
            (Twine(options.outputPath) + ".part-" + Twine(index)).str(),
            nativeObject ? ".o" : ".bc");
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
      releaseFreedHeapPages();
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
      std::vector<double> partitionParseSeconds(splitPlan->groups.size(), 0.0);
      std::vector<double> partitionOptimizationSeconds(splitPlan->groups.size(),
                                                       0.0);
      std::vector<double> partitionObjectSeconds(splitPlan->groups.size(), 0.0);
      double bitcodeSerializationSeconds = 0.0;
      std::vector<std::unique_ptr<TargetMachine>> partitionTargets;
      partitionTargets.reserve(splitPlan->groups.size());
      double cloneSeconds = 0.0;
      for (auto [index, group] : llvm::enumerate(splitPlan->groups)) {
        auto cloneStart = std::chrono::steady_clock::now();
        std::unique_ptr<llvm::Module> partition =
            cloneNativeModulePartition(*llvmModule, *splitPlan, group);
        SmallVector<char, 0> storage;
        raw_svector_ostream stream(storage);
        auto serializationStart = std::chrono::steady_clock::now();
        cloneSeconds +=
            std::chrono::duration<double>(serializationStart - cloneStart)
                .count();
        WriteBitcodeToFile(*partition, stream);
        bitcodeSerializationSeconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          serializationStart)
                .count();
        partitionBitcode[index].assign(storage.begin(), storage.end());
        FailureOr<SmallString<256>> temporary = makeTemporaryBeside(
            (Twine(options.outputPath) + ".part-" + Twine(index)).str(), ".o");
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
      releaseFreedHeapPages();
      if (options.timing) {
        errs() << "obelisk backend timing: partition cloning: " << cloneSeconds
               << " s\n";
        errs() << "obelisk backend timing: partition bitcode serialization: "
               << bitcodeSerializationSeconds << " s\n";
      }
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
          auto optimizationStart = std::chrono::steady_clock::now();
          partitionParseSeconds[index] =
              std::chrono::duration<double>(optimizationStart - workerStart)
                  .count();
          if (failed(optimizeLLVMModule(**parsed, *partitionTargets[index],
                                        hostOptLevel)))
            return false;
          auto objectStart = std::chrono::steady_clock::now();
          partitionOptimizationSeconds[index] =
              std::chrono::duration<double>(objectStart - optimizationStart)
                  .count();
          if (failed(writeObject(**parsed, *partitionTargets[index],
                                 modulePaths[index],
                                 backend->getDescription())))
            return false;
          auto workerEnd = std::chrono::steady_clock::now();
          partitionObjectSeconds[index] =
              std::chrono::duration<double>(workerEnd - objectStart).count();
          partitionCodegenSeconds[index] =
              std::chrono::duration<double>(workerEnd - workerStart).count();
          return true;
        }));
      pool.wait();
      for (const std::shared_future<bool> &future : futures)
        if (!future.get()) {
          removeTemporaries();
          return failure();
        }
      if (options.timing)
        for (auto [index, seconds] : llvm::enumerate(partitionCodegenSeconds)) {
          errs() << "obelisk backend timing: partition " << index
                 << " optimization and codegen: " << seconds << " s\n";
          errs() << "obelisk backend timing: partition " << index
                 << " bitcode parsing: " << partitionParseSeconds[index]
                 << " s\n";
          errs() << "obelisk backend timing: partition " << index
                 << " LLVM optimization: "
                 << partitionOptimizationSeconds[index] << " s\n";
          errs() << "obelisk backend timing: partition " << index
                 << " object emission: " << partitionObjectSeconds[index]
                 << " s\n";
        }
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
