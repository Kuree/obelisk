//===- SimulationNBAPlanning.cpp - Native static NBA plan support -------===//

#include "../SimulationToLLVMCoroutine/SimulationNBALowering.h"
#include "../SimulationToLLVMCoroutine/SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/LoopLikeInterface.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Twine.h"

#include <limits>

using namespace mlir;

#include "NativeNBAUtils.h"
namespace obelisk::detail {
/// Return true when `enqueue` cannot execute more than once between two NBA
/// barriers. Its statement must occur once in this call closure, sit
/// outside any loop, lie in a function no call can re-enter, and have no
/// control-flow path back to its own block without advancing simulation time.
static bool nbaEnqueueExecutesAtMostOnce(
    sim::SimNBAEnqueueOp enqueue, const NativeStaticNBAPlan &plan,
    const llvm::DenseMap<uint64_t, unsigned> &closureSiteOps,
    const llvm::SmallPtrSetImpl<Operation *> &callees) {
  schedule::NBASiteAttr site = enqueue.getSiteAttr();
  if (!site)
    return false;
  auto origin = plan.siteSemanticOrigins.find(site.getId());
  uint64_t semanticSite =
      origin == plan.siteSemanticOrigins.end() ? site.getId() : origin->second;
  // Count only within this activation's call closure. Two-state, four-state
  // and Eval clones of one body are alternative implementations of the same
  // statement, not additional executions, so a module-wide count would reject
  // every cloned actor.
  if (closureSiteOps.lookup(semanticSite) != 1)
    return false;
  sim::SimFuncOp function = enqueue->getParentOfType<sim::SimFuncOp>();
  if (!function || callees.contains(function.getOperation()))
    return false;
  for (Operation *ancestor = enqueue->getParentOp();
       ancestor && ancestor != function.getOperation();
       ancestor = ancestor->getParentOp())
    if (isa<LoopLikeOpInterface>(ancestor))
      return false;
  // An event or #0 suspension can resume the process in the same time slot
  // before the NBA region. Only a strictly positive delay guarantees that
  // the next activation starts after a fresh NBA barrier (LRM 4.4.2.3-4).
  auto advancesTime = [](Operation *terminator) {
    auto delay = dyn_cast<sim::SimSuspendDelayOp>(terminator);
    auto constant =
        delay ? delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>()
              : sim::SimTimeConstantOp{};
    return constant && constant.getValue() != 0;
  };
  Block *originBlock = enqueue->getBlock();
  if (advancesTime(originBlock->getTerminator()))
    return true;
  SmallVector<Block *, 8> worklist;
  for (Block *successor : originBlock->getTerminator()->getSuccessors())
    worklist.push_back(successor);
  llvm::SmallPtrSet<Block *, 16> visited;
  while (!worklist.empty()) {
    Block *block = worklist.pop_back_val();
    if (block == originBlock)
      return false;
    if (!visited.insert(block).second)
      continue;
    if (advancesTime(block->getTerminator()))
      continue;
    for (Block *successor : block->getTerminator()->getSuccessors())
      worklist.push_back(successor);
  }
  return true;
}

FailureOr<NativeStaticNBAPlan> buildNativeStaticNBAPlan(
    ModuleOp module, const NativeStateLayout &stateLayout,
    ArrayRef<schedule::ComputeNBACommitAttr> orderedCommits, bool enabled) {
  NativeStaticNBAPlan plan;
  if (!enabled)
    return plan;
  for (schedule::ComputeNBACommitAttr commit : orderedCommits) {
    schedule::ComputeEffectAttr effect = commit.getEffect();
    if (effect.getResource() != schedule::ComputeResourceKind::Storage ||
        effect.getTarget() != schedule::ComputeTargetKind::Descriptor ||
        effect.getDynamic() || effect.getDeferred())
      return module.emitError(
                 "static NBA commit does not identify one fixed storage root"),
             failure();
    auto handle = stateLayout.storage.find(effect.getDescriptor());
    if (handle == stateLayout.storage.end())
      return module.emitError(
                 "static NBA commit references an unknown storage descriptor"),
             failure();
    obelisk_rt_stable_handle_v1 decoded{};
    if (!obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
        decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.offset != 0)
      return module.emitError(
                 "static NBA commit has an invalid native state root"),
             failure();
    auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
      return candidate.handleID == decoded.id;
    });
    if (bound == stateLayout.bounds.end())
      return module.emitError(
                 "static NBA commit root is absent from native state layout"),
             failure();
    if (!stateLayout.nbaHandles.contains(decoded.id) ||
        !commit.getFrontierSites().empty())
      continue;
    uint32_t root = static_cast<uint32_t>(plan.roots.size());
    plan.roots.push_back({commit.getId(), decoded.id,
                          static_cast<uint64_t>(bound->width), nullptr});
    plan.generatedAccumulators.emplace_back();
    plan.generatedOffsets.push_back(bound->offset);
    plan.generatedCommitRegions.push_back(UINT32_MAX);
    plan.generatedFullRootStages.push_back(false);
    plan.generatedFixedWriteMasks.push_back(0);
    if (bound->width <= OBELISK_RT_GENERATED_NBA_MAX_BITS)
      plan.generatedAccumulators.back() =
          ("__obelisk_aot_nba_accumulator_" + Twine(root)).str();
    auto appendSites = [&](DenseI64ArrayAttr ids, uint32_t storage) {
      for (int64_t id : ids.asArrayRef()) {
        if (id < 0)
          return failure();
        plan.sites.push_back({static_cast<uint64_t>(id), root, storage});
      }
      return success();
    };
    if (failed(
            appendSites(commit.getSlots(), OBELISK_RT_STATIC_NBA_FIXED_SLOT)) ||
        failed(appendSites(commit.getAccumulatorSites(),
                           OBELISK_RT_STATIC_NBA_ROOT_ACCUMULATOR)))
      return module.emitError("static NBA site identity is negative"),
             failure();
  }
  llvm::sort(plan.sites, [](const auto &left, const auto &right) {
    return left.site < right.site;
  });
  if (std::adjacent_find(plan.sites.begin(), plan.sites.end(),
                         [](const auto &left, const auto &right) {
                           return left.site == right.site;
                         }) != plan.sites.end())
    return module.emitError("static NBA site identity is duplicated"),
           failure();
  for (const obelisk_rt_static_nba_site &site : plan.sites)
    plan.siteRoots.try_emplace(site.site, site.root);
  module.walk([&](sim::SimNBAEnqueueOp enqueue) {
    schedule::NBASiteAttr site = enqueue.getSiteAttr();
    if (!site)
      return;
    auto origin =
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalOriginNbaSite>(
            enqueue);
    plan.siteSemanticOrigins.try_emplace(
        site.getId(), origin ? origin.getValue().getZExtValue() : site.getId());
  });

  struct Lane {
    uint64_t stride;
    uint64_t low;
    uint64_t width;
  };
  SmallVector<DenseMap<uint64_t, Lane>> lanes(plan.roots.size());
  DenseMap<uint64_t, SmallVector<sim::SimNBAEnqueueOp>> originEnqueues;
  SmallVector<bool> conflictingLanes(plan.roots.size(), false);
  module.walk([&](sim::SimNBAEnqueueOp enqueue) {
    auto function = enqueue->getParentOfType<sim::SimFuncOp>();
    if (!function ||
        !::obelisk::schedule::has<::obelisk::schedule::Field::EvalRawCaptures>(
            function))
      return;
    auto site = enqueue.getSiteAttr();
    auto mapped =
        site ? plan.siteRoots.find(site.getId()) : plan.siteRoots.end();
    if (mapped == plan.siteRoots.end())
      return;
    uint32_t rootIndex = mapped->second;
    const auto &root = plan.roots[rootIndex];
    if (root.bit_width <= 64)
      return;
    auto reject = [&] { conflictingLanes[rootIndex] = true; };
    if (enqueue.getDelay() || site.getTiming())
      return reject();
    Value reference = enqueue.getDestination();
    auto width = nativeStateWidth(enqueue.getValue().getType());
    uint64_t origin = plan.siteSemanticOrigins.lookup(site.getId());
    originEnqueues[origin].push_back(enqueue);
    auto recordLane = [&](Lane lane) {
      auto [previous, inserted] = lanes[rootIndex].try_emplace(origin, lane);
      if (!inserted && (previous->second.stride != lane.stride ||
                        previous->second.low != lane.low ||
                        previous->second.width != lane.width))
        reject();
    };
    // A fixed subelement is a lane in a single root-sized element. This
    // proves disjoint packed words as well as unpacked array elements without
    // assuming that distinct descriptors or source expressions cannot alias.
    if (auto fixed = resolveStaticNBADestination(reference, stateLayout)) {
      if (fixed->staticID != root.static_state || !width || *width == 0 ||
          *width > 64 || fixed->offset > root.bit_width ||
          *width > root.bit_width - fixed->offset)
        return reject();
      return recordLane({root.bit_width, fixed->offset, *width});
    }
    uint64_t low = 0;
    if (auto extract = reference.getDefiningOp<sim::SimRefExtractOp>()) {
      low = extract.getLowBit();
      reference = extract.getInput();
    }
    auto element = reference.getDefiningOp<sim::SimRefArrayElementOp>();
    if (!element)
      return reject();
    auto base = resolveStaticNBADestination(element.getInput(), stateLayout);
    auto stride =
        nativeStateWidth(element.getResult().getType().getElementType());
    auto arrayWidth =
        nativeStateWidth(element.getInput().getType().getElementType());
    // Restrict this proof to complete, fixed-width arrays rooted at zero.
    // Unknown/out-of-range indices remain guarded by normal handle lowering;
    // partially clipped slices and runtime-selected lanes are not admitted.
    if (!base || base->staticID != root.static_state || base->offset != 0 ||
        !stride || !arrayWidth || *arrayWidth != root.bit_width || !width ||
        *width == 0 || *width > 64 || low > *stride || *width > *stride - low)
      return reject();
    recordLane({*stride, low, *width});
  });
  // Overlapping slots are also independent if their statements cannot both
  // execute. Require one occurrence of each origin in the same outlined
  // activation and no CFG path in either direction. Helpers, separate owners,
  // compiler copies, nested regions and repeated sites remain conservative.
  // The later periodic-ingress and at-most-once proofs still apply: this only
  // proves mutual exclusion within one activation (LRM 4.6 and 10.4.2).
  auto mutuallyExclusive = [&](uint64_t left, uint64_t right) {
    ArrayRef<sim::SimNBAEnqueueOp> a = originEnqueues[left];
    ArrayRef<sim::SimNBAEnqueueOp> b = originEnqueues[right];
    if (a.size() != 1 || b.size() != 1)
      return false;
    auto function = a.front()->getParentOfType<sim::SimFuncOp>();
    if (!function || function != b.front()->getParentOfType<sim::SimFuncOp>() ||
        a.front()->getParentRegion() != &function.getBody() ||
        b.front()->getParentRegion() != &function.getBody())
      return false;
    auto reaches = [](Block *from, Block *to) {
      SmallVector<Block *> pending{from};
      llvm::SmallPtrSet<Block *, 16> seen;
      while (!pending.empty()) {
        Block *current = pending.pop_back_val();
        if (current == to)
          return true;
        if (!seen.insert(current).second)
          continue;
        llvm::append_range(pending, current->getSuccessors());
      }
      return false;
    };
    return !reaches(a.front()->getBlock(), b.front()->getBlock()) &&
           !reaches(b.front()->getBlock(), a.front()->getBlock());
  };
  plan.independentSiteWrites.assign(plan.roots.size(), false);
  for (uint32_t root = 0; root != plan.roots.size(); ++root) {
    if (conflictingLanes[root] || lanes[root].size() < 2)
      continue;
    bool complete = llvm::all_of(plan.sites, [&](const auto &site) {
      return site.root != root ||
             lanes[root].contains(plan.siteSemanticOrigins.lookup(site.site));
    });
    if (!complete)
      continue;
    bool disjoint = true;
    for (const auto &left : lanes[root])
      for (const auto &right : lanes[root]) {
        if (left.first >= right.first)
          continue;
        const Lane &a = left.second;
        const Lane &b = right.second;
        disjoint &= (a.stride == b.stride &&
                     (a.low + a.width <= b.low || b.low + b.width <= a.low)) ||
                    mutuallyExclusive(left.first, right.first);
      }
    plan.independentSiteWrites[root] = disjoint;
  }

  // Prove which roots the barrier may merge to one last write per bit. This
  // is a property of the root's own sites, so it holds no matter which tier or
  // body later owns them.
  llvm::SmallVector<llvm::SmallDenseSet<uint64_t, 4>> originsByRoot(
      plan.roots.size());
  for (const obelisk_rt_static_nba_site &entry : plan.sites) {
    auto root = plan.siteRoots.find(entry.site);
    if (root == plan.siteRoots.end() || root->second >= plan.roots.size())
      continue;
    auto origin = plan.siteSemanticOrigins.find(entry.site);
    originsByRoot[root->second].insert(
        origin == plan.siteSemanticOrigins.end() ? entry.site : origin->second);
  }
  // The call closure is read-only; avoid one whole-design symbol scan per
  // call while deriving every root's merge proof.
  SymbolTableCollection callSymbols;
  llvm::SmallPtrSet<Operation *, 8> callees;
  module.walk([&](sim::SimCallOp call) {
    if (auto design = call->getParentOfType<sim::SimDesignOp>())
      if (sim::SimFuncOp callee = callSymbols.lookupSymbolIn<sim::SimFuncOp>(
              design, call.getCalleeAttr()))
        callees.insert(callee.getOperation());
  });
  llvm::DenseMap<Operation *, llvm::DenseMap<uint64_t, unsigned>> closureOps;
  auto closureSiteOpsFor = [&](sim::SimFuncOp function) -> auto & {
    auto [entry, inserted] = closureOps.try_emplace(function.getOperation());
    if (!inserted)
      return entry->second;
    sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
    SmallVector<sim::SimFuncOp> pending{function};
    llvm::SmallPtrSet<Operation *, 8> visited;
    while (!pending.empty()) {
      sim::SimFuncOp current = pending.pop_back_val();
      if (!visited.insert(current.getOperation()).second)
        continue;
      current.walk([&](sim::SimNBAEnqueueOp nested) {
        if (schedule::NBASiteAttr site = nested.getSiteAttr()) {
          auto origin = plan.siteSemanticOrigins.find(site.getId());
          ++entry->second[origin == plan.siteSemanticOrigins.end()
                              ? site.getId()
                              : origin->second];
        }
      });
      if (design)
        current.walk([&](sim::SimCallOp call) {
          if (sim::SimFuncOp callee =
                  callSymbols.lookupSymbolIn<sim::SimFuncOp>(
                      design, call.getCalleeAttr()))
            pending.push_back(callee);
        });
    }
    return entry->second;
  };
  plan.mergeSafeRoots.assign(plan.roots.size(), true);
  // Distinct disjoint writes are still distinct update events. An expression
  // spanning their bits can observe an intermediate value (LRM 9.4.2), so
  // a root needs one semantic NBA site unless an observer-specific proof is
  // available. The independent-lane proof alone is insufficient here.
  for (uint32_t root = 0; root != plan.roots.size(); ++root)
    if (originsByRoot[root].size() != 1)
      plan.mergeSafeRoots[root] = false;
  llvm::DenseMap<StringRef, unsigned> spawnCounts;
  llvm::SmallDenseSet<StringRef, 8> potentiallyRepeatedSpawns;
  module.walk([&](sim::SimSpawnOp spawn) {
    StringRef callee = spawn.getCallee();
    ++spawnCounts[callee];
    sim::SimFuncOp owner = spawn->getParentOfType<sim::SimFuncOp>();
    if (!owner || owner.getEntryKind() != sim::EntryKind::RootInitializer)
      potentiallyRepeatedSpawns.insert(callee);
    Operation *ownerOp = owner ? owner.getOperation() : nullptr;
    for (Operation *ancestor = spawn->getParentOp();
         ancestor && ancestor != ownerOp; ancestor = ancestor->getParentOp())
      if (isa<LoopLikeOpInterface>(ancestor))
        potentiallyRepeatedSpawns.insert(callee);
    Block *origin = spawn->getBlock();
    SmallVector<Block *, 8> pending;
    for (Block *successor : origin->getTerminator()->getSuccessors())
      pending.push_back(successor);
    llvm::SmallPtrSet<Block *, 16> visited;
    while (!pending.empty()) {
      Block *block = pending.pop_back_val();
      if (block == origin) {
        potentiallyRepeatedSpawns.insert(callee);
        break;
      }
      if (!visited.insert(block).second)
        continue;
      for (Block *successor : block->getTerminator()->getSuccessors())
        pending.push_back(successor);
    }
  });
  module.walk([&](sim::SimNBAEnqueueOp enqueue) {
    schedule::NBASiteAttr site = enqueue.getSiteAttr();
    auto mapped =
        site ? plan.siteRoots.find(site.getId()) : plan.siteRoots.end();
    if (mapped == plan.siteRoots.end() || mapped->second >= plan.roots.size())
      return;
    sim::SimFuncOp function = enqueue->getParentOfType<sim::SimFuncOp>();
    bool uniqueProcess =
        function &&
        (function.getEntryKind() == sim::EntryKind::RootInitializer ||
         (spawnCounts.lookup(function.getSymName()) == 1 &&
          !potentiallyRepeatedSpawns.contains(function.getSymName())));
    if (!uniqueProcess ||
        !nbaEnqueueExecutesAtMostOnce(enqueue, plan,
                                      closureSiteOpsFor(function), callees))
      plan.mergeSafeRoots[mapped->second] = false;
  });
  // A root whose intermediate NBA values nothing observes may merge any
  // number of updates, in any order relative to other roots.
  sim::SimDesignOp design;
  module.walk([&](sim::SimDesignOp candidate) {
    design = candidate;
    return WalkResult::interrupt();
  });
  analysis::NBAMergeSafety mergeSafety(design);
  SmallVector<bool> unobservable(plan.roots.size());
  plan.trackTransients.assign(plan.roots.size(), false);
  plan.changeWatchedRoots.assign(plan.roots.size(), false);
  for (auto [index, root] : llvm::enumerate(plan.roots)) {
    plan.changeWatchedRoots[index] =
        mergeSafety.commitNeedsTransients(root.commit_node);
    // A root seen only by change waits merges with a transient mask. Every
    // generated and runtime merge of a scalar root maintains that mask.
    plan.trackTransients[index] =
        root.bit_width <= OBELISK_RT_SCALAR_NBA_MAX_BITS &&
        mergeSafety.commitNeedsTransients(root.commit_node);
    unobservable[index] = mergeSafety.commitMayMerge(root.commit_node) ||
                          plan.trackTransients[index];
  }
  // The generated commit scans roots in layout order, while the runtime NBA
  // queue performs updates in enqueue order across all roots (LRM 4.6(b)).
  // Until generated replay has a global sequence, static staging of an
  // observable root is sound only when the design has one semantic NBA site
  // on an observable root. A generic site mixed with that root would likewise
  // lose the global order. Unobservable roots cannot reveal that order.
  llvm::SmallDenseSet<uint64_t, 4> globalOrigins;
  bool hasUnmappedEnqueue = false;
  module.walk([&](sim::SimNBAEnqueueOp enqueue) {
    schedule::NBASiteAttr site = enqueue.getSiteAttr();
    auto mapped =
        site ? plan.siteRoots.find(site.getId()) : plan.siteRoots.end();
    if (mapped != plan.siteRoots.end() && mapped->second < plan.roots.size() &&
        unobservable[mapped->second] && !enqueue.getClockingOutputAttr() &&
        !enqueue.getDelay() && !site.getTiming() &&
        site.getStorage() != schedule::ComputeNBAStorageKind::DynamicFrontier)
      return;
    if (!site || !plan.siteRoots.contains(site.getId()) ||
        enqueue.getClockingOutputAttr() ||
        static_cast<bool>(enqueue.getDelay()) || site.getTiming() ||
        site.getStorage() == schedule::ComputeNBAStorageKind::DynamicFrontier ||
        isa<sim::DriverType>(enqueue.getDestination().getType())) {
      hasUnmappedEnqueue = true;
      return;
    }
    auto origin = plan.siteSemanticOrigins.find(site.getId());
    globalOrigins.insert(origin == plan.siteSemanticOrigins.end()
                             ? site.getId()
                             : origin->second);
  });
  unsigned strictRoots = 0;
  for (uint32_t root = 0; root != plan.roots.size(); ++root)
    strictRoots += plan.mergeSafeRoots[root] && !unobservable[root];
  if (hasUnmappedEnqueue || globalOrigins.size() != 1 || strictRoots != 1)
    llvm::fill(plan.mergeSafeRoots, false);
  for (uint32_t root = 0; root != plan.roots.size(); ++root)
    plan.mergeSafeRoots[root] = plan.mergeSafeRoots[root] || unobservable[root];

  if (module->hasAttr("obelisk.debug.native_timing"))
    for (uint32_t root = 0; root != plan.roots.size(); ++root)
      if (!plan.mergeSafeRoots[root])
        llvm::errs() << "obelisk NBA root keeps ordered commits: root=" << root
                     << " width=" << plan.roots[root].bit_width
                     << " semantic-sites=" << originsByRoot[root].size()
                     << " independent-lanes="
                     << plan.independentSiteWrites[root] << '\n';
  // Prove the subset for which a dirty bit is also a complete generated-stage
  // validity proof. Fixed part-selects retain a write mask; roots shared
  // between event regions keep the existing accumulator field checks.
  SmallVector<bool> eligible(plan.roots.size(), true);
  SmallVector<bool> fullRoot(plan.roots.size(), true);
  SmallVector<bool> fixedMask(plan.roots.size(), true);
  SmallVector<uint64_t> writeMasks(plan.roots.size(), 0);
  SmallVector<bool> seen(plan.roots.size(), false);
  module.walk([&](sim::SimNBAEnqueueOp enqueue) {
    schedule::NBASiteAttr site = enqueue.getSiteAttr();
    auto mapped =
        site ? plan.siteRoots.find(site.getId()) : plan.siteRoots.end();
    if (mapped == plan.siteRoots.end())
      return;
    uint32_t rootIndex = mapped->second;
    const obelisk_rt_static_nba_root &root = plan.roots[rootIndex];
    seen[rootIndex] = true;
    std::optional<unsigned> width =
        nativeStateWidth(enqueue.getValue().getType());
    DenseSet<Value> active;
    std::optional<StaticNBADestination> destination =
        resolveStaticNBADestination(enqueue.getDestination(), stateLayout,
                                    active);
    sim::SimFuncOp function = enqueue->getParentOfType<sim::SimFuncOp>();
    uint32_t homeRegion =
        function ? getRuntimeEventRegion(function.getHomeRegion()) : UINT32_MAX;
    uint32_t commitRegion = homeRegion == OBELISK_RT_REGION_ACTIVE ||
                                    homeRegion == OBELISK_RT_REGION_REACTIVE
                                ? homeRegion + 2
                                : UINT32_MAX;
    bool direct =
        width && root.bit_width <= 64 && destination &&
        *width <= root.bit_width && destination->offset <= root.bit_width &&
        *width <= root.bit_width - destination->offset &&
        destination->staticID == root.static_state && !enqueue.getDelay() &&
        site && !site.getTiming() &&
        site.getStorage() != schedule::ComputeNBAStorageKind::DynamicFrontier &&
        commitRegion != UINT32_MAX;
    if (!direct) {
      eligible[rootIndex] = false;
      fullRoot[rootIndex] = false;
      return;
    }
    if (*width != root.bit_width || destination->offset != 0)
      fullRoot[rootIndex] = false;
    uint64_t sourceMask =
        *width == 64 ? UINT64_MAX : (uint64_t{1} << *width) - 1;
    uint64_t writeMask = sourceMask << destination->offset;
    if (writeMasks[rootIndex] == 0)
      writeMasks[rootIndex] = writeMask;
    else if (writeMasks[rootIndex] != writeMask)
      fixedMask[rootIndex] = false;
    uint32_t &plannedRegion = plan.generatedCommitRegions[rootIndex];
    if (plannedRegion == UINT32_MAX)
      plannedRegion = commitRegion;
    else if (plannedRegion != commitRegion)
      eligible[rootIndex] = false;
  });
  for (uint32_t root = 0; root != plan.roots.size(); ++root)
    if (!eligible[root] || !seen[root]) {
      plan.generatedCommitRegions[root] = UINT32_MAX;
      plan.generatedFullRootStages[root] = false;
      plan.generatedFixedWriteMasks[root] = 0;
    } else {
      plan.generatedFullRootStages[root] = fullRoot[root];
      plan.generatedFixedWriteMasks[root] =
          fixedMask[root] ? writeMasks[root] : 0;
    }
  return plan;
}

} // namespace obelisk::detail
