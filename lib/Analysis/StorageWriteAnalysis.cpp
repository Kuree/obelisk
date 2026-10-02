//===- StorageWriteAnalysis.cpp - Storage-write dataflow
//--------------------===//
#include "obelisk/Analysis/StorageWriteAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk::analysis {

void StorageViewFact::print(raw_ostream &os) const {
  if (!hasRoot())
    os << "unknown";
  else {
    os << "root=" << descriptor << " bits=" << rootWidth;
    if (hasLane())
      os << " lane=" << low << ":" << width << " stride=" << stride
         << " dynamic=" << dynamic << " clipped=" << clipped;
  }
}

namespace {
class MustDefinitionLattice : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  bool reachable = false;
  llvm::BitVector definitions;
  ChangeResult joinDefinitions(const llvm::BitVector &incoming) {
    if (!reachable) {
      reachable = true;
      definitions = incoming;
      return ChangeResult::Change;
    }
    llvm::BitVector next = definitions;
    next &= incoming;
    if (next == definitions)
      return ChangeResult::NoChange;
    definitions = std::move(next);
    return ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const MustDefinitionLattice &>(rhs);
    return other.reachable ? joinDefinitions(other.definitions)
                           : ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override {
    os << (reachable ? "must-defined" : "unreachable");
  }
};
class MustDefinitionDataflow
    : public dataflow::DenseForwardDataFlowAnalysis<MustDefinitionLattice> {
public:
  MustDefinitionDataflow(DataFlowSolver &solver,
                         const DenseMap<Operation *, unsigned> &indices,
                         const DenseSet<Operation *> &barriers,
                         bool initiallyDefined = false)
      : DenseForwardDataFlowAnalysis(solver), indices(indices),
        barriers(barriers), initiallyDefined(initiallyDefined) {}
  LogicalResult visitOperation(Operation *op,
                               const MustDefinitionLattice &before,
                               MustDefinitionLattice *after) override {
    if (!before.reachable)
      return success();
    llvm::BitVector next = before.definitions;
    if (barriers.contains(op))
      next.reset();
    if (auto found = indices.find(op); found != indices.end())
      next.set(found->second);
    propagateIfChanged(after, after->joinDefinitions(next));
    return success();
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const MustDefinitionLattice &before,
                                    MustDefinitionLattice *after) override {
    (void)visitOperation(call, before, after);
  }

private:
  void setToEntryState(MustDefinitionLattice *state) override {
    propagateIfChanged(state, state->joinDefinitions(llvm::BitVector(
                                  indices.size(), initiallyDefined)));
  }
  const DenseMap<Operation *, unsigned> &indices;
  const DenseSet<Operation *> &barriers;
  bool initiallyDefined;
};

class HotPathLattice : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  bool reachable = false;
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    bool incoming = static_cast<const HotPathLattice &>(rhs).reachable;
    if (reachable || !incoming)
      return ChangeResult::NoChange;
    reachable = true;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override { os << reachable; }
};
class HotPathAnalysis
    : public dataflow::DenseForwardDataFlowAnalysis<HotPathLattice> {
public:
  HotPathAnalysis(DataFlowSolver &solver, const DenseSet<Block *> &cold)
      : DenseForwardDataFlowAnalysis(solver), cold(cold) {}
  LogicalResult visitOperation(Operation *op, const HotPathLattice &before,
                               HotPathLattice *after) override {
    if (!cold.contains(op->getBlock()))
      propagateIfChanged(after, after->join(before));
    return success();
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const HotPathLattice &before,
                                    HotPathLattice *after) override {
    (void)visitOperation(call, before, after);
  }

private:
  void setToEntryState(HotPathLattice *state) override {
    HotPathLattice entry(state->getAnchor());
    entry.reachable = true;
    propagateIfChanged(state, state->join(entry));
  }
  const DenseSet<Block *> &cold;
};
class WriteExecutionLattice : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  bool reachable = false;
  // The product of {0, 1, many} counts is two may-bitsets. Keeping the
  // representation packed makes joins word-parallel without losing any
  // per-statement execution or mutual-exclusion information.
  llvm::BitVector seen, repeated;
  ChangeResult joinCounts(const llvm::BitVector &incomingSeen,
                          const llvm::BitVector &incomingRepeated) {
    if (!reachable) {
      reachable = true;
      seen = incomingSeen;
      repeated = incomingRepeated;
      return ChangeResult::Change;
    }
    if (!incomingSeen.test(seen) && !incomingRepeated.test(repeated))
      return ChangeResult::NoChange;
    seen |= incomingSeen;
    repeated |= incomingRepeated;
    return ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const WriteExecutionLattice &>(rhs);
    return other.reachable ? joinCounts(other.seen, other.repeated)
                           : ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override {
    if (!reachable)
      os << "unreachable";
    else
      for (unsigned index = 0; index != seen.size(); ++index)
        os << (repeated.test(index) ? 2 : seen.test(index) ? 1 : 0) << ' ';
  }
};

