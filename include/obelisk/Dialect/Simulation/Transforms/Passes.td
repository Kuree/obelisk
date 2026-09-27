//===- Passes.td - Simulation transformation passes -------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_TD
#define OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_TD

include "mlir/Pass/PassBase.td"

def ObeliskSimUnrollBoundedLoopsPass
    : Pass<"obelisk-sim-unroll-bounded-loops", "obelisk::sim::SimFuncOp"> {
  let summary = "Unroll small constant-trip, non-suspending CFG loops";
  let dependentDialects = ["mlir::arith::ArithDialect",
                           "mlir::cf::ControlFlowDialect"];
}

def ObeliskSimMarkBoundedLoopsPass
    : Pass<"obelisk-sim-mark-bounded-loops", "obelisk::sim::SimFuncOp"> {
  let summary = "Record the termination proof for bounded induction loops";
  let description = [{
    Marks every constant-induction, non-suspending CFG loop whose exit is proven
    in closed form, including loops the unroller refused to replicate because
    their trip count or body size exceeded its budget. The mark carries no
    iteration count and changes no IR; it lets schedule-group classification
    distinguish such a loop from an unbounded control loop, which IEEE
    1800-2023 12.7.6 describes as able to hang the event scheduler.
  }];
  let dependentDialects = ["mlir::arith::ArithDialect",
                           "mlir::cf::ControlFlowDialect"];
}

def ObeliskSimThreadSuspensionPass
    : Pass<"obelisk-sim-thread-suspension", "obelisk::sim::SimFuncOp"> {
  let summary = "Thread promoted SSA state through suspension continuations";
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::cf::ControlFlowDialect"
  ];
}

def ObeliskSimSCCPPass
    : Pass<"obelisk-sim-sccp", "obelisk::sim::SimDesignOp"> {
  let summary = "Propagate constants across simulation code-unit boundaries";
  let description = [{
    Runs MLIR sparse conditional constant propagation independently within
    isolated simulation code units and deterministically exchanges constants
    across direct calls, process spawns, and returns until reaching a fixed
    point.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect"
  ];
  let options = [
    Option<"vpi", "vpi", "std::string", "\"off\"",
           "VPI observability mode: off, read, or full">
  ];
}

def ObeliskSimPropagateInitializedStoragePass
    : Pass<"obelisk-sim-propagate-initialized-storage",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Propagate initialized scalar storage through whole and sliced reads";
  let description = [{
    Proves a unique constant whole-storage initializer executes before process
    startup, including stores inlined into the root initializer. Replaces whole
    and fixed packed-slice reads while preserving four-state X/Z planes and the
    observable initializing store. Later writes, opaque aliases, initializer
    reads, and externally writable state conservatively prevent propagation.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect"
  ];
  let options = [
    Option<"vpi", "vpi", "std::string", "\"off\"",
           "VPI observability mode: off, read, or full">
  ];
}

def ObeliskSimDevirtualizeClassCallsPass
    : Pass<"obelisk-sim-devirtualize-class-calls",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Resolve exact and monomorphic managed-class dispatches";
  let description = [{
    Tracks exact non-null dynamic classes through allocation, checked casts,
    and shallow copies, and computes closed-world monomorphic targets for
    final or non-overridden hierarchies. Virtual function calls with a uniquely
    resolved effective method are rewritten to ordinary simulation calls so
    standard call-graph optimization and inlining can see them. Potentially
    null receivers retain virtual dispatch on a guarded null path. Existing
    direct class calls are normalized to the same call representation.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::cf::ControlFlowDialect"
  ];
  let options = [
    Option<"preserveAllMethods", "preserve-all-methods", "bool", "false",
           "Keep every prepared class method for lowering inspection">
  ];
}

def ObeliskSimEliminateDeadCapturesPass
    : Pass<"obelisk-sim-eliminate-dead-captures",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Remove unused private simulation entry arguments";
  let description = [{
    Solves argument liveness across direct call and spawn boundaries, then
    removes dead private captures and formals from functions, processes, and
    all of their invocation sites. Context arguments and externally observable
    or otherwise opaque ABIs are retained conservatively.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
  let options = [
    Option<"missedRemarks", "missed-remarks", "bool", "false",
           "Emit developer remarks for conservatively pinned ABIs">
  ];
}

def ObeliskSimEliminateDeadBoundariesPass
    : Pass<"obelisk-sim-eliminate-dead-boundaries",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Remove dead private simulation function boundaries and calls";
  let description = [{
    Solves whole-design demand across direct function arguments, results,
    calls, returns, and process spawns. Dead private ABI positions are removed
    in lockstep at every boundary, and calls to discardable zero-time
    functions are erased when none of their results are demanded. Context
    arguments and externally observable or otherwise opaque ABIs are retained
    conservatively.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
  let options = [
    Option<"missedRemarks", "missed-remarks", "bool", "false",
           "Emit developer remarks for conservatively pinned ABIs">
  ];
}

def ObeliskSimInlinePass
    : Pass<"obelisk-sim-inline", "obelisk::sim::SimDesignOp"> {
  let summary = "Propagate control and inline profitable zero-time calls";
  let description = [{
    Uses MLIR's SCC inliner for CFG/SSA transformation while Obelisk owns
    simulation legality, specialization profitability, hierarchy retention,
    growth budgets, and deterministic optimization-level presets. Calls that
    transitively carry process control are mandatory at every optimization
    level; ordinary calls remain subject to the selected profitability preset.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect"
  ];
  let options = [
    Option<"optLevel", "opt-level", "unsigned", "3",
           "Profitability preset from 0 (mandatory control only) through 3">,
    Option<"tinyCost", "tiny-cost", "int64_t", "-1",
           "Override the preset tiny-leaf weighted cost threshold">,
    Option<"specializationCost", "specialization-cost", "int64_t", "-1",
           "Override the specialization-candidate weighted cost threshold">,
    Option<"callerGrowthPercent", "caller-growth-percent", "int64_t", "-1",
           "Override caller growth percentage budget">,
    Option<"callerGrowthConstant", "caller-growth-constant", "int64_t", "-1",
           "Override caller fixed weighted-growth budget">,
    Option<"designGrowthPercent", "design-growth-percent", "int64_t", "-1",
           "Override design growth percentage budget">,
    Option<"designGrowthConstant", "design-growth-constant", "int64_t", "-1",
           "Override design fixed weighted-growth budget">,
    Option<"maxIterations", "max-iterations", "int64_t", "-1",
           "Override the maximum SCC inlining iterations">,
    Option<"missedRemarks", "missed-remarks", "bool", "false",
           "Emit a remark explaining every missed inline opportunity">
  ];
}


#endif
