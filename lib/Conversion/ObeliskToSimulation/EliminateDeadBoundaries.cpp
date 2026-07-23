//===- EliminateDeadBoundaries.cpp - Prune simulation boundaries --------===//

#include "EliminateDeadBoundaries.h"
#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/BitVector.h"

#include <optional>
#include <type_traits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMELIMINATEDEADBOUNDARIESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

struct BoundarySite {
  Operation *operation;
  std::optional<unsigned> callee;
  bool isCall;
  llvm::BitVector demandedResults;
  bool active = true;
};

struct FunctionInfo {
  sim::SimFuncOp function;
  llvm::BitVector liveArguments;
  llvm::BitVector liveResults;
  llvm::BitVector eraseArguments;
  llvm::BitVector eraseResults;
  SmallVector<sim::SimReturnOp> returns;
  bool pinned = false;
  StringRef pinReason;
  bool discardable = false;
};

static void addStatistic(Pass::Statistic *statistic, uint64_t amount = 1) {
  if (statistic)
    *statistic += amount;
}

static bool isKnownOperationMetadata(StringRef name) {
  return name == bindingsAttrName || name == delayScaleAttrName ||
         name == delayQuantumAttrName ||
         name == "obelisk_sim.capture_kind" ||
         name == "obelisk_sim.descriptor_id" ||
         name == "obelisk_sim.descriptor_root_type" ||
         name == "obelisk_sim.descriptor_low" ||
         name == "obelisk_sim.descriptor_indices" ||
         name == "obelisk_sim.descriptor_aggregate_type" ||
         name == "obelisk_sim.descriptor_packed_low" ||
         name == "obelisk_sim.hierarchical_name";
}

