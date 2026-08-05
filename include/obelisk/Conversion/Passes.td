//===- Passes.td - Obelisk conversion pass definitions -----*- tablegen -*-===//

#ifndef OBELISK_CONVERSION_PASSES_TD
#define OBELISK_CONVERSION_PASSES_TD

include "mlir/Pass/PassBase.td"

def ConvertSlangToObeliskPass
    : Pass<"convert-slang-to-obelisk", "mlir::ModuleOp"> {
  let summary = "Convert elaborated Slang IR to exact Obelisk semantic IR";
  let description = [{
    Exhaustively converts every concrete Slang operation and type to its
    verifier-backed Obelisk counterpart. Slang entities are illegal after this
    pass and conversion fails if any source semantics remain.
  }];
  let dependentDialects = [
    "obelisk::ir::ObeliskDialect"
  ];
}

def ObeliskSimPreparePass
    : Pass<"obelisk-sim-prepare", "mlir::ModuleOp"> {
  let summary = "Resolve and freeze semantic IR into isolated simulation units";
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect"
  ];
}

def ObeliskSimLowerUnitPass
    : Pass<"obelisk-sim-lower-unit", "obelisk::sim::SimFuncOp"> {
  let summary = "Lower one frozen simulation code unit to SSA and CF";
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect",
    "mlir::math::MathDialect"
  ];
}

def ObeliskSimThreadSuspensionPass
    : Pass<"obelisk-sim-thread-suspension", "obelisk::sim::SimFuncOp"> {
  let summary = "Thread promoted SSA state through suspension continuations";
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::cf::ControlFlowDialect"
  ];
}

def ObeliskSimThreadProcessCFGPass
    : Pass<"obelisk-sim-thread-process-cfg", "obelisk::sim::SimFuncOp"> {
  let summary = "Make process state explicit on every CFG edge";
  let description = [{
    Threads non-capture SSA values through block arguments to make process
    continuation state explicit, and rematerializes constant continuation
    operands instead of storing them in the canonical process frame.
  }];
}

def ObeliskSimThreadRuntimeStatusesPass
    : Pass<"obelisk-sim-thread-runtime-statuses", "mlir::ModuleOp"> {
  let summary = "Thread runtime failures through simulation calls";
  let description = [{
    Computes the transitive set of simulation functions that may fail, appends
    a runtime status result to their signatures and call sites, and lowers
    status checks to explicit success and failure control flow.
  }];
}

def ObeliskSimInstrumentManagedRootsPass
    : Pass<"obelisk-sim-instrument-managed-roots", "mlir::ModuleOp"> {
  let summary = "Bind live managed SSA values to precise GC roots";
  let description = [{
    Uses MLIR liveness to allocate and maintain precise managed-root slots
    around collection points and process activation boundaries.
  }];
}

