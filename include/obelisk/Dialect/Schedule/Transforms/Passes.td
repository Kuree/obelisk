//===- Passes.td - Simulation transformation passes -------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SCHEDULE_TRANSFORMS_PASSES_TD
#define OBELISK_DIALECT_SCHEDULE_TRANSFORMS_PASSES_TD

include "mlir/Pass/PassBase.td"

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
    : Pass<"obelisk-sim-optimize-native-regions",
           "obelisk::sim::SimDesignOp"> {
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
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect", "mlir::cf::ControlFlowDialect"
  ];
}


def AnnotateCompactNativeNBAPass : Pass<"schedule-annotate-compact-nba", "mlir::ModuleOp"> {
  let summary = "Propagate selected native NBA proofs to their consuming operations";
}
def SpecializeNativeCapturesPass : Pass<"schedule-specialize-native-captures", "mlir::ModuleOp"> {
  let summary = "schedule specialize native captures";
  let description = [{
    Runs as a module pass in the native scheduling pipeline. Requires the
    Actors phase of the preserved native frame and schedule analysis; this
    preserves the canonical fallback ABI across native-only rewrites.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "obelisk::schedule::ScheduleDialect",
    "obelisk::runtime::ObeliskRuntimeDialect",
    "mlir::arith::ArithDialect", "mlir::cf::ControlFlowDialect",
    "mlir::func::FuncDialect", "mlir::LLVM::LLVMDialect",
    "mlir::math::MathDialect", "mlir::scf::SCFDialect"
  ];
}
def MarkCleanNativeNBAPass : Pass<"schedule-mark-clean-native-nba", "mlir::ModuleOp"> {
  let summary = "schedule mark clean native nba";
  let description = [{
    Runs as a module pass in the native scheduling pipeline. Requires the
    Roots phase of the preserved native frame and schedule analysis; this
    preserves the canonical fallback ABI across native-only rewrites.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "obelisk::schedule::ScheduleDialect",
    "obelisk::runtime::ObeliskRuntimeDialect",
    "mlir::arith::ArithDialect", "mlir::cf::ControlFlowDialect",
    "mlir::func::FuncDialect", "mlir::LLVM::LLVMDialect",
    "mlir::math::MathDialect", "mlir::scf::SCFDialect"
  ];
}
def SpecializeNativeEvalPass : Pass<"schedule-specialize-native-eval", "mlir::ModuleOp"> {
  let summary = "schedule specialize native eval";
  let description = [{
    Runs as a module pass in the native scheduling pipeline. Requires the
    CleanNBA phase of the preserved native frame and schedule analysis; this
    preserves the canonical fallback ABI across native-only rewrites.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "obelisk::schedule::ScheduleDialect",
    "obelisk::runtime::ObeliskRuntimeDialect",
    "mlir::arith::ArithDialect", "mlir::cf::ControlFlowDialect",
    "mlir::func::FuncDialect", "mlir::LLVM::LLVMDialect",
    "mlir::math::MathDialect", "mlir::scf::SCFDialect"
  ];
}

#endif
