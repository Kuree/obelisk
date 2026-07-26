//===- NativeBackend.h - Hermetic x86-64 object and ELF emission -*- C++
//-*-===//

#ifndef OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H
#define OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H

#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mlir {
class ModuleOp;
}

namespace obelisk::driver {

enum class NativeOutputKind { Object, LLVMIR, Executable };

struct SharedLibraryInput {
  // The canonical path is used for compiler-side loading and path
  // deduplication.  The supplied directory remains the runtime search
  // location so a symlinked input can be deployed without being rewritten.
  std::string canonicalPath;
  std::string suppliedDirectory;
  std::string basename;
  std::string loaderName;
  bool hasSoname = false;
  bool hasVPIStartup = false;
  bool suppliedPathWasAbsolute = false;
};

struct NativeLinkInput {
  enum class Kind { File, SharedLibrary };
  Kind kind = Kind::File;
  std::string path;
  size_t sharedLibraryIndex = 0;
};

struct NativeOutputOptions {
  NativeOutputKind kind = NativeOutputKind::Executable;
  std::string outputPath;
  std::string explicitSysroot;
  std::string executablePath;
  std::vector<NativeLinkInput> nativeLinkInputs;
  std::vector<SharedLibraryInput> sharedLibraryInputs;
  std::string vpi = "off";
  bool bytecode = false;
  uint32_t optLevel = 3;
  uint32_t compileThreads = 1;
};

mlir::LogicalResult emitNativeOutput(mlir::ModuleOp module,
                                     const NativeOutputOptions &options);

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H
