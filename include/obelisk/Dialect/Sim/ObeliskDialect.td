//===- ObeliskDialect.td - Obelisk simulation dialect -----*- tablegen -*-===//
//
// Obelisk's semantic IR sits between elaborated Slang IR and LLVM dialects. It
// preserves the SystemVerilog concepts which cannot be represented faithfully
// as ordinary two-state SSA dataflow: four-state values, scheduler regions,
// mutable variables and nets, processes, objects, randomization, and runtime
// services.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_DIALECT_SIM_OBELISKDIALECT_TD
#define OBELISK_DIALECT_SIM_OBELISKDIALECT_TD

include "mlir/IR/DialectBase.td"

def ObeliskDialect : Dialect {
  let name = "obelisk";
  let cppNamespace = "::obelisk::ir";

  let summary = "Exact SystemVerilog simulation semantics";
  let description = [{
    The Obelisk dialect is the complete semantic landing boundary for the
    elaborated Slang frontend and target-specific lowering. Operations retain
    IEEE 1800 event scheduling and four-state behavior while making
    ownership, side effects, suspension, and runtime dependencies explicit.

    The dialect is intentionally not an AST.  Elaboration and source-language
    resolution happen before it; ABI layout and machine-code selection happen
    after it.
  }];

  // Enum attributes are specialized builtin IntegerAttr wrappers, so the
  // dialect has no standalone `#obelisk.*` attributes to parse.
  let useDefaultAttributePrinterParser = 0;
  let useDefaultTypePrinterParser = 1;
  // Preserve standard SymbolTable interoperability across textual round trips.
  let usePropertiesForAttributes = 0;
}

#endif // OBELISK_DIALECT_SIM_OBELISKDIALECT_TD
