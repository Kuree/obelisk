//===- NativeBackend.cpp - Hermetic x86-64 object and ELF emission -------===//

#include "NativeBackend.h"

#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"

#include "lld/Common/Driver.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
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

std::optional<std::string> findNativeSupport(StringRef executablePath) {
  SmallString<256> executable(executablePath);
  if (sys::fs::make_absolute(executable))
    return std::nullopt;
  sys::path::remove_filename(executable);
  SmallVector<SmallString<256>, 2> candidates;
  SmallString<256> installed(executable);
  sys::path::append(installed, "..", "lib", "obelisk", "targets");
  sys::path::append(installed, kTargetTriple);
  candidates.push_back(installed);
  SmallString<256> buildTree(executable);
  sys::path::append(buildTree, "..", "..", "lib", "obelisk");
  sys::path::append(buildTree, "targets", kTargetTriple);
  candidates.push_back(buildTree);
  for (SmallString<256> &candidate : candidates) {
    sys::path::remove_dots(candidate, true);
    if (sys::fs::exists(Twine(candidate) + "/.complete"))
      return candidate.str().str();
  }
  return std::nullopt;
}

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

std::unique_ptr<TargetMachine> createTargetMachine(std::string &error) {
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
      CodeGenOptLevel::Aggressive));
}

LogicalResult addMinimalMain(ModuleOp module) {
  if (module.lookupSymbol("main"))
    return success();
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToEnd(module.getBody());
  Location location = module.getLoc();
  mlir::Type i32 = builder.getI32Type();
  auto main = LLVM::LLVMFuncOp::create(
      builder, location, "main", LLVM::LLVMFunctionType::get(i32, {}, false));
  Block *entry = main.addEntryBlock(builder);
  builder.setInsertionPointToStart(entry);
  mlir::Value zero = LLVM::ConstantOp::create(builder, location, i32,
                                              builder.getI32IntegerAttr(0));
  LLVM::ReturnOp::create(builder, location, zero);
  return success();
}

LogicalResult lowerToLLVM(ModuleOp module, TargetMachine &targetMachine) {
  module->setAttr("llvm.target_triple",
                  StringAttr::get(module.getContext(), kTargetTriple));
  module->setAttr(
      "llvm.data_layout",
      StringAttr::get(
          module.getContext(),
          targetMachine.createDataLayout().getStringRepresentation()));
  mlir::PassManager manager(module.getContext());
  manager.addPass(createConvertObeliskSimProcessesToLLVMCoroutinesPass());
  if (failed(manager.run(module)))
    return failure();
  return addMinimalMain(module);
}

LogicalResult optimizeLLVMModule(llvm::Module &module,
                                 TargetMachine &targetMachine) {
  if (verifyModule(module, &errs())) {
    errs() << "obelisk: error: invalid LLVM IR before native optimization\n";
    return failure();
  }
  PassBuilder builder(&targetMachine);
  LoopAnalysisManager loopAnalyses;
  FunctionAnalysisManager functionAnalyses;
  CGSCCAnalysisManager cgsccAnalyses;
  llvm::ModuleAnalysisManager moduleAnalyses;
  builder.registerModuleAnalyses(moduleAnalyses);
  builder.registerCGSCCAnalyses(cgsccAnalyses);
  builder.registerFunctionAnalyses(functionAnalyses);
  builder.registerLoopAnalyses(loopAnalyses);
  builder.crossRegisterProxies(loopAnalyses, functionAnalyses, cgsccAnalyses,
                               moduleAnalyses);
  ModulePassManager passes =
      builder.buildPerModuleDefaultPipeline(OptimizationLevel::O3);
  passes.run(module, moduleAnalyses);
  if (verifyModule(module, &errs())) {
    errs() << "obelisk: error: invalid LLVM IR after native optimization\n";
    return failure();
  }
  return success();
}

LogicalResult writeObject(llvm::Module &module, TargetMachine &targetMachine,
                          StringRef path) {
  std::error_code error;
  raw_fd_ostream output(path, error, sys::fs::OF_None);
  if (error) {
    errs() << "obelisk: error: could not create object '" << path
           << "': " << error.message() << '\n';
    return failure();
  }
  legacy::PassManager codegen;
  if (targetMachine.addPassesToEmitFile(codegen, output, nullptr,
                                        CodeGenFileType::ObjectFile)) {
    errs() << "obelisk: error: x86-64 target cannot emit ELF objects\n";
    return failure();
  }
  codegen.run(module);
  output.flush();
  if (output.has_error()) {
    errs() << "obelisk: error: failed while writing object '" << path
           << "': " << output.error().message() << '\n';
    output.clear_error();
    return failure();
  }
  return success();
}

