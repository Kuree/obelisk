//===- SimulationAttrs.td - Late simulation analysis attributes -*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD

include "obelisk/Dialect/Simulation/SimulationDialect.td"
include "obelisk/Dialect/Simulation/SimulationEnums.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/EnumAttr.td"

class SimAttr<string name, string attrMnemonic, list<Trait> traits = []>
    : AttrDef<ObeliskSimulationDialect, name, traits> {
  let mnemonic = attrMnemonic;
}

def SimVPIPropertyAttr : SimAttr<"VPIProperty", "vpi_property"> {
  let summary = "one typed immutable VPI property value";
  let parameters = (ins "::mlir::IntegerAttr":$selector,
      "::mlir::Attribute":$value);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimVPIPropertySetAttr : SimAttr<"VPIPropertySet", "vpi_properties"> {
  let summary = "sorted unique immutable VPI property values";
  let parameters = (ins "::mlir::ArrayAttr":$properties);
  let assemblyFormat = "`<` $properties `>`";
  let genVerifyDecl = 1;
}

def SimVPIObjectRefAttr : SimAttr<"VPIObjectRef", "vpi_ref"> {
  let summary = "stable logical reference to a VPI-visible design object";
  let description = [{
    Identifies an existing immutable design-inventory declaration without
    exposing its eventual reflection-image table index. Each reference kind
    has its own stable ID namespace. The serializer resolves references only
    after physical object records have been sorted.
  }];
  let parameters = (ins EnumParameter<SimVPIObjectRefKind>:$kind,
      "::mlir::IntegerAttr":$id);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimVPIObjectBackingAttr
    : SimAttr<"VPIObjectBacking", "vpi_backing"> {
  let summary = "physical executable descriptor backing a source VPI object";
  let description = [{
    Maps one immutable source-object anchor to an already materialized scope,
    class, code-unit, or net descriptor. Scope, code-unit, and net backings use
    `id`; class backings use `symbol`. An anchor without this attribute is
    itself serialized as a static-only reflection object.
  }];
  let parameters = (ins EnumParameter<SimVPIObjectBackingKind>:$kind,
      OptionalParameter<"::mlir::IntegerAttr">:$id,
      OptionalParameter<"::mlir::FlatSymbolRefAttr">:$symbol);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimVPITypeSemanticsAttr
    : SimAttr<"VPITypeSemantics", "vpi_type"> {
  let summary = "source-semantic type retained for VPI reflection";
  let description = [{
    Preserves source type identity after executable lowering normalizes
    integer flavors, enums, time, and managed values. `children` contains the
    enum base type, array/container element type, associative key and element,
    or declaration-ordered aggregate fields as appropriate. Aggregate layout,
    container bounds, wildcard indexing, virtual-interface modports, symbolic
    declaration identity, and typedef layers are retained in typed optional
    payloads. This attribute is immutable inventory only and must not
    participate in executable type conversion or scheduler decisions.
  }];
  let parameters = (ins EnumParameter<SimVPITypeKind>:$kind,
      "bool":$isSigned, "bool":$isFourState,
      OptionalParameter<"::mlir::StringAttr">:$name,
      OptionalParameter<"::mlir::SymbolRefAttr">:$symbol,
      OptionalParameter<"::mlir::StringAttr">:$modport,
      "::mlir::DenseI64ArrayAttr":$range, "::mlir::ArrayAttr":$children,
      "::mlir::ArrayAttr":$childNames,
      OptionalParameter<"::mlir::BoolAttr">:$isTagged,
      OptionalParameter<"::mlir::BoolAttr">:$isSoft,
      OptionalParameter<"::mlir::IntegerAttr">:$bitWidth,
      OptionalParameter<"::mlir::IntegerAttr">:$selectableWidth,
      OptionalParameter<"::mlir::IntegerAttr">:$bitstreamWidth,
      OptionalParameter<"::mlir::IntegerAttr">:$tagBits,
      OptionalParameter<"::mlir::IntegerAttr">:$queueBound,
      OptionalParameter<"::mlir::BoolAttr">:$wildcardIndex,
      OptionalParameter<"::mlir::DenseI64ArrayAttr">:$childOrdinals,
      OptionalParameter<"::mlir::DenseI64ArrayAttr">:$childPackedOffsets,
      OptionalParameter<"::mlir::DenseI64ArrayAttr">:$childRandTypes,
      OptionalParameter<"::mlir::ArrayAttr">:$typedefAliases);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

//===----------------------------------------------------------------------===//
// Prepared unit boundary
//===----------------------------------------------------------------------===//

def SimFrozenConstantAttr
    : SimAttr<"FrozenConstant", "frozen_constant", [TypedAttrInterface]> {
  let summary = "typed elaborated constant frozen into one simulation unit";
  let description = [{
    Carries the exact value of a parameter, localparam, enum value, specparam,
    or elaborated genvar after whole-design elaboration has completed. Packed
    values use the same two-plane `[value, unknown]` representation as
    `obelisk_sim.logic.constant`; real values use an f64 FloatAttr. Fixed
    unpacked arrays use an ArrayAttr of recursively typed frozen constants.
    The declared normalized type is retained so aggregate shape is not lost.
  }];
  let parameters = (ins AttributeSelfTypeParameter<"">:$type,
      "::mlir::Attribute":$value, "bool":$isSigned);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimArgumentBindingAttr : SimAttr<"ArgumentBinding", "argument_binding"> {
  let summary = "source name bound to one prepared unit argument";
  let parameters = (ins "::mlir::StringAttr":$path, "uint64_t":$argument,
      EnumParameter<SimUnitArgumentKind>:$kind, "bool":$copyOut,
      OptionalParameter<"::mlir::IntegerAttr">:$lvalueNode,
      // IEEE 1800-2017 13.5.1: an input or inout formal takes the caller's
      // value at the call. An output formal does not, which a static subroutine
      // can tell apart: its formal keeps what the previous call left in it.
      DefaultValuedParameter<"bool", "true">:$copyIn);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimDescriptorBindingAttr
    : SimAttr<"DescriptorBinding", "descriptor_binding"> {
  let summary = "source name bound directly through the simulation context";
  let parameters = (ins "::mlir::StringAttr":$path, "uint64_t":$descriptor,
      "::mlir::Type":$type);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimLocalBindingAttr : SimAttr<"LocalBinding", "local_binding"> {
  let summary = "source name bound to activation-local storage";
  let parameters = (ins "::mlir::StringAttr":$path, "::mlir::Type":$type,
      "bool":$automatic, "bool":$patternVariable, "bool":$isReturn);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimConstantBindingAttr : SimAttr<"ConstantBinding", "constant_binding"> {
  let summary = "source name bound to a typed elaborated constant";
  let parameters = (ins "::mlir::StringAttr":$path,
      SimFrozenConstantAttr:$value);
  // `qualified` is required for this nested typed attribute so its mnemonic
  // and trailing type survive textual IR round-trips.
  let assemblyFormat =
      "`<` `path` `=` $path `,` `value` `=` qualified($value) `>`";
  let genVerifyDecl = 1;
}

def SimFieldAttr : SimAttr<"Field", "field"> {
  let summary = "ordered aggregate field metadata";
  let parameters = (ins "::mlir::StringAttr":$name, "::mlir::Type":$type,
      "uint32_t":$ordinal, "uint64_t":$packedOffset);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// A constraint template is defined relative to one active object.  Keep its
// object traversal symbolic until a target-specific class layout is available:
// every path component and the terminal variable name a class-field
// declaration, never a byte offset.  An empty path denotes a variable on the
// origin object itself.
def SimRandomVariableReferenceAttr
    : SimAttr<"RandomVariableReference", "random_variable_reference"> {
  let summary = "object-relative symbolic random-variable reference";
  let description = [{
    Identifies one packed rand or randc field relative to the object owning a
    randomization constraint template. `path` follows strong rand class-handle
    fields in source order; `target` names the terminal packed random field.
    The owning class operation verifies the complete static type chain.
  }];
  let parameters =
      (ins OptionalArrayRefParameter<"::mlir::FlatSymbolRefAttr">:$path,
          "::mlir::FlatSymbolRefAttr":$target);
  let assemblyFormat =
      "`<` (`path` `=` `[` $path^ `]` `,`)? `target` `=` $target `>`";
  let genVerifyDecl = 1;
}

def SimRandomVariableReferenceArrayAttr
    : TypedArrayAttrBase<SimRandomVariableReferenceAttr,
                         "random-variable reference array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

// Unlike RandomVariableReferenceAttr, this is an operand of a constraint
// template.  A field that is an active random variable for one invocation may
// be state for another invocation after rand_mode is applied.  Preserve one
// neutral symbolic reference and let active-object composition choose whether
// it becomes a solver variable or a captured value.
def SimRandomValueReferenceAttr
    : SimAttr<"RandomValueReference", "random_value_reference"> {
  let summary = "symbolic packed value consumed by a constraint template";
  let description = [{
    Identifies a packed bit slice without embedding a target layout offset.
    `object_field` starts at the object owning the template, follows the
    optional strong class-handle `path`, and names a terminal instance field.
    `storage` names an immutable design-storage descriptor ID.  Runtime graph
    composition classifies an object field as an active random variable or as
    current state according to the invocation's active-object set and
    rand_mode state.
  }];
  let parameters = (ins EnumParameter<SimRandomValueReferenceKind>:$kind,
      OptionalArrayRefParameter<"::mlir::FlatSymbolRefAttr">:$path,
      OptionalParameter<"::mlir::FlatSymbolRefAttr">:$target,
      OptionalParameter<"::mlir::IntegerAttr">:$storage, "uint64_t":$low,
      "uint64_t":$width);
  let assemblyFormat = [{
    `<` `kind` `=` $kind `,`
    (`path` `=` `[` $path^ `]` `,`)?
    (`target` `=` $target^ `,`)?
    (`storage` `=` $storage^ `,`)?
    `low` `=` $low `,` `width` `=` $width `>`
  }];
  let genVerifyDecl = 1;
}

def SimRandomValueReferenceArrayAttr
    : TypedArrayAttrBase<SimRandomValueReferenceAttr,
                         "random-value reference array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def SimRandomConstraintBlockReferenceAttr
    : SimAttr<"RandomConstraintBlockReference",
              "random_constraint_block_reference"> {
  let summary = "symbolic constraint_mode state consumed by a template";
  let parameters =
      (ins EnumParameter<SimRandomConstraintBlockReferenceKind>:$kind,
          OptionalParameter<"::mlir::IntegerAttr">:$index,
          OptionalParameter<"::mlir::IntegerAttr">:$storage);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimRandomConstraintBlockReferenceArrayAttr
    : TypedArrayAttrBase<SimRandomConstraintBlockReferenceAttr,
                         "random-constraint block reference array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}


def SimDPIABIAttr : SimAttr<"DPIABI", "dpi_abi"> {
  let summary = "one logical value in a DPI-C import signature";
  let parameters = (ins EnumParameter<SimDPIABIKind>:$kind,
      EnumParameter<SimDPIArgumentDirection>:$direction, "uint32_t":$width,
      "bool":$fourState, "bool":$isSigned);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimDPIABIArrayAttr
    : TypedArrayAttrBase<SimDPIABIAttr, "DPI ABI signature"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def SimDPIOpenArrayABIAttr : SimAttr<"DPIOpenArrayABI", "dpi_open_array_abi"> {
  let summary = "concrete actual shape bound to one DPI open-array formal";
  let parameters = (ins
      // 0 is a fixed packed transport, 1 is a single dynamic-array or queue
      // handle, and 2 is a provenance-layout value interpreted by shapePlan.
      "uint32_t":$storage,
      EnumParameter<SimDPIABIKind>:$elementKind, "uint32_t":$elementWidth,
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

def SimDPIAggregateABIAttr : SimAttr<"DPIAggregateABI", "dpi_aggregate_abi"> {
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

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD
