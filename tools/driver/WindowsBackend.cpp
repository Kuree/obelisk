//===- WindowsBackend.cpp - Native Windows COFF emission -----------------===//
//
// The Windows-native TargetBackend. Clang's driver library supplies the host
// SDK link arguments and LLD's COFF driver links in process. The shared
// compilation pipeline lives in TargetBackend.cpp.
//
//===----------------------------------------------------------------------===//

#include "BackendUtils.h"
#include "NativeBackend.h"

#include "lld/Common/Driver.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"

#include <algorithm>

using namespace llvm;
using namespace mlir;

LLD_HAS_DRIVER(coff)

namespace obelisk::driver {
namespace {
constexpr StringLiteral kTargetTriple = OBELISK_NATIVE_TARGET_TRIPLE;

class WindowsLinkDiagnostics final : public clang::DiagnosticConsumer {
public:
  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &diagnostic) override {
    DiagnosticConsumer::HandleDiagnostic(level, diagnostic);
    if (level < clang::DiagnosticsEngine::Warning)
      return;
    SmallString<256> message;
    diagnostic.FormatDiagnostic(message);
    errs() << "obelisk: "
           << (level >= clang::DiagnosticsEngine::Error ? "error: "
                                                        : "warning: ")
           << message << '\n';
  }
};

// Inspect PE tables without loading foreign code into the compiler process.
// The import library is generated in process and passed directly to LLD.
Error makeDLLImportLibrary(const SharedLibraryInput &input, StringRef path,
                           unsigned index, StringRef executableName,
                           SmallVectorImpl<std::string> &hostExports,
                           std::string &keepAlive) {
  auto buffer = MemoryBuffer::getFile(input.canonicalPath);
  if (!buffer)
    return errorCodeToError(buffer.getError());
  auto dll = object::COFFObjectFile::create((*buffer)->getMemBufferRef());
  if (!dll)
    return dll.takeError();
  SmallVector<object::COFFShortExport, 0> exports;
  for (const object::ExportDirectoryEntryRef &entry :
       (*dll)->export_directories()) {
    StringRef name;
    uint32_t rva;
    if (Error error = entry.getSymbolName(name))
      return error;
    if (Error error = entry.getExportRVA(rva))
      return error;
    if (name.empty() || !rva)
      continue;
    object::COFFShortExport symbol;
    symbol.Name = name.str();
    bool forwarder;
    if (Error error = entry.isForwarder(forwarder))
      return error;
    // A forwarder RVA names an export-directory string, not the target code
    // or data. Keep a callable thunk; data imports still use __imp_ symbols.
    if (!forwarder) {
      for (const object::SectionRef &section : (*dll)->sections()) {
        const object::coff_section *header = (*dll)->getCOFFSection(section);
        if (rva >= header->VirtualAddress &&
            rva - header->VirtualAddress <
                std::max(header->VirtualSize, header->SizeOfRawData)) {
          symbol.Data =
              !(header->Characteristics & COFF::IMAGE_SCN_MEM_EXECUTE);
          break;
        }
      }
    }
    exports.push_back(std::move(symbol));
  }
  if (exports.empty())
    return createStringError(inconvertibleErrorCode(),
                             "DLL has no named exports");
  // Keep every direct DLL input loaded, including VPI-only modules. Distinct
  // aliases let multiple modules export their own vlog_startup_routines table.
  object::COFFShortExport anchor;
  anchor.Name = (Twine("__obelisk_dll_") + Twine(index)).str();
  anchor.ExportAs = exports.front().Name;
  anchor.Data = true;
  keepAlive = "__imp_" + anchor.Name;
  exports.push_back(std::move(anchor));
  for (const object::ImportDirectoryEntryRef &entry :
       (*dll)->import_directories()) {
    StringRef name;
    if (Error error = entry.getName(name))
      return error;
    if (sys::path::extension(name).equals_insensitive(".exe") &&
        !name.equals_insensitive(executableName))
      return createStringError(
          inconvertibleErrorCode(),
          "DLL imports simulator image '%s' but output image is '%s'; "
          "regenerate its host import library with the output image name",
          name.str().c_str(), executableName.str().c_str());
    if (!name.equals_insensitive(executableName))
      continue;
    for (const object::ImportedSymbolRef &symbol : entry.imported_symbols()) {
      bool ordinal;
      if (Error error = symbol.isOrdinal(ordinal))
        return error;
      if (ordinal)
        return createStringError(inconvertibleErrorCode(),
                                 "simulator API imports must use names");
      if (Error error = symbol.getSymbolName(name))
        return error;
      hostExports.push_back(("/export:" + name).str());
    }
  }
  return object::writeImportLibrary(
      input.loaderName, path, exports,
      static_cast<COFF::MachineTypes>((*dll)->getMachine()), false);
}

LogicalResult publishExecutable(StringRef executable, StringRef outputPath,
                                ArrayRef<SharedLibraryInput> inputs) {
  struct PreparedDLL {
    SmallString<256> destination;
    std::unique_ptr<FileRemover> staged;
    std::unique_ptr<FileRemover> backup;
    SmallString<256> stagedPath;
    SmallString<256> backupPath;
    bool savedOriginal = false;
    bool published = false;
  };
  SmallVector<PreparedDLL, 0> prepared;
  // Finish all copies before touching an existing executable's companions.
  for (const SharedLibraryInput &input : inputs) {
    SmallString<256> destination(outputPath);
    sys::path::remove_filename(destination);
    sys::path::append(destination, input.basename);
    if (sys::fs::equivalent(input.canonicalPath, destination))
      continue;
    auto staged = makeTemporaryBeside(destination, ".dll");
    if (failed(staged))
      return failure();
    PreparedDLL &dll = prepared.emplace_back();
    dll.destination = destination;
    dll.stagedPath = *staged;
    dll.staged = std::make_unique<FileRemover>(*staged);
    if (std::error_code error =
            sys::fs::copy_file(input.canonicalPath, *staged)) {
      errs() << "obelisk: error: could not stage DLL '" << input.canonicalPath
             << "' beside the executable: " << error.message() << '\n';
      return failure();
    }
    if (sys::fs::exists(destination)) {
      if (sys::fs::is_directory(destination)) {
        errs() << "obelisk: error: DLL destination is a directory: '"
               << destination << "'\n";
        return failure();
      }
      auto backup = makeTemporaryBeside(destination, ".dll-backup");
      if (failed(backup))
        return failure();
      dll.backupPath = *backup;
      dll.backup = std::make_unique<FileRemover>(*backup);
    }
  }
  auto rollback = [&] {
    for (PreparedDLL &dll : llvm::reverse(prepared)) {
      if (dll.savedOriginal) {
        if (failed(atomicallyReplace(dll.backupPath, dll.destination))) {
          // Retain the original if another process prevents its restoration.
          dll.backup->releaseFile();
          errs() << "obelisk: error: original DLL retained at '"
                 << dll.backupPath << "'\n";
        }
      } else if (dll.published) {
        if (std::error_code error = sys::fs::remove(dll.destination))
          errs() << "obelisk: error: could not remove staged DLL '"
                 << dll.destination << "': " << error.message() << '\n';
      }
    }
    return failure();
  };
  for (PreparedDLL &dll : prepared) {
    if (dll.backup) {
      if (failed(atomicallyReplace(dll.destination, dll.backupPath)))
        return rollback();
      dll.savedOriginal = true;
    }
    if (failed(atomicallyReplace(dll.stagedPath, dll.destination)))
      return rollback();
    dll.published = true;
  }
  if (failed(atomicallyReplace(executable, outputPath)))
    return rollback();
  return success();
}

LogicalResult linkCOFFExecutable(ArrayRef<std::string> modulePaths,
                                 StringRef outputPath, StringRef supportRoot,
                                 const NativeOutputOptions &options,
                                 bool thinLTO) {
  FailureOr<SmallString<256>> temporary =
      makeTemporaryBeside(outputPath, ".exe");
  if (failed(temporary))
    return failure();
  sys::fs::remove(*temporary);
  bool fullLTO = options.optLevel != 0 && !options.noLTO && !thinLTO;
  SmallString<256> runtime(supportRoot);
  sys::path::append(runtime,
                    fullLTO ? "libobelisk_rt_lto.a" : "libobelisk_rt.a");
  if (!sys::fs::exists(runtime)) {
    errs() << "obelisk: error: native support is missing '" << runtime << "'\n";
    return failure();
  }

  // Clang's driver discovers Visual C++ and the Windows SDK/UCRT in process.
  // Read its linker job, then call LLD's COFF library; no subprocess runs.
  clang::DiagnosticOptions diagnosticOptions;
  WindowsLinkDiagnostics diagnosticConsumer;
  IntrusiveRefCntPtr<clang::DiagnosticIDs> diagnosticIDs(
      new clang::DiagnosticIDs);
  clang::DiagnosticsEngine diagnostics(diagnosticIDs, diagnosticOptions,
                                       &diagnosticConsumer, false);
  clang::driver::Driver driver(options.executablePath, kTargetTriple,
                               diagnostics);
  driver.setCheckInputsExist(false);
  SmallVector<std::string> owned{
      options.executablePath, "--driver-mode=g++",
      "--no-default-config",  "--target=" + kTargetTriple.str(),
      "-fuse-ld=lld",         "-fms-runtime-lib=dll",
      "-nostartfiles",        "-o",
      temporary->str().str()};
  // Pass precompiled inputs straight to the link job. Otherwise Clang plans a
  // backend subprocess for .bc files, whose output would never be produced by
  // this in-process linker path.
  auto addLinkInput = [&](StringRef path) {
    owned.push_back("-Xlinker");
    owned.push_back(path.str());
  };
  for (StringRef path : modulePaths)
    addLinkInput(path);
  SmallVector<std::unique_ptr<FileRemover>> importLibraries;
  SmallVector<std::string> dllArguments;
  for (const NativeLinkInput &input : options.nativeLinkInputs) {
    if (input.kind == NativeLinkInput::Kind::File) {
      addLinkInput(input.path);
      continue;
    }
    auto importPath = makeTemporaryBeside(outputPath, ".lib");
    if (failed(importPath))
      return failure();
    importLibraries.push_back(std::make_unique<FileRemover>(*importPath));
    const SharedLibraryInput &dll =
        options.sharedLibraryInputs[input.sharedLibraryIndex];
    if (StringRef(dll.basename)
            .equals_insensitive(sys::path::filename(outputPath))) {
      errs()
          << "obelisk: error: DLL input name conflicts with executable name: '"
          << dll.basename << "'\n";
      return failure();
    }
    std::string keepAlive;
    if (Error error = makeDLLImportLibrary(
            dll, *importPath, input.sharedLibraryIndex,
            sys::path::filename(outputPath), dllArguments, keepAlive)) {
      errs() << "obelisk: error: could not link DLL '" << dll.canonicalPath
             << "': " << toString(std::move(error)) << '\n';
      return failure();
    }
    dllArguments.push_back("/include:" + keepAlive);
    addLinkInput(*importPath);
  }
  addLinkInput(runtime);
  SmallVector<const char *> driverArguments;
  for (const std::string &argument : owned)
    driverArguments.push_back(argument.c_str());
  std::unique_ptr<clang::driver::Compilation> compilation(
      driver.BuildCompilation(driverArguments));
  if (!compilation || diagnostics.hasErrorOccurred())
    return failure();

  SmallVector<std::string> linkArguments{
      "lld-link",
      "/noimplib",
      "/debug:symtab",
      "/timestamp:0",
      "/stack:8388608",
      "/opt:ref",
      "/opt:noicf",
      (Twine("/threads:") + Twine(options.compileThreads)).str(),
      (Twine("/opt:lldlto=") + Twine(options.optLevel)).str()};
  llvm::append_range(linkArguments, dllArguments);
  if (options.dpi) {
    // Export the base DPI API only for designs that use the boundary. Marking
    // the static runtime itself dllexport would retain it in every simulator.
    for (StringRef name :
         {"svDpiVersion",       "svGetBitselBit",    "svGetBitselLogic",
          "svPutBitselBit",     "svPutBitselLogic",  "svGetPartselBit",
          "svGetPartselLogic",  "svPutPartselBit",   "svPutPartselLogic",
          "svGetScope",         "svSetScope",        "svGetNameFromScope",
          "svGetScopeFromName", "svPutUserData",     "svGetUserData",
          "svGetCallerInfo",    "svIsDisabledState", "svAckDisabledState",
          "svGetTime",          "svGetTimeUnit",     "svGetTimePrecision"})
      linkArguments.push_back(("/export:" + name).str());
  }
  bool foundLinkJob = false;
  for (const clang::driver::Command &job : compilation->getJobs()) {
    if (job.getSource().getKind() != clang::driver::Action::LinkJobClass)
      continue;
    for (const char *argument : job.getArguments())
      linkArguments.emplace_back(argument);
    foundLinkJob = true;
    break;
  }
  if (!foundLinkJob) {
    errs() << "obelisk: error: Clang did not produce a Windows linker job\n";
    return failure();
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
    linkArguments.push_back("/lldltocache:" + cache);
  }
  SmallVector<const char *> arguments;
  for (const std::string &argument : linkArguments)
    arguments.push_back(argument.c_str());
  lld::Result linked = lld::lldMain(arguments, outs(), errs(),
                                    {{lld::WinLink, &lld::coff::link}});
  if (linked.retCode != 0) {
    sys::fs::remove(*temporary);
    return failure();
  }
  if (failed(publishExecutable(*temporary, outputPath,
                               options.sharedLibraryInputs))) {
    sys::fs::remove(*temporary);
    return failure();
  }
  return success();
}

class WindowsBackend final : public TargetBackend {
public:
  StringRef getTriple() const override { return kTargetTriple; }
  StringRef getDescription() const override { return "native Windows COFF"; }
  bool supportsSemanticPartitions() const override { return true; }

