//===- RuntimeOps.td - Typed runtime ABI operations -----------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEOPS_TD
#define OBELISK_DIALECT_RUNTIME_RUNTIMEOPS_TD

include "obelisk/Dialect/Runtime/RuntimeAttrs.td"
include "obelisk/Dialect/Runtime/RuntimeDialect.td"
include "obelisk/Dialect/Runtime/RuntimeTypes.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def RTRuntimeResource : Resource<"::obelisk::runtime::RuntimeResource">;
def RTIOResource : Resource<"::obelisk::runtime::IOResource">;

class RTOp<string mnemonic, list<Trait> traits = []>
    : Op<ObeliskRuntimeDialect, mnemonic, traits> {
  let assemblyFormat = "operands attr-dict `:` functional-type(operands, results)";
}

// Compiler materializers. These operations make all target-layout-sensitive
// records and storage explicit before the LLVM boundary.
def RTBytesConstantOp : RTOp<"bytes.constant", [Pure, ConstantLike]> {
  let arguments = (ins StrAttr:$value);
  let results = (outs RTByteSpanType:$result);
  let assemblyFormat = "$value attr-dict";
  let hasFolder = 1;
}

def RTScratchOp : RTOp<"bytes.scratch", [MemoryEffects<[MemAlloc]>]> {
  let arguments = (ins I64Attr:$size);
  let results = (outs RTMutableByteSpanType:$result);
  let assemblyFormat = "$size attr-dict";
  let hasVerifier = 1;
}

def RTBytesSizeOp : RTOp<"bytes.size", [Pure]> {
  let arguments = (ins AnyType:$bytes);
  let results = (outs I64:$size);
  let hasVerifier = 1;
}

def RTPackedFromBytesOp : RTOp<"bytes.to_packed", [MemoryEffects<[MemRead]>]> {
  let arguments = (ins AnyType:$bytes, I64:$count,
                       BoolAttr:$high_alignment);
  let results = (outs AnyInteger:$result);
  let hasVerifier = 1;
}

def RTArgumentEmptyOp : RTOp<"argument.empty", [Pure]> {
  let results = (outs RTArgumentType:$result);
  let hasVerifier = 1;
}

def RTArgumentPackedOp : RTOp<"argument.packed", [MemoryEffects<[MemAlloc]>]> {
  let arguments = (ins AnyInteger:$value, Optional<AnyInteger>:$unknown,
                       BoolAttr:$is_signed);
  let results = (outs RTArgumentType:$result);
  let hasVerifier = 1;
}

def RTArgumentBytesOp : RTOp<"argument.bytes", [Pure]> {
  let arguments = (ins RTByteSpanType:$value,
                       BoolAttr:$is_format_string);
  let results = (outs RTArgumentType:$result);
  let hasVerifier = 1;
}

def RTArgumentArrayOp : RTOp<"argument.array", [MemoryEffects<[MemAlloc]>]> {
  let arguments = (ins Variadic<RTArgumentType>:$arguments);
  let results = (outs RTArgumentArrayType:$result);
  let hasVerifier = 1;
}