class WriteExecutionAnalysis
    : public dataflow::DenseForwardDataFlowAnalysis<WriteExecutionLattice> {
public:
  WriteExecutionAnalysis(DataFlowSolver &solver,
                         const DenseMap<Operation *, unsigned> &writes,
                         bool unknownCallEffects, bool resetAtPositiveDelay)
      : DenseForwardDataFlowAnalysis(solver), writes(writes),
        unknownCallEffects(unknownCallEffects),
        resetAtPositiveDelay(resetAtPositiveDelay) {}
  LogicalResult visitOperation(Operation *op,
                               const WriteExecutionLattice &before,
                               WriteExecutionLattice *after) override {
    if (!before.reachable)
      return success();
    auto write = writes.find(op);
    if (resetsWindow(op)) {
      llvm::BitVector zero(writes.size());
      propagateIfChanged(after, after->joinCounts(zero, zero));
    } else if (write != writes.end()) {
      llvm::BitVector seen = before.seen, repeated = before.repeated;
      if (seen.test(write->second))
        repeated.set(write->second);
      seen.set(write->second);
      propagateIfChanged(after, after->joinCounts(seen, repeated));
    } else if (hasUnknownCallEffects(op)) {
      llvm::BitVector many(writes.size(), true);
      propagateIfChanged(after, after->joinCounts(many, many));
    } else if (&before != after) {
      propagateIfChanged(after, after->join(before));
    }
    return success();
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const WriteExecutionLattice &before,
                                    WriteExecutionLattice *after) override {
    if (!before.reachable)
      return;
    (void)visitOperation(call.getOperation(), before, after);
  }

private:
  bool resetsWindow(Operation *op) const {
    auto delay = dyn_cast<sim::SimSuspendDelayOp>(op);
    if (!resetAtPositiveDelay || !delay)
      return false;
    auto constant = delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
    return constant && constant.getValue() != 0;
  }
  bool hasUnknownCallEffects(Operation *op) const {
    return unknownCallEffects &&
           isa<CallOpInterface, sim::SimCallOp, sim::SimClassDirectCallOp,
               sim::SimClassVirtualCallOp>(op);
  }
  void buildOperationEquivalentLatticeAnchor(Operation *op) override {
    // Most operations have an identity transfer. Use the solver's lattice
    // equivalence classes so a long arithmetic cone shares one execution
    // state, rather than propagating the entire write product at every op.
    // Region/call/CFG transfers remain under the dense framework's control.
    if (op->getNumRegions() || op->getNumSuccessors() ||
        op->hasTrait<OpTrait::IsTerminator>() || isa<CallOpInterface>(op) ||
        writes.contains(op) || resetsWindow(op) || hasUnknownCallEffects(op))
      return;
    unionLatticeAnchors<WriteExecutionLattice>(getProgramPointBefore(op),
                                               getProgramPointAfter(op));
  }
  void setToEntryState(WriteExecutionLattice *lattice) override {
    llvm::BitVector zero(writes.size());
    propagateIfChanged(lattice, lattice->joinCounts(zero, zero));
  }
  const DenseMap<Operation *, unsigned> &writes;
  bool unknownCallEffects;
  bool resetAtPositiveDelay;
};
} // namespace

MustDefinitionAnalysis::MustDefinitionAnalysis(
    sim::SimFuncOp function, ArrayRef<Operation *> definitions,
    llvm::function_ref<bool(Operation *)> isBarrier) {
  for (Operation *definition : definitions)
    indices.try_emplace(definition, indices.size());
  if (indices.empty())
    return;
  DenseSet<Operation *> barriers;
  function.walk([&](Operation *op) {
    if (isBarrier(op))
      barriers.insert(op);
  });
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<MustDefinitionDataflow>(indices, barriers);
  if (failed(solver.initializeAndRun(function)))
    return;
  function.walk([&](Operation *op) {
    const auto *state = solver.lookupState<MustDefinitionLattice>(
        solver.getProgramPointBefore(op));
    if (state && state->reachable)
      before.try_emplace(op, state->definitions);
  });
}
bool MustDefinitionAnalysis::containsBefore(Operation *definition,
                                            Operation *use) const {
  auto index = indices.find(definition);
  auto state = before.find(use);
  return index != indices.end() && state != before.end() &&
         state->second.test(index->second);
}

NoBarrierAnalysis::NoBarrierAnalysis(
    sim::SimFuncOp function, llvm::function_ref<bool(Operation *)> isBarrier) {
  // One must fact is seeded at entry, killed by barriers and never generated
  // by an operation. Null is an internal index key, not an IR definition.
  DenseMap<Operation *, unsigned> indices{{nullptr, 0}};
  DenseSet<Operation *> barriers;
  function.walk([&](Operation *op) {
    if (isBarrier(op))
      barriers.insert(op);
  });
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<MustDefinitionDataflow>(indices, barriers, true);
  if (failed(solver.initializeAndRun(function)))
    return;
  function.walk([&](Operation *op) {
    const auto *state = solver.lookupState<MustDefinitionLattice>(
        solver.getProgramPointBefore(op));
    if (state && state->reachable && state->definitions.test(0))
      safe.insert(op);
  });
}

