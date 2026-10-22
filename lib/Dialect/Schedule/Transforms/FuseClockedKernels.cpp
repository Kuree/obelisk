//===- FuseClockedKernels.cpp - One activation per clock edge ------------===//
//
// IEEE 1800-2023 4.4.2.2 and 4.7 let a simulator take the Active events woken
// by one clock edge in any order, so every edge-triggered process of a clock
// may run as one activation (compute-body fusion relies on the same
// permission inside one instance). A port connection is an implied continuous
// assignment (23.3.3, 4.9.6). When a copy process is the only writer of a
// variable, the copy and its source have the same value sequence within each
// time slot, and an edge of the copy is an Active event the simulator may
// choose to process immediately after the source changes. This pass therefore
// forms one kernel per clock alias class and edge, across instances.
//
// Inside that activation an NBA (4.9.4, 10.4.2) normally updates its target
// in the NBA region, after every Active and Inactive event of the slot (4.5).
// Storing at the NBA statement has the same user-visible effect (4.3) when:
//   (a) every read in the kernel that can execute after an NBA site of the
//       root sees the root's incoming value: either the kernel delays the
//       root's updates to its end or the read uses a snapshot;
//   (b) the kernel's NBA sites are the root's only writers, apart from static
//       initialization and activations that cannot share the kernel's phase;
//       a delayed NBA may land in any NBA region and always conflicts;
//   (c) every process waiting on the root, transitively through its writes,
//       is a side-effect-free combinational process. 4.5 already lets those
//       evaluate between individual NBA updates, and they reevaluate from the
//       final values; no edge, level, event, VPI or toggle observer can see
//       the earlier update (CombinationalFusionAnalysis glitch insensitivity);
//   (d) no other activation that may share the kernel's phase reads or
//       writes the root or anything in its combinational cone.
// Phases come from ActivationPhaseAnalysis: activations in different phases
// of a slot are separated by an NBA region and cannot interleave. A root that
// nothing reads keeps its NBA, which unread-NBA elimination removes entirely.
// The stores keep the NBA execution order (4.6(b)); RHS and target operands
// are still computed at the statement (4.9.4).
//
// The kernel must run at most once between NBA regions: a second run in the
// same phase would read its own stores where the original reads values its
// NBAs have not updated yet. A kernel that runs as soon as its edge wakes it
// therefore needs every phase to see clock changes from at most one atomic
// source: the NBA region before it, or one activation of a process that
// suspends only on positive constant delays and so runs once per slot. A
// clock that rises again through an Inactive event keeps every NBA.
//
// With `defer`, a kernel's wait may instead resume it in the Inactive region,
// as `@(edge) #0` would: after every Active event of its pass (4.4.2.3, 4.5).
// The original may run its members last in that pass (4.7). Then (b) and (d)
// only concern activations that may follow it, which PostKernelAnalysis finds:
// what its own blocking writes and stores wake, transitively, other kernels,
// and other Inactive events of its phase with what they wake. Those Inactive
// events may also run before it, where the original members cannot see their
// writes, so they must not write what it reads. Same-edge samplers and
// testbench delays run before it. No activation after it may change its
// clock.
//
// A simulation may also end within a slot: `$finish` and its kind stop the
// remaining events, and final procedures read the values from before every
// pending NBA (9.2.3, 20.2). Where something that may end the simulation can
// follow the kernel, the roots final procedures read count as accessed by it.
// Every executor skips a deferred kernel once a finish is requested, so an
// Active finisher of its pass needs nothing.
//
// Cost: direct stores wake their combinational readers in the kernel's phase,
// the NBAs the kernel keeps wake theirs in the next. Logic both reach would
// settle twice per slot, so direct roots that reach it keep their NBAs too.
// NBAs of other processes wake their readers with or without the kernel.
//
// The kernel never suspends between its members. 4.7 permits interleaving
// other processes inside an activation but does not require it, so nothing
// can observe a member part way through. Monitors and strobes print in the
// Observed and Postponed regions (21.2.2, 21.2.3), and a VCD dump records
// the values each time increment ends with (21.7.2.1), so none of them sees
// when in the slot a store happened. VPI value-change callbacks could, which
// is why the pass requires VPI off; force, procedural continuous assignment
// and DPI calls make it leave the design unchanged.
//
//===----------------------------------------------------------------------===//

#include "ComputeFusion.h"
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "obelisk/Analysis/ActivationPhaseAnalysis.h"
#include "obelisk/Analysis/ClockInferenceAnalysis.h"
#include "obelisk/Analysis/DPIEffectAnalysis.h"
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Analysis/NativeStateLayoutAnalysis.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"

#include <queue>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMFUSECLOCKEDKERNELSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {

using analysis::ActivationMemoryAnalysis;

class ObeliskSimFuseClockedKernelsPass final
    : public impl::ObeliskSimFuseClockedKernelsPassBase<
          ObeliskSimFuseClockedKernelsPass> {
public:
  using Base::Base;
  void runOnOperation() override;

private:
  Statistic kernels{this, "kernels", "clocked kernels formed"};
  Statistic fusedMembers{this, "fused-members",
                         "edge-triggered processes fused into kernels"};
  Statistic directRoots{this, "direct-roots",
                        "NBA roots stored at their statements"};
  Statistic snapshotRoots{this, "snapshot-roots",
                          "direct NBA roots read through a snapshot"};
  Statistic delayedRoots{this, "delayed-roots",
                         "direct NBA roots updated at the end of the kernel"};
  Statistic delayedSites{this, "delayed-sites",
                         "NBA statements delayed to the end of the kernel"};
  Statistic retainedRoots{this, "retained-roots",
                          "kernel NBA roots kept on the NBA region"};
  Statistic directSites{this, "direct-sites",
                        "NBA statements replaced by direct stores"};
};

/// Snapshots copy the root once per activation. Wider roots keep their NBA.
constexpr uint64_t maximumSnapshotBits = 2048;

/// A delayed update keeps its value and target operands, sets a flag and
/// tests it at the end of the kernel: about as much work as copying this many
/// bits of a snapshot. Roots read late whose snapshot is cheaper than their
/// delayed updates use the snapshot.
constexpr uint64_t delayedUpdateBits = 256;

struct Member {
  sim::SimFuncOp function;
  sim::SimSpawnOp spawn;
  Block *wait = nullptr;
  Block *body = nullptr;
  SmallVector<Block *> bodyBlocks;
  SmallVector<Operation *> preamble;
  uint64_t clock = 0;
  sim::EdgeKind edge = sim::EdgeKind::Change;
  /// The root a net clock follows through exact pure copies, whether the
  /// copies complement it, and every root on that copy chain.
  uint64_t origin = 0;
  bool inverted = false;
  SmallVector<uint64_t, 2> clockChain;
  uint32_t order = UINT32_MAX;
  llvm::DenseSet<uint64_t> reads, nbaWrites, writes;
  /// An update whose root is unresolved.
  bool unresolvedUpdate = false;
  /// A read whose root is unresolved.
  bool unresolvedRead = false;
  SmallVector<unsigned> fusedArguments;
};

bool isSuspension(Operation *operation) {
  return sim::isSuspensionOp(operation);
}

std::optional<uint64_t> effectRoot(schedule::ComputeEffectAttr effect) {
  if (effect.getTarget() != schedule::ComputeTargetKind::Descriptor)
    return std::nullopt;
  return ActivationMemoryAnalysis::getRoot(effect.getResource(),
                                           effect.getDescriptor());
}

bool isAccess(schedule::ComputeEffectKind kind) {
  return kind == schedule::ComputeEffectKind::Read ||
         kind == schedule::ComputeEffectKind::Write ||
         kind == schedule::ComputeEffectKind::Drive ||
         kind == schedule::ComputeEffectKind::NBA ||
         kind == schedule::ComputeEffectKind::Trigger;
}

bool isWrite(schedule::ComputeEffectKind kind) {
  return kind == schedule::ComputeEffectKind::Write ||
         kind == schedule::ComputeEffectKind::Drive ||
         kind == schedule::ComputeEffectKind::NBA;
}

/// The storage root of a whole-variable, constant-address handle.
std::optional<uint64_t> wholeStorageRoot(Value value,
                                         const analysis::HandleFacts &facts) {
  auto found = facts.find(value);
  if (found == facts.end() || !found->second.descriptor ||
      found->second.resource != schedule::ComputeResourceKind::Storage ||
      found->second.dynamic || found->second.low != 0 ||
      found->second.width != found->second.rootWidth)
    return std::nullopt;
  return *found->second.descriptor;
}

/// The storage root a handle may select, if it is a known storage root.
std::optional<uint64_t> storageRoot(Value value,
                                    const analysis::HandleFacts &facts) {
  auto found = facts.find(value);
  if (found == facts.end() || !found->second.descriptor ||
      found->second.resource != schedule::ComputeResourceKind::Storage)
    return std::nullopt;
  return *found->second.descriptor;
}

/// A one-bit clock: four-state `logic` or two-state `bit`.
bool isClockBit(Type type) {
  return isa<sim::LogicType, IntegerType>(type) &&
         sim::getPackedWidth(type) == 1u;
}

/// Remove the termination polls LowerUnit leaves after direct calls. Fusion
/// eligibility excludes every scheduler-writing operation from the members
/// and their callees, so these polls cannot observe a new request during the
/// kernel activation.
void removeTerminationPolls(sim::SimFuncOp function) {
  SmallVector<cf::CondBranchOp> polls;
  function.walk([&](cf::CondBranchOp branch) {
    auto requested =
        branch.getCondition().getDefiningOp<sim::SimTerminationRequestedOp>();
    if (requested &&
        isa<sim::SimReturnOp>(branch.getTrueDest()->getTerminator()))
      polls.push_back(branch);
  });
  OpBuilder builder(function.getContext());
  for (cf::CondBranchOp branch : polls) {
    auto requested =
        branch.getCondition().getDefiningOp<sim::SimTerminationRequestedOp>();
    Block *source = branch->getBlock();
    Block *continuation = branch.getFalseDest();
    SmallVector<Value> operands(branch.getFalseDestOperands());
    Location location = branch.getLoc();
    branch.erase();
    if (requested->use_empty())
      requested.erase();
    builder.setInsertionPointToEnd(source);
    cf::BranchOp::create(builder, location, continuation, operands);
  }
  for (auto block = function.getBody().begin(), end = function.getBody().end();
       block != end;) {
    Block &current = *block++;
    if (&current != &function.getBody().front() &&
        current.hasNoPredecessors() && current.without_terminator().empty() &&
        isa<sim::SimReturnOp>(current.getTerminator()))
      current.erase();
  }
}

bool collectBodyBlocks(Member &member) {
  SmallVector<Block *> pending{member.body};
  llvm::SmallPtrSet<Block *, 16> visited;
  Block *entry = &member.function.getBody().front();
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (block == member.wait || !visited.insert(block).second)
      continue;
    if (block == entry)
      return false;
    member.bodyBlocks.push_back(block);
    Operation *terminator = block->getTerminator();
    if (isSuspension(terminator))
      return false;
    if (isa<sim::SimReturnOp>(terminator)) {
      // Only termination-poll exits are accepted; they are removed after
      // fusion.
      for (Block *predecessor : block->getPredecessors()) {
        auto branch = dyn_cast<cf::CondBranchOp>(predecessor->getTerminator());
        if (!branch || branch.getTrueDest() != block ||
            !branch.getCondition()
                 .getDefiningOp<sim::SimTerminationRequestedOp>())
          return false;
      }
      if (block->hasNoPredecessors())
        return false;
      continue;
    }
    if (!isa<BranchOpInterface>(terminator))
      return false;
    for (Block *successor : terminator->getSuccessors())
      if (successor != member.wait)
        pending.push_back(successor);
  }
  if (member.bodyBlocks.size() + 2 !=
      member.function.getBody().getBlocks().size())
    return false;
  DenseMap<Block *, unsigned> position;
  for (auto [index, block] : llvm::enumerate(member.function.getBody()))
    position[&block] = index;
  llvm::sort(member.bodyBlocks, [&](Block *lhs, Block *rhs) {
    return position[lhs] < position[rhs];
  });
  return true;
}