FailureOr<SmallString<256>> makeTemporaryBeside(StringRef output,
                                                StringRef suffix) {
  SmallString<256> pattern(output);
  pattern.append(".tmp-%%%%%%");
  pattern.append(suffix);
  int descriptor = -1;
  SmallString<256> path;
  if (sys::fs::createUniqueFile(pattern, descriptor, path))
    return failure();
  sys::Process::SafelyCloseFileDescriptor(descriptor);
  return path;
}

LogicalResult atomicallyReplace(StringRef temporary, StringRef output) {
  std::error_code error = sys::fs::rename(temporary, output);
  if (error) {
    errs() << "obelisk: error: could not publish '" << output
           << "': " << error.message() << '\n';
    return failure();
  }
  return success();
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

LogicalResult linkExecutable(StringRef objectPath, StringRef outputPath,
                             StringRef supportRoot, StringRef explicitSysroot) {
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
  for (StringRef name :
       {"clang_rt.crtbegin.o", "libobelisk_rt.a", "libc++.a", "libc++abi.a",
        "libunwind.a", "libclang_rt.builtins.a", "clang_rt.crtend.o"}) {
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
  owned.push_back("-pie");
  owned.push_back("--eh-frame-hdr");
  owned.push_back("--hash-style=gnu");
  owned.push_back("--dynamic-linker=/lib64/ld-linux-x86-64.so.2");
  owned.push_back((Twine("--sysroot=") + glibcRoot).str());
  owned.push_back("-o");
  owned.push_back(temporary->str().str());
  owned.push_back(glibcInputs[0]);
  owned.push_back(glibcInputs[1]);
  owned.push_back(staticInputs[0]);
  owned.push_back(objectPath.str());
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
  if (std::error_code error =
          sys::fs::setPermissions(*temporary, sys::fs::perms::all_read |
                                                  sys::fs::perms::all_exe |
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

} // namespace

LogicalResult emitNativeOutput(ModuleOp module,
                               const NativeOutputOptions &options) {
  std::string targetError;
  std::unique_ptr<TargetMachine> targetMachine =
      createTargetMachine(targetError);
  if (!targetMachine) {
    errs() << "obelisk: error: could not create generic x86-64 target: "
           << targetError << '\n';
    return failure();
  }
  if (failed(lowerToLLVM(module, *targetMachine)))
    return failure();

  registerLLVMDialectTranslation(*module.getContext());
  registerBuiltinDialectTranslation(*module.getContext());
  llvm::LLVMContext llvmContext;
  std::unique_ptr<llvm::Module> llvmModule =
      translateModuleToLLVMIR(module, llvmContext, "obelisk");
  if (!llvmModule) {
    errs() << "obelisk: error: LLVM dialect translation failed\n";
    return failure();
  }
  llvmModule->setTargetTriple(Triple(kTargetTriple));
  llvmModule->setDataLayout(targetMachine->createDataLayout());
  if (failed(optimizeLLVMModule(*llvmModule, *targetMachine)))
    return failure();

  if (options.kind == NativeOutputKind::LLVMIR) {
    std::error_code error;
    raw_fd_ostream output(options.outputPath, error, sys::fs::OF_Text);
    if (error) {
      errs() << "obelisk: error: could not open LLVM IR output '"
             << options.outputPath << "': " << error.message() << '\n';
      return failure();
    }
    llvmModule->print(output, nullptr);
    output.flush();
    if (output.has_error()) {
      errs() << "obelisk: error: failed while writing LLVM IR output '"
             << options.outputPath << "': " << output.error().message() << '\n';
      output.clear_error();
      return failure();
    }
    return success();
  }

  FailureOr<SmallString<256>> objectTemporary =
      makeTemporaryBeside(options.outputPath, ".o");
  if (failed(objectTemporary)) {
    errs() << "obelisk: error: could not create temporary object\n";
    return failure();
  }
  if (failed(writeObject(*llvmModule, *targetMachine, *objectTemporary))) {
    sys::fs::remove(*objectTemporary);
    return failure();
  }
  if (options.kind == NativeOutputKind::Object) {
    if (failed(atomicallyReplace(*objectTemporary, options.outputPath))) {
      sys::fs::remove(*objectTemporary);
      return failure();
    }
    return success();
  }

  std::optional<std::string> support =
      findNativeSupport(options.executablePath);
  if (!support) {
    errs() << "obelisk: error: native support tree was not found relative to '"
           << options.executablePath << "'\n";
    sys::fs::remove(*objectTemporary);
    return failure();
  }
  LogicalResult linked = linkExecutable(*objectTemporary, options.outputPath,
                                        *support, options.explicitSysroot);
  sys::fs::remove(*objectTemporary);
  return linked;
}

} // namespace obelisk::driver
