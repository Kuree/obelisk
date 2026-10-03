#include "obelisk/Analysis/NBAInfluenceAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk::analysis {
namespace {
class InfluenceState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  bool observed = false;
  ChangeResult join(bool incoming) {
    if (!incoming || observed)
      return ChangeResult::NoChange;
    observed = true;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override { os << observed; }
};
using Dependencies = DenseMap<Operation *, SmallVector<Operation *>>;

// One sparse lattice per declared root, rather than a dense root bitvector at
// every instruction. Solver dependencies revisit predecessors when an observer
// reaches their successors. A root is never certified before convergence.
class InfluenceDataflow final : public DataFlowAnalysis {
public:
  InfluenceDataflow(DataFlowSolver &solver, const Dependencies &dependencies,
                    const DenseSet<Operation *> &seeds)
      : DataFlowAnalysis(solver), dependencies(dependencies), seeds(seeds) {}
  LogicalResult initialize(Operation *) override {
    for (const auto &entry : dependencies)
      if (failed(visit(getProgramPointBefore(entry.first))))
        return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    Operation *root = point->getNextOp();
    bool observed = seeds.contains(root);
    for (Operation *target : dependencies.at(root))
      observed |=
          getOrCreateFor<InfluenceState>(point, getProgramPointBefore(target))
              ->observed;
    auto *state = getOrCreate<InfluenceState>(point);
    propagateIfChanged(state, state->join(observed));
    return success();
  }

private:
  const Dependencies &dependencies;
  const DenseSet<Operation *> &seeds;
};
} // namespace

NBAInfluenceAnalysis::NBAInfluenceAnalysis(sim::SimDesignOp design,
                                           ArrayRef<Root> observers) {
  if (!design || !design.getComputeGraphAttr() ||
      design.getComputeGraphAttr().getVpi() != schedule::ComputeVPIMode::Off)
    return;
  using Resource = schedule::ComputeResourceKind;
  using Effect = schedule::ComputeEffectKind;
  DenseMap<Root, Operation *> declarations;
  design.walk([&](sim::SimStorageDeclOp op) {
    declarations[{Resource::Storage, op.getId()}] = op;
  });
  design.walk([&](sim::SimNetDeclOp op) {
    declarations[{Resource::Net, op.getId()}] = op;
  });
  Dependencies dependencies;
  DenseSet<Operation *> seeds;
  for (const auto &entry : declarations) {
    dependencies[entry.second];
    known.insert(entry.first);
  }
  // Waveform dumping reads settled canonical state without a per-root Watch
  // effect. It permits batching but forbids erasing the stored value. Dynamic
  // scopes/depths conservatively make every root a possible reader target.
  bool dumpsState = design
                        .walk([](Operation *op) {
                          return isa<sim::SimDumpVarsOp, sim::SimDumpAllOp,
                                     sim::SimDumpControlOp, sim::SimDumpPortsOp,
                                     sim::SimDumpPortsControlOp>(op)
                                     ? WalkResult::interrupt()
                                     : WalkResult::advance();
                        })
                        .wasInterrupted();
  if (dumpsState)
    readers = known;
  for (Root root : observers) {
    auto found = declarations.find(root);
    if (found == declarations.end())
      return;
    seeds.insert(found->second);
  }
  bool resolved = true;
  design.walk([&](sim::SimNetConnectDeclOp connection) {
    Operation *lhs =
        declarations.lookup({Resource::Net, connection.getLhsNetId()});
    Operation *rhs =
        declarations.lookup({Resource::Net, connection.getRhsNetId()});
    if (!lhs || !rhs) {
      resolved = false;
      return;
    }
    // Root-wide may-alias closure; even a directed/partial connection is
    // conservatively bidirectional until a range-sensitive proof is available.
    dependencies[lhs].push_back(rhs);
    dependencies[rhs].push_back(lhs);
  });
  design.walk([&](sim::SimFuncOp function) {
    // Called function effects are already instantiated in each caller summary.
    // Declaration initialization precedes process execution (LRM 6.8).
    if (function.getEntryKind() == sim::EntryKind::Function ||
        function.getEntryKind() == sim::EntryKind::Task ||
        function.getEntryKind() == sim::EntryKind::RootInitializer)
      return;
    ArrayAttr summary = function.getEffectSummaryAttr();
    if (!summary) {
      resolved = false;
      return;
    }
    SmallVector<Operation *> inputs, outputs;
    for (auto effect : summary.getAsRange<schedule::ComputeEffectAttr>()) {
      if (effect.getResource() == Resource::Unknown) {
        resolved = false;
        continue;
      }
      if (effect.getResource() != Resource::Storage &&
          effect.getResource() != Resource::Net)
        continue;
      bool input = effect.getEffect() == Effect::Read ||
                   effect.getEffect() == Effect::Watch;
      bool output = effect.getEffect() == Effect::Write ||
                    effect.getEffect() == Effect::Drive;
      // RHS/target evaluation remains at enqueue; no immediate dependency
      // crosses an NBA commit boundary (LRM 4.9.4).
      if (!input && !output)
        continue;
      if (effect.getTarget() != schedule::ComputeTargetKind::Descriptor) {
        resolved = false;
        continue;
      }
      auto found =
          declarations.find({effect.getResource(), effect.getDescriptor()});
      if (found == declarations.end()) {
        resolved = false;
        continue;
      }
      (input ? inputs : outputs).push_back(found->second);
      if (input)
        readers.insert({effect.getResource(), effect.getDescriptor()});
    }
    for (Operation *input : inputs) {
      llvm::append_range(dependencies[input], outputs);
    }
  });
  if (!resolved)
    return;
  DataFlowSolver solver;
  solver.load<InfluenceDataflow>(dependencies, seeds);
  if (failed(solver.initializeAndRun(design)))
    return;
  for (const auto &entry : declarations) {
    const auto *state = solver.lookupState<InfluenceState>(
        solver.getProgramPointBefore(entry.second));
    if (!state)
      return;
    if (state->observed)
      observed.insert(entry.first);
  }
  complete = true;
}
} // namespace obelisk::analysis
