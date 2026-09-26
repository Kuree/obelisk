//===- SimulationAOTMaterialization.cpp - Native AOT LLVM plan --------===//

#include "SimulationAOTPlanning.h"
#include "SimulationEvalNBAQueue.h"
#include "SimulationEvalReadySet.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/LoopLikeInterface.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include <functional>
#include <numeric>

using namespace mlir;

namespace obelisk::detail {

static uint32_t fanoutRoute(const obelisk_rt_static_fanout_entry &entry) {
  return entry.reserved & OBELISK_RT_FANOUT_ROUTE_MASK;
}

using GeneratedTransitionRange =
    std::tuple<uint32_t, uint64_t, uint64_t, unsigned>;

static std::optional<uint64_t> constantU64(Value value) {
  IntegerAttr integer;
  if (auto constant = value.getDefiningOp<LLVM::ConstantOp>())
    integer = dyn_cast<IntegerAttr>(constant.getValue());
  else if (auto constant = value.getDefiningOp<arith::ConstantOp>())
    integer = dyn_cast<IntegerAttr>(constant.getValue());
  return integer ? std::optional<uint64_t>{integer.getValue().getZExtValue()}
                 : std::nullopt;
}

static bool isGeneratedEvalBody(
    sim::SimFuncOp function,
    const llvm::StringSet<> *selectedRawBodies = nullptr) {
  if (selectedRawBodies && function->hasAttr("obelisk.eval.raw_captures") &&
      !selectedRawBodies->contains(function.getSymName()))
    return false;
  return !function->hasAttr(evalRuntimeNBARequiredAttr) &&
         (function->hasAttr("obelisk.eval.raw_captures") ||
          function->hasAttr("obelisk.eval.path_known_predicate") ||
          function->hasAttr("obelisk.eval.selected_two_state"));
}

static SmallVector<sim::SimFuncOp>
collectGeneratedEvalCallClosure(
    ModuleOp module, const llvm::StringSet<> *selectedRawBodies = nullptr) {
  // This walk does not mutate symbols. Index each owning design once instead
  // of scanning all code units for every edge of the generated call graph.
  SymbolTableCollection symbolTables;
  SmallVector<sim::SimFuncOp> closure;
  SmallVector<sim::SimFuncOp> pending;
  llvm::SmallPtrSet<Operation *, 16> visited;
  module.walk([&](sim::SimFuncOp function) {
    if (isGeneratedEvalBody(function, selectedRawBodies))
      pending.push_back(function);
  });
  while (!pending.empty()) {
    sim::SimFuncOp function = pending.pop_back_val();
    if (!visited.insert(function.getOperation()).second)
      continue;
    closure.push_back(function);
    sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
    function.walk([&](Operation *operation) {
      if (!design)
        return;
      StringRef calleeName;
      if (auto call = dyn_cast<sim::SimCallOp>(operation))
        calleeName = call.getCallee();
      else if (auto call = dyn_cast<LLVM::CallOp>(operation)) {
        if (!call.getCallee())
          return;
        calleeName = *call.getCallee();
      } else {
        return;
      }
      if (sim::SimFuncOp callee = symbolTables.lookupSymbolIn<sim::SimFuncOp>(
              design, StringAttr::get(module.getContext(), calleeName)))
        pending.push_back(callee);
    });
  }
  return closure;
}

static FailureOr<SmallVector<GeneratedTransitionRange>>
collectGeneratedTransitionRanges(ModuleOp module,
                                 ArrayRef<NativeDirectFragment> fragments,
                                 const llvm::StringSet<> *selectedRawBodies) {
  SmallVector<GeneratedTransitionRange> ranges;
  LogicalResult valid = success();
  for (sim::SimFuncOp function :
       collectGeneratedEvalCallClosure(module, selectedRawBodies)) {
    if (failed(valid))
      break;
    unsigned directFragment = UINT_MAX;
    if (auto identity = function->getAttrOfType<IntegerAttr>(
            "obelisk.eval.direct_fragment");
        identity && identity.getInt() >= 0 &&
        static_cast<uint64_t>(identity.getInt()) < fragments.size())
      directFragment = static_cast<unsigned>(identity.getUInt());
    function.walk([&](LLVM::CallOp call) {
      if (failed(valid) || !call.getCallee() ||
          *call.getCallee() != "obelisk_rt_v1_scheduler_static_transition")
        return;
      ValueRange arguments = call.getArgOperands();
      if (arguments.size() != 8) {
        valid = call.emitError("malformed static transition ABI");
        return;
      }
      std::optional<uint64_t> staticState = constantU64(arguments[1]);
      std::optional<uint64_t> lowBit = constantU64(arguments[2]);
      std::optional<uint64_t> bitWidth = constantU64(arguments[3]);
      if (!staticState || !lowBit || !bitWidth || *bitWidth == 0 ||
          *bitWidth > 64) {
        valid = call.emitError("eval transition is not a fixed scalar range");
        return;
      }
      ranges.emplace_back(static_cast<uint32_t>(*staticState), *lowBit,
                          *bitWidth, directFragment);
    });
  }
  if (failed(valid))
    return failure();
  return ranges;
}

struct DynamicEvalNBAProofContext {
  const NativeStateLayout &stateLayout;
  const NativeStaticNBAPlan &staticNBAPlan;
  ArrayRef<obelisk_rt_static_fanout_entry> fanoutEntries;
  ArrayRef<NativeDirectFragment> directFragments;
  ArrayRef<std::string> mergedExecutors;
  ArrayRef<unsigned> periodicEntryRecords;
  ArrayRef<uint32_t> periodicOwnerBits;
  ArrayRef<NativePeriodicClock> periodicClocks;
  ArrayRef<NativePeriodicAlias> periodicAliases;
  ArrayRef<GeneratedTransitionRange> generatedTransitionRanges;
  sim::ComputeGraphAttr computeGraph;
  ArrayRef<uint8_t> orderedRootClosed;
};

struct DynamicEvalNBAProof {
  bool eligible = false;
  bool periodicWideLatch = false;
  bool orderedWide = false;
  uint32_t commitRegion = UINT32_MAX;
  std::optional<unsigned> periodicRecord;
  bool exclusivePeriodicIngress = false;
  unsigned periodicIngressCount = 0;
  unsigned nonPeriodicIngressCount = 0;
  unsigned periodicIngressTransitionConflicts = 0;
  bool uniqueSemanticRootSite = false;
  size_t rootSiteCount = 0;
  size_t semanticRootSiteCount = 0;
  uint64_t firstRootSite = UINT64_MAX;
  uint64_t lastRootSite = UINT64_MAX;
  bool siteExecutesAtMostOnce = false;
};

/// Return true only when `operation` cannot be revisited through its enclosing
/// function's structured or CFG control flow. Generated call-graph
/// multiplicity is checked separately before this local fact is consumed.
static bool locallyExecutesAtMostOnce(Operation *operation,
                                      sim::SimFuncOp enclosing) {
  for (Operation *ancestor = operation->getParentOp();
       ancestor && ancestor != enclosing.getOperation();
       ancestor = ancestor->getParentOp())
    if (isa<LoopLikeOpInterface>(ancestor))
      return false;

  Block *origin = operation->getBlock();
  SmallVector<Block *, 8> worklist;
  for (Block *successor : origin->getTerminator()->getSuccessors())
    worklist.push_back(successor);
  llvm::SmallPtrSet<Block *, 16> visited;
  while (!worklist.empty()) {
    Block *block = worklist.pop_back_val();
    if (block == origin)
      return false;
    if (!visited.insert(block).second)
      continue;
    for (Block *successor : block->getTerminator()->getSuccessors())
      worklist.push_back(successor);
  }
  return true;
}

static uint32_t
getDynamicNBACommitRegion(sim::SimFuncOp function,
                          const DynamicEvalNBAProofContext &proofContext) {
  if (auto identity =
          function->getAttrOfType<IntegerAttr>("obelisk.eval.direct_fragment");
      identity && identity.getInt() >= 0 &&
      static_cast<uint64_t>(identity.getInt()) <
          proofContext.directFragments.size() &&
      proofContext.computeGraph) {
    uint32_t graphRegion = UINT32_MAX;
    const NativeDirectFragment &direct =
        proofContext.directFragments[identity.getUInt()];
    for (uint32_t fragmentID : direct.fragmentIDs) {
      if (fragmentID >= proofContext.computeGraph.getNodes().size())
        return UINT32_MAX;
      auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
          proofContext.computeGraph.getNodes()[fragmentID]);
      if (!fragment)
        continue;
      uint32_t region = UINT32_MAX;
      if (fragment.getRegion() == sim::ComputeRegionKind::Active)
        region = OBELISK_RT_REGION_ACTIVE;
      else if (fragment.getRegion() == sim::ComputeRegionKind::Reactive)
        region = OBELISK_RT_REGION_REACTIVE;
      if (region == UINT32_MAX)
        return UINT32_MAX;
      if (graphRegion != UINT32_MAX && graphRegion != region)
        return UINT32_MAX;
      graphRegion = region;
    }
    if (graphRegion != UINT32_MAX)
      return graphRegion + 2;
  }

  uint32_t homeRegion = getRuntimeEventRegion(function.getHomeRegion());
  ArrayAttr owners =
      function->getAttrOfType<ArrayAttr>("obelisk.eval.source_owners");
  sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
  if (!owners || !design)
    return homeRegion == OBELISK_RT_REGION_ACTIVE ||
                   homeRegion == OBELISK_RT_REGION_REACTIVE
               ? homeRegion + 2
               : UINT32_MAX;

  uint32_t semanticRegion = UINT32_MAX;
  for (Attribute attribute : owners) {
    auto owner = dyn_cast<DictionaryAttr>(attribute);
    auto codeUnit =
        owner ? owner.getAs<IntegerAttr>("code_unit") : IntegerAttr{};
    if (!codeUnit || codeUnit.getInt() < 0)
      return UINT32_MAX;
    sim::SimFuncOp source;
    design.walk([&](sim::SimFuncOp candidate) {
      if (!source && candidate.getCodeUnitIdAttr() &&
          candidate.getCodeUnitIdAttr().getUInt() == codeUnit.getUInt())
        source = candidate;
    });
    uint32_t sourceRegion =
        source ? getRuntimeEventRegion(source.getHomeRegion()) : UINT32_MAX;
    if (sourceRegion != OBELISK_RT_REGION_ACTIVE &&
        sourceRegion != OBELISK_RT_REGION_REACTIVE)
      return UINT32_MAX;
    sourceRegion += 2;
    if (semanticRegion != UINT32_MAX && semanticRegion != sourceRegion)
      return UINT32_MAX;
    semanticRegion = sourceRegion;
  }
  return semanticRegion;
}

static FailureOr<DynamicEvalNBAProof>
proveDynamicEvalNBA(LLVM::CallOp call,
                    const DynamicEvalNBAProofContext &proofContext,
                    bool enclosingExecutesAtMostOnce) {
  ValueRange arguments = call.getArgOperands();
  if (arguments.size() != 9)
    return call.emitError("malformed static NBA ABI"), failure();
  const NativeStateLayout &stateLayout = proofContext.stateLayout;
  const NativeStaticNBAPlan &staticNBAPlan = proofContext.staticNBAPlan;
  ArrayRef<obelisk_rt_static_nba_site> nbaSites = staticNBAPlan.sites;
  std::optional<uint64_t> site = constantU64(arguments[1]);
  std::optional<uint64_t> width = constantU64(arguments[6]);
  auto root = site ? staticNBAPlan.siteRoots.find(*site)
                   : staticNBAPlan.siteRoots.end();
  auto offsetCall = arguments[5].getDefiningOp<LLVM::CallOp>();
  if (!offsetCall) {
    Operation *selected = arguments[5].getDefiningOp();
    if (isa_and_nonnull<arith::SelectOp, LLVM::SelectOp>(selected)) {
      std::optional<uint64_t> trueConstant =
          constantU64(selected->getOperand(1));
      std::optional<uint64_t> falseConstant =
          constantU64(selected->getOperand(2));
      if (falseConstant && *falseConstant == UINT64_MAX)
        offsetCall = selected->getOperand(1).getDefiningOp<LLVM::CallOp>();
      else if (trueConstant && *trueConstant == UINT64_MAX)
        offsetCall = selected->getOperand(2).getDefiningOp<LLVM::CallOp>();
    }
  }

  DynamicEvalNBAProof proof;
  sim::SimFuncOp enclosing = call->getParentOfType<sim::SimFuncOp>();
  proof.commitRegion = enclosing
                           ? getDynamicNBACommitRegion(enclosing, proofContext)
                           : UINT32_MAX;
  if (enclosing)
    if (auto identity = enclosing->getAttrOfType<IntegerAttr>(
            "obelisk.eval.direct_fragment");
        identity && identity.getInt() >= 0 &&
        static_cast<uint64_t>(identity.getInt()) <
            proofContext.directFragments.size()) {
      const NativeDirectFragment &direct =
          proofContext.directFragments[identity.getUInt()];
      // The periodic entry may be the complete instance coordinator, while
      // this body retains an exact owner for disturbance/fallback ingress.
      // Resolve that exact identity too. The fanout proof below still requires
      // every ingress to be an unmodified periodic clock; being nested in a
      // periodic coordinator alone is not an at-most-once certificate.
      auto record = llvm::find(proofContext.mergedExecutors, direct.wrapper);
      if (record != proofContext.mergedExecutors.end())
        proof.periodicRecord = record - proofContext.mergedExecutors.begin();
    }

  auto periodicLocalBit = [&](uint32_t staticState,
                              uint64_t physicalBit) -> std::optional<uint64_t> {
    auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
      return candidate.handleID == staticState;
    });
    if (bound == stateLayout.bounds.end() || physicalBit < bound->offset ||
        physicalBit - bound->offset >= bound->width)
      return std::nullopt;
    return physicalBit - bound->offset;
  };
  auto isExactPeriodicIngress = [&](const auto &fanout) {
    if (fanout.bit_width == 0 ||
        fanoutRoute(fanout) == OBELISK_RT_FANOUT_RUNTIME ||
        fanoutRoute(fanout) == OBELISK_RT_FANOUT_PERIODIC_ALIAS)
      return false;
    auto periodicBitTouches = [&](uint64_t bit) {
      if (fanout.low_bit > bit || bit - fanout.low_bit >= fanout.bit_width)
        return false;
      return fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
             fanout.low_bit == bit;
    };
    bool clockIngress = llvm::any_of(
        proofContext.periodicClocks, [&](const NativePeriodicClock &clock) {
          std::optional<uint64_t> bit =
              periodicLocalBit(clock.staticState, clock.bitOffset);
          return bit && fanout.static_state == clock.staticState &&
                 periodicBitTouches(*bit);
        });
    bool aliasIngress = llvm::any_of(
        proofContext.periodicAliases, [&](const NativePeriodicAlias &alias) {
          std::optional<uint64_t> bit =
              periodicLocalBit(alias.targetStaticState, alias.targetBitOffset);
          return bit && fanout.static_state == alias.targetStaticState &&
                 periodicBitTouches(*bit);
        });
    return clockIngress || aliasIngress;
  };
  if (proof.periodicRecord) {
    bool sawIngress = false;
    proof.exclusivePeriodicIngress =
        llvm::all_of(proofContext.fanoutEntries, [&](const auto &fanout) {
          size_t fanoutIndex =
              static_cast<size_t>(&fanout - proofContext.fanoutEntries.data());
          uint32_t periodicOwner =
              fanoutIndex < proofContext.periodicOwnerBits.size() &&
                      proofContext.periodicOwnerBits[fanoutIndex] != UINT32_MAX
                  ? proofContext.periodicOwnerBits[fanoutIndex]
                  : fanout.merged_bit;
          bool exactPeriodic = isExactPeriodicIngress(fanout);
          // Runtime routes do not own a merged model bit. Their default bit
          // field can equal this record (especially record zero), but a
          // separate procedural clock waiter is not ingress to this writer.
          bool genericIngress =
              fanoutRoute(fanout) != OBELISK_RT_FANOUT_RUNTIME &&
              fanout.merged_bit == *proof.periodicRecord;
          bool periodicIngress =
              exactPeriodic && periodicOwner == *proof.periodicRecord;
          if (!genericIngress && !periodicIngress)
            return true;
          sawIngress = true;
          if (genericIngress && !exactPeriodic) {
            ++proof.nonPeriodicIngressCount;
            return false;
          }
          ++proof.periodicIngressCount;
          uint64_t end = fanout.low_bit + fanout.bit_width;
          bool conflict = llvm::any_of(
              proofContext.generatedTransitionRanges, [&](const auto &range) {
                auto [state, low, rangeWidth, sourceIndex] = range;
                bool periodicKernelSource = false;
                if (sourceIndex < proofContext.directFragments.size()) {
                  const NativeDirectFragment &source =
                      proofContext.directFragments[sourceIndex];
                  periodicKernelSource = llvm::any_of(
                      proofContext.periodicClocks,
                      [&](const NativePeriodicClock &clock) {
                        return source.actorSlot == clock.actorSlot &&
                               source.continuation == clock.continuation;
                      });
                  periodicKernelSource |= llvm::any_of(
                      proofContext.periodicAliases,
                      [&](const NativePeriodicAlias &alias) {
                        return source.actorSlot == alias.forwardingActorSlot &&
                               source.continuation ==
                                   alias.forwardingContinuation;
                      });
                }
                if (periodicKernelSource)
                  return false;
                return state == fanout.static_state && low < end &&
                       fanout.low_bit < low + rangeWidth;
              });
          proof.periodicIngressTransitionConflicts += conflict;
          return !conflict;
        });
    proof.exclusivePeriodicIngress &= sawIngress;
  }

  unsigned matchingSiteCalls = 0;
  auto semanticOriginFor = [&](uint64_t siteID) {
    auto origin = staticNBAPlan.siteSemanticOrigins.find(siteID);
    return origin == staticNBAPlan.siteSemanticOrigins.end() ? siteID
                                                             : origin->second;
  };
  if (site && enclosing) {
    uint64_t semanticSite = semanticOriginFor(*site);
    enclosing.walk([&](LLVM::CallOp other) {
      if (!other.getCallee() ||
          *other.getCallee() != "obelisk_rt_v1_scheduler_static_nba" ||
          other.getArgOperands().size() != 9)
        return;
      std::optional<uint64_t> otherSite =
          constantU64(other.getArgOperands()[1]);
      matchingSiteCalls +=
          otherSite && semanticOriginFor(*otherSite) == semanticSite;
    });
  }
  proof.siteExecutesAtMostOnce =
      matchingSiteCalls == 1 && enclosingExecutesAtMostOnce &&
      locallyExecutesAtMostOnce(call.getOperation(), enclosing);

  auto fixedHandle = constantU64(arguments[5]);
  if (!fixedHandle)
    fixedHandle = resolveCFGConstantInteger(arguments[5]);
  obelisk_rt_stable_handle_v1 fixedDestination{};
  bool fixedRoot =
      fixedHandle && root != staticNBAPlan.siteRoots.end() &&
      root->second < staticNBAPlan.roots.size() &&
      obelisk_rt_stable_handle_decode(*fixedHandle, &fixedDestination) &&
      fixedDestination.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
      fixedDestination.id == staticNBAPlan.roots[root->second].static_state &&
      fixedDestination.offset >= 0 && width &&
      static_cast<uint64_t>(fixedDestination.offset) <=
          staticNBAPlan.roots[root->second].bit_width &&
      *width <= staticNBAPlan.roots[root->second].bit_width -
                    static_cast<uint64_t>(fixedDestination.offset);
  bool dynamicRoot =
      offsetCall && offsetCall.getCallee() &&
      *offsetCall.getCallee() == "obelisk_rt_v1_native_handle_offset" &&
      offsetCall.getArgOperands().size() == 2;
  bool commonDynamicRoot =
      site && width && *width != 0 && *width <= 64 &&
      root != staticNBAPlan.siteRoots.end() &&
      root->second < staticNBAPlan.roots.size() &&
      *width <= staticNBAPlan.roots[root->second].bit_width &&
      root->second < staticNBAPlan.generatedOffsets.size() &&
      (fixedRoot || dynamicRoot) &&
      (staticNBAPlan.generatedOffsets[root->second] & 7) == 0 &&
      proof.commitRegion != UINT32_MAX;
  bool directAccumulatorCandidate =
      commonDynamicRoot &&
      root->second < staticNBAPlan.generatedAccumulators.size() &&
      !staticNBAPlan.generatedAccumulators[root->second].empty() &&
      staticNBAPlan.roots[root->second].bit_width <= 64;
  llvm::SmallDenseSet<uint64_t, 4> semanticRootSites;
  if (root != staticNBAPlan.siteRoots.end())
    for (const auto &candidate : nbaSites)
      if (candidate.root == root->second) {
        ++proof.rootSiteCount;
        if (proof.firstRootSite == UINT64_MAX)
          proof.firstRootSite = candidate.site;
        proof.lastRootSite = candidate.site;
        semanticRootSites.insert(semanticOriginFor(candidate.site));
      }
  proof.semanticRootSiteCount = semanticRootSites.size();
  proof.uniqueSemanticRootSite = site && semanticRootSites.size() == 1;
  // A root accumulator stores only the final value and publishes one
  // old-to-final transition at the NBA barrier. Each staged write merges into
  // it under its bit mask in execution order, so the final value is the one
  // IEEE 1800-2023 4.6(b) and 10.4.2 require. What the accumulator drops is
  // every intermediate update event, for example 0 -> 1 -> 0. A merge-safe
  // root is one whose intermediate values no process can observe, or whose
  // only observers wait for any change and are served by the accumulator's
  // transient mask (9.4.2). For such a root any number of statements and
  // executions may merge; any other root keeps the ordered queue below.
  bool mergeSafe = root != staticNBAPlan.siteRoots.end() &&
                   root->second < staticNBAPlan.mergeSafeRoots.size() &&
                   staticNBAPlan.mergeSafeRoots[root->second];
  bool directAccumulator = directAccumulatorCandidate && mergeSafe;
  // The periodic fast loop owns the design-side NBA handoff only. Reactive
  // owners require the later Re-NBA phase, which remains runtime scheduled.
  // Disjoint site masks do not make separate update events interchangeable:
  // a change or edge expression can span those lanes (LRM 9.4.2). Only a
  // merge-safe root may give each independent site its own final-value
  // latch; otherwise use the ordered queue below.
  bool independentSites =
      proof.uniqueSemanticRootSite ||
      (root != staticNBAPlan.siteRoots.end() &&
       root->second < staticNBAPlan.independentSiteWrites.size() &&
       staticNBAPlan.independentSiteWrites[root->second]);
  bool latchSafe =
      mergeSafe ||
      (root != staticNBAPlan.siteRoots.end() &&
       root->second < staticNBAPlan.changeWatchedRoots.size() &&
       staticNBAPlan.changeWatchedRoots[root->second]);
  proof.periodicWideLatch = commonDynamicRoot && latchSafe &&
                            independentSites &&
                            proof.exclusivePeriodicIngress &&
                            proof.siteExecutesAtMostOnce &&
                            proof.commitRegion == OBELISK_RT_REGION_NBA &&
                            staticNBAPlan.roots[root->second].bit_width > 64;
  // The generated queue records every enqueue and drains in execution order,
  // including overlapping sites and repeated executions of the same site.
  // Those cases cannot use a final-value latch, but do not need the runtime
  // NBA scheduler when the root and commit region are otherwise certified.
  proof.orderedWide = commonDynamicRoot &&
                      root->second < proofContext.orderedRootClosed.size() &&
                      proofContext.orderedRootClosed[root->second] &&
                      proof.commitRegion == OBELISK_RT_REGION_NBA &&
                      (!mergeSafe ||
                       staticNBAPlan.roots[root->second].bit_width > 64) &&
                      !directAccumulator &&
                      !proof.periodicWideLatch;
  proof.eligible =
      directAccumulator || proof.periodicWideLatch || proof.orderedWide;
  return proof;
}

// Wide port/storage copies use the generic snapshot ABI during state lowering.
// On a clean, fully owned generated route, publish its machine-word slices
// through the same exact fanout logic as narrow stores. All state bytes have
// already been written; no actor executes between these publications.
static void normalizeGeneratedWideTransitions(
    ModuleOp module, const llvm::DataLayout &dataLayout,
    const NativeStateLayout &layout, const NativeStaticFanoutPlan &fanout,
    const llvm::StringSet<> *selectedRawBodies) {
  NativeStateLayout clean = makeCleanEvalStateLayout(layout);
  SmallVector<LLVM::CallOp> calls;
  for (auto function :
       collectGeneratedEvalCallClosure(module, selectedRawBodies))
    function.walk([&](LLVM::CallOp call) {
      if (call.getCallee() &&
          *call.getCallee() == "obelisk_rt_v1_scheduler_signal_transition")
        calls.push_back(call);
    });
  for (LLVM::CallOp call : calls) {
    ValueRange args = call.getArgOperands();
    if (args.size() != 7)
      continue;
    auto width = constantU64(args[2]);
    if (!width || *width <= 64 || *width > UINT_MAX)
      continue;
    auto range = resolveDirectStaticStateRange(args[1], *width, &clean);
    if (!range || fanout.runtimeTransitionStates.contains(range->staticID))
      continue;
    OpBuilder builder(call);
    auto i64 = builder.getI64Type();
    for (uint64_t low = 0; low < *width; low += 64) {
      unsigned bits = std::min<uint64_t>(64, *width - low);
      uint64_t byteOffset = dataLayout.isLittleEndian()
                                ? low / 8
                                : (*width + 7) / 8 - low / 8 - (bits + 7) / 8;
      auto load = [&](Value snapshot) -> Value {
        if (snapshot.getDefiningOp<LLVM::ZeroOp>())
          return llvmConstant(builder, call.getLoc(), i64, 0);
        Value value = LLVM::LoadOp::create(
            builder, call.getLoc(), builder.getIntegerType(bits),
            byteGEP(builder, call.getLoc(), snapshot, byteOffset), 1);
        return bits == 64
                   ? value
                   : LLVM::ZExtOp::create(builder, call.getLoc(), i64, value)
                         .getResult();
      };
      auto publication = LLVM::CallOp::create(
          builder, call.getLoc(), TypeRange{},
          SymbolRefAttr::get(module.getContext(),
                             "obelisk_rt_v1_scheduler_static_transition"),
          ValueRange{args[0],
                     llvmConstant(builder, call.getLoc(), builder.getI32Type(),
                                  range->staticID),
                     llvmConstant(builder, call.getLoc(), i64,
                                  range->localOffset + low),
                     llvmConstant(builder, call.getLoc(), i64, bits),
                     load(args[3]), load(args[4]), load(args[5]),
                     load(args[6])});
      if (auto owner = call->getAttr(sim::metadata::evalSourceOwner))
        publication->setAttr(sim::metadata::evalSourceOwner, owner);
    }
    call.erase();
  }
}

