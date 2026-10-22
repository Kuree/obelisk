//===- DPIDialect.td - SystemVerilog DPI-C boundary --------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_DPI_DPIDIALECT_TD
#define OBELISK_DIALECT_DPI_DPIDIALECT_TD

include "mlir/IR/DialectBase.td"

def DPIDialect : Dialect {
  let name = "dpi";
  let cppNamespace = "::obelisk::dpi";
  let summary = "SystemVerilog DPI-C boundary metadata (IEEE 1800-2023 35)";
  let description = [{
    `dpi` describes the C ABI of imported and exported subroutines: one
    `#dpi.abi` entry per logical value, exact C layouts for unpacked
    aggregates and open arrays, and the identity metadata that elaboration
    attaches to DPI calls, simulation functions and native thunks. It has no
    operations; its attributes annotate operations of other dialects.
  }];
  let useDefaultAttributePrinterParser = 1;
  let usePropertiesForAttributes = 0;
  let hasOperationAttrVerify = 1;
  let discardableAttrs = (ins
    // Roles of a code unit or function at the DPI boundary.
    "::mlir::UnitAttr":$import,
    "::mlir::UnitAttr":$export,
    // The capture-free function external C callers enter.
    "::mlir::UnitAttr":$export_bridge,
    // The SystemVerilog body behind an export bridge.
    "::mlir::UnitAttr":$export_body,
    "::mlir::UnitAttr":$task,
    "::mlir::UnitAttr":$pure,
    "::mlir::UnitAttr":$context,
    // Nonzero 32-bit identities stable across compilations of the design.
    "::mlir::IntegerAttr":$import_id,
    "::mlir::IntegerAttr":$export_id,
    "::mlir::StringAttr":$c_identifier,
    "::mlir::IntegerAttr":$scope_id,
    // `#dpi.abi` entries: logical inputs, then the result, then copy-outs.
    "::mlir::ArrayAttr":$abi_signature,
    // Per signature entry, a `#dpi.aggregate_abi` or a unit placeholder.
    "::mlir::ArrayAttr":$aggregate_layouts,
    "::mlir::IntegerAttr":$logical_inputs,
    // Logical output formals whose unused internal copy-in was removed.
    "::mlir::DenseI64ArrayAttr":$elided_inputs,
    "::mlir::IntegerAttr":$abi_hash,
    "::mlir::StringAttr":$export_bridge_symbol,
    "::mlir::StringAttr":$export_body_symbol,
    // Module: some scope exports a subroutine.
    "::mlir::UnitAttr":$has_exports,
    // Design: every export closure touches only its own locals and
    // arguments. Recorded with the compute graph's effect summaries, which
    // later rewrites may drop.
    "::mlir::BoolAttr":$transparent_exports
  );
}

#endif // OBELISK_DIALECT_DPI_DPIDIALECT_TD
