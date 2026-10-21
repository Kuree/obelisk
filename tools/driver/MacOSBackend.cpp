//===- MacOSBackend.cpp - Native macOS ARM64 Mach-O emission -------------===//
//
// The shared MLIR/LLVM pipeline emits native objects or LTO bitcode. This
// backend links them in process with LLD and the staged macOS link stubs.
//
//===----------------------------------------------------------------------===//

#include "BackendUtils.h"
#include "NativeBackend.h"

#include "lld/Common/Driver.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"

#include <algorithm>

using namespace llvm;
using namespace mlir;

LLD_HAS_DRIVER(macho)

namespace obelisk::driver {
namespace {
constexpr StringLiteral kTargetTriple = OBELISK_NATIVE_TARGET_TRIPLE;
constexpr StringLiteral kMinimumOS = OBELISK_MACOS_DEPLOYMENT_TARGET;
constexpr StringLiteral kSDKVersion = OBELISK_MACOS_SDK_VERSION;

LogicalResult linkMachOExecutable(ArrayRef<std::string> modulePaths,
                                  StringRef outputPath, StringRef supportRoot,
                                  const NativeOutputOptions &options,
                                  bool thinLTO) {
  SmallString<256> runtime(supportRoot);
  bool fullLTO = options.optLevel != 0 && !options.noLTO && !thinLTO;
  sys::path::append(runtime,
                    fullLTO ? "libobelisk_rt_lto.a" : "libobelisk_rt.a");
  SmallString<256> sysroot(supportRoot);
  sys::path::append(sysroot, "sdk");
  for (StringRef path : {StringRef(runtime), StringRef(sysroot)}) {
    if (!sys::fs::exists(path)) {
      errs() << "obelisk: error: native support is missing '" << path << "'\n";
      return failure();
    }
  }
  // LLD names the ad-hoc signature, and so the image UUID, after the output
  // file. Link under the final name in a private directory beside it so the
  // image is reproducible and publishing it is still a rename.
  SmallString<256> stagingPrefix(outputPath);
  if (std::error_code error = sys::fs::make_absolute(stagingPrefix)) {
    errs() << "obelisk: error: could not resolve output path '" << outputPath
           << "': " << error.message() << '\n';
    return failure();
  }
  stagingPrefix.append(".tmp");
  SmallString<256> staging;
  if (std::error_code error =
          sys::fs::createUniqueDirectory(stagingPrefix, staging)) {
    errs() << "obelisk: error: could not create a link directory beside '"
           << outputPath << "': " << error.message() << '\n';
    return failure();
  }
  scope_exit removeStaging([&] { sys::fs::remove(staging); });
  SmallString<256> temporary(staging);
  sys::path::append(temporary, sys::path::filename(outputPath));
  FileRemover cleanup(temporary);
  SmallVector<std::string> owned{
      "ld64.lld",
      "-arch",
      "arm64",
      "-platform_version",
      "macos",
      kMinimumOS.str(),
      kSDKVersion.str(),
      "-syslibroot",
      sysroot.str().str(),
      "-dead_strip",
      "-adhoc_codesign",
      "-o",
      temporary.str().str(),
      (Twine("--threads=") + Twine(std::max(options.compileThreads, 1u))).str(),
      (Twine("--lto-O") + Twine(options.optLevel)).str(),
      (Twine("--lto-CGO") + Twine(options.optLevel)).str()};
  SmallString<256> weakImportsPath(supportRoot);
  sys::path::append(weakImportsPath, "weak-imports.txt");
  auto weakImports = MemoryBuffer::getFile(weakImportsPath);
  if (!weakImports) {
    errs() << "obelisk: error: could not read macOS weak-import manifest: "
           << weakImports.getError().message() << '\n';
    return failure();
  }
  SmallVector<StringRef> weakSymbols;
  (*weakImports)->getBuffer().split(weakSymbols, '\n', -1, false);
  for (StringRef symbol : weakSymbols) {
    owned.push_back("-U");
    owned.push_back(symbol.str());
  }
  // Foreign dylibs bind the sv*/vpi* API and user DPI exports from this image.
  // Name each one exactly: LLD does not read a dylib's imports, and LTO keeps
  // only literal -exported_symbol names. An export that nothing defines fails
  // the link instead of the later dyld load.
  SmallVector<std::string> exports;
  StringSet<> exported;
  auto exportSymbol = [&](const Twine &name) {
    std::string symbol = name.str();
    if (exported.insert(symbol).second)
      exports.push_back(std::move(symbol));
  };
  if (options.dpi)
    for (StringRef name : getRuntimeDPIEntryPoints())
      exportSymbol("_" + name);
  if (options.vpi != "off")
    for (StringRef name : getRuntimeVPIEntryPoints())
      exportSymbol("_" + name);
  // A flat-namespace dylib may bind any symbol in the process.
  bool exportAll = false;
  for (const SharedLibraryInput &library : options.sharedLibraryInputs) {
    exportAll |= library.flatNamespace;
    for (const std::string &name : library.hostImports)
      exportSymbol(name);
  }
  if (exportAll) {
    owned.push_back("-export_dynamic");
  } else if (exports.empty()) {
    owned.push_back("-no_exported_symbols");
  } else {
    for (std::string &name : exports) {
      owned.push_back("-exported_symbol");
      owned.push_back(std::move(name));
    }
  }
  if (thinLTO) {
    std::string cache = options.thinLTOCacheDir.empty()
                            ? (outputPath + ".thinlto-cache").str()
                            : options.thinLTOCacheDir;
    if (std::error_code error = sys::fs::create_directories(cache)) {
      errs() << "obelisk: error: could not create ThinLTO cache: "
             << error.message() << '\n';
      return failure();
    }
    owned.push_back("-cache_path_lto");
    owned.push_back(std::move(cache));
    owned.push_back(
        (Twine("--thinlto-jobs=") + Twine(std::max(options.compileThreads, 1u)))
            .str());
  }
  llvm::append_range(owned, modulePaths);
  StringSet<> runpaths;
  for (const NativeLinkInput &input : options.nativeLinkInputs) {
    if (input.kind == NativeLinkInput::Kind::File) {
      owned.push_back(input.path);
      continue;
    }
    if (input.sharedLibraryIndex >= options.sharedLibraryInputs.size()) {
      errs() << "obelisk: error: invalid classified shared-library input\n";
      return failure();
    }
    const SharedLibraryInput &library =
        options.sharedLibraryInputs[input.sharedLibraryIndex];
    // Retain VPI startup-only libraries even when no design symbol uses them.
    owned.push_back("-needed_library");
    owned.push_back(library.canonicalPath);
    FailureOr<std::string> runpath = getSharedLibraryRunpath(
        outputPath, library.suppliedDirectory, library.suppliedPathWasAbsolute,
        "@executable_path");
    if (failed(runpath))
      return failure();
    if (runpaths.insert(*runpath).second) {
      owned.push_back("-rpath");
      owned.push_back(std::move(*runpath));
    }
  }
  owned.push_back(runtime.str().str());
  owned.push_back("-lc++");
  owned.push_back("-lSystem");
  SmallVector<const char *> arguments;
  for (const std::string &argument : owned)
    arguments.push_back(argument.c_str());
  lld::Result result = lld::lldMain(arguments, outs(), errs(),
                                    {{lld::Darwin, &lld::macho::link}});
  if (result.retCode != 0)
    return failure();
  // The signature covers the linked bytes. Do not rewrite paths or otherwise
  // mutate the image after LLD has signed it.
  if (std::error_code error = sys::fs::setPermissions(
          temporary, sys::fs::perms::all_read | sys::fs::perms::all_exe |
                         sys::fs::perms::owner_write)) {
    errs() << "obelisk: error: could not make '" << outputPath
           << "' executable: " << error.message() << '\n';
    return failure();
  }
  return atomicallyReplace(temporary, outputPath);
}

class MacOSBackend final : public TargetBackend {
public:
  StringRef getTriple() const override { return kTargetTriple; }
  StringRef getDescription() const override {
    return "native macOS ARM64 Mach-O";
  }
  bool supportsSemanticPartitions() const override { return true; }

