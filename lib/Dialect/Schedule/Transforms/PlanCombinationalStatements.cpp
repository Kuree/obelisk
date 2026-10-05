//===- PlanCombinationalStatements.cpp - Statement DAG fusion ===//
#include "ComputeFusion.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Analysis/CombinationalDependencyAnalysis.h"
#include "obelisk/Analysis/GraphAlgorithms.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_PLANCOMBINATIONALSTATEMENTSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {
class PlanCombinationalStatementsPass final
    : public impl::PlanCombinationalStatementsPassBase<
          PlanCombinationalStatementsPass> {
public:
  using Base::Base;
  void runOnOperation() override {
    auto design = getOperation();
    auto graph = design.getComputeGraphAttr();
    if (!graph || graph.getWorkers() != 1 ||
        graph.getVpi() == schedule::ComputeVPIMode::Full)
      return;
    if (maxMembers < 2 || maxMembers > 64) {
      design.emitOpError(
          "statement fusion member limit must be between 2 and 64");
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
    for (Attribute raw : graph.getNodes()) {
      auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(raw);
      if (!fragment ||
          fragment.getTier() != schedule::ComputeTierKind::Native ||
          (fragment.getAction() != schedule::ComputeActionKind::SuspendChange &&
           fragment.getAction() != schedule::ComputeActionKind::SuspendAny))
        continue;
      auto function =
          symbols.lookup<sim::SimFuncOp>(fragment.getFunction().getValue());
      if (!function || indices.contains(function) ||
          function.getBody().getBlocks().size() != 2 ||
          !eligibility.analyze(function, handles, /*interleaved=*/true))
        continue;
      indices[function] = candidates.size();
      candidates.push_back(function);
      fragments.push_back(fragment.getId());
    }
    if (candidates.size() < 2)
      return;

    analysis::CombinationalDependencyAnalysis dependencies(design);
    SmallVector<unsigned> owners;
    DenseMap<unsigned, SmallVector<unsigned>> adjacency;
    for (unsigned owner = 0; owner < candidates.size(); ++owner)
      owners.push_back(owner);
    auto nodes = dependencies.getNodes();
    for (const auto &edge : dependencies.getEdges()) {
      if (edge.kind !=
          analysis::CombinationalDependencyAnalysis::EdgeKind::Memory)
        continue;
      auto from = indices.find(nodes[edge.source].owner);
      auto to = indices.find(nodes[edge.target].owner);
      if (from != indices.end() && to != indices.end() &&
          from->second != to->second)
        adjacency[from->second].push_back(to->second);
    }
    for (auto &[owner, targets] : adjacency) {
      llvm::sort(targets);
      targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    }
    auto components = analysis::computeStronglyConnectedComponents<unsigned>(
        owners, adjacency);
    auto module = design->getParentOfType<ModuleOp>();
    auto limit = schedule::get<schedule::Field::MaxInlineOps>(module);
    uint64_t budget = limit ? limit.getUInt() : 5000;
    SmallVector<Attribute> fusions;
    for (const auto &component : components) {
      if (component.size() < 2 || component.size() > maxMembers)
        continue;
      SmallVector<sim::SimFuncOp> members;
      SmallVector<int64_t> memberFragments;
      uint64_t cost = 0;
      for (unsigned member : component) {
        members.push_back(candidates[member]);
        memberFragments.push_back(fragments[member]);
        cost += analysis::getSimulationOperationCost(candidates[member]);
      }
      if ((budget && cost > budget) || !dependencies.getInterleaving(members))
        continue;
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
  }
};
} // namespace
} // namespace obelisk
