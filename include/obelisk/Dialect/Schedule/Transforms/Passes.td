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

def ObeliskSimFuseClockedKernelsPass
    : Pass<"obelisk-sim-fuse-clocked-kernels", "obelisk::sim::SimDesignOp"> {
  let summary = "Run each clock edge's processes as one activation with direct NBA stores";
  let description = [{
    Fuses the root-spawned edge-triggered processes of one clock alias class
    and edge, across instances, into a single activation. Inside it, an NBA
    whose root is written only there, is observed only by side-effect-free
    combinational processes, and is not accessed by any activation that can
    run between the kernel and the NBA region becomes a store at its
    statement. Where a read may follow an NBA site of its root, the kernel
    instead keeps each site's value and target operands and applies them
    after its last member, or the read uses a snapshot of the incoming value.
    Requires VPI off and a single worker; invalidates the compute graph when
    it changes the design.

    With `defer`, a kernel's wait may instead resume it in the Inactive
    region (`resume_region = inactive`, as `@(edge) #0` would), after every
    Active event of its pass (IEEE 1800-2023 4.4.2.3, 4.5). Then only what
    its own updates wake, other kernels and other Inactive events of its
    phase can observe its stores, so same-edge readers, testbench delays and
    their `$finish` no longer keep NBAs. The driver pipeline enables it
    (`--clocked-kernels=deferred`, the default).
  }];
  let options = [
    Option<"defer", "defer", "bool", /*default=*/"false",
           "Allow kernels that resume in the Inactive region">
  ];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::cf::ControlFlowDialect"
  ];
}

def PlanCombinationalStatementsPass
    : Pass<"schedule-plan-combinational-statements", "obelisk::sim::SimDesignOp"> {
  let summary = "Plan source-ordered DAGs across combinational process boundaries";
  let description = [{
    Uses whole-design call, storage and statement dependence analysis to find
    artificial process SCCs whose individual operations form a DAG. Plans only
    complete, exclusive, finite combinational activations. The materializer
    rechecks the proof and preserves every process's statement order. Actual
    feedback and unsupported effects retain their existing scheduler.

    Each cohort then grows over the acyclic activations connected to it whose
    writes only combinational processes observe and which do not wake on an
    edge-triggered root, so one wake runs that logic once in data order. A
    rejected grown cohort falls back to the cohorts it contains.
  }];
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                           "mlir::arith::ArithDialect", "mlir::cf::ControlFlowDialect"];
  let options = [Option<"maxMembers", "max-members", "unsigned", "0",
                        "Maximum activations in one statement DAG (0: unlimited)">,
                 Option<"explain", "explain", "bool", "false",
                        "Report rejected activation and component proofs">,
                 Option<"growAcyclic", "grow-acyclic", "bool", "true",
                        "Grow each cohort over its connected acyclic activations">];
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
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect", "mlir::ub::UBDialect"
  ];
  let options = [
    Option<"materializeEvalBodies", "materialize-eval-bodies", "bool", "true",
           "Clone native evaluators after the final body-fusion round">
  ];
}

def SpecializeReferenceJoinsPass
    : Pass<"schedule-specialize-reference-joins", "obelisk::sim::SimFuncOp"> {
  let summary = "Specialize small reference joins along their incoming edges";
  let description = [{
    Expose storage identities hidden by CFG block merging. Clone small blocks
    that receive different references, without moving statements or creating
    runtime recovery. The total duplicated work is bounded per function.
  }];
  let options = [Option<"nativeBodiesOnly", "native-bodies-only", "bool",
                        "false", "Only rewrite borrowed native evaluator bodies">];
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                           "mlir::cf::ControlFlowDialect"];
}

def LowerConditionalReferenceAccessesPass
    : Pass<"schedule-lower-conditional-reference-accesses",
           "obelisk::sim::SimFuncOp"> {
  let summary = "Lower selected reference accesses to conditional branches";
  let description = [{
    Access the selected reference at the original statement, preserving source
    snapshots and effects between reference selection and use. Pure reference
    views are rebuilt in the selected arm, a plain CFG block that later CFG
    cleanups keep apart from the other arm; an access inside a single-block
    region keeps a structured choice. This removes mixed local and canonical
    reference families from generated native kernels without speculative
    loads or runtime handoff.
  }];
  let options = [Option<"nativeBodiesOnly", "native-bodies-only", "bool",
                        "false", "Only rewrite borrowed native evaluator bodies">];
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                           "mlir::arith::ArithDialect",
                           "mlir::cf::ControlFlowDialect",
                           "mlir::scf::SCFDialect"];
}

