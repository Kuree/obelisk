//===- RuntimeABI.h - Central runtime operation/symbol catalog -*- C++ -*-===//

#ifndef OBELISK_DIALECT_RUNTIME_RUNTIMEABI_H
#define OBELISK_DIALECT_RUNTIME_RUNTIMEABI_H

#include "llvm/ADT/StringRef.h"

#include <optional>

namespace mlir {
class Operation;
}

namespace obelisk::runtime {

enum class RuntimeCall {
#define OBELISK_RUNTIME_CALL(Name, Op, Symbol, Signature) Name,
#include "obelisk/Dialect/Runtime/RuntimeABI.def"
#undef OBELISK_RUNTIME_CALL
};

enum class RuntimeSignature {
  ContextCreate,
  ContextDestroy,
  StatusString,
  BufferRelease,
  LastError,
  Finish,
  TerminationRequested,
  SchedulerTime,
  Format,
  StringOutputFormat,
  Display,
  TimeFormat,
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

} // namespace obelisk::runtime

#endif // OBELISK_DIALECT_RUNTIME_RUNTIMEABI_H
