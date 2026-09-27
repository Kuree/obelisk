//===- Passes.td - Simulation transformation passes -------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_TD
#define OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_TD

include "mlir/Pass/PassBase.td"

def ObeliskSimExtractPeriodicClocksPass
    : Pass<"obelisk-sim-extract-periodic-clocks", "obelisk::sim::SimDesignOp"> {
  let summary = "Extract private toggle clocks from uniform periodic processes";
  let description = [{
    Replaces equal positive constant delays in a singleton, nonterminating
    process with waits on a private periodic clock. All original assignments,
    including NBA publication and initialization, remain in the source process.
    Runs before suspension threading and persistent compute-graph construction.
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

def ObeliskSimMaterializeClockedControlPass
    : Pass<"obelisk-sim-materialize-clocked-control",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Make singleton initial clock-control state explicit";
  let description = [{
    Coalesces same-edge waits in a statically spawned initial process into one
    dispatch boundary. Scalar continuation operands and the resume phase live
    in private design storage shared by native evaluation and coroutine fallback.
    Startup side effects remain in the canonical process entry.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect"
  ];
}

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

def ObeliskSimBuildComputeGraphPass
    : Pass<"obelisk-sim-build-compute-graph", "obelisk::sim::SimDesignOp"> {
  let summary = "Derive descriptor-range summaries and a deterministic schedule";
  let description = [{
    Propagates stable descriptor provenance through SSA and CFG edges, computes
    interprocedural bit-range mod/ref summaries, assigns fixed continuation,
    timing, and NBA sites, and derives the late fragment compute graph. The
    graph is diagnostic compiler metadata, not the primary simulation IR.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect"
  ];
  let options = [
    Option<"workers", "workers", "uint32_t", "1",
           "Number of generated static worker lanes">,
    Option<"vpi", "vpi", "std::string", "\"off\"",
           "VPI observability mode: off, read, or full">
  ];
}

def ObeliskSimVerifyComputeGraphPass
    : Pass<"obelisk-sim-verify-compute-graph", "obelisk::sim::SimDesignOp"> {
  let summary = "Verify fragment ABI, range summaries, and region schedules";
  let description = [{
    Checks the persistent late-analysis metadata independently of the builder:
    dense node IDs, valid descriptor-range effects and edges, complete event
    regions, unique region membership, fixed site targets, and fragment ABI
    records. This pass is also usable on reloaded simulation IR.
  }];
}

def ObeliskSimMaterializeGraphRegionsPass
    : Pass<"obelisk-sim-materialize-graph-regions",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Materialize verified compute regions as coarse native kernels";
  let description = [{
    Coarsens adjacent acyclic groups into deterministic macro tasks while
    retaining their fine compute-fragment IDs as bytecode, VPI, and
    deoptimization boundaries. Convergence and control-loop groups remain
    explicit kernel boundaries. Whole kernels receive greedy worker-lane
    placement so later native lowering can generate persistent lane functions
    without repartitioning semantic fragments. The pass also freezes exact
    physical-trigger groups, exclusive owner/ready-bit identities, Tier 1/2/3
    ownership, mixed-tier root barriers, and inductive two-state promotion
    closures for native lowering.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
  let options = [
    Option<"maxAcyclicCost", "max-acyclic-cost", "uint64_t", "4096",
           "Maximum static cost of one merged acyclic kernel">
  ];
}

def ObeliskSimSpecializeStaticStateNBAPass
    : Pass<"obelisk-sim-specialize-static-state-nba",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Plan guarded direct static state and fixed-root NBA";
  let description = [{
    Consumes the final verified compute graph and records a revision-coupled
    specialization plan for fixed storage roots. Integer and logic roots no
    wider than the configured limit receive direct or guarded state policy
    and actor read/write dependencies. Every eligible fixed-storage NBA root
    receives a canonical commit order independent of the direct-state width
    policy. The pass does not clone or otherwise rewrite executable coroutine
    bodies.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
  let options = [
    Option<"maxPackedWidth", "max-packed-width", "uint32_t", "64",
           "Maximum fixed packed integer or logic root width">,
    Option<"missedRemarks", "missed-remarks", "bool", "false",
           "Emit remarks for roots and operations left generic">
  ];
}

def ObeliskSimPlanStaticSuperstepPass
    : Pass<"obelisk-sim-plan-static-superstep",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Plan a guarded fixed-actor native AOT superstep";
  let description = [{
    Consumes the final verified single-worker compute graph and records a typed,
    revision-coupled superstep candidate with a fixed actor inventory. Native
    lowering combines the candidate with its complete AOT eligibility analysis
    and suppresses it when later ABI or operation checks require a hybrid
    schedule. The runtime activates an accepted plan only while the VPI
    environment remains clean.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
  let options = [
    Option<"missedRemarks", "missed-remarks", "bool", "false",
           "Emit a remark explaining why no static superstep was planned">
  ];
}

def ObeliskSimFuseComputeFragmentsPass
    : Pass<"obelisk-sim-fuse-compute-fragments",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Plan conservative static compute-fragment fusion";
  let description = [{
    Groups native single-worker suspension fragments with identical direct
    sensitivity into deterministic AOT execution batches. The pass consumes
    the verified compute graph, rejects observable or control-sensitive
    boundaries, and records only typed fusion metadata; bytecode continuation
    boundaries and the executable CFG remain unchanged.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
  let options = [
    Option<"bodyFusion", "body-fusion", "bool", "false",
           "Plan candidates for physical process-body fusion instead of AOT dispatch batches">,
    Option<"primitiveOnly", "primitive-only", "bool", "false",
           "Restrict physical body fusion to continuous built-in primitive actors">,
    Option<"maxStraightLineMembers", "max-straight-line-members", "uint32_t", "16",
           "Maximum number of continuous actors in one i64-mask region kernel">
  ];
}

def ObeliskSimMaterializeComputeFusionPass
    : Pass<"obelisk-sim-materialize-compute-fusion",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Fuse proven-adjacent process activations into one body";
  let description = [{
    Consumes verified static body-fusion metadata and combines eligible
    root-created processes with the same direct sensitivity into one simulation
    function. The pass preserves the graph's deterministic activation order,
    rewrites root spawn captures, and invalidates the consumed compute graph so
    it can be rebuilt from the fused executable CFG.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::cf::ControlFlowDialect"
  ];
}

def ObeliskSimOptimizeNativeRegionsPass
    : Pass<"obelisk-sim-optimize-native-regions", "mlir::ModuleOp"> {
  let summary = "Forward generated native-region next state through SSA";
  let description = [{
    Runs after the hybrid bytecode image has been frozen and before native
    lowering. For a bounded acyclic generated activation, forwards each eligible
    immediate root-accumulator assignment through CFG block arguments and
    emits at most one stage for that root in the wait-boundary epilogue. Fixed
    slots, delayed work, dynamic frontiers, mixed part-select roots, and
    convergence SCCs remain explicit.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
}

#endif // OBELISK_DIALECT_SIMULATION_TRANSFORMS_PASSES_TD