def RTFormatEnvironmentOp : RTOp<"format.environment", [
  MemoryEffects<[MemAlloc]>
]> {
  let arguments = (ins DefaultValuedAttr<StrAttr, "\"\"">:$scope,
                       DefaultValuedAttr<StrAttr, "\"\"">:$library_cell,
                       DefaultValuedAttr<I32Attr, "0">:$time_width,
                       DefaultValuedAttr<StrAttr, "\"\"">:$time_suffix,
                       DefaultValuedAttr<I64Attr, "1">:$time_multiplier);
  let results = (outs RTFormatEnvironmentType:$result);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def RTFileDescriptorFromBitsOp : RTOp<"file_descriptor.from_bits", [Pure]> {
  let arguments = (ins I32:$value);
  let results = (outs RTFileDescriptorType:$result);
}

def RTFileDescriptorToBitsOp : RTOp<"file_descriptor.to_bits", [Pure]> {
  let arguments = (ins RTFileDescriptorType:$value);
  let results = (outs I32:$result);
}

def RTStatusIsOp : RTOp<"status.is", [Pure]> {
  let arguments = (ins RTStatusType:$status, I32Attr:$value);
  let results = (outs I1:$result);
  let assemblyFormat = "$status `,` $value attr-dict";
}

def RTStatusFromBitsOp : RTOp<"status.from_bits", [Pure]> {
  let arguments = (ins I32:$value);
  let results = (outs RTStatusType:$result);
}

def RTStatusToBitsOp : RTOp<"status.to_bits", [Pure]> {
  let arguments = (ins RTStatusType:$value);
  let results = (outs I32:$result);
}

// Context, errors, and owned buffers.
def RTContextCreateOp : RTOp<"context.create", [
  MemoryEffects<[MemAlloc<RTRuntimeResource>]>
]> {
  let results = (outs RTStatusType:$status, RTContextType:$context);
}

def RTContextDestroyOp : RTOp<"context.destroy", [
  MemoryEffects<[MemFree<RTRuntimeResource>, MemWrite<RTIOResource>]>
]> {
  let arguments = (ins RTContextType:$context);
}

def RTStatusStringOp : RTOp<"status.string", [Pure]> {
  let arguments = (ins RTStatusType:$status);
  let results = (outs RTCStringType:$string);
}

def RTBufferReleaseOp : RTOp<"buffer.release", [
  MemoryEffects<[MemFree<RTRuntimeResource>, MemFree]>
]> {
  let arguments = (ins RTBufferType:$buffer);
  let hasVerifier = 1;
}

def RTLastErrorOp : RTOp<"last_error", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemAlloc<RTRuntimeResource>, MemAlloc]>
]> {
  let arguments = (ins RTContextType:$context);
  let results = (outs RTStatusType:$status, RTBufferType:$message);
  let hasVerifier = 1;
}

def RTFinishOp : RTOp<"finish", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>]>
]> {
  let arguments = (ins RTContextType:$context, I32:$verbosity);
  let results = (outs RTStatusType:$status);
}

def RTFatalOp : RTOp<"fatal", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>]>
]> {
  let arguments = (ins RTContextType:$context, I32:$verbosity);
  let results = (outs RTStatusType:$status);
}

def RTTerminationRequestedOp : RTOp<"termination.requested", [
  MemoryEffects<[MemRead<RTRuntimeResource>]>
]> {
  let arguments = (ins RTContextType:$context);
  let results = (outs I1:$result);
}

// Formatting and display.
def RTFormatOp : RTOp<"format", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemAlloc<RTRuntimeResource>, MemRead, MemAlloc]>
]> {
  let arguments = (ins RTContextType:$context, RTByteSpanType:$format,
                       RTArgumentArrayType:$arguments,
                       RTFormatEnvironmentType:$environment);
  let results = (outs RTStatusType:$status, RTBufferType:$buffer);
  let hasVerifier = 1;
}

def RTDisplayOp : RTOp<"display", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemWrite<RTIOResource>, MemRead]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor,
                       I1:$append_newline, RTArgumentArrayType:$items,
                       RTFormatEnvironmentType:$environment,
                       RTRadix:$default_radix);
  let results = (outs RTStatusType:$status);
}

// File services.
def RTFileOpenMCDOp : RTOp<"file.open_mcd", [
  MemoryEffects<[MemWrite<RTRuntimeResource>, MemWrite<RTIOResource>, MemRead]>
]> {
  let arguments = (ins RTContextType:$context, RTByteSpanType:$path);
  let results = (outs RTStatusType:$status, RTFileDescriptorType:$descriptor);
}

def RTFileOpenOp : RTOp<"file.open", [
  MemoryEffects<[MemWrite<RTRuntimeResource>, MemWrite<RTIOResource>, MemRead]>
]> {
  let arguments = (ins RTContextType:$context, RTByteSpanType:$path,
                       RTByteSpanType:$mode);
  let results = (outs RTStatusType:$status, RTFileDescriptorType:$descriptor);
}

def RTFileCloseOp : RTOp<"file.close", [
  MemoryEffects<[MemWrite<RTRuntimeResource>, MemWrite<RTIOResource>]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor);
  let results = (outs RTStatusType:$status);
}

def RTFileFlushOp : RTOp<"file.flush", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemWrite<RTIOResource>]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor);
  let results = (outs RTStatusType:$status);
}

def RTFileWriteOp : RTOp<"file.write", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemWrite<RTIOResource>, MemRead]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor,
                       RTByteSpanType:$data);
  let results = (outs RTStatusType:$status, I64:$written);
}

def RTFileReadOp : RTOp<"file.read", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemRead<RTIOResource>, MemWrite<RTIOResource>, MemWrite]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor,
                       RTMutableByteSpanType:$data);
  let results = (outs RTStatusType:$status, I64:$read);
}

