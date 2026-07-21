//===- ObeliskTypes.td - Obelisk simulation types ----------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SIM_OBELISKTYPES_TD
#define OBELISK_DIALECT_SIM_OBELISKTYPES_TD

include "obelisk/Dialect/Sim/ObeliskDialect.td"
include "obelisk/Dialect/Sim/ObeliskEnums.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/OpBase.td"

class ObeliskType<string name> : TypeDef<ObeliskDialect, name> {}

// Source-semantic integral data keeps the declared direction and signedness in
// addition to width and four-state domain. LogicType remains the normalized
// runtime representation used by lower-level operations.
def IntegralType : ObeliskType<"Integral"> {
  let mnemonic = "integral";
  let summary = "range-preserving elaborated integral type";
  let parameters = (ins
    "unsigned":$width,
    "bool":$isSigned,
    "bool":$isFourState,
    "int64_t":$left,
    "int64_t":$right,
    EnumParameter<ObeliskSVIntegralFlavor>:$flavor
  );
  let assemblyFormat =
      "`<` $width `,` $isSigned `,` $isFourState `,` $left `:` $right `,` $flavor `>`";
  let genVerifyDecl = 1;
}

def StringType : ObeliskType<"String"> {
  let mnemonic = "string";
  let summary = "source-semantic SystemVerilog string";
}

def RealType : ObeliskType<"Real"> {
  let mnemonic = "real";
  let summary = "source-semantic SystemVerilog real";
}

def RealtimeType : ObeliskType<"Realtime"> {
  let mnemonic = "realtime";
  let summary = "source-semantic SystemVerilog realtime";
}

def ShortRealType : ObeliskType<"ShortReal"> {
  let mnemonic = "shortreal";
  let summary = "source-semantic SystemVerilog shortreal";
}

def EnumType : ObeliskType<"Enum"> {
  let mnemonic = "enum";
  let parameters = (ins
    "::mlir::StringAttr":$name,
    "::mlir::Type":$baseType
  );
  let assemblyFormat = "`<` $name `,` $baseType `>`";
  let genVerifyDecl = 1;
}

def RangedPackedArrayType : ObeliskType<"RangedPackedArray"> {
  let mnemonic = "ranged_packed_array";
  let parameters = (ins
    "::mlir::Type":$elementType,
    "int64_t":$left,
    "int64_t":$right
  );
  let assemblyFormat = "`<` $left `:` $right `x` $elementType `>`";
  let genVerifyDecl = 1;
}

def RangedUnpackedArrayType : ObeliskType<"RangedUnpackedArray"> {
  let mnemonic = "ranged_unpacked_array";
  let parameters = (ins
    "::mlir::Type":$elementType,
    "int64_t":$left,
    "int64_t":$right
  );
  let assemblyFormat = "`<` $left `:` $right `x` $elementType `>`";
}

def QueueType : ObeliskType<"Queue"> {
  let mnemonic = "queue";
  let parameters = (ins "::mlir::Type":$elementType, "uint32_t":$bound);
  let assemblyFormat = "`<` $elementType `,` $bound `>`";
}

def SourceAggregateType : ObeliskType<"SourceAggregate"> {
  let mnemonic = "source_aggregate";
  let parameters = (ins
    "::mlir::StringAttr":$name,
    "bool":$isPacked,
    "bool":$isUnion,
    "bool":$isTagged,
    "bool":$isSigned,
    "bool":$isFourState,
    "bool":$isSoft,
    "uint64_t":$bitWidth,
    "uint64_t":$selectableWidth,
    "uint64_t":$bitstreamWidth,
    "uint32_t":$tagBits
  );
  let assemblyFormat =
      "`<` $name `,` $isPacked `,` $isUnion `,` $isTagged `,` $isSigned `,` $isFourState `,` $isSoft `,` $bitWidth `,` $selectableWidth `,` $bitstreamWidth `,` $tagBits `>`";
  let genVerifyDecl = 1;
}

def CovergroupHandleType : ObeliskType<"CovergroupHandle"> {
  let mnemonic = "covergroup_handle";
  let parameters = (ins "::mlir::SymbolRefAttr":$covergroupName);
  let assemblyFormat = "`<` $covergroupName `>`";
}

def VirtualInterfaceType : ObeliskType<"VirtualInterface"> {
  let mnemonic = "virtual_interface";
  let parameters = (ins
    "::mlir::SymbolRefAttr":$interfaceName,
    "::mlir::StringAttr":$modport
  );
  let assemblyFormat = "`<` $interfaceName `,` $modport `>`";
}

