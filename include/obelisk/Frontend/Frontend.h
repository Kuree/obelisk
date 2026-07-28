//===- Frontend.h - Obelisk SystemVerilog frontend support ------*- C++ -*-===//

#ifndef OBELISK_FRONTEND_FRONTEND_H
#define OBELISK_FRONTEND_FRONTEND_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Support/LogicalResult.h"

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mlir {
class MLIRContext;
} // namespace mlir

namespace obelisk::frontend {

enum class LanguageVersion : uint8_t {
  IEEE1800_2017,
  IEEE1800_2023,
};

/// Obelisk-owned, typed configuration for the slang driver. This is the public
/// frontend contract; no third-party driver option type crosses this boundary.
struct FrontendOptions {
  LanguageVersion languageVersion = LanguageVersion::IEEE1800_2023;

  std::vector<std::string> includeDirs;
  std::vector<std::string> includeSystemDirs;
  std::vector<std::string> defines;
  std::vector<std::string> undefines;
  std::vector<std::string> commandFiles;
  std::vector<std::string> libDirs;
  std::vector<std::string> libExts;
  std::vector<std::string> libraryFiles;
  std::vector<std::string> topModules;
  std::vector<std::string> paramOverrides;
  std::vector<std::string> warningOptions;
  std::vector<std::string> suppressWarningsPaths;
  std::vector<std::string> slangArgs;

  bool singleUnit = false;
  bool librariesInheritMacros = false;
  bool allowUseBeforeDeclare = false;
  bool ignoreUnknownModules = false;
  std::optional<uint32_t> maxIncludeDepth;
  std::optional<uint32_t> errorLimit;
  std::optional<std::string> timeScale;
};

/// Run preprocessing without parsing or elaborating the resulting token
/// stream. The returned string is the fully preprocessed source text.
mlir::FailureOr<std::string>
preprocessSystemVerilog(llvm::ArrayRef<std::string> inputFilenames,
                        const FrontendOptions &options);

/// Import one SystemVerilog compilation from the given primary source files.
/// Frontend search paths, macros, libraries, and language settings are supplied
/// through `options`. The returned module has passed the MLIR verifier when
/// `verifyIR` is true.
mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
importSystemVerilog(llvm::ArrayRef<std::string> inputFilenames,
                    mlir::MLIRContext &context, const FrontendOptions &options,
                    bool verifyIR = true);

/// Return the version string from the upstream slang library in this build.
std::string getSlangVersion();

} // namespace obelisk::frontend

#endif // OBELISK_FRONTEND_FRONTEND_H
