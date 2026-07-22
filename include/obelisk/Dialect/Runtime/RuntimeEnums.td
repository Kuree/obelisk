//===- RuntimeEnums.td - Runtime ABI enumerations ------------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEENUMS_TD
#define OBELISK_DIALECT_RUNTIME_RUNTIMEENUMS_TD

include "mlir/IR/EnumAttr.td"

def RTDescriptorInvalid : I32EnumAttrCase<"Invalid", 0, "invalid">;
def RTDescriptorScope : I32EnumAttrCase<"Scope", 1, "scope">;
def RTDescriptorStorage : I32EnumAttrCase<"Storage", 2, "storage">;
def RTDescriptorNet : I32EnumAttrCase<"Net", 3, "net">;
def RTDescriptorDriver : I32EnumAttrCase<"Driver", 4, "driver">;
def RTDescriptorEvent : I32EnumAttrCase<"Event", 5, "event">;
def RTDescriptorProcess : I32EnumAttrCase<"Process", 6, "process">;
def RTDescriptorFragment : I32EnumAttrCase<"Fragment", 7, "fragment">;
def RTDescriptorKind : I32EnumAttr<"DescriptorKind", "runtime descriptor kind", [
  RTDescriptorInvalid, RTDescriptorScope, RTDescriptorStorage, RTDescriptorNet,
  RTDescriptorDriver, RTDescriptorEvent, RTDescriptorProcess,
  RTDescriptorFragment
]> { let cppNamespace = "::obelisk::runtime"; }

def RTActionContinue : I32EnumAttrCase<"Continue", 0, "continue">;
def RTActionSuspend : I32EnumAttrCase<"Suspend", 1, "suspend">;
def RTActionTerminate : I32EnumAttrCase<"Terminate", 2, "terminate">;
def RTActionKind : I32EnumAttr<"ActionKind", "fragment action kind", [
  RTActionContinue, RTActionSuspend, RTActionTerminate
]> { let cppNamespace = "::obelisk::runtime"; }

def RTSuspendNone : I32EnumAttrCase<"None", 0, "none">;
def RTSuspendDelay : I32EnumAttrCase<"Delay", 1, "delay">;
def RTSuspendChange : I32EnumAttrCase<"Change", 2, "change">;
def RTSuspendEdge : I32EnumAttrCase<"Edge", 3, "edge">;
def RTSuspendEvent : I32EnumAttrCase<"Event", 4, "event">;
def RTSuspendAwait : I32EnumAttrCase<"Await", 5, "await">;
def RTSuspendJoin : I32EnumAttrCase<"Join", 6, "join">;
def RTSuspendFrontier : I32EnumAttrCase<"Frontier", 7, "frontier">;
def RTSuspensionKind : I32EnumAttr<"SuspensionKind", "fragment suspension kind", [
  RTSuspendNone, RTSuspendDelay, RTSuspendChange, RTSuspendEdge, RTSuspendEvent,
  RTSuspendAwait, RTSuspendJoin, RTSuspendFrontier
]> { let cppNamespace = "::obelisk::runtime"; }

def RTRadixBinary : I32EnumAttrCase<"Binary", 2, "binary">;
def RTRadixOctal : I32EnumAttrCase<"Octal", 8, "octal">;
def RTRadixDecimal : I32EnumAttrCase<"Decimal", 10, "decimal">;
def RTRadixHex : I32EnumAttrCase<"Hex", 16, "hex">;
def RTRadix : I32EnumAttr<"Radix", "default display radix", [
  RTRadixBinary, RTRadixOctal, RTRadixDecimal, RTRadixHex
]> { let cppNamespace = "::obelisk::runtime"; }

def RTSeekSet : I32EnumAttrCase<"Set", 0, "set">;
def RTSeekCurrent : I32EnumAttrCase<"Current", 1, "current">;
def RTSeekEnd : I32EnumAttrCase<"End", 2, "end">;
def RTSeekOrigin : I32EnumAttr<"SeekOrigin", "file seek origin", [
  RTSeekSet, RTSeekCurrent, RTSeekEnd
]> { let cppNamespace = "::obelisk::runtime"; }

def RTCodeNative : I32EnumAttrCase<"Native", 0, "native">;
def RTCodeBytecode : I32EnumAttrCase<"Bytecode", 1, "bytecode">;
def RTCodeKind : I32EnumAttr<"CodeKind", "fragment code representation", [
  RTCodeNative, RTCodeBytecode
]> { let cppNamespace = "::obelisk::runtime"; }

