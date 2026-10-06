//===- PlanCombinationalStatements.cpp - Statement DAG fusion ===//
#include "ComputeFusion.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Analysis/GraphAlgorithms.h"
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
  void runOnOperation() override {
    auto design = getOperation();
    auto graph = design.getComputeGraphAttr();
    if (!graph || graph.getWorkers() != 1 ||
        graph.getVpi() == schedule::ComputeVPIMode::Full)
      return;
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
    CombinationalFusionAnalysis eligibility(design, handles);
    SymbolTable symbols(design);
    SmallVector<sim::SimFuncOp> candidates;
    SmallVector<int64_t> fragments;
    DenseMap<Operation *, unsigned> indices;
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
      if (!body || !body->structured) {
        if (explain)
          function.emitRemark()
              << "statement scheduling rejected: " << reason.getMessage();
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
            effect.getResource() == schedule::ComputeResourceKind::Storage &&
            effect.getTarget() == schedule::ComputeTargetKind::Descriptor &&
            effect.getWidth() &&
            effect.getLow() <= UINT64_MAX - effect.getWidth())
          watchers[effect.getDescriptor()].push_back(
              {found->second,
               {static_cast<uint64_t>(effect.getDescriptor()), effect.getLow(),
                effect.getWidth()}});
    }
    llvm::IntEqClasses cohorts(candidates.size());
    SmallVector<unsigned> pending;
    DenseSet<unsigned> included;
    auto include = [&](unsigned owner) {
      if (included.insert(owner).second)
        pending.push_back(owner);
    };
    for (const auto &component : components) {
      if (component.size() < 2)
        continue;
      ++componentsFound;
      for (unsigned owner : component) {
        cohorts.join(component.front(), owner);
        include(owner);
      }
    }
    for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
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
    llvm::MapVector<unsigned, SmallVector<unsigned>> closedComponents;
    for (unsigned owner : owners)
      if (included.contains(owner))
        closedComponents[cohorts.findLeader(owner)].push_back(owner);
    auto module = design->getParentOfType<ModuleOp>();
    auto limit = schedule::get<schedule::Field::MaxInlineOps>(module);
    uint64_t budget = limit ? limit.getUInt() : 5000;
    SmallVector<Attribute> fusions;
    for (const auto &[leader, component] : closedComponents) {
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
      if (maxMembers && component.size() > maxMembers)
        reason = ComponentRejection::MemberBudget;
      else if (budget && cost > budget)
        reason = ComponentRejection::OperationBudget;
      else if (!eligibility.canInterleave(members, &observerReason))
        reason = ComponentRejection::Observer;
      else if (!analysis::getActivationRegionOrder(memberActivations,
                                                   explain ? &cycle : nullptr))
        reason = ComponentRejection::StructuredDependenceCycle;
      if (reason != ComponentRejection::None) {
        if (explain) {
          auto diagnostic = members.front().emitRemark();
          diagnostic << "statement component rejected: "
                     << stringifyComponentRejection(reason, observerReason)
                     << "; operations=" << cost << "; members=";
          for (auto member : members)
            diagnostic << ' ' << member.getSymName();
          if (reason == ComponentRejection::StructuredDependenceCycle) {
            SmallVector<analysis::ActivationRegionStep> publicationCycle;
            auto valueOrder = analysis::getActivationPublicationOrder(
                memberActivations, &publicationCycle);
            diagnostic << "; SSA publication graph: "
                       << (valueOrder                 ? "acyclic"
                           : publicationCycle.empty() ? "unavailable"
                                                      : "cyclic");
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
        continue;
      }
      ++plans;
      // This requests materialization, not permission to assume acyclicity.
      // The materializer consumes a fresh revision-local analysis and checks
      // exclusive writers, complete assignments and spawn/observation rules.
      fusions.push_back(schedule::ComputeFusionAttr::get(
          design.getContext(), fusions.size(),
          DenseI64ArrayAttr::get(design.getContext(), memberFragments), true));
    }
    if (!fusions.empty())
      schedule::set<schedule::metadata::staticBodyFusion>(
          design, ArrayAttr::get(design.getContext(), fusions));
    if (explain)
      design.emitRemark() << "statement scheduling: " << candidates.size()
                          << " eligible owners, " << closedComponents.size()
                          << " observer-closed cohorts, " << fusions.size()
                          << " plans";
  }
};
} // namespace
} // namespace obelisk
