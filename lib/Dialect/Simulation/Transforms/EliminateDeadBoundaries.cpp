//===- EliminateDeadBoundaries.cpp - Prune simulation boundaries --------===//

#include "EliminateDeadBoundaries.h"
#include "Utils.h"

#include "obelisk/Dialect/Simulation/Transforms/Passes.h"

#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseSet.h"

#include <optional>
#include <type_traits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMELIMINATEDEADBOUNDARIESPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

struct BoundarySite {
  Operation *operation;
  std::optional<unsigned> callee;
  bool isCall;
  bool isTask;
  llvm::BitVector demandedResults;
  bool active = true;
};

struct FunctionInfo {
  sim::SimFuncOp function;
  llvm::BitVector liveArguments;
  llvm::BitVector liveResults;
  llvm::BitVector eraseArguments;
  llvm::BitVector eraseResults;
  llvm::BitVector dpiOutputArguments;
  SmallVector<sim::SimReturnOp> returns;
  bool pinned = false;
  StringRef pinReason;
  bool discardable = false;
};

static void addStatistic(Pass::Statistic *statistic, uint64_t amount = 1) {
  if (statistic)
    *statistic += amount;
}

static bool isDPIExportMetadata(StringRef name) {
  return name == "obelisk_sim.dpi_export" ||
         name == "obelisk_sim.dpi_c_identifier" ||
         name == "obelisk_sim.dpi_scope_id" ||
         name == "obelisk_sim.dpi_export_id" ||
         name == "obelisk_sim.dpi_abi_signature" ||
         name == "obelisk_sim.dpi_aggregate_layouts" ||
         name == "obelisk_sim.dpi_logical_inputs";
}

static bool hasUnknownOperationMetadata(Operation *operation,
                                        bool allowDPIExport = false) {
  for (NamedAttribute named : operation->getAttrs()) {
    StringRef name = named.getName().strref();
    if (name.starts_with("obelisk_sim.") &&
        !sim::metadata::isKnownOperation(name) &&
        !(allowDPIExport && isDPIExportMetadata(name)))
      return true;
  }
  return false;
}

/// Return output-formal value arguments that may participate in ordinary
/// liveness analysis without changing the external DPI ABI.  The companion
/// destination references and every input/inout value remain ABI-pinned.
static std::optional<llvm::BitVector>
getDPIOutputArguments(sim::SimFuncOp function) {
  if (!function->hasAttr("obelisk_sim.dpi_export") ||
      function->hasAttr(sim::metadata::dpiElidedInputs) ||
      function.getEntryKind() != sim::EntryKind::Task ||
      !function.getFunctionType().getResults().empty())
    return std::nullopt;

  auto identifier =
      function->getAttrOfType<StringAttr>("obelisk_sim.dpi_c_identifier");
  auto scope = function->getAttrOfType<IntegerAttr>("obelisk_sim.dpi_scope_id");
  auto exportID =
      function->getAttrOfType<IntegerAttr>("obelisk_sim.dpi_export_id");
  auto logicalInputs =
      function->getAttrOfType<IntegerAttr>("obelisk_sim.dpi_logical_inputs");
  auto signature =
      function->getAttrOfType<ArrayAttr>("obelisk_sim.dpi_abi_signature");
  if (!identifier || identifier.empty() || !scope || !exportID ||
      !logicalInputs || logicalInputs.getValue().isNegative() || !signature)
    return std::nullopt;
  uint64_t inputCount = logicalInputs.getValue().getLimitedValue();
  FunctionType type = function.getFunctionType();
  if (inputCount > signature.size() || type.getNumInputs() <= inputCount ||
      !isa<sim::ContextType>(type.getInput(0)))
    return std::nullopt;
  if (auto layouts = function->getAttrOfType<ArrayAttr>(
          "obelisk_sim.dpi_aggregate_layouts");
      layouts && layouts.size() != signature.size())
    return std::nullopt;

  SmallVector<sim::DPIABIAttr> entries;
  entries.reserve(signature.size());
  for (Attribute attribute : signature) {
    auto entry = dyn_cast<sim::DPIABIAttr>(attribute);
    if (!entry)
      return std::nullopt;
    entries.push_back(entry);
  }

  llvm::BitVector candidates(type.getNumInputs());
  uint64_t copyOut = inputCount;
  uint64_t destination = inputCount + 1;
  for (uint64_t index = 0; index != inputCount; ++index) {
    sim::DPIABIAttr input = entries[index];
    if (input.getDirection() == sim::DPIArgumentDirection::Result)
      return std::nullopt;
    auto capture = dyn_cast_or_null<sim::CaptureKindAttr>(
        function.getArgAttr(index + 1, sim::metadata::captureKind));
    if (!capture || capture.getValue() != sim::CaptureKind::Formal)
      return std::nullopt;
    if (input.getDirection() == sim::DPIArgumentDirection::Input)
      continue;
    if (copyOut >= entries.size() || destination >= type.getNumInputs() ||
        !isa<sim::RefType>(type.getInput(destination)))
      return std::nullopt;
    sim::DPIABIAttr output = entries[copyOut++];
    auto destinationCapture = dyn_cast_or_null<sim::CaptureKindAttr>(
        function.getArgAttr(destination, sim::metadata::captureKind));
    if (!destinationCapture ||
        destinationCapture.getValue() != sim::CaptureKind::Formal ||
        output.getDirection() != sim::DPIArgumentDirection::Output ||
        output.getKind() != input.getKind() ||
        output.getWidth() != input.getWidth() ||
        output.getFourState() != input.getFourState() ||
        output.getIsSigned() != input.getIsSigned())
      return std::nullopt;
    if (input.getDirection() == sim::DPIArgumentDirection::Output)
      candidates.set(index + 1);
    ++destination;
  }
  if (copyOut != entries.size())
    return std::nullopt;
  return candidates;
}

