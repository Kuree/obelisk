//===- RuntimeABI.h - Central runtime operation/symbol catalog -*- C++ -*-===//

#ifndef TALOS_DIALECT_RUNTIME_RUNTIMEABI_H
#define TALOS_DIALECT_RUNTIME_RUNTIMEABI_H

#include "llvm/ADT/StringRef.h"

#include <optional>

namespace mlir {
class Operation;
}

namespace talos::runtime {

enum class RuntimeCall {
#define TALOS_RUNTIME_CALL(Name, Op, Symbol, Signature) Name,
#include "talos/Dialect/Runtime/RuntimeABI.def"
#undef TALOS_RUNTIME_CALL
};

enum class RuntimeSignature {
  ContextCreate,
  ContextDestroy,
  StatusString,
  BufferRelease,
  LastError,
  Finish,
  Error,
  TerminationRequested,
  SchedulerTime,
  Format,
  StringOutputFormat,
  Display,
  TimeFormat,
  TimeScanScale,
  DumpOpen,
  DumpVars,
  DumpContext,
  DumpU32,
  DumpU64,
  FileOpenMCD,
  FileOpen,
  FileDescriptorStatus,
  FileBytesCount,
  FileReadMemToken,
  FileByteOut,
  FileUngetc,
  FileBufferOut,
  FileU32Out,
  FileError,
  FileSeek,
  FileI64Out,
  FragmentExecute,
  BytecodeBounded,
  ProcessCreate,
  ProcessFrame,
  ProcessExecute,
  ProcessDestroy,
};

/// Return the exact current in-tree C ABI symbol for a runtime call.
llvm::StringRef getRuntimeSymbol(RuntimeCall call);

/// Return the canonical current C function signature key for a runtime call.
RuntimeSignature getRuntimeSignature(RuntimeCall call);

/// Identify a typed runtime operation, or std::nullopt for another operation.
std::optional<RuntimeCall> getRuntimeCall(mlir::Operation *operation);

/// Whether a runtime call is a diagnostic or termination service: its own
/// cost dwarfs any code placement, so generated code marks its declaration
/// cold and the optimizer lays the paths that reach it out of line. This
/// affects placement only, never what executes.
bool isColdRuntimeCall(RuntimeCall call);

} // namespace talos::runtime

#endif // TALOS_DIALECT_RUNTIME_RUNTIMEABI_H
