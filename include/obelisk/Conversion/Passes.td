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
  let options = [
    Option<"pruneUnusedCoverage", "prune-unused-coverage", "bool", "true",
           "Prune unreferenced embedded covergroups before declaration materialization">
  ];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
    "mlir::cf::ControlFlowDialect"
  ];
}

def ObeliskSimMaterializeDPIExportsPass
    : Pass<"obelisk-sim-materialize-dpi-exports", "mlir::ModuleOp"> {
  let summary = "Materialize scope-specific zero-time DPI export bridges";
  let description = [{
    Builds one capture-free bridge for every elaborated exported function.
    Each bridge resolves the owning scope's immutable descriptor captures
    from its runtime context, so native and bytecode export dispatch share the
    same ordinary simulation function body and result ordering.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
}

def ObeliskSimPrepareCoveragePass
    : Pass<"obelisk-sim-prepare-coverage", "mlir::ModuleOp"> {
  let summary = "Inventory coverage obligations and emit the native schema";
  let description = [{
    Runs after early symbol DCE and before per-unit lowering so stable coverage
    identities do not depend on the optimization level. The pass owns all IR
    analysis and schema construction; the driver only supplies policy attrs.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
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

def ObeliskSimMaterializeClockedSamplesPass
    : Pass<"obelisk-sim-materialize-clocked-samples",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Materialize deferred assertion and coverage clock samplers";
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect",
    "mlir::arith::ArithDialect",
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

def ObeliskSimPlanNativePartitionsPass
    : Pass<"obelisk-sim-plan-native-partitions",
           "obelisk::sim::SimDesignOp"> {
  let summary = "Plan stable semantic native compilation partitions";
  let description = [{
    Groups executable simulation functions by semantic class or code-unit
    ownership, keeps direct-call SCCs indivisible, and records deterministic
    members, imports, exports, and dependencies. Partition identity never
    depends on IR order, worker count, or host load, so the same manifest can
    drive parallel object emission, ThinLTO, and incremental cache keys.
  }];
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
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

def MaterializeObeliskNativeEvalGroupsPass
    : Pass<"obelisk-materialize-native-eval-groups", "mlir::ModuleOp"> {
  let summary = "Materialize certified native activation bodies and boundary state";
  let description = [{
    Expands certified executor calls in LLVM dialect after value-domain routes
    are specialized and the canonical bytecode fallback is frozen. Preserves
    activation predicates and publishes SSA state at retained boundaries.
    Run before computing the final native partition manifest. The coroutine
    conversion invokes the same materializer at that point in its pipeline.
  }];
  let dependentDialects = ["mlir::LLVM::LLVMDialect"];
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
    "mlir::math::MathDialect",
    "mlir::scf::SCFDialect"
  ];
}

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

#endif // OBELISK_CONVERSION_PASSES_TD