static bool hasCompiledSiteMetadata(Operation *operation) {
  for (NamedAttribute named : operation->getAttrs())
    if (isa<sim::ContinuationSiteAttr, sim::TimingSiteAttr, sim::NBASiteAttr,
            sim::EventSiteAttr>(named.getValue()))
      return true;
  return false;
}

template <typename Callback>
static void walkFunctionBody(sim::SimFuncOp function, Callback &&callback) {
  if (function.isExternal())
    return;
  function.getBody().walk<WalkOrder::PreOrder>([&](Operation *operation) {
    if (isa<sim::SimFuncOp>(operation))
      return WalkResult::skip();
    callback(operation);
    return WalkResult::advance();
  });
}

static LogicalResult validateDictionaryArray(Operation *owner, ArrayAttr attrs,
                                             unsigned expected,
                                             StringRef description) {
  if (!attrs)
    return success();
  if (attrs.size() != expected)
    return owner->emitOpError()
           << "has malformed " << description << ": expected " << expected
           << " dictionaries but found " << attrs.size();
  for (auto [index, attr] : llvm::enumerate(attrs))
    if (!isa<DictionaryAttr>(attr))
      return owner->emitOpError()
             << "has malformed " << description << " entry #" << index
             << ": expected a dictionary";
  return success();
}

static unsigned countSymbolReferences(Operation *operation,
                                      SymbolRefAttr reference) {
  unsigned count = 0;
  for (NamedAttribute named : operation->getAttrs())
    named.getValue().walk([&](SymbolRefAttr candidate) {
      if (candidate == reference)
        ++count;
    });
  return count;
}

static ArrayAttr filterPositionalMetadata(MLIRContext *context, ArrayAttr attrs,
                                          const llvm::BitVector &erase) {
  if (!attrs)
    return {};
  SmallVector<Attribute> filtered;
  filtered.reserve(attrs.size() - erase.count());
  for (auto [index, attr] : llvm::enumerate(attrs))
    if (!erase.test(index))
      filtered.push_back(attr);
  return ArrayAttr::get(context, filtered);
}

static SmallVector<Type> filterTypes(TypeRange types,
                                     const llvm::BitVector &erase) {
  SmallVector<Type> filtered;
  filtered.reserve(types.size() - erase.count());
  for (auto [index, type] : llvm::enumerate(types))
    if (!erase.test(index))
      filtered.push_back(type);
  return filtered;
}

static void updateBindings(sim::SimFuncOp function,
                           const llvm::BitVector &erase) {
  auto bindings = function->getAttrOfType<ArrayAttr>(sim::metadata::bindings);
  if (!bindings)
    return;
  llvm::DenseSet<StringAttr> deadCopyOutPaths;
  for (Attribute attr : bindings) {
    auto argument = dyn_cast<sim::ArgumentBindingAttr>(attr);
    if (argument && erase.test(argument.getArgument()) &&
        argument.getKind() == sim::UnitArgumentKind::FormalLocal &&
        argument.getCopyOut())
      deadCopyOutPaths.insert(argument.getPath());
  }
  SmallVector<Attribute> updated;
  updated.reserve(bindings.size());
  SmallVector<uint64_t> removedBefore(erase.size() + 1);
  for (unsigned index = 0; index != erase.size(); ++index)
    removedBefore[index + 1] = removedBefore[index] + erase.test(index);
  for (Attribute attr : bindings) {
    auto argument = dyn_cast<sim::ArgumentBindingAttr>(attr);
    if (!argument) {
      updated.push_back(attr);
      continue;
    }
    uint64_t oldIndex = argument.getArgument();
    if (erase.test(oldIndex) ||
        (argument.getKind() == sim::UnitArgumentKind::CopyOutDestination &&
         deadCopyOutPaths.contains(argument.getPath())))
      continue;
    uint64_t newIndex = oldIndex - removedBefore[oldIndex];
    updated.push_back(sim::ArgumentBindingAttr::get(
        function.getContext(), argument.getPath(), newIndex, argument.getKind(),
        argument.getCopyOut(), argument.getLvalueNode(), argument.getCopyIn()));
  }
  function->setAttr(sim::metadata::bindings,
                    ArrayAttr::get(function.getContext(), updated));
}

/// Return whether an operation is allowed inside a discardable zero-time
/// function. Calls are handled by the interprocedural purity fixed point.
static bool hasOnlyDiscardableEffects(Operation *operation) {
  auto interface = dyn_cast<MemoryEffectOpInterface>(operation);
  if (!interface)
    return operation->hasTrait<OpTrait::HasRecursiveMemoryEffects>();
  SmallVector<MemoryEffects::EffectInstance> effects;
  interface.getEffects(effects);
  for (const MemoryEffects::EffectInstance &effect : effects) {
    if (!isa<MemoryEffects::Read>(effect.getEffect()))
      return false;
    if (!isa<sim::StorageResource, sim::NetResource>(effect.getResource()))
      return false;
  }
  return true;
}

class BoundaryEliminator {
public:
  BoundaryEliminator(sim::SimDesignOp design, bool eliminateResults,
                     bool missedRemarks, EliminationStatistics statistics)
      : design(design), eliminateResults(eliminateResults),
        missedRemarks(missedRemarks), statistics(statistics) {}

