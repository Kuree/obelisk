//===- ActivationPhaseAnalysis.h - Time-slot phases -------------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_ACTIVATIONPHASEANALYSIS_H
#define OBELISK_ANALYSIS_ACTIVATIONPHASEANALYSIS_H

#include "mlir/Analysis/DataFlowFramework.h"
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/DenseMap.h"
#include <memory>

namespace obelisk::analysis {

/// IEEE 1800-2023 4.5 executes a time slot as Active passes separated by
/// region moves. Number the passes by the NBA regions that precede them:
/// phase 1 runs before the slot's first NBA region, phase p + 1 runs after the
/// p-th. Inactive passes stay in their phase because the Inactive region is
/// moved to Active before the NBA region.
///
/// This forward analysis computes, for every block of every process and task,
/// the phases in which it may execute, and for every storage or net root the
/// phases in which it may change. A block's phases are constant, so the
/// solve is over blocks: each function's blocks are summarized from its own
/// body (in parallel), and the solver joins them across processes:
///   - the root initializer starts each process at time zero, in phase 1;
///     a process started by another activation (fork) and a task called by
///     one start in the phases of that activation;
///   - a positive constant delay resumes in phase 1; a zero, variable or
///     otherwise unknown delay, and any wait without a modeled trigger, may
///     resume in every phase;
///   - an edge, change or level wait resumes in the phases its roots change;
///   - a blocking write changes its root in the writer's phases, an NBA in
///     the following phases (4.9.4), a delayed NBA in every phase;
///   - a task without suspensions returns in its caller's phases;
///   - a proven copy changes its destination in its source's phases.
/// The time-zero slot is tracked separately from every later slot: a process
/// leaves time zero only through a positive delay, so a wait made after one
/// never sees time-zero changes such as continuous assignments' time-zero
/// evaluations (4.9.1). Two activations can interleave in an Active region
/// only if their phases meet in the same kind of slot. Effects come from the
/// verified compute graph, which already folds callee summaries; task formals
/// resolve through task-call sites.
class ActivationPhaseAnalysis {
public:
  /// Bit i is phase i + 1; bit 31 also stands for every later phase, so NBA
  /// feedback widens upward and keeps its minimum.
  using PhaseSet = uint32_t;
  static constexpr PhaseSet firstPhase = 1;
  static constexpr PhaseSet anyPhase = ~PhaseSet{0};
  /// The phases after one more NBA region.
  static PhaseSet afterNBA(PhaseSet phases) {
    if (phases == anyPhase)
      return anyPhase;
    return (phases << 1) | ((phases >> 30) ? PhaseSet{1} << 31 : 0);
  }
  /// Phases in the time-zero slot and in every later slot.
  struct Phases {
    PhaseSet zero = 0, later = 0;
    static Phases any() { return {anyPhase, anyPhase}; }
    bool empty() const { return !zero && !later; }
    bool meets(Phases other) const {
      return (zero & other.zero) || (later & other.later);
    }
    Phases operator|(Phases other) const {
      return {zero | other.zero, later | other.later};
    }
    bool operator==(Phases other) const {
      return zero == other.zero && later == other.later;
    }
    bool operator!=(Phases other) const { return !(*this == other); }
  };
  static Phases afterNBA(Phases phases) {
    return {afterNBA(phases.zero), afterNBA(phases.later)};
  }

  /// A root-started continuous process `loop: %v = load src; store %v to
  /// dst; wait change src` that is the only writer of `dst`, where neither
  /// root is statically initialized. Both start at X, so `dst` has the value
  /// sequence of `src` (23.3.3, 4.9.1): it changes exactly when `src` does,
  /// and the copy's time-zero evaluation is no update event.
  struct Copy {
    uint64_t source;
    mlir::StringAttr process;
  };
  using Copies = llvm::DenseMap<uint64_t, Copy>;

  /// What one block contributes to the solve, from its own function and the
  /// compute graph. A block's phases are constant: they change only across
  /// suspensions, so solves over activations are over blocks.
  struct BlockInfo {
    /// How the terminator carries the block's activation to its successors.
    enum class Transfer : uint8_t {
      /// No suspension: the successor continues the same activation.
      Plain,
      /// A task call: the same activation unless the callee suspends.
      TaskCall,
      /// A positive constant delay, a zero (or negative) constant delay, or a
      /// variable delay.
      Positive,
      Zero,
      Variable,
      /// Never resumes.
      Forever,
      /// Resumes when one of `watched` changes.
      Watch,
      /// Resumes when the event `watched` holds is triggered.
      Event,
      /// Any other suspension, or a wait on an unresolved handle.
      Unknown,
    };
    /// How a write updates its root relative to the writer's phases.
    enum class Update : uint8_t { Same, AfterNBA, Any };