def RTFileGetcOp : RTOp<"file.getc", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemRead<RTIOResource>, MemWrite<RTIOResource>]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor);
  let results = (outs RTStatusType:$status, I8:$byte);
}

def RTFileUngetcOp : RTOp<"file.ungetc", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemWrite<RTIOResource>]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor,
                       I8:$byte);
  let results = (outs RTStatusType:$status);
}

def RTFileGetlineOp : RTOp<"file.getline", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemRead<RTIOResource>, MemWrite<RTIOResource>,
                 MemAlloc<RTRuntimeResource>, MemAlloc]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor,
                       I64:$max_bytes);
  let results = (outs RTStatusType:$status, RTBufferType:$line);
  let hasVerifier = 1;
}

def RTFileEofOp : RTOp<"file.eof", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemRead<RTIOResource>]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor);
  let results = (outs RTStatusType:$status, I32:$is_eof);
}

def RTFileErrorOp : RTOp<"file.error", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemRead<RTIOResource>, MemAlloc<RTRuntimeResource>, MemAlloc]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor);
  let results = (outs RTStatusType:$status, I32:$error_code,
                      RTBufferType:$message);
  let hasVerifier = 1;
}

def RTFileSeekOp : RTOp<"file.seek", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemWrite<RTIOResource>]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor,
                       I64:$offset, I32:$origin);
  let results = (outs RTStatusType:$status);
}

def RTFileTellOp : RTOp<"file.tell", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemRead<RTIOResource>]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor);
  let results = (outs RTStatusType:$status, I64:$offset);
}

def RTFileRewindOp : RTOp<"file.rewind", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemWrite<RTIOResource>]>
]> {
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor);
  let results = (outs RTStatusType:$status);
}

// Uniform fragment dispatch. Lifecycle and driver construction deliberately
// remain outside bytecode-callable services.
def RTFragmentExecuteOp : RTOp<"fragment.execute", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemRead<RTIOResource>, MemWrite<RTIOResource>, MemRead,
                 MemWrite]>
]> {
  let arguments = (ins RTFragmentDescriptorType:$descriptor,
                       RTContextType:$context, RTMutableByteSpanType:$frame,
                       I32:$continuation);
  let results = (outs RTStatusType:$status, RTFragmentActionType:$action);
}

def RTBytecodeExecuteBoundedOp : RTOp<"bytecode.execute_bounded", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemRead<RTIOResource>, MemWrite<RTIOResource>, MemRead,
                 MemWrite]>
]> {
  let arguments = (ins RTFragmentDescriptorType:$descriptor,
                       RTContextType:$context, RTMutableByteSpanType:$frame,
                       I32:$continuation, I64:$instruction_limit);
  let results = (outs RTStatusType:$status, RTFragmentActionType:$action);
}

// Runtime-owned suspendable process instances. Creation performs the sole
// lifetime allocation; frame returns only the canonical prefix, while execute
// selects an available native or bytecode tier without moving that prefix.
def RTProcessInstanceCreateOp : RTOp<"process.instance.create", [
  MemoryEffects<[MemRead, MemAlloc<RTRuntimeResource>, MemAlloc]>
]> {
  let arguments = (ins RTProcessDescriptorType:$descriptor);
  let results = (outs RTStatusType:$status, RTProcessInstanceType:$instance);
}

def RTProcessInstanceFrameOp : RTOp<"process.instance.frame", [
  MemoryEffects<[MemRead<RTRuntimeResource>]>
]> {
  let arguments = (ins RTProcessInstanceType:$instance);
  let results = (outs RTStatusType:$status, RTMutableByteSpanType:$frame);
}

def RTProcessInstanceExecuteOp : RTOp<"process.instance.execute", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemRead<RTIOResource>, MemWrite<RTIOResource>, MemRead,
                 MemWrite]>
]> {
  let arguments = (ins RTProcessInstanceType:$instance,
                       RTContextType:$context, I32:$requested_tier);
  let results = (outs RTStatusType:$status, RTFragmentActionType:$action);
}

def RTProcessInstanceDestroyOp : RTOp<"process.instance.destroy", [
  MemoryEffects<[MemRead<RTRuntimeResource>, MemWrite<RTRuntimeResource>,
                 MemFree<RTRuntimeResource>, MemFree]>
]> {
  let arguments = (ins RTProcessInstanceType:$instance);
  let results = (outs RTStatusType:$status);
}

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEOPS_TD
