//===- HostCRuntime.cpp - Host C-runtime discovery -----------------------===//

#include "HostCRuntime.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/ToolChain.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <string>

using namespace llvm;
using namespace mlir;

namespace obelisk::driver {
namespace {

class CapturingDiagnosticConsumer final : public clang::DiagnosticConsumer {
public:
  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &diagnostic) override {
    DiagnosticConsumer::HandleDiagnostic(level, diagnostic);
    if (level < clang::DiagnosticsEngine::Error)
      return;
    SmallString<256> message;
    diagnostic.FormatDiagnostic(message);
    if (!errors.empty())
      errors += "; ";
    errors += message.str();
  }

  std::string errors;
};

} // namespace

FailureOr<HostCRuntimeInputs>
discoverHostCRuntime(StringRef triple, StringRef driverExecutablePath,
                     IntrusiveRefCntPtr<vfs::FileSystem> vfs) {
  if (!vfs)
    vfs = vfs::getRealFileSystem();

  clang::DiagnosticOptions diagnosticOptions;
  CapturingDiagnosticConsumer diagnosticConsumer;
  IntrusiveRefCntPtr<clang::DiagnosticIDs> diagnosticIDs(
      new clang::DiagnosticIDs());
  clang::DiagnosticsEngine diagnostics(diagnosticIDs, diagnosticOptions,
                                       &diagnosticConsumer,
                                       /*ShouldOwnClient=*/false);
  std::string executable = driverExecutablePath.str();
  clang::driver::Driver driver(executable, triple, diagnostics,
                               "obelisk host C-runtime discovery", vfs);
  driver.setCheckInputsExist(false);

  std::string target = (Twine("--target=") + triple).str();
  SmallVector<const char *> arguments{executable.c_str(), "--no-default-config",
                                      target.c_str(),     "-pie",
                                      "-nostdlib",        "-o",
                                      "/dev/null",        "probe.o"};
  std::unique_ptr<clang::driver::Compilation> compilation(
      driver.BuildCompilation(arguments));
  if (!compilation || diagnostics.hasErrorOccurred()) {
    errs() << "obelisk: error: clang could not discover the host C runtime";
    if (!diagnosticConsumer.errors.empty())
      errs() << ": " << diagnosticConsumer.errors;
    errs() << '\n';
    return failure();
  }

  HostCRuntimeInputs result;
  for (const clang::driver::Command &job : compilation->getJobs()) {
    if (job.getSource().getKind() != clang::driver::Action::LinkJobClass)
      continue;
    const llvm::opt::ArgStringList &jobArguments = job.getArguments();
    for (size_t index = 0; index + 1 < jobArguments.size(); ++index) {
      if (StringRef(jobArguments[index]) == "-dynamic-linker") {
        result.dynamicLinker = jobArguments[index + 1];
        break;
      }
    }
    if (!result.dynamicLinker.empty())
      break;
  }
  if (result.dynamicLinker.empty() ||
      !sys::path::is_absolute(result.dynamicLinker) ||
      !vfs->exists(result.dynamicLinker)) {
    errs() << "obelisk: error: clang's host link job has no usable dynamic "
              "linker";
    if (!result.dynamicLinker.empty())
      errs() << ": " << result.dynamicLinker;
    errs() << '\n';
    return failure();
  }

  const clang::driver::ToolChain &toolChain =
      compilation->getDefaultToolChain();
  struct FileInput {
    const char *name;
    std::string *path;
  };
  FileInput inputs[] = {{"Scrt1.o", &result.crt1},
                        {"crti.o", &result.crti},
                        {"crtn.o", &result.crtn},
                        {"libc.so", &result.libc},
                        {"libm.so", &result.libm}};
  SmallVector<StringRef> missing;
  for (FileInput input : inputs) {
    *input.path = toolChain.GetFilePath(input.name);
    if (!sys::path::is_absolute(*input.path) || !vfs->exists(*input.path)) {
      for (const std::string &directory : toolChain.getFilePaths()) {
        SmallString<256> candidate(directory);
        sys::path::append(candidate, input.name);
        if (!vfs->exists(candidate))
          continue;
        sys::path::remove_dots(candidate, true);
        *input.path = candidate.str().str();
        break;
      }
    }
    if (!sys::path::is_absolute(*input.path) || !vfs->exists(*input.path))
      missing.push_back(input.name);
  }
  if (missing.empty())
    return result;

  errs() << "obelisk: error: host C runtime is missing ";
  for (size_t index = 0; index < missing.size(); ++index) {
    if (index)
      errs() << ", ";
    errs() << missing[index];
  }
  errs() << "; install the host distribution's libc development files "
            "(common package names: libc6-dev, glibc-devel, or glibc; musl "
            "development files are not a glibc substitute)\nclang searched:";
  for (const std::string &path : toolChain.getFilePaths())
    errs() << "\n  " << path;
  errs() << '\n';
  return failure();
}

} // namespace obelisk::driver
