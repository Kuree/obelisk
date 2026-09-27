//===- RuntimeAttrs.td - Symbolic runtime ABI attributes -----*- tablegen -*-===//
#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEATTRS_TD
#define OBELISK_DIALECT_RUNTIME_RUNTIMEATTRS_TD

include "obelisk/Dialect/Runtime/RuntimeDialect.td"
include "obelisk/Dialect/Runtime/RuntimeEnums.td"

// Preserve ABI enums as dialect attributes; integer encoding belongs to the
// final native-call or bytecode conversion.
class RTEnumAttr<EnumAttrInfo info, string mnemonic>
    : EnumAttr<ObeliskRuntimeDialect, info, mnemonic> {
  let assemblyFormat = "`<` $value `>`";
}
def RTDescriptorKindAttr : RTEnumAttr<RTDescriptorKind, "descriptor_kind">;
def RTActionKindAttr : RTEnumAttr<RTActionKind, "action_kind">;
def RTSuspensionKindAttr : RTEnumAttr<RTSuspensionKind, "suspension_kind">;
def RTRadixAttr : RTEnumAttr<RTRadix, "radix">;
def RTSeekOriginAttr : RTEnumAttr<RTSeekOrigin, "seek_origin">;
def RTCodeKindAttr : RTEnumAttr<RTCodeKind, "code_kind">;
def RTBytecodeValueKindAttr : RTEnumAttr<RTBytecodeValueKind, "bytecode_value_kind">;
def RTBytecodeOpcodeAttr : RTEnumAttr<RTBytecodeOpcode, "bytecode_opcode">;
def RTBytecodeOperandKindAttr : RTEnumAttr<RTBytecodeOperandKind, "bytecode_operand_kind">;
def RTBytecodeOperandDirectionAttr : RTEnumAttr<RTBytecodeOperandDirection, "bytecode_operand_direction">;
def RTBytecodeServiceValueKindAttr : RTEnumAttr<RTBytecodeServiceValueKind, "bytecode_service_value_kind">;
def RTBytecodeServiceAttr : RTEnumAttr<RTBytecodeService, "bytecode_service">;
def RTStatusCodeAttr : RTEnumAttr<RTStatusCode, "status_code">;
def RTArgumentKindAttr : RTEnumAttr<RTArgumentKind, "argument_kind">;
def RTArgumentFlagsAttr : RTEnumAttr<RTArgumentFlags, "argument_flags">;

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEATTRS_TD
