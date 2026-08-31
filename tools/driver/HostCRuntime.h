//===- HostCRuntime.h - Host C-runtime discovery --------------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_HOSTCRUNTIME_H
#define OBELISK_TOOLS_DRIVER_HOSTCRUNTIME_H

#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VirtualFileSystem.h"

#include <string>

namespace obelisk::driver {

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

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_HOSTCRUNTIME_H
