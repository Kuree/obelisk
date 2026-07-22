//===- SlangTypes.td - Elaborated SystemVerilog types --------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SLANG_SLANGTYPES_TD
#define OBELISK_DIALECT_SLANG_SLANGTYPES_TD

include "obelisk/Dialect/Slang/SlangDialect.td"
include "obelisk/Dialect/Slang/SlangEnums.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/OpBase.td"

class SlangType<string name> : TypeDef<SlangDialect, name> {}

def SlangIntegralType : SlangType<"Integral"> {
  let mnemonic = "integral";
  let summary = "range- and signedness-preserving two- or four-state integral";
  let parameters = (ins
    "unsigned":$width,
    "bool":$isSigned,
    "bool":$isFourState,
    "int64_t":$left,
    "int64_t":$right,
    EnumParameter<SlangIntegralFlavor>:$flavor
  );
  let assemblyFormat =
      "`<` $width `,` $isSigned `,` $isFourState `,` $left `:` $right `,` $flavor `>`";
  let genVerifyDecl = 1;
}

def SlangStringType : SlangType<"String"> {
  let mnemonic = "string";
  let summary = "SystemVerilog dynamically sized string";
}

def SlangRealType : SlangType<"Real"> {
  let mnemonic = "real";
  let summary = "SystemVerilog IEEE double precision real";
}

def SlangRealtimeType : SlangType<"Realtime"> {
  let mnemonic = "realtime";
  let summary = "SystemVerilog realtime type";
}

def SlangShortRealType : SlangType<"ShortReal"> {
  let mnemonic = "shortreal";
  let summary = "SystemVerilog IEEE single precision shortreal";
}

def SlangVoidType : SlangType<"Void"> {
  let mnemonic = "void";
}

def SlangTimeType : SlangType<"Time"> {
  let mnemonic = "time";
}

def SlangNullType : SlangType<"Null"> {
  let mnemonic = "null";
}

def SlangChandleType : SlangType<"Chandle"> {
  let mnemonic = "chandle";
}

def SlangEventType : SlangType<"Event"> {
  let mnemonic = "event";
}

def SlangUnboundedType : SlangType<"Unbounded"> {
  let mnemonic = "unbounded";
}

def SlangUntypedType : SlangType<"Untyped"> {
  let mnemonic = "untyped";
}

def SlangTypeReferenceType : SlangType<"TypeReference"> {
  let mnemonic = "type_reference";
}

def SlangSequenceType : SlangType<"Sequence"> {
  let mnemonic = "sequence";
}

def SlangPropertyType : SlangType<"Property"> {
  let mnemonic = "property";
}

def SlangErrorType : SlangType<"Error"> {
  let mnemonic = "error";
  let parameters = (ins "bool":$invalid);
  let assemblyFormat = "`<` $invalid `>`";
  let genVerifyDecl = 1;
}

def SlangEnumType : SlangType<"Enum"> {
  let mnemonic = "enum";
  let parameters = (ins
    "::mlir::StringAttr":$name,
    "::mlir::Type":$baseType
  );
  let assemblyFormat = "`<` $name `,` $baseType `>`";
  let genVerifyDecl = 1;
}

def SlangPackedArrayType : SlangType<"PackedArray"> {
  let mnemonic = "packed_array";
  let parameters = (ins
    "::mlir::Type":$elementType,
    "int64_t":$left,
    "int64_t":$right
  );
  let assemblyFormat = "`<` $left `:` $right `x` $elementType `>`";
  let genVerifyDecl = 1;
}

def SlangUnpackedArrayType : SlangType<"UnpackedArray"> {
  let mnemonic = "unpacked_array";
  let parameters = (ins
    "::mlir::Type":$elementType,
    "int64_t":$left,
    "int64_t":$right
  );
  let assemblyFormat = "`<` $left `:` $right `x` $elementType `>`";
}

def SlangDynamicArrayType : SlangType<"DynamicArray"> {
  let mnemonic = "dynamic_array";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def SlangOpenArrayType : SlangType<"OpenArray"> {
  let mnemonic = "open_array";
  let parameters = (ins "::mlir::Type":$elementType, "bool":$isPacked);
  let assemblyFormat = "`<` $elementType `,` $isPacked `>`";
}

def SlangAssociativeArrayType : SlangType<"AssociativeArray"> {
  let mnemonic = "associative_array";
  let parameters = (ins
    "::mlir::Type":$elementType,
    "::mlir::Type":$indexType,
    "bool":$wildcardIndex
  );
  let assemblyFormat =
      "`<` $indexType `,` $elementType `,` $wildcardIndex `>`";
  let genVerifyDecl = 1;
}

def SlangQueueType : SlangType<"Queue"> {
  let mnemonic = "queue";
  let parameters = (ins "::mlir::Type":$elementType, "uint32_t":$bound);
  let assemblyFormat = "`<` $elementType `,` $bound `>`";
}

def SlangAggregateType : SlangType<"Aggregate"> {
  let mnemonic = "aggregate";
  let summary = "named packed or unpacked struct or union";
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
    "uint32_t":$tagBits,
    "::mlir::ArrayAttr":$fields
  );
  let assemblyFormat =
      "`<` $name `,` $isPacked `,` $isUnion `,` $isTagged `,` $isSigned `,` $isFourState `,` $isSoft `,` $bitWidth `,` $selectableWidth `,` $bitstreamWidth `,` $tagBits `,` $fields `>`";
  let genVerifyDecl = 1;
}

def SlangClassHandleType : SlangType<"ClassHandle"> {
  let mnemonic = "class_handle";
  let parameters = (ins "::mlir::SymbolRefAttr":$className);
  let assemblyFormat = "`<` $className `>`";
}

def SlangCovergroupHandleType : SlangType<"CovergroupHandle"> {
  let mnemonic = "covergroup_handle";
  let parameters = (ins "::mlir::SymbolRefAttr":$covergroupName);
  let assemblyFormat = "`<` $covergroupName `>`";
}

def SlangVirtualInterfaceType : SlangType<"VirtualInterface"> {
  let mnemonic = "virtual_interface";
  let parameters = (ins
    "::mlir::SymbolRefAttr":$interfaceName,
    "::mlir::StringAttr":$modport
  );
  let assemblyFormat = "`<` $interfaceName `,` $modport `>`";
}

def SlangSubroutineType : SlangType<"Subroutine"> {
  let mnemonic = "subroutine";
  let parameters = (ins "::mlir::Type":$signature, "bool":$isTask);
  let assemblyFormat = "`<` $signature `,` $isTask `>`";
  let genVerifyDecl = 1;
}

def SlangSourceRangeType : SlangType<"SourceRange"> {
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

#endif // OBELISK_DIALECT_SLANG_SLANGTYPES_TD
