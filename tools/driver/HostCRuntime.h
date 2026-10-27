//===- HostCRuntime.h - Host C-runtime discovery --------------*- C++ -*-===//

#ifndef TALOS_TOOLS_DRIVER_HOSTCRUNTIME_H
#define TALOS_TOOLS_DRIVER_HOSTCRUNTIME_H

#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VirtualFileSystem.h"

#include <string>

namespace talos::driver {

/// Host C-runtime link inputs, discovered from clang's Driver by filesystem
/// inspection only. The constructed Compilation's jobs are read, never run.
struct HostCRuntimeInputs {
  std::string dynamicLinker;
  std::string crt1;
  std::string crti;
  std::string crtn;
  std::string libc;
  std::string libm;
};

mlir::FailureOr<HostCRuntimeInputs> discoverHostCRuntime(
    llvm::StringRef triple, llvm::StringRef driverExecutablePath,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> vfs = nullptr);

} // namespace talos::driver

#endif // TALOS_TOOLS_DRIVER_HOSTCRUNTIME_H