FailureOr<bool> makeNativeEvalPlan(
    ModuleOp module, const llvm::DataLayout &dataLayout, uint32_t actorCount,
    ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    ArrayRef<obelisk_rt_static_actor_root> actorRoots,
    ArrayRef<NativeDirectFragment> directFragments,
    const NativeEvalOwnershipPlan &evalOwnership,
    sim::ComputeGraphAttr computeGraph,
    ArrayRef<NativePeriodicClock> periodicClocks,
    ArrayRef<NativePeriodicAlias> periodicAliases, bool enableDirectState,
    bool enableStaticNBA, bool enableStaticControl, bool enableStaticFanout,
    bool enableCleanSuperstep, bool fullyStatic, bool staticEvalIsland,
    bool rootSlotZero, const analysis::SimulationVPIAnalysis &vpi) {
  if (actorCount == 0 || executableNodes.empty())
    return module.emitError("AOT schedule has no executable actor nodes"),
           failure();
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  llvm::StringSet<> selectedEvalBodies;
  if (staticEvalIsland)
    for (const NativeDirectFragment &direct : directFragments) {
      if (!direct.body.empty())
        selectedEvalBodies.insert(direct.body);
      if (!direct.twoStateBody.empty())
        selectedEvalBodies.insert(direct.twoStateBody);
    }
  const llvm::StringSet<> *selectedRawBodies =
      staticEvalIsland ? &selectedEvalBodies : nullptr;

  // Route shells and runtime checkpoint wrappers can both reference this
  // tuple, including across a conservative late Eval handoff.
  if (failed(materializeEvalCheckpointHandoffGlobals(module)))
    return failure();

  FailureOr<ResolvedNativeEvalPlan> resolved =
      resolveNativeEvalPlan(module, executableNodes, stateLayout, staticNBAPlan,
                            staticFanoutPlan, directFragments, evalOwnership,
                            computeGraph, periodicClocks, periodicAliases);
  if (failed(resolved))
    return failure();
  // All input executors exist before plan materialization. Only their bodies
  // and attributes change below; newly emitted plan helpers are not executors.
  // Keep this snapshot local rather than retaining a table across IR phases.
  SymbolTable executorSymbols(module);
  SmallVector<GeneratedTransitionRange> generatedTransitionRanges;
  llvm::DenseMap<Operation *, DynamicEvalNBAProof> dynamicNBAProofs;
  if (!resolved->clockKernels.empty()) {
    // Normalize before the ingress/NBA proof so wide clock disturbances are
    // visible to the same range analysis as ordinary narrow publications.
    normalizeGeneratedWideTransitions(module, dataLayout, stateLayout,
                                      staticFanoutPlan, selectedRawBodies);
    FailureOr<SmallVector<GeneratedTransitionRange>> transitionRanges =
        collectGeneratedTransitionRanges(module, directFragments,
                                         selectedRawBodies);
    if (failed(transitionRanges))
      return failure();
    generatedTransitionRanges = std::move(*transitionRanges);
    // An ordinary helper has no single logical-process identity. Most of its
    // fixed transitions can still publish directly into generated ingress,
    // but an active-self-suppressed route needs the runtime's exact current
    // actor check. Reject only that overlap; all other helper transitions are
    // rewritten with the rest of the generated call closure below.
    bool unownedTransitionNeedsActiveSelfCheck =
        llvm::any_of(generatedTransitionRanges, [&](const auto &range) {
          auto [state, low, width, sourceIndex] = range;
          if (sourceIndex != UINT_MAX)
            return false;
          uint64_t end = low + width;
          return llvm::any_of(resolved->fanoutEntries, [&](const auto &entry) {
            return entry.static_state == state && entry.low_bit < end &&
                   low < entry.low_bit + entry.bit_width &&
                   (entry.reserved & OBELISK_RT_FANOUT_SUPPRESS_ACTIVE_SELF) !=
                       0;
          });
        });
    if (unownedTransitionNeedsActiveSelfCheck)
      return false;
    SmallVector<sim::SimFuncOp> evalClosure =
        collectGeneratedEvalCallClosure(module, selectedRawBodies);
    // The generated queue can order every execution of its own sites, but it
    // cannot interleave a runtime-owned NBA to the same overlapping root.
    // Original coroutine bodies and generated clones share semantic origins;
    // require each origin on an ordered root to have a generated owner.
    SmallVector<llvm::SmallDenseSet<uint64_t, 4>> generatedOrigins(
        staticNBAPlan.roots.size());
    auto semanticOrigin = [&](uint64_t site) {
      auto origin = staticNBAPlan.siteSemanticOrigins.find(site);
      return origin == staticNBAPlan.siteSemanticOrigins.end() ? site
                                                               : origin->second;
    };
    for (sim::SimFuncOp function : evalClosure) {
      if (!function->hasAttr("obelisk.eval.direct_fragment"))
        continue;
      function.walk([&](LLVM::CallOp call) {
        if (!call.getCallee() ||
            *call.getCallee() != "obelisk_rt_v1_scheduler_static_nba" ||
            call.getArgOperands().size() != 9)
          return;
        auto site = constantU64(call.getArgOperands()[1]);
        auto root = site ? staticNBAPlan.siteRoots.find(*site)
                         : staticNBAPlan.siteRoots.end();
        if (root != staticNBAPlan.siteRoots.end() &&
            root->second < generatedOrigins.size())
          generatedOrigins[root->second].insert(semanticOrigin(*site));
      });
    }
    SmallVector<uint8_t> orderedRootClosed(staticNBAPlan.roots.size(), 1);
    for (const obelisk_rt_static_nba_site &site : staticNBAPlan.sites)
      if (site.root >= orderedRootClosed.size() ||
          !generatedOrigins[site.root].contains(semanticOrigin(site.site))) {
        if (site.root < orderedRootClosed.size())
          orderedRootClosed[site.root] = 0;
      }
    DynamicEvalNBAProofContext proofContext{stateLayout,
                                            staticNBAPlan,
                                            resolved->fanoutEntries,
                                            directFragments,
                                            resolved->mergedExecutors,
                                            resolved->periodicEntryRecords,
                                            resolved->periodicOwnerBits,
                                            periodicClocks,
                                            periodicAliases,
                                            generatedTransitionRanges,
                                            computeGraph,
                                            orderedRootClosed};
    llvm::SmallPtrSet<Operation *, 16> evalClosureSet;
    for (sim::SimFuncOp function : evalClosure)
      evalClosureSet.insert(function.getOperation());
    DenseMap<Operation *, SmallVector<Operation *, 2>> incomingEvalCalls;
    SymbolTableCollection callSymbols;
    for (sim::SimFuncOp caller : evalClosure) {
      sim::SimDesignOp design = caller->getParentOfType<sim::SimDesignOp>();
      if (!design)
        continue;
      caller.walk([&](Operation *operation) {
        StringRef calleeName;
        if (auto call = dyn_cast<sim::SimCallOp>(operation))
          calleeName = call.getCallee();
        else if (auto call = dyn_cast<LLVM::CallOp>(operation)) {
          if (!call.getCallee())
            return;
          calleeName = *call.getCallee();
        } else {
          return;
        }
        sim::SimFuncOp callee = callSymbols.lookupSymbolIn<sim::SimFuncOp>(
            design, StringAttr::get(context, calleeName));
        if (callee && evalClosureSet.contains(callee.getOperation()))
          incomingEvalCalls[callee.getOperation()].push_back(operation);
      });
    }
    DenseMap<Operation *, bool> closureOnceMemo;
    llvm::SmallPtrSet<Operation *, 16> closureOnceVisiting;
    std::function<bool(sim::SimFuncOp)> closureExecutesAtMostOnce =
        [&](sim::SimFuncOp function) {
          auto memo = closureOnceMemo.find(function.getOperation());
          if (memo != closureOnceMemo.end())
            return memo->second;
          if (!closureOnceVisiting.insert(function.getOperation()).second)
            return false;
          ArrayRef<Operation *> incoming =
              incomingEvalCalls[function.getOperation()];
          bool once = incoming.empty();
          if (incoming.size() == 1) {
            Operation *call = incoming.front();
            sim::SimFuncOp caller = call->getParentOfType<sim::SimFuncOp>();
            once = caller && locallyExecutesAtMostOnce(call, caller) &&
                   closureExecutesAtMostOnce(caller);
          }
          closureOnceVisiting.erase(function.getOperation());
          closureOnceMemo[function.getOperation()] = once;
          return once;
        };
    LogicalResult valid = success();
    bool needsRuntimeFallback = false;
    for (sim::SimFuncOp function : evalClosure) {
      if (failed(valid))
        break;
      bool directBody = function->hasAttr("obelisk.eval.direct_fragment");
      function.walk([&](LLVM::CallOp call) {
        if (failed(valid) || !call.getCallee() ||
            *call.getCallee() != "obelisk_rt_v1_scheduler_static_nba")
          return;
        if (!directBody) {
          // Shared helpers may execute through multiple call sites or the
          // ordinary coroutine fallback. Keep their ordered NBA staging in
          // the runtime scheduler until an interprocedural path/count proof
          // and a private Eval clone both exist.
          needsRuntimeFallback = true;
          return;
        }
        FailureOr<DynamicEvalNBAProof> proof = proveDynamicEvalNBA(
            call, proofContext, closureExecutesAtMostOnce(function));
        if (failed(proof)) {
          valid = failure();
          return;
        }
        if (!proof->eligible)
          call.emitRemark("dynamic NBA is ineligible for generated eval")
              << " (commit-region=" << proof->commitRegion
              << ", periodic-entry=" << static_cast<bool>(proof->periodicRecord)
              << ", exclusive-periodic-ingress="
              << proof->exclusivePeriodicIngress
              << ", periodic-ingress-count=" << proof->periodicIngressCount
              << ", non-periodic-ingress-count="
              << proof->nonPeriodicIngressCount
              << ", periodic-ingress-transition-conflicts="
              << proof->periodicIngressTransitionConflicts
              << ", unique-semantic-root-site=" << proof->uniqueSemanticRootSite
              << ", site-once=" << proof->siteExecutesAtMostOnce << ")";
        needsRuntimeFallback |= !proof->eligible;
        dynamicNBAProofs.try_emplace(call.getOperation(), std::move(*proof));
      });
    }
    if (failed(valid))
      return failure();
    if (needsRuntimeFallback)
      return false;
  }
  module->setAttr("obelisk.eval.generated", UnitAttr::get(module.getContext()));
  ArrayRef<obelisk_rt_static_nba_root> nbaRoots = staticNBAPlan.roots;
  ArrayRef<obelisk_rt_static_nba_site> nbaSites = staticNBAPlan.sites;
  SmallVector<obelisk_rt_static_fanout_entry> indexedFanoutEntries =
      std::move(resolved->fanoutEntries);
  ArrayRef<obelisk_rt_static_fanout_entry> fanoutEntries = indexedFanoutEntries;
  SmallVector<NativeEvalClockKernel> clockKernels =
      std::move(resolved->clockKernels);
  SmallVector<obelisk_rt_native_merged_fragment> mergedFragments =
      std::move(resolved->mergedFragments);
  SmallVector<std::string> mergedExecutors =
      std::move(resolved->mergedExecutors);
  SmallVector<std::string> mergedTwoStateExecutors =
      std::move(resolved->mergedTwoStateExecutors);
  SmallVector<SmallVector<NativePromotionRange>> mergedPromotionRanges =
      std::move(resolved->mergedPromotionRanges);
  SmallVector<uint32_t> periodicOwnerBits =
      std::move(resolved->periodicOwnerBits);
  SmallVector<APInt> ownerSubsumptionMasks =
      std::move(resolved->ownerSubsumptionMasks);
  SmallVector<unsigned> periodicClosureRecords =
      std::move(resolved->periodicClosureRecords);
  uint32_t nbaTaintWordCount = resolved->nbaTaintWordCount;
  SmallVector<SmallVector<uint64_t>> recordNBATaintMasks =
      std::move(resolved->recordNBATaintMasks);
  llvm::BitVector nbaTaintedRecords = std::move(resolved->nbaTaintedRecords);
  bool prioritySignalHandoff = false;
  module.walk([&](sim::SimFuncOp function) {
    prioritySignalHandoff |=
        function->hasAttr("obelisk_sim.priority_signal_resume");
  });
  struct DynamicEvalNBA {
    uint32_t rootIndex;
    uint64_t site;
    uint64_t width;
    std::string offsetName;
    std::string valueName;
    std::string unknownName;
    std::string validName;
    bool queued = false;
  };
  SmallVector<DynamicEvalNBA> dynamicEvalNBAs;
  llvm::SmallDenseSet<uint32_t, 8> orderedNBARoots;
  for (auto &[operation, proof] : dynamicNBAProofs)
    if (proof.orderedWide) {
      auto call = cast<LLVM::CallOp>(operation);
      orderedNBARoots.insert(staticNBAPlan.siteRoots.lookup(
          *constantU64(call.getArgOperands()[1])));
    }
  const bool hasOrderedNBA = !orderedNBARoots.empty();
  if (hasOrderedNBA) {
    OpBuilder globals = OpBuilder::atBlockBegin(module.getBody());
    auto makeZero = [&](StringRef name, Type type) {
      auto global =
          LLVM::GlobalOp::create(globals, location, type, false,
                                 LLVM::Linkage::Internal, name, Attribute{}, 8);
      Block *init = new Block;
      global.getInitializerRegion().push_back(init);
      OpBuilder initBuilder = OpBuilder::atBlockBegin(init);
      LLVM::ReturnOp::create(initBuilder, location,
                             LLVM::ZeroOp::create(initBuilder, location, type));
    };
    makeZero(evalNBAQueueName, LLVM::LLVMStructType::getLiteral(
                                   context, {pointer, i32, i32, i32}));
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_eval_nba_reserve", i32,
                             {pointer, pointer});
  }
  for (const NativeDirectFragment &direct : directFragments)
    if (direct.tier2Convergence)
      for (StringRef name :
           {StringRef(direct.wrapper), StringRef(direct.twoStateWrapper)})
        if (!name.empty())
          if (auto function = executorSymbols.lookup<LLVM::LLVMFuncOp>(name))
            function->setAttr(sim::metadata::evalTier2Convergence,
                              UnitAttr::get(context));
  auto ownerMayTaintNBA = [&](unsigned recordIndex) {
    return recordIndex < nbaTaintedRecords.size() &&
           nbaTaintedRecords.test(recordIndex);
  };
  auto markOwnerNBATaint = [&](unsigned recordIndex) {
    if (!ownerMayTaintNBA(recordIndex) || nbaTaintWordCount == 0)
      return;
    Value taintBase = LLVM::AddressOfOp::create(
        builder, location, pointer,
        "__obelisk_eval_step_four_state_nba_roots_v1");
    for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
      uint64_t mask = recordNBATaintMasks[recordIndex][word];
      if (mask == 0)
        continue;
      Value address = byteGEP(builder, location, taintBase,
                              uint64_t{word} * sizeof(uint64_t));
      Value old = LLVM::LoadOp::create(builder, location, i64, address, 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::OrIOp::create(builder, location, old,
                               llvmConstant(builder, location, i64, mask)),
          address, 8);
    }
  };
  // Static time/region control and exact actor fanout are independent of VPI
  // reads. Writable VPI hands dirty roots and the affected event slot to the
  // existing guarded state/control paths; the exact dependency table remains
  // valid and can continue to wake native actors without subscriptions.
  bool closedEvalSchedule = fullyStatic || staticEvalIsland;
  bool staticControlEnabled =
      enableStaticControl && closedEvalSchedule && vpi.hasComputeGraph();
  bool staticFanoutEnabled = enableStaticFanout && staticFanoutPlan.exact &&
                             closedEvalSchedule && vpi.hasComputeGraph();
  bool guardedFanoutEnabled = !staticFanoutEnabled && staticFanoutPlan.exact &&
                              closedEvalSchedule && vpi.hasComputeGraph();
  bool guardedSpecializationEnabled =
      vpi.allowsWrite() && (enableDirectState || enableStaticNBA);
  bool cleanSuperstepEnabled = enableCleanSuperstep && staticControlEnabled &&
                               staticFanoutPlan.exact && closedEvalSchedule;
  // Retain direct kernels only for a certified complete schedule or a closed
  // eval island. Other hybrid/guarded schedules keep the same fanout metadata
  // but use its exact compute-node fallback identities transactionally.
  if (!cleanSuperstepEnabled) {
    clockKernels.clear();
    mergedFragments.clear();
    mergedExecutors.clear();
    mergedTwoStateExecutors.clear();
    mergedPromotionRanges.clear();
    for (obelisk_rt_static_fanout_entry &entry : indexedFanoutEntries) {
      entry.kernel = 0;
      entry.merged_bit = 0;
    }
  }
  uint64_t graphLayoutChecksum = 0;
  if (auto image =
          module->getAttrOfType<DenseI8ArrayAttr>("obelisk.bytecode.image")) {
    ArrayRef<int8_t> bytes = image.asArrayRef();
    if (bytes.size() < 40)
      return module.emitError("embedded bytecode checksum is truncated");
    for (unsigned byte = 0; byte != 8; ++byte)
      graphLayoutChecksum |= uint64_t{static_cast<uint8_t>(bytes[32 + byte])}
                             << (byte * 8);
  }
  Type stateType = LLVM::LLVMArrayType::get(pointer, actorCount);
  constexpr StringLiteral stateName = "__obelisk_aot_schedule_state_v1";
  constexpr StringLiteral nodesName = "__obelisk_aot_schedule_nodes_v1";
  constexpr StringLiteral nbaRootsName = "__obelisk_aot_nba_roots_v1";
  constexpr StringLiteral nbaSitesName = "__obelisk_aot_nba_sites_v1";
  constexpr StringLiteral nbaDirtyRootsName =
      "__obelisk_aot_nba_dirty_roots_v1";
  constexpr StringLiteral nbaDirtySummaryName =
      "__obelisk_aot_nba_dirty_summary_v1";
  constexpr StringLiteral fanoutName = "__obelisk_aot_static_fanout_v1";
  constexpr StringLiteral actorRootsName =
      "__obelisk_aot_static_actor_roots_v1";
  constexpr StringLiteral clockKernelsName = "__obelisk_aot_clock_kernels_v1";
  constexpr StringLiteral mergedFragmentsName =
      "__obelisk_aot_merged_fragments_v1";
  constexpr StringLiteral bindName = "__obelisk_aot_schedule_bind_v1";
  constexpr StringLiteral runName = "__obelisk_aot_schedule_run_v1";
  constexpr StringLiteral snapshotName = "__obelisk_aot_schedule_snapshot_v1";
  constexpr StringLiteral nbaCommitName = "__obelisk_aot_static_nba_commit_v1";
  constexpr StringLiteral runtimeNBACommitName =
      "__obelisk_aot_runtime_nba_commit_v1";
  constexpr StringLiteral nbaKnownName = "__obelisk_eval_nba_known_v1";
  constexpr StringLiteral evalStepFourStateFallbackName =
      "__obelisk_eval_step_four_state_fallback_v1";
  constexpr StringLiteral evalStepFourStateNBARootsName =
      "__obelisk_eval_step_four_state_nba_roots_v1";
  constexpr StringLiteral evalFastNBARootsName =
      "__obelisk_eval_fast_nba_roots_v1";
  constexpr StringLiteral periodicTerminationName =
      "__obelisk_periodic_termination_v1";
  constexpr StringLiteral promotionReadyName =
      "__obelisk_eval_promotion_ready_v1";
  constexpr StringLiteral promotionLatchedName =
      "__obelisk_eval_promotion_latched_v1";
  constexpr StringLiteral promotionKernelLatchedName =
      "__obelisk_eval_kernel_promotion_latched_v1";
  constexpr StringLiteral promotionPendingMaskName =
      "__obelisk_eval_promotion_pending_mask_v1";
  constexpr StringLiteral promotionInvalidateName =
      "__obelisk_eval_promotion_invalidate_v1";
  constexpr StringLiteral promotionQueryName =
      "__obelisk_eval_promotion_query_v1";
  constexpr StringLiteral planName = "__obelisk_aot_schedule_plan_v1";
  SmallVector<std::string> promotionKernelReadyNames(mergedFragments.size());
  unsigned ownerCount = std::max<size_t>(64, mergedFragments.size());
  APInt allOwners = APInt::getAllOnes(ownerCount);
  const runtime::ReadySetLayout readyLayout(ownerCount);
  APInt pathGuardedOwnerMask(ownerCount, 0);

  builder.setInsertionPointToStart(module.getBody());
  auto state = LLVM::GlobalOp::create(builder, location, stateType, false,
                                      LLVM::Linkage::Internal, stateName,
                                      Attribute{}, 8);
  Block *initializer = new Block;
  state.getInitializerRegion().push_back(initializer);
  builder.setInsertionPointToStart(initializer);
  LLVM::ReturnOp::create(builder, location,
                         LLVM::ZeroOp::create(builder, location, stateType));

  builder.setInsertionPointToStart(module.getBody());
  auto periodicTermination = LLVM::GlobalOp::create(
      builder, location, pointer, false, LLVM::Linkage::Internal,
      periodicTerminationName, Attribute{}, 8);
  Block *periodicTerminationInitializer = new Block;
  periodicTermination.getInitializerRegion().push_back(
      periodicTerminationInitializer);
  builder.setInsertionPointToStart(periodicTerminationInitializer);
  LLVM::ReturnOp::create(builder, location,
                         LLVM::ZeroOp::create(builder, location, pointer));

  builder.setInsertionPointToStart(module.getBody());
  (void)LLVM::GlobalOp::create(
      builder, location, builder.getI8Type(), false, LLVM::Linkage::Internal,
      evalStepFourStateFallbackName, builder.getI8IntegerAttr(0), 1);
  if (nbaTaintWordCount != 0) {
    Type taintType = LLVM::LLVMArrayType::get(i64, nbaTaintWordCount);
    builder.setInsertionPointToStart(module.getBody());
    auto taint = LLVM::GlobalOp::create(
        builder, location, taintType, false, LLVM::Linkage::Internal,
        evalStepFourStateNBARootsName, Attribute{}, 8);
    Block *initializer = new Block;
    taint.getInitializerRegion().push_back(initializer);
    builder.setInsertionPointToStart(initializer);
    LLVM::ReturnOp::create(builder, location,
                           LLVM::ZeroOp::create(builder, location, taintType));
    builder.setInsertionPointToStart(module.getBody());
    auto fastRoots = LLVM::GlobalOp::create(
        builder, location, taintType, false, LLVM::Linkage::Internal,
        evalFastNBARootsName, Attribute{}, 8);
    Block *fastRootsInitializer = new Block;
    fastRoots.getInitializerRegion().push_back(fastRootsInitializer);
    builder.setInsertionPointToStart(fastRootsInitializer);
    LLVM::ReturnOp::create(builder, location,
                           LLVM::ZeroOp::create(builder, location, taintType));
  }
  builder.setInsertionPointToStart(module.getBody());
  auto promotionLatched = LLVM::GlobalOp::create(
      builder, location, builder.getI8Type(), false, LLVM::Linkage::Internal,
      promotionLatchedName, builder.getI8IntegerAttr(0), 1);


  Type kernelLatchType =
      LLVM::LLVMArrayType::get(builder.getI8Type(), mergedFragments.size());
  builder.setInsertionPointToStart(module.getBody());
  auto promotionKernelLatched = LLVM::GlobalOp::create(
      builder, location, kernelLatchType, false, LLVM::Linkage::Internal,
      promotionKernelLatchedName, Attribute{}, 1);
  Block *kernelLatchInitializer = new Block;
  promotionKernelLatched.getInitializerRegion().push_back(
      kernelLatchInitializer);
  builder.setInsertionPointToStart(kernelLatchInitializer);
  LLVM::ReturnOp::create(
      builder, location,
      LLVM::ZeroOp::create(builder, location, kernelLatchType));

  APInt initialPromotionPendingMask(ownerCount, 0);
  for (auto [index, executor] : llvm::enumerate(mergedTwoStateExecutors))
    if (!executor.empty())
      initialPromotionPendingMask.setBit(mergedFragments[index].bit);
  builder.setInsertionPointToStart(module.getBody());
  Type pendingMaskType =
      LLVM::LLVMArrayType::get(i64, initialPromotionPendingMask.getNumWords());
  auto promotionPendingMask = LLVM::GlobalOp::create(
      builder, location, pendingMaskType, false, LLVM::Linkage::Internal,
      promotionPendingMaskName, Attribute{}, 8);
  Block *pendingInitializer = new Block;
  promotionPendingMask.getInitializerRegion().push_back(pendingInitializer);
  builder.setInsertionPointToStart(pendingInitializer);
  Value pendingInitial =
      LLVM::ZeroOp::create(builder, location, pendingMaskType);
  for (unsigned word = 0; word != initialPromotionPendingMask.getNumWords();
       ++word)
    pendingInitial = LLVM::InsertValueOp::create(
        builder, location, pendingInitial,
        llvmConstant(builder, location, i64,
                     ownerMaskWord(initialPromotionPendingMask, word)),
        ArrayRef<int64_t>{word});
  LLVM::ReturnOp::create(builder, location, pendingInitial);

  // Preserve the revision-verified physical proof ranges for the final LLVM
  // reverse index. These describe value-domain certificates, not new owners.
  SmallVector<Attribute> promotionDependencies;
  for (auto [index, executor] : llvm::enumerate(mergedTwoStateExecutors)) {
    if (executor.empty() || mergedPromotionRanges[index].empty())
      continue;
    SmallVector<int64_t> encoded;
    for (const auto &range : mergedPromotionRanges[index]) {
      encoded.push_back(range.bitOffset);
      encoded.push_back(range.bitWidth);
    }
    promotionDependencies.push_back(builder.getDictionaryAttr(
        {builder.getNamedAttr("latch", builder.getI64IntegerAttr(index)),
         builder.getNamedAttr("pending_bit", builder.getI64IntegerAttr(
                                                 mergedFragments[index].bit)),
         builder.getNamedAttr("ranges",
                              builder.getDenseI64ArrayAttr(encoded))}));
  }
  promotionKernelLatched->setAttr("obelisk.eval.kernel_proof_dependencies",
                                  builder.getArrayAttr(promotionDependencies));

  // Scan an outlined owner's exact canonical closure independently. A
  // dormant X-valued instance therefore cannot keep unrelated clock owners
  // on their four-state route.
  for (auto [index, twoStateExecutor] :
       llvm::enumerate(mergedTwoStateExecutors)) {
    if (twoStateExecutor.empty())
      continue;
    std::string readyName =
        (Twine("__obelisk_eval_kernel_promotion_ready_v1_") + Twine(index))
            .str();
    promotionKernelReadyNames[index] = readyName;
    builder.setInsertionPointToEnd(module.getBody());
    auto ready = LLVM::LLVMFuncOp::create(
        builder, location, readyName,
        LLVM::LLVMFunctionType::get(builder.getI1Type(), {}, false));
    ready->setAttr("passthrough", builder.getArrayAttr(
                                      {builder.getStringAttr("alwaysinline")}));
    Block *readyEntry = ready.addEntryBlock(builder);
    Block *readyScan = new Block;
    Block *readyLatched = new Block;
    ready.getBody().push_back(readyScan);
    ready.getBody().push_back(readyLatched);
    builder.setInsertionPointToStart(readyEntry);
    Value latches = LLVM::AddressOfOp::create(
        builder, location, pointer, promotionKernelLatched.getSymName());
    Value latchAddress = byteGEP(builder, location, latches, index);
    Value latch = LLVM::LoadOp::create(builder, location, builder.getI8Type(),
                                       latchAddress, 1);
    Value isLatched = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, latch,
        llvmConstant(builder, location, builder.getI8Type(), 0));
    cf::CondBranchOp::create(builder, location, isLatched, readyLatched,
                             ValueRange{}, readyScan, ValueRange{});
    builder.setInsertionPointToStart(readyLatched);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI1Type(), 1));

    builder.setInsertionPointToStart(readyScan);
    Value unknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                              "__obelisk_state_unknown");
    llvm::SmallDenseMap<uint64_t, uint8_t, 16> scanByteMasks;
    for (const NativePromotionRange &range : mergedPromotionRanges[index]) {
      if (range.bitWidth == 0 || range.bitOffset > stateLayout.bitCount ||
          range.bitWidth > stateLayout.bitCount - range.bitOffset)
        return module.emitError("kernel promotion range exceeds native state");
      uint64_t firstByte = range.bitOffset / 8;
      uint64_t lastBit = range.bitOffset + range.bitWidth;
      uint64_t lastByte = (lastBit + 7) / 8;
      for (uint64_t byte = firstByte; byte != lastByte; ++byte) {
        uint8_t mask = UINT8_MAX;
        if (byte == firstByte && range.bitOffset % 8 != 0)
          mask &= static_cast<uint8_t>(UINT8_MAX << (range.bitOffset % 8));
        if (byte + 1 == lastByte && lastBit % 8 != 0)
          mask &= static_cast<uint8_t>((uint16_t{1} << (lastBit % 8)) - 1);
        scanByteMasks[byte] |= mask;
      }
    }
    SmallVector<std::pair<uint64_t, uint8_t>> orderedScanBytes(
        scanByteMasks.begin(), scanByteMasks.end());
    llvm::sort(orderedScanBytes, [](const auto &lhs, const auto &rhs) {
      return lhs.first < rhs.first;
    });
    Block *readyUnknown = new Block;
    ready.getBody().push_back(readyUnknown);
    for (auto [byte, mask] : orderedScanBytes) {
      Value bits =
          LLVM::LoadOp::create(builder, location, builder.getI8Type(),
                               byteGEP(builder, location, unknown, byte), 1);
      if (mask != UINT8_MAX)
        bits = arith::AndIOp::create(
            builder, location, bits,
            llvmConstant(builder, location, builder.getI8Type(), mask));
      Value byteUnknown = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, bits,
          llvmConstant(builder, location, builder.getI8Type(), 0));
      Block *nextByte = new Block;
      ready.getBody().push_back(nextByte);
      cf::CondBranchOp::create(builder, location, byteUnknown, readyUnknown,
                               ValueRange{}, nextByte, ValueRange{});
      builder.setInsertionPointToStart(nextByte);
    }
    LLVM::StoreOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI8Type(), 1), latchAddress,
        1);
    Value pendingAddress = ownerWordAddress(
        builder, location,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  promotionPendingMask.getSymName()),
        mergedFragments[index].bit / 64);
    Value pending =
        LLVM::LoadOp::create(builder, location, i64, pendingAddress, 8);
    Value clearedPending = arith::AndIOp::create(
        builder, location, pending,
        llvmConstant(builder, location, i64,
                     ~(uint64_t{1} << (mergedFragments[index].bit % 64))));
    LLVM::StoreOp::create(builder, location, clearedPending, pendingAddress, 8);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI1Type(), 1));
    builder.setInsertionPointToStart(readyUnknown);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI1Type(), 0));
  }

  // Path-dependent entry bodies keep activation-local probes. They do not
  // participate in a model-wide controller promotion certificate.
  for (auto [index, name] : llvm::enumerate(mergedTwoStateExecutors))
    if (auto function = executorSymbols.lookup<LLVM::LLVMFuncOp>(name))
      if (function->hasAttr(sim::metadata::evalPathGuardedTwoState))
        pathGuardedOwnerMask.setBit(mergedFragments[index].bit);

  builder.setInsertionPointToEnd(module.getBody());
  auto promotionReady = LLVM::LLVMFuncOp::create(
      builder, location, promotionReadyName,
      LLVM::LLVMFunctionType::get(builder.getI1Type(), {}, false));
  promotionReady->setAttr(
      "passthrough",
      builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
  Block *entry = promotionReady.addEntryBlock(builder);
  Block *scan = new Block;
  Block *alreadyKnown = new Block;
  promotionReady.getBody().push_back(scan);
  promotionReady.getBody().push_back(alreadyKnown);
  builder.setInsertionPointToStart(entry);
  Value latchedAddress = LLVM::AddressOfOp::create(
      builder, location, pointer, promotionLatched.getSymName());
  Value latched = LLVM::LoadOp::create(builder, location, builder.getI8Type(),
                                       latchedAddress, 1);
  Value isLatched = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ne, latched,
      llvmConstant(builder, location, builder.getI8Type(), 0));
  cf::CondBranchOp::create(builder, location, isLatched, alreadyKnown,
                           ValueRange{}, scan, ValueRange{});

  builder.setInsertionPointToStart(alreadyKnown);
  LLVM::ReturnOp::create(
      builder, location,
      llvmConstant(builder, location, builder.getI1Type(), 1));

  builder.setInsertionPointToStart(scan);
  Value pending = maskedOwnerWords(
      builder, location,
      LLVM::AddressOfOp::create(builder, location, pointer,
                                promotionPendingMask.getSymName()),
      allOwners);
  Value known =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                            pending, llvmConstant(builder, location, i64, 0));
  LLVM::StoreOp::create(
      builder, location,
      arith::SelectOp::create(
          builder, location, known,
          llvmConstant(builder, location, builder.getI8Type(), 1), latched),
      latchedAddress, 1);
  LLVM::ReturnOp::create(builder, location, known);

  // Runtime intervention invalidates the selected two-state closure without
  // scanning it. The next quiescent generated dispatch performs the single
  // masked unknown-plane scan and selects the appropriate variant again.
  builder.setInsertionPointToEnd(module.getBody());
  auto promotionInvalidate = LLVM::LLVMFuncOp::create(
      builder, location, promotionInvalidateName,
      LLVM::LLVMFunctionType::get(LLVM::LLVMVoidType::get(context), {}, false));
  promotionInvalidate->setAttr(
      "passthrough", builder.getArrayAttr({builder.getStringAttr("noinline"),
                                           builder.getStringAttr("cold")}));
  Block *invalidateEntry = promotionInvalidate.addEntryBlock(builder);
  builder.setInsertionPointToStart(invalidateEntry);
  LLVM::StoreOp::create(
      builder, location,
      llvmConstant(builder, location, builder.getI8Type(), 0),
      LLVM::AddressOfOp::create(builder, location, pointer,
                                promotionLatched.getSymName()),
      1);
  LLVM::MemsetOp::create(
      builder, location,
      LLVM::AddressOfOp::create(builder, location, pointer,
                                promotionKernelLatched.getSymName()),
      llvmConstant(builder, location, builder.getI8Type(), 0),
      llvmConstant(builder, location, i64, mergedFragments.size()),
      /*isVolatile=*/false);
  storeOwnerMask(builder, location,
                 LLVM::AddressOfOp::create(builder, location, pointer,
                                           promotionPendingMask.getSymName()),
                 initialPromotionPendingMask);
  if (nbaTaintWordCount != 0)
    LLVM::MemsetOp::create(
        builder, location,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  evalFastNBARootsName),
        llvmConstant(builder, location, builder.getI8Type(), 0),
        llvmConstant(builder, location, i64,
                     uint64_t{nbaTaintWordCount} * sizeof(uint64_t)),
        /*isVolatile=*/false);
  LLVM::ReturnOp::create(builder, location, ValueRange{});

  // The runtime calls this only while its transient fine scheduler is
  // quiescent. Keep the internal i1 promotion predicate convenient for the
  // generated coordinator and expose an ABI-sized result in the plan.
  builder.setInsertionPointToEnd(module.getBody());
  auto promotionQuery =
      LLVM::LLVMFuncOp::create(builder, location, promotionQueryName,
                               LLVM::LLVMFunctionType::get(i32, {}, false));
  Block *queryEntry = promotionQuery.addEntryBlock(builder);
  builder.setInsertionPointToStart(queryEntry);
  Value queryReady =
      LLVM::CallOp::create(builder, location, TypeRange{builder.getI1Type()},
                           SymbolRefAttr::get(context, promotionReadyName),
                           ValueRange{})
          .getResult();
  LLVM::ReturnOp::create(
      builder, location,
      arith::ExtUIOp::create(builder, location, i32, queryReady));

  Type nodeType = LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32});
  Type nodesType = LLVM::LLVMArrayType::get(nodeType, executableNodes.size());
  makeConstantGlobal(
      module, location, nodesType, nodesName, LLVM::Linkage::Internal, 4,
      [&](OpBuilder &initializerBuilder) {
        Value nodes =
            LLVM::ZeroOp::create(initializerBuilder, location, nodesType);
        for (auto [index, node] : llvm::enumerate(executableNodes)) {
          Value value =
              LLVM::ZeroOp::create(initializerBuilder, location, nodeType);
          value = insertValue(
              initializerBuilder, location, value,
              llvmConstant(initializerBuilder, location, i32, node.actor_slot),
              0);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.continuation),
                              1);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.fusion_group),
                              2);
          nodes = LLVM::InsertValueOp::create(
              initializerBuilder, location, nodes, value,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return nodes;
      });

  Type nbaRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, pointer});
  if (!nbaRoots.empty()) {
    Type rootsType = LLVM::LLVMArrayType::get(nbaRootType, nbaRoots.size());
    makeConstantGlobal(
        module, location, rootsType, nbaRootsName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value roots =
              LLVM::ZeroOp::create(initializerBuilder, location, rootsType);
          for (auto [index, root] : llvm::enumerate(nbaRoots)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.commit_node),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, root.bit_width),
                2);
            Value accumulator =
                index < staticNBAPlan.generatedAccumulators.size() &&
                        !staticNBAPlan.generatedAccumulators[index].empty()
                    ? LLVM::AddressOfOp::create(
                          initializerBuilder, location, pointer,
                          staticNBAPlan.generatedAccumulators[index])
                          .getResult()
                    : LLVM::ZeroOp::create(initializerBuilder, location,
                                           pointer)
                          .getResult();
            value = insertValue(initializerBuilder, location, value,
                                accumulator, 3);
            roots = LLVM::InsertValueOp::create(
                initializerBuilder, location, roots, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return roots;
        });
  }
  uint32_t nbaDirtyWordCount =
      static_cast<uint32_t>((nbaRoots.size() + 63) / 64);
  uint32_t nbaDirtySummaryWordCount = (nbaDirtyWordCount + 63) / 64;
  if (nbaDirtyWordCount != 0) {
    Type dirtyType = LLVM::LLVMArrayType::get(i64, nbaDirtyWordCount);
    builder.setInsertionPointToStart(module.getBody());
    auto dirty = LLVM::GlobalOp::create(builder, location, dirtyType, false,
                                        LLVM::Linkage::Internal,
                                        nbaDirtyRootsName, Attribute{}, 8);
    Block *dirtyInitializer = new Block;
    dirty.getInitializerRegion().push_back(dirtyInitializer);
    builder.setInsertionPointToStart(dirtyInitializer);
    LLVM::ReturnOp::create(builder, location,
                           LLVM::ZeroOp::create(builder, location, dirtyType));
  }
  if (nbaDirtySummaryWordCount != 0) {
    Type summaryType = LLVM::LLVMArrayType::get(i64, nbaDirtySummaryWordCount);
    builder.setInsertionPointToStart(module.getBody());
    auto summary = LLVM::GlobalOp::create(builder, location, summaryType, false,
                                          LLVM::Linkage::Internal,
                                          nbaDirtySummaryName, Attribute{}, 8);
    Block *summaryInitializer = new Block;
    summary.getInitializerRegion().push_back(summaryInitializer);
    builder.setInsertionPointToStart(summaryInitializer);
    LLVM::ReturnOp::create(
        builder, location,
        LLVM::ZeroOp::create(builder, location, summaryType));
  }
  Type nbaSiteType = LLVM::LLVMStructType::getLiteral(context, {i64, i32, i32});
  if (!nbaSites.empty()) {
    Type sitesType = LLVM::LLVMArrayType::get(nbaSiteType, nbaSites.size());
    makeConstantGlobal(
        module, location, sitesType, nbaSitesName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value sites =
              LLVM::ZeroOp::create(initializerBuilder, location, sitesType);
          for (auto [index, site] : llvm::enumerate(nbaSites)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaSiteType);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, site.site), 0);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.root), 1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.storage),
                2);
            sites = LLVM::InsertValueOp::create(
                initializerBuilder, location, sites, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return sites;
        });
  }
  Type fanoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i32, i32, i64, i64, i32, i32});
  if (!fanoutEntries.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(fanoutType, fanoutEntries.size());
    makeConstantGlobal(
        module, location, entriesType, fanoutName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(fanoutEntries)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, fanoutType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                1);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.continuation),
                                2);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.edge), 3);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.compute_node),
                                4);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.reserved),
                5);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, entry.low_bit),
                6);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             entry.bit_width),
                                7);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.kernel),
                8);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.merged_bit),
                                9);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }
  auto ingressWordCount = [&](const NativeEvalClockKernel &kernel) {
    (void)kernel;
    size_t bits = mergedFragments.size();
    return static_cast<uint32_t>((bits + 63) / 64);
  };
  builder.setInsertionPointToStart(module.getBody());
  llvm::SmallDenseSet<StringRef, 4> emittedIngress;
  for (const NativeEvalClockKernel &kernel : clockKernels) {
    uint32_t words = ingressWordCount(kernel);
    Type ingressType = LLVM::LLVMArrayType::get(i64, readyLayout.storageWords);
    if (emittedIngress.insert(kernel.ingressName).second) {
      auto ingress = LLVM::GlobalOp::create(builder, location, ingressType,
                                            false, LLVM::Linkage::Internal,
                                            kernel.ingressName, Attribute{}, 8);
      Block *initializer = new Block;
      ingress.getInitializerRegion().push_back(initializer);
      OpBuilder initializerBuilder = OpBuilder::atBlockBegin(initializer);
      LLVM::ReturnOp::create(
          initializerBuilder, location,
          LLVM::ZeroOp::create(initializerBuilder, location, ingressType));
    }
    Type activeType = LLVM::LLVMArrayType::get(i64, words);
    auto active = LLVM::GlobalOp::create(builder, location, activeType, false,
                                         LLVM::Linkage::Internal,
                                         kernel.activeName, Attribute{}, 8);
    Block *activeInitializer = new Block;
    active.getInitializerRegion().push_back(activeInitializer);
    OpBuilder activeBuilder = OpBuilder::atBlockBegin(activeInitializer);
    LLVM::ReturnOp::create(
        activeBuilder, location,
        LLVM::ZeroOp::create(activeBuilder, location, activeType));
  }

  // The eval scheduler is a closed generated call graph. Replace scalar
  // transition publication in private eval bodies with direct ingress-bit
  // updates. No actor lookup, continuation validation, subscription scan, or
  // runtime callback remains on this path; records without a generated body
  // are intentionally outside this generated boundary.
  // Fixed generated state transitions are the only way a closed Tier-1 body
  // can reactivate a sensitivity owner before the NBA barrier.  Retain their
  // ranges after replacing the runtime calls so later one-entry NBA staging
  // can prove that its periodic ingress is not written by another eval body.
  if (!clockKernels.empty()) {
    // Resolve the generated body that executes under each compact ready bit.
    // Fusion helpers inherit the identity of their instance coordinator: the
    // coordinator executes the complete group before its ready bit is
    // consumed, so intra-group publications are already represented by its
    // statically ordered body.
    llvm::StringMap<unsigned> directByWrapper;
    llvm::DenseMap<uint32_t, SmallVector<unsigned>> fusionMembers;
    for (auto [index, direct] : llvm::enumerate(directFragments)) {
      directByWrapper.try_emplace(direct.wrapper, index);
      if (direct.fusionGroup != UINT32_MAX)
        fusionMembers[direct.fusionGroup].push_back(index);
    }
    llvm::StringMap<APInt> activeOwnerBits;
    DenseMap<std::pair<uint64_t, uint32_t>, std::pair<uint32_t, uint32_t>>
        physicalSourceOwners;
    DenseMap<uint64_t, std::pair<uint32_t, uint32_t>> uniqueCodeUnitOwners;
    DenseSet<uint64_t> ambiguousCodeUnitOwners;
    auto mapActiveBody = [&](StringRef name, uint32_t bit) -> LogicalResult {
      if (name.empty())
        return success();
      activeOwnerBits.try_emplace(name, ownerCount, 0)
          .first->second.setBit(bit);
      return success();
    };
    for (auto [recordIndex, executor] : llvm::enumerate(mergedExecutors)) {
      auto selected = directByWrapper.find(executor);
      if (selected == directByWrapper.end())
        continue;
      const NativeDirectFragment &direct = directFragments[selected->second];
      SmallVector<unsigned, 1> members{selected->second};
      if (direct.instanceCoordinator && direct.fusionGroup != UINT32_MAX)
        members = fusionMembers.lookup(direct.fusionGroup);
      for (unsigned member : members) {
        if (failed(mapActiveBody(directFragments[member].body,
                                 mergedFragments[recordIndex].bit)) ||
            failed(mapActiveBody(directFragments[member].twoStateBody,
                                 mergedFragments[recordIndex].bit)))
          return failure();
      }
    }
    for (const NativeDirectFragment &direct : directFragments) {
      if (direct.instanceCoordinator)
        continue;
      std::pair<uint32_t, uint32_t> physical{direct.actorSlot,
                                             direct.continuation};
      for (uint64_t codeUnit : direct.sourceCodeUnits) {
        physicalSourceOwners.try_emplace(
            std::pair{codeUnit, direct.continuation}, physical);
        auto [found, inserted] =
            uniqueCodeUnitOwners.try_emplace(codeUnit, physical);
        if (!inserted && found->second != physical)
          ambiguousCodeUnitOwners.insert(codeUnit);
      }
    }
    struct GeneratedTransition {
      LLVM::CallOp call;
      bool periodicTwoState = false;
      APInt activeOwnerMask;
      std::optional<std::pair<uint32_t, uint32_t>> physicalSourceOwner;
    };
    SmallVector<GeneratedTransition> transitions;
    for (sim::SimFuncOp function :
         collectGeneratedEvalCallClosure(module, selectedRawBodies)) {
      bool periodicTwoState =
          function->hasAttr("obelisk.eval.selected_two_state");
      auto activeOwner = activeOwnerBits.find(function.getSymName());
      APInt activeOwnerMask = activeOwner == activeOwnerBits.end()
                                  ? APInt(ownerCount, 0)
                                  : activeOwner->second;
      function.walk([&](LLVM::CallOp call) {
        if (!call.getCallee() ||
            *call.getCallee() != "obelisk_rt_v1_scheduler_static_transition")
          return;
        std::optional<std::pair<uint32_t, uint32_t>> physicalSourceOwner;
        if (auto owner = call->getAttrOfType<DictionaryAttr>(
                sim::metadata::evalSourceOwner)) {
          auto codeUnit = owner.getAs<IntegerAttr>("code_unit");
          auto continuation = owner.getAs<IntegerAttr>("continuation");
          if (codeUnit && continuation && continuation.getInt() > 0 &&
              static_cast<uint64_t>(continuation.getInt()) <= UINT32_MAX) {
            auto exact = physicalSourceOwners.find(
                {codeUnit.getUInt(),
                 static_cast<uint32_t>(continuation.getInt())});
            if (exact != physicalSourceOwners.end())
              physicalSourceOwner = exact->second;
            else if (!ambiguousCodeUnitOwners.contains(codeUnit.getUInt()))
              if (auto unique = uniqueCodeUnitOwners.find(codeUnit.getUInt());
                  unique != uniqueCodeUnitOwners.end())
                physicalSourceOwner = unique->second;
          }
        }
        transitions.push_back(
            {call, periodicTwoState, activeOwnerMask, physicalSourceOwner});
      });
    }
    auto packedMask = [](uint64_t width) {
      return width >= 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
    };
    for (const GeneratedTransition &transition : transitions) {
      LLVM::CallOp call = transition.call;
      bool periodicTwoState = transition.periodicTwoState;
      const APInt &activeOwnerMask = transition.activeOwnerMask;
      std::optional<std::pair<uint32_t, uint32_t>> physicalSourceOwner =
          transition.physicalSourceOwner;
      ValueRange arguments = call.getArgOperands();
      if (arguments.size() != 8)
        return call.emitError("malformed static transition ABI"), failure();
      std::optional<uint64_t> staticState = constantU64(arguments[1]);
      std::optional<uint64_t> lowBit = constantU64(arguments[2]);
      std::optional<uint64_t> bitWidth = constantU64(arguments[3]);
      if (!staticState || !lowBit || !bitWidth || *bitWidth == 0 ||
          *bitWidth > 64)
        return call.emitError("eval transition is not a fixed scalar range"),
               failure();
      bool hasRuntimeOwnedObserver =
          staticFanoutPlan.runtimeTransitionStates.contains(*staticState);
      OpBuilder transitionBuilder(call);
      Value oldValue = arguments[4];
      Value oldUnknown = arguments[5];
      Value newValue = arguments[6];
      Value newUnknown = arguments[7];
      Value changed = arith::OrIOp::create(
          transitionBuilder, call.getLoc(),
          arith::XOrIOp::create(transitionBuilder, call.getLoc(), oldValue,
                                newValue),
          arith::XOrIOp::create(transitionBuilder, call.getLoc(), oldUnknown,
                                newUnknown));
      struct MergedPublication {
        uint32_t bit;
        uint64_t changeMask = 0;
        uint64_t posedgeMask = 0;
        uint64_t negedgeMask = 0;
      };
      SmallVector<MergedPublication, 4> publications;
      uint64_t rangeEnd = *lowBit + *bitWidth;
      // A source event control is inactive while its controlled statement is
      // executing. Suppress only the publication back to the current compact
      // owner. Other owners watching the same range must still be activated.
      // The generated coordinator consumes this owner bit on the appropriate
      // side of execution, matching the source logical-process identity
      // without adding a runtime token lookup to the hot path.
      bool needsActiveSelfCheck = false;
      for (const obelisk_rt_static_fanout_entry &entry : fanoutEntries) {
        if (entry.static_state != *staticState)
          continue;
        uint64_t overlapLow = std::max(*lowBit, entry.low_bit);
        uint64_t overlapHigh =
            std::min(rangeEnd, entry.low_bit + entry.bit_width);
        if (overlapLow >= overlapHigh)
          continue;
        if (periodicTwoState && !periodicClosureRecords.empty() &&
            !llvm::is_contained(periodicClosureRecords,
                                static_cast<unsigned>(entry.merged_bit)))
          continue;
        if (fanoutRoute(entry) != OBELISK_RT_FANOUT_DIRECT ||
            entry.kernel >= clockKernels.size())
          continue;
        if ((entry.reserved & OBELISK_RT_FANOUT_SUPPRESS_ACTIVE_SELF) != 0) {
          if (physicalSourceOwner) {
            if (*physicalSourceOwner ==
                std::pair{entry.actor_slot, entry.continuation})
              continue;
          } else if (activeOwnerMask == 0) {
            needsActiveSelfCheck = true;
            continue;
          } else if (activeOwnerMask[entry.merged_bit]) {
            continue;
          }
        }
        uint64_t overlapMask = packedMask(overlapHigh - overlapLow)
                               << (overlapLow - *lowBit);
        auto publication = llvm::find_if(
            publications, [&](const MergedPublication &candidate) {
              return candidate.bit == entry.merged_bit;
            });
        if (publication == publications.end()) {
          publications.push_back({entry.merged_bit});
          publication = std::prev(publications.end());
        }
        if (entry.edge == OBELISK_RT_WAIT_EDGE_POSEDGE)
          publication->posedgeMask |= overlapMask;
        else if (entry.edge == OBELISK_RT_WAIT_EDGE_NEGEDGE)
          publication->negedgeMask |= overlapMask;
        else if (entry.edge == OBELISK_RT_WAIT_EDGE_BOTH) {
          publication->posedgeMask |= overlapMask;
          publication->negedgeMask |= overlapMask;
        } else {
          publication->changeMask |= overlapMask;
        }
      }
      if (needsActiveSelfCheck)
        continue;
      if (publications.empty()) {
        if (!hasRuntimeOwnedObserver)
          call.erase();
        continue;
      }
      Value widthMask = llvmConstant(transitionBuilder, call.getLoc(), i64,
                                     packedMask(*bitWidth));
      auto invert = [&](Value value) {
        return arith::XOrIOp::create(transitionBuilder, call.getLoc(), value,
                                     llvmConstant(transitionBuilder,
                                                  call.getLoc(), i64,
                                                  UINT64_MAX))
            .getResult();
      };
      bool needsPosedge = llvm::any_of(publications, [](const auto &entry) {
        return entry.posedgeMask != 0;
      });
      bool needsNegedge = llvm::any_of(publications, [](const auto &entry) {
        return entry.negedgeMask != 0;
      });
      Value posedge;
      Value negedge;
      if (needsPosedge || needsNegedge) {
        Value oldKnown = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                               invert(oldUnknown), widthMask);
        Value newKnown = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                               invert(newUnknown), widthMask);
        Value oldZero = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                              oldKnown, invert(oldValue));
        Value oldOne = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                             oldKnown, oldValue);
        Value newZero = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                              newKnown, invert(newValue));
        Value newOne = arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                             newKnown, newValue);
        if (needsPosedge)
          posedge = arith::AndIOp::create(
              transitionBuilder, call.getLoc(),
              arith::OrIOp::create(
                  transitionBuilder, call.getLoc(),
                  arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                        oldZero, invert(newZero)),
                  arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                        oldUnknown, newOne)),
              widthMask);
        if (needsNegedge)
          negedge = arith::AndIOp::create(
              transitionBuilder, call.getLoc(),
              arith::OrIOp::create(
                  transitionBuilder, call.getLoc(),
                  arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                        oldOne, invert(newOne)),
                  arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                        oldUnknown, newZero)),
              widthMask);
      }
      for (const MergedPublication &publication : publications) {
        Value observed = llvmConstant(transitionBuilder, call.getLoc(), i64, 0);
        auto addObserved = [&](Value edges, uint64_t mask) {
          if (mask == 0)
            return;
          Value masked = arith::AndIOp::create(
              transitionBuilder, call.getLoc(), edges,
              llvmConstant(transitionBuilder, call.getLoc(), i64, mask));
          observed = arith::OrIOp::create(transitionBuilder, call.getLoc(),
                                          observed, masked);
        };
        addObserved(changed, publication.changeMask);
        addObserved(posedge, publication.posedgeMask);
        addObserved(negedge, publication.negedgeMask);
        Value triggered = arith::CmpIOp::create(
            transitionBuilder, call.getLoc(), arith::CmpIPredicate::ne,
            observed, llvmConstant(transitionBuilder, call.getLoc(), i64, 0));
        Value ingress =
            LLVM::AddressOfOp::create(transitionBuilder, call.getLoc(), pointer,
                                      clockKernels.front().ingressName);
        Value selected = arith::SelectOp::create(
            transitionBuilder, call.getLoc(), triggered,
            llvmConstant(transitionBuilder, call.getLoc(), i64,
                         uint64_t{1} << (publication.bit % 64)),
            llvmConstant(transitionBuilder, call.getLoc(), i64, 0));
        updateEvalReadyWord(transitionBuilder, call.getLoc(), ingress,
                            readyLayout, publication.bit / 64, selected);
      }
      if (!hasRuntimeOwnedObserver)
        call.erase();
    }

    // Dynamic writes into a fixed packed root use a generated one-entry NBA
    // latch. This is the common register-file shape: the clock body records
    // offset/value locally and the generated NBA epilogue publishes it after
    // the activation returns, without constructing a runtime NBA object.
    SmallVector<std::pair<LLVM::CallOp, bool>> runtimeEscapes;
    module.walk([&](sim::SimFuncOp function) {
      if (!isGeneratedEvalBody(function, selectedRawBodies))
        return;
      bool twoState = function->hasAttr("obelisk.eval.selected_two_state");
      function.walk([&](LLVM::CallOp call) {
        if (!call.getCallee())
          return;
        bool staticNBA =
            *call.getCallee() == "obelisk_rt_v1_scheduler_static_nba";
        bool staticNBAFailure = false;
        if (*call.getCallee() == "obelisk_rt_v1_scheduler_fail" &&
            call.getArgOperands().size() == 2)
          if (auto source =
                  call.getArgOperands()[1].getDefiningOp<LLVM::CallOp>())
            staticNBAFailure = source.getCallee() &&
                               *source.getCallee() ==
                                   "obelisk_rt_v1_scheduler_static_nba";
        // A generic NBA call retained for an unsafe root still needs its
        // scheduler_fail companion. Only remove the companion for a static
        // call that this pass replaces with a generated stage.
        if (staticNBA || staticNBAFailure)
          runtimeEscapes.push_back({call, twoState});
      });
    });
    for (auto [call, twoState] : runtimeEscapes) {
      LLVM::CallOp handleOffsetToErase;
      Operation *handleSelectToErase = nullptr;
      if (call.getCallee() &&
          *call.getCallee() == "obelisk_rt_v1_scheduler_static_nba") {
        ValueRange arguments = call.getArgOperands();
        if (arguments.size() != 9)
          return call.emitError("malformed static NBA ABI"), failure();
        std::optional<uint64_t> site = constantU64(arguments[1]);
        std::optional<uint64_t> width = constantU64(arguments[6]);
        auto root = site ? staticNBAPlan.siteRoots.find(*site)
                         : staticNBAPlan.siteRoots.end();
        auto offsetCall = arguments[5].getDefiningOp<LLVM::CallOp>();
        Value dynamicValid;
        bool invertDynamicValid = false;
        if (!offsetCall) {
          Operation *selected = arguments[5].getDefiningOp();
          if (isa_and_nonnull<arith::SelectOp, LLVM::SelectOp>(selected)) {
            handleSelectToErase = selected;
            std::optional<uint64_t> trueConstant =
                constantU64(selected->getOperand(1));
            std::optional<uint64_t> falseConstant =
                constantU64(selected->getOperand(2));
            if (falseConstant && *falseConstant == UINT64_MAX) {
              dynamicValid = selected->getOperand(0);
              offsetCall =
                  selected->getOperand(1).getDefiningOp<LLVM::CallOp>();
            } else if (trueConstant && *trueConstant == UINT64_MAX) {
              dynamicValid = selected->getOperand(0);
              invertDynamicValid = true;
              offsetCall =
                  selected->getOperand(2).getDefiningOp<LLVM::CallOp>();
            }
          }
        }
        auto proof = dynamicNBAProofs.find(call.getOperation());
        if (proof == dynamicNBAProofs.end() || !proof->second.eligible) {
          const DynamicEvalNBAProof emptyProof;
          const DynamicEvalNBAProof &details =
              proof == dynamicNBAProofs.end() ? emptyProof : proof->second;
          auto diagnostic =
              call.emitError("runtime-free eval cannot lower dynamic NBA site");
          diagnostic
              << " (site=" << static_cast<bool>(site)
              << ", width=" << (width ? *width : 0) << ", root="
              << (root != staticNBAPlan.siteRoots.end()
                      ? static_cast<uint64_t>(root->second)
                      : UINT64_MAX)
              << ", accumulator="
              << (root != staticNBAPlan.siteRoots.end() &&
                  root->second < staticNBAPlan.generatedAccumulators.size() &&
                  !staticNBAPlan.generatedAccumulators[root->second].empty())
              << ", offset-call=" << static_cast<bool>(offsetCall)
              << ", offset-arity="
              << (offsetCall ? offsetCall.getArgOperands().size() : 0)
              << ", root-width="
              << (root != staticNBAPlan.siteRoots.end() &&
                          root->second < staticNBAPlan.roots.size()
                      ? staticNBAPlan.roots[root->second].bit_width
                      : 0)
              << ", commit-region=" << details.commitRegion
              << ", periodic-entry="
              << static_cast<bool>(details.periodicRecord)
              << ", exclusive-periodic-ingress="
              << details.exclusivePeriodicIngress
              << ", periodic-ingress-count=" << details.periodicIngressCount
              << ", non-periodic-ingress-count="
              << details.nonPeriodicIngressCount
              << ", periodic-ingress-transition-conflicts="
              << details.periodicIngressTransitionConflicts
              << ", unique-semantic-root-site="
              << details.uniqueSemanticRootSite
              << ", root-site-count=" << details.rootSiteCount
              << ", semantic-root-site-count=" << details.semanticRootSiteCount
              << ", first-root-site=" << details.firstRootSite
              << ", last-root-site=" << details.lastRootSite
              << ", site-once=" << details.siteExecutesAtMostOnce << ")";
          return failure();
        }
        bool orderedWide = orderedNBARoots.contains(root->second);
        bool periodicWideLatch = proof->second.periodicWideLatch;
        handleOffsetToErase = offsetCall;
        OpBuilder nbaBuilder(call);
        Value dynamicBit =
            offsetCall ? offsetCall.getArgOperands()[1]
                       : llvmConstant(nbaBuilder, call.getLoc(), i64, 0);
        IntegerType valueType = IntegerType::get(context, *width);
        Value staged = LLVM::LoadOp::create(nbaBuilder, call.getLoc(),
                                            valueType, arguments[7], 1);
        Value staged64 =
            *width == 64
                ? staged
                : LLVM::ZExtOp::create(nbaBuilder, call.getLoc(), i64, staged)
                      .getResult();
        Value stagedUnknown64 = llvmConstant(nbaBuilder, call.getLoc(), i64, 0);
        // A locally promoted executor can still contain four-state literals
        // or values unrelated to its promoted inputs. The NBA ABI, not the
        // enclosing function's domain, determines whether this value carries
        // an unknown plane. ImmediateNBAConversion supplies null only for a
        // genuinely two-state value.
        if (!arguments[8].getDefiningOp<LLVM::ZeroOp>()) {
          Value stagedUnknown = LLVM::LoadOp::create(
              nbaBuilder, call.getLoc(), valueType, arguments[8], 1);
          stagedUnknown64 =
              *width == 64 ? stagedUnknown
                           : LLVM::ZExtOp::create(nbaBuilder, call.getLoc(),
                                                  i64, stagedUnknown)
                                 .getResult();
        }

        // Keep the source value and destination offset from this execution.
        // The ordered wide path records every activation; a one-entry latch
        // would lose intermediate updates from overlapping or repeated sites,
        // contrary to IEEE 1800-2017 4.6 and 10.4.2.
        uint64_t rootWidth = staticNBAPlan.roots[root->second].bit_width;
        Value encodedBase =
            offsetCall ? offsetCall.getArgOperands()[0] : arguments[5];
        uint64_t expectedPrefix =
            OBELISK_RT_STABLE_HANDLE_STATIC_TAG |
            (uint64_t{staticNBAPlan.roots[root->second].static_state} << 32);
        Value basePrefix =
            arith::AndIOp::create(nbaBuilder, call.getLoc(), encodedBase,
                                  llvmConstant(nbaBuilder, call.getLoc(), i64,
                                               UINT64_C(0xffffffff00000000)));
        Value baseValid = arith::CmpIOp::create(
            nbaBuilder, call.getLoc(), arith::CmpIPredicate::eq, basePrefix,
            llvmConstant(nbaBuilder, call.getLoc(), i64, expectedPrefix));
        Value baseOffset = LLVM::SExtOp::create(
            nbaBuilder, call.getLoc(), i64,
            LLVM::TruncOp::create(nbaBuilder, call.getLoc(), i32, encodedBase));
        // Compare the dynamic amount against base-adjusted bounds instead of
        // first adding it to the signed 32-bit base.  Thus even an extreme
        // representable source index cannot overflow before being rejected.
        Value lower =
            arith::SubIOp::create(nbaBuilder, call.getLoc(),
                                  llvmConstant(nbaBuilder, call.getLoc(), i64,
                                               -static_cast<int64_t>(*width)),
                                  baseOffset);
        Value upper = arith::SubIOp::create(
            nbaBuilder, call.getLoc(),
            llvmConstant(nbaBuilder, call.getLoc(), i64, rootWidth),
            baseOffset);
        Value overlaps = arith::AndIOp::create(
            nbaBuilder, call.getLoc(),
            arith::CmpIOp::create(nbaBuilder, call.getLoc(),
                                  arith::CmpIPredicate::sgt, dynamicBit, lower),
            arith::CmpIOp::create(nbaBuilder, call.getLoc(),
                                  arith::CmpIPredicate::slt, dynamicBit,
                                  upper));
        overlaps = arith::AndIOp::create(nbaBuilder, call.getLoc(), baseValid,
                                         overlaps);
        if (dynamicValid) {
          if (invertDynamicValid)
            dynamicValid =
                arith::XOrIOp::create(nbaBuilder, call.getLoc(), dynamicValid,
                                      llvmConstant(nbaBuilder, call.getLoc(),
                                                   nbaBuilder.getI1Type(), 1));
          overlaps = arith::AndIOp::create(nbaBuilder, call.getLoc(),
                                           dynamicValid, overlaps);
        }
        Value zero64 = llvmConstant(nbaBuilder, call.getLoc(), i64, 0);
        Value safeDynamic = arith::SelectOp::create(
            nbaBuilder, call.getLoc(), overlaps, dynamicBit,
            arith::SubIOp::create(nbaBuilder, call.getLoc(), zero64,
                                  baseOffset));
        Value requestedStart = arith::AddIOp::create(nbaBuilder, call.getLoc(),
                                                     safeDynamic, baseOffset);
        if (orderedWide) {
          auto existing =
              llvm::find_if(dynamicEvalNBAs, [&](const DynamicEvalNBA &entry) {
                return entry.site == *site;
              });
          if (existing == dynamicEvalNBAs.end()) {
            DynamicEvalNBA entry{root->second, *site, *width};
            entry.queued = true;
            entry.offsetName = "offset";
            entry.valueName = "value";
            entry.unknownName = "unknown";
            dynamicEvalNBAs.push_back(std::move(entry));
          }
          // Preserve each activation, including repeated writes to one lane.
          // The hot path is only direct record stores. Storage growth is cold
          // and cannot execute actors or re-enter the scheduler.
          Block *before = call->getBlock();
          Block *continuation = before->splitBlock(call->getIterator());
          Region *region = before->getParent();
          auto block = [&]() {
            auto *result = new Block;
            region->getBlocks().insert(Region::iterator(continuation), result);
            return result;
          };
          Block *check = block(), *grow = block(), *stage = block(),
                *failed = block();
          nbaBuilder.setInsertionPointToEnd(before);
          cf::CondBranchOp::create(nbaBuilder, call.getLoc(), overlaps, check,
                                   ValueRange{}, continuation, ValueRange{});
          nbaBuilder.setInsertionPointToStart(check);
          Value size = evalNBAQueueSize(nbaBuilder, call.getLoc());
          Value capacity = LLVM::LoadOp::create(
              nbaBuilder, call.getLoc(), i32,
              evalNBAQueueField(nbaBuilder, call.getLoc(), 2), 4);
          Value full =
              arith::CmpIOp::create(nbaBuilder, call.getLoc(),
                                    arith::CmpIPredicate::eq, size, capacity);
          cf::CondBranchOp::create(nbaBuilder, call.getLoc(), full, grow,
                                   ValueRange{}, stage, ValueRange{});
          nbaBuilder.setInsertionPointToStart(grow);
          Value status =
              LLVM::CallOp::create(
                  nbaBuilder, call.getLoc(), TypeRange{i32},
                  SymbolRefAttr::get(context, "obelisk_rt_v1_eval_nba_reserve"),
                  ValueRange{arguments[0], LLVM::AddressOfOp::create(
                                               nbaBuilder, call.getLoc(),
                                               pointer, evalNBAQueueName)})
                  .getResult();
          Value ok = arith::CmpIOp::create(
              nbaBuilder, call.getLoc(), arith::CmpIPredicate::eq, status,
              llvmConstant(nbaBuilder, call.getLoc(), i32, OBELISK_RT_OK));
          cf::CondBranchOp::create(nbaBuilder, call.getLoc(), ok, stage,
                                   ValueRange{}, failed, ValueRange{});
          nbaBuilder.setInsertionPointToStart(failed);
          LLVM::StoreOp::create(nbaBuilder, call.getLoc(), status,
                                evalNBAQueueField(nbaBuilder, call.getLoc(), 3),
                                4);
          // The queue descriptor is only a cleanup owner. Report allocation
          // failure through the scheduler so a truncated activation cannot
          // appear to complete successfully.
          LLVM::CallOp::create(
              nbaBuilder, call.getLoc(), TypeRange{},
              SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_fail"),
              ValueRange{arguments[0], status});
          cf::BranchOp::create(nbaBuilder, call.getLoc(), continuation);
          nbaBuilder.setInsertionPointToStart(stage);
          Value data = LLVM::LoadOp::create(
              nbaBuilder, call.getLoc(), pointer,
              evalNBAQueueField(nbaBuilder, call.getLoc(), 0));
          Value index =
              arith::ExtUIOp::create(nbaBuilder, call.getLoc(), i64, size);
          Value record = LLVM::GEPOp::create(nbaBuilder, call.getLoc(), pointer,
                                             LLVM::LLVMArrayType::get(i64, 4),
                                             data, ValueRange{index});
          for (auto [field, value] : llvm::enumerate(SmallVector<Value>{
                   llvmConstant(nbaBuilder, call.getLoc(), i64, *site),
                   requestedStart, staged64, stagedUnknown64}))
            LLVM::StoreOp::create(
                nbaBuilder, call.getLoc(), value,
                byteGEP(nbaBuilder, call.getLoc(), record, field * 8), 8);
          LLVM::StoreOp::create(
              nbaBuilder, call.getLoc(),
              arith::AddIOp::create(
                  nbaBuilder, call.getLoc(), size,
                  llvmConstant(nbaBuilder, call.getLoc(), i32, 1)),
              evalNBAQueueField(nbaBuilder, call.getLoc(), 1), 4);
          cf::BranchOp::create(nbaBuilder, call.getLoc(), continuation);
        } else if (periodicWideLatch) {
          auto existing =
              llvm::find_if(dynamicEvalNBAs, [&](const DynamicEvalNBA &entry) {
                return entry.site == *site;
              });
          if (existing == dynamicEvalNBAs.end()) {
            DynamicEvalNBA entry{root->second, *site, *width};
            entry.offsetName =
                (Twine("__obelisk_eval_nba_offset_") + Twine(*site)).str();
            entry.valueName =
                (Twine("__obelisk_eval_nba_value_") + Twine(*site)).str();
            entry.unknownName =
                (Twine("__obelisk_eval_nba_unknown_") + Twine(*site)).str();
            entry.validName =
                (Twine("__obelisk_eval_nba_valid_") + Twine(*site)).str();
            auto makeZero = [&](StringRef name, Type type, unsigned alignment) {
              OpBuilder globalBuilder =
                  OpBuilder::atBlockBegin(module.getBody());
              auto global = LLVM::GlobalOp::create(
                  globalBuilder, call.getLoc(), type, false,
                  LLVM::Linkage::Internal, name, Attribute{}, alignment);
              Block *initializer = new Block;
              global.getInitializerRegion().push_back(initializer);
              OpBuilder initBuilder = OpBuilder::atBlockBegin(initializer);
              LLVM::ReturnOp::create(
                  initBuilder, call.getLoc(),
                  LLVM::ZeroOp::create(initBuilder, call.getLoc(), type));
            };
            makeZero(entry.offsetName, i64, 8);
            makeZero(entry.valueName, i64, 8);
            makeZero(entry.unknownName, i64, 8);
            makeZero(entry.validName, i32, 4);
            dynamicEvalNBAs.push_back(std::move(entry));
            existing = std::prev(dynamicEvalNBAs.end());
          }
          auto storeGlobal = [&](StringRef name, Value value,
                                 unsigned alignment) {
            LLVM::StoreOp::create(nbaBuilder, call.getLoc(), value,
                                  LLVM::AddressOfOp::create(
                                      nbaBuilder, call.getLoc(), pointer, name),
                                  alignment);
          };
          storeGlobal(existing->offsetName, requestedStart, 8);
          storeGlobal(existing->valueName, staged64, 8);
          storeGlobal(existing->unknownName, stagedUnknown64, 8);
          storeGlobal(
              existing->validName,
              LLVM::ZExtOp::create(nbaBuilder, call.getLoc(), i32, overlaps),
              4);
        } else {
          Value below = arith::CmpIOp::create(nbaBuilder, call.getLoc(),
                                              arith::CmpIPredicate::slt,
                                              requestedStart, zero64);
          Value start = arith::SelectOp::create(nbaBuilder, call.getLoc(),
                                                below, zero64, requestedStart);
          Value requestedEnd = arith::AddIOp::create(
              nbaBuilder, call.getLoc(), requestedStart,
              llvmConstant(nbaBuilder, call.getLoc(), i64, *width));
          Value rootWidthValue =
              llvmConstant(nbaBuilder, call.getLoc(), i64, rootWidth);
          Value above = arith::CmpIOp::create(nbaBuilder, call.getLoc(),
                                              arith::CmpIPredicate::sgt,
                                              requestedEnd, rootWidthValue);
          Value end = arith::SelectOp::create(nbaBuilder, call.getLoc(), above,
                                              rootWidthValue, requestedEnd);
          Value overlapWidth =
              arith::SubIOp::create(nbaBuilder, call.getLoc(), end, start);
          Value sourceStart = arith::SubIOp::create(nbaBuilder, call.getLoc(),
                                                    start, requestedStart);
          Value safeWidth = arith::SelectOp::create(
              nbaBuilder, call.getLoc(), overlaps, overlapWidth, zero64);
          Value safeSource = arith::SelectOp::create(
              nbaBuilder, call.getLoc(), overlaps, sourceStart, zero64);
          Value safeDestination = arith::SelectOp::create(
              nbaBuilder, call.getLoc(), overlaps, start, zero64);
          Value widthIs64 = arith::CmpIOp::create(
              nbaBuilder, call.getLoc(), arith::CmpIPredicate::eq, safeWidth,
              llvmConstant(nbaBuilder, call.getLoc(), i64, 64));
          Value maskShift = arith::SelectOp::create(
              nbaBuilder, call.getLoc(), widthIs64,
              llvmConstant(nbaBuilder, call.getLoc(), i64, 63), safeWidth);
          Value lowMask = arith::SubIOp::create(
              nbaBuilder, call.getLoc(),
              arith::ShLIOp::create(
                  nbaBuilder, call.getLoc(),
                  llvmConstant(nbaBuilder, call.getLoc(), i64, 1), maskShift),
              llvmConstant(nbaBuilder, call.getLoc(), i64, 1));
          lowMask = arith::SelectOp::create(
              nbaBuilder, call.getLoc(), widthIs64,
              llvmConstant(nbaBuilder, call.getLoc(), i64, UINT64_MAX),
              lowMask);
          Value mask = arith::ShLIOp::create(nbaBuilder, call.getLoc(), lowMask,
                                             safeDestination);
          auto position = [&](Value source) {
            return arith::AndIOp::create(
                nbaBuilder, call.getLoc(),
                arith::ShLIOp::create(
                    nbaBuilder, call.getLoc(),
                    arith::ShRUIOp::create(nbaBuilder, call.getLoc(), source,
                                           safeSource),
                    safeDestination),
                mask);
          };
          Value accumulator = LLVM::AddressOfOp::create(
              nbaBuilder, call.getLoc(), pointer,
              staticNBAPlan.generatedAccumulators[root->second]);
          if (root->second < staticNBAPlan.trackTransients.size() &&
              staticNBAPlan.trackTransients[root->second])
            emitGeneratedNBATransient(nbaBuilder, call.getLoc(), accumulator, 0,
                                      mask, position(staged64),
                                      position(stagedUnknown64));
          auto mergeField = [&](size_t fieldOffset, Value positioned) {
            Value address =
                byteGEP(nbaBuilder, call.getLoc(), accumulator, fieldOffset);
            Value previous = LLVM::LoadOp::create(nbaBuilder, call.getLoc(),
                                                  i64, address, 8);
            Value merged = arith::OrIOp::create(
                nbaBuilder, call.getLoc(),
                arith::AndIOp::create(
                    nbaBuilder, call.getLoc(), previous,
                    arith::XOrIOp::create(nbaBuilder, call.getLoc(), mask,
                                          llvmConstant(nbaBuilder,
                                                       call.getLoc(), i64,
                                                       UINT64_MAX))),
                positioned);
            LLVM::StoreOp::create(nbaBuilder, call.getLoc(), merged, address,
                                  8);
          };
          mergeField(offsetof(obelisk_rt_generated_nba_accumulator_256, value),
                     position(staged64));
          mergeField(
              offsetof(obelisk_rt_generated_nba_accumulator_256, unknown),
              position(stagedUnknown64));
          Value maskAddress = byteGEP(
              nbaBuilder, call.getLoc(), accumulator,
              offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask));
          Value previousMask = LLVM::LoadOp::create(nbaBuilder, call.getLoc(),
                                                    i64, maskAddress, 8);
          LLVM::StoreOp::create(nbaBuilder, call.getLoc(),
                                arith::OrIOp::create(nbaBuilder, call.getLoc(),
                                                     previousMask, mask),
                                maskAddress, 8);
          Value validAddress = byteGEP(
              nbaBuilder, call.getLoc(), accumulator,
              offsetof(obelisk_rt_generated_nba_accumulator_256, valid));
          Value previousValid = LLVM::LoadOp::create(nbaBuilder, call.getLoc(),
                                                     i32, validAddress, 4);
          LLVM::StoreOp::create(
              nbaBuilder, call.getLoc(),
              arith::OrIOp::create(nbaBuilder, call.getLoc(), previousValid,
                                   LLVM::ZExtOp::create(nbaBuilder,
                                                        call.getLoc(), i32,
                                                        overlaps)),
              validAddress, 4);
          LLVM::StoreOp::create(
              nbaBuilder, call.getLoc(),
              llvmConstant(nbaBuilder, call.getLoc(), i32,
                           proof->second.commitRegion),
              byteGEP(nbaBuilder, call.getLoc(), accumulator,
                      offsetof(obelisk_rt_generated_nba_accumulator_256,
                               exec_region)),
              4);
          uint32_t dirtyWord = root->second / 64;
          Value dirtyBase = LLVM::AddressOfOp::create(
              nbaBuilder, call.getLoc(), pointer, nbaDirtyRootsName);
          Value dirtyAddress =
              byteGEP(nbaBuilder, call.getLoc(), dirtyBase,
                      static_cast<uint64_t>(dirtyWord) * sizeof(uint64_t));
          Value previousDirty = LLVM::LoadOp::create(nbaBuilder, call.getLoc(),
                                                     i64, dirtyAddress, 8);
          Value dirtyBit = arith::SelectOp::create(
              nbaBuilder, call.getLoc(), overlaps,
              llvmConstant(nbaBuilder, call.getLoc(), i64,
                           uint64_t{1} << (root->second % 64)),
              zero64);
          LLVM::StoreOp::create(nbaBuilder, call.getLoc(),
                                arith::OrIOp::create(nbaBuilder, call.getLoc(),
                                                     previousDirty, dirtyBit),
                                dirtyAddress, 8);
          Value summaryBase = LLVM::AddressOfOp::create(
              nbaBuilder, call.getLoc(), pointer, nbaDirtySummaryName);
          Value summaryAddress =
              byteGEP(nbaBuilder, call.getLoc(), summaryBase,
                      static_cast<uint64_t>(dirtyWord / 64) * sizeof(uint64_t));
          Value previousSummary = LLVM::LoadOp::create(
              nbaBuilder, call.getLoc(), i64, summaryAddress, 8);
          Value summaryBit = arith::SelectOp::create(
              nbaBuilder, call.getLoc(), overlaps,
              llvmConstant(nbaBuilder, call.getLoc(), i64,
                           uint64_t{1} << (dirtyWord % 64)),
              zero64);
          LLVM::StoreOp::create(nbaBuilder, call.getLoc(),
                                arith::OrIOp::create(nbaBuilder, call.getLoc(),
                                                     previousSummary,
                                                     summaryBit),
                                summaryAddress, 8);
        }
      }
      if (call.getNumResults() != 0) {
        if (call.getNumResults() != 1 || call.getResult().getType() != i32)
          return call.emitError("unsupported eval runtime escape ABI"),
                 failure();
        OpBuilder escapeBuilder(call);
        call.getResult().replaceAllUsesWith(
            llvmConstant(escapeBuilder, call.getLoc(), i32, OBELISK_RT_OK));
      }
      call.erase();
      if (handleSelectToErase && handleSelectToErase->use_empty())
        handleSelectToErase->erase();
      if (handleOffsetToErase && handleOffsetToErase->use_empty())
        handleOffsetToErase.erase();
    }
    // Lower a dynamic selection within one proven packed root directly into
    // the canonical state plane. The generic state ABI must decode arbitrary
    // stable handles and consult runtime overlays; a generated eval body has
    // already crossed the clean-state handover and carries a statically known
    // root. Keep the dynamic subscript, its source-language validity guard,
    // and the root bounds check, but do not re-enter the runtime for each
    // register-file read.
    SmallVector<std::pair<LLVM::CallOp, bool>> dynamicPlaneLoads;
    module.walk([&](sim::SimFuncOp function) {
      if (!isGeneratedEvalBody(function, selectedRawBodies))
        return;
      bool twoState = function->hasAttr("obelisk.eval.selected_two_state");
      function.walk([&](LLVM::CallOp call) {
        if (call.getCallee() &&
            *call.getCallee() == "obelisk_rt_v1_native_state_load_plane")
          dynamicPlaneLoads.push_back({call, twoState});
      });
    });
    for (auto [call, twoState] : dynamicPlaneLoads) {
      ValueRange arguments = call.getArgOperands();
      if (arguments.size() != 8)
        return call.emitError("malformed native state load ABI"), failure();
      std::optional<uint64_t> stateBits = constantU64(arguments[2]);
      std::optional<uint64_t> width = constantU64(arguments[4]);
      std::optional<uint64_t> unknownPlane = constantU64(arguments[5]);
      std::optional<uint64_t> fallback = constantU64(arguments[6]);
      if (!stateBits || *stateBits != stateLayout.bitCount || !width ||
          *width == 0 || *width > 64 || !unknownPlane || *unknownPlane > 1 ||
          !fallback || *fallback > 1)
        return call.emitError(
                   "runtime-free eval cannot lower dynamic state load"),
               failure();

      Value handle = arguments[3];
      Value sourceValid;
      bool invertSourceValid = false;
      Operation *handleSelect = handle.getDefiningOp();
      if (isa_and_nonnull<arith::SelectOp, LLVM::SelectOp>(handleSelect)) {
        Value condition = handleSelect->getOperand(0);
        Value trueValue = handleSelect->getOperand(1);
        Value falseValue = handleSelect->getOperand(2);
        std::optional<uint64_t> trueConstant = constantU64(trueValue);
        std::optional<uint64_t> falseConstant = constantU64(falseValue);
        if (falseConstant && *falseConstant == UINT64_MAX) {
          sourceValid = condition;
          handle = trueValue;
        } else if (trueConstant && *trueConstant == UINT64_MAX) {
          sourceValid = condition;
          invertSourceValid = true;
          handle = falseValue;
        } else {
          return call.emitError(
                     "dynamic state handle has no invalid-handle guard"),
                 failure();
        }
      }
      auto owner = call->getParentOfType<sim::SimFuncOp>();
      // Local references, including slices with a handle-offset operation,
      // may survive here only in the retained cold callback copy. Route
      // materialization fractures checkpoint paths afterwards, and the closed
      // call-graph verifier rejects any runtime load still reachable hot.
      // Helper selection also precedes that fracture: a selected ordinary
      // helper may be reachable only from a cold successor of its caller.
      // Predicates themselves never get this deferral.
      const bool deferColdReference =
          owner && !owner->hasAttr("obelisk.eval.path_known_predicate") &&
          ((!owner->hasAttr("obelisk.eval.raw_captures") &&
            owner->hasAttr("obelisk.eval.selected_two_state")) ||
           owner->hasAttr(sim::metadata::evalPathGuardedTwoState) ||
           (owner->hasAttr("obelisk.eval.inherited_two_state_checkpoint") &&
            owner->hasAttr(sim::metadata::evalTwoStateVariant)));
      auto offsetCall = handle.getDefiningOp<LLVM::CallOp>();
      if (!offsetCall || !offsetCall.getCallee() ||
          *offsetCall.getCallee() != "obelisk_rt_v1_native_handle_offset" ||
          offsetCall.getArgOperands().size() != 2) {
        if (deferColdReference)
          continue;
        return call.emitError(
                   "dynamic state load has no fixed-root handle offset"),
               failure();
      }
      std::optional<uint64_t> encodedRoot =
          constantU64(offsetCall.getArgOperands()[0]);
      obelisk_rt_stable_handle_v1 decoded{};
      if (!encodedRoot ||
          !obelisk_rt_stable_handle_decode(*encodedRoot, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
          (!stateLayout.directHandles.contains(decoded.id) &&
           !stateLayout.guardedHandles.contains(decoded.id))) {
        if (deferColdReference)
          continue;
        return call.emitError(
                   "dynamic state load root is not direct-state certified"),
               failure();
      }
      auto bound = llvm::find_if(
          stateLayout.bounds, [&](const NativeStateLayout::Bound &candidate) {
            return candidate.handleID == decoded.id;
          });
      if (bound == stateLayout.bounds.end() || *width > bound->width ||
          decoded.offset < 0 ||
          static_cast<uint64_t>(decoded.offset) > bound->width - *width)
        return call.emitError("dynamic state load root is out of bounds"),
               failure();

      OpBuilder loadBuilder(call);
      Value dynamicOffset = offsetCall.getArgOperands()[1];
      // IEEE 1800-2017 11.5.1 preserves every in-range bit of a partially
      // overhanging packed select and supplies the ordinary invalid-index
      // value only for the remainder.  Accept any overlap here, clamp the
      // physical load into the root, then align and mask it back into the
      // source-language result coordinates.
      int64_t lowerOverlap = -decoded.offset - static_cast<int64_t>(*width - 1);
      int64_t upperOverlap =
          static_cast<int64_t>(bound->width - 1) - decoded.offset;
      Value lower = llvmConstant(loadBuilder, call.getLoc(), i64, lowerOverlap);
      Value upper = llvmConstant(loadBuilder, call.getLoc(), i64, upperOverlap);
      Value inLower = arith::CmpIOp::create(loadBuilder, call.getLoc(),
                                            arith::CmpIPredicate::sge,
                                            dynamicOffset, lower);
      Value inUpper = arith::CmpIOp::create(loadBuilder, call.getLoc(),
                                            arith::CmpIPredicate::sle,
                                            dynamicOffset, upper);
      Value overlaps =
          arith::AndIOp::create(loadBuilder, call.getLoc(), inLower, inUpper);
      if (sourceValid) {
        if (invertSourceValid)
          sourceValid =
              arith::XOrIOp::create(loadBuilder, call.getLoc(), sourceValid,
                                    llvmConstant(loadBuilder, call.getLoc(),
                                                 loadBuilder.getI1Type(), 1));
        overlaps = arith::AndIOp::create(loadBuilder, call.getLoc(),
                                         sourceValid, overlaps);
      }
      // Select a safe coordinate before the load itself: LLVM select is not a
      // control dependency and an invalid source index must never form an
      // out-of-plane speculative address.
      Value safeDynamic = arith::SelectOp::create(
          loadBuilder, call.getLoc(), overlaps, dynamicOffset,
          llvmConstant(loadBuilder, call.getLoc(), i64, -decoded.offset));
      Value requestedStart = arith::AddIOp::create(
          loadBuilder, call.getLoc(), safeDynamic,
          llvmConstant(loadBuilder, call.getLoc(), i64, decoded.offset));
      Value zero64 = llvmConstant(loadBuilder, call.getLoc(), i64, 0);
      Value maximumStart =
          llvmConstant(loadBuilder, call.getLoc(), i64, bound->width - *width);
      Value below = arith::CmpIOp::create(loadBuilder, call.getLoc(),
                                          arith::CmpIPredicate::slt,
                                          requestedStart, zero64);
      Value above = arith::CmpIOp::create(loadBuilder, call.getLoc(),
                                          arith::CmpIPredicate::sgt,
                                          requestedStart, maximumStart);
      Value clampedLow = arith::SelectOp::create(loadBuilder, call.getLoc(),
                                                 below, zero64, requestedStart);
      Value clampedStart = arith::SelectOp::create(
          loadBuilder, call.getLoc(), above, maximumStart, clampedLow);
      Value sourceBit = arith::AddIOp::create(
          loadBuilder, call.getLoc(), clampedStart,
          llvmConstant(loadBuilder, call.getLoc(), i64, bound->offset));
      IntegerType resultType = IntegerType::get(context, *width);
      Value loaded;
      if (twoState && *unknownPlane) {
        // The two-state handover certifies that every live canonical unknown
        // bit is zero.  Its generated body therefore omits unknown-plane
        // traffic, but an invalid source-language index must still select the
        // ABI fallback below (all ones for a four-state integral result).
        loaded = arith::ConstantOp::create(
            loadBuilder, call.getLoc(), resultType,
            loadBuilder.getIntegerAttr(resultType, 0));
      } else {
        Value byteOffset = arith::ShRUIOp::create(
            loadBuilder, call.getLoc(), sourceBit,
            llvmConstant(loadBuilder, call.getLoc(), i64, 3));
        Value bitOffset = arith::AndIOp::create(
            loadBuilder, call.getLoc(), sourceBit,
            llvmConstant(loadBuilder, call.getLoc(), i64, 7));
        Value address = LLVM::GEPOp::create(
            loadBuilder, call.getLoc(), pointer, loadBuilder.getI8Type(),
            arguments[1], ValueRange{byteOffset});
        unsigned spanWidth = static_cast<unsigned>(((*width + 14) / 8) * 8);
        IntegerType spanType = IntegerType::get(context, spanWidth);
        Value span = LLVM::LoadOp::create(loadBuilder, call.getLoc(), spanType,
                                          address, 1);
        Value shift = spanWidth < 64
                          ? LLVM::TruncOp::create(loadBuilder, call.getLoc(),
                                                  spanType, bitOffset)
                                .getResult()
                          : LLVM::ZExtOp::create(loadBuilder, call.getLoc(),
                                                 spanType, bitOffset)
                                .getResult();
        Value shifted =
            arith::ShRUIOp::create(loadBuilder, call.getLoc(), span, shift);
        loaded = spanWidth == *width
                     ? shifted
                     : LLVM::TruncOp::create(loadBuilder, call.getLoc(),
                                             resultType, shifted)
                           .getResult();
      }
      APInt fallbackBits =
          *fallback ? APInt::getAllOnes(*width) : APInt::getZero(*width);
      Value lowClip = arith::SelectOp::create(
          loadBuilder, call.getLoc(), below,
          arith::SubIOp::create(loadBuilder, call.getLoc(), zero64,
                                requestedStart),
          zero64);
      Value highClip = arith::SelectOp::create(
          loadBuilder, call.getLoc(), above,
          arith::SubIOp::create(loadBuilder, call.getLoc(), requestedStart,
                                maximumStart),
          zero64);
      auto resultShift = [&](Value shift) -> Value {
        return *width == 64 ? shift
                            : LLVM::TruncOp::create(loadBuilder, call.getLoc(),
                                                    resultType, shift)
                                  .getResult();
      };
      Value lowShift = resultShift(lowClip);
      Value highShift = resultShift(highClip);
      loaded =
          arith::ShLIOp::create(loadBuilder, call.getLoc(), loaded, lowShift);
      loaded =
          arith::ShRUIOp::create(loadBuilder, call.getLoc(), loaded, highShift);
      Value widthMask = arith::ConstantOp::create(
          loadBuilder, call.getLoc(), resultType,
          loadBuilder.getIntegerAttr(resultType, APInt::getAllOnes(*width)));
      Value overlapMask = arith::ShLIOp::create(loadBuilder, call.getLoc(),
                                                widthMask, lowShift);
      overlapMask = arith::ShRUIOp::create(loadBuilder, call.getLoc(),
                                           overlapMask, highShift);
      overlapMask = arith::SelectOp::create(
          loadBuilder, call.getLoc(), overlaps, overlapMask,
          arith::ConstantOp::create(
              loadBuilder, call.getLoc(), resultType,
              loadBuilder.getIntegerAttr(resultType, APInt::getZero(*width))));
      Value fallbackValue = arith::ConstantOp::create(
          loadBuilder, call.getLoc(), resultType,
          loadBuilder.getIntegerAttr(resultType, fallbackBits));
      Value selected = arith::OrIOp::create(
          loadBuilder, call.getLoc(),
          arith::AndIOp::create(loadBuilder, call.getLoc(), loaded,
                                overlapMask),
          arith::AndIOp::create(loadBuilder, call.getLoc(), fallbackValue,
                                arith::XOrIOp::create(loadBuilder,
                                                      call.getLoc(),
                                                      overlapMask, widthMask)));
      LLVM::StoreOp::create(loadBuilder, call.getLoc(), selected, arguments[7],
                            1);
      if (call.getNumResults() != 1 || call.getResult().getType() != i32)
        return call.emitError("unsupported native state load status ABI"),
               failure();
      call.getResult().replaceAllUsesWith(
          llvmConstant(loadBuilder, call.getLoc(), i32, OBELISK_RT_OK));
      call.erase();
      if (handleSelect && handleSelect->use_empty())
        handleSelect->erase();
    }
    // A fixed lane of a dynamic array element retains the inner encoded
    // handle as the outer slice's base. Inline that pure constant-root offset
    // calculation, including the signed 32-bit handle bounds. The outer NBA
    // guard still rejects invalid array indices and a mismatched root tag.
    module.walk([&](sim::SimFuncOp function) {
      if (!isGeneratedEvalBody(function, selectedRawBodies))
        return;
      SmallVector<LLVM::CallOp> offsets;
      function.walk([&](LLVM::CallOp call) {
        if (call.getCallee() &&
            *call.getCallee() == "obelisk_rt_v1_native_handle_offset" &&
            call.getArgOperands().size() == 2 && !call->use_empty())
          offsets.push_back(call);
      });
      for (LLVM::CallOp call : offsets) {
        auto root = constantU64(call.getArgOperands()[0]);
        obelisk_rt_stable_handle_v1 decoded{};
        if (!root || !obelisk_rt_stable_handle_decode(*root, &decoded) ||
            decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC)
          continue;
        OpBuilder offsetBuilder(call);
        Location loc = call.getLoc();
        auto constant = [&](int64_t value) {
          return llvmConstant(offsetBuilder, loc, i64, value);
        };
        Value amount = call.getArgOperands()[1];
        Value valid = arith::AndIOp::create(
            offsetBuilder, loc,
            arith::CmpIOp::create(offsetBuilder, loc, arith::CmpIPredicate::sge,
                                  amount, constant(INT32_MIN - decoded.offset)),
            arith::CmpIOp::create(offsetBuilder, loc, arith::CmpIPredicate::sle,
                                  amount,
                                  constant(INT32_MAX - decoded.offset)));
        Value offset = arith::AddIOp::create(offsetBuilder, loc, amount,
                                             constant(decoded.offset));
        offset = arith::AndIOp::create(offsetBuilder, loc, offset,
                                       constant(UINT32_MAX));
        Value encoded = arith::OrIOp::create(
            offsetBuilder, loc, offset,
            constant(*root & UINT64_C(0xffffffff00000000)));
        Value result = arith::SelectOp::create(offsetBuilder, loc, valid,
                                               encoded, constant(-1));
        call.getResult().replaceAllUsesWith(result);
        call.erase();
      }
    });
    // Reference lowering can leave a short chain of now-dead validity selects
    // above a handle-offset call.  Peel it to a fixed point so the hot-closure
    // verifier sees neither the obsolete runtime call nor its guard plumbing.
    while (true) {
      SmallVector<Operation *> deadHandleOperations;
      module.walk([&](sim::SimFuncOp function) {
        if (!isGeneratedEvalBody(function, selectedRawBodies))
          return;
        function.walk([&](Operation *operation) {
          if (!operation->use_empty())
            return;
          if (auto call = dyn_cast<LLVM::CallOp>(operation)) {
            if (call.getCallee() &&
                *call.getCallee() == "obelisk_rt_v1_native_handle_offset")
              deadHandleOperations.push_back(operation);
            return;
          }
          if (!isa<arith::SelectOp, LLVM::SelectOp>(operation))
            return;
          bool selectsHandleOffset = llvm::any_of(
              operation->getOperands().drop_front(), [](Value operand) {
                auto producer = operand.getDefiningOp<LLVM::CallOp>();
                return producer && producer.getCallee() &&
                       *producer.getCallee() ==
                           "obelisk_rt_v1_native_handle_offset";
              });
          if (selectsHandleOffset)
            deadHandleOperations.push_back(operation);
        });
      });
      if (deadHandleOperations.empty())
        break;
      for (Operation *operation : deadHandleOperations)
        operation->erase();
    }
    llvm::SmallDenseSet<uint32_t, 8> dynamicRoots;
    SmallVector<llvm::SmallDenseSet<uint64_t, 4>> dynamicOrigins(
        staticNBAPlan.roots.size());
    for (const DynamicEvalNBA &entry : dynamicEvalNBAs)
      if (!entry.queued &&
          ((!dynamicRoots.insert(entry.rootIndex).second &&
            !staticNBAPlan.independentSiteWrites[entry.rootIndex]) ||
           !dynamicOrigins[entry.rootIndex]
                .insert(staticNBAPlan.siteSemanticOrigins.lookup(entry.site))
                .second))
        return module.emitError("runtime-free eval has multiple ordered "
                                "dynamic NBA sites for "
                                "one root"),
               failure();
  }
  Type clockKernelType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, pointer, i32, i32, pointer});
  if (!clockKernels.empty()) {
    Type clocksType =
        LLVM::LLVMArrayType::get(clockKernelType, clockKernels.size());
    makeConstantGlobal(
        module, location, clocksType, clockKernelsName, LLVM::Linkage::Internal,
        8, [&](OpBuilder &initializerBuilder) {
          Value clocks =
              LLVM::ZeroOp::create(initializerBuilder, location, clocksType);
          for (auto [index, kernel] : llvm::enumerate(clockKernels)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               clockKernelType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             kernel.staticState),
                                0);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, kernel.edge),
                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, kernel.lowBit),
                2);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             kernel.bitWidth),
                                3);
            value = insertValue(initializerBuilder, location, value,
                                LLVM::AddressOfOp::create(initializerBuilder,
                                                          location, pointer,
                                                          kernel.ingressName),
                                4);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             ingressWordCount(kernel)),
                                5);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32,
                             readyLayout.hasCache()
                                 ? runtime::indexedClockKernelReadySet
                                 : 0),
                6);
            value = insertValue(initializerBuilder, location, value,
                                LLVM::AddressOfOp::create(initializerBuilder,
                                                          location, pointer,
                                                          kernel.activeName),
                                7);
            clocks = LLVM::InsertValueOp::create(
                initializerBuilder, location, clocks, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return clocks;
        });
  }
  Type mergedFragmentType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i32, i32, pointer});
  if (!mergedFragments.empty()) {
    Type mergedType =
        LLVM::LLVMArrayType::get(mergedFragmentType, mergedFragments.size());
    makeConstantGlobal(
        module, location, mergedType, mergedFragmentsName,
        LLVM::Linkage::Internal, 8, [&](OpBuilder &initializerBuilder) {
          Value merged =
              LLVM::ZeroOp::create(initializerBuilder, location, mergedType);
          for (auto [index, record] : llvm::enumerate(mergedFragments)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               mergedFragmentType);
            const uint32_t fields[] = {record.actor_slot,   record.continuation,
                                       record.kernel,       record.bit,
                                       record.compute_node, record.flags};
            for (unsigned field = 0; field != std::size(fields); ++field)
              value = insertValue(initializerBuilder, location, value,
                                  llvmConstant(initializerBuilder, location,
                                               i32, fields[field]),
                                  field);
            Value execute =
                mergedExecutors[index].empty()
                    ? LLVM::ZeroOp::create(initializerBuilder, location,
                                           pointer)
                          .getResult()
                    : LLVM::AddressOfOp::create(initializerBuilder, location,
                                                pointer, mergedExecutors[index])
                          .getResult();
            value =
                insertValue(initializerBuilder, location, value, execute, 6);
            merged = LLVM::InsertValueOp::create(
                initializerBuilder, location, merged, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return merged;
        });
  }
  Type actorRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32, i32});
  if (!actorRoots.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(actorRootType, actorRoots.size());
    makeConstantGlobal(
        module, location, entriesType, actorRootsName, LLVM::Linkage::Internal,
        4, [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(actorRoots)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               actorRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.flags),
                2);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }

  constexpr StringLiteral periodicAliasesName =
      "__obelisk_periodic_alias_plan_v1";
  Type periodicAliasType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i64, i64, i64});
  if (!periodicAliases.empty()) {
    Type aliasesType =
        LLVM::LLVMArrayType::get(periodicAliasType, periodicAliases.size());
    makeConstantGlobal(
        module, location, aliasesType, periodicAliasesName,
        LLVM::Linkage::Internal, 8, [&](OpBuilder &initializerBuilder) {
          Value aliases =
              LLVM::ZeroOp::create(initializerBuilder, location, aliasesType);
          for (auto [index, alias] : llvm::enumerate(periodicAliases)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               periodicAliasType);
            const uint32_t fields[] = {
                alias.sourceStaticState, alias.forwardingActorSlot,
                alias.forwardingContinuation, alias.targetStaticState};
            for (unsigned field = 0; field != std::size(fields); ++field)
              value = insertValue(initializerBuilder, location, value,
                                  llvmConstant(initializerBuilder, location,
                                               i32, fields[field]),
                                  field);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             alias.sourceBitOffset),
                                4);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             alias.targetBitOffset),
                                5);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             alias.driverBitOffset),
                                6);
            aliases = LLVM::InsertValueOp::create(
                initializerBuilder, location, aliases, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return aliases;
        });
  }

  builder.setInsertionPointToEnd(module.getBody());
  auto bind = LLVM::LLVMFuncOp::create(
      builder, location, bindName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  Block *bindEntry = bind.addEntryBlock(builder);
  builder.setInsertionPointToStart(bindEntry);
  Value slot =
      LLVM::ZExtOp::create(builder, location, i64, bindEntry->getArgument(2));
  Value actorAddress =
      LLVM::GEPOp::create(builder, location, pointer, pointer,
                          bindEntry->getArgument(0), ValueRange{slot});
  LLVM::StoreOp::create(builder, location, bindEntry->getArgument(3),
                        actorAddress, 8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, OBELISK_RT_OK));

  builder.setInsertionPointToEnd(module.getBody());
  auto run = LLVM::LLVMFuncOp::create(
      builder, location, runName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *runEntry = run.addEntryBlock(builder);
  builder.setInsertionPointToStart(runEntry);
  bool generatedEvalPlan =
      !clockKernels.empty() && !mergedFragments.empty() &&
      mergedExecutors.size() == mergedFragments.size() &&
      mergedTwoStateExecutors.size() == mergedFragments.size() &&
      llvm::none_of(mergedExecutors,
                    [](const std::string &name) { return name.empty(); });
  if (!generatedEvalPlan) {
    auto firstEmpty = llvm::find_if(
        mergedExecutors, [](const std::string &name) { return name.empty(); });
    auto diagnostic = module.emitError();
    diagnostic << "cannot materialize generated eval loop: clocks="
               << periodicClocks.size() << " kernels=" << clockKernels.size()
               << " fragments=" << mergedFragments.size()
               << " executors=" << mergedTwoStateExecutors.size()
               << " empty-executors="
               << llvm::count_if(mergedExecutors, [](const std::string &name) {
                    return name.empty();
                  });
    if (firstEmpty != mergedExecutors.end()) {
      size_t index = static_cast<size_t>(firstEmpty - mergedExecutors.begin());
      diagnostic << " first-empty-actor=" << mergedFragments[index].actor_slot
                 << " continuation=" << mergedFragments[index].continuation;
      if (auto planned = staticFanoutPlan.fragments.find(
              {mergedFragments[index].actor_slot,
               mergedFragments[index].continuation});
          planned != staticFanoutPlan.fragments.end()) {
        diagnostic << " planned-fragments=";
        for (uint32_t fragment : planned->second)
          diagnostic << fragment << ",";
        diagnostic << " intersecting-direct=";
        for (const NativeDirectFragment &candidate : directFragments)
          if (llvm::any_of(planned->second, [&](uint32_t fragment) {
                return llvm::is_contained(candidate.fragmentIDs, fragment);
              })) {
            diagnostic << "[" << candidate.actorSlot << ":"
                       << candidate.continuation << ":";
            for (uint32_t fragment : candidate.fragmentIDs)
              diagnostic << fragment << ",";
            diagnostic << "]";
          }
      }
      for (const NativeDirectFragment &direct : directFragments)
        if (direct.actorSlot == mergedFragments[index].actor_slot) {
          diagnostic << " direct-continuation=" << direct.continuation
                     << " direct-fragments=" << direct.fragmentIDs.size()
                     << " direct-ranges=" << direct.promotionRanges.size()
                     << " direct-two-state=" << !direct.twoStateWrapper.empty();
          for (uint32_t fragment : direct.fragmentIDs)
            diagnostic << " fragment=" << fragment;
        }
      diagnostic << " available-direct=";
      for (const NativeDirectFragment &direct : directFragments)
        diagnostic << "[" << direct.actorSlot << ":" << direct.continuation
                   << ":" << direct.fusionGroup << ":"
                   << !direct.twoStateWrapper.empty() << "]";
    }
    return failure();
  }
  if (periodicClocks.empty()) {
    // A clockless design retains ordinary calendar/control ownership in the
    // trusted AOT node loop. Static fanout queues generated ingress there, so
    // the same coordinator below drains event-driven model work without the
    // legacy metadata-only schedule wrapper.
    Value nodes =
        LLVM::AddressOfOp::create(builder, location, pointer, nodesName);
    Value status = LLVM::CallOp::create(
                       builder, location, TypeRange{i32},
                       SymbolRefAttr::get(
                           context, "obelisk_rt_v1_scheduler_run_aot_nodes"),
                       ValueRange{runEntry->getArgument(1), nodes,
                                  llvmConstant(builder, location, i32,
                                               executableNodes.size())})
                       .getResult();
    LLVM::ReturnOp::create(builder, location, status);
  } else {
    // One model-wide ready bit owns each direct fragment. Physical trigger
    // groups remain distinct and merely OR into that shared model mask, so a
    // fragment reached by coincident clocks executes once before the common
    // NBA barrier.
    SmallVector<SmallVector<std::pair<APInt, APInt>>> clockMasks(
        periodicClocks.size(),
        SmallVector<std::pair<APInt, APInt>>(
            clockKernels.size(), {APInt(ownerCount, 0), APInt(ownerCount, 0)}));
    SmallVector<SmallVector<std::pair<APInt, APInt>>> clockDirectMasks(
        periodicClocks.size(),
        SmallVector<std::pair<APInt, APInt>>(
            clockKernels.size(), {APInt(ownerCount, 0), APInt(ownerCount, 0)}));
    auto canDispatchClockOwnerDirectly = [&](uint32_t bit) {
      auto record = llvm::find_if(mergedFragments, [&](const auto &candidate) {
        return candidate.bit == bit;
      });
      if (record == mergedFragments.end())
        return false;
      size_t index = static_cast<size_t>(record - mergedFragments.begin());
      if (mergedExecutors[index].empty())
        return false;
      LLVM::LLVMFuncOp executor =
          executorSymbols.lookup<LLVM::LLVMFuncOp>(mergedExecutors[index]);
      // A fractured checkpoint owner can share the direct prefix with an
      // infallible owner because the prefix checks its status before any
      // downstream owner executes. Convergence owners must consume their ready
      // bit before execution
      // so self-publication survives and drives the next fixpoint iteration;
      // the straight-line clock bypass deliberately clears after execution.
      // Coincident clocks OR into the shared direct-ready mask and the owner
      // table is traversed once, so one outlined module-instance body still
      // executes exactly once in a multi-clock slot.
      bool directStatus =
          executor && (executor->hasAttr(sim::metadata::evalInfallible) ||
                       executor->hasAttr(sim::metadata::evalCheckpointSafe));
      return directStatus &&
             !executor->hasAttr(sim::metadata::evalTier2Convergence);
    };
    auto periodicBitTouchesFanout =
        [](uint64_t bit, const obelisk_rt_static_fanout_entry &fanout) {
          if (fanout.bit_width == 0 || fanout.low_bit > bit ||
              bit - fanout.low_bit >= fanout.bit_width)
            return false;
          return fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
                 fanout.low_bit == bit;
        };
    auto clockTouchesFanout =
        [&](const NativePeriodicClock &clock,
            const obelisk_rt_static_fanout_entry &fanout) {
          if (fanout.static_state != clock.staticState)
            return false;
          auto bound =
              llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
                return candidate.handleID == clock.staticState;
              });
          if (bound == stateLayout.bounds.end() ||
              clock.bitOffset < bound->offset ||
              clock.bitOffset - bound->offset >= bound->width)
            return false;
          uint64_t localBit = clock.bitOffset - bound->offset;
          return periodicBitTouchesFanout(localBit, fanout);
        };
    for (auto [clockIndex, clock] : llvm::enumerate(periodicClocks))
      for (auto [fanoutIndex, fanout] : llvm::enumerate(fanoutEntries)) {
        if (fanoutRoute(fanout) == OBELISK_RT_FANOUT_RUNTIME ||
            !clockTouchesFanout(clock, fanout) ||
            fanout.kernel >= clockKernels.size())
          continue;
        if (llvm::any_of(
                periodicAliases, [&](const NativePeriodicAlias &alias) {
                  return alias.sourceStaticState == clock.staticState &&
                         alias.sourceBitOffset == clock.bitOffset &&
                         alias.forwardingActorSlot == fanout.actor_slot &&
                         alias.forwardingContinuation == fanout.continuation;
                }))
          continue;
        uint32_t owner = periodicOwnerBits[fanoutIndex] == UINT32_MAX
                             ? fanout.merged_bit
                             : periodicOwnerBits[fanoutIndex];
        APInt bit = APInt::getOneBitSet(ownerCount, owner);
        auto &masks = canDispatchClockOwnerDirectly(owner)
                          ? clockDirectMasks[clockIndex][fanout.kernel]
                          : clockMasks[clockIndex][fanout.kernel];
        auto &[rising, falling] = masks;
        if (fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_POSEDGE)
          rising |= bit;
        if (fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_NEGEDGE)
          falling |= bit;
      }
    for (const NativePeriodicAlias &alias : periodicAliases) {
      auto clock = llvm::find_if(periodicClocks, [&](const auto &candidate) {
        return candidate.staticState == alias.sourceStaticState &&
               candidate.bitOffset == alias.sourceBitOffset;
      });
      if (clock == periodicClocks.end())
        continue;
      size_t clockIndex = static_cast<size_t>(clock - periodicClocks.begin());
      auto targetBound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == alias.targetStaticState;
          });
      if (targetBound == stateLayout.bounds.end() ||
          alias.targetBitOffset < targetBound->offset ||
          alias.targetBitOffset - targetBound->offset >= targetBound->width)
        continue;
      uint64_t targetLocalBit = alias.targetBitOffset - targetBound->offset;
      for (auto [fanoutIndex, fanout] : llvm::enumerate(fanoutEntries)) {
        if (fanoutRoute(fanout) == OBELISK_RT_FANOUT_RUNTIME ||
            (fanout.actor_slot == alias.forwardingActorSlot &&
             fanout.continuation == alias.forwardingContinuation) ||
            fanout.static_state != alias.targetStaticState ||
            !periodicBitTouchesFanout(targetLocalBit, fanout) ||
            fanout.kernel >= clockKernels.size())
          continue;
        uint32_t owner = periodicOwnerBits[fanoutIndex] == UINT32_MAX
                             ? fanout.merged_bit
                             : periodicOwnerBits[fanoutIndex];
        APInt bit = APInt::getOneBitSet(ownerCount, owner);
        auto &masks = canDispatchClockOwnerDirectly(owner)
                          ? clockDirectMasks[clockIndex][fanout.kernel]
                          : clockMasks[clockIndex][fanout.kernel];
        auto &[rising, falling] = masks;
        if (fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_POSEDGE)
          rising |= bit;
        if (fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_NEGEDGE)
          falling |= bit;
      }
    }

    Value nextEdges =
        entryAlloca(builder, location, i64, periodicClocks.size(), 8);
    Type controlType =
        LLVM::LLVMStructType::getLiteral(context, {pointer, pointer, i64});
    Value control = entryAlloca(builder, location, controlType, 1, 8);
    Value nodes =
        LLVM::AddressOfOp::create(builder, location, pointer, nodesName);
    Value clocks = LLVM::AddressOfOp::create(
        builder, location, pointer, "__obelisk_periodic_clock_plan_v1");
    LLVM::StoreOp::create(
        builder, location, runEntry->getArgument(0),
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  evalCheckpointMutableStateName),
        8);
    Value aliases =
        periodicAliases.empty()
            ? LLVM::ZeroOp::create(builder, location, pointer).getResult()
            : LLVM::AddressOfOp::create(builder, location, pointer,
                                        periodicAliasesName)
                  .getResult();
    Value prepareStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_scheduler_prepare_periodic_aot"),
            ValueRange{
                runEntry->getArgument(1), nodes,
                llvmConstant(builder, location, i32, executableNodes.size()),
                clocks,
                llvmConstant(builder, location, i32, periodicClocks.size()),
                aliases,
                llvmConstant(builder, location, i32, periodicAliases.size()),
                nextEdges, control})
            .getResult();
    Value prepared = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, prepareStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));

    Block *preparedEntry = new Block;
    Block *promote = new Block;
    Block *loop = new Block;
    Block *step = new Block;
    Block *dispatchStep = new Block;
    Block *executeStep = new Block;
    Block *silentFall = nullptr;
    Block *advanceSilentFall = nullptr;
    Block *afterStep = new Block;
    Block *handoff = new Block;
    Block *executeCheckpoint = new Block;
    Block *prepareFailed = new Block;
    Block *prepareUnavailable = new Block;
    Block *runFallbackNodes = new Block;
    Block *executePrepareCheckpoint = new Block;
    Block *returnFromHandoff = new Block;
    Block *failed = new Block;
    run.getBody().push_back(preparedEntry);
    run.getBody().push_back(promote);
    run.getBody().push_back(loop);
    run.getBody().push_back(step);
    run.getBody().push_back(dispatchStep);
    run.getBody().push_back(executeStep);
    APInt directOwnerMask(ownerCount, 0);
    for (const auto &clock : clockDirectMasks)
      for (const auto &[rising, falling] : clock)
        directOwnerMask |= rising | falling;
    SmallVector<unsigned> directOwnerRecords;
    for (auto [index, record] : llvm::enumerate(mergedFragments))
      if (directOwnerMask[record.bit])
        directOwnerRecords.push_back(static_cast<unsigned>(index));
    auto directOwnerNeedsStatusCheck = [&](unsigned recordIndex) {
      auto mayTerminate = [&](StringRef symbol) {
        LLVM::LLVMFuncOp executor =
            symbol.empty() ? LLVM::LLVMFuncOp{}
                           : executorSymbols.lookup<LLVM::LLVMFuncOp>(symbol);
        return executor && executor->hasAttr(sim::metadata::evalMayTerminate);
      };
      return mayTerminate(mergedExecutors[recordIndex]) ||
             mayTerminate(mergedTwoStateExecutors[recordIndex]);
    };
    bool canCompressSilentFall =
        periodicClocks.size() == 1 &&
        llvm::all_of(clockMasks.front(),
                     [](const auto &masks) { return masks.second == 0; }) &&
        llvm::all_of(clockDirectMasks.front(),
                     [](const auto &masks) { return masks.second == 0; }) &&
        !staticFanoutPlan.runtimeTransitionStates.contains(
            periodicClocks.front().staticState) &&
        llvm::none_of(periodicAliases, [&](const NativePeriodicAlias &alias) {
          return staticFanoutPlan.runtimeTransitionStates.contains(
              alias.targetStaticState);
        });
    if (canCompressSilentFall) {
      silentFall = new Block;
      advanceSilentFall = new Block;
      run.getBody().push_back(silentFall);
      run.getBody().push_back(advanceSilentFall);
    }
    run.getBody().push_back(afterStep);
    run.getBody().push_back(handoff);
    run.getBody().push_back(executeCheckpoint);
    run.getBody().push_back(prepareFailed);
    run.getBody().push_back(prepareUnavailable);
    run.getBody().push_back(runFallbackNodes);
    run.getBody().push_back(executePrepareCheckpoint);
    run.getBody().push_back(returnFromHandoff);
    run.getBody().push_back(failed);
    afterStep->addArgument(i32, location);
    handoff->addArgument(i32, location);
    returnFromHandoff->addArgument(i32, location);
    cf::CondBranchOp::create(builder, location, prepared, preparedEntry,
                             ValueRange{}, prepareFailed, ValueRange{});

    builder.setInsertionPointToStart(preparedEntry);
    bool forcedTwoState = module->hasAttr("obelisk.eval.force_two_state");
    if (forcedTwoState)
      LLVM::MemsetOp::create(
          builder, location,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    "__obelisk_state_unknown"),
          llvmConstant(builder, location, builder.getI8Type(), 0),
          llvmConstant(builder, location, i64, (stateLayout.bitCount + 7) / 8),
          /*isVolatile=*/false);
    // Pointer fields have target-dependent offsets (0/4/8 on wasm32,
    // 0/8/16 on x86_64). Use the same struct type as the out-parameter alloca.
    Value preparedTerminationAddress = LLVM::LoadOp::create(
        builder, location, pointer,
        fieldGEP(builder, location, control, controlType, 1), 0);
    // The runtime is not re-entered while this generated transaction runs, so
    // its control addresses and deadline are immutable until handoff. Capture
    // them once outside the hot loop; direct bodies have no runtime escape
    // through which a new timed callback could be installed.
    Value preparedTimeAddress = LLVM::LoadOp::create(
        builder, location, pointer,
        fieldGEP(builder, location, control, controlType, 0), 0);
    Value preparedDeadline = LLVM::LoadOp::create(
        builder, location, i64,
        fieldGEP(builder, location, control, controlType, 2), 8);
    Value terminationSlot = LLVM::AddressOfOp::create(
        builder, location, pointer, periodicTerminationName);
    LLVM::StoreOp::create(builder, location, preparedTerminationAddress,
                          terminationSlot, 8);
    Value preparedTermination = LLVM::LoadOp::create(
        builder, location, i32, preparedTerminationAddress, 4);
    Value alreadyStopping = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, preparedTermination,
        llvmConstant(builder, location, i32, 0));
    Value enterHotLoop = arith::XOrIOp::create(
        builder, location, alreadyStopping,
        llvmConstant(builder, location, builder.getI1Type(), 1));
    cf::CondBranchOp::create(
        builder, location, enterHotLoop, promote, ValueRange{}, handoff,
        ValueRange{
            llvmConstant(builder, location, i32, OBELISK_RT_AOT_CHECKPOINT)});

    builder.setInsertionPointToStart(promote);
    // Synthetic region kernels do not have a source coroutine whose initial
    // activation can run during bootstrap. Seed exactly those combinational
    // owners once; clocked and ordinary always-process bodies were already
    // evaluated by the generic time-zero drain and must not be replayed.
    APInt initialMask(ownerCount, 0);
    for (auto [index, executor] : llvm::enumerate(mergedExecutors)) {
      auto direct = llvm::find_if(directFragments, [&](const auto &candidate) {
        return candidate.wrapper == executor &&
               candidate.actorSlot == mergedFragments[index].actor_slot &&
               candidate.continuation == mergedFragments[index].continuation;
      });
      if (direct != directFragments.end() && direct->initialActivation)
        initialMask.setBit(mergedFragments[index].bit);
    }
    if (initialMask != 0) {
      Value ingress = LLVM::AddressOfOp::create(
          builder, location, pointer, clockKernels.front().ingressName);
      updateOwnerMask(builder, location, ingress, initialMask,
                      /*clear=*/false, {}, &readyLayout);
      // Re-establish combinational quiescence before the first generated
      // clock edge. The runtime cold prefix may end immediately after a
      // clocked reset continuation; deferring these level-sensitive owners
      // until the first edge lets clocked logic sample stale canonical nets.
      Value initialStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, evalDispatchName),
              ValueRange{runEntry->getArgument(0), runEntry->getArgument(1)})
              .getResult();
      Value initialized = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, initialStatus,
          llvmConstant(builder, location, i32, OBELISK_RT_OK));
      cf::CondBranchOp::create(builder, location, initialized, loop,
                               ValueRange{}, failed, ValueRange{initialStatus});
    } else {
      cf::BranchOp::create(builder, location, loop);
    }

    builder.setInsertionPointToStart(loop);
    Value nextTime = LLVM::LoadOp::create(builder, location, i64, nextEdges, 8);
    for (uint32_t index = 1; index != periodicClocks.size(); ++index) {
      Value candidate =
          LLVM::LoadOp::create(builder, location, i64,
                               byteGEP(builder, location, nextEdges,
                                       uint64_t{index} * sizeof(uint64_t)),
                               8);
      Value earlier = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, candidate, nextTime);
      nextTime = arith::SelectOp::create(builder, location, earlier, candidate,
                                         nextTime);
    }
    Value runtimeDue =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ule,
                              preparedDeadline, nextTime);
    step->addArgument(i64, location);
    cf::CondBranchOp::create(
        builder, location, runtimeDue, handoff,
        ValueRange{llvmConstant(builder, location, i32,
                                OBELISK_RT_AOT_TIMED_CHECKPOINT)},
        step, ValueRange{nextTime});

    builder.setInsertionPointToStart(step);
    LLVM::StoreOp::create(builder, location, step->getArgument(0),
                          preparedTimeAddress, 8);
    Value hasIngress = llvmConstant(builder, location, builder.getI1Type(), 0);
    SmallVector<Value> directReady(allOwners.getNumWords(),
                                   llvmConstant(builder, location, i64, 0));
    Value stateValue = LLVM::AddressOfOp::create(builder, location, pointer,
                                                 "__obelisk_state_value");
    auto recordClockCoverage = [&](const NativePeriodicClock &clock,
                                   Value enabled) {
      if (clock.coveragePoints.empty())
        return;
      Value flag = arith::ExtUIOp::create(builder, location, i32, enabled);
      for (uint64_t point : clock.coveragePoints)
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_coverage_point_hit"),
            ValueRange{runEntry->getArgument(1),
                       llvmConstant(builder, location, i64, point), flag});
    };
    auto publishRuntimeClockBit = [&](uint32_t staticState,
                                      uint64_t absoluteBit, Value oldSet,
                                      Value newSet) -> LogicalResult {
      if (!staticFanoutPlan.runtimeTransitionStates.contains(staticState))
        return success();
      auto bound =
          llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
            return candidate.handleID == staticState;
          });
      if (bound == stateLayout.bounds.end() || absoluteBit < bound->offset ||
          absoluteBit - bound->offset >= bound->width)
        return module.emitError(
            "runtime-owned periodic observer has an invalid static root");
      Value oldValue = arith::ExtUIOp::create(builder, location, i64, oldSet);
      Value newValue = arith::ExtUIOp::create(builder, location, i64, newSet);
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context,
                             "obelisk_rt_v1_scheduler_static_transition"),
          ValueRange{
              runEntry->getArgument(1),
              llvmConstant(builder, location, i32, staticState),
              llvmConstant(builder, location, i64, absoluteBit - bound->offset),
              llvmConstant(builder, location, i64, 1), oldValue,
              llvmConstant(builder, location, i64, 0), newValue,
              llvmConstant(builder, location, i64, 0)});
      return success();
    };
    for (auto [clockIndex, clock] : llvm::enumerate(periodicClocks)) {
      Value edgeAddress = byteGEP(builder, location, nextEdges,
                                  uint64_t{clockIndex} * sizeof(uint64_t));
      Value edge = LLVM::LoadOp::create(builder, location, i64, edgeAddress, 8);
      // With one periodic source, nextTime is this source's edge by
      // construction. Materialize that proof as a constant so canonicalize
      // and LLVM can remove the otherwise redundant due/select chain. The
      // general equality-based path remains for coincident multi-clock edges.
      Value due;
      if (periodicClocks.size() == 1)
        due = llvmConstant(builder, location, builder.getI1Type(), 1);
      else
        due = arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                    edge, step->getArgument(0));
      Value noOverflow = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ule, edge,
          llvmConstant(builder, location, i64, UINT64_MAX - clock.halfPeriod));
      Value advancedCandidate = arith::AddIOp::create(
          builder, location, edge,
          llvmConstant(builder, location, i64, clock.halfPeriod));
      Value advanced = arith::SelectOp::create(
          builder, location, noOverflow, advancedCandidate,
          llvmConstant(builder, location, i64, UINT64_MAX));
      LLVM::StoreOp::create(
          builder, location,
          arith::SelectOp::create(builder, location, due, advanced, edge),
          edgeAddress, 8);

      Value byteAddress =
          byteGEP(builder, location, stateValue, clock.bitOffset / 8);
      Value oldByte = LLVM::LoadOp::create(builder, location,
                                           builder.getI8Type(), byteAddress, 1);
      uint8_t bitMask = uint8_t{1} << (clock.bitOffset % 8);
      Value oldSet = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(
              builder, location, oldByte,
              llvmConstant(builder, location, builder.getI8Type(), bitMask)),
          llvmConstant(builder, location, builder.getI8Type(), 0));
      Value toggled = arith::XOrIOp::create(
          builder, location, oldByte,
          llvmConstant(builder, location, builder.getI8Type(), bitMask));
      LLVM::StoreOp::create(
          builder, location,
          arith::SelectOp::create(builder, location, due, toggled, oldByte),
          byteAddress, 1);
      Value newSet = arith::SelectOp::create(
          builder, location, due,
          arith::XOrIOp::create(
              builder, location, oldSet,
              llvmConstant(builder, location, builder.getI1Type(), 1)),
          oldSet);
      recordClockCoverage(clock, due);
      if (mlir::failed(publishRuntimeClockBit(clock.staticState,
                                              clock.bitOffset, oldSet, newSet)))
        return failure();

      for (const NativePeriodicAlias &alias : periodicAliases) {
        if (alias.sourceStaticState != clock.staticState ||
            alias.sourceBitOffset != clock.bitOffset)
          continue;
        // The forwarding driver's canonical plane is not consumed inside the
        // closed generated loop. Keep the resolved net current for model
        // reads, and reconstruct the driver at a runtime/checkpoint handoff.
        // This avoids maintaining a checkpoint-only projection per edge.
        for (uint64_t bitOffset : {alias.targetBitOffset}) {
          Value aliasAddress =
              byteGEP(builder, location, stateValue, bitOffset / 8);
          Value aliasOld = LLVM::LoadOp::create(
              builder, location, builder.getI8Type(), aliasAddress, 1);
          uint8_t aliasMask = uint8_t{1} << (bitOffset % 8);
          Value aliasValue = arith::SelectOp::create(
              builder, location, oldSet,
              arith::AndIOp::create(
                  builder, location, aliasOld,
                  llvmConstant(builder, location, builder.getI8Type(),
                               static_cast<uint8_t>(~aliasMask))),
              arith::OrIOp::create(builder, location, aliasOld,
                                   llvmConstant(builder, location,
                                                builder.getI8Type(),
                                                aliasMask)));
          Value storedAlias = arith::SelectOp::create(builder, location, due,
                                                      aliasValue, aliasOld);
          LLVM::StoreOp::create(builder, location, storedAlias, aliasAddress,
                                1);
          Value aliasOldSet = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne,
              arith::AndIOp::create(builder, location, aliasOld,
                                    llvmConstant(builder, location,
                                                 builder.getI8Type(),
                                                 aliasMask)),
              llvmConstant(builder, location, builder.getI8Type(), 0));
          Value aliasNewSet = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne,
              arith::AndIOp::create(builder, location, storedAlias,
                                    llvmConstant(builder, location,
                                                 builder.getI8Type(),
                                                 aliasMask)),
              llvmConstant(builder, location, builder.getI8Type(), 0));
          if (mlir::failed(publishRuntimeClockBit(alias.targetStaticState,
                                                  bitOffset, aliasOldSet,
                                                  aliasNewSet)))
            return failure();
        }
      }

      for (auto [kernelIndex, kernel] : llvm::enumerate(clockKernels)) {
        const auto &[riseOwners, fallOwners] =
            clockMasks[clockIndex][kernelIndex];
        for (unsigned word = 0; word != allOwners.getNumWords(); ++word) {
          uint64_t riseMask = ownerMaskWord(riseOwners, word);
          uint64_t fallMask = ownerMaskWord(fallOwners, word);
          if (riseMask == 0 && fallMask == 0)
            continue;
          Value edgeMask = arith::SelectOp::create(
              builder, location, oldSet,
              llvmConstant(builder, location, i64, fallMask),
              llvmConstant(builder, location, i64, riseMask));
          Value selected =
              arith::SelectOp::create(builder, location, due, edgeMask,
                                      llvmConstant(builder, location, i64, 0));
          hasIngress = arith::OrIOp::create(
              builder, location, hasIngress,
              arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                    selected,
                                    llvmConstant(builder, location, i64, 0)));
          Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                    kernel.ingressName);
          updateEvalReadyWord(builder, location, ingress, readyLayout, word,
                              selected);
        }
      }
      for (auto [kernelIndex, kernel] : llvm::enumerate(clockKernels)) {
        const auto &[riseOwners, fallOwners] =
            clockDirectMasks[clockIndex][kernelIndex];
        for (unsigned word = 0; word != allOwners.getNumWords(); ++word) {
          uint64_t riseMask = ownerMaskWord(riseOwners, word);
          uint64_t fallMask = ownerMaskWord(fallOwners, word);
          if (riseMask == 0 && fallMask == 0)
            continue;
          Value edgeMask = arith::SelectOp::create(
              builder, location, oldSet,
              llvmConstant(builder, location, i64, fallMask),
              llvmConstant(builder, location, i64, riseMask));
          Value selected =
              arith::SelectOp::create(builder, location, due, edgeMask,
                                      llvmConstant(builder, location, i64, 0));
          directReady[word] = arith::OrIOp::create(builder, location,
                                                   directReady[word], selected);
          // The straight-line prefix can stop at a cold checkpoint before its
          // last owner. Keep unconsumed direct owners in the same-slot ready
          // set so the callback's coordinator can finish that edge. Each owner
          // clears only its own bit after execution below; an SSA-only mask
          // silently drops the suffix when control leaves run_until.
          Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                    kernel.ingressName);
          updateEvalReadyWord(builder, location, ingress, readyLayout, word,
                              selected);
          hasIngress = arith::OrIOp::create(
              builder, location, hasIngress,
              arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                    selected,
                                    llvmConstant(builder, location, i64, 0)));
        }
      }
    }
    // A cold checkpoint can return with the clock high. Its next edge is
    // then a silent falling edge, even though steady execution normally
    // consumes that edge in silentFall. Do not run the constant rising-owner
    // sequence on re-entry until an actual rising edge has produced ingress.
    if (canCompressSilentFall)
      cf::CondBranchOp::create(builder, location, hasIngress, dispatchStep,
                               ValueRange{}, loop, ValueRange{});
    else
      cf::BranchOp::create(builder, location, dispatchStep);

    builder.setInsertionPointToStart(dispatchStep);
    Value stepFourStateFallback = LLVM::AddressOfOp::create(
        builder, location, pointer, evalStepFourStateFallbackName);
    auto resetStepFourStateTracking = [&] {
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI8Type(), 0),
          stepFourStateFallback, 1);
      if (nbaTaintWordCount == 0)
        return;
      Value taint = LLVM::AddressOfOp::create(builder, location, pointer,
                                              evalStepFourStateNBARootsName);
      for (uint32_t word = 0; word != nbaTaintWordCount; ++word)
        LLVM::StoreOp::create(builder, location,
                              llvmConstant(builder, location, i64, 0),
                              byteGEP(builder, location, taint,
                                      uint64_t{word} * sizeof(uint64_t)),
                              8);
    };
    // The compressed single-clock path below has a structurally separate
    // trusted two-state block. Keep transient four-state accounting out of
    // that block entirely; it is initialized only when routing through the
    // hybrid prefix. General coincident-clock dispatch still shares one
    // coordinator entry, so retain its common initialization here.
    if (!canCompressSilentFall || directOwnerRecords.empty())
      resetStepFourStateTracking();
    auto handoffPrioritySignal = [&] {
      if (!prioritySignalHandoff)
        return;
      Block *executeOwner = new Block;
      run.getBody().push_back(executeOwner);
      Value prioritySignalPending =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(
                  context, "obelisk_rt_v1_scheduler_priority_signal_pending"),
              ValueRange{runEntry->getArgument(1)})
              .getResult();
      Value mustHandoff = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, prioritySignalPending,
          llvmConstant(builder, location, i32, 0));
      cf::CondBranchOp::create(
          builder, location, mustHandoff, afterStep,
          ValueRange{
              llvmConstant(builder, location, i32, OBELISK_RT_AOT_CHECKPOINT)},
          executeOwner, ValueRange{});
      builder.setInsertionPointToStart(executeOwner);
    };
    // Clock-edge owners are already quiescent with respect to the preceding
    // slot, so invoke them directly and reserve the cttz coordinator for the
    // publications they produce. A bitset keeps coincident-clock dispatch
    // compact while ensuring a shared owner executes exactly once.
    auto directOwnerConsumedMask = [&](unsigned recordIndex) {
      return APInt::getOneBitSet(ownerCount, mergedFragments[recordIndex].bit) |
             ownerSubsumptionMasks[recordIndex];
    };
    if (canCompressSilentFall && !directOwnerRecords.empty()) {
      // The only generated step is now the rising phase of one proven
      // periodic source. Its direct owner set is a compile-time constant, so
      // outline the fully promoted path as a straight-line instance sequence.
      // The transient prefix selects each instance independently.
      Block *prepareDirectHybrid = new Block;
      Block *executeDirectHybrid = new Block;
      Block *executeDirectTwoState = new Block;
      Block *afterDirectSequence = new Block;
      run.getBody().push_back(prepareDirectHybrid);
      run.getBody().push_back(executeDirectHybrid);
      run.getBody().push_back(executeDirectTwoState);
      run.getBody().push_back(afterDirectSequence);
      APInt directPromotionMask(ownerCount, 0);
      for (unsigned recordIndex : directOwnerRecords)
        if (!mergedTwoStateExecutors[recordIndex].empty())
          directPromotionMask.setBit(mergedFragments[recordIndex].bit);
      // This ingress sequence checks only its participating owner proofs.
      cf::BranchOp::create(builder, location, prepareDirectHybrid);
      builder.setInsertionPointToStart(prepareDirectHybrid);
      resetStepFourStateTracking();
      Value directPending = LLVM::AddressOfOp::create(
          builder, location, pointer, promotionPendingMaskName);
      Value directOwnersPromoted = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq,
          maskedOwnerWords(builder, location, directPending,
                           directPromotionMask & ~pathGuardedOwnerMask),
          llvmConstant(builder, location, i64, 0));
      cf::CondBranchOp::create(builder, location, directOwnersPromoted,
                               executeDirectTwoState, ValueRange{},
                               executeDirectHybrid, ValueRange{});
      auto clearDirectOwner = [&](unsigned recordIndex) {
        // Preserve coordinator fixpoint semantics between owners.  A
        // preceding owner may republish an earlier clock owner; clearing
        // the whole initial mask after the sequence would erase that
        // required retrigger.  Clear only the owner just executed, exactly
        // where the bitset coordinator would consume it.
        APInt consumed = directOwnerConsumedMask(recordIndex);
        for (const NativeEvalClockKernel &kernel : clockKernels) {
          Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                    kernel.ingressName);
          updateOwnerMask(builder, location, ingress, consumed, /*clear=*/true,
                          {}, &readyLayout);
        }
      };
      builder.setInsertionPointToStart(executeDirectTwoState);
      for (unsigned recordIndex : directOwnerRecords) {
        handoffPrioritySignal();
        if (mergedTwoStateExecutors[recordIndex].empty()) {
          LLVM::StoreOp::create(
              builder, location,
              llvmConstant(builder, location, builder.getI8Type(), 1),
              stepFourStateFallback, 1);
          if (ownerMayTaintNBA(recordIndex))
            markOwnerNBATaint(recordIndex);
        }
        StringRef executor =
            mergedTwoStateExecutors[recordIndex].empty()
                ? StringRef(mergedExecutors[recordIndex])
                : StringRef(mergedTwoStateExecutors[recordIndex]);
        Value status =
            LLVM::CallOp::create(builder, location, TypeRange{i32},
                                 SymbolRefAttr::get(context, executor),
                                 ValueRange{runEntry->getArgument(1)})
                .getResult();
        if (!mergedTwoStateExecutors[recordIndex].empty())
          status.getDefiningOp()->setAttr("obelisk.eval.proven_two_state_call",
                                         builder.getUnitAttr());
        clearDirectOwner(recordIndex);
        if (directOwnerNeedsStatusCheck(recordIndex)) {
          Block *nextOwner = new Block;
          run.getBody().push_back(nextOwner);
          Value ok = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, status,
              llvmConstant(builder, location, i32, OBELISK_RT_OK));
          cf::CondBranchOp::create(builder, location, ok, nextOwner,
                                   ValueRange{}, afterStep, ValueRange{status});
          builder.setInsertionPointToStart(nextOwner);
        }
      }
      cf::BranchOp::create(builder, location, afterDirectSequence);

      Block *hybridCursor = executeDirectHybrid;
      for (unsigned recordIndex : directOwnerRecords) {
        // Consecutive infallible owners share this block. Append each call in
        // source order; restarting at the block front would put the eventual
        // branch before earlier calls and leave an invalid CFG.
        builder.setInsertionPointToEnd(hybridCursor);
        handoffPrioritySignal();
        if (promotionKernelReadyNames[recordIndex].empty()) {
          LLVM::StoreOp::create(
              builder, location,
              llvmConstant(builder, location, builder.getI8Type(), 1),
              stepFourStateFallback, 1);
          if (ownerMayTaintNBA(recordIndex))
            markOwnerNBATaint(recordIndex);
          Value status =
              LLVM::CallOp::create(
                  builder, location, TypeRange{i32},
                  SymbolRefAttr::get(context, mergedExecutors[recordIndex]),
                  ValueRange{runEntry->getArgument(1)})
                  .getResult();
          clearDirectOwner(recordIndex);
          if (directOwnerNeedsStatusCheck(recordIndex)) {
            Block *nextOwner = new Block;
            run.getBody().push_back(nextOwner);
            Value ok = arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::eq, status,
                llvmConstant(builder, location, i32, OBELISK_RT_OK));
            cf::CondBranchOp::create(builder, location, ok, nextOwner,
                                     ValueRange{}, afterStep,
                                     ValueRange{status});
            hybridCursor = nextOwner;
          }
          continue;
        }
        Block *executeFourState = new Block;
        Block *executeTwoState = new Block;
        Block *nextOwner = new Block;
        run.getBody().push_back(executeFourState);
        run.getBody().push_back(executeTwoState);
        run.getBody().push_back(nextOwner);
        Value kernelReady =
            LLVM::CallOp::create(
                builder, location, TypeRange{builder.getI1Type()},
                SymbolRefAttr::get(context,
                                   promotionKernelReadyNames[recordIndex]),
                ValueRange{})
                .getResult();
        cf::CondBranchOp::create(builder, location, kernelReady,
                                 executeTwoState, ValueRange{},
                                 executeFourState, ValueRange{});
        builder.setInsertionPointToStart(executeFourState);
        LLVM::StoreOp::create(
            builder, location,
            llvmConstant(builder, location, builder.getI8Type(), 1),
            stepFourStateFallback, 1);
        if (ownerMayTaintNBA(recordIndex))
          markOwnerNBATaint(recordIndex);
        Value fourStateStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(context, mergedExecutors[recordIndex]),
                ValueRange{runEntry->getArgument(1)})
                .getResult();
        clearDirectOwner(recordIndex);
        if (directOwnerNeedsStatusCheck(recordIndex)) {
          Value ok = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, fourStateStatus,
              llvmConstant(builder, location, i32, OBELISK_RT_OK));
          cf::CondBranchOp::create(builder, location, ok, nextOwner,
                                   ValueRange{}, afterStep,
                                   ValueRange{fourStateStatus});
        } else {
          cf::BranchOp::create(builder, location, nextOwner);
        }
        builder.setInsertionPointToStart(executeTwoState);
        Value twoStateStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(context,
                                   mergedTwoStateExecutors[recordIndex]),
                ValueRange{runEntry->getArgument(1)})
                .getResult();
        twoStateStatus.getDefiningOp()->setAttr(
            "obelisk.eval.proven_two_state_call", builder.getUnitAttr());
        clearDirectOwner(recordIndex);
        if (directOwnerNeedsStatusCheck(recordIndex)) {
          Value ok = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, twoStateStatus,
              llvmConstant(builder, location, i32, OBELISK_RT_OK));
          cf::CondBranchOp::create(builder, location, ok, nextOwner,
                                   ValueRange{}, afterStep,
                                   ValueRange{twoStateStatus});
        } else {
          cf::BranchOp::create(builder, location, nextOwner);
        }
        hybridCursor = nextOwner;
      }
      builder.setInsertionPointToEnd(hybridCursor);
      cf::BranchOp::create(builder, location, afterDirectSequence);
      builder.setInsertionPointToStart(afterDirectSequence);
    }
    if (!canCompressSilentFall)
      for (unsigned recordIndex : directOwnerRecords) {
        handoffPrioritySignal();
        const auto &record = mergedFragments[recordIndex];
        Value selected = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne,
            arith::AndIOp::create(
                builder, location, directReady[record.bit / 64],
                llvmConstant(builder, location, i64,
                             uint64_t{1} << (record.bit % 64))),
            llvmConstant(builder, location, i64, 0));
        Block *executeDirect = new Block;
        Block *nextDirect = new Block;
        run.getBody().push_back(executeDirect);
        run.getBody().push_back(nextDirect);
        cf::CondBranchOp::create(builder, location, selected, executeDirect,
                                 ValueRange{}, nextDirect, ValueRange{});
        builder.setInsertionPointToStart(executeDirect);
        StringRef fourState = mergedExecutors[recordIndex];
        StringRef twoState = mergedTwoStateExecutors[recordIndex];
        if (!twoState.empty() && twoState != fourState) {
          Block *selectTransientVariant = new Block;
          Block *executeFourState = new Block;
          Block *executeTwoState = new Block;
          Block *afterExecute = new Block;
          run.getBody().push_back(selectTransientVariant);
          run.getBody().push_back(executeFourState);
          run.getBody().push_back(executeTwoState);
          run.getBody().push_back(afterExecute);
          // A promoted multi-clock closure has the same monotonic proof as
          // the single-clock direct prefix. Bypass the per-owner scanner once
          // that proof is latched; only transient slots consult local closure
          // readiness.
          cf::BranchOp::create(builder, location, selectTransientVariant);
          builder.setInsertionPointToStart(selectTransientVariant);
          Value kernelReady =
              LLVM::CallOp::create(
                  builder, location, TypeRange{builder.getI1Type()},
                  SymbolRefAttr::get(context,
                                     promotionKernelReadyNames[recordIndex]),
                  ValueRange{})
                  .getResult();
          cf::CondBranchOp::create(builder, location, kernelReady,
                                   executeTwoState, ValueRange{},
                                   executeFourState, ValueRange{});
          builder.setInsertionPointToStart(executeFourState);
          LLVM::StoreOp::create(
              builder, location,
              llvmConstant(builder, location, builder.getI8Type(), 1),
              stepFourStateFallback, 1);
          if (ownerMayTaintNBA(recordIndex))
            markOwnerNBATaint(recordIndex);
          Value fourStateStatus =
              LLVM::CallOp::create(builder, location, TypeRange{i32},
                                   SymbolRefAttr::get(context, fourState),
                                   ValueRange{runEntry->getArgument(1)})
                  .getResult();
          Value fourStateOK = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, fourStateStatus,
              llvmConstant(builder, location, i32, OBELISK_RT_OK));
          cf::CondBranchOp::create(builder, location, fourStateOK, afterExecute,
                                   ValueRange{}, afterStep,
                                   ValueRange{fourStateStatus});
          builder.setInsertionPointToStart(executeTwoState);
          Value twoStateStatus =
              LLVM::CallOp::create(builder, location, TypeRange{i32},
                                   SymbolRefAttr::get(context, twoState),
                                   ValueRange{runEntry->getArgument(1)})
                  .getResult();
          twoStateStatus.getDefiningOp()->setAttr(
              "obelisk.eval.proven_two_state_call", builder.getUnitAttr());
          Value twoStateOK = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, twoStateStatus,
              llvmConstant(builder, location, i32, OBELISK_RT_OK));
          cf::CondBranchOp::create(builder, location, twoStateOK, afterExecute,
                                   ValueRange{}, afterStep,
                                   ValueRange{twoStateStatus});
          builder.setInsertionPointToStart(afterExecute);
        } else {
          LLVM::StoreOp::create(
              builder, location,
              llvmConstant(builder, location, builder.getI8Type(), 1),
              stepFourStateFallback, 1);
          if (ownerMayTaintNBA(recordIndex))
            markOwnerNBATaint(recordIndex);
          Value directStatus =
              LLVM::CallOp::create(builder, location, TypeRange{i32},
                                   SymbolRefAttr::get(context, fourState),
                                   ValueRange{runEntry->getArgument(1)})
                  .getResult();
          Value directOK = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq, directStatus,
              llvmConstant(builder, location, i32, OBELISK_RT_OK));
          Block *afterExecute = new Block;
          run.getBody().push_back(afterExecute);
          cf::CondBranchOp::create(builder, location, directOK, afterExecute,
                                   ValueRange{}, afterStep,
                                   ValueRange{directStatus});
          builder.setInsertionPointToStart(afterExecute);
        }
        // Match coordinator execution semantics: neither the coordinator nor
        // an exact member already executed inside it may remain pending. An
        // implicit combinational sensitivity cannot retrigger its currently
        // executing logical owner, and fused bodies commonly publish one of
        // their own roots.
        APInt consumed = directOwnerConsumedMask(recordIndex);
        for (const NativeEvalClockKernel &kernel : clockKernels) {
          Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                    kernel.ingressName);
          updateOwnerMask(builder, location, ingress, consumed, /*clear=*/true,
                          {}, &readyLayout);
        }
        cf::BranchOp::create(builder, location, nextDirect);
        builder.setInsertionPointToStart(nextDirect);
      }
    Block *completeStep = new Block;
    completeStep->addArgument(i32, location);
    run.getBody().push_back(completeStep);
    cf::CondBranchOp::create(
        builder, location, hasIngress, executeStep, ValueRange{}, completeStep,
        ValueRange{llvmConstant(builder, location, i32, OBELISK_RT_OK)});

    builder.setInsertionPointToStart(executeStep);
    // One dispatcher owns Active/NBA iteration. Value-domain selection is
    // local to the selected executor, never another model-wide controller.
    Value stepStatus = LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context, evalDispatchName),
        ValueRange{runEntry->getArgument(0), runEntry->getArgument(1)})
        .getResult();
    cf::BranchOp::create(builder, location, completeStep, ValueRange{stepStatus});

    builder.setInsertionPointToStart(completeStep);
    Value completedStatus = completeStep->getArgument(0);
    Value stepOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, completedStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    Value handoffPending =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_scheduler_handoff_pending"),
            ValueRange{runEntry->getArgument(1)})
            .getResult();
    Value noRuntimeHandoff = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, handoffPending,
        llvmConstant(builder, location, i32, 0));
    Value continueGenerated =
        arith::AndIOp::create(builder, location, stepOK, noRuntimeHandoff);
    Value runtimeHandoffStatus = arith::SelectOp::create(
        builder, location, stepOK,
        llvmConstant(builder, location, i32, OBELISK_RT_TIER_UNAVAILABLE),
        completedStatus);
    cf::CondBranchOp::create(builder, location, continueGenerated,
                             canCompressSilentFall ? silentFall : loop,
                             ValueRange{}, afterStep,
                             ValueRange{runtimeHandoffStatus});

    if (canCompressSilentFall) {
      // A falling phase with no physical fanout is not an event region. It may
      // be compressed into the generated run_until loop provided no runtime
      // deadline occurs at or before that edge.
      builder.setInsertionPointToStart(silentFall);
      Value fallTime =
          LLVM::LoadOp::create(builder, location, i64, nextEdges, 8);
      Value runtimeAtFall =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ule,
                                preparedDeadline, fallTime);
      cf::CondBranchOp::create(
          builder, location, runtimeAtFall, handoff,
          ValueRange{llvmConstant(builder, location, i32,
                                  OBELISK_RT_AOT_TIMED_CHECKPOINT)},
          advanceSilentFall, ValueRange{});

      builder.setInsertionPointToStart(advanceSilentFall);
      const NativePeriodicClock &clock = periodicClocks.front();
      recordClockCoverage(
          clock, llvmConstant(builder, location, builder.getI1Type(), 1));
      Value sourceAddress =
          byteGEP(builder, location, stateValue, clock.bitOffset / 8);
      Value source = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(), sourceAddress, 1);
      uint8_t sourceMask = uint8_t{1} << (clock.bitOffset % 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::AndIOp::create(
              builder, location, source,
              llvmConstant(builder, location, builder.getI8Type(),
                           static_cast<uint8_t>(~sourceMask))),
          sourceAddress, 1);
      for (const NativePeriodicAlias &alias : periodicAliases) {
        if (alias.sourceStaticState != clock.staticState ||
            alias.sourceBitOffset != clock.bitOffset)
          continue;
        for (uint64_t bitOffset : {alias.targetBitOffset}) {
          Value address = byteGEP(builder, location, stateValue, bitOffset / 8);
          Value old = LLVM::LoadOp::create(builder, location,
                                           builder.getI8Type(), address, 1);
          uint8_t mask = uint8_t{1} << (bitOffset % 8);
          LLVM::StoreOp::create(
              builder, location,
              arith::AndIOp::create(builder, location, old,
                                    llvmConstant(builder, location,
                                                 builder.getI8Type(),
                                                 static_cast<uint8_t>(~mask))),
              address, 1);
        }
      }
      Value noOverflow = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ule, fallTime,
          llvmConstant(builder, location, i64, UINT64_MAX - clock.halfPeriod));
      Value nextRiseCandidate = arith::AddIOp::create(
          builder, location, fallTime,
          llvmConstant(builder, location, i64, clock.halfPeriod));
      Value nextRise = arith::SelectOp::create(
          builder, location, noOverflow, nextRiseCandidate,
          llvmConstant(builder, location, i64, UINT64_MAX));
      LLVM::StoreOp::create(builder, location, nextRise, nextEdges, 8);
      cf::BranchOp::create(builder, location, loop);
    }

    builder.setInsertionPointToStart(afterStep);
    // prepare_periodic_aot detaches the runtime clock deadlines.  Once that
    // succeeds every generated exit, including TierUnavailable and fatal body
    // statuses, must restore them before returning to the runtime.
    cf::BranchOp::create(builder, location, handoff,
                         ValueRange{afterStep->getArgument(0)});

    builder.setInsertionPointToStart(handoff);
    // Direct periodic forwarding elides checkpoint-only driver writes in the
    // hot loop. Reconstruct both canonical planes before runtime callbacks or
    // asynchronous Tier-2 intervention can observe them.
    Value stateUnknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_state_unknown");
    Value handoffState = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_state_value");
    for (const NativePeriodicAlias &alias : periodicAliases) {
      auto source = llvm::find_if(periodicClocks, [&](const auto &clock) {
        return clock.staticState == alias.sourceStaticState &&
               clock.bitOffset == alias.sourceBitOffset;
      });
      if (source == periodicClocks.end())
        continue;
      Value sourceAddress =
          byteGEP(builder, location, handoffState, source->bitOffset / 8);
      Value sourceByte = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(), sourceAddress, 1);
      uint8_t sourceMask = uint8_t{1} << (source->bitOffset % 8);
      Value sourceSet = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(
              builder, location, sourceByte,
              llvmConstant(builder, location, builder.getI8Type(), sourceMask)),
          llvmConstant(builder, location, builder.getI8Type(), 0));
      Value driverAddress =
          byteGEP(builder, location, handoffState, alias.driverBitOffset / 8);
      Value driverByte = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(), driverAddress, 1);
      uint8_t driverMask = uint8_t{1} << (alias.driverBitOffset % 8);
      Value driverValue = arith::SelectOp::create(
          builder, location, sourceSet,
          arith::OrIOp::create(
              builder, location, driverByte,
              llvmConstant(builder, location, builder.getI8Type(), driverMask)),
          arith::AndIOp::create(
              builder, location, driverByte,
              llvmConstant(builder, location, builder.getI8Type(),
                           static_cast<uint8_t>(~driverMask))));
      LLVM::StoreOp::create(builder, location, driverValue, driverAddress, 1);
      Value driverUnknownAddress =
          byteGEP(builder, location, stateUnknown, alias.driverBitOffset / 8);
      Value driverUnknown = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(), driverUnknownAddress, 1);
      LLVM::StoreOp::create(
          builder, location,
          arith::AndIOp::create(
              builder, location, driverUnknown,
              llvmConstant(builder, location, builder.getI8Type(),
                           static_cast<uint8_t>(~driverMask))),
          driverUnknownAddress, 1);
    }
    Value handoffStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_scheduler_handoff_periodic_aot"),
            ValueRange{
                runEntry->getArgument(1), clocks,
                llvmConstant(builder, location, i32, periodicClocks.size()),
                nextEdges})
            .getResult();
    Value handoffOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, handoffStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    Value isCheckpoint = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, handoff->getArgument(0),
        llvmConstant(builder, location, i32,
                     OBELISK_RT_AOT_GENERATED_CHECKPOINT));
    Value runCheckpoint =
        arith::AndIOp::create(builder, location, handoffOK, isCheckpoint);
    Value ordinaryStatus = arith::SelectOp::create(
        builder, location, handoffOK, handoff->getArgument(0), handoffStatus);
    cf::CondBranchOp::create(builder, location, runCheckpoint,
                             executeCheckpoint, ValueRange{}, returnFromHandoff,
                             ValueRange{ordinaryStatus});

    builder.setInsertionPointToStart(executeCheckpoint);
    Value checkpointActor = LLVM::LoadOp::create(
        builder, location, i32,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  evalCheckpointActorName),
        4);
    Value checkpointContinuation = LLVM::LoadOp::create(
        builder, location, i32,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  evalCheckpointContinuationName),
        4);
    Value checkpointCallback = LLVM::LoadOp::create(
        builder, location, pointer,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  evalCheckpointCallbackName),
        8);
    Value checkpointStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_scheduler_queue_aot_checkpoint"),
            ValueRange{runEntry->getArgument(1), checkpointActor,
                       checkpointContinuation, checkpointCallback})
            .getResult();
    Value checkpointOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, checkpointStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    Value checkpointReturn = arith::SelectOp::create(
        builder, location, checkpointOK,
        llvmConstant(builder, location, i32, OBELISK_RT_AOT_CHECKPOINT),
        checkpointStatus);
    cf::BranchOp::create(builder, location, returnFromHandoff,
                         ValueRange{checkpointReturn});

    // A generated evaluator may discover a cold checkpoint while
    // prepare_periodic_aot is draining the finite Tier-3/bootstrap prefix.
    // Periodic deadlines have not been detached in that case, so there is no
    // handoff to restore. Publish the exact actor continuation and let the
    // outer scheduler drain it once through the normal checkpoint path.
    builder.setInsertionPointToStart(prepareFailed);
    Value prepareIsCheckpoint = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, prepareStatus,
        llvmConstant(builder, location, i32,
                     OBELISK_RT_AOT_GENERATED_CHECKPOINT));
    cf::CondBranchOp::create(builder, location, prepareIsCheckpoint,
                             executePrepareCheckpoint, ValueRange{},
                             prepareUnavailable, ValueRange{});

    // An open waveform or persistent runtime clock consumer prevents whole
    // clock-group ownership, not native fragment execution. Preparation has
    // not detached periodic deadlines on failure; retain the installed plan
    // and its canonical state while the Tier-2 node loop owns the calendar.
    builder.setInsertionPointToStart(prepareUnavailable);
    Value unavailable = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, prepareStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_TIER_UNAVAILABLE));
    cf::CondBranchOp::create(builder, location, unavailable, runFallbackNodes,
                             ValueRange{}, failed, ValueRange{prepareStatus});
    builder.setInsertionPointToStart(runFallbackNodes);
    Value fallbackStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_scheduler_run_aot_nodes"),
            ValueRange{
                runEntry->getArgument(1), nodes,
                llvmConstant(builder, location, i32, executableNodes.size())})
            .getResult();
    LLVM::ReturnOp::create(builder, location, fallbackStatus);

    builder.setInsertionPointToStart(executePrepareCheckpoint);
    Value prepareCheckpointActor = LLVM::LoadOp::create(
        builder, location, i32,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  evalCheckpointActorName),
        4);
    Value prepareCheckpointContinuation = LLVM::LoadOp::create(
        builder, location, i32,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  evalCheckpointContinuationName),
        4);
    Value prepareCheckpointCallback = LLVM::LoadOp::create(
        builder, location, pointer,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  evalCheckpointCallbackName),
        8);
    Value prepareCheckpointStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_scheduler_queue_aot_checkpoint"),
            ValueRange{runEntry->getArgument(1), prepareCheckpointActor,
                       prepareCheckpointContinuation,
                       prepareCheckpointCallback})
            .getResult();
    Value prepareCheckpointOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, prepareCheckpointStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    Value prepareCheckpointReturn = arith::SelectOp::create(
        builder, location, prepareCheckpointOK,
        llvmConstant(builder, location, i32, OBELISK_RT_AOT_CHECKPOINT),
        prepareCheckpointStatus);
    LLVM::ReturnOp::create(builder, location, prepareCheckpointReturn);

    builder.setInsertionPointToStart(returnFromHandoff);
    LLVM::ReturnOp::create(builder, location,
                           returnFromHandoff->getArgument(0));

    failed->addArgument(i32, location);
    builder.setInsertionPointToStart(failed);
    LLVM::ReturnOp::create(builder, location, failed->getArgument(0));
  }

  // Keep the exceptional unknown-NBA drain out of coordinators that are
  // intentionally inlined into run_until. Otherwise each promoted variant
  // duplicates the complete four-state coordinator on a cold edge and
  // needlessly expands the instruction working set of the periodic loop.
  constexpr StringLiteral evalFourStateNBAHandoffName =
      "__obelisk_eval_four_state_nba_handoff_v1";

  SmallVector<std::string> dynamicNBAValidNames;
  for (const DynamicEvalNBA &entry : dynamicEvalNBAs)
    if (!entry.queued)
      dynamicNBAValidNames.push_back(entry.validName);
  NativeEvalCoordinatorPlan coordinatorPlan{clockKernels,
                                            mergedFragments,
                                            mergedExecutors,
                                            mergedTwoStateExecutors,
                                            promotionKernelReadyNames,
                                            ownerSubsumptionMasks,
                                            resolved->rankedNodes,
                                            recordNBATaintMasks,
                                            nbaTaintedRecords,
                                            nbaTaintWordCount,
                                            prioritySignalHandoff,
                                            dynamicNBAValidNames,
                                            hasOrderedNBA};
  auto rankedGroups = materializeNativeRankedGroups(module, coordinatorPlan);
  if (failed(rankedGroups))
    return failure();
  coordinatorPlan.rankedGroupExecutors = *rankedGroups;
  if (failed(materializeNativeEvalDispatch(module, coordinatorPlan)))
    return failure();

  // Cold four-state commit returns to the same region dispatcher. It must
  // not recursively drain post-NBA work in another controller activation.
  builder.setInsertionPointToEnd(module.getBody());
  auto evalFourStateNBAHandoff = LLVM::LLVMFuncOp::create(
      builder, location, evalFourStateNBAHandoffName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, pointer}, false));
  evalFourStateNBAHandoff->setAttr(
      "passthrough", builder.getArrayAttr({builder.getStringAttr("noinline"),
                                           builder.getStringAttr("cold")}));
  Block *handoffEntry = evalFourStateNBAHandoff.addEntryBlock(builder);
  builder.setInsertionPointToStart(handoffEntry);
  if (nbaTaintWordCount != 0) {
    Value taintBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                evalStepFourStateNBARootsName);
    Value fastRootsBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                    evalFastNBARootsName);
    for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
      Value taint =
          LLVM::LoadOp::create(builder, location, i64,
                               byteGEP(builder, location, taintBase,
                                       uint64_t{word} * sizeof(uint64_t)),
                               8);
      Value fastAddress = byteGEP(builder, location, fastRootsBase,
                                  uint64_t{word} * sizeof(uint64_t));
      Value fastRoots =
          LLVM::LoadOp::create(builder, location, i64, fastAddress, 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::AndIOp::create(
              builder, location, fastRoots,
              arith::XOrIOp::create(
                  builder, location, taint,
                  llvmConstant(builder, location, i64, UINT64_MAX))),
          fastAddress, 8);
    }
  }
  auto fourStateCall = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, nbaCommitName),
      ValueRange{handoffEntry->getArgument(0), handoffEntry->getArgument(1),
                 llvmConstant(builder, location, i32, 2),
                 handoffEntry->getArgument(2)});
  fourStateCall->setAttr("obelisk.eval.keep_four_state_nba",
                         builder.getUnitAttr());
  LLVM::ReturnOp::create(builder, location, fourStateCall.getResult());

  builder.setInsertionPointToEnd(module.getBody());
  auto snapshot = LLVM::LLVMFuncOp::create(
      builder, location, snapshotName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, pointer}, false));
  Block *snapshotEntry = snapshot.addEntryBlock(builder);
  builder.setInsertionPointToStart(snapshotEntry);
  Value snapshotStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_snapshot_aot"),
          ValueRange{snapshotEntry->getArgument(1),
                     snapshotEntry->getArgument(2)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, snapshotStatus);

  builder.setInsertionPointToEnd(module.getBody());
  auto nbaCommit = LLVM::LLVMFuncOp::create(
      builder, location, nbaCommitName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  nbaCommit->setAttr(
      "passthrough",
      builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
  Block *nbaCommitEntry = nbaCommit.addEntryBlock(builder);
  Block *genericNBACommit = new Block;
  nbaCommit.getBody().push_back(genericNBACommit);
  builder.setInsertionPointToStart(nbaCommitEntry);

  // A generated eval plan enters only at a clean boundary. Its private NBA
  // barrier may commit directly even when canonical actors remain guarded
  // for writable VPI; runtime handoffs use runtimeNBACommit below.
  bool generateScalarCommits =
      cleanSuperstepEnabled && enableDirectState &&
      (!guardedSpecializationEnabled || generatedEvalPlan) &&
      staticNBAPlan.generatedOffsets.size() == nbaRoots.size();
  SmallVector<SmallVector<uint32_t>> scalarRootsByWord(nbaDirtyWordCount);
  uint64_t planeBytes = (stateLayout.bitCount + 7) / 8 + sizeof(uint64_t);
  if (generateScalarCommits)
    for (auto [rootIndex, root, accumulator, offset] :
         llvm::enumerate(nbaRoots, staticNBAPlan.generatedAccumulators,
                         staticNBAPlan.generatedOffsets)) {
      uint64_t firstByte = offset / 8;
      uint64_t shift = offset % 8;
      bool crossesWord = root.bit_width > 64 - shift;
      bool addressable = root.bit_width != 0 && root.bit_width <= 64 &&
                         !accumulator.empty() && firstByte + 8 <= planeBytes &&
                         (!crossesWord || firstByte + 9 <= planeBytes);
      if (addressable)
        scalarRootsByWord[rootIndex / 64].push_back(
            static_cast<uint32_t>(rootIndex));
    }
  if (nbaTaintWordCount != 0) {
    // Only fixed roots consumed by the value-only barrier have a persistent
    // destination certificate. Dynamic destinations retain canonical unknown
    // stores; their selected lanes are not certified by this root bitmap.
    SmallVector<Attribute> rootDependencies;
    for (ArrayRef<uint32_t> roots : scalarRootsByWord)
      for (uint32_t index : roots)
        rootDependencies.push_back(builder.getDictionaryAttr(
            {builder.getNamedAttr("bit", builder.getI64IntegerAttr(index)),
             builder.getNamedAttr(
                 "ranges",
                 builder.getDenseI64ArrayAttr(
                     {static_cast<int64_t>(
                          staticNBAPlan.generatedOffsets[index]),
                      static_cast<int64_t>(nbaRoots[index].bit_width)}))}));
    module.lookupSymbol<LLVM::GlobalOp>(evalFastNBARootsName)
        ->setAttr("obelisk.eval.nba_proof_dependencies",
                  builder.getArrayAttr(rootDependencies));
  }
  bool generateGroupedFanout =
      llvm::any_of(scalarRootsByWord, [&](ArrayRef<uint32_t> roots) {
        return llvm::any_of(roots, [&](uint32_t rootIndex) {
          return llvm::any_of(
              fanoutEntries, [&](const obelisk_rt_static_fanout_entry &entry) {
                return entry.static_state == nbaRoots[rootIndex].static_state;
              });
        });
      });
  uint32_t activationWordCount =
      generateGroupedFanout
          ? static_cast<uint32_t>((executableNodes.size() + 63) / 64)
          : 0;
  // Eval mode owns NBA fanout as well as active-region fanout.  The generated
  // commit epilogue translates changed roots straight into model-method bits
  // and immediately drains the generated fixpoint; it never reconstructs a
  // runtime ready-node worklist.
  uint32_t directActivationWordCount =
      static_cast<uint32_t>((mergedFragments.size() + 63) / 64);
  Value activatedNodes;
  Value activatedDirect;
  if (generateGroupedFanout) {
    activatedNodes =
        entryAlloca(builder, location, i64, activationWordCount, 8);
    for (uint32_t word = 0; word != activationWordCount; ++word)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, activatedNodes,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
  }
  // Direct eval publication is independent of whether any generic grouped
  // fanout exists.  In particular, an eval-only design can have no
  // `activatedNodes` bitmap while still publishing fixed NBA roots to direct
  // fragments.  Keep this alloca governed by its own word count so the later
  // direct epilogue never observes an unset MLIR Value.
  if (directActivationWordCount != 0) {
    activatedDirect =
        entryAlloca(builder, location, i64, directActivationWordCount, 8);
    for (uint32_t word = 0; word != directActivationWordCount; ++word)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, activatedDirect,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
  }

  Value stateValue = LLVM::AddressOfOp::create(builder, location, pointer,
                                               "__obelisk_state_value");
  Value stateUnknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                                 "__obelisk_state_unknown");
  // Build the source barrier with the complete four-state semantics.  The
  // canonical and value-only two-state variants are derived from this body
  // later, after every dynamic and fixed-root unknown-plane access exists.
  // Dropping dynamic unknown stores here leaves the canonical handover unable
  // to clear a register element that became known during promoted execution.
  constexpr bool forcedTwoStateEval = false;
  // Drain records in staging order. No actor executes within the NBA barrier,
  // so the allocation and payload stay stable until the drain is complete.
  Block *queueAdvance = nullptr;
  Value queueOffset, queueValue, queueUnknown;
  llvm::DenseMap<uint64_t, Block *> queueCases;
  if (hasOrderedNBA) {
    auto block = [&]() {
      auto *result = new Block;
      nbaCommit.getBody().getBlocks().insert(Region::iterator(genericNBACommit),
                                             result);
      return result;
    };
    Block *head = block(), *select = block(), *done = block(),
          *invalid = block(), *failed = block();
    queueAdvance = block();
    head->addArgument(i32, location);
    // A failed reserve dropped an update. Publishing the rest would expose a
    // sequence the source never executed; report the latched status instead.
    Value error = LLVM::LoadOp::create(builder, location, i32,
                                       evalNBAQueueField(builder, location, 3),
                                       4);
    Value clean =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              error,
                              llvmConstant(builder, location, i32,
                                           OBELISK_RT_OK));
    cf::CondBranchOp::create(
        builder, location, clean, head,
        ValueRange{llvmConstant(builder, location, i32, 0)}, failed,
        ValueRange{});
    builder.setInsertionPointToStart(failed);
    LLVM::ReturnOp::create(builder, location, error);
    builder.setInsertionPointToStart(head);
    Value count = evalNBAQueueSize(builder, location);
    Value pending =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                              head->getArgument(0), count);
    cf::CondBranchOp::create(builder, location, pending, select, ValueRange{},
                             done, ValueRange{});
    builder.setInsertionPointToStart(select);
    Value data = LLVM::LoadOp::create(builder, location, pointer,
                                      evalNBAQueueField(builder, location, 0));
    Value index =
        arith::ExtUIOp::create(builder, location, i64, head->getArgument(0));
    Value record = LLVM::GEPOp::create(builder, location, pointer,
                                       LLVM::LLVMArrayType::get(i64, 4), data,
                                       ValueRange{index});
    auto field = [&](unsigned offset) -> Value {
      // A two-state route may still enqueue an explicit X/Z literal. Keep
      // each record's unknown bits in every commit clone; only the compact
      // accumulator's unknown load is proven zero by promotion.
      return LLVM::LoadOp::create(builder, location, i64,
                                  byteGEP(builder, location, record, offset),
                                  8);
    };
    Value site = field(0);
    queueOffset = field(8);
    queueValue = field(16);
    queueUnknown = field(24);
    SmallVector<APInt> values;
    SmallVector<Block *> destinations;
    SmallVector<ValueRange> operands;
    for (const DynamicEvalNBA &entry : dynamicEvalNBAs)
      if (entry.queued) {
        Block *destination = block();
        queueCases[entry.site] = destination;
        values.emplace_back(64, entry.site);
        destinations.push_back(destination);
        operands.push_back(ValueRange{});
      }
    LLVM::SwitchOp::create(builder, location, site, invalid, ValueRange{},
                           values, destinations, operands);
    builder.setInsertionPointToStart(invalid);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, i32, OBELISK_RT_INVALID_ARGUMENT));
    builder.setInsertionPointToStart(queueAdvance);
    Value next = arith::AddIOp::create(builder, location, head->getArgument(0),
                                       llvmConstant(builder, location, i32, 1));
    cf::BranchOp::create(builder, location, head, ValueRange{next});
    builder.setInsertionPointToStart(done);
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32, 0),
                          evalNBAQueueField(builder, location, 1), 4);
  }
  for (const DynamicEvalNBA &entry : dynamicEvalNBAs) {
    auto legacyIP = builder.saveInsertionPoint();
    Value validAddress, active;
    Block *nextDynamic;
    if (entry.queued) {
      builder.setInsertionPointToStart(queueCases.lookup(entry.site));
      active = llvmConstant(builder, location, builder.getI1Type(), 1);
      nextDynamic = queueAdvance;
    } else {
      validAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                               entry.validName);
      Value valid =
          LLVM::LoadOp::create(builder, location, i32, validAddress, 4);
      active =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                valid, llvmConstant(builder, location, i32, 0));
      // An empty latch must not read its stale payload.
      Block *commitDynamic = new Block;
      nextDynamic = new Block;
      nbaCommit.getBody().getBlocks().insert(Region::iterator(genericNBACommit),
                                             commitDynamic);
      nbaCommit.getBody().getBlocks().insert(Region::iterator(genericNBACommit),
                                             nextDynamic);
      cf::CondBranchOp::create(builder, location, active, commitDynamic,
                               ValueRange{}, nextDynamic, ValueRange{});
      builder.setInsertionPointToStart(commitDynamic);
    }
    auto loadStaged = [&](StringRef name) -> Value {
      if (entry.queued)
        return name == entry.offsetName  ? queueOffset
               : name == entry.valueName ? queueValue
                                         : queueUnknown;
      return LLVM::LoadOp::create(
          builder, location, i64,
          LLVM::AddressOfOp::create(builder, location, pointer, name), 8);
    };
    Value dynamicBit = loadStaged(entry.offsetName);
    uint64_t rootWidth = staticNBAPlan.roots[entry.rootIndex].bit_width;
    auto publicationRange = builder.getDenseI64ArrayAttr(
        {static_cast<int64_t>(staticNBAPlan.generatedOffsets[entry.rootIndex]),
         static_cast<int64_t>(rootWidth)});
    Value zero64 = llvmConstant(builder, location, i64, 0);
    Value rootWidthValue = llvmConstant(builder, location, i64, rootWidth);
    Value lowerBound = llvmConstant(builder, location, i64,
                                    -static_cast<int64_t>(entry.width));
    Value belowEnd =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::slt,
                              dynamicBit, rootWidthValue);
    Value aboveBegin = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sgt, dynamicBit, lowerBound);
    Value overlaps =
        arith::AndIOp::create(builder, location, belowEnd, aboveBegin);
    Value safeRequestedStart = arith::SelectOp::create(
        builder, location, overlaps, dynamicBit, zero64);
    Value maximumOffset =
        llvmConstant(builder, location, i64, rootWidth - entry.width);
    Value below =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::slt,
                              safeRequestedStart, zero64);
    Value above =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::sgt,
                              safeRequestedStart, maximumOffset);
    Value clampedLow = arith::SelectOp::create(builder, location, below, zero64,
                                               safeRequestedStart);
    Value safeDynamicBit = arith::SelectOp::create(builder, location, above,
                                                   maximumOffset, clampedLow);
    Value lowClip = arith::SelectOp::create(
        builder, location, below,
        arith::SubIOp::create(builder, location, zero64, safeRequestedStart),
        zero64);
    Value highClip = arith::SelectOp::create(
        builder, location, above,
        arith::SubIOp::create(builder, location, safeRequestedStart,
                              maximumOffset),
        zero64);
    Value fullyContained = arith::AndIOp::create(
        builder, location,
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              lowClip, zero64),
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              highClip, zero64));
    Value dynamicByte =
        arith::ShRUIOp::create(builder, location, safeDynamicBit,
                               llvmConstant(builder, location, i64, 3));
    Value byteOffset = arith::AddIOp::create(
        builder, location, dynamicByte,
        llvmConstant(builder, location, i64,
                     staticNBAPlan.generatedOffsets[entry.rootIndex] / 8));
    auto planeAddress = [&](Value plane) {
      return LLVM::GEPOp::create(builder, location, pointer,
                                 builder.getI8Type(), plane,
                                 ValueRange{byteOffset});
    };
    active = arith::AndIOp::create(builder, location, active, overlaps);
    Value oldFieldValue = entryAlloca(builder, location, i64, 1, 8);
    Value oldFieldUnknown = entryAlloca(builder, location, i64, 1, 8);
    Value bitInByte = arith::AndIOp::create(
        builder, location, safeDynamicBit,
        llvmConstant(builder, location, i64, uint64_t{7}));
    Block *dynamicJoin = nullptr;
    if ((entry.width & 7) == 0) {
      Block *aligned = new Block;
      Block *unaligned = new Block;
      dynamicJoin = new Block;
      auto beforeGeneric = Region::iterator(genericNBACommit);
      nbaCommit.getBody().getBlocks().insert(beforeGeneric, aligned);
      beforeGeneric = Region::iterator(genericNBACommit);
      nbaCommit.getBody().getBlocks().insert(beforeGeneric, unaligned);
      beforeGeneric = Region::iterator(genericNBACommit);
      nbaCommit.getBody().getBlocks().insert(beforeGeneric, dynamicJoin);
      Value byteAligned =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                bitInByte,
                                llvmConstant(builder, location, i64, 0))
              .getResult();
      byteAligned =
          arith::AndIOp::create(builder, location, byteAligned, fullyContained);
      cf::CondBranchOp::create(builder, location, byteAligned, aligned,
                               ValueRange{}, unaligned, ValueRange{});

      builder.setInsertionPointToStart(aligned);
      IntegerType alignedType = IntegerType::get(context, entry.width);
      auto commitAlignedPlane = [&](Value plane, StringRef stagedName,
                                    Value oldField) {
        Value address = planeAddress(plane);
        auto old =
            LLVM::LoadOp::create(builder, location, alignedType, address, 1);
        if (plane == stateUnknown)
          old->setAttr("obelisk.eval.preserve_nba_unknown",
                       builder.getUnitAttr());
        LLVM::StoreOp::create(
            builder, location,
            resizeNativeInteger(builder, location, old, cast<IntegerType>(i64)),
            oldField, 8);
        Value staged64 = loadStaged(stagedName);
        Value staged =
            resizeNativeInteger(builder, location, staged64, alignedType);
        auto store = LLVM::StoreOp::create(
            builder, location,
            arith::SelectOp::create(builder, location, active, staged, old),
            address, 1);
        if (plane == stateUnknown) {
          store->setAttr("obelisk.eval.preserve_nba_unknown",
                         builder.getUnitAttr());
          store->setAttr("obelisk.eval.unknown_write_range", publicationRange);
        }
      };
      commitAlignedPlane(stateValue, entry.valueName, oldFieldValue);
      if (!forcedTwoStateEval)
        commitAlignedPlane(stateUnknown, entry.unknownName, oldFieldUnknown);
      else
        LLVM::StoreOp::create(builder, location,
                              llvmConstant(builder, location, i64, 0),
                              oldFieldUnknown, 8);
      cf::BranchOp::create(builder, location, dynamicJoin);
      builder.setInsertionPointToStart(unaligned);
    }
    // Load the exact byte window touched by an unaligned field. The state
    // planes carry an eight-byte guard, so a <=64-bit field can safely use its
    // width+7-bit window at the final root while the dynamic mask preserves
    // adjacent roots sharing those bytes.
    // LLVM stores an odd-width integer using its byte-rounded storage size.
    // The padding bits are not represented by the SSA value and therefore
    // cannot participate in the merge below.  Use an explicitly byte-sized
    // window so an unaligned dynamic field cannot clear the first bit of the
    // adjacent packed element (for example an i39 store for a 32-bit field
    // at bit offset 32 would otherwise overwrite bit 39).
    uint64_t windowWidth = llvm::alignTo(entry.width + 7, uint64_t{8});
    IntegerType windowType = IntegerType::get(context, windowWidth);
    Value windowShift =
        resizeNativeInteger(builder, location, bitInByte, windowType);
    uint64_t fieldMask =
        entry.width == 64 ? UINT64_MAX : (uint64_t{1} << entry.width) - 1;
    Value unshiftedMask =
        llvmConstant(builder, location, windowType, fieldMask);
    Value windowLowClip =
        resizeNativeInteger(builder, location, lowClip, windowType);
    Value windowHighClip =
        resizeNativeInteger(builder, location, highClip, windowType);
    Value destinationMask =
        arith::ShRUIOp::create(builder, location, unshiftedMask, windowLowClip);
    destinationMask = arith::ShLIOp::create(builder, location, destinationMask,
                                            windowHighClip);
    Value shiftedMask =
        arith::ShLIOp::create(builder, location, destinationMask, windowShift);
    Value valueAddress = planeAddress(stateValue);
    Value oldValue =
        LLVM::LoadOp::create(builder, location, windowType, valueAddress, 1);
    Value oldSelectedValue = arith::AndIOp::create(
        builder, location,
        arith::ShRUIOp::create(builder, location, oldValue, windowShift),
        unshiftedMask);
    oldSelectedValue = arith::ShRUIOp::create(builder, location,
                                              oldSelectedValue, windowHighClip);
    oldSelectedValue = arith::ShLIOp::create(builder, location,
                                             oldSelectedValue, windowLowClip);
    LLVM::StoreOp::create(builder, location,
                          resizeNativeInteger(builder, location,
                                              oldSelectedValue,
                                              cast<IntegerType>(i64)),
                          oldFieldValue, 8);
    Value staged64 = loadStaged(entry.valueName);
    Value stagedValue =
        resizeNativeInteger(builder, location, staged64, windowType);
    stagedValue =
        arith::ShRUIOp::create(builder, location, stagedValue, windowLowClip);
    stagedValue =
        arith::ShLIOp::create(builder, location, stagedValue, windowHighClip);
    stagedValue = arith::ShLIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, stagedValue, destinationMask),
        windowShift);
    Value mergedValue = arith::XOrIOp::create(
        builder, location, oldValue,
        arith::AndIOp::create(
            builder, location,
            arith::XOrIOp::create(builder, location, oldValue, stagedValue),
            shiftedMask));
    LLVM::StoreOp::create(builder, location,
                          arith::SelectOp::create(builder, location, active,
                                                  mergedValue, oldValue),
                          valueAddress, 1);
    if (!forcedTwoStateEval) {
      Value unknownAddress = planeAddress(stateUnknown);
      auto oldUnknown = LLVM::LoadOp::create(builder, location, windowType,
                                             unknownAddress, 1);
      oldUnknown->setAttr("obelisk.eval.preserve_nba_unknown",
                          builder.getUnitAttr());
      Value oldSelectedUnknown = arith::AndIOp::create(
          builder, location,
          arith::ShRUIOp::create(builder, location, oldUnknown, windowShift),
          unshiftedMask);
      oldSelectedUnknown = arith::ShRUIOp::create(
          builder, location, oldSelectedUnknown, windowHighClip);
      oldSelectedUnknown = arith::ShLIOp::create(
          builder, location, oldSelectedUnknown, windowLowClip);
      LLVM::StoreOp::create(builder, location,
                            resizeNativeInteger(builder, location,
                                                oldSelectedUnknown,
                                                cast<IntegerType>(i64)),
                            oldFieldUnknown, 8);
      Value stagedUnknown64 = loadStaged(entry.unknownName);
      Value stagedUnknown =
          resizeNativeInteger(builder, location, stagedUnknown64, windowType);
      stagedUnknown = arith::ShRUIOp::create(builder, location, stagedUnknown,
                                             windowLowClip);
      stagedUnknown = arith::ShLIOp::create(builder, location, stagedUnknown,
                                            windowHighClip);
      stagedUnknown = arith::ShLIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, stagedUnknown,
                                destinationMask),
          windowShift);
      Value mergedUnknown = arith::XOrIOp::create(
          builder, location, oldUnknown,
          arith::AndIOp::create(builder, location,
                                arith::XOrIOp::create(builder, location,
                                                      oldUnknown,
                                                      stagedUnknown),
                                shiftedMask));
      auto store = LLVM::StoreOp::create(
          builder, location,
          arith::SelectOp::create(builder, location, active, mergedUnknown,
                                  oldUnknown),
          unknownAddress, 1);
      store->setAttr("obelisk.eval.preserve_nba_unknown",
                     builder.getUnitAttr());
      // The clipped mask preserves every bit outside this canonical root,
      // including neighbors in the byte-rounded load/store window.
      store->setAttr("obelisk.eval.unknown_write_range", publicationRange);
    } else
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            oldFieldUnknown, 8);
    if (dynamicJoin) {
      cf::BranchOp::create(builder, location, dynamicJoin);
      builder.setInsertionPointToStart(dynamicJoin);
    }
    if (!entry.queued)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i32, 0),
                            validAddress, 4);
    Value oldPublishedValue =
        LLVM::LoadOp::create(builder, location, i64, oldFieldValue, 8);
    Value oldPublishedUnknown =
        LLVM::LoadOp::create(builder, location, i64, oldFieldUnknown, 8);
    Value stagedPublishedValue = loadStaged(entry.valueName);
    Value stagedPublishedUnknown = loadStaged(entry.unknownName);
    Value publishedMask = llvmConstant(
        builder, location, i64,
        entry.width == 64 ? UINT64_MAX : (uint64_t{1} << entry.width) - 1);
    Value requestedMask =
        arith::ShRUIOp::create(builder, location, publishedMask, highClip);
    requestedMask =
        arith::ShLIOp::create(builder, location, requestedMask, lowClip);
    requestedMask = arith::SelectOp::create(builder, location, active,
                                            requestedMask, zero64);
    auto mergePublished = [&](Value oldValue, Value stagedValue) {
      return arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, stagedValue, requestedMask),
          arith::AndIOp::create(
              builder, location, oldValue,
              arith::XOrIOp::create(
                  builder, location, requestedMask,
                  llvmConstant(builder, location, i64, UINT64_MAX))));
    };
    Value newPublishedValue = arith::SelectOp::create(
        builder, location, active,
        mergePublished(oldPublishedValue, stagedPublishedValue),
        oldPublishedValue);
    Value newPublishedUnknown = arith::SelectOp::create(
        builder, location, active,
        mergePublished(oldPublishedUnknown, stagedPublishedUnknown),
        oldPublishedUnknown);
    uint64_t publishedMaskBits =
        entry.width == 64 ? UINT64_MAX : (uint64_t{1} << entry.width) - 1;
    auto invertPublished = [&](Value value) {
      return arith::XOrIOp::create(
          builder, location, value,
          llvmConstant(builder, location, i64, UINT64_MAX));
    };
    Value widthMask = llvmConstant(builder, location, i64, publishedMaskBits);
    Value oldKnown = arith::AndIOp::create(
        builder, location, invertPublished(oldPublishedUnknown), widthMask);
    Value newKnown = arith::AndIOp::create(
        builder, location, invertPublished(newPublishedUnknown), widthMask);
    Value oldZero = arith::AndIOp::create(builder, location, oldKnown,
                                          invertPublished(oldPublishedValue));
    Value oldOne =
        arith::AndIOp::create(builder, location, oldKnown, oldPublishedValue);
    Value newZero = arith::AndIOp::create(builder, location, newKnown,
                                          invertPublished(newPublishedValue));
    Value newOne =
        arith::AndIOp::create(builder, location, newKnown, newPublishedValue);
    Value changed = arith::AndIOp::create(
        builder, location,
        arith::OrIOp::create(
            builder, location,
            arith::XOrIOp::create(builder, location, oldPublishedValue,
                                  newPublishedValue),
            arith::XOrIOp::create(builder, location, oldPublishedUnknown,
                                  newPublishedUnknown)),
        widthMask);
    Value posedge = arith::AndIOp::create(
        builder, location,
        arith::OrIOp::create(builder, location,
                             arith::AndIOp::create(builder, location, oldZero,
                                                   invertPublished(newZero)),
                             arith::AndIOp::create(builder, location,
                                                   oldPublishedUnknown,
                                                   newOne)),
        widthMask);
    Value negedge = arith::AndIOp::create(
        builder, location,
        arith::OrIOp::create(builder, location,
                             arith::AndIOp::create(builder, location, oldOne,
                                                   invertPublished(newOne)),
                             arith::AndIOp::create(builder, location,
                                                   oldPublishedUnknown,
                                                   newZero)),
        widthMask);
    const obelisk_rt_static_nba_root &dynamicRoot =
        staticNBAPlan.roots[entry.rootIndex];
    Value publishedStart =
        arith::AddIOp::create(builder, location, safeDynamicBit, highClip);
    Value publishedWidth = arith::SubIOp::create(
        builder, location,
        arith::SubIOp::create(builder, location,
                              llvmConstant(builder, location, i64, entry.width),
                              lowClip),
        highClip);
    for (const obelisk_rt_static_fanout_entry &fanout : fanoutEntries) {
      if (fanout.static_state != dynamicRoot.static_state ||
          fanout.bit_width == 0)
        continue;
      Value writeHigh = arith::AddIOp::create(builder, location, publishedStart,
                                              publishedWidth);
      Value fanoutLow = llvmConstant(builder, location, i64, fanout.low_bit);
      Value fanoutHigh = llvmConstant(builder, location, i64,
                                      fanout.low_bit + fanout.bit_width);
      Value dynamicAbove =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                                publishedStart, fanoutLow);
      Value overlapLow = arith::SelectOp::create(
          builder, location, dynamicAbove, publishedStart, fanoutLow);
      Value dynamicBelow = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ule, writeHigh, fanoutHigh);
      Value overlapHigh = arith::SelectOp::create(
          builder, location, dynamicBelow, writeHigh, fanoutHigh);
      Value overlaps =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                                overlapLow, overlapHigh);
      Value localLow =
          arith::SubIOp::create(builder, location, overlapLow, publishedStart);
      Value overlapWidth =
          arith::SubIOp::create(builder, location, overlapHigh, overlapLow);
      Value safeLocalLow =
          arith::SelectOp::create(builder, location, overlaps, localLow,
                                  llvmConstant(builder, location, i64, 0));
      Value safeOverlapWidth =
          arith::SelectOp::create(builder, location, overlaps, overlapWidth,
                                  llvmConstant(builder, location, i64, 1));
      Value observed = changed;
      switch (fanout.edge) {
      case OBELISK_RT_WAIT_EDGE_POSEDGE:
        observed = posedge;
        break;
      case OBELISK_RT_WAIT_EDGE_NEGEDGE:
        observed = negedge;
        break;
      case OBELISK_RT_WAIT_EDGE_BOTH:
        observed = arith::OrIOp::create(builder, location, posedge, negedge);
        break;
      default:
        break;
      }
      // Edge masks above use the source-language field coordinates.  Remove
      // the invalid low prefix so bit zero now names `publishedStart`, which
      // is the coordinate used by the fanout overlap below.
      observed = arith::ShRUIOp::create(builder, location, observed, lowClip);
      Value selectedObserved =
          arith::ShRUIOp::create(builder, location, observed, safeLocalLow);
      Value maskShift = arith::SubIOp::create(
          builder, location, llvmConstant(builder, location, i64, 64),
          safeOverlapWidth);
      Value overlapMask = arith::ShRUIOp::create(
          builder, location, llvmConstant(builder, location, i64, UINT64_MAX),
          maskShift);
      Value anyObserved = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(builder, location, selectedObserved,
                                overlapMask),
          llvmConstant(builder, location, i64, 0));
      Value triggered =
          arith::AndIOp::create(builder, location, overlaps, anyObserved);
      bool direct = fanoutRoute(fanout) == OBELISK_RT_FANOUT_DIRECT &&
                    fanout.merged_bit < directActivationWordCount * 64 &&
                    directActivationWordCount != 0;
      Value activation = direct ? activatedDirect : activatedNodes;
      uint32_t activationBit = direct ? fanout.merged_bit : fanout.compute_node;
      uint32_t activationWords =
          direct ? directActivationWordCount : activationWordCount;
      if (!activation || activationBit >= activationWords * 64)
        return module.emitError(
            "dynamic NBA fanout has no generated activation target");
      Value activationAddress =
          byteGEP(builder, location, activation,
                  uint64_t{activationBit / 64} * sizeof(uint64_t));
      Value priorActivation =
          LLVM::LoadOp::create(builder, location, i64, activationAddress, 8);
      Value selectedActivation = arith::SelectOp::create(
          builder, location, triggered,
          llvmConstant(builder, location, i64,
                       uint64_t{1} << (activationBit % 64)),
          llvmConstant(builder, location, i64, 0));
      LLVM::StoreOp::create(builder, location,
                            arith::OrIOp::create(builder, location,
                                                 priorActivation,
                                                 selectedActivation),
                            activationAddress, 8);
    }
    cf::BranchOp::create(builder, location, nextDynamic);
    if (entry.queued)
      builder.restoreInsertionPoint(legacyIP);
    else
      builder.setInsertionPointToStart(nextDynamic);
  }

  SmallVector<Block *> wordBlocks(nbaDirtyWordCount);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word)
    if (!scalarRootsByWord[word].empty()) {
      wordBlocks[word] = new Block;
      nbaCommit.getBody().getBlocks().insert(Region::iterator(genericNBACommit),
                                             wordBlocks[word]);
    }
  Block *firstWord = genericNBACommit;
  for (Block *block : wordBlocks)
    if (block) {
      firstWord = block;
      break;
    }
  cf::BranchOp::create(builder, location, firstWord);

  auto nextWordAfter = [&](uint32_t current) -> Block * {
    for (uint32_t word = current + 1; word != nbaDirtyWordCount; ++word)
      if (wordBlocks[word])
        return wordBlocks[word];
    return genericNBACommit;
  };
  auto scalarMask = [](uint64_t width) {
    return width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
  };
  auto loadRoot = [&](Value plane, uint64_t offset, uint64_t width) {
    uint64_t firstByte = offset / 8;
    uint64_t shift = offset % 8;
    if (shift == 0 && width < 8) {
      Value exact =
          LLVM::LoadOp::create(builder, location, builder.getI8Type(),
                               byteGEP(builder, location, plane, firstByte), 1);
      exact = arith::AndIOp::create(builder, location, exact,
                                    llvmConstant(builder, location,
                                                 builder.getI8Type(),
                                                 scalarMask(width)));
      return LLVM::ZExtOp::create(builder, location, i64, exact).getResult();
    }
    if (shift == 0 &&
        (width == 8 || width == 16 || width == 32 || width == 64)) {
      IntegerType rootType = IntegerType::get(context, width);
      Value exact =
          LLVM::LoadOp::create(builder, location, rootType,
                               byteGEP(builder, location, plane, firstByte), 1);
      return width == 64 ? exact
                         : LLVM::ZExtOp::create(builder, location, i64, exact)
                               .getResult();
    }
    Value low =
        LLVM::LoadOp::create(builder, location, i64,
                             byteGEP(builder, location, plane, firstByte), 1);
    Value value = low;
    if (shift != 0)
      value =
          arith::ShRUIOp::create(builder, location, value,
                                 llvmConstant(builder, location, i64, shift));
    if (width > 64 - shift) {
      Value high = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(),
          byteGEP(builder, location, plane, firstByte + 8), 1);
      high = LLVM::ZExtOp::create(builder, location, i64, high);
      high = arith::ShLIOp::create(
          builder, location, high,
          llvmConstant(builder, location, i64, 64 - shift));
      value = arith::OrIOp::create(builder, location, value, high);
    }
    return arith::AndIOp::create(
               builder, location, value,
               llvmConstant(builder, location, i64, scalarMask(width)))
        .getResult();
  };
  auto storeRoot = [&](Value plane, uint64_t offset, uint64_t width,
                       Value value) {
    uint64_t firstByte = offset / 8;
    uint64_t shift = offset % 8;
    if (shift == 0 && width < 8) {
      Value exact =
          LLVM::TruncOp::create(builder, location, builder.getI8Type(), value);
      LLVM::StoreOp::create(builder, location, exact,
                            byteGEP(builder, location, plane, firstByte), 1);
      return;
    }
    if (shift == 0 &&
        (width == 8 || width == 16 || width == 32 || width == 64)) {
      IntegerType rootType = IntegerType::get(context, width);
      Value exact = width == 64 ? value
                                : LLVM::TruncOp::create(builder, location,
                                                        rootType, value)
                                      .getResult();
      LLVM::StoreOp::create(builder, location, exact,
                            byteGEP(builder, location, plane, firstByte), 1);
      return;
    }
    Value address = byteGEP(builder, location, plane, firstByte);
    Value oldLow = LLVM::LoadOp::create(builder, location, i64, address, 1);
    uint64_t lowMask = scalarMask(width) << shift;
    Value cleared =
        arith::AndIOp::create(builder, location, oldLow,
                              llvmConstant(builder, location, i64, ~lowMask));
    Value positioned = value;
    if (shift != 0)
      positioned =
          arith::ShLIOp::create(builder, location, positioned,
                                llvmConstant(builder, location, i64, shift));
    positioned =
        arith::AndIOp::create(builder, location, positioned,
                              llvmConstant(builder, location, i64, lowMask));
    LLVM::StoreOp::create(
        builder, location,
        arith::OrIOp::create(builder, location, cleared, positioned), address,
        1);
    if (width <= 64 - shift)
      return;
    uint64_t highWidth = width - (64 - shift);
    uint8_t highMask = static_cast<uint8_t>((uint16_t{1} << highWidth) - 1);
    Value highAddress = byteGEP(builder, location, plane, firstByte + 8);
    Value oldHigh = LLVM::LoadOp::create(builder, location, builder.getI8Type(),
                                         highAddress, 1);
    Value highValue = arith::ShRUIOp::create(
        builder, location, value,
        llvmConstant(builder, location, i64, 64 - shift));
    highValue = LLVM::TruncOp::create(builder, location, builder.getI8Type(),
                                      highValue);
    Value newHigh = arith::OrIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, oldHigh,
                              llvmConstant(builder, location,
                                           builder.getI8Type(),
                                           static_cast<uint8_t>(~highMask))),
        arith::AndIOp::create(
            builder, location, highValue,
            llvmConstant(builder, location, builder.getI8Type(), highMask)));
    LLVM::StoreOp::create(builder, location, newHigh, highAddress, 1);
  };

  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
    if (!wordBlocks[word])
      continue;
    builder.setInsertionPointToStart(wordBlocks[word]);
    Value dirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                nbaDirtyRootsName);
    Value dirty =
        LLVM::LoadOp::create(builder, location, i64,
                             byteGEP(builder, location, dirtyBase,
                                     uint64_t{word} * sizeof(uint64_t)),
                             8);
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i64, 0),
                          byteGEP(builder, location, dirtyBase,
                                  uint64_t{word} * sizeof(uint64_t)),
                          8);
    Value wordEmpty =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              dirty, llvmConstant(builder, location, i64, 0));
    Block *next = nextWordAfter(word);
    Block *firstRoot = new Block;
    nbaCommit.getBody().getBlocks().insert(Region::iterator(next), firstRoot);
    cf::CondBranchOp::create(builder, location, wordEmpty, next, ValueRange{},
                             firstRoot, ValueRange{});

    Block *rootBlock = firstRoot;
    Block *afterGroup = next;
    // Keep dense roots in source order, but skip clean clusters with one
    // mask test instead of visiting every root in a nonempty 64-bit word.
    constexpr unsigned rootsPerGroup = 8;

    for (auto [position, rootIndex] :
         llvm::enumerate(scalarRootsByWord[word])) {
      if (scalarRootsByWord[word].size() > rootsPerGroup &&
          position % rootsPerGroup == 0) {
        size_t groupEnd =
            std::min(position + rootsPerGroup, scalarRootsByWord[word].size());
        afterGroup =
            groupEnd == scalarRootsByWord[word].size() ? next : new Block;
        if (afterGroup != next)
          nbaCommit.getBody().getBlocks().insert(Region::iterator(next),
                                                 afterGroup);
        Block *firstInGroup = new Block;
        nbaCommit.getBody().getBlocks().insert(Region::iterator(afterGroup),
                                               firstInGroup);
        uint64_t groupMask = 0;
        for (size_t index = position; index != groupEnd; ++index)
          groupMask |= uint64_t{1} << (scalarRootsByWord[word][index] % 64);
        builder.setInsertionPointToStart(rootBlock);
        Value groupEmpty = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq,
            arith::AndIOp::create(
                builder, location, dirty,
                llvmConstant(builder, location, i64, groupMask)),
            llvmConstant(builder, location, i64, 0));
        cf::CondBranchOp::create(builder, location, groupEmpty, afterGroup,
                                 ValueRange{}, firstInGroup, ValueRange{});
        rootBlock = firstInGroup;
      }
      const obelisk_rt_static_nba_root &root = nbaRoots[rootIndex];
      uint64_t offset = staticNBAPlan.generatedOffsets[rootIndex];
      uint32_t fixedCommitRegion =
          staticNBAPlan.generatedCommitRegions[rootIndex];
      bool fixedRegionStage = fixedCommitRegion != UINT32_MAX;
      bool fullRootStage =
          rootIndex < staticNBAPlan.generatedFullRootStages.size() &&
          staticNBAPlan.generatedFullRootStages[rootIndex];
      uint64_t fixedWriteMask =
          rootIndex < staticNBAPlan.generatedFixedWriteMasks.size()
              ? staticNBAPlan.generatedFixedWriteMasks[rootIndex]
              : 0;
      StringRef accumulator = staticNBAPlan.generatedAccumulators[rootIndex];
      Block *afterRoot = position + 1 == scalarRootsByWord[word].size() ||
                                 (position + 1) % rootsPerGroup == 0
                             ? afterGroup
                             : new Block;
      if (afterRoot != afterGroup)
        nbaCommit.getBody().getBlocks().insert(Region::iterator(afterGroup),
                                               afterRoot);
      Block *commitRoot = new Block;
      nbaCommit.getBody().getBlocks().insert(Region::iterator(afterRoot),
                                             commitRoot);
      builder.setInsertionPointToStart(rootBlock);
      // Extract the one-bit predicate directly. On x86 this gives instruction
      // selection a `bt`/shift-immediate shape for upper-half roots instead of
      // materializing a 64-bit mask in a register at every barrier.
      Value selected = arith::TruncIOp::create(
          builder, location, builder.getI1Type(),
          arith::ShRUIOp::create(
              builder, location, dirty,
              llvmConstant(builder, location, i64, rootIndex % 64)));
      Value accumulatorBase =
          LLVM::AddressOfOp::create(builder, location, pointer, accumulator);
      Value regionMatches;
      if (fixedRegionStage) {
        regionMatches = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq,
            nbaCommitEntry->getArgument(2),
            llvmConstant(builder, location, i32, fixedCommitRegion));
      } else {
        Value valid = LLVM::LoadOp::create(
            builder, location, i32,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
        Value region = LLVM::LoadOp::create(
            builder, location, i32,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256,
                             exec_region)),
            4);
        regionMatches = arith::AndIOp::create(
            builder, location,
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  valid,
                                  llvmConstant(builder, location, i32, 0)),
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  region, nbaCommitEntry->getArgument(2)));
      }
      Value validRoot =
          arith::AndIOp::create(builder, location, selected, regionMatches);
      cf::CondBranchOp::create(builder, location, validRoot, commitRoot,
                               ValueRange{}, afterRoot, ValueRange{});

      builder.setInsertionPointToStart(commitRoot);
      Value stagedValue = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(builder, location, accumulatorBase,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, value)),
          8);
      Value stagedUnknown;
      if (forcedTwoStateEval) {
        stagedUnknown = llvmConstant(builder, location, i64, 0);
      } else {
        auto load = LLVM::LoadOp::create(
            builder, location, i64,
            byteGEP(
                builder, location, accumulatorBase,
                offsetof(obelisk_rt_generated_nba_accumulator_256, unknown)),
            8);
        // The later two-state commit clone must zero both canonical unknown
        // loads and staged accumulator unknown data left by an earlier
        // four-state slot. Mark this semantic role explicitly instead of
        // reverse-engineering a byte GEP after LLVM lowering.
        load->setAttr("obelisk.eval.two_state_zero_unknown",
                      UnitAttr::get(context));
        stagedUnknown = load;
      }
      Value writeMask =
          fixedWriteMask != 0
              ? llvmConstant(builder, location, i64, fixedWriteMask)
              : arith::AndIOp::create(
                    builder, location,
                    LLVM::LoadOp::create(
                        builder, location, i64,
                        byteGEP(
                            builder, location, accumulatorBase,
                            offsetof(obelisk_rt_generated_nba_accumulator_256,
                                     write_mask)),
                        8),
                    llvmConstant(builder, location, i64,
                                 scalarMask(root.bit_width)))
                    .getResult();
      Value oldValue = loadRoot(stateValue, offset, root.bit_width);
      Value oldUnknown = forcedTwoStateEval
                             ? llvmConstant(builder, location, i64, 0)
                             : loadRoot(stateUnknown, offset, root.bit_width);
      Value inverseMask = arith::XOrIOp::create(
          builder, location, writeMask,
          llvmConstant(builder, location, i64, UINT64_MAX));
      Value newValue = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, oldValue, inverseMask),
          arith::AndIOp::create(builder, location, stagedValue, writeMask));
      Value newUnknown =
          forcedTwoStateEval
              ? llvmConstant(builder, location, i64, 0)
              : arith::OrIOp::create(
                    builder, location,
                    arith::AndIOp::create(builder, location, oldUnknown,
                                          inverseMask),
                    arith::AndIOp::create(builder, location, stagedUnknown,
                                          writeMask))
                    .getResult();
      storeRoot(stateValue, offset, root.bit_width, newValue);
      if (!forcedTwoStateEval)
        storeRoot(stateUnknown, offset, root.bit_width, newUnknown);
      if (fixedWriteMask == 0 && !fullRootStage) {
        LLVM::StoreOp::create(
            builder, location, llvmConstant(builder, location, i64, 0),
            byteGEP(
                builder, location, accumulatorBase,
                offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask)),
            8);
        LLVM::StoreOp::create(
            builder, location, llvmConstant(builder, location, i32, 0),
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
      }
      Value changed = arith::OrIOp::create(
          builder, location,
          arith::XOrIOp::create(builder, location, oldValue, newValue),
          arith::XOrIOp::create(builder, location, oldUnknown, newUnknown));
      // A change watcher also wakes on a bit that changed and changed back
      // within this barrier. Only change triggers read `changed`; a tracked
      // root has no edge watcher.
      if (rootIndex < staticNBAPlan.trackTransients.size() &&
          staticNBAPlan.trackTransients[rootIndex]) {
        Value transientAddress = byteGEP(
            builder, location, accumulatorBase,
            offsetof(obelisk_rt_generated_nba_accumulator_256, transient));
        changed = arith::OrIOp::create(
            builder, location, changed,
            arith::AndIOp::create(
                builder, location,
                LLVM::LoadOp::create(builder, location, i64, transientAddress,
                                     8),
                llvmConstant(builder, location, i64,
                             scalarMask(root.bit_width))));
        LLVM::StoreOp::create(builder, location,
                              llvmConstant(builder, location, i64, 0),
                              transientAddress, 8);
      }
      struct TriggerGroup {
        uint32_t edge;
        uint64_t mask;
        SmallVector<uint64_t> nodes;
        SmallVector<uint64_t> direct;
      };
      SmallVector<TriggerGroup> groups;
      for (const obelisk_rt_static_fanout_entry &entry : fanoutEntries) {
        if (entry.static_state != root.static_state ||
            entry.low_bit >= root.bit_width)
          continue;
        uint64_t high =
            std::min<uint64_t>(root.bit_width, entry.low_bit + entry.bit_width);
        if (entry.low_bit >= high)
          continue;
        uint64_t mask = scalarMask(high - entry.low_bit) << entry.low_bit;
        auto group = llvm::find_if(groups, [&](const TriggerGroup &candidate) {
          return candidate.edge == entry.edge && candidate.mask == mask;
        });
        if (group == groups.end()) {
          groups.push_back(
              {entry.edge, mask, SmallVector<uint64_t>(activationWordCount, 0),
               SmallVector<uint64_t>(directActivationWordCount, 0)});
          group = std::prev(groups.end());
        }
        if (fanoutRoute(entry) == OBELISK_RT_FANOUT_DIRECT &&
            entry.merged_bit < directActivationWordCount * 64 &&
            directActivationWordCount != 0)
          group->direct[entry.merged_bit / 64] |= uint64_t{1}
                                                  << (entry.merged_bit % 64);
        else
          group->nodes[entry.compute_node / 64] |= uint64_t{1}
                                                   << (entry.compute_node % 64);
      }
      if (!groups.empty()) {
        Value widthMask =
            llvmConstant(builder, location, i64, scalarMask(root.bit_width));
        auto invert = [&](Value value) {
          return arith::XOrIOp::create(
                     builder, location, value,
                     llvmConstant(builder, location, i64, UINT64_MAX))
              .getResult();
        };
        Value oldKnown = arith::AndIOp::create(builder, location,
                                               invert(oldUnknown), widthMask);
        Value newKnown = arith::AndIOp::create(builder, location,
                                               invert(newUnknown), widthMask);
        Value oldZero = arith::AndIOp::create(builder, location, oldKnown,
                                              invert(oldValue));
        Value oldOne =
            arith::AndIOp::create(builder, location, oldKnown, oldValue);
        Value newZero = arith::AndIOp::create(builder, location, newKnown,
                                              invert(newValue));
        Value newOne =
            arith::AndIOp::create(builder, location, newKnown, newValue);
        Value posedge = arith::AndIOp::create(
            builder, location,
            arith::OrIOp::create(
                builder, location,
                arith::AndIOp::create(builder, location, oldZero,
                                      invert(newZero)),
                arith::AndIOp::create(builder, location, oldUnknown, newOne)),
            widthMask);
        Value negedge = arith::AndIOp::create(
            builder, location,
            arith::OrIOp::create(
                builder, location,
                arith::AndIOp::create(builder, location, oldOne,
                                      invert(newOne)),
                arith::AndIOp::create(builder, location, oldUnknown, newZero)),
            widthMask);
        for (const TriggerGroup &group : groups) {
          Value observed = changed;
          switch (group.edge) {
          case OBELISK_RT_WAIT_EDGE_POSEDGE:
            observed = posedge;
            break;
          case OBELISK_RT_WAIT_EDGE_NEGEDGE:
            observed = negedge;
            break;
          case OBELISK_RT_WAIT_EDGE_BOTH:
            observed =
                arith::OrIOp::create(builder, location, posedge, negedge);
            break;
          default:
            break;
          }
          Value triggered = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne,
              arith::AndIOp::create(
                  builder, location, observed,
                  llvmConstant(builder, location, i64, group.mask)),
              llvmConstant(builder, location, i64, 0));
          for (auto [activationWord, nodeMask] : llvm::enumerate(group.nodes)) {
            if (nodeMask == 0)
              continue;
            Value address =
                byteGEP(builder, location, activatedNodes,
                        uint64_t{activationWord} * sizeof(uint64_t));
            Value active =
                LLVM::LoadOp::create(builder, location, i64, address, 8);
            Value selected = arith::SelectOp::create(
                builder, location, triggered,
                llvmConstant(builder, location, i64, nodeMask),
                llvmConstant(builder, location, i64, 0));
            LLVM::StoreOp::create(
                builder, location,
                arith::OrIOp::create(builder, location, active, selected),
                address, 8);
          }
          for (auto [activationWord, directMask] :
               llvm::enumerate(group.direct)) {
            if (directMask == 0)
              continue;
            Value address =
                byteGEP(builder, location, activatedDirect,
                        uint64_t{activationWord} * sizeof(uint64_t));
            Value active =
                LLVM::LoadOp::create(builder, location, i64, address, 8);
            Value selected = arith::SelectOp::create(
                builder, location, triggered,
                llvmConstant(builder, location, i64, directMask),
                llvmConstant(builder, location, i64, 0));
            LLVM::StoreOp::create(
                builder, location,
                arith::OrIOp::create(builder, location, active, selected),
                address, 8);
          }
        }
      }
      cf::BranchOp::create(builder, location, afterRoot);
      rootBlock = afterRoot;
    }
  }

  builder.setInsertionPointToStart(genericNBACommit);
  if (directActivationWordCount != 0 && !clockKernels.empty()) {
    Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                              clockKernels.front().ingressName);
    Value any = llvmConstant(builder, location, builder.getI1Type(), 0);
    for (uint32_t word = 0; word != directActivationWordCount; ++word) {
      Value activated =
          LLVM::LoadOp::create(builder, location, i64,
                               byteGEP(builder, location, activatedDirect,
                                       uint64_t{word} * sizeof(uint64_t)),
                               8);
      Value selected = activated;
      updateEvalReadyWord(builder, location, ingress, readyLayout, word,
                          selected);
      any = arith::OrIOp::create(
          builder, location, any,
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                selected,
                                llvmConstant(builder, location, i64, 0)));
    }
    (void)any;
  }
  Block *directDone = new Block;
  nbaCommit.getBody().push_back(directDone);
  cf::BranchOp::create(builder, location, directDone);

  builder.setInsertionPointToStart(directDone);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, OBELISK_RT_OK));

  // The generated coordinator consumes eval latches and publishes model
  // ingress itself. Runtime-owned actors instead stage canonical accumulators
  // (including wide roots with no generated accumulator). Their barrier must
  // publish through the runtime, both during bootstrap and Tier-2 fallback.
  // Reusing the pure eval barrier here can leave a pending runtime write
  // unconsumed forever.
  builder.setInsertionPointToEnd(module.getBody());
  auto runtimeNBACommit = LLVM::LLVMFuncOp::create(
      builder, location, runtimeNBACommitName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  Block *runtimeCommitEntry = runtimeNBACommit.addEntryBlock(builder);
  builder.setInsertionPointToStart(runtimeCommitEntry);
  Value runtimeCommitStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_static_nba_commit_roots"),
          ValueRange{runtimeCommitEntry->getArgument(1),
                     llvmConstant(builder, location, i32, nbaRoots.size()),
                     runtimeCommitEntry->getArgument(2),
                     runtimeCommitEntry->getArgument(3)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, runtimeCommitStatus);

  // A four-state owner does not necessarily stage an unknown NBA value.  A
  // common example is a checkpoint-capable monitor incrementing a known
  // cycle counter.  Before selecting the expensive four-state barrier, scan
  // only dirty fixed roots and prove that the overwritten canonical and
  // staged unknown bits are zero.  This is a quiescent handoff test, not a
  // persistent shadow state: the canonical planes remain authoritative.
  builder.setInsertionPointToEnd(module.getBody());
  auto nbaKnown = LLVM::LLVMFuncOp::create(
      builder, location, nbaKnownName,
      LLVM::LLVMFunctionType::get(
          builder.getI1Type(), {i32, builder.getI1Type(), builder.getI1Type()},
          false));
  nbaKnown.setLinkage(LLVM::Linkage::Internal);
  Block *knownEntry = nbaKnown.addEntryBlock(builder);
  Block *knownTrue = new Block;
  Block *knownFalse = new Block;
  nbaKnown.getBody().push_back(knownTrue);
  nbaKnown.getBody().push_back(knownFalse);
  builder.setInsertionPointToStart(knownTrue);
  LLVM::ReturnOp::create(
      builder, location,
      llvmConstant(builder, location, builder.getI1Type(), 1));
  builder.setInsertionPointToStart(knownFalse);
  LLVM::ReturnOp::create(
      builder, location,
      llvmConstant(builder, location, builder.getI1Type(), 0));

  builder.setInsertionPointToStart(knownEntry);
  Block *wordCursor = knownEntry;
  if (!dynamicEvalNBAs.empty()) {
    Value dynamicActive =
        llvmConstant(builder, location, builder.getI1Type(), 0);
    if (hasOrderedNBA)
      dynamicActive =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                evalNBAQueueSize(builder, location),
                                llvmConstant(builder, location, i32, 0));
    for (const DynamicEvalNBA &entry : dynamicEvalNBAs) {
      if (entry.queued)
        continue;
      Value valid =
          LLVM::LoadOp::create(builder, location, i32,
                               LLVM::AddressOfOp::create(
                                   builder, location, pointer, entry.validName),
                               4);
      dynamicActive = arith::OrIOp::create(
          builder, location, dynamicActive,
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                valid,
                                llvmConstant(builder, location, i32, 0)));
    }
    wordCursor = new Block;
    nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                          wordCursor);
    cf::CondBranchOp::create(builder, location, dynamicActive, knownFalse,
                             ValueRange{}, wordCursor, ValueRange{});
  }

  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
    if (scalarRootsByWord[word].empty())
      continue;
    builder.setInsertionPointToStart(wordCursor);
    Value knownDirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                     nbaDirtyRootsName);
    Value dirty =
        LLVM::LoadOp::create(builder, location, i64,
                             byteGEP(builder, location, knownDirtyBase,
                                     uint64_t{word} * sizeof(uint64_t)),
                             8);
    Value taintBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                evalStepFourStateNBARootsName);
    Value taintedRoots =
        LLVM::LoadOp::create(builder, location, i64,
                             byteGEP(builder, location, taintBase,
                                     uint64_t{word} * sizeof(uint64_t)),
                             8);
    taintedRoots = arith::SelectOp::create(
        builder, location, knownEntry->getArgument(1),
        llvmConstant(builder, location, i64, UINT64_MAX), taintedRoots);
    dirty = arith::AndIOp::create(builder, location, dirty, taintedRoots);
    // Post-NBA combinational settling often reaches this predicate without
    // staging another NBA. Skip an empty bitmap word before examining any of
    // its roots; otherwise even an empty barrier tests up to 64 accumulators.
    // Dynamic accumulators were checked above and retain their cold route.
    Block *inspectWord = new Block;
    Block *nextWord = new Block;
    nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                          inspectWord);
    nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                          nextWord);
    Value emptyWord =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              dirty, llvmConstant(builder, location, i64, 0));
    cf::CondBranchOp::create(builder, location, emptyWord, nextWord,
                             ValueRange{}, inspectWord, ValueRange{});
    builder.setInsertionPointToStart(inspectWord);
    uint64_t supportedMask = 0;
    for (uint32_t rootIndex : scalarRootsByWord[word])
      supportedMask |= uint64_t{1} << (rootIndex % 64);
    Value unsupported = arith::AndIOp::create(
        builder, location, dirty,
        llvmConstant(builder, location, i64, ~supportedMask));
    Value hasUnsupported = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, unsupported,
        llvmConstant(builder, location, i64, 0));
    Block *rootCursor = new Block;
    nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                          rootCursor);
    cf::CondBranchOp::create(builder, location, hasUnsupported, knownFalse,
                             ValueRange{}, rootCursor, ValueRange{});

    for (uint32_t rootIndex : scalarRootsByWord[word]) {
      const obelisk_rt_static_nba_root &root = nbaRoots[rootIndex];
      StringRef accumulator = staticNBAPlan.generatedAccumulators[rootIndex];
      uint32_t fixedCommitRegion =
          staticNBAPlan.generatedCommitRegions[rootIndex];
      uint64_t fixedWriteMask =
          rootIndex < staticNBAPlan.generatedFixedWriteMasks.size()
              ? staticNBAPlan.generatedFixedWriteMasks[rootIndex]
              : 0;
      builder.setInsertionPointToStart(rootCursor);
      Value selected = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(builder, location, dirty,
                                llvmConstant(builder, location, i64,
                                             uint64_t{1} << (rootIndex % 64))),
          llvmConstant(builder, location, i64, 0));
      Value accumulatorBase =
          LLVM::AddressOfOp::create(builder, location, pointer, accumulator);
      Value regionMatches;
      if (fixedCommitRegion != UINT32_MAX) {
        regionMatches = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq,
            knownEntry->getArgument(0),
            llvmConstant(builder, location, i32, fixedCommitRegion));
      } else {
        Value valid = LLVM::LoadOp::create(
            builder, location, i32,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
        Value region = LLVM::LoadOp::create(
            builder, location, i32,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256,
                             exec_region)),
            4);
        regionMatches = arith::AndIOp::create(
            builder, location,
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  valid,
                                  llvmConstant(builder, location, i32, 0)),
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  region, knownEntry->getArgument(0)));
      }
      Value inspectRoot =
          arith::AndIOp::create(builder, location, selected, regionMatches);
      Block *inspect = new Block;
      Block *nextRoot = new Block;
      nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                            inspect);
      nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                            nextRoot);
      cf::CondBranchOp::create(builder, location, inspectRoot, inspect,
                               ValueRange{}, nextRoot, ValueRange{});

      builder.setInsertionPointToStart(inspect);
      Value writeMask =
          fixedWriteMask != 0
              ? llvmConstant(builder, location, i64, fixedWriteMask)
              : arith::AndIOp::create(
                    builder, location,
                    LLVM::LoadOp::create(
                        builder, location, i64,
                        byteGEP(
                            builder, location, accumulatorBase,
                            offsetof(obelisk_rt_generated_nba_accumulator_256,
                                     write_mask)),
                        8),
                    llvmConstant(builder, location, i64,
                                 scalarMask(root.bit_width)))
                    .getResult();
      Value stagedUnknown = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(builder, location, accumulatorBase,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, unknown)),
          8);
      stagedUnknown = arith::SelectOp::create(
          builder, location, knownEntry->getArgument(2), stagedUnknown,
          llvmConstant(builder, location, i64, 0));
      Value oldUnknown =
          loadRoot(LLVM::AddressOfOp::create(builder, location, pointer,
                                             "__obelisk_state_unknown"),
                   staticNBAPlan.generatedOffsets[rootIndex], root.bit_width);
      Value relevantUnknown = arith::AndIOp::create(
          builder, location,
          arith::OrIOp::create(builder, location, stagedUnknown, oldUnknown),
          writeMask);
      Value hasUnknown = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, relevantUnknown,
          llvmConstant(builder, location, i64, 0));
      cf::CondBranchOp::create(builder, location, hasUnknown, knownFalse,
                               ValueRange{}, nextRoot, ValueRange{});
      rootCursor = nextRoot;
    }
    builder.setInsertionPointToStart(rootCursor);
    cf::BranchOp::create(builder, location, nextWord);
    wordCursor = nextWord;
  }
  builder.setInsertionPointToStart(wordCursor);
  cf::BranchOp::create(builder, location, knownTrue);

  constexpr StringLiteral invalidateRangeName =
      "__obelisk_eval_promotion_invalidate_range_v1";
  getOrDeclareLLVMFunction(module, invalidateRangeName,
                           LLVM::LLVMVoidType::get(context), {i64, i64});
  constexpr StringLiteral recheckRangeName =
      "__obelisk_eval_promotion_recheck_range_v1";
  getOrDeclareLLVMFunction(module, recheckRangeName,
                           LLVM::LLVMVoidType::get(context), {i64, i64});
  auto planType = getNativeSchedulePlanLLVMType(context);
  makeConstantGlobal(
      module, location, planType, planName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &initializerBuilder) {
        Value value =
            LLVM::ZeroOp::create(initializerBuilder, location, planType);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32,
                                         getNativeSchedulePlanSize(dataLayout)),
                            NativeSchedulePlanField::Size);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         graphLayoutChecksum),
                            NativeSchedulePlanField::GraphLayoutChecksum);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, stateName),
                        NativeSchedulePlanField::MutableState);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64,
                         uint64_t{actorCount} * dataLayout.getPointerSize()),
            NativeSchedulePlanField::MutableStateSize);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, actorCount),
            NativeSchedulePlanField::ActorCapacity);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(
                initializerBuilder, location, i32,
                (fullyStatic ? OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC : 0) |
                    (rootSlotZero ? OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO
                                  : 0) |
                    (staticControlEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL
                         : 0) |
                    (staticFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT
                         : 0) |
                    (enableDirectState ? OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE
                                       : 0) |
                    (enableStaticNBA ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA
                                     : 0) |
                    (closedEvalSchedule
                         ? OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS
                         : 0) |
                    (guardedFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT
                         : 0) |
                    (guardedSpecializationEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION
                         : 0) |
                    (cleanSuperstepEnabled && fullyStatic
                         ? OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP
                         : 0) |
                    (cleanSuperstepEnabled && staticEvalIsland
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND
                         : 0) |
                    OBELISK_RT_NATIVE_SCHEDULE_EVAL),
            NativeSchedulePlanField::Flags);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(initializerBuilder,
                                                      location, pointer,
                                                      "__obelisk_state_value"),
                            NativeSchedulePlanField::StateValue);
        value = insertValue(
            initializerBuilder, location, value,
            LLVM::AddressOfOp::create(initializerBuilder, location, pointer,
                                      "__obelisk_state_unknown"),
            NativeSchedulePlanField::StateUnknown);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         stateLayout.bitCount),
                            NativeSchedulePlanField::StateBitCount);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, bindName),
                        NativeSchedulePlanField::Bind);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(
                                initializerBuilder, location, pointer, runName),
                            NativeSchedulePlanField::Run);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, snapshotName),
                        NativeSchedulePlanField::FallbackSnapshot);
        Value rootsAddress =
            nbaRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaRootsName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, rootsAddress,
                            NativeSchedulePlanField::NBARoots);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, nbaRoots.size()),
            NativeSchedulePlanField::NBARootCount);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32, 0),
                            NativeSchedulePlanField::Reserved0);
        Value sitesAddress =
            nbaSites.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaSitesName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, sitesAddress,
                            NativeSchedulePlanField::NBASites);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, nbaSites.size()),
            NativeSchedulePlanField::NBASiteCount);
        Value fanoutAddress =
            fanoutEntries.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, fanoutName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, fanoutAddress,
                            NativeSchedulePlanField::FanoutEntries);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         fanoutEntries.size()),
                            NativeSchedulePlanField::FanoutEntryCount);
        Value actorRootsAddress =
            actorRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, actorRootsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, actorRootsAddress,
                        NativeSchedulePlanField::ActorRoots);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, actorRoots.size()),
            NativeSchedulePlanField::ActorRootCount);
        Value commitAddress =
            enableStaticNBA
                ? LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, runtimeNBACommitName)
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, commitAddress,
                            NativeSchedulePlanField::NBACommit);
        Value specializationFast =
            guardedSpecializationEnabled
                ? LLVM::AddressOfOp::create(
                      initializerBuilder, location, pointer,
                      "__obelisk_static_specialization_fast_v1")
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, specializationFast,
                        NativeSchedulePlanField::SpecializationFast);
        Value dirtyRoots =
            nbaDirtyWordCount == 0
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaDirtyRootsName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, dirtyRoots,
                            NativeSchedulePlanField::NBADirtyRoots);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, nbaDirtyWordCount),
            NativeSchedulePlanField::NBADirtyWordCount);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32, 0),
                            NativeSchedulePlanField::Reserved1);
        Value dirtySummary =
            nbaDirtySummaryWordCount == 0
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaDirtySummaryName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, dirtySummary,
                            NativeSchedulePlanField::NBADirtySummary);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32,
                                         nbaDirtySummaryWordCount),
                            NativeSchedulePlanField::NBADirtySummaryWordCount);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32, 0),
                            NativeSchedulePlanField::Reserved2);
        Value clocksAddress =
            clockKernels.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, clockKernelsName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, clocksAddress,
                            NativeSchedulePlanField::ClockKernels);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32,
                                         clockKernels.size()),
                            NativeSchedulePlanField::ClockKernelCount);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32, 0),
                            NativeSchedulePlanField::Reserved3);
        Value mergedAddress =
            mergedFragments.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, mergedFragmentsName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value, mergedAddress,
                            NativeSchedulePlanField::MergedFragments);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         mergedFragments.size()),
                            NativeSchedulePlanField::MergedFragmentCount);
        Value coordinatorAddress =
            clockKernels.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, evalDispatchName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, coordinatorAddress,
                        NativeSchedulePlanField::TimeslotCoordinator);
        Value promotionInvalidateAddress = LLVM::AddressOfOp::create(
            initializerBuilder, location, pointer, promotionInvalidateName);
        value = insertValue(initializerBuilder, location, value,
                            promotionInvalidateAddress,
                            NativeSchedulePlanField::PromotionInvalidate);
        Value promotionQueryAddress = LLVM::AddressOfOp::create(
            initializerBuilder, location, pointer, promotionQueryName);
        value = insertValue(initializerBuilder, location, value,
                            promotionQueryAddress,
                            NativeSchedulePlanField::PromotionReady);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, invalidateRangeName),
                        NativeSchedulePlanField::PromotionInvalidateRange);
        return insertValue(initializerBuilder, location, value,
                           LLVM::AddressOfOp::create(initializerBuilder,
                                                     location, pointer,
                                                     recheckRangeName),
                           NativeSchedulePlanField::PromotionRecheckRange);
      });
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_snapshot_aot", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run_aot_nodes", i32,
                           {pointer, pointer, i32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_prepare_periodic_aot", i32,
      {pointer, pointer, i32, pointer, i32, pointer, i32, pointer, pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_handoff_periodic_aot", i32,
                           {pointer, pointer, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_root", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_roots", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_direct_commit_guard", i32, {pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_static_nba_account_generated_commits",
                           LLVM::LLVMVoidType::get(context), {pointer, i32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_activate_static_nodes",
      LLVM::LLVMVoidType::get(context), {pointer, pointer, i32});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_direct_fragment_enter", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_direct_fragment_leave", i32,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_execute_aot_actor",
                           i32, {pointer, i32});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_priority_signal_pending",
                           i32, {pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_queue_aot_checkpoint", i32,
                           {pointer, i32, i32, pointer});
  return true;
}

} // namespace obelisk::detail