  LogicalResult run();

private:
  void pin(unsigned index, StringRef reason);
  bool isSiteActive(const BoundarySite &site) const;
  bool willRemoveUse(OpOperand &use) const;
  void classifyPurity();
  void solveDemand();
  LogicalResult preflight();
  void mutate();

  sim::SimDesignOp design;
  bool eliminateResults;
  bool missedRemarks;
  EliminationStatistics statistics;
  SmallVector<FunctionInfo, 0> functions;
  DenseMap<Operation *, unsigned> functionIndices;
  SmallVector<BoundarySite> sites;
  DenseMap<Operation *, unsigned> siteIndices;
};

void BoundaryEliminator::pin(unsigned index, StringRef reason) {
  FunctionInfo &info = functions[index];
  if (info.pinned)
    return;
  info.pinned = true;
  info.pinReason = reason;
  info.liveArguments.set();
  info.liveResults.set();
}

LogicalResult BoundaryEliminator::run() {

  // Late analysis and compilation metadata contains positional ABI records.
  // Reject it before collecting or changing any executable boundary.
  if (design.getComputeGraphAttr()) {
    design.emitOpError(
        eliminateResults
            ? "cannot eliminate dead boundaries after compute-graph metadata "
              "exists"
            : "cannot eliminate dead captures after compute-graph metadata "
              "exists");
    return failure();
  }
  bool hasLateMetadata = false;
  design.walk([&](Operation *operation) {
    if (auto function = dyn_cast<sim::SimFuncOp>(operation))
      hasLateMetadata |= static_cast<bool>(function.getEffectSummaryAttr()) ||
                         static_cast<bool>(function.getFragmentAbiAttr());
    hasLateMetadata |= hasCompiledSiteMetadata(operation);
  });
  if (hasLateMetadata) {
    design.emitOpError(
        eliminateResults
            ? "cannot eliminate dead boundaries after fragment ABI, "
              "effect-summary, or compiled-site metadata exists"
            : "cannot eliminate dead captures after fragment ABI, "
              "effect-summary, or compiled-site metadata exists");
    return failure();
  }

  design.walk<WalkOrder::PreOrder>([&](sim::SimFuncOp function) {
    unsigned index = functions.size();
    FunctionType type = function.getFunctionType();
    functions.push_back({function, llvm::BitVector(type.getNumInputs()),
                         llvm::BitVector(type.getNumResults()),
                         llvm::BitVector(type.getNumInputs()),
                         llvm::BitVector(type.getNumResults()),
                         llvm::BitVector(type.getNumInputs())});
    functionIndices[function.getOperation()] = index;
  });
  addStatistic(statistics.functionsConsidered, functions.size());

  SymbolTableCollection symbolTables;
  bool invalidBoundary = false;

  // Validate every piece of positional metadata and every direct boundary
  // before changing anything. This also builds stable IR-order site records.
  for (auto indexedInfo : llvm::enumerate(functions)) {
    unsigned index = indexedInfo.index();
    FunctionInfo &info = indexedInfo.value();
    sim::SimFuncOp function = info.function;
    FunctionType type = function.getFunctionType();
    ArrayAttr argAttrs = function.getArgAttrsAttr();
    if (!argAttrs) {
      function.emitOpError(
          "has malformed function argument metadata: expected a dictionary "
          "for every argument");
      return failure();
    }
    if (failed(validateDictionaryArray(function, argAttrs, type.getNumInputs(),
                                       "function argument metadata")) ||
        failed(validateDictionaryArray(function, function.getResAttrsAttr(),
                                       type.getNumResults(),
                                       "function result metadata")) ||
        failed(sim::verifyUnitBindings(function))) {
      return failure();
    }
    if (!function.isExternal()) {
      if (function.getBody().empty() ||
          function.getBody().front().getNumArguments() != type.getNumInputs()) {
        function.emitOpError(
            "has malformed entry block arguments for its function signature");
        return failure();
      }
      for (auto [argument, input] : llvm::zip_equal(
               function.getBody().front().getArgumentTypes(), type.getInputs()))
        if (argument != input) {
          function.emitOpError(
              "has malformed entry block argument types for its signature");
          return failure();
        }
    }

    if (std::optional<llvm::BitVector> dpiOutputArguments =
            getDPIOutputArguments(function))
      info.dpiOutputArguments = std::move(*dpiOutputArguments);
    bool canPruneDPIOutput = info.dpiOutputArguments.any();

    if (hasUnknownOperationMetadata(function, canPruneDPIOutput))
      pin(index, "unknown obelisk_sim operation metadata");
    else if (function->getParentOp() != design.getOperation() &&
             !canPruneDPIOutput)
      pin(index, "nested function ABI");
    else if (function.isExternal())
      pin(index, "external declaration ABI");
    else if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      pin(index, "root initializer ABI");
    else if (SymbolTable::getSymbolVisibility(function) ==
                 SymbolTable::Visibility::Nested &&
             !canPruneDPIOutput)
      pin(index, "nested visibility ABI");
    else if (SymbolTable::getSymbolVisibility(function) !=
                 SymbolTable::Visibility::Private &&
             !canPruneDPIOutput)
      pin(index, "non-private ABI");

    // A DPI export has a stable external ABI.  Permit ordinary demand to
    // prove only output copy-ins dead; every other argument remains pinned.
    if (canPruneDPIOutput && !info.pinned) {
      info.liveArguments.set();
      for (int64_t argument : info.dpiOutputArguments.set_bits())
        info.liveArguments.reset(argument);
    }

    // The executable dialect requires an explicit context at position zero.
    if (!function.isExternal() && !info.liveArguments.empty())
      info.liveArguments.set(0);
    if (!eliminateResults)
      info.liveResults.set();

    walkFunctionBody(function, [&](Operation *operation) {
      if (hasUnknownOperationMetadata(operation))
        pin(index, "unknown obelisk_sim operation metadata");

      auto recordSite = [&](auto site) -> LogicalResult {
        if constexpr (!std::is_same_v<decltype(site), sim::SimTaskCallOp>) {
          ArrayAttr siteArgAttrs = site.getArgAttrsAttr();
          if (failed(validateDictionaryArray(
                  site, siteArgAttrs, site.getNumOperands(),
                  isa<sim::SimCallOp>(site.getOperation())
                      ? "call argument metadata"
                      : "spawn argument metadata")) ||
              failed(validateDictionaryArray(
                  site, site.getResAttrsAttr(), site->getNumResults(),
                  isa<sim::SimCallOp>(site.getOperation())
                      ? "call result metadata"
                      : "spawn result metadata")))
            return failure();
        }

        sim::SimFuncOp callee =
            symbolTables.lookupNearestSymbolFrom<sim::SimFuncOp>(
                site.getOperation(), site.getCalleeAttr());
        std::optional<unsigned> calleeIndex;
        if (callee) {
          auto found = functionIndices.find(callee.getOperation());
          if (found != functionIndices.end())
            calleeIndex = found->second;
        }
        unsigned siteIndex = sites.size();
        bool isCall = isa<sim::SimCallOp>(site.getOperation());
        bool isTask = isa<sim::SimTaskCallOp>(site.getOperation());
        sites.push_back({site.getOperation(), calleeIndex, isCall, isTask,
                         llvm::BitVector(isCall ? site->getNumResults() : 0)});
        siteIndices[site.getOperation()] = siteIndex;

        if (!calleeIndex)
          return success();
        FunctionType calleeType = callee.getFunctionType();
        bool valid;
        if constexpr (std::is_same_v<decltype(site), sim::SimTaskCallOp>)
          valid = site.getArguments().getTypes() == calleeType.getInputs();
        else
          valid = site.getOperandTypes() == calleeType.getInputs();
        if constexpr (std::is_same_v<decltype(site), sim::SimCallOp>)
          valid &= site.getResultTypes() == calleeType.getResults() &&
                   (callee.getEntryKind() == sim::EntryKind::Function ||
                    callee.getEntryKind() == sim::EntryKind::Observer);
        else if constexpr (std::is_same_v<decltype(site), sim::SimTaskCallOp>)
          valid &= calleeType.getResults().empty() &&
                   callee.getEntryKind() == sim::EntryKind::Task;
        else
          valid &= calleeType.getResults().empty() &&
                   callee.getEntryKind() != sim::EntryKind::Function &&
                   callee.getEntryKind() != sim::EntryKind::RootInitializer;
        if (!valid)
          return site.emitOpError(
              "has a malformed positional boundary for its callee");
        if (hasUnknownOperationMetadata(site))
          pin(*calleeIndex, "unknown obelisk_sim call-site metadata");
        return success();
      };

      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        if (failed(recordSite(call)))
          invalidBoundary = true;
      } else if (auto task = dyn_cast<sim::SimTaskCallOp>(operation)) {
        if (failed(recordSite(task)))
          invalidBoundary = true;
      } else if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation)) {
        if (failed(recordSite(spawn)))
          invalidBoundary = true;
      } else if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation)) {
        info.returns.push_back(returnOp);
      }
    });
    if (invalidBoundary) {
      return failure();
    }
  }

  // Complete symbol-use visibility is required before a private ABI can be
  // changed. Only the canonical callee attribute of direct calls, task calls,
  // and spawns is accepted as an observable use.
  struct SymbolUseRecord {
    Operation *user;
    SymbolRefAttr reference;
  };
  SmallVector<SmallVector<SymbolUseRecord>> functionUses(functions.size());
  std::optional<SymbolTable::UseRange> allUses =
      SymbolTable::getSymbolUses(&design.getBody());
  if (!allUses) {
    for (unsigned index = 0; index != functions.size(); ++index)
      pin(index, "unresolved symbol uses");
  } else {
    for (const SymbolTable::SymbolUse &use : *allUses) {
      Operation *user = use.getUser();
      SymbolRefAttr reference = use.getSymbolRef();
      sim::SimFuncOp function =
          symbolTables.lookupNearestSymbolFrom<sim::SimFuncOp>(user, reference);
      if (!function)
        continue;
      auto found = functionIndices.find(function.getOperation());
      if (found != functionIndices.end())
        functionUses[found->second].push_back({user, reference});
    }
  }
  for (auto [index, info] : llvm::enumerate(functions)) {
    if (info.pinned)
      continue;
    for (const SymbolUseRecord &use : functionUses[index]) {
      Operation *user = use.user;
      auto site = siteIndices.find(user);
      bool direct =
          site != siteIndices.end() && sites[site->second].callee == index;
      if (direct) {
        SymbolRefAttr callee;
        if (auto call = dyn_cast<sim::SimCallOp>(user))
          callee = call.getCalleeAttr();
        else if (auto task = dyn_cast<sim::SimTaskCallOp>(user))
          callee = task.getCalleeAttr();
        else
          callee = cast<sim::SimSpawnOp>(user).getCalleeAttr();
        direct =
            use.reference == callee && countSymbolReferences(user, callee) == 1;
      }
      if (!direct) {
        pin(index, "non-direct or address-taken symbol use");
        break;
      }
    }
  }

  for (FunctionInfo &info : functions) {
    if (!info.pinned)
      continue;
    addStatistic(statistics.abiPinnedFunctions);
    if (missedRemarks)
      info.function.emitRemark()
          << (eliminateResults ? "dead boundary elimination retained ABI: "
                               : "dead capture elimination retained ABI: ")
          << info.pinReason;
  }

  classifyPurity();
  solveDemand();

  for (FunctionInfo &info : functions) {
    if (info.pinned)
      continue;
    for (unsigned index = 1; index < info.liveArguments.size(); ++index)
      if (!info.liveArguments.test(index))
        info.eraseArguments.set(index);
    if (eliminateResults)
      for (unsigned index = 0; index < info.liveResults.size(); ++index)
        if (!info.liveResults.test(index))
          info.eraseResults.set(index);
  }
  for (BoundarySite &site : sites)
    site.active = isSiteActive(site);

  if (failed(preflight()))
    return failure();
  mutate();
  return success();
}

