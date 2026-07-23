//===- EliminateDeadCaptures.cpp - Prune simulation entry captures -------===//

#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/FunctionInterfaces.h"

#include "llvm/ADT/BitVector.h"

#include <optional>
#include <type_traits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMELIMINATEDEADCAPTURESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

struct BoundarySite {
  std::optional<unsigned> callee;
};

struct FunctionInfo {
  sim::SimFuncOp function;
  llvm::BitVector live;
  llvm::BitVector erase;
  bool pinned = false;
  StringRef pinReason;
};

static bool isKnownOperationMetadata(StringRef name) {
  return name == bindingsAttrName || name == delayScaleAttrName ||
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

static void filterPositionalMetadata(Operation *operation, ArrayAttr attrs,
                                     const llvm::BitVector &erase) {
  if (!attrs)
    return;
  SmallVector<Attribute> filtered;
  filtered.reserve(attrs.size() - erase.count());
  for (auto [index, attr] : llvm::enumerate(attrs))
    if (!erase.test(index))
      filtered.push_back(attr);
  operation->setAttr("arg_attrs",
                     ArrayAttr::get(operation->getContext(), filtered));
}

static void eraseInvocationOperands(Operation *operation,
                                    const llvm::BitVector &erase) {
  operation->eraseOperands(erase);
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

class ObeliskSimEliminateDeadCapturesPass final
    : public impl::ObeliskSimEliminateDeadCapturesPassBase<
          ObeliskSimEliminateDeadCapturesPass> {
public:
  using Base = impl::ObeliskSimEliminateDeadCapturesPassBase<
      ObeliskSimEliminateDeadCapturesPass>;
  using Base::Base;

  void runOnOperation() override;

private:
  Statistic functionsConsidered{this, "functions-considered",
                                "simulation functions considered"};
  Statistic abiPinnedFunctions{this, "abi-pinned-functions",
                               "functions whose complete ABI was retained"};
  Statistic functionsPruned{this, "functions-pruned",
                            "functions with arguments removed"};
  Statistic argumentsRemoved{this, "arguments-removed",
                             "function arguments removed"};
  Statistic callOperandsRemoved{this, "call-operands-removed",
                                "direct call operands removed"};
  Statistic spawnOperandsRemoved{this, "spawn-operands-removed",
                                 "direct spawn operands removed"};
};

void ObeliskSimEliminateDeadCapturesPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();

  // Late analysis and compilation metadata contains positional ABI records.
  // Reject it before collecting or changing any executable boundary.
  if (design.getComputeGraphAttr()) {
    design.emitOpError(
        "cannot eliminate dead captures after compute-graph metadata exists");
    return signalPassFailure();
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
        "cannot eliminate dead captures after fragment ABI, effect-summary, "
        "or compiled-site metadata exists");
    return signalPassFailure();
  }

  SmallVector<FunctionInfo> functions;
  DenseMap<Operation *, unsigned> functionIndices;
  design.walk<WalkOrder::PreOrder>([&](sim::SimFuncOp function) {
    unsigned index = functions.size();
    unsigned numArguments = function.getFunctionType().getNumInputs();
    functions.push_back({function, llvm::BitVector(numArguments),
                         llvm::BitVector(numArguments)});
    functionIndices[function.getOperation()] = index;
  });
  functionsConsidered += functions.size();

  auto pin = [&](unsigned index, StringRef reason) {
    FunctionInfo &info = functions[index];
    if (info.pinned)
      return;
    info.pinned = true;
    info.pinReason = reason;
    info.live.set();
  };

  SymbolTableCollection symbolTables;
  DenseMap<Operation *, BoundarySite> sites;
  SmallVector<Operation *> orderedSites;
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
      signalPassFailure();
      return;
    }
    if (failed(validateDictionaryArray(function, argAttrs, type.getNumInputs(),
                                       "function argument metadata")) ||
        failed(validateDictionaryArray(function, function.getResAttrsAttr(),
                                       type.getNumResults(),
                                       "function result metadata")) ||
        failed(validateBindings(function))) {
      signalPassFailure();
      return;
    }
    if (!function.isExternal()) {
      if (function.getBody().empty() ||
          function.getBody().front().getNumArguments() != type.getNumInputs()) {
        function.emitOpError(
            "has malformed entry block arguments for its function signature");
        signalPassFailure();
        return;
      }
      for (auto [argument, input] : llvm::zip_equal(
               function.getBody().front().getArgumentTypes(), type.getInputs()))
        if (argument != input) {
          function.emitOpError(
              "has malformed entry block argument types for its signature");
          signalPassFailure();
          return;
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

    if (!function.isExternal() && !info.live.empty())
      info.live.set(0);

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
        sites[site.getOperation()] = {calleeIndex};
        orderedSites.push_back(site.getOperation());

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
      }
    });
    if (invalidBoundary) {
      signalPassFailure();
      return;
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
      auto site = sites.find(user);
      bool direct = site != sites.end() && site->second.callee == index;
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
    ++abiPinnedFunctions;
    if (missedRemarks)
      info.function.emitRemark()
          << "dead capture elimination retained ABI: " << info.pinReason;
  }

  // Analyze functions in parallel without mutating shared liveness. Each
  // worker owns one observation slot, and merges remain in deterministic IR
  // order after the thread-pool wave joins.
  SmallVector<unsigned> deterministicOrder;
  deterministicOrder.reserve(functions.size());
  for (unsigned index = 0; index < functions.size(); ++index)
    deterministicOrder.push_back(index);

  // Seed semantic consumers. Direct invocation operands are dependencies on
  // the corresponding callee argument and are propagated below instead.
  SmallVector<llvm::BitVector> seedObservations;
  seedObservations.reserve(functions.size());
  for (const FunctionInfo &info : functions)
    seedObservations.emplace_back(info.live.size());
  const DenseMap<Operation *, BoundarySite> &frozenSites = sites;
  parallelFor(design.getContext(), 0, functions.size(), [&](size_t index) {
    const FunctionInfo &info = functions[index];
    sim::SimFuncOp function = info.function;
    if (info.pinned || function.isExternal())
      return;
    Block &entry = function.getBody().front();
    llvm::BitVector &observed = seedObservations[index];
    for (unsigned argumentIndex = 1; argumentIndex < entry.getNumArguments();
         ++argumentIndex) {
      BlockArgument argument = entry.getArgument(argumentIndex);
      for (OpOperand &use : argument.getUses()) {
        auto site = frozenSites.find(use.getOwner());
        if (site == frozenSites.end() || !site->second.callee ||
            use.getOperandNumber() >=
                functions[*site->second.callee].live.size()) {
          observed.set(argumentIndex);
          break;
        }
      }
    }
  });
  for (unsigned index : deterministicOrder)
    functions[index].live |= seedObservations[index];

  // Monotone boundary liveness reaches a deterministic fixed point. A cycle
  // made solely of direct argument forwarding has no seed and stays dead.
  // Snapshotting each wave avoids data races and makes propagation independent
  // of thread scheduling.
  bool changed;
  do {
    SmallVector<llvm::BitVector> liveSnapshot;
    SmallVector<llvm::BitVector> waveObservations;
    liveSnapshot.reserve(functions.size());
    waveObservations.reserve(functions.size());
    for (const FunctionInfo &info : functions) {
      liveSnapshot.push_back(info.live);
      waveObservations.emplace_back(info.live.size());
    }
    parallelFor(design.getContext(), 0, functions.size(), [&](size_t index) {
      const FunctionInfo &info = functions[index];
      sim::SimFuncOp function = info.function;
      if (info.pinned || function.isExternal())
        return;
      Block &entry = function.getBody().front();
      llvm::BitVector &observed = waveObservations[index];
      for (unsigned argumentIndex = 1; argumentIndex < entry.getNumArguments();
           ++argumentIndex) {
        if (liveSnapshot[index].test(argumentIndex))
          continue;
        for (OpOperand &use : entry.getArgument(argumentIndex).getUses()) {
          auto site = frozenSites.find(use.getOwner());
          if (site == frozenSites.end() || !site->second.callee)
            continue;
          const llvm::BitVector &calleeLive =
              liveSnapshot[*site->second.callee];
          if (use.getOperandNumber() < calleeLive.size() &&
              calleeLive.test(use.getOperandNumber())) {
            observed.set(argumentIndex);
            break;
          }
        }
      }
    });
    changed = false;
    for (unsigned index : deterministicOrder) {
      if (waveObservations[index].none())
        continue;
      functions[index].live |= waveObservations[index];
      changed = true;
    }
  } while (changed);

  for (FunctionInfo &info : functions) {
    if (info.pinned)
      continue;
    for (unsigned index = 1; index < info.live.size(); ++index)
      if (!info.live.test(index))
        info.erase.set(index);
  }

  // Preflight both the replacement type and every remaining SSA use before
  // the first call/spawn or function boundary is mutated. Correct liveness
  // leaves an erased argument used only by operands erased at the same wave.
  for (FunctionInfo &info : functions) {
    if (info.erase.none())
      continue;
    if (!info.function.getTypeWithoutArgs(info.erase)) {
      info.function.emitOpError(
          "cannot construct a function type without dead captures");
      signalPassFailure();
      return;
    }
    Block &entry = info.function.getBody().front();
    for (int64_t argumentIndex = info.erase.find_first(); argumentIndex >= 0;
         argumentIndex = info.erase.find_next(argumentIndex)) {
      for (OpOperand &use : entry.getArgument(argumentIndex).getUses()) {
        auto site = sites.find(use.getOwner());
        bool erasedAtBoundary =
            site != sites.end() && site->second.callee &&
            use.getOperandNumber() <
                functions[*site->second.callee].erase.size() &&
            functions[*site->second.callee].erase.test(use.getOperandNumber());
        if (erasedAtBoundary)
          continue;
        info.function.emitOpError()
            << "cannot erase argument #" << argumentIndex
            << " because it has a surviving use";
        signalPassFailure();
        return;
      }
    }
  }

  // Rewrite all direct sites before deleting the entry arguments whose only
  // remaining uses are those boundary operands.
  for (Operation *operation : orderedSites) {
    const BoundarySite &site = sites.lookup(operation);
    if (!site.callee)
      continue;
    const llvm::BitVector &erase = functions[*site.callee].erase;
    if (erase.none())
      continue;
    ArrayAttr argAttrs =
        isa<sim::SimCallOp>(operation)
            ? cast<sim::SimCallOp>(operation).getArgAttrsAttr()
            : cast<sim::SimSpawnOp>(operation).getArgAttrsAttr();
    filterPositionalMetadata(operation, argAttrs, erase);
    eraseInvocationOperands(operation, erase);
    if (isa<sim::SimCallOp>(operation))
      callOperandsRemoved += erase.count();
    else
      spawnOperandsRemoved += erase.count();
  }

  for (FunctionInfo &info : functions) {
    if (info.erase.none())
      continue;
    updateBindings(info.function, info.erase);
    if (failed(info.function.eraseArguments(info.erase)))
      llvm_unreachable("function argument erasure was preflighted");
    ++functionsPruned;
    argumentsRemoved += info.erase.count();
  }
}

} // namespace
} // namespace obelisk
