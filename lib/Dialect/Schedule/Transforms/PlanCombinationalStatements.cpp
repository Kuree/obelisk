//===- PlanCombinationalStatements.cpp - Statement DAG fusion ===//
#include "ComputeFusion.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Analysis/GraphAlgorithms.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "llvm/ADT/IntEqClasses.h"
#include "llvm/ADT/MapVector.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_PLANCOMBINATIONALSTATEMENTSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {
enum class ComponentRejection {
  None,
  MemberBudget,
  OperationBudget,
  Observer,
  StructuredDependenceCycle,
  DataDependenceCycle,
  SliceConstruction,
  ReorderObserver,
};

std::string
stringifyComponentRejection(ComponentRejection reason,
                            const CombinationalFusionFailure &observerFailure) {
  switch (reason) {
  case ComponentRejection::None:
    return {};
  case ComponentRejection::MemberBudget:
    return "member budget";
  case ComponentRejection::OperationBudget:
    return "operation budget";
  case ComponentRejection::Observer:
    return observerFailure.getMessage();
  case ComponentRejection::StructuredDependenceCycle:
    return "structured dependence cycle";
  case ComponentRejection::DataDependenceCycle:
    return "data dependence cycle";
  case ComponentRejection::SliceConstruction:
    return "slice construction failed";
  case ComponentRejection::ReorderObserver:
    return "statement order required: " + observerFailure.getMessage();
  }
  llvm_unreachable("unknown statement component rejection");
}