void BoundaryEliminator::classifyPurity() {
  if (!eliminateResults)
    return;

  struct PurityObservation {
    bool locallyDiscardable = false;
    SmallVector<unsigned> callees;
  };
  SmallVector<PurityObservation> observations(functions.size());
  const DenseMap<Operation *, unsigned> &frozenSiteIndices = siteIndices;
  parallelFor(design.getContext(), 0, functions.size(), [&](size_t index) {
    const FunctionInfo &info = functions[index];
    sim::SimFuncOp function = info.function;
    PurityObservation &observation = observations[index];
    if (function.isExternal() ||
        function.getEntryKind() != sim::EntryKind::Function ||
        !getReexecutingBlocks(function).empty())
      return;
    observation.locallyDiscardable = true;
    walkFunctionBody(function, [&](Operation *operation) {
      if (!observation.locallyDiscardable)
        return;
      if (isa<sim::SimReturnOp>(operation))
        return;
      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        auto found = frozenSiteIndices.find(call.getOperation());
        if (found == frozenSiteIndices.end()) {
          observation.locallyDiscardable = false;
          return;
        }
        const BoundarySite &site = sites[found->second];
        if (!site.callee || functions[*site.callee].function.isExternal() ||
            functions[*site.callee].function.getEntryKind() !=
                sim::EntryKind::Function) {
          observation.locallyDiscardable = false;
          return;
        }
        observation.callees.push_back(*site.callee);
        return;
      }
      if (isa<sim::SimSpawnOp>(operation) || operation->getNumRegions() != 0 ||
          !hasOnlyDiscardableEffects(operation))
        observation.locallyDiscardable = false;
    });
    llvm::sort(observation.callees);
    observation.callees.erase(
        std::unique(observation.callees.begin(), observation.callees.end()),
        observation.callees.end());
  });

  // IEEE 1800-2023 13.4 prohibits suspension, not nontermination (12.7.6).
  // Prove discardability from returning leaves outward. Recursive SCCs and
  // their callers stay live unless a separate termination proof is available.
  SmallVector<SmallVector<unsigned>> callers(functions.size());
  SmallVector<unsigned> remainingCallees(functions.size());
  for (auto [caller, observation] : llvm::enumerate(observations))
    for (unsigned callee : observation.callees) {
      callers[callee].push_back(caller);
      ++remainingCallees[caller];
    }
  SmallVector<unsigned> worklist;
  for (unsigned index = 0; index != functions.size(); ++index)
    if (observations[index].locallyDiscardable && !remainingCallees[index]) {
      functions[index].discardable = true;
      worklist.push_back(index);
    }
  while (!worklist.empty()) {
    unsigned callee = worklist.pop_back_val();
    for (unsigned caller : callers[callee]) {
      if (--remainingCallees[caller] ||
          !observations[caller].locallyDiscardable)
        continue;
      functions[caller].discardable = true;
      worklist.push_back(caller);
    }
  }
}

