//===- WasmBackend.cpp - wasm64 object and module emission ---------------===//
//
// The wasm64 TargetBackend, mirroring NativeBackend.cpp. The shared pipeline
// in TargetBackend.cpp is unchanged; what differs is the backend to
// initialize, the triple, the target features, and the link.
//
// wasm64 rather than wasm32 is not a preference: runtime/lib/ABI.cpp asserts
// sizeof(void*) == 8 and every descriptor layout assertion depends on it, so
// the runtime archive this links against only exists for a 64-bit pointer
// target.
//
//===----------------------------------------------------------------------===//

#include "WasmBackend.h"

#include "BackendUtils.h"

#include "lld/Common/Driver.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include <memory>
#include <optional>
#include <system_error>

using namespace llvm;
using namespace mlir;

LLD_HAS_DRIVER(wasm)

extern "C" void LLVMInitializeWebAssemblyTargetInfo();
extern "C" void LLVMInitializeWebAssemblyTarget();
extern "C" void LLVMInitializeWebAssemblyTargetMC();
extern "C" void LLVMInitializeWebAssemblyAsmPrinter();

namespace obelisk::driver {
namespace {

constexpr StringLiteral kTargetTriple = "wasm64-unknown-emscripten";

// Emscripten's own runtime is built with shared memory, and wasm-ld rejects
// objects that lack these features against such a link. They are inert in a
// single-threaded module, and the staged runtime archive is compiled with the
// matching -matomics -mbulk-memory in cmake/TargetWasmSupport.cmake.
constexpr StringLiteral kTargetFeatures =
    "+atomics,+bulk-memory,+mutable-globals,+sign-ext";

/// Locates the sysroot holding wasm libc, libc++ and the startup object.
/// An explicit --sysroot wins; otherwise EMSDK is consulted, which is how a
/// machine with emsdk on PATH is already configured.
FailureOr<SmallString<256>> findWasmSysroot(StringRef explicitSysroot) {
  SmallString<256> sysroot;
  if (!explicitSysroot.empty()) {
    sysroot = explicitSysroot;
  } else if (std::optional<std::string> emsdk =
                 sys::Process::GetEnv("EM_SYSROOT")) {
    sysroot = *emsdk;
  } else if (std::optional<std::string> emsdk =
                 sys::Process::GetEnv("EMSDK")) {
    sysroot = *emsdk;
    sys::path::append(sysroot, "upstream", "emscripten", "cache", "sysroot");
  } else {
    errs() << "obelisk: error: no wasm sysroot; pass --sysroot=<dir> or set "
              "EMSDK\n";
    return failure();
  }
  if (sys::fs::make_absolute(sysroot)) {
    errs() << "obelisk: error: invalid wasm sysroot '" << sysroot << "'\n";
    return failure();
  }
  sys::path::remove_dots(sysroot, true);
  if (!sys::fs::is_directory(sysroot)) {
    errs() << "obelisk: error: wasm sysroot '" << sysroot
           << "' is not a directory\n";
    return failure();
  }
  return sysroot;
}

LogicalResult linkWasmModule(StringRef modulePath, StringRef outputPath,
                             StringRef supportRoot,
                             const NativeOutputOptions &options) {
  if (!options.sharedLibraryInputs.empty()) {
    errs() << "obelisk: error: shared libraries are not linkable into a wasm "
              "module; link them statically instead\n";
    return failure();
  }

  FailureOr<SmallString<256>> sysroot =
      findWasmSysroot(options.explicitSysroot);
  if (failed(sysroot))
    return failure();

  SmallString<256> libraryDirectory(*sysroot);
  sys::path::append(libraryDirectory, "lib", "wasm64-emscripten");
  if (!sys::fs::is_directory(libraryDirectory)) {
    errs() << "obelisk: error: wasm sysroot '" << *sysroot
           << "' has no wasm64-emscripten libraries; a wasm32 sysroot cannot "
              "satisfy the 64-bit runtime ABI\n";
    return failure();
  }

  bool fullLTO = options.optLevel != 0;
  SmallString<256> runtimeArchive(supportRoot);
  sys::path::append(runtimeArchive, fullLTO ? "libobelisk_rt_lto.a"
                                            : "libobelisk_rt.a");
  if (!sys::fs::exists(runtimeArchive)) {
    errs() << "obelisk: error: wasm support is missing '" << runtimeArchive
           << "'\n";
    return failure();
  }

  FailureOr<SmallString<256>> temporary =
      makeTemporaryBeside(outputPath, ".wasm");
  if (failed(temporary)) {
    errs() << "obelisk: error: could not create temporary module beside '"
           << outputPath << "'\n";
    return failure();
  }
  sys::fs::remove(*temporary);

  SmallVector<std::string> owned;
  owned.push_back("wasm-ld");
  owned.push_back("-mwasm64");
  owned.push_back("--gc-sections");
  owned.push_back("-o");
  owned.push_back(temporary->str().str());
  if (fullLTO) {
    owned.push_back((Twine("--lto-O") + Twine(options.optLevel)).str());
    owned.push_back("--lto-whole-program-visibility");
  }
  // crt1.o provides _start, which calls main.
  SmallString<256> startup(libraryDirectory);
  sys::path::append(startup, "crt1.o");
  if (!sys::fs::exists(startup)) {
    errs() << "obelisk: error: wasm sysroot is missing '" << startup << "'\n";
    return failure();
  }
  owned.push_back(startup.str().str());
  owned.push_back(modulePath.str());
  for (const NativeLinkInput &linkInput : options.nativeLinkInputs)
    owned.push_back(linkInput.path);
  owned.push_back(runtimeArchive.str().str());
  owned.push_back((Twine("-L") + libraryDirectory).str());
  // A static link resolves in one pass over a group; the runtime, libc++ and
  // libc reference each other.
  owned.push_back("--start-group");
  for (StringRef library : {"-lc", "-lc++", "-lc++abi", "-lcompiler_rt"})
    owned.push_back(library.str());
  owned.push_back("--end-group");

  SmallVector<const char *> arguments;
  for (std::string &argument : owned)
    arguments.push_back(argument.c_str());

  std::string stdoutText;
  std::string stderrText;
  raw_string_ostream stdoutStream(stdoutText);
  raw_string_ostream stderrStream(stderrText);
  lld::Result result = lld::lldMain(arguments, stdoutStream, stderrStream,
                                    {{lld::Wasm, &lld::wasm::link}});
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
  if (failed(atomicallyReplace(*temporary, outputPath))) {
    sys::fs::remove(*temporary);
    return failure();
  }
  return success();
}

/// The wasm64 target.
class WasmBackend final : public TargetBackend {
public:
  StringRef getTriple() const override { return kTargetTriple; }
  StringRef getDescription() const override { return "wasm64"; }

