//===- TargetBackend.h - Code-generation target interface -------*- C++ -*-===//
//
// Everything from MLIR lowering through LLVM optimization and object writing
// is target-independent. What differs per target is which LLVM backend to
// initialize, the triple and target machine, where the staged link support
// lives, and how the executable is linked. That boundary is this class.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_TOOLS_DRIVER_TARGETBACKEND_H
#define OBELISK_TOOLS_DRIVER_TARGETBACKEND_H

#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class TargetMachine;
}

namespace mlir {
class ModuleOp;
}

namespace obelisk::driver {

enum class NativeOutputKind { Object, LLVMIR, Executable };

/// Which code-generation target the driver is producing.
enum class TargetKind {
  /// Hermetic x86-64 ELF against a pinned glibc sysroot.
  Native,
  /// wasm64 modules for a WebAssembly host.
  Wasm,
};

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
  TargetKind target = TargetKind::Native;
  std::string outputPath;
  std::string explicitSysroot;
  std::string executablePath;
  std::vector<NativeLinkInput> nativeLinkInputs;
  std::vector<SharedLibraryInput> sharedLibraryInputs;
  std::string vpi = "off";
  std::string nativeScheduler = "auto";
  bool bytecode = false;
  // Full LTO re-optimizes the entire runtime on every link. Opting out links
  // the prebuilt runtime archive instead, trading peak simulation speed for a
  // link that costs milliseconds rather than seconds.
  bool noLTO = false;
  bool timing = false;
  uint32_t optLevel = 3;
  uint32_t compileThreads = 1;
};

/// A code-generation target. Implementations own only the target-specific
/// pieces; the shared pipeline lives in TargetBackend.cpp.
class TargetBackend {
public:
  virtual ~TargetBackend() = default;

  /// Target triple for the LLVM module and the staged support directory.
  virtual llvm::StringRef getTriple() const = 0;

  /// Human-readable name used in diagnostics.
  virtual llvm::StringRef getDescription() const = 0;

  /// Initializes the LLVM backend on first use and builds a target machine.
  /// Returns null and sets `error` on failure.
  virtual std::unique_ptr<llvm::TargetMachine>
  createTargetMachine(std::string &error, uint32_t optLevel) = 0;

  /// Whether an executable link at this optimization level consumes bitcode
  /// and runs full LTO, rather than consuming a relocatable object.
  virtual bool usesFullLTO(uint32_t optLevel) const { return optLevel != 0; }

  /// Links a previously written module into an executable image. `modulePath`
  /// is bitcode when usesFullLTO() holds and an object otherwise.
  virtual mlir::LogicalResult
  linkExecutable(llvm::StringRef modulePath, llvm::StringRef outputPath,
                 llvm::StringRef supportRoot,
                 const NativeOutputOptions &options) = 0;

  /// Locates the staged target-link support tree relative to the running
  /// driver, or nullopt when it is absent.
  std::optional<std::string>
  findSupportTree(llvm::StringRef executablePath) const;
};

/// Builds the backend for `target`, or null when this build does not include
/// it. A build configured for one target does not link the other's LLVM
/// backend or LLD driver, so availability is a build-time property.
std::unique_ptr<TargetBackend> createTargetBackend(TargetKind target);

/// Compiles `module` all the way to the requested output.
mlir::LogicalResult emitTargetOutput(mlir::ModuleOp module,
                                     const NativeOutputOptions &options);

} // namespace obelisk::driver

#endif // OBELISK_TOOLS_DRIVER_TARGETBACKEND_H
