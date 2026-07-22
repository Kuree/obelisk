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
  let arguments = (ins RTContextType:$context, RTFileDescriptorType:$descriptor);
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
                       I64:$offset, RTSeekOrigin:$origin);
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

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEOPS_TD