def SubroutineType : ObeliskType<"Subroutine"> {
  let mnemonic = "subroutine";
  let parameters = (ins "::mlir::Type":$signature, "bool":$isTask);
  let assemblyFormat = "`<` $signature `,` $isTask `>`";
  let genVerifyDecl = 1;
}

def SourceRangeType : ObeliskType<"SourceRange"> {
  let mnemonic = "source_range";
  let parameters = (ins
    "::mlir::StringAttr":$startFile,
    "uint32_t":$startLine,
    "uint32_t":$startColumn,
    "::mlir::StringAttr":$endFile,
    "uint32_t":$endLine,
    "uint32_t":$endColumn,
    "::mlir::StringAttr":$macroName
  );
  let assemblyFormat = [{
    `<` $startFile `,` $startLine `,` $startColumn `,`
    $endFile `,` $endLine `,` $endColumn `,` $macroName `>`
  }];
  let genVerifyDecl = 1;
}

def UnboundedType : ObeliskType<"Unbounded"> {
  let mnemonic = "unbounded";
}

def UntypedType : ObeliskType<"Untyped"> {
  let mnemonic = "untyped";
}

def TypeReferenceType : ObeliskType<"TypeReference"> {
  let mnemonic = "type_reference";
}

def SequenceType : ObeliskType<"Sequence"> {
  let mnemonic = "sequence";
}

def PropertyType : ObeliskType<"Property"> {
  let mnemonic = "property";
}

def ErrorType : ObeliskType<"Error"> {
  let mnemonic = "error";
  let parameters = (ins "bool":$invalid);
  let assemblyFormat = "`<` $invalid `>`";
  let genVerifyDecl = 1;
}

// Exact four-state packed data.  The canonical lowering is two W-bit planes:
// `unknown=0` selects ordinary 0/1 from `value`; `unknown=1` selects X/Z from
// `value`.  This represents all four symbols without a third plane.
def LogicType : ObeliskType<"Logic"> {
  let mnemonic = "logic";
  let summary = "fixed-width exact four-state packed value";
  let parameters = (ins "unsigned":$width);
  let assemblyFormat = "`<` $width `>`";
  let genVerifyDecl = 1;
}

def Logic1Type : ConfinedType<LogicType, [
  CPred<"::llvm::cast<::obelisk::ir::LogicType>($_self).getWidth() == 1">
], "one-bit exact four-state logic", "::obelisk::ir::LogicType">;

def VoidType : ObeliskType<"Void"> {
  let mnemonic = "void";
  let summary = "source-level SystemVerilog void";
}

// Simulation time is unsigned and independent of host time_t.  The eventual
// runtime ABI uses a fixed-width representation selected by the lowering.
def TimeType : ObeliskType<"Time"> {
  let mnemonic = "time";
  let summary = "scaled simulation time";
}

