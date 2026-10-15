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

def CoverageFsmBindingAttr : CoverageAttr<"FsmBinding", "fsm_binding"> {
  let summary = "FSM state-vector bits held by a declaration";
  let description = [{
    The declaration's state from bit `low` upward holds `width` bits of the
    state vector of the FSM whose schema states carry `machine`, starting at
    vector bit `shift`.
  }];
  let parameters = (ins "uint64_t":$machine, "uint64_t":$shift,
                        "int64_t":$low, "uint64_t":$width);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

#endif // OBELISK_DIALECT_COVERAGE_COVERAGEATTRS_TD