class PlanCombinationalStatementsPass final
    : public impl::PlanCombinationalStatementsPassBase<
          PlanCombinationalStatementsPass> {
public:
  using Base::Base;
  Statistic examined{this, "examined",
                     "Native change-sensitive owners examined"};
  Statistic admitted{this, "admitted",
                     "Owners with complete activation proofs"};
  Statistic componentsFound{this, "components", "Nontrivial candidate SCCs"};
  Statistic plans{this, "plans", "Source-ordered DAGs planned"};
  Statistic dataflowPlans{
      this, "dataflow-plans",
      "Data-dependence orders planned (IEEE 1800-2023 4.3)"};
  void runOnOperation() override {
    auto design = getOperation();
    auto graph = design.getComputeGraphAttr();
    if (!graph || graph.getWorkers() != 1 ||
        graph.getVpi() == schedule::ComputeVPIMode::Full)
      return;
    bool trace =
        native::has<native::DebugTiming>(design->getParentOfType<ModuleOp>());
    if (maxMembers == 1) {
      design.emitOpError(
          "statement fusion member limit must be 0 or at least 2");
      return signalPassFailure();
    }
    // Fusion plans are consumed as a transaction. Never append a plan that
    // might overlap an existing transaction's owners.
    if (schedule::has<schedule::metadata::staticBodyFusion>(design))
      return;
    analysis::HandleDataflowAnalysis handles(design);
    CombinationalFusionAnalysis eligibility(
        design, handles,
        analysis::ActivationMemoryAnalysis::DependencyMode::ReachingValues);
    SymbolTable symbols(design);
    SmallVector<sim::SimFuncOp> candidates;
    SmallVector<int64_t> fragments;
    DenseMap<Operation *, unsigned> indices;
    DenseSet<Operation *> requiresReordering;
    SmallVector<const analysis::ActivationRegionAnalysis *> activations;
    for (Attribute raw : graph.getNodes()) {
      auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(raw);
      if (!fragment ||
          fragment.getTier() != schedule::ComputeTierKind::Native ||
          (fragment.getAction() != schedule::ComputeActionKind::SuspendChange &&
           fragment.getAction() != schedule::ComputeActionKind::SuspendAny))
        continue;
      auto function =
          symbols.lookup<sim::SimFuncOp>(fragment.getFunction().getValue());
      if (!function || indices.contains(function))
        continue;
      ++examined;
      CombinationalFusionFailure reason;
      auto body =
          eligibility.analyze(function, handles, /*interleaved=*/true, &reason);
      if (!body &&
          (reason.reason == CombinationalFusionFailure::Reason::UnwatchedLiveIn ||
           reason.reason ==
               CombinationalFusionFailure::Reason::IncomingOutputState)) {
        body = eligibility.analyzeForReordering(function, handles, &reason);
        if (body)
          requiresReordering.insert(function);
      }
      if (!body || !body->structured) {
        if (explain)
          function.emitRemark()
              << "statement scheduling rejected: " << reason.getMessage()
              << "; owner=" << function.getSymName();
        continue;
      }
      ++admitted;
      activations.push_back(body->structured);
      indices[function] = candidates.size();
      candidates.push_back(function);
      fragments.push_back(fragment.getId());
    }
    if (candidates.size() < 2)
      return;

    SmallVector<unsigned> owners;
    DenseMap<unsigned, SmallVector<unsigned>> adjacency;
    using Range = analysis::ActivationMemoryAnalysis::Range;
    DenseMap<uint64_t, SmallVector<std::pair<unsigned, Range>>> readers;
    for (unsigned owner = 0; owner < candidates.size(); ++owner) {
      owners.push_back(owner);
      for (const auto &read : activations[owner]->getMemory().getLiveIns())
        readers[read.root].emplace_back(owner, read);
    }
    // Sensitivity is also an activation dependency (IEEE 1800-2023 9.4.2).
    // An aggregate watch can include lanes the body does not read. Ignoring
    // it here but retaining it in native ordering can create a cycle around
    // the newly fused owner. Include these edges when closing cohorts; the
    // publication proof still decides whether that coarse cycle can be sliced.
    for (Attribute raw : graph.getNodes()) {
      auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(raw);
      if (!fragment)
        continue;
      auto function =
          symbols.lookup<sim::SimFuncOp>(fragment.getFunction().getValue());
      auto owner = indices.find(function);
      if (owner == indices.end())
        continue;
      for (auto effect :
           fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
        if (effect.getEffect() != schedule::ComputeEffectKind::Watch ||
            effect.getTarget() != schedule::ComputeTargetKind::Descriptor ||
            !effect.getWidth())
          continue;
        if (auto root = analysis::ActivationMemoryAnalysis::getRoot(
                effect.getResource(), effect.getDescriptor()))
          readers[*root].emplace_back(
              owner->second, Range{*root, effect.getLow(), effect.getWidth()});
      }
    }
    for (unsigned owner = 0; owner < candidates.size(); ++owner)
      for (const auto &write : activations[owner]->getMemory().getMayWrites())
        for (const auto &[consumer, read] : readers[write.root])
          if (consumer != owner && write.low < read.low + read.width &&
              read.low < write.low + write.width)
            adjacency[owner].push_back(consumer);
    for (auto &[owner, targets] : adjacency) {
      llvm::sort(targets);
      targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    }
    auto components = analysis::computeStronglyConnectedComponents<unsigned>(
        owners, adjacency);
    // Close each cyclic cohort over eligible observers of its intermediate
    // publications. The new observers may publish intermediate values too;
    // a worklist computes that transitive closure. Merge overlapping cohorts
    // before planning, so no owner can appear in two transactions.
    DenseMap<uint64_t, SmallVector<std::pair<unsigned, Range>>> watchers;
    for (Attribute raw : graph.getNodes()) {
      auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(raw);
      if (!fragment)
        continue;
      auto function =
          symbols.lookup<sim::SimFuncOp>(fragment.getFunction().getValue());
      auto found = indices.find(function);
      if (found == indices.end())
        continue;
      for (auto effect :
           fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>())
        if (effect.getEffect() == schedule::ComputeEffectKind::Watch &&
            (effect.getResource() == schedule::ComputeResourceKind::Storage ||
             effect.getResource() == schedule::ComputeResourceKind::Net) &&
            effect.getTarget() == schedule::ComputeTargetKind::Descriptor &&
            effect.getWidth() &&
            effect.getLow() <= UINT64_MAX - effect.getWidth()) {
          auto root = analysis::ActivationMemoryAnalysis::getRoot(
              effect.getResource(), effect.getDescriptor());
          if (!root)
            continue;
          watchers[*root].push_back(
              {found->second, {*root, effect.getLow(), effect.getWidth()}});
        }
    }
    using Cohorts = llvm::MapVector<unsigned, SmallVector<unsigned>>;
    // Each seed is a set of owners that must share one transaction.
    auto closeCohorts = [&](ArrayRef<SmallVector<unsigned>> seeds) {
      llvm::IntEqClasses cohorts(candidates.size());
      SmallVector<unsigned> pending;
      DenseSet<unsigned> included;
      auto include = [&](unsigned owner) {
        if (included.insert(owner).second)
          pending.push_back(owner);
      };
      for (const auto &seed : seeds)
        for (unsigned owner : seed) {
          cohorts.join(seed.front(), owner);
          include(owner);
        }
      size_t cursor = 0;
      while (true) {
        for (; cursor < pending.size(); ++cursor) {
          unsigned owner = pending[cursor];
          for (const auto &write :
               activations[owner]->getMemory().getRepeatedWrites())
            for (const auto &[consumer, watch] : watchers[write.root])
              if (write.low < watch.low + watch.width &&
                  watch.low < write.low + write.width) {
                cohorts.join(owner, consumer);
                include(consumer);
              }
        }
        // Observer closure can join nonadjacent components: A and C belong to
        // one cohort, while A -> B -> C still leaves B outside. Contracting
        // that cohort would introduce a kernel -> B -> kernel cycle. Close SCCs
        // of the quotient graph, then include the new members' observers too.
        SmallVector<unsigned> quotientOwners;
        DenseMap<unsigned, SmallVector<unsigned>> quotientEdges;
        for (unsigned owner : owners)
          quotientOwners.push_back(cohorts.findLeader(owner));
        llvm::sort(quotientOwners);
        quotientOwners.erase(
            std::unique(quotientOwners.begin(), quotientOwners.end()),
            quotientOwners.end());
        for (const auto &[owner, targets] : adjacency) {
          unsigned from = cohorts.findLeader(owner);
          for (unsigned target : targets) {
            unsigned to = cohorts.findLeader(target);
            if (from != to)
              quotientEdges[from].push_back(to);
          }
        }
        for (auto &[owner, targets] : quotientEdges) {
          llvm::sort(targets);
          targets.erase(std::unique(targets.begin(), targets.end()),
                        targets.end());
        }
        bool merged = false;
        for (const auto &component :
             analysis::computeStronglyConnectedComponents<unsigned>(
                 quotientOwners, quotientEdges)) {
          if (component.size() < 2)
            continue;
          merged = true;
          for (unsigned leader : component) {
            cohorts.join(component.front(), leader);
            // Every nonsingleton already had all members in the worklist.
            // Newly absorbed singleton components still need observer closure.
            include(leader);
          }
        }
        if (!merged)
          break;
      }
      Cohorts closed;
      for (unsigned owner : owners)
        if (included.contains(owner))
          closed[cohorts.findLeader(owner)].push_back(owner);
      return closed;
    };
    SmallVector<SmallVector<unsigned>> cyclic;
    for (const auto &component : components)
      if (component.size() > 1) {
        ++componentsFound;
        cyclic.emplace_back(component.begin(), component.end());
      }
    Cohorts closedComponents = closeCohorts(cyclic);
    // IEEE 1800-2023 4.3: an update event is a change in state, so another
    // evaluation of an activation that copies its retained state back
    // unchanged (the admission proof above) creates none when its inputs are
    // unchanged. 4.4.2.2, 4.7: the Active region evaluates the processes an
    // update wakes in any order, so running each activation after its
    // producers is a reference execution (4.5). A cohort may therefore absorb
    // the acyclic activations connected to it and run them once per wake in
    // data order. Admission below still decides every grown cohort exactly as
    // it decides a closed one. Growth only picks members likely to pass it:
    // owners whose writes nothing but combinational processes observe, since
    // a data-dependence order needs that certificate for every member. An
    // owner that wakes on an edge-triggered root, typically a clock, would
    // wake the whole cohort on both edges, so it stays out as a cost choice.
    // A path cohort -> X -> cohort through such an owner X would make the
    // closure absorb X, so the owners on such paths stay out as well.
    Cohorts grownComponents = closedComponents;
    if (growAcyclic) {
      llvm::BitVector seeded(candidates.size());
      for (const auto &[leader, component] : closedComponents)
        for (unsigned owner : component)
          seeded.set(owner);
      DenseMap<uint64_t, SmallVector<Range>> edgeWatched;
      for (Attribute raw : graph.getNodes())
        if (auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(raw))
          for (auto effect : fragment.getEffects()
                                 .getAsRange<schedule::ComputeEffectAttr>()) {
            auto trigger = effect.getTrigger();
            if (effect.getEffect() != schedule::ComputeEffectKind::Watch ||
                (trigger != schedule::ComputeTriggerKind::Posedge &&
                 trigger != schedule::ComputeTriggerKind::Negedge &&
                 trigger != schedule::ComputeTriggerKind::Both) ||
                effect.getTarget() != schedule::ComputeTargetKind::Descriptor)
              continue;
            if (auto root = analysis::ActivationMemoryAnalysis::getRoot(
                    effect.getResource(), effect.getDescriptor()))
              edgeWatched[*root].push_back(
                  {*root, effect.getLow(), effect.getWidth()});
          }
      llvm::BitVector blocked(candidates.size());
      for (const auto &[root, ranges] : edgeWatched) {
        auto found = watchers.find(root);
        if (found == watchers.end())
          continue;
        for (const auto &entry : found->second) {
          const Range &watch = entry.second;
          if (llvm::any_of(ranges, [&](const Range &edge) {
                return !edge.width || (watch.low < edge.low + edge.width &&
                                       edge.low < watch.low + watch.width);
              }))
            blocked.set(entry.first);
        }
      }
      blocked.reset(seeded);
      unsigned clockWakes = blocked.count();
      for (unsigned owner : owners)
        if (!blocked.test(owner) && !seeded.test(owner) &&
            !eligibility.canReorder({candidates[owner]}))
          blocked.set(owner);
      DenseMap<unsigned, SmallVector<unsigned>> predecessors;
      for (const auto &[owner, targets] : adjacency)
        for (unsigned target : targets)
          predecessors[target].push_back(owner);
      // Descendants of a blocked owner stay out; a blocked owner that feeds a
      // seed keeps its ancestors out as well. Paths need not continue through
      // a seed: each seed cohort is closed, so no blocked owner lies on a
      // path from a seed cohort back to itself. Two seed cohorts can still
      // meet around a blocked owner. The closure then absorbs it, and the
      // grown cohort falls back if admission rejects it.
      auto reach = [&](llvm::BitVector start,
                       const DenseMap<unsigned, SmallVector<unsigned>> &edges,
                       bool throughSeeds) {
        SmallVector<unsigned> work(start.set_bits_begin(),
                                   start.set_bits_end());
        while (!work.empty()) {
          unsigned owner = work.pop_back_val();
          auto found = edges.find(owner);
          if (found == edges.end() || (!throughSeeds && seeded.test(owner)))
            continue;
          for (unsigned next : found->second)
            if (!start.test(next)) {
              start.set(next);
              work.push_back(next);
            }
        }
        return start;
      };
      llvm::BitVector excluded = reach(blocked, adjacency, false);
      llvm::BitVector feedsSeed = reach(seeded, predecessors, true);
      feedsSeed &= blocked;
      excluded |= reach(feedsSeed, predecessors, false);
      excluded.reset(seeded);
      if (trace)
        llvm::errs() << "obelisk statement cohort growth: clock-wakes="
                     << clockWakes
                     << " observed=" << blocked.count() - clockWakes
                     << " excluded=" << excluded.count() << '\n';
      // Owners connected to a cohort join it. So do unconnected ones that the
      // same edge-triggered procedure wakes: its activation updates them all
      // in one pass, and they form a cohort of their own without a seed.
      // Owners that no edge-triggered procedure wakes join nothing else;
      // joining would rerun them on every unrelated wake.
      DenseMap<StringAttr, unsigned> clockedWriters;
      DenseMap<uint64_t, SmallVector<StringAttr>> writers;
      for (Attribute raw : graph.getNodes()) {
        auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(raw);
        if (!fragment)
          continue;
        StringAttr owner = fragment.getFunction().getAttr();
        auto function = symbols.lookup<sim::SimFuncOp>(owner);
        bool always = function && !indices.contains(function) &&
                      (function.getEntryKind() == sim::EntryKind::Always ||
                       function.getEntryKind() == sim::EntryKind::AlwaysFF);
        for (auto effect :
             fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
          auto kind = effect.getEffect();
          auto trigger = effect.getTrigger();
          if (always && kind == schedule::ComputeEffectKind::Watch &&
              (trigger == schedule::ComputeTriggerKind::Posedge ||
               trigger == schedule::ComputeTriggerKind::Negedge ||
               trigger == schedule::ComputeTriggerKind::Both))
            clockedWriters.try_emplace(owner, 0);
          if ((kind == schedule::ComputeEffectKind::Write ||
               kind == schedule::ComputeEffectKind::NBA ||
               kind == schedule::ComputeEffectKind::Drive) &&
              effect.getTarget() == schedule::ComputeTargetKind::Descriptor)
            if (auto root = analysis::ActivationMemoryAnalysis::getRoot(
                    effect.getResource(), effect.getDescriptor()))
              writers[*root].push_back(owner);
        }
      }
      unsigned writerCount = 0;
      for (auto &[owner, id] : clockedWriters)
        id = candidates.size() + writerCount++;
      llvm::IntEqClasses islands(candidates.size() + writerCount);
      for (const auto &[owner, targets] : adjacency)
        for (unsigned target : targets)
          if (!excluded.test(owner) && !excluded.test(target))
            islands.join(owner, target);
      for (const auto &[root, list] : watchers) {
        auto found = writers.find(root);
        if (found == writers.end())
          continue;
        for (const auto &entry : list)
          if (!excluded.test(entry.first))
            for (StringAttr writer : found->second)
              if (auto clocked = clockedWriters.find(writer);
                  clocked != clockedWriters.end())
                islands.join(entry.first, clocked->second);
      }
      DenseSet<unsigned> clocked;
      for (const auto &[writer, id] : clockedWriters)
        clocked.insert(islands.findLeader(id));
      llvm::MapVector<unsigned, SmallVector<unsigned>> grown;
      for (unsigned owner : owners)
        if (!excluded.test(owner))
          grown[islands.findLeader(owner)].push_back(owner);
      SmallVector<SmallVector<unsigned>> seeds;
      for (auto &[leader, island] : grown)
        if (llvm::any_of(island,
                         [&](unsigned owner) { return seeded.test(owner); }) ||
            (island.size() > 1 && clocked.contains(leader)))
          seeds.push_back(std::move(island));
      grownComponents = closeCohorts(seeds);
    }
    auto module = design->getParentOfType<ModuleOp>();
    auto limit = schedule::get<schedule::Field::MaxInlineOps>(module);
    uint64_t budget = limit ? limit.getUInt() : 5000;
    auto statementLimit =
        schedule::get<schedule::Field::MaxStatementOps>(module);
    uint64_t statementBudget =
        statementLimit ? statementLimit.getUInt() : 262144;
    SmallVector<Attribute> fusions;
    unsigned dataflowCount = 0;
    auto plan = [&](ArrayRef<unsigned> component) {
      SmallVector<sim::SimFuncOp> members;
      SmallVector<int64_t> memberFragments;
      SmallVector<const analysis::ActivationRegionAnalysis *> memberActivations;
      uint64_t cost = 0;
      for (unsigned member : component) {
        members.push_back(candidates[member]);
        memberActivations.push_back(activations[member]);
        memberFragments.push_back(fragments[member]);
        cost += analysis::getSimulationOperationCost(candidates[member]);
      }
      CombinationalFusionFailure observerReason;
      ComponentRejection reason = ComponentRejection::None;
      SmallVector<analysis::ActivationRegionStep> cycle;
      SmallVector<analysis::ActivationRegionStep> dataCycle;
      bool dataflow = false;
      if (maxMembers && component.size() > maxMembers)
        reason = ComponentRejection::MemberBudget;
      else if (!eligibility.canInterleave(members, &observerReason))
        reason = ComponentRejection::Observer;
      else if (llvm::any_of(members,
                            [&](auto member) {
                              return requiresReordering.contains(member);
                            }) ||
               !analysis::getActivationRegionOrder(
                   memberActivations, explain ? &cycle : nullptr)) {
        // Statement order is cyclic. Without each process's source order
        // (4.6(a)), a data-dependence order is valid under 4.3 only when no
        // outside observer sees the transitions it removes.
        auto slices = analysis::getActivationSliceOrder(
            memberActivations, explain ? &dataCycle : nullptr);
        if (!slices)
          reason = ComponentRejection::DataDependenceCycle;
        else if (!eligibility.canReorder(members, &observerReason))
          reason = ComponentRejection::ReorderObserver;
        else {
          dataflow = true;
          cost = 0;
          for (const auto &slice : *slices) {
            // Charge the executable slice, including reconstructed private
            // versions. The complete source CFG can be far larger than the
            // publication retained after dead control and store elimination.
            auto reduced = analysis::cloneActivationSlice(slice);
            if (!reduced) {
              reason = ComponentRejection::SliceConstruction;
              break;
            }
            cost += analysis::getSimulationOperationCost(*reduced);
          }
        }
      }
      uint64_t selectedBudget = dataflow ? statementBudget : budget;
      if (reason == ComponentRejection::None && selectedBudget &&
          cost > selectedBudget)
        reason = ComponentRejection::OperationBudget;
      if (reason != ComponentRejection::None) {
        if (trace)
          llvm::errs() << "obelisk statement cohort rejected: members="
                       << members.size() << " operations=" << cost << " reason="
                       << stringifyComponentRejection(reason, observerReason)
                       << '\n';
        if (explain) {
          auto diagnostic = members.front().emitRemark();
          diagnostic << "statement component rejected: "
                     << stringifyComponentRejection(reason, observerReason)
                     << "; operations=" << cost << "; members=";
          for (auto member : members)
            diagnostic << ' ' << member.getSymName();
          if (reason == ComponentRejection::StructuredDependenceCycle ||
              reason == ComponentRejection::DataDependenceCycle ||
              reason == ComponentRejection::ReorderObserver) {
            SmallVector<analysis::ActivationRegionStep> publicationCycle;
            auto valueOrder = analysis::getActivationPublicationOrder(
                memberActivations, &publicationCycle);
            diagnostic << "; SSA publication graph: "
                       << (valueOrder                 ? "acyclic"
                           : publicationCycle.empty() ? "unavailable"
                                                      : "cyclic");
            auto describePublication = [&](StringRef kind, const auto &steps) {
              for (const auto &step : steps) {
                auto owner = llvm::find(memberActivations, step.activation) -
                             memberActivations.begin();
                diagnostic << "; " << kind << " @"
                           << members[owner].getSymName() << " writes";
                for (auto range :
                     step.activation->getMemory().getWrites().lookup(
                         step.operation))
                  diagnostic << " s" << range.root << '[' << range.low << ','
                             << range.low + range.width << ')';
              }
            };
            describePublication("slice cycle", dataCycle);
            describePublication("value cycle", publicationCycle);
          }
          for (unsigned i = 0; i < cycle.size(); ++i) {
            const auto &from = cycle[i];
            const auto &to = cycle[(i + 1) % cycle.size()];
            if (from.activation == to.activation)
              continue;
            auto describe = [&](const analysis::ActivationRegionStep &step) {
              auto owner = llvm::find(memberActivations, step.activation) -
                           memberActivations.begin();
              auto nodes = step.activation->getNodes();
              auto node = llvm::find_if(nodes, [&](const auto &node) {
                return node.operation == step.operation;
              });
              diagnostic << '@' << members[owner].getSymName() << ':'
                         << (node - nodes.begin()) << " ("
                         << step.operation->getName() << ')';
              return node;
            };
            diagnostic << "; cycle edge: ";
            auto producer = describe(from);
            diagnostic << " -> ";
            auto consumer = describe(to);
            diagnostic << " via";
            for (const auto &write : producer->writes)
              for (const auto &read : consumer->reads) {
                uint64_t low = std::max(write.low, read.low);
                uint64_t end =
                    std::min(write.low + write.width, read.low + read.width);
                if (write.root == read.root && low < end)
                  diagnostic << " s" << write.root << '[' << low << ',' << end
                             << ')';
              }
          }
        }
        return false;
      }
      ++plans;
      if (dataflow) {
        ++dataflowPlans;
        ++dataflowCount;
      }
      if (trace)
        llvm::errs() << "obelisk statement cohort planned: members="
                     << members.size() << " operations=" << cost
                     << " dataflow=" << dataflow << '\n';
      // This requests materialization, not permission to assume acyclicity.
      // The materializer consumes a fresh revision-local analysis and checks
      // exclusive writers, complete assignments and spawn/observation rules.
      fusions.push_back(schedule::ComputeFusionAttr::get(
          design.getContext(), fusions.size(),
          DenseI64ArrayAttr::get(design.getContext(), memberFragments), true,
          dataflow));
      return true;
    };
    for (const auto &[leader, component] : grownComponents) {
      size_t closed = 0;
      for (const auto &[original, members] : closedComponents)
        if (llvm::is_contained(component, members.front()))
          closed += members.size();
      if (closed != component.size() && trace)
        llvm::errs() << "obelisk statement cohort grown: members="
                     << component.size() << " from=" << closed << '\n';
      if (plan(component) || closed == component.size())
        continue;
      // Growth is only a cost choice. Keep the cohorts it would replace.
      for (const auto &[original, members] : closedComponents)
        if (llvm::is_contained(component, members.front()))
          plan(members);
    }
    if (!fusions.empty())
      schedule::set<schedule::metadata::staticBodyFusion>(
          design, ArrayAttr::get(design.getContext(), fusions));
    if (trace)
      llvm::errs() << "obelisk statement scheduling: eligible="
                   << candidates.size()
                   << " cohorts=" << closedComponents.size()
                   << " plans=" << fusions.size()
                   << " dataflow=" << dataflowCount << '\n';
    if (explain)
      design.emitRemark() << "statement scheduling: " << candidates.size()
                          << " eligible owners, " << closedComponents.size()
                          << " observer-closed cohorts, " << fusions.size()
                          << " plans (" << dataflowCount
                          << " data-dependence order)";
  }
};
} // namespace
} // namespace obelisk
