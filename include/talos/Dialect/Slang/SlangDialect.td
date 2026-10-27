//===- SlangDialect.td - Elaborated slang semantic AST -------*- tablegen -*-===//

#ifndef TALOS_DIALECT_SLANG_SLANGDIALECT_TD
#define TALOS_DIALECT_SLANG_SLANGDIALECT_TD

include "mlir/IR/DialectBase.td"

def SlangDialect : Dialect {
  let name = "slang";
  let cppNamespace = "::talos::slangir";
  let summary = "Lossless elaborated SystemVerilog semantic AST";
  let description = [{
    The slang dialect is Talos's source-language boundary. Every registered
    operation corresponds to one concrete dispatch kind in the selected slang
    semantic AST. The IR retains elaborated hierarchy, resolved references,
    semantic types, constants, source ranges, and macro expansion locations.
  }];
  let useDefaultTypePrinterParser = 1;
  // SymbolTable currently indexes symbol names through the operation attribute
  // dictionary. Keep ODS attributes there so parsed and programmatically built
  // semantic symbols have identical lookup behavior.
  let usePropertiesForAttributes = 0;
  // This dialect defines no standalone attributes.
  let useDefaultAttributePrinterParser = 0;
}

#endif // TALOS_DIALECT_SLANG_SLANGDIALECT_TD
