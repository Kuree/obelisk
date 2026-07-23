//===- SimulationTypes.td - Executable simulation types -------*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_TD

include "obelisk/Dialect/Simulation/SimulationDialect.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/MemorySlotInterfaces.td"

class SimType<string name, list<Trait> traits = []>
    : TypeDef<ObeliskSimulationDialect, name, traits> {}

def SimLogicType : SimType<"Logic"> {
  let mnemonic = "logic";
  let summary = "fixed-width exact four-state packed value";
  let parameters = (ins "unsigned":$width);
  let assemblyFormat = "`<` $width `>`";
  let genVerifyDecl = 1;
}

def SimLogic1Type : ConfinedType<SimLogicType, [
  CPred<"::llvm::cast<::obelisk::sim::LogicType>($_self).getWidth() == 1">
], "one-bit exact four-state logic", "::obelisk::sim::LogicType">;

def SimContextType : SimType<"Context"> {
  let mnemonic = "context";
  let summary = "opaque simulator and design context";
}

def SimBytesType : SimType<"Bytes"> {
  let mnemonic = "bytes";
  let summary = "immutable literal byte sequence";
  let description = [{
    This type is intentionally limited to compile-time byte strings. Dynamic
    SystemVerilog strings remain in the semantic dialect until their ownership
    and mutation model is implemented.
  }];
}

def SimTimeType : SimType<"Time"> {
  let mnemonic = "time";
  let summary = "target-independent scaled simulation time";
}

class SimFixedArrayType<string name, string typeMnemonic>
    : SimType<name, [
        DeclareTypeInterfaceMethods<DestructurableTypeInterface>
      ]> {
  let mnemonic = typeMnemonic;
  let parameters = (ins
    "::mlir::Type":$elementType,
    "int64_t":$left,
    "int64_t":$right
  );
  let assemblyFormat = "`<` $left `:` $right `x` $elementType `>`";
  let genVerifyDecl = 1;
}

def SimPackedArrayType
    : SimFixedArrayType<"PackedArray", "packed_array"> {
  let summary = "fixed-size packed array with its declared source range";
}

def SimUnpackedArrayType
    : SimFixedArrayType<"UnpackedArray", "unpacked_array"> {
  let summary = "fixed-size unpacked array with its declared source range";
}

class SimRecordType<string name, string typeMnemonic>
    : SimType<name, [
        DeclareTypeInterfaceMethods<DestructurableTypeInterface>
      ]> {
  let mnemonic = typeMnemonic;
  let parameters = (ins "::mlir::ArrayAttr":$fields);
  let assemblyFormat = "`<` $fields `>`";
  let genVerifyDecl = 1;
}

def SimPackedStructType
    : SimRecordType<"PackedStruct", "packed_struct"> {
  let summary = "ordered fixed packed struct";
}

def SimUnpackedStructType
    : SimRecordType<"UnpackedStruct", "unpacked_struct"> {
  let summary = "ordered fixed unpacked struct";
}

class SimUnionType<string name, string typeMnemonic>
    : SimType<name, [
        DeclareTypeInterfaceMethods<DestructurableTypeInterface>
      ]> {
  let mnemonic = typeMnemonic;
  let parameters = (ins
    "::mlir::ArrayAttr":$fields,
    "bool":$isTagged,
    DefaultValuedParameter<"uint32_t", "0">:$tagBits
  );
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SimPackedUnionType : SimUnionType<"PackedUnion", "packed_union"> {
  let summary = "ordered fixed packed union";
}

def SimUnpackedUnionType
    : SimUnionType<"UnpackedUnion", "unpacked_union"> {
  let summary = "ordered fixed unpacked union";
}

def SimRefType : SimType<"Ref"> {
  let mnemonic = "ref";
  let summary = "typed mutable storage handle";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
  let genVerifyDecl = 1;
}

def SimNetType : SimType<"Net"> {
  let mnemonic = "net";
  let summary = "typed resolved net handle";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
  let genVerifyDecl = 1;
}

def SimDriverType : SimType<"Driver"> {
  let mnemonic = "driver";
  let summary = "typed static net-driver handle";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
  let genVerifyDecl = 1;
}

def SimEventType : SimType<"Event"> {
  let mnemonic = "event";
  let summary = "opaque scheduler event handle";
}

def SimProcessType : SimType<"Process"> {
  let mnemonic = "process";
  let summary = "opaque scheduler process handle";
}

def SimControlType : SimType<"Control"> {
  let mnemonic = "control";
  let summary = "opaque dynamic named-control activation token";
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_TD
