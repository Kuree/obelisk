//===- SDFOps.td - Transient normalized SDF operations ------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SDF_SDFOPS_TD
#define OBELISK_DIALECT_SDF_SDFOPS_TD

include "obelisk/Dialect/SDF/SDFAttrs.td"
include "mlir/IR/BuiltinAttributes.td"

class SDFOp<string mnemonic, list<Trait> traits = []>
    : Op<ObeliskSDFDialect, mnemonic, traits>;

def SDFDelayFileOp : SDFOp<"delay_file", [
    IsolatedFromAbove, SingleBlock, NoTerminator
  ]> {
  let summary = "one normalized IEEE 1800-2017 Clause 32 delay file";
  let arguments = (ins StrAttr:$source, StrAttr:$sdf_version,
      OptionalAttr<StrAttr>:$design, OptionalAttr<StrAttr>:$date,
      OptionalAttr<StrAttr>:$vendor, OptionalAttr<StrAttr>:$program,
      OptionalAttr<StrAttr>:$program_version,
      OptionalAttr<StrAttr>:$divider, OptionalAttr<SDFDelayValueAttr>:$voltage,
      OptionalAttr<StrAttr>:$process,
      OptionalAttr<SDFDelayValueAttr>:$temperature,
      OptionalAttr<SDFTimeScaleAttr>:$timescale);
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
  let hasVerifier = 1;
}

def SDFCellOp : SDFOp<"cell", [SingleBlock, NoTerminator,
    HasParent<"SDFDelayFileOp">]> {
  let summary = "one CELL and its ordered annotation records";
  let arguments = (ins StrAttr:$cell_type, OptionalAttr<StrAttr>:$instance,
      OptionalAttr<UnitAttr>:$wildcard);
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
  let hasVerifier = 1;
}

def SDFPathDelayOp : SDFOp<"path_delay", [HasParent<"SDFCellOp">]> {
  let arguments = (ins SDFDelayMode:$mode, SDFPathKind:$kind,
      SDFPortAttr:$input, SDFPortAttr:$output,
      OptionalAttr<SDFConditionAttr>:$condition,
      SDFDelayValueArrayAttr:$delays);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def SDFTimingCheckOp : SDFOp<"timing_check", [HasParent<"SDFCellOp">]> {
  let arguments = (ins SDFTimingCheckKind:$kind,
      SDFTimingEventArrayAttr:$events, SDFDelayValueArrayAttr:$limits);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def SDFLabelOp : SDFOp<"label", [HasParent<"SDFCellOp">]> {
  let arguments = (ins StrAttr:$name, SDFDelayValueAttr:$value);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def SDFInterconnectDelayOp
    : SDFOp<"interconnect_delay", [HasParent<"SDFCellOp">]> {
  let arguments = (ins SDFDelayMode:$mode, SDFPortAttr:$source_port,
      SDFPortAttr:$destination_port, SDFDelayValueArrayAttr:$delays);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def SDFTerminalDelayOp
    : SDFOp<"terminal_delay", [HasParent<"SDFCellOp">]> {
  let arguments = (ins SDFDelayMode:$mode, SDFTerminalDelayKind:$kind,
      OptionalAttr<SDFPortAttr>:$terminal,
      SDFDelayValueArrayAttr:$delays);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def SDFPulseOp : SDFOp<"pulse", [HasParent<"SDFCellOp">]> {
  let arguments = (ins SDFPulseKind:$kind,
      OptionalAttr<SDFPortAttr>:$input, OptionalAttr<SDFPortAttr>:$output,
      SDFDelayValueAttr:$reject, SDFDelayValueAttr:$error);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

#endif // OBELISK_DIALECT_SDF_SDFOPS_TD
