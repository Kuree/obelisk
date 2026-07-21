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

def ObeliskSimFinalizePass
    : Pass<"obelisk-sim-finalize", "mlir::ModuleOp"> {
  let summary = "Remove semantic IR and verify the executable simulation boundary";
  let dependentDialects = [
    "obelisk::sim::ObeliskSimulationDialect"
  ];
}

#endif // OBELISK_CONVERSION_PASSES_TD