    Transfer transfer = Transfer::Plain;
    mlir::Operation *terminator = nullptr;
    llvm::SmallVector<uint64_t, 1> watched;
    /// Neighbors over executable CFG edges: a branch on constant operands
    /// takes only the selected edge.
    llvm::SmallVector<mlir::Block *, 2> predecessors, successors;
    /// Functions the block's spawns and task calls start, and the task-call
    /// terminator's callee.
    llvm::SmallVector<mlir::StringAttr, 1> spawnNames, taskCallNames;
    llvm::SmallVector<mlir::Operation *, 1> spawns, taskCalls;
    mlir::StringAttr calleeName;
    mlir::Operation *callee = nullptr;
    bool calleeSuspends = false;
    /// The terminator returns; `returns` also needs a task some modeled call
    /// enters.
    bool isReturn = false, returns = false;
    /// Reachable from the function entry over executable edges.
    bool reachable = false;
    /// The block has a compute-graph fragment, whose effects follow.
    bool hasEffects = false;
    /// Roots changed by blocking writes, drives and NBAs, with how each
    /// update relates to the writer's phases; `anyRoot` when unresolved.
    llvm::SmallVector<std::pair<uint64_t, Update>, 2> writes;
    /// Roots changed by blocking writes and drives or triggered, which wake
    /// waiters in the writer's phase; `anyRoot` when unresolved.
    llvm::SmallVector<uint64_t, 2> wakes;
    /// (destination, source) of the function's proven copies.
    llvm::SmallVector<std::pair<uint64_t, uint64_t>, 1> copies;
  };
  /// Root key standing for every root an unresolved write may update.
  static constexpr uint64_t anyRoot = ~uint64_t{0};

  /// `design` must carry its verified compute graph.
  ActivationPhaseAnalysis(sim::SimDesignOp design,
                          const HandleDataflowAnalysis &handles);
  ~ActivationPhaseAnalysis();
  ActivationPhaseAnalysis(ActivationPhaseAnalysis &&);

  bool isValid() const { return valid; }
  /// Phases in which `block` may execute; empty if it never runs in a slot.
  Phases getPhases(mlir::Block *block) const;
  Phases getPhases(mlir::StringAttr function, uint32_t blockIndex) const;
  /// Phases in which a root (ActivationMemoryAnalysis root encoding) may
  /// change, including writes whose target is unresolved.
  Phases getChangePhases(uint64_t root) const;
  /// Every root an effect of `function`'s graph fragments may denote. Task
  /// formals resolve through every call site; nullopt when unresolved.
  std::optional<llvm::SmallVector<uint64_t>>
  getEffectRoots(mlir::StringAttr function,
                 schedule::ComputeEffectAttr effect) const;
  /// Proven copies, keyed by destination root.
  const Copies &getCopies() const;
  /// The root a handle in `function` denotes, if it is known.
  std::optional<uint64_t> getRoot(sim::SimFuncOp function,
                                  mlir::Value handle) const;
  /// Whether every start of `function` is a spawn or task call this analysis
  /// models; other entries may occur in any phase.
  bool isModeledStart(mlir::StringAttr function) const;
  /// Whether a task may suspend before it returns.
  bool taskSuspends(mlir::StringAttr task) const;
  /// The compute-graph effects of one block, or null without a fragment.
  mlir::ArrayAttr getFragmentEffects(mlir::Block *block) const;
  /// The summary of a block of a defined function, or null.
  const BlockInfo *getBlockInfo(mlir::Block *block) const;
  /// Every summarized block.
  const llvm::DenseMap<mlir::Block *, BlockInfo> &getBlockInfos() const;

  struct Shared;

private:
  static void findCopies(Shared &facts);
  bool valid = false;
  std::unique_ptr<Shared> shared;
  std::unique_ptr<mlir::DataFlowSolver> solver;
};

} // namespace obelisk::analysis

#endif