// References have identity and aliasing.  They are deliberately distinct from
// MLIR's memref, which cannot express all SystemVerilog storage behavior.
def RefType : ObeliskType<"Ref"> {
  let mnemonic = "ref";
  let summary = "mutable variable or object-field reference";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

// Nets are resolved, potentially multi-driver resources rather than variables.
def NetType : ObeliskType<"Net"> {
  let mnemonic = "net";
  let summary = "resolved SystemVerilog net handle";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def ProcessType : ObeliskType<"Process"> {
  let mnemonic = "process";
  let summary = "scheduler-owned process handle";
}

def EventType : ObeliskType<"Event"> {
  let mnemonic = "event";
  let summary = "named event handle";
}

// Keep the declared class in the type until object layout and virtual dispatch
// have been resolved. Erasing this to a raw pointer during frontend conversion
// would make inherited field and method references ambiguous.
def ClassHandleType : ObeliskType<"ClassHandle"> {
  let mnemonic = "class_handle";
  let summary = "nullable statically typed SystemVerilog class handle";
  let parameters = (ins "::mlir::SymbolRefAttr":$className);
  let assemblyFormat = "`<` $className `>`";
}

def NullType : ObeliskType<"Null"> {
  let mnemonic = "null";
  let summary = "polymorphic SystemVerilog null handle literal";
}

def ObjectType : ObeliskType<"Object"> {
  let mnemonic = "object";
  let summary = "nullable dynamically-dispatched class object handle";
}

def BoxType : ObeliskType<"Box"> {
  let mnemonic = "any";
  let summary = "boxed runtime value for untyped containers and reflection";
}

def DynArrayType : ObeliskType<"DynArray"> {
  let mnemonic = "dynarray";
  let summary = "dynamic array with reference semantics";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

// Preserve packedness until ABI/layout lowering. Collapsing both forms to
// hw.array would make an unpacked array indistinguishable from a packed vector
// aggregate.
def PackedArrayType : ObeliskType<"PackedArray"> {
  let mnemonic = "packed_array";
  let summary = "fixed-size packed SystemVerilog array";
  let parameters = (ins
    "::mlir::Type":$elementType,
    "unsigned":$size
  );
  let assemblyFormat = "`<` $size `x` $elementType `>`";
  let genVerifyDecl = 1;
}

def UnpackedArrayType : ObeliskType<"UnpackedArray"> {
  let mnemonic = "unpacked_array";
  let summary = "fixed-size unpacked SystemVerilog array";
  let parameters = (ins
    "::mlir::Type":$elementType,
    "unsigned":$size
  );
  let assemblyFormat = "`<` $size `x` $elementType `>`";
}

// A DictionaryAttr maps each source field name to a TypeAttr. Keeping the field
// inventory in Obelisk avoids depending on a hardware-specific dialect and
// preserves source packedness and union semantics explicitly.
def PackedStructType : ObeliskType<"PackedStruct"> {
  let mnemonic = "packed_struct";
  let summary = "packed SystemVerilog struct";
  let parameters = (ins "::mlir::DictionaryAttr":$fields);
  let assemblyFormat = "`<` $fields `>`";
  let genVerifyDecl = 1;
}

def UnpackedStructType : ObeliskType<"UnpackedStruct"> {
  let mnemonic = "unpacked_struct";
  let summary = "unpacked SystemVerilog struct";
  let parameters = (ins "::mlir::DictionaryAttr":$fields);
  let assemblyFormat = "`<` $fields `>`";
  let genVerifyDecl = 1;
}

def PackedUnionType : ObeliskType<"PackedUnion"> {
  let mnemonic = "packed_union";
  let summary = "packed SystemVerilog union";
  let parameters = (ins "::mlir::DictionaryAttr":$fields);
  let assemblyFormat = "`<` $fields `>`";
  let genVerifyDecl = 1;
}

def UnpackedUnionType : ObeliskType<"UnpackedUnion"> {
  let mnemonic = "unpacked_union";
  let summary = "unpacked SystemVerilog union";
  let parameters = (ins "::mlir::DictionaryAttr":$fields);
  let assemblyFormat = "`<` $fields `>`";
  let genVerifyDecl = 1;
}

// An open array is a borrowed DPI view.  It neither owns storage nor has the
// resize/value semantics of a dynamic array, so it must remain a distinct type.
def OpenArrayType : ObeliskType<"OpenArray"> {
  let mnemonic = "open_array";
  let summary = "borrowed packed or unpacked open-array view";
  let parameters = (ins
    "::mlir::Type":$elementType,
    "bool":$isPacked
  );
  let assemblyFormat = "`<` $elementType `,` $isPacked `>`";
}

def AssocArrayType : ObeliskType<"AssocArray"> {
  let mnemonic = "assoc";
  let summary = "associative array with reference semantics";
  let parameters = (ins
    "::mlir::Type":$keyType,
    "::mlir::Type":$elementType,
    "bool":$wildcardIndex
  );
  let assemblyFormat = "`<` $keyType `,` $elementType `,` $wildcardIndex `>`";
  let genVerifyDecl = 1;
}

def ScanStateType : ObeliskType<"ScanState"> {
  let mnemonic = "scan_state";
  let summary = "state of an active fscanf or sscanf conversion";
}

def MailboxType : ObeliskType<"Mailbox"> {
  let mnemonic = "mailbox";
  let summary = "typed or boxed blocking mailbox";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def SemaphoreType : ObeliskType<"Semaphore"> {
  let mnemonic = "semaphore";
  let summary = "SystemVerilog semaphore handle";
}

def RandomStreamType : ObeliskType<"RandomStream"> {
  let mnemonic = "random_stream";
  let summary = "deterministic hierarchical pseudorandom stream";
}

def RandomStateType : ObeliskType<"RandomState"> {
  let mnemonic = "random_state";
  let summary = "saved random stream state";
}

def RandCStateType : ObeliskType<"RandCState"> {
  let mnemonic = "randc_state";
  let summary = "state for a random-cyclic variable";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def ConstraintSetType : ObeliskType<"ConstraintSet"> {
  let mnemonic = "constraint_set";
  let summary = "compiled random constraint graph handle";
}

def ChandleType : ObeliskType<"Chandle"> {
  let mnemonic = "chandle";
  let summary = "opaque DPI C pointer";
}

def VPIHandleType : ObeliskType<"VPIHandle"> {
  let mnemonic = "vpi_handle";
  let summary = "opaque VPI object handle";
}

#endif // OBELISK_DIALECT_SIM_OBELISKTYPES_TD
