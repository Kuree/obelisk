//===- StorageWriteAnalysis.cpp - Storage-write dataflow --------------------===//
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
  SmallVector<uint8_t> counts;
  ChangeResult joinCounts(ArrayRef<uint8_t> incoming) {
    if (!reachable) {
      reachable = true;
      counts.assign(incoming.begin(), incoming.end());
      return ChangeResult::Change;
    }
    bool changed = false;
    for (auto [count, rhs] : llvm::zip(counts, incoming))
      if (rhs > count) {
        count = rhs;
        changed = true;
      }
    return changed ? ChangeResult::Change : ChangeResult::NoChange;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const WriteExecutionLattice &>(rhs);
    return other.reachable ? joinCounts(other.counts) : ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override {
    if (!reachable)
      os << "unreachable";
    else
      llvm::interleaveComma(counts, os);
  }
};

class WriteExecutionAnalysis
    : public dataflow::DenseForwardDataFlowAnalysis<WriteExecutionLattice> {
public:
  WriteExecutionAnalysis(DataFlowSolver &solver,
                         const DenseMap<Operation *, unsigned> &writes,
                         bool unknownCallEffects)
      : DenseForwardDataFlowAnalysis(solver), writes(writes),
        unknownCallEffects(unknownCallEffects) {}
  LogicalResult visitOperation(Operation *op,
                               const WriteExecutionLattice &before,
                               WriteExecutionLattice *after) override {
    if (!before.reachable)
      return success();
    SmallVector<uint8_t> next = before.counts;
    if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(op)) {
      auto constant = delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
      if (constant && constant.getValue() != 0)
        std::fill(next.begin(), next.end(), 0);
    } else if (auto write = writes.find(op); write != writes.end()) {
      uint8_t &count = next[write->second];
      count = std::min<unsigned>(2, count + 1);
    } else if (unknownCallEffects &&
               isa<CallOpInterface, sim::SimCallOp, sim::SimClassDirectCallOp,
                   sim::SimClassVirtualCallOp>(op)) {
      std::fill(next.begin(), next.end(), 2);
    }
    propagateIfChanged(after, after->joinCounts(next));
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
  void setToEntryState(WriteExecutionLattice *lattice) override {
    SmallVector<uint8_t> zero(writes.size(), 0);
    propagateIfChanged(lattice, lattice->joinCounts(zero));
  }
  const DenseMap<Operation *, unsigned> &writes;
  bool unknownCallEffects;
};
} // namespace

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
                                           bool unknownCallEffects) {
  for (Operation *op : tracked)
    indices.try_emplace(op, indices.size());
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<WriteExecutionAnalysis>(indices, unknownCallEffects);
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
      bounds[index] = after->reachable ? after->counts[index] : 0;
      if (before->reachable)
        for (auto [other, count] : llvm::enumerate(before->counts))
          if (count)
            preceding[index].set(other);
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