def ObeliskSimInstrumentReferenceLifetimesPass
    : Pass<"obelisk-sim-instrument-reference-lifetimes",
           "obelisk::sim::SimFuncOp"> {
  let summary = "Release automatic simulation references on CFG exits";
  let description = [{
    Uses MLIR dominance and liveness to insert one owner release on every
    control-flow edge where an automatic simulation reference dies.
  }];
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
  let summary = "Inline profitable zero-time simulation calls";
  let description = [{
    Uses MLIR's SCC inliner for CFG/SSA transformation while Obelisk owns
    simulation legality, specialization profitability, hierarchy retention,
    growth budgets, and deterministic optimization-level presets.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect"
  ];
  let options = [
    Option<"optLevel", "opt-level", "unsigned", "3",
           "Optimization preset from 0 (disabled) through 3">,
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
           "Plan candidates for physical process-body fusion instead of AOT dispatch batches">
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

def ObeliskSimFinalizePass
    : Pass<"obelisk-sim-finalize", "mlir::ModuleOp"> {
  let summary = "Remove semantic IR and verify the executable simulation boundary";
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::math::MathDialect"
  ];
}

def ConvertObeliskSimValuesToStandardPass
    : Pass<"convert-obelisk-sim-values-to-standard", "mlir::ModuleOp"> {
  let summary = "Lower pure four-state packed values to standard integer IR";
  let description = [{
    Converts each !obelisk_sim.logic<W> value to an iW value plane and an iW
    unknown plane, lowers the complete pure packed-value operation family, and
    converts standard function and control-flow boundaries. Runtime resources,
    scheduling, time, and obelisk_sim code-unit structure are left untouched.
  }];
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect",
    "mlir::func::FuncDialect",
    "mlir::math::MathDialect",
    "mlir::scf::SCFDialect"
  ];
}

def ObeliskSimLowerRealConversionsPass
    : Pass<"obelisk-sim-lower-real-conversions",
           "obelisk::sim::SimFuncOp"> {
  let summary = "Normalize pure simulation real conversions to arith";
  let description = [{
    Rewrites simulation time-to-real and integer/real conversions whose
    operands and results already have standard types. The pass introduces no
    runtime calls. Time-valued operations are normalized later with their
    backend type boundary.
  }];
  let dependentDialects = [
    "mlir::arith::ArithDialect"
  ];
}

def ConvertObeliskSimToRuntimePass
    : Pass<"convert-obelisk-sim-to-runtime", "mlir::ModuleOp"> {
  let summary = "Lower precise simulation display and file I/O to runtime calls";
  let description = [{
    Lowers literal bytes, display/write, and the supported packed file service
    subset to typed obelisk_rt calls and target-independent materializers. It
    also lowers four-state packed arguments to value/unknown planes, while
    leaving context projection and fragment status propagation as explicit
    hooks for native fragment lowering.
  }];
  let dependentDialects = [
    "obelisk::runtime::ObeliskRuntimeDialect",
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::func::FuncDialect"
  ];
}

def EncodeObeliskSimToBytecodePass
    : Pass<"encode-obelisk-sim-to-bytecode", "mlir::ModuleOp"> {
  let summary = "Strictly encode the normalized executable simulation boundary";
  let description = [{
    Builds one deterministic, pointer-free design bytecode image while leaving
    source obelisk_sim.func operations intact for native lowering. Every
    defined function must use the encoder's closed normalized legality set:
    integer arith constants, arithmetic, bitwise, shifts, comparisons, select,
    extensions/truncation/index casts; cf branches and switches; and the
    explicitly implemented obelisk_sim value, aggregate, reference, state,
    scheduling, suspension, call, formatting, and file operations. Any other
    executable operation is rejected at that operation instead of being
    silently omitted. Optional VPI reflection is emitted as a separate
    DWARF-like database image.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect"
  ];
  let options = [
    Option<"vpi", "vpi", "std::string", "\"auto\"",
           "Reflection profile: auto, off, read, or full">,
    Option<"requireBytecode", "require-bytecode", "bool", "false",
           "Require scheduler execution through the embedded bytecode tier">
  ];
}

def ConvertObeliskRuntimeToLLVMPass
    : Pass<"convert-obelisk-runtime-to-llvm", "mlir::ModuleOp"> {
  let summary = "Lower typed Obelisk runtime calls to the in-tree LLVM C ABI";
  let description = [{
    Validates the selected 64-bit little-endian target layout and linear runtime
    buffer ownership, converts runtime and function/CFG boundary types, and
    materializes exact external C declarations plus ABI out-parameter storage.
  }];
  let dependentDialects = [
    "mlir::LLVM::LLVMDialect"
  ];
}

def ConvertObeliskSimProcessesToLLVMCoroutinesPass
    : Pass<"convert-obelisk-sim-processes-to-llvm-coroutines",
           "mlir::ModuleOp"> {
  let summary = "Lower simulation processes to switched-resume LLVM coroutines";
  let description = [{
    Builds a deterministic target-layout canonical process frame, lowers only
    semantic suspension terminators to switched-resume LLVM coroutine points,
    and fully converts packed values, typed runtime operations, functions,
    arithmetic, and control flow without unrealized conversion casts.
  }];
  let dependentDialects = [
    "obelisk::runtime::ObeliskRuntimeDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect",
    "mlir::func::FuncDialect",
    "mlir::LLVM::LLVMDialect",
    "mlir::math::MathDialect"
  ];
}

#endif // OBELISK_CONVERSION_PASSES_TD