bool BoundaryEliminator::isSiteActive(const BoundarySite &site) const {
  if (!site.isCall || !eliminateResults || !site.callee)
    return true;
  return !functions[*site.callee].discardable || site.demandedResults.any();
}

void BoundaryEliminator::solveDemand() {
  enum class NodeKind : uint8_t {
    FunctionArgument,
    FunctionResult,
    CallResult
  };
  struct DemandNode {
    NodeKind kind;
    unsigned owner;
    unsigned index;
  };
  SmallVector<DemandNode> nodes;
  SmallVector<unsigned> argumentBases(functions.size());
  SmallVector<unsigned> resultBases(functions.size());
  DenseMap<Value, unsigned> valueNodes;
  for (auto [functionIndex, info] : llvm::enumerate(functions)) {
    argumentBases[functionIndex] = nodes.size();
    for (unsigned index = 0; index != info.liveArguments.size(); ++index)
      nodes.push_back(
          {NodeKind::FunctionArgument, unsigned(functionIndex), index});
    resultBases[functionIndex] = nodes.size();
    for (unsigned index = 0; index != info.liveResults.size(); ++index)
      nodes.push_back(
          {NodeKind::FunctionResult, unsigned(functionIndex), index});
    if (info.function.isExternal())
      continue;
    Block &entry = info.function.getBody().front();
    for (BlockArgument argument : entry.getArguments())
      valueNodes[argument] =
          argumentBases[functionIndex] + argument.getArgNumber();
  }

  constexpr unsigned noNode = std::numeric_limits<unsigned>::max();
  SmallVector<unsigned> callResultBases(sites.size(), noNode);
  for (auto [siteIndex, site] : llvm::enumerate(sites)) {
    if (!site.isCall)
      continue;
    callResultBases[siteIndex] = nodes.size();
    auto call = cast<sim::SimCallOp>(site.operation);
    for (auto [resultIndex, result] : llvm::enumerate(call.getResults())) {
      unsigned node = nodes.size();
      nodes.push_back(
          {NodeKind::CallResult, unsigned(siteIndex), unsigned(resultIndex)});
      valueNodes[result] = node;
    }
  }

  struct GatedArgument {
    unsigned argument;
    unsigned source;
  };
  SmallVector<SmallVector<unsigned>> dependents(nodes.size());
  SmallVector<SmallVector<GatedArgument>> siteArguments(sites.size());
  SmallVector<SmallVector<std::pair<unsigned, unsigned>>> argumentGates(
      nodes.size());
  SmallVector<unsigned> roots;

  auto observeUses = [&](Value value, unsigned sourceNode) {
    for (OpOperand &use : value.getUses()) {
      Operation *user = use.getOwner();
      auto foundSite = siteIndices.find(user);
      if (foundSite != siteIndices.end()) {
        unsigned siteIndex = foundSite->second;
        const BoundarySite &site = sites[siteIndex];
        if (!site.callee || use.getOperandNumber() >=
                                functions[*site.callee].liveArguments.size()) {
          roots.push_back(sourceNode);
          continue;
        }
        unsigned argument =
            argumentBases[*site.callee] + use.getOperandNumber();
        siteArguments[siteIndex].push_back({argument, sourceNode});
        argumentGates[argument].push_back({siteIndex, sourceNode});
        continue;
      }
      if (isa<sim::SimReturnOp>(user)) {
        sim::SimFuncOp function = user->getParentOfType<sim::SimFuncOp>();
        auto foundFunction = functionIndices.find(function.getOperation());
        if (foundFunction != functionIndices.end() &&
            use.getOperandNumber() <
                functions[foundFunction->second].liveResults.size()) {
          unsigned result =
              resultBases[foundFunction->second] + use.getOperandNumber();
          dependents[result].push_back(sourceNode);
          continue;
        }
      }
      roots.push_back(sourceNode);
    }
  };
  for (auto [value, node] : valueNodes)
    observeUses(value, node);

  for (auto [siteIndex, site] : llvm::enumerate(sites)) {
    if (!site.isCall || !site.callee)
      continue;
    unsigned callBase = callResultBases[siteIndex];
    unsigned calleeBase = resultBases[*site.callee];
    for (unsigned index = 0; index != site.demandedResults.size(); ++index)
      dependents[callBase + index].push_back(calleeBase + index);
  }

  SmallVector<uint8_t> activeSites(sites.size());
  for (auto [index, site] : llvm::enumerate(sites))
    activeSites[index] = !site.isCall || !eliminateResults || !site.callee ||
                         !functions[*site.callee].discardable;

  SmallVector<uint8_t> live(nodes.size());
  SmallVector<unsigned> worklist;
  auto markLive = [&](unsigned node) {
    if (live[node])
      return;
    live[node] = true;
    const DemandNode &demand = nodes[node];
    if (demand.kind == NodeKind::FunctionArgument)
      functions[demand.owner].liveArguments.set(demand.index);
    else if (demand.kind == NodeKind::FunctionResult)
      functions[demand.owner].liveResults.set(demand.index);
    else
      sites[demand.owner].demandedResults.set(demand.index);
    worklist.push_back(node);
  };
  for (auto [functionIndex, info] : llvm::enumerate(functions)) {
    for (int64_t index : info.liveArguments.set_bits())
      markLive(argumentBases[functionIndex] + index);
    for (int64_t index : info.liveResults.set_bits())
      markLive(resultBases[functionIndex] + index);
  }
  for (unsigned root : roots)
    markLive(root);

  auto activateSite = [&](unsigned siteIndex) {
    if (activeSites[siteIndex])
      return;
    activeSites[siteIndex] = true;
    for (GatedArgument gate : siteArguments[siteIndex])
      if (live[gate.argument])
        markLive(gate.source);
  };
  while (!worklist.empty()) {
    unsigned node = worklist.pop_back_val();
    for (unsigned dependent : dependents[node])
      markLive(dependent);
    for (auto [siteIndex, source] : argumentGates[node])
      if (activeSites[siteIndex])
        markLive(source);
    const DemandNode &demand = nodes[node];
    if (demand.kind == NodeKind::CallResult)
      activateSite(demand.owner);
  }
}

