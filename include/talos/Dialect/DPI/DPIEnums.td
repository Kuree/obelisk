//===- DPIEnums.td - DPI-C ABI enumerations ---------------*- tablegen -*-===//

#ifndef TALOS_DIALECT_DPI_DPIENUMS_TD
#define TALOS_DIALECT_DPI_DPIENUMS_TD

include "mlir/IR/EnumAttr.td"

def DPIABIBit : I32EnumAttrCase<"Bit", 0, "bit">;
def DPIABILogic : I32EnumAttrCase<"Logic", 1, "logic">;
def DPIABIByte : I32EnumAttrCase<"Byte", 2, "byte">;
def DPIABIShortInt : I32EnumAttrCase<"ShortInt", 3, "shortint">;
def DPIABIInt : I32EnumAttrCase<"Int", 4, "int">;
def DPIABILongInt : I32EnumAttrCase<"LongInt", 5, "longint">;
def DPIABIBitVector : I32EnumAttrCase<"BitVector", 6, "bit_vector">;
def DPIABILogicVector : I32EnumAttrCase<"LogicVector", 7, "logic_vector">;
def DPIABIString : I32EnumAttrCase<"String", 8, "string">;
def DPIABIChandle : I32EnumAttrCase<"Chandle", 9, "chandle">;
def DPIABIShortReal : I32EnumAttrCase<"ShortReal", 10, "shortreal">;
def DPIABIReal : I32EnumAttrCase<"Real", 11, "real">;
def DPIABIOpenArray : I32EnumAttrCase<"OpenArray", 12, "open_array">;
def DPIABIUnpackedAggregate
    : I32EnumAttrCase<"UnpackedAggregate", 13, "unpacked_aggregate">;
def DPIABIKind : I32EnumAttr<"ABIKind", "DPI-C value ABI category", [
    DPIABIBit, DPIABILogic, DPIABIByte, DPIABIShortInt, DPIABIInt,
    DPIABILongInt, DPIABIBitVector, DPIABILogicVector, DPIABIString,
    DPIABIChandle, DPIABIShortReal, DPIABIReal, DPIABIOpenArray,
    DPIABIUnpackedAggregate
  ]> {
  let cppNamespace = "::talos::dpi";
}

def DPIInput : I32EnumAttrCase<"Input", 0, "input">;
def DPIOutput : I32EnumAttrCase<"Output", 1, "output">;
def DPIInOut : I32EnumAttrCase<"InOut", 2, "inout">;
def DPIResult : I32EnumAttrCase<"Result", 3, "result">;
def DPIArgumentDirection
    : I32EnumAttr<"ArgumentDirection", "DPI-C ABI value role", [
        DPIInput, DPIOutput, DPIInOut, DPIResult
      ]> {
  let cppNamespace = "::talos::dpi";
}

#endif // TALOS_DIALECT_DPI_DPIENUMS_TD
