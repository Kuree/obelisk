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

struct NativeOutputOptions {
  NativeOutputKind kind = NativeOutputKind::Executable;
  std::string outputPath;
  std::string explicitSysroot;
  std::string executablePath;
  std::vector<std::string> dpiLinkInputs;
  bool bytecode = false;
  uint32_t optLevel = 3;
  uint32_t compileThreads = 1;
};

mlir::LogicalResult emitNativeOutput(mlir::ModuleOp module,
                                     const NativeOutputOptions &options);

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_NATIVEBACKEND_H