def LocalizePrivateEvalStoragePass
    : Pass<"schedule-localize-private-eval-storage",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Localize evaluator storage using activation memory proofs";
  let description = [{
    After evaluator cloning and inlining, prove exclusive storage ownership and
    write-before-read coverage within each invocation. Replace eligible roots
    with typed local allocations while retaining canonical coroutine storage.
    Unwatched shared roots with no callee or external observer use local slots
    with incoming-value preservation and one write-back per return. Large
    partially written roots retain their stores and only forward loads.
    Rebuild the compute graph after changing evaluator memory effects.
  }];
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                          "mlir::arith::ArithDialect"];
}

def PromoteEvalStoragePass
    : Pass<"schedule-promote-eval-storage", "obelisk::sim::SimDesignOp"> {
  let summary = "Promote local evaluator storage to SSA";
  let description = [{
    After activation storage localization, expose compact reference joins,
    canonicalize constant views, promote whole values, then destructure closed
    slots accessed through static fields and promote their loads and stores.
    Only borrowed evaluators are rewritten: resumable
    actors retain their frozen continuation operands. Escaped and dynamically
    indexed slots stay addressable. Rebuild the compute graph afterward.
  }];
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                          "mlir::arith::ArithDialect"];
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

def OptimizeNativeWordsPass
    : Pass<"schedule-optimize-native-words", "mlir::ModuleOp"> {
  let summary = "Prove independent packed lanes and form native word regions";
  let options = [Option<"predicationOperationBudget",
      "predication-operation-budget", "uint64_t", "262144",
      "Maximum operations cloned for predication retries per function">];
  let dependentDialects = ["obelisk::schedule::ScheduleDialect",
    "obelisk::sim::ObeliskSimulationDialect", "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect", "mlir::ub::UBDialect"];
}

def OptimizeNativeAggregatesPass
    : Pass<"schedule-optimize-native-aggregates", "::obelisk::sim::SimFuncOp"> {
  let summary = "Forward proved SSA array elements before packed lowering";
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                          "mlir::arith::ArithDialect"];
  let options = [Option<"evalOnly", "eval-only", "bool", "false",
      "Only rewrite specialized eval bodies after native frame planning">];
}
def PrepareNativeAggregatesPass
    : Pass<"schedule-prepare-native-aggregates", "mlir::ModuleOp"> {
  let summary = "Optimize native SSA arrays with concurrent function passes";
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                          "mlir::arith::ArithDialect"];
}

