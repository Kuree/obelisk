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

def SimStorageLayoutAttr : SimAttr<"StorageLayout", "storage_layout"> {
  let summary = "LLVM DataLayout geometry of one unpacked aggregate plane";
  let description = [{
    Sizes, ABI alignment, array stride and declaration-order struct offsets
    are in bytes. Semantic normalization obtains these from the target LLVM
    array/struct type. Native and bytecode backends share this layout; packed
    leaves retain their exact SystemVerilog bit width. An absent encoding
    denotes the legacy, target-independent Simulation IR layout.
  }];
  let parameters = (ins "uint64_t":$size, "uint64_t":$alignment,
      "uint64_t":$stride, "::mlir::DenseI64ArrayAttr":$offsets);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
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
    `simulation.logic.constant`; real values use an f64 FloatAttr. Fixed
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


def SimIntegerRangeAttr : SimAttr<"IntegerRange", "integer_range"> {
  let summary = "unsigned and signed bounds an integer value never leaves";
  let parameters = (ins "::mlir::IntegerAttr":$umin,
      "::mlir::IntegerAttr":$umax, "::mlir::IntegerAttr":$smin,
      "::mlir::IntegerAttr":$smax);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimIntegerExpressionAttr
    : SimAttr<"IntegerExpression", "integer_expression"> {
  let summary = "symbolic integer function of a call's arguments";
  let description = [{
    A `constant` holds its `value`; an `argument` names the call operand
    `index`; every other kind applies to its `operands`. All nodes have a
    nonzero `width` of at most 128 bits.
  }];
  let parameters = (ins EnumParameter<SimIntegerExpressionKind>:$kind,
      "uint32_t":$width, OptionalParameter<"::mlir::IntegerAttr">:$value,
      OptionalParameter<"std::optional<uint32_t>">:$index,
      OptionalParameter<"::mlir::ArrayAttr">:$operands);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimBooleanMinimizationAttr
    : SimAttr<"BooleanMinimization", "boolean_minimization"> {
  let summary = "boolean minimization of one concurrent property operand";
  let description = [{
    Compile-time statistics of one minimization of the boolean sequence
    alternatives of a concurrent property's `scope` operand: the `solver`
    backend, the number of queries it answered, and the alternative and
    literal counts before and after minimization.
  }];
  let parameters = (ins EnumParameter<SimBooleanMinimizationScope>:$scope,
      StringRefParameter<>:$solver, "uint64_t":$queries,
      "uint64_t":$alternativesBefore, "uint64_t":$alternativesAfter,
      "uint64_t":$literalsBefore, "uint64_t":$literalsAfter);
  let assemblyFormat = [{
    `<` $scope `,` `solver` `=` $solver `,` `queries` `=` $queries `,`
    `alternatives` `=` $alternativesBefore `->` $alternativesAfter `,`
    `literals` `=` $literalsBefore `->` $literalsAfter `>`
  }];
}

// Randomization plans frozen onto `randomize()` calls (IEEE 1800-2023 18.5.9,
// 18.6.2, 18.8). They are compile-time hand-offs from preparation to unit
// lowering and vanish with the semantic call.

def SimRandomObjectStepAttr
    : SimAttr<"RandomObjectStep", "random_object_step"> {
  let summary = "one class-handle field on a path to a nested random object";
  let description = [{
    Loads `field`, declared with class-handle type `storage`, from the
    previous object on the path. An intermediate step casts the loaded
    handle to its `concrete` class; a step that controls rand_mode() also
    names the field's effective `modeIndex` in its owner.
  }];
  let parameters = (ins "::mlir::FlatSymbolRefAttr":$field,
      "::obelisk::sim::ClassHandleType":$storage,
      OptionalParameter<"::obelisk::sim::ClassHandleType">:$concrete,
      OptionalParameter<"std::optional<uint32_t>">:$modeIndex);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimRandomNestedObjectAttr
    : SimAttr<"RandomNestedObject", "random_nested_object"> {
  let summary = "a random object reached through rand class-handle fields";
  let description = [{
    The receiver's `field`, with class-handle type `storage` and constant
    `concrete` class, followed by the `#simulation.random_object_step`s of
    `path`. `outerModeIndex` is the receiver's rand_mode() index of `field`.
  }];
  let parameters = (ins "::mlir::FlatSymbolRefAttr":$field,
      "::obelisk::sim::ClassHandleType":$concrete,
      "::obelisk::sim::ClassHandleType":$storage, "uint32_t":$outerModeIndex,
      OptionalParameter<"::mlir::ArrayAttr">:$path);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimRandomizeHookAttr : SimAttr<"RandomizeHook", "randomize_hook"> {
  let summary = "a pre_randomize() or post_randomize() call";
  let description = [{
    Calls method `callee` of class `owner`, lowered from semantic subroutine
    `source`. Once captures are frozen, `captures` lists the unit-local paths
    passed as arguments and `reads` the paths the hook reads.
  }];
  let parameters = (ins "::mlir::FlatSymbolRefAttr":$callee,
      "::mlir::FlatSymbolRefAttr":$owner, "::mlir::FlatSymbolRefAttr":$source,
      OptionalParameter<"::mlir::ArrayAttr">:$captures,
      OptionalParameter<"::mlir::ArrayAttr">:$reads);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimRandomNestedHooksAttr
    : SimAttr<"RandomNestedHooks", "random_nested_hooks"> {
  let summary = "randomization hooks of a nested random object";
  let parameters = (ins "::obelisk::sim::RandomNestedObjectAttr":$object,
      OptionalParameter<"::obelisk::sim::RandomizeHookAttr">:$pre,
      OptionalParameter<"::obelisk::sim::RandomizeHookAttr">:$post);
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimRandomNestedConstraintsAttr
    : SimAttr<"RandomNestedConstraints", "random_nested_constraints"> {
  let summary = "constraint blocks contributed by a nested random object";
  let description = [{
    `globalIndices` are the positions of the nested object's constraint
    blocks in the randomize() call's constraint set.
  }];
  let parameters = (ins "::obelisk::sim::RandomNestedObjectAttr":$object,
      "::mlir::DenseI64ArrayAttr":$globalIndices);
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimRandomAliasGuardAttr
    : SimAttr<"RandomAliasGuard", "random_alias_guard"> {
  let summary = "a recursive random-object path that may alias an ancestor";
  let description = [{
    Randomization fails at run time when the object at the end of `object`'s
    path is the object `aliasDepth` steps along it.
  }];
  let parameters = (ins "::obelisk::sim::RandomNestedObjectAttr":$object,
      "uint32_t":$aliasDepth);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimRandomNestedSelectionAttr
    : SimAttr<"RandomNestedSelection", "random_nested_selection"> {
  let summary = "the dynamic class a nested randomize() alternative handles";
  let description = [{
    Selects the alternative of a nested randomize() dispatch whose target,
    reached through `field` or the field symbols of `path`, is an instance of
    class `target`; without `target` the alternative handles a null object.
  }];
  let parameters = (ins "::mlir::FlatSymbolRefAttr":$field,
      OptionalParameter<"::mlir::ArrayAttr">:$path,
      OptionalParameter<"::mlir::FlatSymbolRefAttr">:$target);
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimRandomPatternAttr : SimAttr<"RandomPattern", "random_pattern"> {
  let summary = "a ternary pattern of a finite random domain";
  let description = [{
    Matches the values whose bits under `mask` equal `value`; the other bits
    are free.
  }];
  let parameters = (ins "::mlir::IntegerAttr":$mask,
      "::mlir::IntegerAttr":$value);
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimRandomDomainAttr : SimAttr<"RandomDomain", "random_domain"> {
  let summary = "the finite value domain of a bit range of a random property";
  let description = [{
    Bits `offset` to `offset + width - 1` of the property take only values
    matched by one of the disjoint `#simulation.random_pattern`s of
    `patterns` (an enum type's values, IEEE 1800-2023 18.4).
  }];
  let parameters = (ins "uint64_t":$offset, "uint64_t":$width,
      "::mlir::ArrayAttr":$patterns);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimRandomCycleStateAttr
    : SimAttr<"RandomCycleState", "random_cycle_state"> {
  let summary = "the permutation state of a randc variable";
  let description = [{
    The permutation `key`, the `position` in the current cycle, and the cycle
    `domain` of a randc variable (IEEE 1800-2023 18.4.2). Instance state
    names three class fields; static state names three storage paths.
  }];
  let parameters = (ins "::mlir::Attribute":$key,
      "::mlir::Attribute":$position, "::mlir::Attribute":$domain);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
  let extraClassDeclaration = [{
    /// Whether the state lives in class fields rather than static storage.
    bool isInstanceState() const {
      return ::mlir::isa<::mlir::FlatSymbolRefAttr>(getKey());
    }
  }];
}

def SimRandomContainerSizeAttr
    : SimAttr<"RandomContainerSize", "random_container_size"> {
  let summary = "a random dynamic container whose size is randomized";
  let description = [{
    The property is the size of a container of type `type`.
    `sizeConstraintMask` selects the constraint blocks that constrain the
    size; `unconditional` is set when one of them always applies.
  }];
  let parameters = (ins "::mlir::Type":$type, "uint64_t":$sizeConstraintMask,
      "bool":$unconditional);
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimRandomMemberStorageAttr
    : SimAttr<"RandomMemberStorage", "random_member_storage"> {
  let summary = "a random member of an unpacked structure property";
  let description = [{
    The member at the element ordinals `path` of the property's storage of
    type `type`. The member is enabled when every rand_mode() bit of
    `parentModeMask` is enabled and, for static properties, the bits of
    `staticModeMask`.
  }];
  let parameters = (ins "::mlir::DenseI64ArrayAttr":$path, "::mlir::Type":$type,
      "uint64_t":$parentModeMask, "uint64_t":$staticModeMask);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimRandomPropertyAttr : SimAttr<"RandomProperty", "random_property"> {
  let summary = "one random variable of a randomize() call";
  let description = [{
    A packed random variable of type `type` and `width` bits: the receiver's
    class `field`, or the unit-local storage `path` (named by `symbol` for a
    scope randomize()). `modeIndex` is its rand_mode() index; static
    properties keep their rand_mode() bit in storage `modeStorage`.

    The variable may be the size of a random dynamic container
    (`containerSize`), a property of a nested random object (`nested`, whose
    `field` names the property within that object), or a member of an
    unpacked structure (`member`). Fixed arrays carry per-element rand_mode()
    bits in `elementModeField` or `elementModeStorage`, `elementWidth` bits
    per element. randc variables keep their `cycle` state, and enum values
    restrict the variable to `domains`.
  }];
  let parameters = (ins OptionalParameter<"::mlir::FlatSymbolRefAttr">:$field,
      OptionalParameter<"::mlir::StringAttr">:$path,
      OptionalParameter<"::mlir::SymbolRefAttr">:$symbol, "::mlir::Type":$type,
      "uint64_t":$width, "uint32_t":$modeIndex, "bool":$isSigned,
      "bool":$isRandC,
      OptionalParameter<"std::optional<uint64_t>">:$modeStorage,
      OptionalParameter<"::obelisk::sim::RandomContainerSizeAttr">:
          $containerSize,
      OptionalParameter<"::obelisk::sim::RandomNestedObjectAttr">:$nested,
      OptionalParameter<"::obelisk::sim::RandomMemberStorageAttr">:$member,
      OptionalParameter<"::mlir::FlatSymbolRefAttr">:$elementModeField,
      OptionalParameter<"std::optional<uint64_t>">:$elementModeStorage,
      OptionalParameter<"std::optional<uint32_t>">:$elementWidth,
      OptionalParameter<"::obelisk::sim::RandomCycleStateAttr">:$cycle,
      OptionalParameter<"::mlir::ArrayAttr">:$domains);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
  let extraClassDeclaration = [{
    /// This property with its unit-local storage at `path` instead.
    RandomPropertyAttr withPath(::mlir::StringAttr path) const;
  }];
}

def SimRandomContainerAttr : SimAttr<"RandomContainer", "random_container"> {
  let summary = "a random dynamic array or queue of a randomize() call";
  let description = [{
    The receiver's class `field` of container type `type`, whose elements of
    `elementType` are randomized as `elementWidth`-bit values; with
    `inertClassHandles`, its class-handle elements are not randomized.
    `nested` places the field in a nested random object.
  }];
  let parameters = (ins "::mlir::FlatSymbolRefAttr":$field, "::mlir::Type":$type,
      "::mlir::Type":$elementType, "uint32_t":$elementWidth,
      "uint32_t":$modeIndex,
      DefaultValuedParameter<"bool", "false">:$inertClassHandles,
      OptionalParameter<"::obelisk::sim::RandomNestedObjectAttr">:$nested);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimRandomStructMemberAttr
    : SimAttr<"RandomStructMember", "random_struct_member"> {
  let summary = "a rand or randc member of an unpacked structure property";
  let description = [{
    The member at the element ordinals `path`, of source type `semanticType`
    and executable type `type`, with rand_mode() index `modeIndex`. A randc
    member keeps its `cycle` state; a fixed-array member keeps per-element
    rand_mode() bits in `elementModeField` or `elementModeStorage`.
  }];
  let parameters = (ins "::mlir::DenseI64ArrayAttr":$path,
      "::mlir::Type":$semanticType, "::mlir::Type":$type, "bool":$isRandC,
      "uint32_t":$modeIndex,
      OptionalParameter<"::obelisk::sim::RandomCycleStateAttr">:$cycle,
      OptionalParameter<"::mlir::FlatSymbolRefAttr">:$elementModeField,
      OptionalParameter<"std::optional<uint64_t>">:$elementModeStorage);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// Call contracts frozen onto semantic calls by preparation.

def SimCalleeFormalAttr : SimAttr<"CalleeFormal", "callee_formal"> {
  let summary = "one formal argument of a call's frozen callee signature";
  let description = [{
    The formal's `direction`, normalized `type` and signedness. `argumentRef`
    marks a ref formal of a task whose actuals are passed as argument
    references; `isStatic` a formal of a static subroutine, which lives in
    design storage. Formals of DPI imports keep their source `semanticType`
    for the C ABI.
  }];
  let parameters = (ins EnumParameter<SimFormalDirection>:$direction,
      "::mlir::Type":$type, "bool":$isSigned, "bool":$argumentRef,
      "bool":$isStatic, OptionalParameter<"::mlir::Type">:$semanticType);
  let assemblyFormat = "`<` struct(params) `>`";
}

def SimVirtualInterfaceCalleeAttr
    : SimAttr<"VirtualInterfaceCallee", "virtual_interface_callee"> {
  let summary = "one candidate of a call through a virtual interface";
  let description = [{
    Calls `callee` when the virtual interface selects the instance with
    scope `scope`. `captures` lists the unit-local paths passed as arguments,
    `reads` and `writes` the paths the callee reads and writes.
  }];
  let parameters = (ins "uint64_t":$scope, "::mlir::FlatSymbolRefAttr":$callee,
      "::mlir::ArrayAttr":$captures, "::mlir::ArrayAttr":$reads,
      "::mlir::ArrayAttr":$writes);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

// Symbolic attributes for closed executable classifications.
class SimEnumAttr<EnumAttrInfo info, string mnemonic>
    : EnumAttr<ObeliskSimulationDialect, info, mnemonic> {
  let assemblyFormat = "`<` $value `>`";
}
def SimElementKindAttr
    : SimEnumAttr<SimElementKind, "element_kind">;
def SimElementFlagsAttr
    : SimEnumAttr<SimElementFlags, "element_flags">;
def SimContainerKindAttr
    : SimEnumAttr<SimContainerKind, "container_kind">;
def SimAssocKeyKindAttr
    : SimEnumAttr<SimAssocKeyKind, "assoc_key_kind">;
def SimRandomDistributionAttr
    : SimEnumAttr<SimRandomDistribution, "random_distribution">;
def SimStochasticQueueActionAttr
    : SimEnumAttr<SimStochasticQueueAction, "stochastic_queue_action">;
def SimAssertionControlActionAttr
    : SimEnumAttr<SimAssertionControlAction, "assertion_control_action">;
def SimBlockEventKindAttr
    : SimEnumAttr<SimBlockEventKind, "block_event_kind">;
def SimRadixAttr : SimEnumAttr<SimRadix, "radix">;
def SimAssocTraversalDirectionAttr
    : SimEnumAttr<SimAssocTraversalDirection, "assoc_traversal_direction">;
def SimMemberVisibilityAttr
    : SimEnumAttr<SimMemberVisibility, "member_visibility">;

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONATTRS_TD
