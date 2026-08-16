//===- NativeBackend.cpp - Hermetic x86-64 object and ELF emission -------===//
//
// The x86-64 TargetBackend. Everything here is specific to producing a
// hermetic ELF against the pinned glibc sysroot; the shared compilation
// pipeline lives in TargetBackend.cpp.
//
//===----------------------------------------------------------------------===//

#include "NativeBackend.h"

#include "BackendUtils.h"

#include "lld/Common/Driver.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <system_error>

using namespace llvm;
using namespace mlir;

LLD_HAS_DRIVER(elf)

extern "C" void LLVMInitializeX86TargetInfo();
extern "C" void LLVMInitializeX86Target();
extern "C" void LLVMInitializeX86TargetMC();
extern "C" void LLVMInitializeX86AsmPrinter();

namespace obelisk::driver {
namespace {

constexpr StringLiteral kTargetTriple = "x86_64-unknown-linux-gnu";

bool pathIsWithin(const std::filesystem::path &root,
                  const std::filesystem::path &path) {
  auto rootIt = root.begin();
  auto pathIt = path.begin();
  for (; rootIt != root.end(); ++rootIt, ++pathIt)
    if (pathIt == path.end() || *rootIt != *pathIt)
      return false;
  return true;
}

/// Resolve target-root symlinks without allowing an absolute target symlink to
/// escape to the build host.  Absolute links are interpreted relative to the
/// selected target root, as an ELF sysroot requires.
FailureOr<std::string> resolveTargetPath(StringRef rootText,
                                         StringRef relativeText) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path root = fs::absolute(fs::path(rootText.str()), ec).lexically_normal();
  if (ec)
    return failure();
  fs::path pending(relativeText.str());
  if (pending.is_absolute())
    pending = pending.relative_path();
  fs::path current = root;
  unsigned linkCount = 0;
  while (!pending.empty()) {
    auto component = pending.begin();
    fs::path rest;
    for (auto it = std::next(component); it != pending.end(); ++it)
      rest /= *it;
    current /= *component;
    current = current.lexically_normal();
    if (!pathIsWithin(root, current))
      return failure();
    fs::file_status status = fs::symlink_status(current, ec);
    if (ec)
      return failure();
    if (!fs::is_symlink(status)) {
      pending = std::move(rest);
      continue;
    }
    if (++linkCount > 40)
      return failure();
    fs::path target = fs::read_symlink(current, ec);
    if (ec)
      return failure();
    current = target.is_absolute() ? root : current.parent_path();
    pending = target.relative_path() / rest;
    if (!pathIsWithin(root, current))
      return failure();
  }
  if (!fs::exists(current, ec) || ec || !pathIsWithin(root, current))
    return failure();
  return current.string();
}

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

LogicalResult linkELFExecutable(StringRef modulePath, StringRef outputPath,
                                StringRef supportRoot, StringRef explicitSysroot,
                             ArrayRef<NativeLinkInput> nativeLinkInputs,
                             ArrayRef<SharedLibraryInput> sharedLibraryInputs,
                             uint32_t optLevel, bool noLTO,
                             uint32_t linkThreads) {
  bool fullLTO = optLevel != 0 && !noLTO;
  SmallString<256> glibcRoot;
  if (explicitSysroot.empty()) {
    glibcRoot = supportRoot;
    sys::path::append(glibcRoot, "glibc");
  } else {
    glibcRoot = explicitSysroot;
  }
  if (sys::fs::make_absolute(glibcRoot)) {
    errs() << "obelisk: error: invalid target sysroot '" << glibcRoot << "'\n";
    return failure();
  }
  sys::path::remove_dots(glibcRoot, true);

  auto targetInput = [&](StringRef relative) -> FailureOr<std::string> {
    FailureOr<std::string> resolved = resolveTargetPath(glibcRoot, relative);
    if (failed(resolved))
      errs() << "obelisk: error: target sysroot input '" << relative
             << "' is missing or escapes '" << glibcRoot << "'\n";
    return resolved;
  };
  SmallVector<std::string> glibcInputs;
  for (StringRef relative :
       {"usr/lib/x86_64-linux-gnu/Scrt1.o", "usr/lib/x86_64-linux-gnu/crti.o",
        "usr/lib/x86_64-linux-gnu/libc.so", "usr/lib/x86_64-linux-gnu/libm.so",
        "usr/lib/x86_64-linux-gnu/libpthread.so",
        "usr/lib/x86_64-linux-gnu/libdl.so",
        "usr/lib/x86_64-linux-gnu/librt.so",
        "usr/lib/x86_64-linux-gnu/crtn.o"}) {
    FailureOr<std::string> path = targetInput(relative);
    if (failed(path))
      return failure();
    glibcInputs.push_back(std::move(*path));
  }
  if (failed(targetInput("lib64/ld-linux-x86-64.so.2")))
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
  SmallVector<StringRef> staticInputNames{"clang_rt.crtbegin.o",
                                          fullLTO ? "libobelisk_rt_lto.a"
                                                  : "libobelisk_rt.a",
                                          "libc++.a",
                                          "libc++abi.a",
                                          "libunwind.a",
                                          "libclang_rt.builtins.a",
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
  owned.push_back("--export-dynamic-symbol=sv*");
  owned.push_back("--export-dynamic-symbol=vpi*");
  owned.push_back((Twine("--threads=") + Twine(linkThreads)).str());
  if (fullLTO) {
    owned.push_back("--lto=full");
    owned.push_back((Twine("--lto-O") + Twine(optLevel)).str());
    owned.push_back((Twine("--lto-CGO") + Twine(optLevel)).str());
    owned.push_back("--lto-whole-program-visibility");
    owned.push_back((Twine("--lto-partitions=") + Twine(linkThreads)).str());
  }
  owned.push_back("--eh-frame-hdr");
  owned.push_back("--hash-style=gnu");
  owned.push_back("--dynamic-linker=/lib64/ld-linux-x86-64.so.2");
  owned.push_back((Twine("--sysroot=") + glibcRoot).str());
  owned.push_back("-o");
  owned.push_back(temporary->str().str());
  owned.push_back(glibcInputs[0]);
  owned.push_back(glibcInputs[1]);
  owned.push_back(staticInputs[0]);
  owned.push_back(modulePath.str());
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
    if (input.hasSoname) {
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
  for (size_t index = 2; index <= 6; ++index)
    owned.push_back(glibcInputs[index]);
  owned.push_back("--end-group");
  owned.push_back(staticInputs[6]);
  owned.push_back(glibcInputs[7]);
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

/// The hermetic x86-64 ELF target.
class NativeBackend final : public TargetBackend {
public:
  StringRef getTriple() const override { return kTargetTriple; }
  StringRef getDescription() const override { return "x86-64 ELF"; }
  bool supportsSemanticPartitions() const override { return true; }

  std::unique_ptr<TargetMachine> createTargetMachine(std::string &error,
                                                     uint32_t optLevel) override {
    static bool initialized = false;
    if (!initialized) {
      LLVMInitializeX86TargetInfo();
      LLVMInitializeX86Target();
      LLVMInitializeX86TargetMC();
      LLVMInitializeX86AsmPrinter();
      initialized = true;
    }
    Triple triple(kTargetTriple);
    const Target *target = TargetRegistry::lookupTarget(triple, error);
    if (!target)
      return nullptr;
    TargetOptions targetOptions;
    return std::unique_ptr<TargetMachine>(target->createTargetMachine(
        triple, "x86-64", "", targetOptions, Reloc::PIC_, CodeModel::Small,
        getCodeGenOptLevel(optLevel)));
  }

  LogicalResult linkExecutable(StringRef modulePath, StringRef outputPath,
                               StringRef supportRoot,
                               const NativeOutputOptions &options) override {
    return linkELFExecutable(modulePath, outputPath, supportRoot,
                             options.explicitSysroot, options.nativeLinkInputs,
                             options.sharedLibraryInputs, options.optLevel,
                             options.noLTO, options.compileThreads);
  }
};

} // namespace

std::unique_ptr<TargetBackend> createNativeBackend() {
  return std::make_unique<NativeBackend>();
}

} // namespace obelisk::driver
