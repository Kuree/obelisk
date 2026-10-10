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
/// This dense forward analysis computes, for every block of every process and
/// task, the phases in which it may execute, and for every storage or net root
/// the phases in which it may change:
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

  struct Shared;

private:
  static void findCopies(Shared &facts);
  bool valid = false;
  std::unique_ptr<Shared> shared;
  std::unique_ptr<mlir::DataFlowSolver> solver;
};

} // namespace obelisk::analysis

#endif