/// Operations defined in the entry block and used by the activation.
void collectPreamble(Member &member) {
  Block &entry = member.function.getBody().front();
  llvm::SmallPtrSet<Operation *, 16> needed;
  SmallVector<Value> pending;
  pending.append(member.wait->getTerminator()->operand_begin(),
                 member.wait->getTerminator()->operand_end());
  for (Operation &operation : member.wait->without_terminator())
    pending.append(operation.operand_begin(), operation.operand_end());
  for (Block *block : member.bodyBlocks)
    block->walk([&](Operation *operation) {
      pending.append(operation->operand_begin(), operation->operand_end());
    });
  while (!pending.empty()) {
    Operation *definition = pending.pop_back_val().getDefiningOp();
    if (!definition || definition->getBlock() != &entry ||
        definition == entry.getTerminator() ||
        !needed.insert(definition).second)
      continue;
    pending.append(definition->operand_begin(), definition->operand_end());
  }
  for (Operation &operation : entry.without_terminator())
    if (needed.contains(&operation))
      member.preamble.push_back(&operation);
}

/// The NBA classes (roots, or individual sites) of the current kernel
/// activation that may already have executed. Such an NBA has not updated its
/// root yet (4.9.4), so a direct store there would be visible early to a
/// later read of the root.
class PendingLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  ChangeResult join(const llvm::BitVector &more) {
    if (more.size() > roots.size())
      roots.resize(more.size());
    llvm::BitVector next = roots;
    next |= more;
    if (next == roots)
      return ChangeResult::NoChange;
    roots = std::move(next);
    return ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    return join(static_cast<const PendingLattice &>(rhs).roots);
  }
  void print(raw_ostream &os) const override { os << roots.count(); }
  llvm::BitVector roots;
};

class PendingNBAAnalysis final
    : public dataflow::DenseForwardDataFlowAnalysis<PendingLattice> {
public:
  PendingNBAAnalysis(DataFlowSolver &solver,
                     const DenseMap<Operation *, unsigned> &enqueues,
                     unsigned roots)
      : DenseForwardDataFlowAnalysis(solver), enqueues(enqueues), roots(roots) {
  }
  LogicalResult visitOperation(Operation *op, const PendingLattice &before,
                               PendingLattice *after) override {
    llvm::BitVector state = before.roots;
    state.resize(roots);
    if (auto found = enqueues.find(op); found != enqueues.end())
      state.set(found->second);
    propagateIfChanged(after, after->join(state));
    return success();
  }
  void visitBlockTransfer(Block *block, ProgramPoint *point, Block *predecessor,
                          const PendingLattice &before,
                          PendingLattice *after) override {
    // The kernel's wait starts a new activation after the NBA region.
    if (sim::isSuspensionOp(predecessor->getTerminator())) {
      propagateIfChanged(after, after->join(llvm::BitVector(roots)));
      return;
    }
    propagateIfChanged(after, after->join(before.roots));
  }
  void setToEntryState(PendingLattice *lattice) override {
    propagateIfChanged(lattice, lattice->join(llvm::BitVector(roots)));
  }

private:
  const DenseMap<Operation *, unsigned> &enqueues;
  unsigned roots;
};

/// Whether a root is in a combinational cone.
class ConeState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  ChangeResult reach() {
    if (reached)
      return ChangeResult::NoChange;
    reached = true;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override { os << reached; }
  bool reached = false;
};

class ConeRoot final
    : public GenericLatticeAnchorBase<ConeRoot,
                                      std::pair<uint64_t, Operation *>> {
public:
  using Base::Base;
  Location getLoc() const override { return getValue().second->getLoc(); }
  void print(raw_ostream &os) const override {
    os << "cone of root " << getValue().first;
  }
};

/// Propagation through side-effect-free combinational processes, which
/// republish the changes of the roots they wait on into the roots they write.
/// Toward sources, a process that writes a reached root reaches every root it
/// waits on: they can affect the seeds. Toward readers, a process that waits
/// on a reached root reaches every root it writes: the seeds wake it.
class ConeAnalysis final : public DataFlowAnalysis {
public:
  struct Process {
    sim::SimFuncOp function;
    SmallVector<uint64_t> watched, written;
  };
  enum class Direction { Sources, Readers };
  ConeAnalysis(DataFlowSolver &solver, Operation *design,
               ArrayRef<Process> processes, ArrayRef<uint64_t> seeds,
               Direction direction)
      : DataFlowAnalysis(solver), design(design), processes(processes),
        seeds(seeds), direction(direction) {
    registerAnchorKind<ConeRoot>();
  }
  LogicalResult initialize(Operation *) override {
    for (uint64_t root : seeds)
      propagateIfChanged(state(root), state(root)->reach());
    for (const Process &process : processes)
      if (failed(visit(pointOf(process))))
        return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    auto found = byPoint.find(point);
    if (found == byPoint.end())
      return success();
    const Process &process = *found->second;
    bool toSources = direction == Direction::Sources;
    ArrayRef<uint64_t> from = toSources ? process.written : process.watched;
    ArrayRef<uint64_t> to = toSources ? process.watched : process.written;
    if (llvm::none_of(from, [&](uint64_t root) {
          return getOrCreateFor<ConeState>(point, anchor(root))->reached;
        }))
      return success();
    for (uint64_t root : to)
      propagateIfChanged(state(root), state(root)->reach());
    return success();
  }
  /// Whether a solved cone contains a root.
  static bool contains(DataFlowSolver &solver, Operation *design,
                       uint64_t root) {
    const auto *state = solver.lookupState<ConeState>(
        solver.getLatticeAnchor<ConeRoot>(std::make_pair(root, design)));
    return state && state->reached;
  }

private:
  ConeRoot *anchor(uint64_t root) {
    return getLatticeAnchor<ConeRoot>(std::make_pair(root, design));
  }
  ConeState *state(uint64_t root) {
    return getOrCreate<ConeState>(anchor(root));
  }
  ProgramPoint *pointOf(const Process &process) {
    sim::SimFuncOp function = process.function;
    ProgramPoint *point = getProgramPointBefore(&function.getBody().front());
    byPoint[point] = &process;
    return point;
  }
  Operation *design;
  ArrayRef<Process> processes;
  ArrayRef<uint64_t> seeds;
  Direction direction;
  DenseMap<ProgramPoint *, const Process *> byPoint;
};

/// Whether an operation may end the simulation (IEEE 1800-2023 20.2, 24.7):
/// `$finish`, `$stop` (batch), `$fatal` and `$exit`, or a call that may.
/// Task calls and spawns start separate activations, whose own blocks are
/// checked where they run; other calls without a known body are opaque.
bool endsSimulation(Operation *operation, const SymbolTable &symbols,
                    function_ref<bool(sim::SimFuncOp)> calleeEnds) {
  if (isa<sim::SimFinishOp, sim::SimStopOp, sim::SimFatalOp,
          sim::SimProgramExitOp>(operation))
    return true;
  if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
    auto callee = symbols.lookup<sim::SimFuncOp>(call.getCallee());
    return !callee || callee.isExternal() || calleeEnds(callee);
  }
  return isa<CallOpInterface>(operation) &&
         !isa<sim::SimSpawnOp, sim::SimTaskCallOp>(operation);
}

/// Whether a function may end the simulation.
class EndsSimulationState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  ChangeResult set() {
    if (ends)
      return ChangeResult::NoChange;
    ends = true;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override { os << ends; }
  bool ends = false;
};

/// Bottom-up over plain calls: a function may end the simulation when one of
/// its operations may (see endsSimulation). States are anchored at function
/// entries.
class EndsSimulationAnalysis final : public DataFlowAnalysis {
public:
  EndsSimulationAnalysis(DataFlowSolver &solver, const SymbolTable &symbols)
      : DataFlowAnalysis(solver), symbols(symbols) {}
  LogicalResult initialize(Operation *top) override {
    top->walk([&](sim::SimFuncOp function) {
      if (!function.isExternal())
        (void)visit(getProgramPointBefore(&function.getBody().front()));
    });
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    Block *entry = point->getBlock();
    auto function = entry ? dyn_cast<sim::SimFuncOp>(entry->getParentOp())
                          : sim::SimFuncOp{};
    if (!function || entry != &function.getBody().front())
      return success();
    bool ends = function
                    .walk([&](Operation *operation) {
                      return endsSimulation(
                                 operation, symbols,
                                 [&](sim::SimFuncOp callee) {
                                   return getOrCreateFor<EndsSimulationState>(
                                              point, entryOf(callee))
                                       ->ends;
                                 })
                                 ? WalkResult::interrupt()
                                 : WalkResult::advance();
                    })
                    .wasInterrupted();
    if (ends) {
      auto *state = getOrCreate<EndsSimulationState>(point);
      propagateIfChanged(state, state->set());
    }
    return success();
  }
  /// Whether a solved function may end the simulation.
  static bool ends(DataFlowSolver &solver, sim::SimFuncOp function) {
    if (function.isExternal())
      return true;
    const auto *state = solver.lookupState<EndsSimulationState>(
        solver.getProgramPointBefore(&function.getBody().front()));
    return state && state->ends;
  }

private:
  ProgramPoint *entryOf(sim::SimFuncOp function) {
    return getProgramPointBefore(&function.getBody().front());
  }
  const SymbolTable &symbols;
};

/// Whether a program point may execute after a deferred kernel, within a
/// phase the kernel runs in and before that phase's NBA region.
class AfterLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  ChangeResult join(bool more) {
    if (after || !more)
      return ChangeResult::NoChange;
    after = true;
    return ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    return join(static_cast<const AfterLattice &>(rhs).after);
  }
  void print(raw_ostream &os) const override { os << after; }
  bool after = false;
};

/// Whether a root may change, or a process or task may start or return,
/// after a deferred kernel within its phase.
class AfterState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  ChangeResult set() {
    if (after)
      return ChangeResult::NoChange;
    after = true;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override { os << after; }
  bool after = false;
};

/// A root changed after the kernel; `anyRoot` stands for an unresolved one.
class AfterRoot final
    : public GenericLatticeAnchorBase<AfterRoot,
                                      std::pair<uint64_t, Operation *>> {
public:
  using Base::Base;
  static constexpr uint64_t anyRoot = ~uint64_t{0};
  Location getLoc() const override { return getValue().second->getLoc(); }
  void print(raw_ostream &os) const override {
    os << "change after kernel of root " << getValue().first;
  }
};

/// The start (0) or the return (1) of a process or task.
class AfterEntry final
    : public GenericLatticeAnchorBase<AfterEntry,
                                      std::pair<Operation *, unsigned>> {
public:
  using Base::Base;
  Location getLoc() const override { return getValue().first->getLoc(); }
  void print(raw_ostream &os) const override {
    os << (getValue().second ? "return" : "start") << " after kernel";
  }
};

