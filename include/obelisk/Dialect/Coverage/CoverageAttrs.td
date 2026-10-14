//===- CoverageAttrs.td - Coverage attributes ---------------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_COVERAGE_COVERAGEATTRS_TD
#define OBELISK_DIALECT_COVERAGE_COVERAGEATTRS_TD

include "obelisk/Dialect/Coverage/CoverageDialect.td"
include "mlir/IR/AttrTypeBase.td"

class CoverageAttr<string name, string attrMnemonic, list<Trait> traits = []>
    : AttrDef<CoverageDialect, name, traits> {
  let mnemonic = attrMnemonic;
}

def CoverageToggleBindingAttr
    : CoverageAttr<"ToggleBinding", "toggle_binding"> {
  let summary = "toggle points covering a bit range of a declaration";
  let description = [{
    `width` toggle points starting at inventory position `base` observe the
    declaration's state from bit `low` upward.
  }];
  let parameters = (ins "uint64_t":$base, "int64_t":$low, "uint64_t":$width);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

#endif // OBELISK_DIALECT_COVERAGE_COVERAGEATTRS_TD
