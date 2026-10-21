//===- BackendUtils.h - Helpers shared by target backends -------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_BACKENDUTILS_H
#define OBELISK_TOOLS_DRIVER_BACKENDUTILS_H

#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CodeGen.h"

#include <cstdint>
#include <string>

namespace obelisk::driver {

llvm::CodeGenOptLevel getCodeGenOptLevel(uint32_t level);

/// Creates a unique temporary next to `output` so publishing it is a rename
/// within one directory, and therefore atomic.
mlir::FailureOr<llvm::SmallString<256>>
makeTemporaryBeside(llvm::StringRef output, llvm::StringRef suffix);

mlir::LogicalResult atomicallyReplace(llvm::StringRef temporary,
                                      llvm::StringRef output);

/// Returns the loader search directory for a shared library supplied from
/// `suppliedDirectory` to an image written at `outputPath`. An absolute
/// directory is kept; a relative one is expressed from `origin`, the loader's
/// token for the image's own directory, so the tree can be moved as a whole.
mlir::FailureOr<std::string>
getSharedLibraryRunpath(llvm::StringRef outputPath,
                        llvm::StringRef suppliedDirectory,
                        bool suppliedPathWasAbsolute, llvm::StringRef origin);

/// IEEE 1800 DPI-C and VPI entry points the static runtime defines, which a
/// simulator image exports to foreign code when DPI or VPI is enabled.
llvm::ArrayRef<llvm::StringLiteral> getRuntimeDPIEntryPoints();
llvm::ArrayRef<llvm::StringLiteral> getRuntimeVPIEntryPoints();

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_BACKENDUTILS_H