def RTBCTypeNone : I32EnumAttrCase<"None", 0, "none">;
def RTBCTypeU64 : I32EnumAttrCase<"U64", 1, "u64">;
def RTBCTypeI64 : I32EnumAttrCase<"I64", 2, "i64">;
def RTBCTypeBool : I32EnumAttrCase<"Bool", 3, "bool">;
def RTBCTypeStatus : I32EnumAttrCase<"Status", 4, "status">;
def RTBCTypeResource : I32EnumAttrCase<"Resource", 5, "resource">;
def RTBytecodeValueKind : I32EnumAttr<"BytecodeValueKind", "bytecode register type", [
  RTBCTypeNone, RTBCTypeU64, RTBCTypeI64, RTBCTypeBool, RTBCTypeStatus,
  RTBCTypeResource
]> { let cppNamespace = "::obelisk::runtime"; }

def RTBCNop : I32EnumAttrCase<"Nop", 0, "nop">;
def RTBCConst : I32EnumAttrCase<"Const", 1, "const">;
def RTBCMove : I32EnumAttrCase<"Move", 2, "move">;
def RTBCAdd : I32EnumAttrCase<"Add", 3, "add">;
def RTBCSub : I32EnumAttrCase<"Sub", 4, "sub">;
def RTBCMul : I32EnumAttrCase<"Mul", 5, "mul">;
def RTBCAnd : I32EnumAttrCase<"And", 6, "and">;
def RTBCOr : I32EnumAttrCase<"Or", 7, "or">;
def RTBCXor : I32EnumAttrCase<"Xor", 8, "xor">;
def RTBCNot : I32EnumAttrCase<"Not", 9, "not">;
def RTBCEq : I32EnumAttrCase<"Eq", 10, "eq">;
def RTBCUlt : I32EnumAttrCase<"Ult", 11, "ult">;
def RTBCSlt : I32EnumAttrCase<"Slt", 12, "slt">;
def RTBCLoadFrame : I32EnumAttrCase<"LoadFrame", 13, "load_frame">;
def RTBCStoreFrame : I32EnumAttrCase<"StoreFrame", 14, "store_frame">;
def RTBCJump : I32EnumAttrCase<"Jump", 15, "jump">;
def RTBCBranchZero : I32EnumAttrCase<"BranchZero", 16, "branch_zero">;
def RTBCContinue : I32EnumAttrCase<"Continue", 17, "continue">;
def RTBCSuspend : I32EnumAttrCase<"Suspend", 18, "suspend">;
def RTBCTerminate : I32EnumAttrCase<"Terminate", 19, "terminate">;
def RTBCCallService : I32EnumAttrCase<"CallService", 20, "call_service">;
def RTBCFail : I32EnumAttrCase<"Fail", 21, "fail">;
def RTBytecodeOpcode : I32EnumAttr<"BytecodeOpcode", "bytecode opcode", [
  RTBCNop, RTBCConst, RTBCMove, RTBCAdd, RTBCSub, RTBCMul, RTBCAnd, RTBCOr,
  RTBCXor, RTBCNot, RTBCEq, RTBCUlt, RTBCSlt, RTBCLoadFrame, RTBCStoreFrame,
  RTBCJump, RTBCBranchZero, RTBCContinue, RTBCSuspend, RTBCTerminate,
  RTBCCallService, RTBCFail
]> { let cppNamespace = "::obelisk::runtime"; }

def RTBCOperandImmediate : I32EnumAttrCase<"Immediate", 0, "immediate">;
def RTBCOperandRegister : I32EnumAttrCase<"Register", 1, "register">;
def RTBCOperandFrame : I32EnumAttrCase<"Frame", 2, "frame">;
def RTBCOperandConstant : I32EnumAttrCase<"Constant", 3, "constant">;
def RTBCOperandResource : I32EnumAttrCase<"Resource", 4, "resource">;
def RTBytecodeOperandKind : I32EnumAttr<
    "BytecodeOperandKind", "bytecode service operand location", [
  RTBCOperandImmediate, RTBCOperandRegister, RTBCOperandFrame,
  RTBCOperandConstant, RTBCOperandResource
]> { let cppNamespace = "::obelisk::runtime"; }

def RTBCOperandInput : I32EnumAttrCase<"Input", 0, "input">;
def RTBCOperandOutput : I32EnumAttrCase<"Output", 1, "output">;
def RTBCOperandInOut : I32EnumAttrCase<"InOut", 2, "inout">;
def RTBytecodeOperandDirection : I32EnumAttr<
    "BytecodeOperandDirection", "bytecode service operand direction", [
  RTBCOperandInput, RTBCOperandOutput, RTBCOperandInOut
]> { let cppNamespace = "::obelisk::runtime"; }

