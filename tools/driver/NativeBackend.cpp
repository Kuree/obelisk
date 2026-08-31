//===- NativeBackend.cpp - Host-native object and ELF emission -----------===//
//
// The Linux-native TargetBackend. The shared compilation pipeline lives in
// TargetBackend.cpp.
//
//===----------------------------------------------------------------------===//

#include "NativeBackend.h"

#include "BackendUtils.h"
#include "HostCRuntime.h"

#include "lld/Common/Driver.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>

using namespace llvm;
using namespace mlir;

LLD_HAS_DRIVER(elf)

namespace obelisk::driver {
namespace {

constexpr StringLiteral kTargetTriple = OBELISK_NATIVE_TARGET_TRIPLE;

LogicalResult sanitizeBundledBuildPaths(StringRef path, StringRef supportRoot) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> input = MemoryBuffer::getFile(path);
  if (!input) {
    errs() << "obelisk: error: could not inspect linked output '" << path
           << "': " << input.getError().message() << '\n';
    return failure();
  }
  SmallString<256> manifestPath(supportRoot);
  sys::path::append(manifestPath, "BUILD_PATH_PREFIXES.txt");
  ErrorOr<std::unique_ptr<MemoryBuffer>> manifest =
      MemoryBuffer::getFile(manifestPath);
  if (!manifest) {
    errs() << "obelisk: error: could not inspect native-support build-prefix "
              "manifest '"
           << manifestPath << "': " << manifest.getError().message() << '\n';
    return failure();
  }
  StringRef contents = input.get()->getBuffer();
  std::string rewritten = contents.str();
  SmallVector<StringRef> prefixes;
  manifest.get()->getBuffer().split(prefixes, '\n', -1, false);
  bool changed = false;
  for (StringRef prefix : prefixes) {
    if (prefix.empty() || !contents.contains(prefix))
      continue;
    std::string replacement(prefix.size(), '_');
    constexpr StringLiteral neutral = "/obelisk-sdk";
    size_t neutralSize = std::min(neutral.size(), replacement.size());
    replacement.replace(0, neutralSize, neutral.data(), neutralSize);
    size_t offset = 0;
    while ((offset = rewritten.find(prefix.str(), offset)) !=
           std::string::npos) {
      rewritten.replace(offset, prefix.size(), replacement);
      offset += replacement.size();
      changed = true;
    }
  }
  if (!changed)
    return success();
  std::error_code error;
  raw_fd_ostream output(path, error, sys::fs::OF_None);
  if (error) {
    errs() << "obelisk: error: could not sanitize linked output '" << path
           << "': " << error.message() << '\n';
    return failure();
  }
  output.write(rewritten.data(), rewritten.size());
  output.flush();
  if (output.has_error()) {
    errs() << "obelisk: error: failed while sanitizing linked output '" << path
           << "': " << output.error().message() << '\n';
    output.clear_error();
    return failure();
  }
  return success();
}

LogicalResult linkELFExecutable(
    ArrayRef<std::string> modulePaths, StringRef outputPath,
    StringRef supportRoot, StringRef driverExecutablePath,
    StringRef thinLTOCacheDir, ArrayRef<NativeLinkInput> nativeLinkInputs,
    ArrayRef<SharedLibraryInput> sharedLibraryInputs, uint32_t optLevel,
    bool noLTO, uint32_t linkThreads, bool thinLTO, bool dpi) {
  bool fullLTO = optLevel != 0 && !noLTO && !thinLTO;
  FailureOr<HostCRuntimeInputs> hostRuntime =
      discoverHostCRuntime(kTargetTriple, driverExecutablePath);
  if (failed(hostRuntime))
    return failure();

  auto supportInput = [&](StringRef name) -> FailureOr<std::string> {
    SmallString<256> path(supportRoot);
    sys::path::append(path, name);
    if (!sys::fs::exists(path)) {
      errs() << "obelisk: error: native support is missing '" << path << "'\n";
      return failure();
    }
    return path.str().str();
  };
  SmallVector<std::string> staticInputs;
  StringRef runtimeArchive = fullLTO   ? "libobelisk_rt_lto.a"
                             : thinLTO ? "libobelisk_rt_prelinked.a"
                                       : "libobelisk_rt.a";
  SmallVector<StringRef> staticInputNames{
      "clang_rt.crtbegin.o", runtimeArchive, "libc++.a",
      "libc++abi.a",         "libunwind.a",  "libclang_rt.builtins.a",
      "clang_rt.crtend.o"};
  for (StringRef name : staticInputNames) {
    FailureOr<std::string> path = supportInput(name);
    if (failed(path))
      return failure();
    staticInputs.push_back(std::move(*path));
  }

  FailureOr<SmallString<256>> temporary =
      makeTemporaryBeside(outputPath, ".elf");
  if (failed(temporary)) {
    errs() << "obelisk: error: could not create temporary executable beside '"
           << outputPath << "'\n";
    return failure();
  }
  sys::fs::remove(*temporary);
  SmallVector<std::string> owned;
  owned.push_back("ld.lld");
  owned.push_back("--no-dependent-libraries");
  owned.push_back("--gc-sections");
  owned.push_back("-pie");
  if (dpi)
    owned.push_back("--export-dynamic-symbol=sv*");
  owned.push_back("--export-dynamic-symbol=vpi*");
  owned.push_back((Twine("--threads=") + Twine(linkThreads)).str());
  if (fullLTO) {
    owned.push_back("--lto=full");
    owned.push_back((Twine("--lto-O") + Twine(optLevel)).str());
    owned.push_back((Twine("--lto-CGO") + Twine(optLevel)).str());
    owned.push_back("--lto-whole-program-visibility");
    owned.push_back((Twine("--lto-partitions=") + Twine(linkThreads)).str());
  } else if (thinLTO) {
    owned.push_back("--lto=thin");
    owned.push_back((Twine("--lto-O") + Twine(optLevel)).str());
    owned.push_back((Twine("--lto-CGO") + Twine(optLevel)).str());
    owned.push_back("--lto-whole-program-visibility");
    // The driver supplies bounded, weight-balanced modules. Let the explicit
    // compiler thread budget govern ThinLTO's in-process backend pool too.
    owned.push_back(
        (Twine("--thinlto-jobs=") + Twine(std::max(linkThreads, uint32_t{1})))
            .str());
    SmallString<256> cacheDir;
    if (thinLTOCacheDir.empty()) {
      cacheDir = outputPath;
      cacheDir.append(".thinlto-cache");
    } else {
      cacheDir = thinLTOCacheDir;
    }
    if (std::error_code error = sys::fs::create_directories(cacheDir)) {
      errs() << "obelisk: error: could not create ThinLTO cache '" << cacheDir
             << "': " << error.message() << '\n';
      sys::fs::remove(*temporary);
      return failure();
    }
    owned.push_back((Twine("--thinlto-cache-dir=") + cacheDir.str()).str());
  }
  owned.push_back("--eh-frame-hdr");
  owned.push_back("--hash-style=gnu");
  owned.push_back(
      (Twine("--dynamic-linker=") + hostRuntime->dynamicLinker).str());
  owned.push_back("-o");
  owned.push_back(temporary->str().str());
  owned.push_back(hostRuntime->crt1);
  owned.push_back(hostRuntime->crti);
  owned.push_back(staticInputs[0]);
  for (const std::string &modulePath : modulePaths)
    owned.push_back(modulePath);
  bool noAsNeeded = false;
  for (const NativeLinkInput &linkInput : nativeLinkInputs) {
    if (linkInput.kind == NativeLinkInput::Kind::File) {
      if (noAsNeeded) {
        owned.push_back("--as-needed");
        noAsNeeded = false;
      }
      owned.push_back(linkInput.path);
      continue;
    }
    if (linkInput.sharedLibraryIndex >= sharedLibraryInputs.size()) {
      errs() << "obelisk: error: invalid classified shared-library input\n";
      return failure();
    }
    if (!noAsNeeded) {
      owned.push_back("--no-as-needed");
      noAsNeeded = true;
    }
    const SharedLibraryInput &input =
        sharedLibraryInputs[linkInput.sharedLibraryIndex];
    if (input.hasEmbeddedLoaderIdentity) {
      owned.push_back(input.canonicalPath);
    } else {
      owned.push_back((Twine("-L") + input.suppliedDirectory).str());
      owned.push_back((Twine("-l:") + input.basename).str());
    }
  }
  if (noAsNeeded)
    owned.push_back("--as-needed");

  if (!sharedLibraryInputs.empty()) {
    namespace fs = std::filesystem;
    std::error_code pathError;
    fs::path outputAbsolute =
        fs::absolute(fs::path(outputPath.str()), pathError).lexically_normal();
    if (pathError) {
      errs() << "obelisk: error: could not resolve output directory for "
                "RUNPATH generation: "
             << pathError.message() << '\n';
      return failure();
    }
    fs::path outputDirectory = outputAbsolute.parent_path();
    SmallVector<std::string> runpaths;
    llvm::StringSet<> seenRunpaths;
    for (const SharedLibraryInput &input : sharedLibraryInputs) {
      fs::path suppliedDirectory(input.suppliedDirectory);
      std::string runpath;
      if (input.suppliedPathWasAbsolute) {
        runpath = suppliedDirectory.lexically_normal().string();
      } else {
        fs::path suppliedAbsolute =
            fs::absolute(suppliedDirectory, pathError).lexically_normal();
        if (pathError) {
          errs() << "obelisk: error: could not resolve shared-library "
                    "directory '"
                 << input.suppliedDirectory << "': " << pathError.message()
                 << '\n';
          return failure();
        }
        fs::path relative =
            suppliedAbsolute.lexically_relative(outputDirectory);
        if (relative.empty())
          relative = ".";
        runpath = "$ORIGIN";
        if (relative != ".")
          runpath += "/" + relative.generic_string();
      }
      if (seenRunpaths.insert(runpath).second)
        runpaths.push_back(std::move(runpath));
    }
    if (!runpaths.empty()) {
      owned.push_back("-z");
      owned.push_back("origin");
      std::string joined;
      for (const std::string &runpath : runpaths) {
        if (!joined.empty())
          joined += ':';
        joined += runpath;
      }
      owned.push_back((Twine("--rpath=") + joined).str());
    }
  }
  owned.push_back("--start-group");
  for (size_t index = 1; index <= 5; ++index)
    owned.push_back(staticInputs[index]);
  owned.push_back(hostRuntime->libc);
  owned.push_back(hostRuntime->libm);
  owned.push_back("--end-group");
  owned.push_back(staticInputs[6]);
  owned.push_back(hostRuntime->crtn);
  SmallVector<const char *> arguments;
  for (std::string &argument : owned)
    arguments.push_back(argument.c_str());

  std::string stdoutText;
  std::string stderrText;
  raw_string_ostream stdoutStream(stdoutText);
  raw_string_ostream stderrStream(stderrText);
  lld::Result result = lld::lldMain(arguments, stdoutStream, stderrStream,
                                    {{lld::Gnu, &lld::elf::link}});
  stdoutStream.flush();
  stderrStream.flush();
  if (!stdoutText.empty())
    outs() << stdoutText;
  if (result.retCode != 0) {
    errs() << stderrText;
    sys::fs::remove(*temporary);
    return failure();
  }
  if (!stderrText.empty())
    errs() << stderrText;
  if (failed(sanitizeBundledBuildPaths(*temporary, supportRoot))) {
    sys::fs::remove(*temporary);
    return failure();
  }
  if (std::error_code error = sys::fs::setPermissions(
          *temporary, sys::fs::perms::all_read | sys::fs::perms::all_exe |
                          sys::fs::perms::owner_write)) {
    errs() << "obelisk: error: could not make '" << outputPath
           << "' executable: " << error.message() << '\n';
    sys::fs::remove(*temporary);
    return failure();
  }
  if (failed(atomicallyReplace(*temporary, outputPath))) {
    sys::fs::remove(*temporary);
    return failure();
  }
  return success();
}

/// The host-native Linux ELF target.
class NativeBackend final : public TargetBackend {
public:
  StringRef getTriple() const override { return kTargetTriple; }
  StringRef getDescription() const override { return "native Linux ELF"; }
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
    TargetOptions targetOptions;
    return std::unique_ptr<TargetMachine>(target->createTargetMachine(
        triple, "", "", targetOptions, Reloc::PIC_, CodeModel::Small,
        getCodeGenOptLevel(optLevel)));
  }

  LogicalResult linkExecutable(ArrayRef<std::string> modulePaths,
                               StringRef outputPath, StringRef supportRoot,
                               const NativeOutputOptions &options,
                               bool thinLTO) override {
    return linkELFExecutable(
        modulePaths, outputPath, supportRoot, options.executablePath,
        options.thinLTOCacheDir, options.nativeLinkInputs,
        options.sharedLibraryInputs, options.optLevel, options.noLTO,
        options.compileThreads, thinLTO, options.dpi);
  }
};

} // namespace

std::unique_ptr<TargetBackend> createNativeBackend() {
  return std::make_unique<NativeBackend>();
}

} // namespace obelisk::driver