static bool hasUnknownOperationMetadata(Operation *operation) {
  for (NamedAttribute named : operation->getAttrs()) {
    StringRef name = named.getName().strref();
    if (name.starts_with("obelisk_sim.") && !isKnownOperationMetadata(name))
      return true;
  }
  return false;
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

static LogicalResult validateBindings(sim::SimFuncOp function) {
  Attribute raw = function->getAttr(bindingsAttrName);
  if (!raw)
    return success();
  auto bindings = dyn_cast<ArrayAttr>(raw);
  if (!bindings)
    return function.emitOpError()
           << "has malformed " << bindingsAttrName << ": expected an array";
  unsigned numArguments = function.getFunctionType().getNumInputs();
  for (auto [index, attr] : llvm::enumerate(bindings)) {
    auto dictionary = dyn_cast<DictionaryAttr>(attr);
    if (!dictionary)
      return function.emitOpError()
             << "has malformed " << bindingsAttrName << " entry #" << index
             << ": expected a dictionary";
    Attribute rawArgument = dictionary.get("argument");
    if (!rawArgument)
      continue;
    auto argument = dyn_cast<IntegerAttr>(rawArgument);
    if (!argument || argument.getValue().isNegative() ||
        argument.getValue().getActiveBits() > 64)
      return function.emitOpError()
             << "has malformed " << bindingsAttrName << " entry #" << index
             << ": argument must be a nonnegative 64-bit integer";
    uint64_t argumentIndex = argument.getValue().getZExtValue();
    if (argumentIndex >= numArguments)
      return function.emitOpError()
             << "has malformed " << bindingsAttrName << " entry #" << index
             << ": argument index " << argumentIndex
             << " is outside the function signature";
  }
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
  auto bindings = function->getAttrOfType<ArrayAttr>(bindingsAttrName);
  if (!bindings)
    return;
  Builder builder(function.getContext());
  SmallVector<Attribute> updated;
  updated.reserve(bindings.size());
  for (Attribute attr : bindings) {
    auto dictionary = cast<DictionaryAttr>(attr);
    auto argument = dictionary.getAs<IntegerAttr>("argument");
    if (!argument) {
      updated.push_back(dictionary);
      continue;
    }
    uint64_t oldIndex = argument.getValue().getZExtValue();
    if (erase.test(oldIndex))
      continue;
    uint64_t newIndex = oldIndex;
    for (int64_t removed = erase.find_first();
         removed >= 0 && uint64_t(removed) < oldIndex;
         removed = erase.find_next(removed))
      --newIndex;
    SmallVector<NamedAttribute> attributes(dictionary.begin(),
                                           dictionary.end());
    for (NamedAttribute &named : attributes)
      if (named.getName() == "argument")
        named.setValue(builder.getIntegerAttr(argument.getType(), newIndex));
    updated.push_back(builder.getDictionaryAttr(attributes));
  }
  function->setAttr(bindingsAttrName, builder.getArrayAttr(updated));
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
  bool isDemandedUse(OpOperand &use, ArrayRef<llvm::BitVector> argumentSnapshot,
                     ArrayRef<llvm::BitVector> resultSnapshot,
                     ArrayRef<llvm::BitVector> callSnapshot) const;
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
                         llvm::BitVector(type.getNumResults())});
    functionIndices[function.getOperation()] = index;
  });
  addStatistic(statistics.functionsConsidered, functions.size());

  SymbolTableCollection symbolTables;
  bool invalidBoundary = false;

  // Validate every piece of positional metadata and every direct boundary
  // before changing anything. This also builds stable IR-order site records.
  for (auto [index, info] : llvm::enumerate(functions)) {
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
        failed(validateBindings(function))) {
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

    if (hasUnknownOperationMetadata(function))
      pin(index, "unknown obelisk_sim operation metadata");
    else if (function->getParentOp() != design.getOperation())
      pin(index, "nested function ABI");
    else if (function.isExternal())
      pin(index, "external declaration ABI");
    else if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      pin(index, "root initializer ABI");
    else if (SymbolTable::getSymbolVisibility(function) ==
             SymbolTable::Visibility::Nested)
      pin(index, "nested visibility ABI");
    else if (SymbolTable::getSymbolVisibility(function) !=
             SymbolTable::Visibility::Private)
      pin(index, "non-private ABI");

    // The executable dialect requires an explicit context at position zero.
    if (!function.isExternal() && !info.liveArguments.empty())
      info.liveArguments.set(0);
    if (!eliminateResults)
      info.liveResults.set();

    walkFunctionBody(function, [&](Operation *operation) {
      if (hasUnknownOperationMetadata(operation))
        pin(index, "unknown obelisk_sim operation metadata");

      auto recordSite = [&](auto site) -> LogicalResult {
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
        sites.push_back({site.getOperation(), calleeIndex, isCall,
                         llvm::BitVector(isCall ? site->getNumResults() : 0)});
        siteIndices[site.getOperation()] = siteIndex;

        if (!calleeIndex)
          return success();
        FunctionType calleeType = callee.getFunctionType();
        bool valid = site.getOperandTypes() == calleeType.getInputs();
        if constexpr (std::is_same_v<decltype(site), sim::SimCallOp>)
          valid &= site.getResultTypes() == calleeType.getResults() &&
                   callee.getEntryKind() == sim::EntryKind::Function;
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
  // changed. Only the canonical callee attribute of direct calls and spawns
  // is accepted as an observable use.
  for (auto [index, info] : llvm::enumerate(functions)) {
    if (info.pinned)
      continue;
    std::optional<SymbolTable::UseRange> uses =
        SymbolTable::getSymbolUses(info.function, design);
    if (!uses) {
      pin(index, "unresolved symbol uses");
      continue;
    }
    for (const SymbolTable::SymbolUse &use : *uses) {
      Operation *user = use.getUser();
      auto site = siteIndices.find(user);
      bool direct =
          site != siteIndices.end() && sites[site->second].callee == index;
      if (direct) {
        SymbolRefAttr callee =
            isa<sim::SimCallOp>(user)
                ? cast<sim::SimCallOp>(user).getCalleeAttr()
                : cast<sim::SimSpawnOp>(user).getCalleeAttr();
        direct = use.getSymbolRef() == callee &&
                 countSymbolReferences(user, callee) == 1;
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
        function.getEntryKind() != sim::EntryKind::Function)
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
      if (isa<sim::SimSpawnOp>(operation) ||
          !hasOnlyDiscardableEffects(operation))
        observation.locallyDiscardable = false;
    });
    llvm::sort(observation.callees);
    observation.callees.erase(
        std::unique(observation.callees.begin(), observation.callees.end()),
        observation.callees.end());
  });

  for (auto [index, observation] : llvm::enumerate(observations))
    functions[index].discardable = observation.locallyDiscardable;

  // This greatest fixed point intentionally accepts recursive SCCs containing
  // only otherwise-discardable operations and calls within the SCC.
  bool changed;
  do {
    SmallVector<uint8_t> snapshot;
    snapshot.reserve(functions.size());
    for (const FunctionInfo &info : functions)
      snapshot.push_back(info.discardable);
    SmallVector<uint8_t> wave(functions.size(), false);
    parallelFor(design.getContext(), 0, functions.size(), [&](size_t index) {
      if (!snapshot[index])
        return;
      wave[index] =
          llvm::all_of(observations[index].callees,
                       [&](unsigned callee) { return snapshot[callee]; });
    });
    changed = false;
    for (unsigned index = 0; index < functions.size(); ++index) {
      if (functions[index].discardable && !wave[index]) {
        functions[index].discardable = false;
        changed = true;
      }
    }
  } while (changed);
}

bool BoundaryEliminator::isSiteActive(const BoundarySite &site) const {
  if (!site.isCall || !eliminateResults || !site.callee)
    return true;
  return !functions[*site.callee].discardable || site.demandedResults.any();
}

bool BoundaryEliminator::isDemandedUse(
    OpOperand &use, ArrayRef<llvm::BitVector> argumentSnapshot,
    ArrayRef<llvm::BitVector> resultSnapshot,
    ArrayRef<llvm::BitVector> callSnapshot) const {
  Operation *user = use.getOwner();
  auto found = siteIndices.find(user);
  if (found != siteIndices.end()) {
    const BoundarySite &site = sites[found->second];
    bool active = !site.isCall || !eliminateResults || !site.callee ||
                  !functions[*site.callee].discardable ||
                  callSnapshot[found->second].any();
    if (!active)
      return false;
    if (!site.callee ||
        use.getOperandNumber() >= argumentSnapshot[*site.callee].size())
      return true;
    return argumentSnapshot[*site.callee].test(use.getOperandNumber());
  }
  if (isa<sim::SimReturnOp>(user)) {
    auto function = user->getParentOfType<sim::SimFuncOp>();
    auto index = functionIndices.find(function.getOperation());
    if (index == functionIndices.end() ||
        use.getOperandNumber() >= resultSnapshot[index->second].size())
      return true;
    return resultSnapshot[index->second].test(use.getOperandNumber());
  }
  return true;
}

void BoundaryEliminator::solveDemand() {
  SmallVector<llvm::BitVector> callDemand;
  callDemand.reserve(sites.size());
  for (const BoundarySite &site : sites)
    callDemand.emplace_back(site.demandedResults.size());

  bool changed;
  do {
    SmallVector<llvm::BitVector> argumentSnapshot, resultSnapshot, callSnapshot;
    argumentSnapshot.reserve(functions.size());
    resultSnapshot.reserve(functions.size());
    callSnapshot.reserve(sites.size());
    for (const FunctionInfo &info : functions) {
      argumentSnapshot.push_back(info.liveArguments);
      resultSnapshot.push_back(info.liveResults);
    }
    for (const BoundarySite &site : sites)
      callSnapshot.push_back(site.demandedResults);

    SmallVector<llvm::BitVector> argumentWave;
    argumentWave.reserve(functions.size());
    for (const FunctionInfo &info : functions)
      argumentWave.emplace_back(info.liveArguments.size());
    for (auto [index, site] : llvm::enumerate(sites))
      callDemand[index] = llvm::BitVector(site.demandedResults.size());

    const DenseMap<Operation *, unsigned> &frozenSiteIndices = siteIndices;
    parallelFor(design.getContext(), 0, functions.size(), [&](size_t index) {
      const FunctionInfo &info = functions[index];
      sim::SimFuncOp function = info.function;
      if (function.isExternal())
        return;
      Block &entry = function.getBody().front();
      for (unsigned argumentIndex = 0; argumentIndex < entry.getNumArguments();
           ++argumentIndex) {
        if (argumentSnapshot[index].test(argumentIndex))
          continue;
        for (OpOperand &use : entry.getArgument(argumentIndex).getUses())
          if (isDemandedUse(use, argumentSnapshot, resultSnapshot,
                            callSnapshot)) {
            argumentWave[index].set(argumentIndex);
            break;
          }
      }
      walkFunctionBody(function, [&](Operation *operation) {
        auto call = dyn_cast<sim::SimCallOp>(operation);
        if (!call)
          return;
        auto found = frozenSiteIndices.find(operation);
        if (found == frozenSiteIndices.end())
          return;
        unsigned siteIndex = found->second;
        for (auto [resultIndex, result] : llvm::enumerate(call.getResults())) {
          if (callSnapshot[siteIndex].test(resultIndex))
            continue;
          for (OpOperand &use : result.getUses())
            if (isDemandedUse(use, argumentSnapshot, resultSnapshot,
                              callSnapshot)) {
              callDemand[siteIndex].set(resultIndex);
              break;
            }
        }
      });
    });

    changed = false;
    for (unsigned index = 0; index < functions.size(); ++index) {
      unsigned before = functions[index].liveArguments.count();
      functions[index].liveArguments |= argumentWave[index];
      changed |= before != functions[index].liveArguments.count();
    }
    for (auto [index, site] : llvm::enumerate(sites)) {
      unsigned before = site.demandedResults.count();
      site.demandedResults |= callDemand[index];
      changed |= before != site.demandedResults.count();
      if (!site.isCall || !site.callee)
        continue;
      FunctionInfo &callee = functions[*site.callee];
      for (int64_t result = site.demandedResults.find_first(); result >= 0;
           result = site.demandedResults.find_next(result)) {
        if (!callee.liveResults.test(result)) {
          callee.liveResults.set(result);
          changed = true;
        }
      }
    }
  } while (changed);
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
    for (auto iterator = inactiveCalls.rbegin();
         iterator != inactiveCalls.rend();) {
      sim::SimCallOp call = *iterator;
      if (!llvm::all_of(call->getResults(),
                        [](Value value) { return value.use_empty(); })) {
        ++iterator;
        continue;
      }
      auto base = std::next(iterator).base();
      call.erase();
      inactiveCalls.erase(base);
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

  void runOnOperation() override {
    EliminationStatistics statistics{
        &functionsConsidered,  &abiPinnedFunctions,    &functionsPruned,
        &argumentsRemoved,     &resultsRemoved,        &callOperandsRemoved,
        &spawnOperandsRemoved, &returnOperandsRemoved, &callsRebuilt,
        &pureCallsErased};
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
  Statistic returnOperandsRemoved{this, "return-operands-removed",
                                  "return operands removed"};
  Statistic callsRebuilt{this, "calls-rebuilt",
                         "active calls rebuilt with fewer results"};
  Statistic pureCallsErased{this, "pure-calls-erased",
                            "inactive discardable calls erased"};
};

} // namespace
} // namespace obelisk