def RTBCServiceValueNone : I32EnumAttrCase<"None", 0, "none">;
def RTBCServiceValueU8 : I32EnumAttrCase<"U8", 1, "u8">;
def RTBCServiceValueU32 : I32EnumAttrCase<"U32", 2, "u32">;
def RTBCServiceValueI32 : I32EnumAttrCase<"I32", 3, "i32">;
def RTBCServiceValueU64 : I32EnumAttrCase<"U64", 4, "u64">;
def RTBCServiceValueI64 : I32EnumAttrCase<"I64", 5, "i64">;
def RTBCServiceValueBytes : I32EnumAttrCase<"Bytes", 6, "bytes">;
def RTBCServiceValueMutableBytes
    : I32EnumAttrCase<"MutableBytes", 7, "mutable_bytes">;
def RTBCServiceValueArgumentArray
    : I32EnumAttrCase<"ArgumentArray", 8, "argument_array">;
def RTBCServiceValueFormatEnvironment
    : I32EnumAttrCase<"FormatEnvironment", 9, "format_environment">;
def RTBCServiceValueBuffer : I32EnumAttrCase<"Buffer", 10, "buffer">;
def RTBCServiceValueArgumentEmpty
    : I32EnumAttrCase<"ArgumentEmpty", 11, "argument_empty">;
def RTBCServiceValueArgumentLogic
    : I32EnumAttrCase<"ArgumentLogic", 12, "argument_logic">;
def RTBCServiceValueArgumentString
    : I32EnumAttrCase<"ArgumentString", 13, "argument_string">;
def RTBCServiceValueArgumentReal
    : I32EnumAttrCase<"ArgumentReal", 14, "argument_real">;
def RTBCServiceValueArgumentTime
    : I32EnumAttrCase<"ArgumentTime", 15, "argument_time">;
def RTBytecodeServiceValueKind : I32EnumAttr<
    "BytecodeServiceValueKind", "bytecode service value kind", [
  RTBCServiceValueNone, RTBCServiceValueU8, RTBCServiceValueU32,
  RTBCServiceValueI32, RTBCServiceValueU64, RTBCServiceValueI64,
  RTBCServiceValueBytes, RTBCServiceValueMutableBytes,
  RTBCServiceValueArgumentArray, RTBCServiceValueFormatEnvironment,
  RTBCServiceValueBuffer, RTBCServiceValueArgumentEmpty,
  RTBCServiceValueArgumentLogic, RTBCServiceValueArgumentString,
  RTBCServiceValueArgumentReal, RTBCServiceValueArgumentTime
]> { let cppNamespace = "::obelisk::runtime"; }

def RTBCServiceFormat : I32EnumAttrCase<"Format", 1, "format">;
def RTBCServiceDisplay : I32EnumAttrCase<"Display", 2, "display">;
def RTBCServiceBufferRelease
    : I32EnumAttrCase<"BufferRelease", 3, "buffer_release">;
def RTBCServiceFileOpenMCD
    : I32EnumAttrCase<"FileOpenMCD", 10, "file_open_mcd">;
def RTBCServiceFileOpen : I32EnumAttrCase<"FileOpen", 11, "file_open">;
def RTBCServiceFileClose : I32EnumAttrCase<"FileClose", 12, "file_close">;
def RTBCServiceFileFlush : I32EnumAttrCase<"FileFlush", 13, "file_flush">;
def RTBCServiceFileWrite : I32EnumAttrCase<"FileWrite", 14, "file_write">;
def RTBCServiceFileRead : I32EnumAttrCase<"FileRead", 15, "file_read">;
def RTBCServiceFileGetc : I32EnumAttrCase<"FileGetc", 16, "file_getc">;
def RTBCServiceFileUngetc : I32EnumAttrCase<"FileUngetc", 17, "file_ungetc">;
def RTBCServiceFileGetline
    : I32EnumAttrCase<"FileGetline", 18, "file_getline">;
def RTBCServiceFileEof : I32EnumAttrCase<"FileEof", 19, "file_eof">;
def RTBCServiceFileError : I32EnumAttrCase<"FileError", 20, "file_error">;
def RTBCServiceFileSeek : I32EnumAttrCase<"FileSeek", 21, "file_seek">;
def RTBCServiceFileTell : I32EnumAttrCase<"FileTell", 22, "file_tell">;
def RTBCServiceFileRewind
    : I32EnumAttrCase<"FileRewind", 23, "file_rewind">;
def RTBytecodeService : I32EnumAttr<
    "BytecodeService", "bytecode runtime service", [
  RTBCServiceFormat, RTBCServiceDisplay, RTBCServiceBufferRelease,
  RTBCServiceFileOpenMCD, RTBCServiceFileOpen, RTBCServiceFileClose,
  RTBCServiceFileFlush, RTBCServiceFileWrite, RTBCServiceFileRead,
  RTBCServiceFileGetc, RTBCServiceFileUngetc, RTBCServiceFileGetline,
  RTBCServiceFileEof, RTBCServiceFileError, RTBCServiceFileSeek,
  RTBCServiceFileTell, RTBCServiceFileRewind
]> { let cppNamespace = "::obelisk::runtime"; }

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEENUMS_TD