  std::unique_ptr<TargetMachine>
  createTargetMachine(std::string &error, uint32_t optLevel) override {
    static const bool initialized =
        !InitializeNativeTarget() && !InitializeNativeTargetAsmPrinter();
    if (!initialized) {
      error = "could not initialize the native LLVM target";
      return nullptr;
    }
    Triple triple(kTargetTriple);
    const Target *target = TargetRegistry::lookupTarget(triple, error);
    if (!target)
      return nullptr;
    SubtargetFeatures features;
    for (const auto &feature : sys::getHostCPUFeatures())
      features.AddFeature(feature.getKey(), feature.getValue());
    return std::unique_ptr<TargetMachine>(target->createTargetMachine(
        triple, sys::getHostCPUName(), features.getString(), TargetOptions(),
        Reloc::PIC_, CodeModel::Small, getCodeGenOptLevel(optLevel)));
  }

  LogicalResult linkExecutable(ArrayRef<std::string> modulePaths,
                               StringRef outputPath, StringRef supportRoot,
                               const NativeOutputOptions &options,
                               bool thinLTO) override {
    return linkCOFFExecutable(modulePaths, outputPath, supportRoot, options,
                              thinLTO);
  }
};
} // namespace

std::unique_ptr<TargetBackend> createNativeBackend() {
  return std::make_unique<WindowsBackend>();
}
} // namespace obelisk::driver