  std::unique_ptr<TargetMachine>
  createTargetMachine(std::string &error, uint32_t optLevel) override {
    static bool initialized = false;
    if (!initialized) {
      LLVMInitializeWebAssemblyTargetInfo();
      LLVMInitializeWebAssemblyTarget();
      LLVMInitializeWebAssemblyTargetMC();
      LLVMInitializeWebAssemblyAsmPrinter();
      initialized = true;
    }
    Triple triple(kTargetTriple);
    const Target *target = TargetRegistry::lookupTarget(triple, error);
    if (!target)
      return nullptr;
    TargetOptions targetOptions;
    // WebAssembly has no PIC/code-model distinction to make; the defaults are
    // the only meaningful choice.
    return std::unique_ptr<TargetMachine>(target->createTargetMachine(
        triple, "generic", kTargetFeatures, targetOptions, std::nullopt,
        std::nullopt, getCodeGenOptLevel(optLevel)));
  }

  LogicalResult linkExecutable(StringRef modulePath, StringRef outputPath,
                               StringRef supportRoot,
                               const NativeOutputOptions &options) override {
    return linkWasmModule(modulePath, outputPath, supportRoot, options);
  }
};

} // namespace

std::unique_ptr<TargetBackend> createWasmBackend() {
  return std::make_unique<WasmBackend>();
}

} // namespace obelisk::driver