def PlanNativeTransfersPass
    : Pass<"schedule-plan-native-transfers", "mlir::ModuleOp"> {
  let summary = "Expose ordered capture-copy activations before specialization";
  let dependentDialects = ["obelisk::schedule::ScheduleDialect"];
}
def ShareNativeTransfersPass
    : Pass<"schedule-share-native-transfers", "mlir::ModuleOp"> {
  let summary = "Share transfer implementations with identical canonical capture ABIs";
  let dependentDialects = ["obelisk::schedule::ScheduleDialect"];
}
def PrepareNativeTransferKernelsPass
    : Pass<"schedule-prepare-native-transfer-kernels", "mlir::ModuleOp"> {
  let summary = "Prepare selected transfer kernels for native representation lowering";
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                           "obelisk::schedule::ScheduleDialect",
                           "mlir::cf::ControlFlowDialect"];
}
def CoalesceNativeReadyPass
    : Pass<"schedule-coalesce-native-ready", "mlir::ModuleOp"> {
  let summary = "Combine exact pending-set updates before physical expansion";
  let dependentDialects = ["obelisk::schedule::ScheduleDialect",
                           "mlir::arith::ArithDialect"];
}
def CacheNativePureConesPass
    : Pass<"schedule-cache-pure-cones", "mlir::ModuleOp"> {
  let summary =
      "Cache bounded pure native cones by complete scalar input snapshots";
  let dependentDialects = ["mlir::LLVM::LLVMDialect",
                           "mlir::arith::ArithDialect",
                           "mlir::cf::ControlFlowDialect"];
  let options = [Option<"minimumCost", "minimum-cost", "uint64_t", "256",
                        "Minimum pure operation count">,
                 Option<"maximumCost", "maximum-cost", "uint64_t", "4096",
                        "Maximum cone code budget">,
                 Option<"maximumCaches", "maximum-caches", "unsigned", "128",
                        "Maximum per-thread cache records">];
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
def ThinNativeGCCollectionsPass
    : Pass<"schedule-thin-gc-collections", "mlir::ModuleOp"> {
  let summary = "Keep one designated GC collection per periodic clock cycle";
  let description = [{
    Runs after the generated periodic schedule is materialized. A function
    whose designated collection points follow edges of proven periodic clocks
    keeps only the point after the rising edge of the clock with the shortest
    period; every other point of that function, including those at slot
    ends, is removed and reports success. The heap then reclaims deferred
    allocation pressure once per cycle (IEEE 1800-2023 8.29 leaves the timing
    of reclamation to the implementation). Functions without periodic points
    keep all of theirs.
  }];
  let dependentDialects = ["mlir::arith::ArithDialect",
                           "mlir::cf::ControlFlowDialect"];
}

def LowerNativeGCCollectionsPass
    : Pass<"schedule-lower-gc-collections", "mlir::ModuleOp"> {
  let summary = "Lower designated GC collection points to the runtime dialect";
  let description = [{
    Replaces each schedule.gc.collect with rt.gc.collect on its runtime
    context and returns the runtime status bits. Runtime-to-LLVM conversion
    then calls the runtime, which collects only when pressure is pending. A
    point guarded by an edge runs only in slots where that edge occurred.
  }];
  let dependentDialects = ["obelisk::runtime::ObeliskRuntimeDialect",
                           "mlir::arith::ArithDialect",
                           "mlir::cf::ControlFlowDialect"];
}

def VersionKnownActivationsPass
    : Pass<"schedule-version-known-activations", "mlir::ModuleOp"> {
  let summary = "Run activations on value planes while their gated reads are known";
  let description = [{
    Gives each profitable nonsuspending native activation body a copy, entered
    after one check at its entry finds the unknown summary bits clear for the
    storage roots of its gated reads and known stores. In the copy a gated
    read loads only its value plane, a known store writes only its value
    plane, and a value the bit-precise analysis proves known although its
    operands may hold X/Z elsewhere takes its two-state view; every other
    read and store keeps both planes, so the copy performs the same effects
    with the same values (IEEE 1800-2023 4.3, 6.3.1, 11.4). A failed check
    runs the original four-state body. With `segment-size`, the body is cut
    into segments entered only at their first operation, each checked at its
    own entry together with the private slots and live-in values its copy
    relies on. With `checked-reads`, a read the check cannot cover is tested
    where the copy loads it, and on X/Z the copy continues in the original
    right after the same read, so no effect is repeated or skipped.
    LocalUnknownAnalysis chooses the reads.
  }];
  let options = [
    Option<"minimumBenefit", "minimum-benefit", "uint64_t", "64",
           "Smallest estimated unknown-plane work worth a copy">,
    Option<"segmentSize", "segment-size", "uint64_t", "0",
           "Operations after which a segment ends at the next cut point; 0 "
           "versions the whole body as one segment">,
    Option<"variants", "variants", "bool", "true",
           "Also version guarded two-state variants">,
    Option<"checkedReads", "checked-reads", "bool", "false",
           "Test reads the entry check cannot cover where they load">
  ];
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                           "obelisk::schedule::ScheduleDialect",
                           "mlir::arith::ArithDialect",
                           "mlir::cf::ControlFlowDialect"];
}

def VersionKnownRegionsPass
    : Pass<"schedule-version-known-regions", "mlir::ModuleOp"> {
  let summary = "Run loops and straight-line regions on value planes when known";
  let description = [{
    Gives each profitable region of a native activation body a two-state copy,
    entered after one check finds the region's four-state inputs known: its
    logic live-ins and the locations its reads observe. A loop switches copies
    at every header visit, so four-state iterations last only while an X/Z
    input is live. Copies preserve every effect and its order, and an X/Z input
    keeps the canonical four-state code (IEEE 1800-2023 6.3.1, 11.4).
  }];
  let dependentDialects = ["obelisk::sim::ObeliskSimulationDialect",
                           "obelisk::schedule::ScheduleDialect",
                           "mlir::arith::ArithDialect",
                           "mlir::cf::ControlFlowDialect"];
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