bool BoundaryEliminator::willRemoveUse(OpOperand &use) const {
  Operation *user = use.getOwner();
  auto found = siteIndices.find(user);
  if (found != siteIndices.end()) {
    const BoundarySite &site = sites[found->second];
    if (site.isCall && !site.active)
      return true;
    if (!site.callee)
      return false;
    const llvm::BitVector &erase = functions[*site.callee].eraseArguments;
    return use.getOperandNumber() < erase.size() &&
           erase.test(use.getOperandNumber());
  }
  if (isa<sim::SimReturnOp>(user)) {
    auto function = user->getParentOfType<sim::SimFuncOp>();
    auto index = functionIndices.find(function.getOperation());
    if (index == functionIndices.end())
      return false;
    const llvm::BitVector &erase = functions[index->second].eraseResults;
    return use.getOperandNumber() < erase.size() &&
           erase.test(use.getOperandNumber());
  }
  return false;
}

LogicalResult BoundaryEliminator::preflight() {
  for (FunctionInfo &info : functions) {
    if (info.eraseArguments.none() && info.eraseResults.none())
      continue;
    if (!info.function.getTypeWithoutArgs(info.eraseArguments) ||
        !info.function.getTypeWithoutResults(info.eraseResults) ||
        !info.function.getTypeWithoutArgsAndResults(info.eraseArguments,
                                                    info.eraseResults))
      return info.function.emitOpError(
          eliminateResults
              ? "cannot construct a function type without dead boundaries"
              : "cannot construct a function type without dead captures");
    if (!info.function.isExternal()) {
      Block &entry = info.function.getBody().front();
      for (int64_t index = info.eraseArguments.find_first(); index >= 0;
           index = info.eraseArguments.find_next(index))
        for (OpOperand &use : entry.getArgument(index).getUses())
          if (!willRemoveUse(use))
            return info.function.emitOpError()
                   << "cannot erase argument #" << index
                   << " because it has a surviving use";
    }
    for (sim::SimReturnOp returnOp : info.returns) {
      if (returnOp.getNumOperands() != info.eraseResults.size())
        return returnOp.emitOpError(
            "has a malformed positional boundary for its function");
    }
  }

  for (const BoundarySite &site : sites) {
    if (!site.isCall)
      continue;
    auto call = cast<sim::SimCallOp>(site.operation);
    if (!site.active) {
      for (OpResult result : call.getResults())
        for (OpOperand &use : result.getUses())
          if (!willRemoveUse(use))
            return call.emitOpError(
                "cannot erase inactive pure call because a result has a "
                "surviving use");
      continue;
    }
    if (!site.callee)
      continue;
    const llvm::BitVector &erase = functions[*site.callee].eraseResults;
    for (int64_t index = erase.find_first(); index >= 0;
         index = erase.find_next(index))
      for (OpOperand &use : call.getResult(index).getUses())
        if (!willRemoveUse(use))
          return call.emitOpError() << "cannot erase result #" << index
                                    << " because it has a surviving use";
  }
  return success();
}

