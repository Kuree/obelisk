//===- ObeliskDialect.td - Obelisk semantic dialect -------*- tablegen -*-===//
//
// Obelisk's semantic IR sits between elaborated Slang IR and LLVM dialects. It
// preserves the SystemVerilog concepts which cannot be represented faithfully
// as ordinary two-state SSA dataflow: four-state values, scheduler regions,
// mutable variables and nets, processes, objects, randomization, and runtime
// services.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_DIALECT_OBELISK_OBELISKDIALECT_TD
#define OBELISK_DIALECT_OBELISK_OBELISKDIALECT_TD

include "mlir/IR/DialectBase.td"

def ObeliskDialect : Dialect {
  let name = "obelisk";
  let cppNamespace = "::obelisk::ir";

  let summary = "Elaborated SystemVerilog semantic AST";
  let description = [{
    The Obelisk dialect is the complete semantic landing boundary for the
    elaborated Slang frontend. Its concrete AST operations preserve source
    identity, resolved references, declared types, and the full elaborated
    hierarchy. Executable SSA, explicit scheduler effects, and flattened
    numeric descriptors belong to the separate `obelisk_sim` dialect.
  }];

  // Enum attributes are specialized builtin IntegerAttr wrappers, so the
  // dialect has no standalone `#obelisk.*` attributes to parse.
  let useDefaultAttributePrinterParser = 0;
  let useDefaultTypePrinterParser = 1;
  // Preserve standard SymbolTable interoperability across textual round trips.
  let usePropertiesForAttributes = 0;
}

#endif // OBELISK_DIALECT_OBELISK_OBELISKDIALECT_TD