NativeHotPathReachability::NativeHotPathReachability(sim::SimFuncOp function) {
  DenseSet<Block *> cold;
  function.walk([&](Operation *op) {
    if (schedule::has<schedule::Field::EvalDirectOutput>(op))
      return;
    if (isa<sim::SimFinishOp, sim::SimStopOp, sim::SimFatalOp,
            sim::SimProgramExitOp, sim::SimErrorOp, sim::SimStatusCheckOp,
            sim::SimDisplayOp, sim::SimSampledReadOp, sim::SimSampledHistoryOp>(
            op))
      cold.insert(op->getBlock());
  });
  if (cold.empty())
    return;
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<HotPathAnalysis>(cold);
  if (failed(solver.initializeAndRun(function)))
    return;
  function.walk([&](Operation *op) {
    auto *before =
        solver.lookupState<HotPathLattice>(solver.getProgramPointBefore(op));
    if (!cold.contains(op->getBlock()) && before && before->reachable)
      hot.insert(op);
  });
  valid = true;
}
bool NativeHotPathReachability::canExecute(Operation *op) const {
  return !valid || hot.contains(op);
}

StorageWriteAnalysis::StorageWriteAnalysis(
    sim::SimFuncOp function, const HandleDataflowAnalysis &analysis)
    : StorageWriteAnalysis(function, analysis.analyze(function)) {}

StorageWriteAnalysis::StorageWriteAnalysis(sim::SimFuncOp function,
                                           HandleDataflowResult handles,
                                           bool trackExecution)
    : handles(std::move(handles)) {
  if (!trackExecution || function.isExternal() || function.getBody().empty())
    return;
  SmallVector<Operation *> writes;
  function.walk([&](Operation *op) {
    if (isa<sim::SimRefStoreOp, sim::SimNBAEnqueueOp>(op))
      writes.push_back(op);
  });
  execution = std::make_unique<WriteExecutionBounds>(function, writes);
}

WriteExecutionBounds::WriteExecutionBounds(Operation *scope,
                                           ArrayRef<Operation *> tracked,
                                           bool unknownCallEffects,
                                           bool resetAtPositiveDelay) {
  for (Operation *op : tracked)
    indices.try_emplace(op, indices.size());
  if (indices.empty())
    return;
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<WriteExecutionAnalysis>(indices, unknownCallEffects,
                                      resetAtPositiveDelay);
  if (failed(solver.initializeAndRun(scope)))
    return;
  bounds.resize(indices.size(), 2);
  preceding.resize(indices.size(), llvm::BitVector(indices.size()));
  for (auto [write, index] : indices) {
    auto *before = solver.lookupState<WriteExecutionLattice>(
        solver.getProgramPointBefore(write));
    auto *after = solver.lookupState<WriteExecutionLattice>(
        solver.getProgramPointAfter(write));
    if (!before || !after)
      preceding[index].set();
    else {
      bounds[index] = !after->reachable        ? 0
                      : after->repeated[index] ? 2
                      : after->seen[index]     ? 1
                                               : 0;
      if (before->reachable)
        preceding[index] = before->seen;
    }
  }
  valid = true;
}
StorageViewFact StorageWriteAnalysis::lookup(Value reference) const {
  auto fact = handles.facts.find(reference);
  if (fact == handles.facts.end() || !fact->second.descriptor ||
      fact->second.resource != schedule::ComputeResourceKind::Storage)
    return {StorageViewFact::Kind::Unknown};
  const auto &p = fact->second;
  auto certificate = handles.certificates.find(reference);
  if (certificate == handles.certificates.end() ||
      !certificate->second.laneKnown)
    return {StorageViewFact::Kind::Root, *p.descriptor, p.rootWidth};
  const auto &lane = certificate->second;
  return {StorageViewFact::Kind::Lane,
          *p.descriptor,
          p.rootWidth,
          lane.low,
          lane.width,
          lane.stride,
          p.dynamic,
          lane.clipped};
}
bool StorageWriteAnalysis::executesAtMostOnce(Operation *write) const {
  return execution && execution->executesAtMostOnce(write);
}
bool StorageWriteAnalysis::mutuallyExclusive(Operation *lhs,
                                             Operation *rhs) const {
  return execution && execution->mutuallyExclusive(lhs, rhs);
}
bool WriteExecutionBounds::executesAtMostOnce(Operation *write) const {
  auto found = indices.find(write);
  return valid && found != indices.end() && bounds[found->second] <= 1;
}
bool WriteExecutionBounds::mutuallyExclusive(Operation *lhs,
                                             Operation *rhs) const {
  auto a = indices.find(lhs), b = indices.find(rhs);
  return valid && a != indices.end() && b != indices.end() && lhs != rhs &&
         !preceding[a->second].test(b->second) &&
         !preceding[b->second].test(a->second);
}
bool StorageWriteAnalysis::hasDirectDynamicSelection(Value reference) const {
  auto proof = handles.certificates.find(reference);
  return proof != handles.certificates.end() &&
         proof->second.directDynamicSelection == reference;
}

} // namespace obelisk::analysis
