//===- RuntimeTypes.td - Typed runtime ABI concepts -----------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMETYPES_TD
#define OBELISK_DIALECT_RUNTIME_RUNTIMETYPES_TD

include "obelisk/Dialect/Runtime/RuntimeDialect.td"
include "mlir/IR/AttrTypeBase.td"

class RTType<string name, string mnemonicValue>
    : TypeDef<ObeliskRuntimeDialect, name> {
  let mnemonic = mnemonicValue;
}

class RTReferenceType<string name, string mnemonicValue>
    : RTType<name, mnemonicValue> {
  let summary = "typed reference to an in-tree runtime ABI object";
  let description = [{
    This is a compiler-visible reference type, not an inline copy of the named
    C record. Runtime-to-LLVM lowering maps it to the target-layout pointer
    type; construction of immutable referenced records is handled by later
    global/materialization operations.
  }];
}

def RTContextType : RTReferenceType<"Context", "context">;
def RTStatusType : RTType<"Status", "status">;
def RTCStringType : RTReferenceType<"CString", "cstring">;
def RTByteSpanType : RTType<"ByteSpan", "bytes">;
def RTMutableByteSpanType : RTType<"MutableByteSpan", "mut_bytes">;
def RTBufferType : RTType<"Buffer", "buffer">;
def RTFileDescriptorType : RTType<"FileDescriptor", "fd">;
def RTArgumentType : RTType<"Argument", "arg">;
def RTArgumentArrayType : RTType<"ArgumentArray", "args">;
def RTFormatEnvironmentType
    : RTReferenceType<"FormatEnvironment", "format_env">;
def RTHandleType : RTType<"Handle", "handle">;
def RTFragmentActionType : RTType<"FragmentAction", "action">;
def RTFragmentDescriptorType
    : RTReferenceType<"FragmentDescriptor", "fragment">;
def RTProcessDescriptorType
    : RTReferenceType<"ProcessDescriptor", "process_descriptor">;
def RTProcessInstanceType
    : RTReferenceType<"ProcessInstance", "process_instance">;
def RTBytecodeProgramType : RTReferenceType<"BytecodeProgram", "bytecode">;
def RTBytecodeEntryType : RTType<"BytecodeEntry", "bytecode_entry">;
def RTBytecodeValidationType
    : RTType<"BytecodeValidation", "bytecode_validation">;
def RTBytecodeOperandType
    : RTType<"BytecodeOperand", "bytecode_operand">;
def RTBytecodeServiceSiteType
    : RTType<"BytecodeServiceSite", "bytecode_service_site">;
def RTBytecodeOpcodeType : RTType<"BytecodeOpcode", "opcode">;
def RTBytecodeValueType : RTType<"BytecodeValue", "bytecode_type">;
def RTBytecodeOperandKindType
    : RTType<"BytecodeOperandKind", "bytecode_operand_kind">;
def RTBytecodeOperandDirectionType
    : RTType<"BytecodeOperandDirection", "bytecode_operand_direction">;
def RTBytecodeServiceValueType
    : RTType<"BytecodeServiceValue", "bytecode_service_value">;
def RTBytecodeServiceType
    : RTType<"BytecodeService", "bytecode_service">;

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMETYPES_TD