void BoundaryEliminator::mutate() {
  // Returns and invocation operands are rewritten first. This removes every
  // planned use of a dead call result or function entry argument.
  for (FunctionInfo &info : functions) {
    if (info.eraseResults.none())
      continue;
    for (sim::SimReturnOp returnOp : info.returns)
      returnOp->eraseOperands(info.eraseResults);
    addStatistic(statistics.returnOperandsRemoved,
                 info.eraseResults.count() * info.returns.size());
  }

  // First remove dead operands from every active invocation. This also drops
  // uses of inactive-call results before those calls are erased.
  for (BoundarySite &site : sites) {
    if (!site.callee || (site.isCall && !site.active))
      continue;
    const llvm::BitVector &eraseArguments =
        functions[*site.callee].eraseArguments;
    if (!site.isCall) {
      if (eraseArguments.none())
        continue;
      if (site.isTask) {
        auto task = cast<sim::SimTaskCallOp>(site.operation);
        int64_t oldArgumentCount = task.getArgumentCount();
        llvm::BitVector eraseTaskOperands(task->getNumOperands());
        for (int64_t index : eraseArguments.set_bits())
          eraseTaskOperands.set(index);
        task->eraseOperands(eraseTaskOperands);
        task.setArgumentCountAttr(
            IntegerAttr::get(task.getArgumentCountAttr().getType(),
                             oldArgumentCount - eraseArguments.count()));
        addStatistic(statistics.taskOperandsRemoved, eraseArguments.count());
        continue;
      }
      auto spawn = cast<sim::SimSpawnOp>(site.operation);
      if (ArrayAttr attrs = spawn.getArgAttrsAttr())
        spawn.setArgAttrsAttr(filterPositionalMetadata(design.getContext(),
                                                       attrs, eraseArguments));
      spawn->eraseOperands(eraseArguments);
      addStatistic(statistics.spawnOperandsRemoved, eraseArguments.count());
      continue;
    }

    auto call = cast<sim::SimCallOp>(site.operation);
    if (eraseArguments.none())
      continue;
    if (ArrayAttr attrs = call.getArgAttrsAttr())
      call.setArgAttrsAttr(
          filterPositionalMetadata(design.getContext(), attrs, eraseArguments));
    call->eraseOperands(eraseArguments);
    addStatistic(statistics.callOperandsRemoved, eraseArguments.count());
  }

  // Erase inactive calls after their return and active-boundary users have
  // been removed. SSA use edges among inactive calls form a DAG; iterate in
  // reverse IR order until every leaf is gone.
  SmallVector<sim::SimCallOp> inactiveCalls;
  for (const BoundarySite &site : sites)
    if (site.isCall && !site.active)
      inactiveCalls.push_back(cast<sim::SimCallOp>(site.operation));
  while (!inactiveCalls.empty()) {
    bool erased = false;
    // Erasing from a SmallVector invalidates all iterators at and after the
    // erased element. Walk by descending index so removing an element cannot
    // invalidate the yet-to-be-visited prefix. In particular, do not retain a
    // reverse iterator across erase: a batch of independent dead calls would
    // otherwise reuse an invalidated underlying iterator.
    for (size_t index = inactiveCalls.size(); index-- > 0;) {
      sim::SimCallOp call = inactiveCalls[index];
      if (!llvm::all_of(call->getResults(),
                        [](Value value) { return value.use_empty(); })) {
        continue;
      }
      call.erase();
      inactiveCalls.erase(inactiveCalls.begin() + index);
      addStatistic(statistics.pureCallsErased);
      erased = true;
    }
    if (!erased)
      llvm_unreachable("preflighted inactive calls must be mutually erasable");
  }

  // Rebuild active calls whose common callee signature lost results.
  for (BoundarySite &site : sites) {
    if (!site.isCall || !site.callee || !site.active)
      continue;
    const llvm::BitVector &eraseResults = functions[*site.callee].eraseResults;
    if (eraseResults.none())
      continue;
    auto call = cast<sim::SimCallOp>(site.operation);

    SmallVector<Type> resultTypes =
        filterTypes(call.getResultTypes(), eraseResults);
    NamedAttrList attrs(call->getAttrs());
    if (ArrayAttr resAttrs = call.getResAttrsAttr())
      attrs.set(call.getResAttrsAttrName(),
                filterPositionalMetadata(design.getContext(), resAttrs,
                                         eraseResults));
    OpBuilder builder(call);
    auto replacement =
        sim::SimCallOp::create(builder, call.getLoc(), resultTypes,
                               call.getOperands(), attrs.getAttrs());
    unsigned replacementIndex = 0;
    for (auto [index, result] : llvm::enumerate(call.getResults())) {
      if (eraseResults.test(index))
        continue;
      result.replaceAllUsesWith(replacement.getResult(replacementIndex++));
    }
    call.erase();
    site.operation = replacement.getOperation();
    addStatistic(statistics.callsRebuilt);
  }

  for (FunctionInfo &info : functions) {
    if (info.eraseArguments.none() && info.eraseResults.none())
      continue;
    SmallVector<int64_t> elidedDPIInputs;
    for (int64_t argument : info.eraseArguments.set_bits())
      if (info.dpiOutputArguments.test(argument))
        elidedDPIInputs.push_back(argument - 1);
    if (!elidedDPIInputs.empty())
      info.function->setAttr(
          sim::metadata::dpiElidedInputs,
          DenseI64ArrayAttr::get(design.getContext(), elidedDPIInputs));
    updateBindings(info.function, info.eraseArguments);
    if (failed(info.function.eraseArguments(info.eraseArguments)) ||
        failed(info.function.eraseResults(info.eraseResults)))
      llvm_unreachable("function boundary erasure was preflighted");
    addStatistic(statistics.functionsPruned);
    addStatistic(statistics.argumentsRemoved, info.eraseArguments.count());
    addStatistic(statistics.resultsRemoved, info.eraseResults.count());
  }
}

} // namespace

