//===- TalosDialect.td - Talos semantic dialect -------*- tablegen -*-===//
//
// Talos's semantic IR sits between elaborated Slang IR and LLVM dialects. It
// preserves the SystemVerilog concepts which cannot be represented faithfully
// as ordinary two-state SSA dataflow: four-state values, scheduler regions,
// mutable variables and nets, processes, objects, randomization, and runtime
// services.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_DIALECT_TALOS_TALOSDIALECT_TD
#define TALOS_DIALECT_TALOS_TALOSDIALECT_TD

include "mlir/IR/DialectBase.td"

def TalosDialect : Dialect {
  let name = "talos";
  let cppNamespace = "::talos::ir";

  let summary = "Elaborated SystemVerilog semantic AST";
  let description = [{
    The Talos dialect is the complete semantic landing boundary for the
    elaborated Slang frontend. Its concrete AST operations preserve source
    identity, resolved references, declared types, and the full elaborated
    hierarchy. Executable SSA, explicit scheduler effects, and flattened
    numeric descriptors belong to the separate `simulation` dialect.
  }];

  // Enum attributes are specialized builtin IntegerAttr wrappers, so the
  // dialect has no standalone `#talos.*` attributes to parse.
  let useDefaultAttributePrinterParser = 0;
  let useDefaultTypePrinterParser = 1;
  // Preserve standard SymbolTable interoperability across textual round trips.
  let usePropertiesForAttributes = 0;
}

#endif // TALOS_DIALECT_TALOS_TALOSDIALECT_TD
