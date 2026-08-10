//===- BackendUtils.h - Helpers shared by target backends -------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_BACKENDUTILS_H
#define OBELISK_TOOLS_DRIVER_BACKENDUTILS_H

#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CodeGen.h"

#include <cstdint>

namespace obelisk::driver {

llvm::CodeGenOptLevel getCodeGenOptLevel(uint32_t level);

/// Creates a unique temporary next to `output` so publishing it is a rename
/// within one directory, and therefore atomic.
mlir::FailureOr<llvm::SmallString<256>> makeTemporaryBeside(llvm::StringRef output,
                                                            llvm::StringRef suffix);

mlir::LogicalResult atomicallyReplace(llvm::StringRef temporary,
                                      llvm::StringRef output);

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_BACKENDUTILS_H
