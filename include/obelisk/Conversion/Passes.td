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
    "mlir::cf::ControlFlowDialect"
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

def ObeliskSimFinalizePass
    : Pass<"obelisk-sim-finalize", "mlir::ModuleOp"> {
  let summary = "Remove semantic IR and verify the executable simulation boundary";
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
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

#endif // OBELISK_CONVERSION_PASSES_TD