  std::unique_ptr<TargetMachine>
  createTargetMachine(std::string &error, uint32_t optLevel) override {
    static bool initialized = false;
    if (!initialized) {
      if (InitializeNativeTarget() || InitializeNativeTargetAsmPrinter()) {
        error = "could not initialize the native LLVM target";
        return nullptr;
      }
      initialized = true;
    }
    Triple triple(kTargetTriple);
    const Target *target = TargetRegistry::lookupTarget(triple, error);
    if (!target)
      return nullptr;
    SubtargetFeatures features;
    for (const auto &feature : sys::getHostCPUFeatures())
      features.AddFeature(feature.getKey(), feature.getValue());
    TargetOptions targetOptions;
    targetOptions.EnableGlobalISel = true;
    targetOptions.GlobalISelAbort = GlobalISelAbortMode::Disable;
    return std::unique_ptr<TargetMachine>(target->createTargetMachine(
        triple, sys::getHostCPUName(), features.getString(), targetOptions,
        Reloc::PIC_, CodeModel::Small, getCodeGenOptLevel(optLevel)));
  }

  LogicalResult linkExecutable(ArrayRef<std::string> modulePaths,
                               StringRef outputPath, StringRef supportRoot,
                               const NativeOutputOptions &options,
                               bool thinLTO) override {
    return linkMachOExecutable(modulePaths, outputPath, supportRoot, options,
                               thinLTO);
  }
};
} // namespace

std::unique_ptr<TargetBackend> createNativeBackend() {
  return std::make_unique<MacOSBackend>();
}
} // namespace obelisk::driver
