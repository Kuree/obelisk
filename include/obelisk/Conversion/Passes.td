//===- Passes.td - Obelisk conversion pass definitions -----*- tablegen -*-===//

#ifndef OBELISK_CONVERSION_PASSES_TD
#define OBELISK_CONVERSION_PASSES_TD

include "mlir/Pass/PassBase.td"

def ConvertMooreToObeliskPass
    : Pass<"convert-moore-to-obelisk", "mlir::ModuleOp"> {
  let summary = "Convert Moore IR to exact Obelisk semantic IR";
  let description = [{
    Exhaustively converts CIRCT Moore operations, types, and attributes to the
    verifier-backed Obelisk semantic dialect and lower-level target dialects.
  }];
  let dependentDialects = [
    "obelisk::ir::ObeliskDialect",
    "mlir::arith::ArithDialect",
    "circt::hw::HWDialect",
    "circt::sim::SimDialect"
  ];
}

#endif // OBELISK_CONVERSION_PASSES_TD