/// IEEE 1800-2023 4.4.2.3 and 4.5: a deferred kernel resumes in the Inactive
/// region, after every Active event of its pass. Within its phase, what may run
/// after it is what updates made after it wake: its own blocking writes and
/// direct stores, and transitively the blocking writes and event triggers of
/// every activation they wake. NBAs land in the next phase and positive delays
/// in a later slot. Other kernels of the phase are deferred as well, so they
/// may run after it. Zero and variable delays, waits on unresolved roots and
/// waits this analysis does not model are conservatively after it.
class PostKernelAnalysis final
    : public dataflow::DenseForwardDataFlowAnalysis<AfterLattice> {
public:
  struct Config {
    Operation *design;
    const SymbolTable &symbols;
    const analysis::ActivationPhaseAnalysis &phases;
    analysis::ActivationPhaseAnalysis::Phases kernel;
    sim::SimFuncOp rootInitializer;
    /// The kernel's members, which are the kernel itself.
    DenseSet<Operation *> members;
    /// Waits of other kernels' members.
    DenseSet<Operation *> otherKernelWaits;
    /// Roots the kernel itself may change in its phase.
    SmallVector<uint64_t> changed;
  };
  PostKernelAnalysis(DataFlowSolver &solver, const Config &config)
      : DenseForwardDataFlowAnalysis(solver), config(config) {
    registerAnchorKind<AfterRoot>();
    registerAnchorKind<AfterEntry>();
  }
  LogicalResult initialize(Operation *top) override {
    for (uint64_t root : config.changed)
      propagateIfChanged(rootState(root), rootState(root)->set());
    return DenseForwardDataFlowAnalysis::initialize(top);
  }
  LogicalResult visitOperation(Operation *op, const AfterLattice &before,
                               AfterLattice *after) override {
    propagateIfChanged(after, after->join(before.after));
    auto function = op->getParentOfType<sim::SimFuncOp>();
    if (!before.after || !function || config.members.contains(function))
      return success();
    auto mark = [&](AfterState *state) {
      propagateIfChanged(state, state->set());
    };
    if (auto spawn = dyn_cast<sim::SimSpawnOp>(op)) {
      if (auto callee = lookup(spawn.getCalleeAttr().getAttr()))
        mark(entryState(callee, /*returns=*/false));
    } else if (auto call = dyn_cast<sim::SimTaskCallOp>(op)) {
      if (auto callee = lookup(call.getCalleeAttr().getAttr()))
        mark(entryState(callee, /*returns=*/false));
    } else if (isa<sim::SimReturnOp>(op)) {
      mark(entryState(function, /*returns=*/true));
    }
    if (op != op->getBlock()->getTerminator())
      return success();
    // Blocking writes and event triggers of this activation, including the
    // summaries of what it calls, wake waiters in the same phase.
    ArrayAttr effects = config.phases.getFragmentEffects(op->getBlock());
    if (!effects)
      return success();
    for (auto effect : effects.getAsRange<schedule::ComputeEffectAttr>()) {
      schedule::ComputeEffectKind kind = effect.getEffect();
      if (effect.getTarget() == schedule::ComputeTargetKind::Local ||
          (kind != schedule::ComputeEffectKind::Write &&
           kind != schedule::ComputeEffectKind::Drive &&
           kind != schedule::ComputeEffectKind::Trigger))
        continue;
      std::optional<SmallVector<uint64_t>> roots =
          config.phases.getEffectRoots(function.getSymNameAttr(), effect);
      if (!roots) {
        mark(rootState(AfterRoot::anyRoot));
        continue;
      }
      for (uint64_t root : *roots)
        mark(rootState(root));
    }
    return success();
  }
  void visitBlockTransfer(Block *block, ProgramPoint *point, Block *predecessor,
                          const AfterLattice &before,
                          AfterLattice *after) override {
    auto function = dyn_cast<sim::SimFuncOp>(block->getParentOp());
    if (!function || config.members.contains(function))
      return;
    auto join = [&](bool more) {
      propagateIfChanged(after, after->join(more));
    };
    Operation *terminator = predecessor->getTerminator();
    if (!sim::isSuspensionOp(terminator)) {
      join(before.after);
      return;
    }
    if (auto call = dyn_cast<sim::SimTaskCallOp>(terminator)) {
      StringAttr callee = call.getCalleeAttr().getAttr();
      if (!config.phases.taskSuspends(callee)) {
        join(before.after);
        return;
      }
      auto task = lookup(callee);
      join(!task || getOrCreateFor<AfterState>(
                        point, getLatticeAnchor<AfterEntry>(
                                   std::make_pair(task.getOperation(), 1u)))
                        ->after);
      return;
    }
    if (!config.phases.getPhases(block).meets(config.kernel))
      return;
    if (config.otherKernelWaits.contains(terminator)) {
      join(true);
      return;
    }
    if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(terminator)) {
      auto constant = delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
      join(!constant || constant.getValue() == 0);
      return;
    }
    if (isa<sim::SimSuspendForeverOp>(terminator))
      return;
    SmallVector<Value> watched;
    if (auto edge = dyn_cast<sim::SimSuspendEdgeOp>(terminator))
      watched.push_back(edge.getWatched());
    else if (auto change = dyn_cast<sim::SimSuspendChangeOp>(terminator))
      watched.push_back(change.getWatched());
    else if (auto any = dyn_cast<sim::SimSuspendAnyOp>(terminator))
      llvm::append_range(watched, any.getWatched());
    else if (auto edge = dyn_cast<sim::SimSuspendEdgeIffOp>(terminator))
      watched.push_back(edge.getWatched());
    else if (auto level = dyn_cast<sim::SimSuspendLevelOp>(terminator))
      watched.push_back(level.getWatched());
    else if (auto event = dyn_cast<sim::SimSuspendEventOp>(terminator))
      watched.push_back(event.getEvent());
    else {
      join(true);
      return;
    }
    bool woken = changedFor(point, AfterRoot::anyRoot);
    for (Value value : watched) {
      std::optional<uint64_t> root = config.phases.getRoot(function, value);
      woken |= !root || changedFor(point, *root);
    }
    join(woken);
  }
  void setToEntryState(AfterLattice *lattice) override {
    auto *point = dyn_cast_if_present<ProgramPoint *>(lattice->getAnchor());
    Block *block = point ? point->getBlock() : nullptr;
    auto function = block ? dyn_cast<sim::SimFuncOp>(block->getParentOp())
                          : sim::SimFuncOp{};
    // Plain functions run inside their callers, whose fragments carry their
    // effects; static initialization precedes every slot.
    if (!function || function == config.rootInitializer ||
        config.members.contains(function) ||
        function.getEntryKind() == sim::EntryKind::Function)
      return;
    if (!config.phases.isModeledStart(function.getSymNameAttr())) {
      propagateIfChanged(
          lattice,
          lattice->join(config.phases.getPhases(block).meets(config.kernel)));
      return;
    }
    propagateIfChanged(
        lattice,
        lattice->join(getOrCreateFor<AfterState>(
                          point, getLatticeAnchor<AfterEntry>(std::make_pair(
                                     function.getOperation(), 0u)))
                          ->after));
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction action,
                                    const AfterLattice &before,
                                    AfterLattice *after) override {
    // Spawns and calls are not interprocedural edges here: process and task
    // entries are modeled by their anchors, plain calls by fragment effects.
    (void)action;
    (void)visitOperation(call, before, after);
  }

private:
  sim::SimFuncOp lookup(StringAttr name) {
    return config.symbols.lookup<sim::SimFuncOp>(name);
  }
  AfterState *rootState(uint64_t root) {
    return getOrCreate<AfterState>(
        getLatticeAnchor<AfterRoot>(std::make_pair(root, config.design)));
  }
  AfterState *entryState(sim::SimFuncOp function, bool returns) {
    return getOrCreate<AfterState>(getLatticeAnchor<AfterEntry>(
        std::make_pair(function.getOperation(), unsigned{returns})));
  }
  bool changedFor(ProgramPoint *point, uint64_t root) {
    return getOrCreateFor<AfterState>(
               point,
               getLatticeAnchor<AfterRoot>(std::make_pair(root, config.design)))
        ->after;
  }
  const Config &config;
};

/// Union-find over storage roots connected by clock copies.
struct ClockClasses {
  DenseMap<uint64_t, uint64_t> parent;
  uint64_t find(uint64_t root) {
    auto found = parent.find(root);
    if (found == parent.end() || found->second == root)
      return root;
    uint64_t top = find(found->second);
    parent[root] = top;
    return top;
  }
  void join(uint64_t source, uint64_t destination) {
    parent.try_emplace(source, source);
    parent[find(destination)] = find(source);
  }
};

void ObeliskSimFuseClockedKernelsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  ModuleOp module = design->getParentOfType<ModuleOp>();
  bool report = module && native::has<native::DebugTiming>(module);
  schedule::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph || graph.getVpi() != schedule::ComputeVPIMode::Off ||
      graph.getWorkers() != 1) {
    if (report)
      llvm::errs() << "obelisk clocked kernel: skipped design: graph\n";
    return;
  }
  llvm::StringMap<uint64_t> rejections;
  auto reject = [&](StringRef reason) { ++rejections[reason]; };

  // Force, procedural continuous assignment and foreign code can update or
  // observe any root outside the scheduler regions modeled below. Foreign
  // code reaches the design only through context exports (IEEE 1800-2023
  // 35.5.1.3, 35.5.3).
  bool unsupportedDesign =
      analysis::DPIEffectAnalysis(design).anyCallMayAccessDesign();
  design.walk([&](Operation *operation) {
    unsupportedDesign |=
        isa<sim::SimOverrideOp, sim::SimDynamicOverrideOp,
            sim::SimReleaseOverrideOp>(operation);
  });
  auto bail = [&](StringRef reason) {
    if (report)
      llvm::errs() << "obelisk clocked kernel: skipped design: " << reason
                   << '\n';
  };
  if (unsupportedDesign)
    return bail("override or foreign call");

  SymbolTable symbols(design);
  analysis::HandleDataflowAnalysis provenance(design);
  ArrayAttr nodes = graph.getNodes();

  sim::SimFuncOp rootInitializer;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>())
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      rootInitializer = function;
  if (!rootInitializer)
    return bail("no root initializer");

  DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> spawns;
  design.walk([&](sim::SimSpawnOp spawn) {
    spawns[spawn.getCalleeAttr().getAttr()].push_back(spawn);
  });
  auto rootSpawn = [&](sim::SimFuncOp function) -> sim::SimSpawnOp {
    auto found = spawns.find(function.getSymNameAttr());
    if (found == spawns.end() || found->second.size() != 1)
      return {};
    sim::SimSpawnOp spawn = found->second.front();
    if (spawn->getBlock() != &rootInitializer.getBody().front() ||
        !spawn.getResult().use_empty())
      return {};
    return spawn;
  };
  // Every symbol reference other than the root spawn keeps a function alive.
  DenseMap<StringAttr, unsigned> symbolUseCounts;
  if (auto uses = SymbolTable::getSymbolUses(&design.getBody()))
    for (const SymbolTable::SymbolUse &use : *uses)
      ++symbolUseCounts[use.getSymbolRef().getRootReference()];

  // Fragment inventory by function and block.
  DenseMap<StringAttr, SmallVector<schedule::ComputeFragmentAttr>> fragments;
  for (Attribute attribute : nodes)
    if (auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute))
      fragments[fragment.getFunction().getAttr()].push_back(fragment);
  auto blockAt = [](sim::SimFuncOp function, uint32_t index) -> Block * {
    for (Block &block : function.getBody())
      if (index-- == 0)
        return &block;
    return nullptr;
  };

  // Schedule order of Active fragments, used to keep the existing relative
  // order of cohort members wherever no dependence decides it.
  DenseMap<uint32_t, uint32_t> scheduleOrder;
  {
    uint32_t next = 0;
    for (Attribute regionAttribute : graph.getRegions()) {
      auto region = cast<schedule::ComputeRegionAttr>(regionAttribute);
      if (region.getKind() != schedule::ComputeRegionKind::Active)
        continue;
      for (Attribute groupAttribute : region.getGroups())
        for (int64_t member : cast<schedule::ComputeGroupAttr>(groupAttribute)
                                  .getFragments()
                                  .asArrayRef())
          scheduleOrder.try_emplace(static_cast<uint32_t>(member), next++);
    }
  }

  // Time-slot phases of every activation and root change (4.5).
  analysis::ActivationPhaseAnalysis phases(design, provenance);
  if (!phases.isValid())
    return bail("phase analysis");
  using Phases = analysis::ActivationPhaseAnalysis::Phases;
  using PhaseSet = analysis::ActivationPhaseAnalysis::PhaseSet;

  // Writers and watchers of each root from the verified graph.
  struct Writer {
    StringAttr function;
    uint32_t block;
    schedule::ComputeEffectKind kind;
    /// An intra-assignment delayed NBA, which may land in any NBA region.
    bool deferred = false;
  };
  DenseMap<uint64_t, SmallVector<Writer>> writers;
  DenseMap<uint64_t, SmallVector<StringAttr>> watchers;
  DenseSet<uint64_t> readRoots;
  bool unknownRead = false;
  // Writes of an unresolved target may update any root.
  SmallVector<Writer> unknownWriters;
  auto effectRoots = [&](StringAttr function,
                         schedule::ComputeEffectAttr effect) {
    return phases.getEffectRoots(function, effect);
  };
  for (Attribute attribute : nodes) {
    auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute);
    if (!fragment)
      continue;
    StringAttr function = fragment.getFunction().getAttr();
    for (auto effect :
         fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
      if (effect.getTarget() == schedule::ComputeTargetKind::Local)
        continue;
      std::optional<SmallVector<uint64_t>> roots =
          effectRoots(function, effect);
      if (isWrite(effect.getEffect()) && !roots) {
        unknownWriters.push_back({function, fragment.getBlock(),
                                  effect.getEffect(), effect.getDeferred()});
        continue;
      }
      bool reads = effect.getEffect() == schedule::ComputeEffectKind::Read ||
                   effect.getEffect() == schedule::ComputeEffectKind::Watch;
      if (!roots) {
        unknownRead |= reads;
        continue;
      }
      for (uint64_t root : *roots) {
        if (reads)
          readRoots.insert(root);
        if (isWrite(effect.getEffect()))
          writers[root].push_back({function, fragment.getBlock(),
                                   effect.getEffect(), effect.getDeferred()});
        if (effect.getEffect() == schedule::ComputeEffectKind::Watch)
          watchers[root].push_back(function);
      }
    }
  }

  CombinationalFusionAnalysis combinational(
      design, provenance,
      ActivationMemoryAnalysis::DependencyMode::ReachingValues);
  const llvm::DenseSet<uint64_t> &glitchInsensitive =
      combinational.getGlitchInsensitiveStorage();
  DenseMap<StringAttr, bool> transparentCache;
  auto isTransparent = [&](StringAttr name) {
    auto [found, inserted] = transparentCache.try_emplace(name, false);
    if (inserted)
      if (auto function = symbols.lookup<sim::SimFuncOp>(name))
        found->second =
            combinational
                .analyze(function, provenance, /*interleaved=*/true, nullptr,
                         /*finalValuesOnly=*/true)
                .has_value();
    return found->second;
  };

  // 1. Clock alias classes: roots connected by proven copies, which change
  // in exactly the phases of their source.
  ClockClasses classes;
  for (auto &[destination, copy] : phases.getCopies())
    classes.join(copy.source, destination);

  // A clock usually reaches a module as a one-bit net: an input port
  // connection or a net declaration assignment such as `wire c = ~clk;`.
  // Each is a continuous assignment (IEEE 1800-2023 10.3.1, 23.3.3) that
  // updates its net only when the value changes (10.3.2). The clock analysis
  // follows exact, exclusive pure copies of this kind back to the variable
  // they all copy, recording whether an odd number of them complement it.
  std::unique_ptr<analysis::NativeStateLayoutAnalysis> clockLayout;
  std::unique_ptr<analysis::ClockInferenceAnalysis> clockCopies;
  DenseMap<uint64_t, uint64_t> clockRootAt;
  bool clockCopiesComputed = false;
  auto computeClockCopies = [&] {
    clockCopiesComputed = true;
    if (!module)
      return;
    // A design the native layout rejects keeps storage clocks only; that
    // rejection is reported by the backend that needs the layout.
    FailureOr<analysis::NativeStateLayoutAnalysis> layout;
    {
      ScopedDiagnosticHandler quiet(design.getContext(),
                                    [](Diagnostic &) { return success(); });
      layout = analysis::NativeStateLayoutAnalysis::compute(module);
    }
    if (failed(layout))
      return;
    clockLayout =
        std::make_unique<analysis::NativeStateLayoutAnalysis>(*layout);
    clockCopies = std::make_unique<analysis::ClockInferenceAnalysis>(
        module, *clockLayout, ArrayRef<analysis::PeriodicClockSeed>{});
    // One-bit roots by physical bit. Aliased descriptors share a bit; the
    // smallest root identifies it deterministically.
    DenseMap<uint64_t, unsigned> widths;
    for (const auto &bound : clockLayout->bounds)
      if (bound.width)
        widths.try_emplace(bound.offset, bound.width);
    auto index = [&](const DenseMap<uint64_t, uint64_t> &offsets,
                     schedule::ComputeResourceKind resource) {
      for (auto [descriptor, offset] : offsets) {
        std::optional<uint64_t> root =
            ActivationMemoryAnalysis::getRoot(resource, descriptor);
        if (widths.lookup(offset) != 1 || !root)
          continue;
        auto [found, inserted] = clockRootAt.try_emplace(offset, *root);
        if (!inserted)
          found->second = std::min(found->second, *root);
      }
    };
    index(clockLayout->storageOffsets, schedule::ComputeResourceKind::Storage);
    index(clockLayout->netOffsets, schedule::ComputeResourceKind::Net);
  };
  // Follows a whole one-bit net clock to its origin root.
  auto resolveNetClock = [&](Value watched, const analysis::HandleFacts &facts,
                             Member &member) {
    auto found = facts.find(watched);
    if (found == facts.end() ||
        found->second.resource != schedule::ComputeResourceKind::Net ||
        !found->second.descriptor || found->second.dynamic ||
        found->second.low != 0 || found->second.width != 1 ||
        found->second.rootWidth != 1)
      return false;
    if (!clockCopiesComputed)
      computeClockCopies();
    if (!clockCopies)
      return false;
    std::optional<analysis::ClockBit> bit = clockCopies->getBit(found->second);
    std::optional<uint64_t> clock = ActivationMemoryAnalysis::getRoot(
        schedule::ComputeResourceKind::Net, *found->second.descriptor);
    if (!bit || !clock)
      return false;
    member.clock = *clock;
    member.clockChain = {*clock};
    member.inverted = false;
    DenseSet<analysis::ClockBit> visited{*bit};
    while (std::optional<analysis::ClockOrigin> source =
               clockCopies->getCopySource(*bit)) {
      auto root = clockRootAt.find(source->bit.second);
      if (root == clockRootAt.end() || !visited.insert(source->bit).second)
        return false;
      bit = source->bit;
      member.inverted ^= source->inverted;
      member.clockChain.push_back(root->second);
    }
    member.origin = member.clockChain.back();
    return true;
  };
  // The level a clock root holds before its first update: x for 4-state
  // variables, 0 for 2-state variables (Table 6-7), and z for nets (6.6).
  // A declaration initializer gives an initial value only that root has.
  enum InitialLevel : unsigned { Low, High, Unknown, Initialized };
  DenseMap<uint64_t, Type> storageTypes;
  for (auto declaration : design.getBody().front().getOps<sim::SimStorageDeclOp>())
    storageTypes[declaration.getId()] = declaration.getType();
  auto initialLevel = [&](uint64_t root) -> unsigned {
    if (root >= ActivationMemoryAnalysis::netRootTag)
      return Unknown;
    if (llvm::any_of(writers.lookup(root), [&](const Writer &writer) {
          return writer.function == rootInitializer.getSymNameAttr();
        }))
      return Initialized;
    Type type = storageTypes.lookup(root);
    if (isa_and_nonnull<IntegerType>(type))
      return Low;
    if (isa_and_nonnull<sim::LogicType>(type))
      return Unknown;
    return Initialized;
  };

  // 2. Clocked members, grouped by clock class, edge, and time-zero
  // behavior.
  using GroupKey = std::pair<uint64_t, unsigned>;
  llvm::MapVector<GroupKey, std::vector<Member>> groups;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>()) {
    if (function.isExternal() ||
        (function.getEntryKind() != sim::EntryKind::AlwaysFF &&
         function.getEntryKind() != sim::EntryKind::Always))
      continue;
    Member member;
    member.function = function;
    member.spawn = rootSpawn(function);
    if (!member.spawn) {
      reject("member spawn");
      continue;
    }
    if (symbolUseCounts.lookup(function.getSymNameAttr()) != 1) {
      reject("member symbol uses");
      continue;
    }
    if (function.getHomeRegion() != sim::EventRegion::Active ||
        function.getDomain() != sim::ExecutionDomain::Design) {
      reject("member region");
      continue;
    }
    sim::SimSuspendEdgeOp wait;
    unsigned suspensions = 0;
    function.walk([&](Operation *operation) {
      if (!isSuspension(operation))
        return;
      ++suspensions;
      wait = dyn_cast<sim::SimSuspendEdgeOp>(operation);
    });
    if (suspensions != 1 || !wait || wait.getResumeRegionAttr() ||
        !wait.getContinuationOperands().empty() ||
        wait->getParentRegion() != &function.getBody()) {
      reject("member wait shape");
      continue;
    }
    member.wait = wait->getBlock();
    member.body = wait.getContinuation();
    member.edge = wait.getEdge();
    Block &entry = function.getBody().front();
    auto entryBranch = dyn_cast<cf::BranchOp>(entry.getTerminator());
    if (!entryBranch || entryBranch.getDest() != member.wait ||
        !entryBranch.getDestOperands().empty() ||
        member.wait->getNumArguments() != 0 ||
        member.body->getNumArguments() != 0 || member.body == member.wait ||
        !llvm::all_of(entry.without_terminator(),
                      [](Operation &operation) {
                        return isMemoryEffectFree(&operation);
                      }) ||
        !llvm::all_of(member.wait->without_terminator(),
                      [](Operation &operation) {
                        return isMemoryEffectFree(&operation);
                      })) {
      reject("member entry shape");
      continue;
    }
    if (!collectBodyBlocks(member)) {
      reject("member body shape");
      continue;
    }
    if (!isComputeBodyFusionEligible(function, provenance)) {
      reject("member body eligibility");
      continue;
    }
    analysis::HandleFacts facts = provenance.derive(function);
    Type watchedType = wait.getWatched().getType();
    if (auto ref = dyn_cast<sim::RefType>(watchedType)) {
      std::optional<uint64_t> clock =
          wholeStorageRoot(wait.getWatched(), facts);
      if (!clock || !isClockBit(ref.getElementType())) {
        reject("member clock");
        continue;
      }
      member.clock = member.origin = *clock;
    } else {
      // An edge event of a copy is an edge event of its source (Table 9-2
      // is symmetric under complement, since `~` maps x and z to x per
      // Table 11-15); a value change is not (`~z` equals `~x`).
      auto net = dyn_cast<sim::NetType>(watchedType);
      if (!net || !isClockBit(net.getElementType()) ||
          member.edge == sim::EdgeKind::Change ||
          !resolveNetClock(wait.getWatched(), facts, member)) {
        reject("member clock");
        continue;
      }
    }
    bool native = true;
    uint32_t bodyIndex = 0;
    for (auto [index, block] : llvm::enumerate(function.getBody()))
      if (&block == member.body)
        bodyIndex = index;
    llvm::DenseSet<Block *> body(member.bodyBlocks.begin(),
                                 member.bodyBlocks.end());
    for (schedule::ComputeFragmentAttr fragment :
         fragments.lookup(function.getSymNameAttr())) {
      native &= fragment.getTier() == schedule::ComputeTierKind::Native;
      if (fragment.getBlock() == bodyIndex) {
        auto order = scheduleOrder.find(fragment.getId());
        if (order != scheduleOrder.end())
          member.order = order->second;
      }
      Block *block = blockAt(function, fragment.getBlock());
      if (!block || !body.contains(block))
        continue;
      for (auto effect :
           fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
        std::optional<uint64_t> root = effectRoot(effect);
        if (!root) {
          bool shared =
              effect.getTarget() != schedule::ComputeTargetKind::Local;
          member.unresolvedUpdate |=
              shared &&
              (isWrite(effect.getEffect()) ||
               effect.getEffect() == schedule::ComputeEffectKind::Trigger);
          member.unresolvedRead |=
              shared && effect.getEffect() == schedule::ComputeEffectKind::Read;
          continue;
        }
        switch (effect.getEffect()) {
        case schedule::ComputeEffectKind::Read:
          member.reads.insert(*root);
          break;
        case schedule::ComputeEffectKind::NBA:
          member.nbaWrites.insert(*root);
          break;
        case schedule::ComputeEffectKind::Write:
        case schedule::ComputeEffectKind::Drive:
        case schedule::ComputeEffectKind::Trigger:
          member.writes.insert(*root);
          break;
        default:
          break;
        }
      }
    }
    if (!native) {
      reject("member tier");
      continue;
    }
    collectPreamble(member);
    // Members are equivalent when their edge events coincide in every time
    // slot, including the time-zero updates of copies (4.9.1, 23.3.3.3).
    // Relative to the class, a complemented chain swaps posedge and negedge.
    // With the class source quiet at time zero (checked per kernel), a
    // variable keeps its initial level then, while a net copy moves once from
    // z to its source's value. A posedge there needs a final 1 from a level
    // other than 1 and a negedge a final 0 from a level other than 0
    // (Table 9-2), so members agree at time zero when their roots agree on
    // whether the initial level rules the edge out; any edge needs the level.
    sim::EdgeKind edge = member.edge;
    unsigned level = initialLevel(member.clock);
    if (member.inverted) {
      if (edge == sim::EdgeKind::Posedge)
        edge = sim::EdgeKind::Negedge;
      else if (edge == sim::EdgeKind::Negedge)
        edge = sim::EdgeKind::Posedge;
      if (level == Low || level == High)
        level = level == Low ? High : Low;
    }
    unsigned variant = level;
    if (level != Initialized && edge == sim::EdgeKind::Posedge)
      variant = level == High;
    else if (level != Initialized && edge == sim::EdgeKind::Negedge)
      variant = level == Low;
    groups[{classes.find(member.origin),
            static_cast<unsigned>(edge) | variant << 8}]
        .push_back(std::move(member));
  }

  // Transparent combinational processes: the roots each one waits on and
  // the roots it writes.
  std::vector<ConeAnalysis::Process> combinationalProcesses;
  {
    DenseMap<StringAttr, unsigned> index;
    for (auto &[root, list] : watchers)
      for (StringAttr watcher : list)
        if (isTransparent(watcher)) {
          auto [found, inserted] =
              index.try_emplace(watcher, combinationalProcesses.size());
          if (inserted)
            combinationalProcesses.push_back(
                {symbols.lookup<sim::SimFuncOp>(watcher), {}, {}});
          combinationalProcesses[found->second].watched.push_back(root);
        }
    for (auto &[root, list] : writers)
      for (const Writer &write : list)
        if (auto found = index.find(write.function); found != index.end())
          combinationalProcesses[found->second].written.push_back(root);
  }

  // IEEE 1800-2023 20.2 and 9.2.3: `$finish` ends the simulation at once,
  // and final procedures then read the state with no remaining scheduled
  // event executed, including NBAs still pending. Find the activations that
  // may end the simulation and the roots final procedures read.
  DataFlowSolver ending;
  ending.load<EndsSimulationAnalysis>(symbols);
  if (failed(ending.initializeAndRun(design)))
    return bail("end-of-simulation analysis");
  DenseSet<std::pair<StringAttr, uint32_t>> endingFragments;
  SmallVector<uint64_t> finalReads;
  bool unknownFinalRead = false;
  for (Attribute attribute : nodes) {
    auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute);
    if (!fragment)
      continue;
    StringAttr name = fragment.getFunction().getAttr();
    auto function = symbols.lookup<sim::SimFuncOp>(name);
    if (!function || function.isExternal())
      continue;
    if (function.getEntryKind() == sim::EntryKind::Final) {
      for (auto effect :
           fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
        if (effect.getEffect() != schedule::ComputeEffectKind::Read ||
            effect.getTarget() == schedule::ComputeTargetKind::Local)
          continue;
        if (std::optional<SmallVector<uint64_t>> roots =
                phases.getEffectRoots(name, effect))
          llvm::append_range(finalReads, *roots);
        else
          unknownFinalRead = true;
      }
      continue;
    }
    Block *block = blockAt(function, fragment.getBlock());
    if (block && llvm::any_of(*block, [&](Operation &operation) {
          return operation
              .walk([&](Operation *nested) {
                return endsSimulation(nested, symbols,
                                      [&](sim::SimFuncOp callee) {
                                        return EndsSimulationAnalysis::ends(
                                            ending, callee);
                                      })
                           ? WalkResult::interrupt()
                           : WalkResult::advance();
              })
              .wasInterrupted();
        }))
      endingFragments.insert({name, fragment.getBlock()});
  }

  // Every root of each clock alias class.
  DenseMap<uint64_t, SmallVector<uint64_t>> classMembers;
  for (auto &[destination, copy] : phases.getCopies()) {
    classMembers[classes.find(destination)].push_back(destination);
    classMembers[classes.find(copy.source)].push_back(copy.source);
  }

  // A process that suspends only on positive constant delays runs at most
  // once per time slot (9.4.1): a delay always resumes in a later slot.
  auto isPeriodic = [&](StringAttr name) {
    auto function = symbols.lookup<sim::SimFuncOp>(name);
    if (!function || function.isExternal() || !rootSpawn(function) ||
        symbolUseCounts.lookup(name) != 1)
      return false;
    bool periodic = true;
    function.walk([&](Operation *operation) {
      if (!isSuspension(operation))
        return;
      auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation);
      auto constant =
          delay ? delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>()
                : sim::SimTimeConstantOp{};
      periodic &= constant && constant.getValue() > 0;
    });
    return periodic;
  };

  // Cost: a direct store wakes the combinational logic that reads its root
  // in the kernel's phase, a retained NBA of the same activation wakes its
  // readers in the next. A process both can wake would settle twice per
  // slot, so direct roots that reach such a process keep their NBAs, which
  // may in turn reach more of them. Returns the roots to keep.
  auto secondWave = [&](SmallVector<uint64_t> later,
                        DenseSet<uint64_t> direct) -> SmallVector<uint64_t> {
    SmallVector<uint64_t> kept;
    while (!later.empty() && !direct.empty()) {
      DataFlowSolver woken;
      woken.load<ConeAnalysis>(design.getOperation(), combinationalProcesses,
                               later, ConeAnalysis::Direction::Readers);
      SmallVector<uint64_t> shared;
      bool solved = succeeded(woken.initializeAndRun(design));
      for (const ConeAnalysis::Process &process : combinationalProcesses)
        if (!solved || llvm::any_of(process.watched, [&](uint64_t root) {
              return ConeAnalysis::contains(woken, design.getOperation(), root);
            }))
          llvm::append_range(shared, process.watched);
      DataFlowSolver feeding;
      feeding.load<ConeAnalysis>(design.getOperation(), combinationalProcesses,
                                 shared, ConeAnalysis::Direction::Sources);
      solved &= succeeded(feeding.initializeAndRun(design));
      SmallVector<uint64_t> twice;
      for (uint64_t root : direct)
        if (!solved ||
            ConeAnalysis::contains(feeding, design.getOperation(), root))
          twice.push_back(root);
      if (twice.empty())
        break;
      for (uint64_t root : twice) {
        direct.erase(root);
        later.push_back(root);
        kept.push_back(root);
      }
    }
    return kept;
  };

  // Waits of every planned member, which a deferred kernel resumes after.
  DenseSet<Operation *> memberWaits;
  for (auto &[key, members] : groups)
    for (Member &member : members)
      memberWaits.insert(member.wait->getTerminator());

  // 3. Plan every kernel on the unmodified design: the analyses above
  // describe the original processes and graph, which materialization erases.
  struct Plan {
    GroupKey key;
    std::vector<Member> *members;
    DenseSet<uint64_t> eligibleRoots;
    bool deferred = false;
    SmallVector<unsigned> sequence;
    unsigned waitMember = 0;
    SmallVector<Value> operands;
    SmallVector<Type> inputTypes;
    SmallVector<DictionaryAttr> argumentAttrs;
    sim::SimSpawnOp insertionSpawn;
  };
  std::vector<Plan> plans;
  for (auto &[key, members] : groups) {
    uint64_t classRoot = key.first;
    // A member that accesses any root of its clock class could observe a
    // copy that has not run yet when the kernel starts. It stays a process of
    // its own, which the root planning below treats like any non-member.
    {
      DenseSet<uint64_t> aliasRoots{classRoot};
      if (auto found = classMembers.find(classRoot);
          found != classMembers.end())
        aliasRoots.insert(found->second.begin(), found->second.end());
      for (const Member &member : members) {
        aliasRoots.insert(member.clock);
        aliasRoots.insert(member.clockChain.begin(), member.clockChain.end());
      }
      auto isAlias = [&](uint64_t root) { return aliasRoots.contains(root); };
      size_t count = members.size();
      llvm::erase_if(members, [&](const Member &member) {
        return llvm::any_of(member.reads, isAlias) ||
               llvm::any_of(member.writes, isAlias) ||
               llvm::any_of(member.nbaWrites, isAlias);
      });
      if (count != members.size())
        rejections["member accesses its clock"] += count - members.size();
      if (members.empty())
        continue;
    }
    DenseSet<StringAttr> memberNames;
    DenseSet<Operation *> memberFunctions;
    for (Member &member : members) {
      memberNames.insert(member.function.getSymNameAttr());
      memberFunctions.insert(member.function);
    }

    SmallVector<uint64_t> clockRoots{classRoot};
    if (auto found = classMembers.find(classRoot); found != classMembers.end())
      llvm::append_range(clockRoots, found->second);
    // Roots a net clock copy chain updates after its origin. They follow
    // their source; their own writers are the exclusive copies.
    DenseSet<uint64_t> netCopies;
    for (Member &member : members) {
      clockRoots.push_back(member.clock);
      llvm::append_range(clockRoots, member.clockChain);
      for (uint64_t root : member.clockChain)
        if (root != member.origin)
          netCopies.insert(root);
    }
    llvm::sort(clockRoots);
    clockRoots.erase(std::unique(clockRoots.begin(), clockRoots.end()),
                     clockRoots.end());
    // The phases in which this kernel's clock edges occur. Every alias in the
    // class changes in exactly the phases of the class root. A net copy can
    // additionally update at its own time-zero evaluation (4.9.1).
    Phases kernelPhases = phases.getChangePhases(classRoot);
    if (!netCopies.empty()) {
      // Net copies are edge-equivalent only while each root changes at most
      // once per phase. A source that also changes at time zero can change a
      // copy twice in the phase of that evaluation (z, then its initial value,
      // then the new one), and a later copy, evaluated after both updates
      // (10.3.2, 4.7), may never see the middle value.
      PhaseSet sourceAtZero = 0;
      for (uint64_t root : clockRoots)
        if (!netCopies.contains(root))
          sourceAtZero |= phases.getChangePhases(root).zero;
      if (sourceAtZero) {
        reject("net clock source changes at time zero");
        continue;
      }
      for (uint64_t root : clockRoots)
        kernelPhases = kernelPhases | phases.getChangePhases(root);
    }
    auto meetsKernel = [&](StringAttr function, uint32_t blockIndex) {
      return phases.getPhases(function, blockIndex).meets(kernelPhases);
    };

    // The kernel must run at most once between NBA regions. A second run in
    // the same phase would read its own direct stores, where its NBAs would
    // still be pending (4.9.4).
    //
    // Immediate kernels run as soon as an edge wakes them (4.7). A process
    // waiting on an event is scheduled once when it occurs (4.5, 9.4.2), so
    // each phase may see clock changes from at most one atomic source:
    //   - the NBA region before it. Its updates, delayed ones included, are
    //     all processed before the evaluations they cause (4.5, 10.4.2);
    //   - one activation of a periodic process, which suspends only on
    //     positive constant delays and so runs at most once per slot
    //     (9.4.1). A resumable named-block exit (9.6.2) or a process-control
    //     operation (9.7) is a suspension too, so `disable` and handles from
    //     other processes cannot restart such a process within a slot.
    // Copies follow their source (23.3.3). A net copy's time-zero evaluation
    // (4.9.1) moves it once from z to its source's value, and later updates
    // track single source changes, so a phase still sees at most one edge of
    // each direction on every root of the class.
    bool singleClockSource = [&] {
      DenseMap<std::pair<unsigned, unsigned>, StringAttr> sources;
      for (uint64_t root : clockRoots) {
        if (phases.getCopies().count(root) || netCopies.contains(root))
          continue;
        for (const Writer &write : writers.lookup(root)) {
          if (write.function == rootInitializer.getSymNameAttr())
            continue;
          if (write.kind == schedule::ComputeEffectKind::NBA)
            continue;
          if (!isPeriodic(write.function))
            return false;
          Phases at = phases.getPhases(write.function, write.block);
          for (auto [slot, set] : {std::pair{0u, at.zero}, {1u, at.later}})
            for (unsigned bit = 0; bit != 32; ++bit)
              if (set & (PhaseSet{1} << bit)) {
                auto [found, inserted] =
                    sources.try_emplace({slot, bit}, write.function);
                if (!inserted && found->second != write.function)
                  return false;
              }
        }
      }
      return llvm::none_of(unknownWriters, [&](const Writer &write) {
        return write.kind != schedule::ComputeEffectKind::NBA &&
               meetsKernel(write.function, write.block);
      });
    }();

    // A deferred kernel resumes in the Inactive region, after every Active
    // event of its pass (4.4.2.3, 4.5). The original could run its members
    // last in that pass (4.7). Then only what its own updates wake, other
    // kernels, and other Inactive events of its phase with what they wake can
    // run after it. The window analysis covers the whole design, so it runs
    // only when deferral could store more roots (see below).
    PostKernelAnalysis::Config windowConfig{
        design.getOperation(), symbols,         phases, kernelPhases,
        rootInitializer,       memberFunctions, {},     {}};
    DataFlowSolver window(DataFlowConfig().setInterprocedural(false));
    auto isAfter = [&](StringAttr name, uint32_t index) {
      auto function = symbols.lookup<sim::SimFuncOp>(name);
      Block *block = function ? blockAt(function, index) : nullptr;
      if (!block)
        return true;
      const auto *state =
          window.lookupState<AfterLattice>(window.getProgramPointBefore(block));
      return state && state->after;
    };
    auto changedIn = [&](DataFlowSolver &solver, uint64_t root) {
      for (uint64_t key : {root, AfterRoot::anyRoot}) {
        const auto *state =
            solver.lookupState<AfterState>(solver.getLatticeAnchor<AfterRoot>(
                std::make_pair(key, design.getOperation())));
        if (state && state->after)
          return true;
      }
      return false;
    };
    auto changedAfter = [&](uint64_t root) { return changedIn(window, root); };
    // Returns why this kernel cannot be deferred, or an empty string.
    auto analyzeDeferral = [&]() -> StringRef {
      // An Inactive event of the phase (a zero or variable delay) and what it
      // wakes run in a later iteration than the original members, so they
      // may run before the deferred kernel only if it reads nothing they
      // write. The window below places them after it as well, which covers
      // their accesses, writers and finishes.
      bool inactiveShared = false;
      design.walk([&](sim::SimSuspendDelayOp delay) {
        auto function = delay->getParentOfType<sim::SimFuncOp>();
        if (!function || memberFunctions.contains(function))
          return;
        auto constant =
            delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
        if (!constant || constant.getValue() == 0)
          inactiveShared |=
              phases.getPhases(delay.getContinuation()).meets(kernelPhases);
      });
      if (inactiveShared) {
        if (llvm::any_of(members, [](const Member &member) {
              return member.unresolvedRead;
            }))
          return "inactive event writes kernel input";
        // Without kernel updates, the window holds exactly what Inactive
        // events start or wake in the phase, plus what it cannot model.
        PostKernelAnalysis::Config inactiveConfig{
            design.getOperation(), symbols,         phases, kernelPhases,
            rootInitializer,       memberFunctions, {},     {}};
        DataFlowSolver inactive(DataFlowConfig().setInterprocedural(false));
        inactive.load<dataflow::DeadCodeAnalysis>();
        inactive.load<dataflow::SparseConstantPropagation>();
        inactive.load<PostKernelAnalysis>(inactiveConfig);
        if (failed(inactive.initializeAndRun(design)))
          return "post-kernel analysis";
        for (const Member &member : members)
          if (llvm::any_of(member.reads, [&](uint64_t root) {
                return changedIn(inactive, root);
              }))
            return "inactive event writes kernel input";
      }
      for (Operation *wait : memberWaits)
        if (!memberFunctions.contains(wait->getParentOfType<sim::SimFuncOp>()))
          windowConfig.otherKernelWaits.insert(wait);
      for (Member &member : members) {
        llvm::append_range(windowConfig.changed, member.writes);
        for (uint64_t root : member.nbaWrites)
          if (glitchInsensitive.contains(root))
            windowConfig.changed.push_back(root);
        if (member.unresolvedUpdate)
          windowConfig.changed.push_back(AfterRoot::anyRoot);
      }
      window.load<dataflow::DeadCodeAnalysis>();
      window.load<dataflow::SparseConstantPropagation>();
      window.load<PostKernelAnalysis>(windowConfig);
      if (failed(window.initializeAndRun(design)))
        return "post-kernel analysis";
      // A clock change after a deferred kernel would run it again.
      if (llvm::any_of(clockRoots, changedAfter))
        return "clock changes after kernel";
      return {};
    };

    // The roots one kernel mode may store directly. `mayFollow` holds for
    // every activation that may run between the kernel and its NBA region.
    enum class Mode { Immediate, Deferred };
    auto planRoots = [&](Mode mode, llvm::StringMap<uint64_t> &reasons)
        -> std::optional<DenseSet<uint64_t>> {
      auto refuse = [&](StringRef reason) { ++reasons[reason]; };
      bool deferred = mode == Mode::Deferred;
      auto mayFollow = [&](StringAttr function, uint32_t block) {
        return deferred ? isAfter(function, block)
                        : meetsKernel(function, block);
      };
      // (d) Accesses by activations that may follow the kernel.
      SmallVector<uint64_t> seeds(clockRoots);
      for (Attribute attribute : nodes) {
        auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute);
        if (!fragment)
          continue;
        StringAttr name = fragment.getFunction().getAttr();
        if (memberNames.contains(name) ||
            name == rootInitializer.getSymNameAttr())
          continue;
        if (auto function = symbols.lookup<sim::SimFuncOp>(name))
          if (function.getEntryKind() == sim::EntryKind::Final)
            continue;
        if (!mayFollow(name, fragment.getBlock()) || isTransparent(name))
          continue;
        for (auto effect :
             fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
          if (!isAccess(effect.getEffect()) ||
              effect.getTarget() == schedule::ComputeTargetKind::Local)
            continue;
          std::optional<SmallVector<uint64_t>> roots =
              effectRoots(name, effect);
          if (!roots) {
            refuse("unknown event access");
            return std::nullopt;
          }
          llvm::append_range(seeds, *roots);
        }
      }
      // An activation that may follow the kernel and end the simulation lets
      // final procedures read a direct store before its NBA would apply.
      // Members cannot end it: body fusion admits no finish, fatal or stop.
      if (llvm::any_of(endingFragments, [&](const auto &fragment) {
            return !memberNames.contains(fragment.first) &&
                   fragment.first != rootInitializer.getSymNameAttr() &&
                   mayFollow(fragment.first, fragment.second);
          })) {
        if (unknownFinalRead) {
          refuse("unknown final procedure access");
          return std::nullopt;
        }
        llvm::append_range(seeds, finalReads);
      }
      // A root is exposed when anything in its combinational cone is accessed
      // by such an activation, or feeds this kernel's clock.
      DataFlowSolver exposure;
      exposure.load<ConeAnalysis>(design.getOperation(), combinationalProcesses,
                                  seeds, ConeAnalysis::Direction::Sources);
      if (failed(exposure.initializeAndRun(design))) {
        refuse("exposure analysis");
        return std::nullopt;
      }
      auto isExposed = [&](uint64_t root) {
        return ConeAnalysis::contains(exposure, design.getOperation(), root);
      };
      // (b) Another NBA of the root that lands in the same NBA region would
      // now be applied after the store instead of before it (4.6(b)); a
      // delayed one may land in any region. A blocking write that may follow
      // the kernel would be overwritten by the NBA but not by the store.
      auto conflicts = [&](const Writer &write) {
        if (write.kind == schedule::ComputeEffectKind::NBA)
          return write.deferred || meetsKernel(write.function, write.block);
        return mayFollow(write.function, write.block);
      };
      DenseSet<uint64_t> eligible;
      for (Member &member : members)
        for (uint64_t root : member.nbaWrites) {
          if (eligible.contains(root))
            continue;
          if (root >= ActivationMemoryAnalysis::netRootTag) {
            refuse("root net");
            continue;
          }
          // Nothing reads the root: unread-NBA elimination removes the update
          // entirely (4.3), which is better than a store.
          if (!unknownRead && !readRoots.contains(root)) {
            refuse("root unread");
            continue;
          }
          if (!glitchInsensitive.contains(root)) {
            refuse("root observed");
            continue;
          }
          if (isExposed(root)) {
            refuse("root accessed by event activation");
            continue;
          }
          bool writersOk = true;
          for (const Writer &write : writers.lookup(root)) {
            if (write.function == rootInitializer.getSymNameAttr())
              continue;
            if (memberNames.contains(write.function))
              writersOk &= write.kind == schedule::ComputeEffectKind::NBA;
            else
              writersOk &= !conflicts(write);
          }
          writersOk &= llvm::none_of(unknownWriters, conflicts);
          if (!writersOk) {
            refuse("root writers");
            continue;
          }
          eligible.insert(root);
        }

      return eligible;
    };

    // Prefer the immediate kernel, which needs no extra Inactive pass, unless
    // deferral stores more roots directly. Deferral only changes which
    // activations may follow the kernel, so it can add only roots the
    // immediate kernel refuses for them ((b), (d)), or rescue a clock that
    // could run an immediate kernel twice per phase.
    llvm::StringMap<uint64_t> immediateReasons, deferredReasons;
    std::optional<DenseSet<uint64_t>> immediate, deferred;
    if (singleClockSource)
      immediate = planRoots(Mode::Immediate, immediateReasons);
    if (defer && (!immediate ||
                  immediateReasons.count("root accessed by event activation") ||
                  immediateReasons.count("root writers"))) {
      if (StringRef blocker = analyzeDeferral(); !blocker.empty())
        reject(("deferral: " + blocker).str());
      else
        deferred = planRoots(Mode::Deferred, deferredReasons);
    }
    bool useDeferred =
        deferred && (!immediate || deferred->size() > immediate->size());
    if (!immediate && !deferred) {
      if (!singleClockSource)
        reject("clock source");
      for (llvm::StringMap<uint64_t> *reasons :
           {&immediateReasons, &deferredReasons})
        for (auto &[reason, total] : *reasons)
          rejections[reason] += total;
      continue;
    }
    for (auto &[reason, total] :
         useDeferred ? deferredReasons : immediateReasons)
      rejections[reason] += total;
    DenseSet<uint64_t> eligibleRoots =
        std::move(useDeferred ? *deferred : *immediate);

    // Kernel order: readers of an eligible root before its NBA writers;
    // otherwise keep the existing schedule order, including between writers
    // and readers of immediate stores.
    size_t count = members.size();
    llvm::sort(members, [](const Member &lhs, const Member &rhs) {
      return lhs.order < rhs.order;
    });
    SmallVector<SmallVector<unsigned>> successors(count);
    SmallVector<unsigned> indegree(count, 0);
    {
      DenseMap<uint64_t, SmallVector<unsigned>> readersOf, nbaWritersOf,
          writersOf;
      for (auto [index, member] : llvm::enumerate(members)) {
        for (uint64_t root : member.reads)
          readersOf[root].push_back(index);
        for (uint64_t root : member.nbaWrites)
          if (eligibleRoots.contains(root))
            nbaWritersOf[root].push_back(index);
        for (uint64_t root : member.writes)
          writersOf[root].push_back(index);
      }
      llvm::DenseSet<std::pair<unsigned, unsigned>> edges;
      auto addEdge = [&](unsigned from, unsigned to) {
        if (from != to && edges.insert({from, to}).second) {
          successors[from].push_back(to);
          ++indegree[to];
        }
      };
      for (auto &[root, nbaWriters] : nbaWritersOf)
        for (unsigned reader : readersOf.lookup(root))
          for (unsigned writer : nbaWriters)
            addEdge(reader, writer);
      for (auto &[root, immediate] : writersOf) {
        SmallVector<unsigned> touching(immediate.begin(), immediate.end());
        llvm::append_range(touching, readersOf.lookup(root));
        llvm::sort(touching);
        touching.erase(std::unique(touching.begin(), touching.end()),
                       touching.end());
        for (unsigned writer : immediate)
          for (unsigned other : touching)
            if (writer < other)
              addEdge(writer, other);
            else if (other < writer)
              addEdge(other, writer);
      }
    }
    SmallVector<unsigned> sequence;
    {
      std::priority_queue<unsigned, std::vector<unsigned>,
                          std::greater<unsigned>>
          ready;
      SmallVector<bool> done(count, false);
      for (unsigned index = 0; index != count; ++index)
        if (indegree[index] == 0)
          ready.push(index);
      while (sequence.size() != count) {
        if (ready.empty()) {
          // A cycle: take the earliest remaining member. Its late reads are
          // resolved by snapshots or retained NBAs below.
          for (unsigned index = 0; index != count; ++index)
            if (!done[index]) {
              ready.push(index);
              indegree[index] = 0;
              break;
            }
        }
        unsigned next = ready.top();
        ready.pop();
        if (done[next])
          continue;
        done[next] = true;
        sequence.push_back(next);
        for (unsigned successor : successors[next])
          if (!done[successor] && --indegree[successor] == 0)
            ready.push(successor);
      }
    }

    // Choose the clock root most members already wait on.
    DenseMap<uint64_t, unsigned> clockVotes;
    for (Member &member : members)
      ++clockVotes[member.clock];
    unsigned waitMember = 0;
    for (auto [index, member] : llvm::enumerate(members))
      if (clockVotes[member.clock] > clockVotes[members[waitMember].clock] ||
          (clockVotes[member.clock] == clockVotes[members[waitMember].clock] &&
           member.clock < members[waitMember].clock))
        waitMember = index;
    // The kernel exists to store NBAs directly; without an eligible root the
    // existing per-instance process fusion is left in place.
    if (eligibleRoots.empty())
      continue;

    // The kernel's captures: the union of the members' spawn operands.
    SmallVector<Value> operands;
    SmallVector<Type> inputTypes;
    SmallVector<DictionaryAttr> argumentAttrs;
    DenseMap<Value, unsigned> operandIndices;
    sim::SimSpawnOp insertionSpawn = members.front().spawn;
    bool compatible = true;
    for (Member &member : members) {
      if (insertionSpawn->isBeforeInBlock(member.spawn))
        insertionSpawn = member.spawn;
      Block &entry = member.function.getBody().front();
      member.fusedArguments.clear();
      for (auto [argument, operand] :
           llvm::zip_equal(entry.getArguments(), member.spawn.getOperands())) {
        DictionaryAttr attrs =
            member.function.getArgAttrDict(argument.getArgNumber());
        auto [found, inserted] =
            operandIndices.try_emplace(operand, operands.size());
        if (inserted) {
          operands.push_back(operand);
          inputTypes.push_back(argument.getType());
          argumentAttrs.push_back(attrs);
        } else if (inputTypes[found->second] != argument.getType() ||
                   argumentAttrs[found->second] != attrs) {
          compatible = false;
        }
        member.fusedArguments.push_back(found->second);
      }
    }
    if (!compatible) {
      reject("incompatible captures");
      continue;
    }
    plans.push_back({key, &members, std::move(eligibleRoots), useDeferred,
                     std::move(sequence), waitMember, std::move(operands),
                     std::move(inputTypes), std::move(argumentAttrs),
                     insertionSpawn});
  }

  // 4. Build each planned kernel.
  bool changed = false;
  for (Plan &plan : plans) {
    std::vector<Member> &members = *plan.members;
    size_t count = members.size();
    ArrayRef<unsigned> sequence = plan.sequence;
    unsigned waitMember = plan.waitMember;
    uint64_t classRoot = plan.key.first;
    Member &first = members[sequence.front()];
    Location location = first.function.getLoc();
    OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
    SmallVector<NamedAttribute> attributes;
    if (IntegerAttr codeUnit = first.function.getCodeUnitIdAttr())
      attributes.emplace_back(first.function.getCodeUnitIdAttrName(), codeUnit);
    unsigned symbolCounter = 0;
    SmallString<48> base;
    ("__obelisk_clocked_" + Twine(classRoot) + "_" + Twine(plan.key.second))
        .toVector(base);
    SmallString<48> name = SymbolTable::generateSymbolName<48>(
        base,
        [&](StringRef candidate) {
          return symbols.lookup(candidate) != nullptr;
        },
        symbolCounter);
    sim::SimFuncOp kernel = sim::SimFuncOp::create(
        builder, location, name,
        FunctionType::get(design.getContext(), plan.inputTypes, TypeRange{}),
        first.function.getEntryKind(), attributes, plan.argumentAttrs);
    symbols.insert(kernel);
    SymbolTable::setSymbolVisibility(kernel, SymbolTable::Visibility::Private);

    Block &entry = kernel.getBody().front();
    Block *wait = new Block;
    kernel.getBody().push_back(wait);
    std::vector<IRMapping> mappings(count);
    for (auto [index, member] : llvm::enumerate(members))
      for (auto [argument, fused] :
           llvm::zip_equal(member.function.getBody().front().getArguments(),
                           member.fusedArguments))
        mappings[index].map(argument, entry.getArgument(fused));
    for (unsigned index : sequence) {
      Member &member = members[index];
      for (Block *source : member.bodyBlocks) {
        Block *cloned = new Block;
        kernel.getBody().push_back(cloned);
        mappings[index].map(source, cloned);
        for (BlockArgument argument : source->getArguments())
          mappings[index].map(argument, cloned->addArgument(argument.getType(),
                                                            argument.getLoc()));
      }
    }
    for (auto [position, index] : llvm::enumerate(sequence)) {
      Block *next = position + 1 == count
                        ? wait
                        : mappings[sequence[position + 1]].lookup(
                              members[sequence[position + 1]].body);
      mappings[index].map(members[index].wait, next);
    }
    // The wait resumes into a block of its own, so state captured at the
    // start of an activation is captured exactly once per activation even if
    // the first member begins with a loop.
    Block *activation = new Block;
    kernel.getBody().getBlocks().insert(std::next(Region::iterator(wait)),
                                        activation);
    {
      OpBuilder start = OpBuilder::atBlockEnd(activation);
      cf::BranchOp::create(start, location,
                           mappings[sequence.front()].lookup(first.body));
    }

    builder.setInsertionPointToStart(&entry);
    for (unsigned index : sequence)
      for (Operation *operation : members[index].preamble)
        builder.clone(*operation, mappings[index]);
    cf::BranchOp::create(builder, location, wait);
    builder.setInsertionPointToStart(wait);
    for (unsigned index : sequence)
      for (Operation &operation : members[index].wait->without_terminator())
        builder.clone(operation, mappings[index]);
    Operation *suspend = builder.clone(
        *members[waitMember].wait->getTerminator(), mappings[waitMember]);
    suspend->setSuccessor(activation, 0);
    // A deferred kernel resumes in the Inactive region of its iteration, as
    // `@(edge) #0` would (4.4.2.3): after every Active event of its pass.
    if (plan.deferred)
      suspend->setAttr("resume_region",
                       sim::EventRegionAttr::get(design.getContext(),
                                                 sim::EventRegion::Inactive));
    for (unsigned index : sequence) {
      Member &member = members[index];
      for (Block *source : member.bodyBlocks) {
        builder.setInsertionPointToEnd(mappings[index].lookup(source));
        for (Operation &operation : *source)
          builder.clone(operation, mappings[index]);
      }
    }
    removeTerminationPolls(kernel);

    builder.setInsertionPoint(plan.insertionSpawn);
    sim::SimSpawnOp::create(builder, location, kernel.getSymNameAttr(),
                            plan.operands, ArrayAttr{}, ArrayAttr{});
    for (Member &member : members) {
      member.spawn.erase();
      symbols.erase(member.function);
    }
    changed = true;
    ++kernels;
    fusedMembers += count;

    // 5. Late reads. A read executed after an NBA site of its root must see
    // the incoming value (4.9.4); find them with a forward may-dataflow over
    // the activation CFG.
    SmallVector<uint64_t> rootList(plan.eligibleRoots.begin(),
                                   plan.eligibleRoots.end());
    llvm::sort(rootList);
    DenseMap<uint64_t, unsigned> rootIndex;
    for (auto [index, root] : llvm::enumerate(rootList))
      rootIndex[root] = index;
    analysis::HandleFacts facts = provenance.derive(kernel);
    llvm::BitVector unsupported(rootList.size());
    enum class EventKind { Enqueue, Read, OpaqueRead };
    struct Event {
      EventKind kind;
      unsigned root;
      Operation *operation;
    };
    SmallVector<Block *> blocks;
    {
      SmallVector<Block *> pending{activation};
      DenseSet<Block *> seen;
      while (!pending.empty()) {
        Block *block = pending.pop_back_val();
        if (block == wait || !seen.insert(block).second)
          continue;
        blocks.push_back(block);
        for (Block *successor : block->getSuccessors())
          pending.push_back(successor);
      }
    }
    DenseMap<Block *, SmallVector<Event>> events;
    auto eligibleIndex = [&](Value value) -> std::optional<unsigned> {
      std::optional<uint64_t> root = storageRoot(value, facts);
      if (!root)
        return std::nullopt;
      auto found = rootIndex.find(*root);
      if (found == rootIndex.end())
        return std::nullopt;
      return found->second;
    };
    auto markAllUnsupported = [&] { unsupported.set(); };
    for (Block *block : blocks) {
      SmallVector<Event> &list = events[block];
      block->walk([&](Operation *operation) {
        bool nested = operation->getBlock() != block;
        if (auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
          std::optional<unsigned> index =
              eligibleIndex(enqueue.getDestination());
          if (!index)
            return;
          auto reference =
              dyn_cast<sim::RefType>(enqueue.getDestination().getType());
          if (nested || enqueue.getDelay() || enqueue.getClockingOutputAttr() ||
              !reference ||
              reference.getElementType() != enqueue.getValue().getType())
            unsupported.set(*index);
          else
            list.push_back({EventKind::Enqueue, *index, operation});
          if (eligibleIndex(enqueue.getValue()))
            unsupported.set(*eligibleIndex(enqueue.getValue()));
          return;
        }
        if (auto load = dyn_cast<sim::SimRefLoadOp>(operation)) {
          if (std::optional<unsigned> index =
                  eligibleIndex(load.getReference()))
            list.push_back({EventKind::Read, *index, operation});
          return;
        }
        if (auto copy = dyn_cast<sim::SimRefCopyOp>(operation)) {
          if (std::optional<unsigned> index = eligibleIndex(copy.getSource()))
            list.push_back({EventKind::Read, *index, operation});
          if (std::optional<unsigned> index =
                  eligibleIndex(copy.getDestination()))
            unsupported.set(*index);
          return;
        }
        if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
          for (Value operand : call->getOperands())
            if (std::optional<unsigned> index = eligibleIndex(operand))
              unsupported.set(*index);
          auto callee =
              symbols.lookup<sim::SimFuncOp>(call.getCalleeAttr().getValue());
          ArrayAttr summary = callee ? callee.getEffectSummaryAttr() : nullptr;
          if (!summary) {
            markAllUnsupported();
            return;
          }
          for (auto effect :
               summary.getAsRange<schedule::ComputeEffectAttr>()) {
            if (effect.getTarget() == schedule::ComputeTargetKind::Formal ||
                effect.getTarget() == schedule::ComputeTargetKind::Local)
              continue;
            std::optional<uint64_t> root = effectRoot(effect);
            if (!root) {
              if (isAccess(effect.getEffect()))
                markAllUnsupported();
              continue;
            }
            auto found = rootIndex.find(*root);
            if (found == rootIndex.end())
              continue;
            if (isWrite(effect.getEffect()))
              unsupported.set(found->second);
            else if (effect.getEffect() == schedule::ComputeEffectKind::Read)
              list.push_back({EventKind::OpaqueRead, found->second, operation});
          }
          return;
        }
        if (isa<ViewLikeOpInterface>(operation))
          return;
        // Any other use of an eligible root's handle, including immediate
        // stores, is outside the contract.
        for (Value operand : operation->getOperands())
          if (std::optional<unsigned> index = eligibleIndex(operand))
            unsupported.set(*index);
      });
    }
    DenseMap<Operation *, unsigned> enqueues;
    for (Block *block : blocks)
      for (const Event &event : events[block])
        if (event.kind == EventKind::Enqueue)
          enqueues[event.operation] = event.root;
    SmallVector<SmallVector<Operation *>> lateReads(rootList.size());
    llvm::BitVector opaqueLate(rootList.size());
    {
      DataFlowSolver pending;
      pending.load<dataflow::DeadCodeAnalysis>();
      pending.load<dataflow::SparseConstantPropagation>();
      pending.load<PendingNBAAnalysis>(enqueues, rootList.size());
      if (failed(pending.initializeAndRun(kernel)))
        unsupported.set();
      for (Block *block : blocks)
        for (const Event &event : events[block]) {
          if (event.kind == EventKind::Enqueue)
            continue;
          const auto *state = pending.lookupState<PendingLattice>(
              pending.getProgramPointBefore(event.operation));
          // Without a state the read is unreachable; keep the NBA anyway.
          if (!state) {
            unsupported.set(event.root);
            continue;
          }
          if (event.root >= state->roots.size() ||
              !state->roots.test(event.root))
            continue;
          if (event.kind == EventKind::OpaqueRead)
            opaqueLate.set(event.root);
          else
            lateReads[event.root].push_back(event.operation);
        }
    }

    // 6. Roots read late. Each NBA site of such a root may keep its value and
    // the operands of its target (4.9.4) in activation-local slots and set a
    // flag instead; the kernel then applies the flagged updates after its
    // last member, in execution order (4.6(b)), as Verilator's V3Delayed
    // does. Every read before that sees the incoming value, at a cost that
    // grows with the updates instead of the root. A site that may run twice
    // in one activation would need a queue. Late reads that are views of the
    // whole root may instead read a snapshot of its incoming value.
    // The commit runs where the activation ends; every path there passes
    // through the entry block and then the block the wait resumes into.
    auto dominatesCommit = [&](Value value) {
      Block *owner = isa<BlockArgument>(value)
                         ? cast<BlockArgument>(value).getOwner()
                         : value.getDefiningOp()->getBlock();
      return owner == &entry || owner == activation;
    };
    // Values the commit can use directly or recreate; the others are
    // captured at the site.
    auto rematerializable = [&](Value value) {
      if (dominatesCommit(value))
        return true;
      Operation *definition = value.getDefiningOp();
      return definition &&
             (definition->hasTrait<OpTrait::ConstantLike>() ||
              isa<sim::SimContextStorageOp>(definition)) &&
             llvm::all_of(definition->getOperands(), dominatesCommit);
    };
    struct DelayedSite {
      sim::SimNBAEnqueueOp enqueue;
      unsigned root;
      /// Views from the target back toward its root handle.
      SmallVector<Operation *> chain;
      /// How many sites may run before this one; orders the commit.
      unsigned position = 0;
    };
    SmallVector<DelayedSite> delays;
    llvm::BitVector delayed(rootList.size());
    SmallVector<uint64_t> delayedCost(rootList.size());
    {
      for (unsigned index = 0; index != rootList.size(); ++index)
        if (!unsupported.test(index) &&
            (!lateReads[index].empty() || opaqueLate.test(index)))
          delayed.set(index);
      SmallVector<DelayedSite> sites;
      DenseMap<Operation *, unsigned> siteIndices;
      for (Block *block : blocks)
        for (const Event &event : events[block])
          if (event.kind == EventKind::Enqueue && delayed.test(event.root)) {
            siteIndices[event.operation] = sites.size();
            sites.push_back(
                {cast<sim::SimNBAEnqueueOp>(event.operation), event.root});
          }
      DataFlowSolver order;
      order.load<dataflow::DeadCodeAnalysis>();
      order.load<dataflow::SparseConstantPropagation>();
      order.load<PendingNBAAnalysis>(siteIndices, sites.size());
      if (!sites.empty() && failed(order.initializeAndRun(kernel)))
        delayed.reset();
      auto capturable = [&](Value value) {
        return rematerializable(value) || sim::hasKnownZero(value.getType());
      };
      for (auto [index, site] : llvm::enumerate(sites)) {
        if (!delayed.test(site.root))
          continue;
        const auto *state = order.lookupState<PendingLattice>(
            order.getProgramPointBefore(site.enqueue));
        // A site in its own past may run again before the commit.
        bool once =
            state && !(index < state->roots.size() && state->roots.test(index));
        bool supported = once && capturable(site.enqueue.getValue());
        Value target = site.enqueue.getDestination();
        while (Operation *definition = target.getDefiningOp()) {
          if (!isa<sim::SimRefSubelementOp, sim::SimRefArrayElementOp,
                   sim::SimRefExtractOp, sim::SimRefDynExtractOp>(definition))
            break;
          site.chain.push_back(definition);
          target = cast<ViewLikeOpInterface>(definition).getViewSource();
          for (Value operand : definition->getOperands())
            if (operand != target && !capturable(operand))
              supported = false;
        }
        if (!supported || !rematerializable(target)) {
          delayed.reset(site.root);
          continue;
        }
        site.position = state->roots.count();
        delayedCost[site.root] +=
            delayedUpdateBits +
            sim::getProvenanceSpan(site.enqueue.getValue().getType())
                .value_or(maximumSnapshotBits);
      }
      for (DelayedSite &site : sites)
        if (delayed.test(site.root))
          delays.push_back(std::move(site));
      // A site that may precede another has fewer sites that may precede it.
      llvm::stable_sort(delays,
                        [](const DelayedSite &lhs, const DelayedSite &rhs) {
                          return lhs.position < rhs.position;
                        });
    }
    struct Snapshot {
      Value base;
      SmallVector<std::pair<Operation *, SmallVector<Operation *>>> reads;
    };
    DenseMap<unsigned, Snapshot> snapshots;
    for (unsigned index = 0; index != rootList.size(); ++index) {
      if (unsupported.test(index) || lateReads[index].empty() ||
          opaqueLate.test(index))
        continue;
      Snapshot snapshot;
      uint64_t snapshotBits = 0;
      bool ok = true;
      for (Operation *read : lateReads[index]) {
        Value reference = read->getOperand(0);
        SmallVector<Operation *> chain;
        while (Operation *definition = reference.getDefiningOp()) {
          if (!isa<sim::SimRefSubelementOp, sim::SimRefArrayElementOp,
                   sim::SimRefExtractOp, sim::SimRefDynExtractOp>(definition))
            break;
          chain.push_back(definition);
          reference = cast<ViewLikeOpInterface>(definition).getViewSource();
        }
        bool argument = isa<BlockArgument>(reference) &&
                        cast<BlockArgument>(reference).getOwner() == &entry;
        bool context = static_cast<bool>(
            reference.getDefiningOp<sim::SimContextStorageOp>());
        std::optional<uint64_t> whole = wholeStorageRoot(reference, facts);
        auto type = dyn_cast<sim::RefType>(reference.getType());
        std::optional<uint64_t> span =
            type ? sim::getProvenanceSpan(type.getElementType()) : std::nullopt;
        if ((!argument && !context) || !whole || *whole != rootList[index] ||
            !span || *span > maximumSnapshotBits ||
            (snapshot.base && snapshot.base.getType() != reference.getType())) {
          ok = false;
          break;
        }
        snapshotBits = *span;
        if (!snapshot.base || argument)
          snapshot.base = reference;
        snapshot.reads.push_back({read, std::move(chain)});
      }
      if (ok && delayed.test(index) && delayedCost[index] < snapshotBits)
        ok = false;
      if (ok) {
        snapshots[index] = std::move(snapshot);
        delayed.reset(index);
      }
    }
    for (unsigned index = 0; index != rootList.size(); ++index)
      if (!lateReads[index].empty() || opaqueLate.test(index))
        if (!snapshots.count(index) && !delayed.test(index))
          unsupported.set(index);
    builder.setInsertionPoint(activation->getTerminator());
    for (auto &[index, snapshot] : snapshots) {
      Value base = snapshot.base;
      if (auto context = base.getDefiningOp<sim::SimContextStorageOp>())
        base = builder.clone(*context)->getResult(0);
      auto type = cast<sim::RefType>(base.getType());
      Value old = sim::SimRefLoadOp::create(builder, location,
                                            type.getElementType(), base);
      Value copy = sim::SimRefAllocOp::create(builder, location, type, old);
      for (auto &[read, chain] : snapshot.reads) {
        OpBuilder viewBuilder(read);
        Value current = copy;
        for (Operation *view : llvm::reverse(chain)) {
          IRMapping mapping;
          mapping.map(cast<ViewLikeOpInterface>(view).getViewSource(), current);
          current = viewBuilder.clone(*view, mapping)->getResult(0);
        }
        read->setOperand(0, current);
      }
    }

    // 7. Keep the NBAs of roots whose direct store would make shared
    // combinational logic settle twice (see secondWave).
    {
      SmallVector<uint64_t> later;
      for (Member &member : members)
        for (uint64_t root : member.nbaWrites)
          if (!plan.eligibleRoots.contains(root))
            later.push_back(root);
      DenseSet<uint64_t> direct;
      for (auto [index, root] : llvm::enumerate(rootList))
        if (unsupported.test(index))
          later.push_back(root);
        else
          direct.insert(root);
      for (uint64_t root : secondWave(std::move(later), std::move(direct))) {
        unsupported.set(rootIndex.lookup(root));
        ++rejections["root second wave"];
      }
    }

    // 8. Store at the NBA statements of every remaining root, or delay them.
    for (Block *block : blocks)
      for (const Event &event : events[block]) {
        if (event.kind != EventKind::Enqueue || unsupported.test(event.root) ||
            delayed.test(event.root))
          continue;
        auto enqueue = cast<sim::SimNBAEnqueueOp>(event.operation);
        OpBuilder storeBuilder(enqueue);
        sim::SimRefStoreOp::create(storeBuilder, enqueue.getLoc(),
                                   enqueue.getValue(),
                                   enqueue.getDestination());
        enqueue.erase();
        ++directSites;
      }
    delayed.reset(unsupported);
    if (delayed.any()) {
      // The slots live for one activation and start unset.
      builder.setInsertionPoint(activation->getTerminator());
      Type flagType = builder.getI1Type();
      auto flagReference = sim::RefType::get(builder.getContext(), flagType);
      Value unset = arith::ConstantOp::create(builder, location,
                                              builder.getBoolAttr(false));
      // Every path that ends the activation commits first.
      Block *commit = new Block;
      kernel.getBody().push_back(commit);
      for (Block *block : blocks)
        for (BlockOperand &successor :
             block->getTerminator()->getBlockOperands())
          if (successor.get() == wait)
            successor.set(commit);
      OpBuilder commitBuilder = OpBuilder::atBlockEnd(commit);
      for (DelayedSite &site : delays) {
        if (!delayed.test(site.root))
          continue;
        Location siteLocation = site.enqueue.getLoc();
        Value flag =
            sim::SimRefAllocOp::create(builder, location, flagReference, unset);
        llvm::MapVector<Value, Value> slots;
        auto capture = [&](Value value) {
          if (rematerializable(value) || slots.count(value))
            return;
          Value zero =
              *sim::materializeKnownZero(builder, location, value.getType());
          slots[value] = sim::SimRefAllocOp::create(
              builder, location,
              sim::RefType::get(builder.getContext(), value.getType()), zero);
        };
        capture(site.enqueue.getValue());
        for (Operation *view : site.chain)
          for (Value operand : view->getOperands())
            if (operand != cast<ViewLikeOpInterface>(view).getViewSource())
              capture(operand);
        {
          OpBuilder siteBuilder(site.enqueue);
          for (auto &[value, slot] : slots)
            sim::SimRefStoreOp::create(siteBuilder, siteLocation, value, slot);
          Value set = arith::ConstantOp::create(siteBuilder, siteLocation,
                                                siteBuilder.getBoolAttr(true));
          sim::SimRefStoreOp::create(siteBuilder, siteLocation, set, flag);
        }
        Block *apply = new Block, *next = new Block;
        kernel.getBody().push_back(apply);
        kernel.getBody().push_back(next);
        Value isSet = sim::SimRefLoadOp::create(commitBuilder, siteLocation,
                                                flagType, flag);
        cf::CondBranchOp::create(commitBuilder, siteLocation, isSet, apply,
                                 next);
        commitBuilder.setInsertionPointToEnd(apply);
        IRMapping mapping;
        for (auto &[value, slot] : slots)
          mapping.map(value,
                      sim::SimRefLoadOp::create(commitBuilder, siteLocation,
                                                value.getType(), slot));
        auto recreate = [&](Value value) {
          if (mapping.contains(value) || dominatesCommit(value))
            return;
          mapping.map(
              value, commitBuilder.clone(*value.getDefiningOp())->getResult(0));
        };
        for (Operation *view : llvm::reverse(site.chain)) {
          for (Value operand : view->getOperands())
            recreate(operand);
          commitBuilder.clone(*view, mapping);
        }
        recreate(site.enqueue.getDestination());
        recreate(site.enqueue.getValue());
        sim::SimRefStoreOp::create(
            commitBuilder, siteLocation,
            mapping.lookupOrDefault(site.enqueue.getValue()),
            mapping.lookupOrDefault(site.enqueue.getDestination()));
        cf::BranchOp::create(commitBuilder, siteLocation, next);
        commitBuilder.setInsertionPointToEnd(next);
        site.enqueue.erase();
        ++delayedSites;
      }
      cf::BranchOp::create(commitBuilder, location, wait);
    }
    for (unsigned index = 0; index != rootList.size(); ++index) {
      if (unsupported.test(index))
        ++retainedRoots;
      else {
        ++directRoots;
        if (snapshots.count(index))
          ++snapshotRoots;
        if (delayed.test(index))
          ++delayedRoots;
      }
    }
    if (report)
      llvm::errs() << "obelisk clocked kernel: " << kernel.getSymName()
                   << " members=" << count << " eligible=" << rootList.size()
                   << " retained=" << unsupported.count()
                   << " snapshots=" << snapshots.size()
                   << " deferred=" << plan.deferred
                   << " delayed=" << delayed.count() << '\n';
  }
  if (report)
    for (auto &[reason, total] : rejections)
      llvm::errs() << "obelisk clocked kernel rejection: " << reason << " = "
                   << total << '\n';
  if (changed)
    design.removeComputeGraphAttr();
}

} // namespace
} // namespace obelisk
