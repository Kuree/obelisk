//===- DPIAttrs.td - DPI-C ABI attributes -------------------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_DPI_DPIATTRS_TD
#define OBELISK_DIALECT_DPI_DPIATTRS_TD

include "obelisk/Dialect/DPI/DPIDialect.td"
include "obelisk/Dialect/DPI/DPIEnums.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"

class DPIAttr<string name, string attrMnemonic, list<Trait> traits = []>
    : AttrDef<DPIDialect, name, traits> {
  let mnemonic = attrMnemonic;
}

def DPIABIAttr : DPIAttr<"ABI", "abi"> {
  let summary = "one logical value in a DPI-C signature";
  let parameters = (ins EnumParameter<DPIABIKind>:$kind,
      EnumParameter<DPIArgumentDirection>:$direction, "uint32_t":$width,
      "bool":$fourState, "bool":$isSigned);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def DPIABIArrayAttr : TypedArrayAttrBase<DPIABIAttr, "DPI ABI signature"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def DPIOpenArrayABIAttr : DPIAttr<"OpenArrayABI", "open_array_abi"> {
  let summary = "concrete actual shape bound to one DPI open-array formal";
  let parameters = (ins
      // 0 is a fixed packed transport, 1 is a single dynamic-array or queue
      // handle, and 2 is a provenance-layout value interpreted by shapePlan.
      "uint32_t":$storage,
      EnumParameter<DPIABIKind>:$elementKind, "uint32_t":$elementWidth,
      "uint64_t":$transportWidth, "bool":$transportFourState,
      "bool":$fourState,
      // Exact canonical C layout of one element. The recursive plan is reused
      // across all elements, so metadata stays proportional to the element type
      // rather than the actual array extent.
      "uint64_t":$elementCSize, "uint32_t":$elementCAlignment,
      "uint64_t":$elementStringCount,
      "::mlir::DenseI64ArrayAttr":$elementLeaves, "int64_t":$packedLeft,
      "int64_t":$packedRight,
      // Declared left/right pairs for unpacked dimensions, outermost first.
      "::mlir::DenseI64ArrayAttr":$ranges,
      // Original actual ranges retain source direction for copy-in/copy-out;
      // sized formal dimensions may have different normalized descriptor
      // ranges even though their extents match.
      "::mlir::DenseI64ArrayAttr":$sourceRanges,
      // Eight-word records for runtime-shaped actuals. Each record describes
      // one unpacked dimension without expanding fixed extents in the IR:
      // kind, descriptor left/right, source start, signed source stride,
      // child provenance span, child four-state flag, reserved. Kinds are
      // fixed(0), dynamic array(1), and queue(2).
      "::mlir::DenseI64ArrayAttr":$shapePlan);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def DPIAggregateABIAttr : DPIAttr<"AggregateABI", "aggregate_abi"> {
  let summary = "exact C layout for one sized DPI unpacked aggregate";
  let parameters = (ins "uint64_t":$cSize, "uint32_t":$cAlignment,
      // Number of recursively expanded string leaves. Native lowering reserves
      // one persistent inline-string scratch slot per leaf for the whole call.
      "uint64_t":$stringCount,
      // Eight-word leaf/repeat records form a compact recursive layout plan;
      // repeat bit strides are signed to encode lower-index-first C arrays.
      "::mlir::DenseI64ArrayAttr":$leaves);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

#endif // OBELISK_DIALECT_DPI_DPIATTRS_TD
