//===- FuseComputeFragments.cpp - Plan static AOT execution batches ------===//

#include "ComputeFusion.h"

#include "obelisk/Dialect/Simulation/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMFUSECOMPUTEFRAGMENTSPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {

class ObeliskSimFuseComputeFragmentsPass final
    : public impl::ObeliskSimFuseComputeFragmentsPassBase<
          ObeliskSimFuseComputeFragmentsPass> {
public:
  using Base = impl::ObeliskSimFuseComputeFragmentsPassBase<
      ObeliskSimFuseComputeFragmentsPass>;
  using Base::Base;
  ObeliskSimFuseComputeFragmentsPass(
      const ObeliskSimFuseComputeFragmentsPass &other)
      : Base(other) {}

  void runOnOperation() override;

private:
  Statistic plannedFusions{this, "planned-fusions",
                           "certified process-body fusions planned"};
  Statistic rejectedActors{
      this, "rejected-actors",
      "native direct-wait actors rejected by body eligibility"};
};

struct FusionCandidate {
  int64_t fragment;
  int64_t resumeTarget;
  uint32_t order;
  uint32_t entryOrder;
  Operation *function;
  uint64_t instanceScope;
};

bool isStraightLineContinuous(sim::SimFuncOp function, bool eligible,
                              bool primitiveDriverOps) {
  if (!function || function.getEntryKind() != sim::EntryKind::Continuous ||
      function.getBody().getBlocks().size() != 2 || !eligible)
    return false;
  Block &entry = function.getBody().front();
  Block &body = function.getBody().back();
  auto branch = dyn_cast<cf::BranchOp>(entry.getTerminator());
  if (!branch || branch.getDest() != &body ||
      branch.getDestOperands().size() != body.getNumArguments())
    return false;
  if (!primitiveDriverOps &&
      (!branch.getDestOperands().empty() || body.getNumArguments() != 0))
    return false;
  auto hasCompatibleState = [&](Operation *suspend) {
    auto branch = cast<BranchOpInterface>(suspend);
    OperandRange forwarded =
        branch.getSuccessorOperands(0).getForwardedOperands();
    return forwarded.size() == body.getNumArguments() &&
           llvm::all_of(llvm::zip_equal(body.getArguments(), forwarded),
                        [](auto pair) {
                          return std::get<0>(pair).getType() ==
                                 std::get<1>(pair).getType();
                        });
  };
  Operation *terminator = body.getTerminator();
  if (auto change = dyn_cast<sim::SimSuspendChangeOp>(terminator))
    return change.getContinuation() == &body && hasCompatibleState(change);
  if (auto any = dyn_cast<sim::SimSuspendAnyOp>(terminator))
    return any.getContinuation() == &body && hasCompatibleState(any) &&
           llvm::all_of(any.getEdges(), [](int32_t edge) {
             return edge == static_cast<int32_t>(sim::EdgeKind::Change);
           });
  return false;
}

void ObeliskSimFuseComputeFragmentsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  if (bodyFusion &&
      (maxStraightLineMembers < 2 || maxStraightLineMembers > 64)) {
    design.emitOpError(
        "straight-line fusion member limit must be between 2 and 64");
    return signalPassFailure();
  }
  ModuleOp module = design->getParentOfType<ModuleOp>();
  // The default auto request has its own large-primitive bytecode guard.
  // Preserve that representation instead of rewriting those actors before
  // the graph's final tier partition is selected. Explicit native execution
  // and scheduler requests do not carry this marker.
  if (primitiveOnly &&
      module->hasAttr("obelisk.native_scheduler.auto_requested"))
    return;
  auto scheduler = module->getAttrOfType<sim::NativeSchedulerModeAttr>(
      "obelisk.native_scheduler");
  bool evalBodyFusion = bodyFusion && scheduler &&
                        scheduler.getValue() == sim::NativeSchedulerMode::Eval;
  StringRef metadataName = bodyFusion ? sim::metadata::staticBodyFusion
                                      : sim::metadata::staticFusion;
  design->removeAttr(metadataName);
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph || graph.getWorkers() != 1)
    return;

  ArrayAttr nodes = graph.getNodes();
  llvm::StringMap<sim::SimFuncOp> functions;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>())
    functions.try_emplace(function.getSymName(), function);
  auto lookupFunction = [&](FlatSymbolRefAttr reference) {
    auto found = functions.find(reference.getValue());
    return found == functions.end() ? sim::SimFuncOp{} : found->second;
  };
  DenseMap<Operation *, bool> bodyEligibility;
  DenseMap<Operation *, bool> primitiveBodyEligibility;
  analysis::DescriptorProvenanceAnalysis provenance(design);
  auto isBodyEligible = [&](sim::SimFuncOp function, bool primitive) {
    if (!function)
      return false;
    auto &cache = primitive ? primitiveBodyEligibility : bodyEligibility;
    auto found = cache.find(function.getOperation());
    if (found != cache.end())
      return found->second;
    bool eligible =
        primitive ? isPrimitiveComputeBodyFusionEligible(function, provenance)
                  : isComputeBodyFusionEligible(function, provenance);
    cache.try_emplace(function.getOperation(), eligible);
    return eligible;
  };
  // Partition planning may assign different blocks of one process body to
  // different tiers. Physical body fusion removes the entire original
  // function, so primitive-only fusion is legal only when every fragment of
  // that actor remains native. In particular, preserve the auto policy's
  // bytecode demotion even when a small entry fragment was promoted.
  DenseMap<Operation *, bool> entirelyNative;
  for (Attribute attribute : nodes) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment)
      continue;
    sim::SimFuncOp function = lookupFunction(fragment.getFunction());
    if (!function)
      continue;
    auto entry =
        entirelyNative.try_emplace(function.getOperation(), true).first;
    if (fragment.getTier() != sim::ComputeTierKind::Native)
      entry->second = false;
  }
  llvm::SmallDenseSet<int64_t> acyclicActive;
  DenseMap<int64_t, uint32_t> scheduleOrder;
  DenseMap<int64_t, int64_t> resumeTargets;
  uint32_t nextOrder = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    if (region.getKind() != sim::ComputeRegionKind::Active)
      continue;
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = cast<sim::ComputeGroupAttr>(groupAttribute);
      for (int64_t member : group.getFragments().asArrayRef()) {
        scheduleOrder.try_emplace(member, nextOrder++);
        if (group.getSchedule() == sim::ComputeScheduleKind::Acyclic)
          acyclicActive.insert(member);
      }
    }
  }
  for (Attribute edgeAttribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(edgeAttribute);
    if (edge.getKind() == sim::ComputeEdgeKind::Resume)
      resumeTargets.try_emplace(edge.getSource(), edge.getTarget());
  }
  SmallVector<int64_t> entryTargets;
  for (Attribute edgeAttribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(edgeAttribute);
    if (edge.getKind() == sim::ComputeEdgeKind::Spawn &&
        scheduleOrder.contains(edge.getTarget()))
      entryTargets.push_back(edge.getTarget());
  }
  llvm::sort(entryTargets, [&](int64_t lhs, int64_t rhs) {
    return scheduleOrder.at(lhs) < scheduleOrder.at(rhs);
  });
  entryTargets.erase(std::unique(entryTargets.begin(), entryTargets.end()),
                     entryTargets.end());
  DenseMap<Operation *, uint32_t> entryOrder;
  for (auto [order, target] : llvm::enumerate(entryTargets)) {
    auto fragment = target >= 0 && static_cast<uint64_t>(target) < nodes.size()
                        ? dyn_cast<sim::ComputeFragmentAttr>(
                              nodes[static_cast<size_t>(target)])
                        : sim::ComputeFragmentAttr{};
    sim::SimFuncOp function =
        fragment ? lookupFunction(fragment.getFunction()) : sim::SimFuncOp{};
    if (function)
      entryOrder.try_emplace(function.getOperation(),
                             static_cast<uint32_t>(order));
  }

  DenseMap<uint64_t, uint64_t> codeUnitScopes;
  for (sim::SimCodeUnitDeclOp declaration :
       design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
    codeUnitScopes.try_emplace(declaration.getId(), declaration.getScopeId());
  auto getInstanceScope =
      [&](sim::SimFuncOp function) -> std::optional<uint64_t> {
    std::optional<uint64_t> codeUnit = function.getCodeUnitId();
    if (!codeUnit)
      return std::nullopt;
    auto scope = codeUnitScopes.find(*codeUnit);
    if (scope == codeUnitScopes.end())
      return std::nullopt;
    return scope->second;
  };

  using FusionKey = std::pair<Attribute, uint64_t>;
  llvm::MapVector<FusionKey, SmallVector<FusionCandidate>> bySensitivity;
  for (auto [index, attribute] : llvm::enumerate(nodes)) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment || fragment.getId() != index ||
        !acyclicActive.contains(static_cast<int64_t>(index)) ||
        fragment.getTier() != sim::ComputeTierKind::Native ||
        fragment.getRegion() != sim::ComputeRegionKind::Active ||
        (fragment.getAction() != sim::ComputeActionKind::SuspendChange &&
         fragment.getAction() != sim::ComputeActionKind::SuspendEdge))
      continue;

    sim::ComputeEffectAttr sensitivity;
    bool unsupported = false;
    for (Attribute effectAttribute : fragment.getEffects()) {
      auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
      if (effect.getEffect() != sim::ComputeEffectKind::Watch)
        continue;
      if (sensitivity) {
        unsupported = true;
        break;
      }
      sensitivity = effect;
    }
    if (unsupported || !sensitivity ||
        sensitivity.getTarget() != sim::ComputeTargetKind::Descriptor ||
        (sensitivity.getResource() != sim::ComputeResourceKind::Storage &&
         sensitivity.getResource() != sim::ComputeResourceKind::Net) ||
        sensitivity.getDynamic() || sensitivity.getDeferred() ||
        sensitivity.getWidth() == 0)
      continue;

    sim::SimFuncOp function = lookupFunction(fragment.getFunction());
    if (primitiveOnly &&
        (!function || function.getEntryKind() != sim::EntryKind::Continuous ||
         !function->hasAttr("obelisk_sim.primitive_name") ||
         !entirelyNative.lookup(function.getOperation())))
      continue;
    // Straight-line continuous actors have their own union-wait kernel
    // planner below.  Emitting them here as well when several share an exact
    // sensitivity would create overlapping fusion records for one actor.
    if (bodyFusion && primitiveOnly &&
        function.getEntryKind() == sim::EntryKind::Continuous)
      continue;
    if (!function || (bodyFusion && !isBodyEligible(function, false))) {
      if (function)
        ++rejectedActors;
      continue;
    }
    auto resume = resumeTargets.find(static_cast<int64_t>(index));
    if (resume == resumeTargets.end())
      continue;
    auto functionEntry = entryOrder.find(function.getOperation());
    if (functionEntry == entryOrder.end())
      continue;
    std::optional<uint64_t> instanceScope = getInstanceScope(function);
    if (bodyFusion && !instanceScope) {
      ++rejectedActors;
      continue;
    }

    bySensitivity[{sensitivity, bodyFusion ? *instanceScope : 0}].push_back(
        {static_cast<int64_t>(index), resume->second, 0, functionEntry->second,
         function.getOperation(), bodyFusion ? *instanceScope : 0});
  }

  SmallVector<Attribute> fusions;
  uint32_t id = 0;
  for (auto &[key, candidates] : bySensitivity) {
    Attribute sensitivity = key.first;
    if (candidates.size() < 2)
      continue;
    // Body fusion chooses a stable, legal Active ordering for this cohort;
    // unrelated ready actors need not remain between its members. Keep the
    // adjacency inventory only for fragment batches, which retain the old
    // dispatch order. The materializer certifies the indivisible activation.
    SmallVector<uint32_t> readyTargets;
    if (!bodyFusion)
      readyTargets = getComputeFusionReadyTargets(
          graph, cast<sim::ComputeEffectAttr>(sensitivity));
    llvm::erase_if(readyTargets, [&](uint32_t target) {
      return !scheduleOrder.contains(target);
    });
    llvm::sort(readyTargets, [&](uint32_t lhs, uint32_t rhs) {
      return scheduleOrder.at(lhs) < scheduleOrder.at(rhs);
    });
    readyTargets.erase(std::unique(readyTargets.begin(), readyTargets.end()),
                       readyTargets.end());
    DenseMap<int64_t, uint32_t> readyOrder;
    for (auto [order, target] : llvm::enumerate(readyTargets))
      readyOrder.try_emplace(target, static_cast<uint32_t>(order));
    llvm::erase_if(candidates, [&](FusionCandidate &candidate) {
      if (bodyFusion) {
        auto order = scheduleOrder.find(candidate.resumeTarget);
        if (order == scheduleOrder.end())
          return true;
        candidate.order = order->second;
        return false;
      }
      auto order = readyOrder.find(candidate.resumeTarget);
      if (order == readyOrder.end())
        return true;
      candidate.order = order->second;
      return false;
    });
    if (candidates.size() < 2)
      continue;
    llvm::sort(candidates,
               [](const FusionCandidate &lhs, const FusionCandidate &rhs) {
                 return std::tie(lhs.order, lhs.fragment) <
                        std::tie(rhs.order, rhs.fragment);
               });
    SmallVector<int64_t> fragments;
    llvm::SmallDenseSet<Operation *> functions;
    auto flush = [&] {
      if (fragments.size() >= 2) {
        fusions.push_back(sim::ComputeFusionAttr::get(
            design.getContext(), id++,
            DenseI64ArrayAttr::get(design.getContext(), fragments)));
        ++plannedFusions;
      }
      fragments.clear();
      functions.clear();
    };
    std::optional<uint32_t> previousOrder;
    std::optional<uint32_t> previousEntryOrder;
    for (const FusionCandidate &candidate : candidates) {
      bool adjacent =
          previousOrder && previousEntryOrder &&
          candidate.order == static_cast<uint64_t>(*previousOrder) + 1 &&
          (bodyFusion || candidate.entryOrder ==
                             static_cast<uint64_t>(*previousEntryOrder) + 1);
      if ((!fragments.empty() && !adjacent && !bodyFusion) ||
          functions.contains(candidate.function))
        flush();
      fragments.push_back(candidate.fragment);
      functions.insert(candidate.function);
      previousOrder = candidate.order;
      previousEntryOrder = candidate.entryOrder;
    }
    flush();
  }
  if (bodyFusion) {
    CombinationalFusionAnalysis combinational(design, provenance);
    using CohortKey = std::pair<uint64_t, Attribute>;
    llvm::MapVector<CohortKey, SmallVector<int64_t>> continuousByScope;
    llvm::SmallDenseSet<Operation *> seen;
    for (auto [index, attribute] : llvm::enumerate(nodes)) {
      auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
      if (!fragment || fragment.getTier() != sim::ComputeTierKind::Native ||
          (fragment.getAction() != sim::ComputeActionKind::SuspendChange &&
           fragment.getAction() != sim::ComputeActionKind::SuspendAny))
        continue;
      sim::SimFuncOp function = lookupFunction(fragment.getFunction());
      bool combinationalBody =
          !primitiveOnly && function &&
          function.getEntryKind() == sim::EntryKind::AlwaysComb &&
          entirelyNative.lookup(function.getOperation()) &&
          combinational.analyze(function, provenance).has_value();
      if (!function || seen.contains(function.getOperation()) ||
          (!combinationalBody &&
           !isStraightLineContinuous(function,
                                     isBodyEligible(function, primitiveOnly),
                                     primitiveOnly)) ||
          (primitiveOnly && (!function->hasAttr("obelisk_sim.primitive_name") ||
                             !entirelyNative.lookup(function.getOperation()))))
        continue;
      seen.insert(function.getOperation());
      std::optional<uint64_t> instanceScope = getInstanceScope(function);
      if ((evalBodyFusion || primitiveOnly || combinationalBody) &&
          !instanceScope) {
        ++rejectedActors;
        continue;
      }
      auto resume = resumeTargets.find(static_cast<int64_t>(index));
      // Primitive cohorts may share a conservative convergence group solely
      // because bit-sliced drivers touch one packed descriptor. The generated
      // kernel propagates exact forward sensitivity edges through its dirty
      // mask; materialization rejects every backward edge, and therefore any
      // internal cycle, while cross-chunk edges remain visible to the outer
      // convergence schedule. Keep the older acyclic-only profitability rule
      // for general actors.
      if (resume == resumeTargets.end() ||
          (!primitiveOnly && !combinationalBody &&
           !acyclicActive.contains(resume->second)))
        continue;
      Attribute primitive =
          primitiveOnly ? function->getAttr("obelisk_sim.primitive_name")
                        : Attribute{};
      if (combinationalBody)
        primitive = function.getEntryKindAttr();
      continuousByScope[{evalBodyFusion || primitiveOnly || combinationalBody
                             ? *instanceScope
                             : 0,
                         primitive}]
          .push_back(static_cast<int64_t>(index));
    }
    DenseMap<StringAttr, SmallVector<StringAttr>> sensitivityTargets;
    bool haveRankedCohort =
        llvm::any_of(continuousByScope, [](const auto &entry) {
          return isa_and_nonnull<sim::EntryKindAttr>(entry.first.second) &&
                 entry.second.size() >= 2;
        });
    if (haveRankedCohort)
      for (Attribute attribute : graph.getEdges()) {
        auto edge = cast<sim::ComputeEdgeAttr>(attribute);
        if (edge.getKind() != sim::ComputeEdgeKind::Sensitivity)
          continue;
        auto source = cast<sim::ComputeFragmentAttr>(nodes[edge.getSource()]);
        auto target = cast<sim::ComputeFragmentAttr>(nodes[edge.getTarget()]);
        sensitivityTargets[source.getFunction().getAttr()].push_back(
            target.getFunction().getAttr());
      }
    for (auto &[key, continuous] : continuousByScope) {
      llvm::sort(continuous, [&](int64_t lhs, int64_t rhs) {
        return std::tie(scheduleOrder[resumeTargets.lookup(lhs)], lhs) <
               std::tie(scheduleOrder[resumeTargets.lookup(rhs)], rhs);
      });
      auto inlineLimit =
          module->getAttrOfType<IntegerAttr>("obelisk.native.max_inline_ops");
      uint64_t budget = inlineLimit ? inlineLimit.getUInt() : 5000;
      bool rankedCombinational =
          isa_and_nonnull<sim::EntryKindAttr>(key.second);
      DenseMap<StringAttr, size_t> order;
      if (rankedCombinational)
        for (auto [index, member] : llvm::enumerate(continuous))
          order[cast<sim::ComputeFragmentAttr>(nodes[member])
                    .getFunction()
                    .getAttr()] = index;
      for (size_t offset = 0; offset < continuous.size();) {
        size_t count = std::min<size_t>(maxStraightLineMembers,
                                        continuous.size() - offset);
        if (rankedCombinational) {
          uint64_t cost = 0;
          for (size_t index = 0; index < count; ++index) {
            auto fragment = cast<sim::ComputeFragmentAttr>(
                nodes[continuous[offset + index]]);
            // Cut a backward edge at a shared-loop boundary instead of
            // excluding every actor in a conservative SCC. Each resulting
            // kernel is forward-only; inter-kernel feedback remains ordinary
            // publication/reactivation under the same event loop.
            bool backward = false;
            for (StringAttr target :
                 sensitivityTargets[fragment.getFunction().getAttr()]) {
              auto found = order.find(target);
              backward |= found != order.end() && found->second >= offset &&
                          found->second <= offset + index;
            }
            uint64_t next = analysis::getSimulationOperationCost(
                lookupFunction(fragment.getFunction()).getOperation());
            if (backward || (budget && next > budget - cost)) {
              count = std::max<size_t>(1, index);
              break;
            }
            if (budget)
              cost += next;
          }
        }
        ArrayRef<int64_t> chunk =
            ArrayRef<int64_t>(continuous).slice(offset, count);
        offset += count;
        if (chunk.size() < 2)
          continue;
        fusions.push_back(sim::ComputeFusionAttr::get(
            design.getContext(), id++,
            DenseI64ArrayAttr::get(design.getContext(), chunk)));
        ++plannedFusions;
      }
    }
  }
  if (!fusions.empty())
    design->setAttr(metadataName, ArrayAttr::get(design.getContext(), fusions));
  // Local cohort fusion is the only Tier-1 coarsening that does not require
  // whole-design admission, so its planned membership is the figure that
  // decides whether a design is losing Tier-1 work to admission or to fusion.
  // Report the distribution, not just the pass timing.
  if (module->hasAttr("obelisk.debug.native_timing")) {
    uint64_t members = 0;
    size_t largest = 0;
    for (Attribute attribute : fusions) {
      size_t size =
          cast<sim::ComputeFusionAttr>(attribute).getFragments().size();
      members += size;
      largest = std::max(largest, size);
    }
    llvm::errs() << "obelisk fusion planning:"
                 << " body=" << bodyFusion << " primitive=" << primitiveOnly
                 << " eval_body=" << evalBodyFusion
                 << " groups=" << fusions.size() << " members=" << members
                 << " largest=" << largest
                 << " rejected_actors=" << rejectedActors.getValue() << '\n';
  }
}

} // namespace
} // namespace obelisk
