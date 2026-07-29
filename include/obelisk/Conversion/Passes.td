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