LogicalResult
eliminateDeadSimulationBoundaries(sim::SimDesignOp design,
                                  bool eliminateResults, bool missedRemarks,
                                  EliminationStatistics statistics) {
  return BoundaryEliminator(design, eliminateResults, missedRemarks, statistics)
      .run();
}

namespace {

class ObeliskSimEliminateDeadBoundariesPass final
    : public impl::ObeliskSimEliminateDeadBoundariesPassBase<
          ObeliskSimEliminateDeadBoundariesPass> {
public:
  using Base = impl::ObeliskSimEliminateDeadBoundariesPassBase<
      ObeliskSimEliminateDeadBoundariesPass>;
  using Base::Base;
  ObeliskSimEliminateDeadBoundariesPass(
      const ObeliskSimEliminateDeadBoundariesPass &other)
      : Base(other) {}

  void runOnOperation() override {
    EliminationStatistics statistics{
        &functionsConsidered,  &abiPinnedFunctions,  &functionsPruned,
        &argumentsRemoved,     &resultsRemoved,      &callOperandsRemoved,
        &spawnOperandsRemoved, &taskOperandsRemoved, &returnOperandsRemoved,
        &callsRebuilt,         &pureCallsErased};
    if (failed(eliminateDeadSimulationBoundaries(getOperation(),
                                                 /*eliminateResults=*/true,
                                                 missedRemarks, statistics)))
      signalPassFailure();
  }

private:
  Statistic functionsConsidered{this, "functions-considered",
                                "simulation functions considered"};
  Statistic abiPinnedFunctions{this, "abi-pinned-functions",
                               "functions whose complete ABI was retained"};
  Statistic functionsPruned{this, "functions-pruned",
                            "functions with boundaries removed"};
  Statistic argumentsRemoved{this, "arguments-removed",
                             "function arguments removed"};
  Statistic resultsRemoved{this, "results-removed", "function results removed"};
  Statistic callOperandsRemoved{this, "call-operands-removed",
                                "direct call operands removed"};
  Statistic spawnOperandsRemoved{this, "spawn-operands-removed",
                                 "direct spawn operands removed"};
  Statistic taskOperandsRemoved{this, "task-operands-removed",
                                "direct task-call operands removed"};
  Statistic returnOperandsRemoved{this, "return-operands-removed",
                                  "return operands removed"};
  Statistic callsRebuilt{this, "calls-rebuilt",
                         "active calls rebuilt with fewer results"};
  Statistic pureCallsErased{this, "pure-calls-erased",
                            "inactive discardable calls erased"};
};

} // namespace
} // namespace obelisk
