//===- driver.cpp - Obelisk compiler driver -------------------------------===//
//
// This executable owns compilation policy: source files, command files,
// include paths, macros, libraries, language revision, and the selected output
// action.
//
//===----------------------------------------------------------------------===//

#include "Options.h"
#include "NativeBackend.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Conversion/SlangToObelisk.h"
#include "obelisk/Dialect/Obelisk/ObeliskDialect.h"
#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Simulation/SimulationDialect.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Slang/SlangDialect.h"
#include "obelisk/Frontend/Frontend.h"

#include "mlir/IR/AsmState.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/PassManager.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::opt;
using namespace mlir;
using namespace obelisk::driver::options;

namespace {

static std::string driverExecutablePath;

static void emitDriverError(const Twine &message) {
  WithColor::error(errs(), "obelisk") << message << '\n';
}

static bool parseUnsignedOption(const ArgList &args, OptSpecifier option,
                                StringRef spelling,
                                std::optional<uint32_t> &result) {
  const Arg *arg = args.getLastArg(option);
  if (!arg)
    return true;
  uint32_t value;
  if (StringRef(arg->getValue()).getAsInteger(10, value)) {
    emitDriverError(Twine("invalid value '") + arg->getValue() + "' for " +
                    spelling);
    return false;
  }
  result = value;
  return true;
}

struct DPIHeaderType {
  std::string spelling;
  bool vector = false;
};

static FailureOr<DPIHeaderType> getDPIHeaderType(mlir::Type type,
                                                 Location location) {
  FailureOr<obelisk::DPIABIType> abi =
      obelisk::classifyDPIABIType(type, location);
  if (failed(abi))
    return failure();
  return DPIHeaderType{
      obelisk::getDPICTypeSpelling(*abi).str(), abi->isVector()};
}

static LogicalResult writeDPIHeader(ModuleOp module, raw_ostream &output) {
  llvm::StringMap<std::string> prototypes;
  WalkResult walked = module.walk([&](obelisk::ir::SVSubroutineSymbolOp op) {
    if (!op.getIsDpiImport().value_or(false))
      return WalkResult::advance();
    StringAttr cIdentifier = op.getDpiCIdentifierAttr();
    if (!cIdentifier) {
      op.emitError("DPI import has no resolved C identifier");
      return WalkResult::interrupt();
    }
    SmallVector<std::string> arguments;
    unsigned argumentIndex = 0;
    for (Operation &child : op.getRegion().front()) {
      auto formal =
          dyn_cast<obelisk::ir::SVFormalArgumentSymbolOp>(child);
      if (!formal)
        continue;
      std::optional<mlir::Type> semanticType = formal.getSemanticType();
      if (!semanticType) {
        formal.emitError("DPI formal has no semantic type");
        return WalkResult::interrupt();
      }
      FailureOr<DPIHeaderType> type =
          getDPIHeaderType(*semanticType, formal.getLoc());
      if (failed(type))
        return WalkResult::interrupt();
      auto direction = formal.getDirection();
      bool input = direction == obelisk::ir::SVArgumentDirection::In;
      bool pointer = !input || type->vector;
      std::string declaration;
      if (input && type->vector)
        declaration += "const ";
      declaration += type->spelling;
      declaration += pointer ? " *" : " ";
      declaration += ("arg" + Twine(argumentIndex++)).str();
      arguments.push_back(std::move(declaration));
    }

    bool task =
        op.getSubroutineKind() == obelisk::ir::SVSubroutineKind::Task;
    std::string returnType = task ? "int" : "";
    if (!task) {
      auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
      auto subroutine =
          semanticType
              ? dyn_cast<obelisk::ir::SubroutineType>(
                    semanticType.getValue())
              : obelisk::ir::SubroutineType{};
      auto signature =
          subroutine
              ? dyn_cast<FunctionType>(subroutine.getSignature())
              : FunctionType{};
      if (!signature || signature.getNumResults() != 1) {
        op.emitError("DPI function has no resolved result signature");
        return WalkResult::interrupt();
      }
      FailureOr<DPIHeaderType> result =
          getDPIHeaderType(signature.getResult(0), op.getLoc());
      if (failed(result))
        return WalkResult::interrupt();
      if (result->vector) {
        returnType = "void";
        arguments.insert(arguments.begin(), result->spelling + " *result");
      } else {
        returnType = result->spelling;
      }
    }
    std::string prototype =
        returnType + " " + cIdentifier.getValue().str() + "(";
    if (arguments.empty()) {
      prototype += "void";
    } else {
      for (auto [index, argument] : llvm::enumerate(arguments)) {
        if (index)
          prototype += ", ";
        prototype += argument;
      }
    }
    prototype += ");";
    auto inserted =
        prototypes.try_emplace(cIdentifier.getValue(), prototype);
    if (!inserted.second && inserted.first->second != prototype) {
      op.emitError() << "C identifier '" << cIdentifier.getValue()
                     << "' is imported with incompatible DPI signatures";
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });
  if (walked.wasInterrupted())
    return failure();
  output << "#ifndef OBELISK_GENERATED_DPI_H\n"
            "#define OBELISK_GENERATED_DPI_H\n\n"
            "#include <stdint.h>\n"
            "#include <svdpi.h>\n\n"
            "#ifdef __cplusplus\n"
            "extern \"C\" {\n"
            "#endif\n\n";
  SmallVector<StringRef> names;
  names.reserve(prototypes.size());
  for (auto &entry : prototypes)
    names.push_back(entry.getKey());
  llvm::sort(names);
  for (StringRef name : names)
    output << prototypes.lookup(name) << '\n';
  output << "\n#ifdef __cplusplus\n"
            "}\n"
            "#endif\n\n"
            "#endif\n";
  return success();
}

static obelisk::frontend::FrontendOptions
buildFrontendOptions(const InputArgList &args, bool &valid) {
  obelisk::frontend::FrontendOptions options;
  options.includeDirs = args.getAllArgValues(OPT_I);
  options.includeSystemDirs = args.getAllArgValues(OPT_isystem);
  options.defines = args.getAllArgValues(OPT_D);
  options.undefines = args.getAllArgValues(OPT_U);
  options.commandFiles = args.getAllArgValues(OPT_f);
  options.libDirs = args.getAllArgValues(OPT_y);
  options.libExts = args.getAllArgValues(OPT_Y);
  options.libraryFiles = args.getAllArgValues(OPT_l);
  options.topModules = args.getAllArgValues(OPT_top_EQ);
  options.paramOverrides = args.getAllArgValues(OPT_G);
  options.warningOptions = args.getAllArgValues(OPT_W);
  options.suppressWarningsPaths =
      args.getAllArgValues(OPT_suppress_warnings_EQ);

  options.singleUnit = args.hasArg(OPT_single_unit);
  options.librariesInheritMacros = args.hasArg(OPT_libraries_inherit_macros);
  options.allowUseBeforeDeclare = args.hasArg(OPT_allow_use_before_declare);
  options.ignoreUnknownModules = args.hasArg(OPT_ignore_unknown_modules);
  if (const Arg *arg = args.getLastArg(OPT_timescale_EQ))
    options.timeScale = arg->getValue();

  valid &= parseUnsignedOption(args, OPT_max_include_depth_EQ,
                               "--max-include-depth", options.maxIncludeDepth);
  valid &= parseUnsignedOption(args, OPT_error_limit_EQ, "--error-limit",
                               options.errorLimit);

  StringRef standard = args.getLastArgValue(OPT_std_EQ, "1800-2023");
  if (standard != "1800-2017" && standard != "1800-2023") {
    emitDriverError(Twine("unsupported SystemVerilog revision '") + standard +
                    "'; expected 1800-2017 or 1800-2023");
    valid = false;
  }
  options.languageVersion =
      standard == "1800-2017"
          ? obelisk::frontend::LanguageVersion::IEEE1800_2017
          : obelisk::frontend::LanguageVersion::IEEE1800_2023;
  for (std::string argument : args.getAllArgValues(OPT_Xslang))
    options.slangArgs.push_back(std::move(argument));
  return options;
}

static int executeCompilation(const InputArgList &args) {
  SmallVector<std::string> inputs;
  for (const Arg *arg : args.filtered(OPT_INPUT))
    inputs.emplace_back(arg->getValue());
  for (const Arg *arg : args.filtered(OPT__DASH_DASH))
    for (const char *value : arg->getValues())
      inputs.emplace_back(value);

  bool valid = true;
  obelisk::frontend::FrontendOptions frontendOptions =
      buildFrontendOptions(args, valid);
  if (inputs.empty() && frontendOptions.commandFiles.empty()) {
    emitDriverError("no input files");
    valid = false;
  }
  if (std::count(inputs.begin(), inputs.end(), "-") > 1) {
    emitDriverError("standard input may only appear once");
    valid = false;
  }
  if (!valid)
    return 1;

  std::optional<uint32_t> requestedWorkers;
  valid &=
      parseUnsignedOption(args, OPT_threads_EQ, "--threads", requestedWorkers);
  if (requestedWorkers && *requestedWorkers == 0) {
    emitDriverError("--threads must be greater than zero");
    valid = false;
  }
  if (requestedWorkers && *requestedWorkers > 65535) {
    emitDriverError("--threads exceeds the generated lane ID limit (65535)");
    valid = false;
  }
  std::optional<uint32_t> compilerThreads;
  valid &= parseUnsignedOption(args, OPT_compile_threads_EQ,
                               "--compile-threads", compilerThreads);
  if (compilerThreads && *compilerThreads == 0) {
    emitDriverError("--compile-threads must be greater than zero");
    valid = false;
  }
  StringRef vpiMode = args.getLastArgValue(OPT_vpi_EQ, "off");
  if (vpiMode != "off" && vpiMode != "read" && vpiMode != "full") {
    emitDriverError(Twine("unsupported VPI mode '") + vpiMode +
                    "'; expected off, read, or full");
    valid = false;
  }
  StringRef executionTier =
      args.getLastArgValue(OPT_execution_tier_EQ, "native");
  if (executionTier != "native" && executionTier != "bytecode") {
    emitDriverError(Twine("unsupported execution tier '") + executionTier +
                    "'; expected native or bytecode");
    valid = false;
  }
  uint32_t optLevel = 3;
  if (const Arg *optimization =
          args.getLastArg(OPT_O0, OPT_O1, OPT_O2, OPT_O3)) {
    if (optimization->getOption().matches(OPT_O0))
      optLevel = 0;
    else if (optimization->getOption().matches(OPT_O1))
      optLevel = 1;
    else if (optimization->getOption().matches(OPT_O2))
      optLevel = 2;
  }
  if (!valid)
    return 1;
  uint32_t resolvedCompilerThreads = compilerThreads.value_or(
      std::max(1u, llvm::hardware_concurrency().compute_thread_count()));

  const Arg *action = args.getLastArg(OPT_emit_slang, OPT_emit_obelisk,
                                      OPT_emit_sim, OPT_emit_schedule, OPT_c,
                                      OPT_emit_llvm, OPT_emit_dpi_header);
  bool emitSlang = action && action->getOption().matches(OPT_emit_slang);
  bool emitSim = action && action->getOption().matches(OPT_emit_sim);
  bool emitSchedule = action && action->getOption().matches(OPT_emit_schedule);
  bool emitObject = action && action->getOption().matches(OPT_c);
  bool emitLLVM = action && action->getOption().matches(OPT_emit_llvm);
  bool emitDPIHeader =
      action && action->getOption().matches(OPT_emit_dpi_header);
  bool native = !action || emitObject || emitLLVM;
  if (native && requestedWorkers.value_or(1) != 1) {
    emitDriverError("native executable generation currently requires --threads=1");
    valid = false;
  }
  if (native && vpiMode != "off") {
    emitDriverError("native executable generation currently requires --vpi=off");
    valid = false;
  }
  std::vector<std::string> dpiLinkInputs =
      args.getAllArgValues(OPT_dpi_link_EQ);
  if (!dpiLinkInputs.empty() &&
      (!native || emitObject || emitLLVM)) {
    emitDriverError(
        "--dpi-link is only valid when linking a native executable");
    valid = false;
  }
  if (!native && executionTier != "native") {
    emitDriverError(
        "--execution-tier is only valid for native executable generation");
    valid = false;
  }
  if (!valid)
    return 1;

  DialectRegistry registry;
  registry.insert<obelisk::slangir::SlangDialect, obelisk::ir::ObeliskDialect,
                  obelisk::runtime::ObeliskRuntimeDialect,
                  obelisk::sim::ObeliskSimulationDialect,
                  mlir::LLVM::LLVMDialect>();
  // One explicitly sized pool is shared by all MLIR parallel pass adaptors.
  // Its lifetime encloses the context as required by MLIRContext.
  std::unique_ptr<llvm::DefaultThreadPool> compilerPool;
  MLIRContext context(registry, compilerThreads
                                    ? MLIRContext::Threading::DISABLED
                                    : MLIRContext::Threading::ENABLED);
  if (compilerThreads) {
    if (*compilerThreads > 1) {
      llvm::ThreadPoolStrategy strategy =
          llvm::hardware_concurrency(*compilerThreads);
      strategy.Limit = true;
      compilerPool = std::make_unique<llvm::DefaultThreadPool>(strategy);
      context.setThreadPool(*compilerPool);
    }
  }
  context.loadAllAvailableDialects();

  auto importedModule =
      obelisk::frontend::importSystemVerilog(inputs, context, frontendOptions);
  if (failed(importedModule))
    return 1;
  OwningOpRef<ModuleOp> module = std::move(*importedModule);

  if (!emitSlang) {
    PassManager passManager(&context);
    passManager.addPass(obelisk::createConvertSlangToObeliskPass());
    if (emitSim || emitSchedule || native)
      obelisk::buildObeliskToSimulationPipeline(
          passManager, requestedWorkers.value_or(1), vpiMode, optLevel);
    if (failed(passManager.run(*module)))
      return 1;
  }

  if (native) {
    obelisk::driver::NativeOutputOptions nativeOptions;
    nativeOptions.kind = emitObject
                             ? obelisk::driver::NativeOutputKind::Object
                         : emitLLVM
                             ? obelisk::driver::NativeOutputKind::LLVMIR
                             : obelisk::driver::NativeOutputKind::Executable;
    nativeOptions.outputPath =
        args.getLastArgValue(OPT_o,
                             emitObject ? "a.o" : emitLLVM ? "-" : "a.out")
            .str();
    nativeOptions.explicitSysroot =
        args.getLastArgValue(OPT_sysroot_EQ).str();
    nativeOptions.executablePath = driverExecutablePath;
    nativeOptions.dpiLinkInputs.assign(dpiLinkInputs.begin(),
                                       dpiLinkInputs.end());
    nativeOptions.bytecode = executionTier == "bytecode";
    nativeOptions.optLevel = optLevel;
    nativeOptions.compileThreads = resolvedCompilerThreads;
    return succeeded(obelisk::driver::emitNativeOutput(*module, nativeOptions))
               ? 0
               : 1;
  }

  std::string outputFilename = args.getLastArgValue(OPT_o, "-").str();
  std::error_code error;
  ToolOutputFile output(outputFilename, error, sys::fs::OF_None);
  if (error) {
    emitDriverError(Twine("could not open output '") + outputFilename +
                    "': " + error.message());
    return 1;
  }

  if (emitDPIHeader) {
    if (failed(writeDPIHeader(*module, output.os())))
      return 1;
  } else if (emitSchedule) {
    for (obelisk::sim::SimDesignOp design :
         module->getBody()->getOps<obelisk::sim::SimDesignOp>()) {
      output.os() << "schedule @" << design.getSymName() << ' ';
      Attribute graph = design.getComputeGraphAttr();
      if (!graph) {
        emitDriverError("simulation lowering produced no compute graph");
        return 1;
      }
      graph.print(output.os());
      output.os() << '\n';
    }
  } else {
    OpPrintingFlags printingFlags;
    if (args.hasArg(OPT_mlir_print_debuginfo))
      printingFlags.enableDebugInfo();
    module->print(output.os(), printingFlags);
    output.os() << '\n';
  }
  output.keep();
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  InitLLVM initLLVM(argc, argv);
  driverExecutablePath =
      sys::fs::getMainExecutable(argv[0], reinterpret_cast<void *>(&main));
  const OptTable &optionTable = obelisk::driver::getDriverOptTable();

  BumpPtrAllocator allocator;
  StringSaver saver(allocator);
  bool parseFailed = false;
  InputArgList args =
      optionTable.parseArgs(argc, argv, OPT_UNKNOWN, saver, [&](StringRef msg) {
        emitDriverError(msg);
        parseFailed = true;
      });
  if (parseFailed)
    return 1;

  if (args.hasArg(OPT_help) || args.hasArg(OPT_help_hidden)) {
    optionTable.printHelp(outs(), "obelisk [options] <input files>",
                          "Obelisk ahead-of-time SystemVerilog compiler",
                          args.hasArg(OPT_help_hidden));
    return 0;
  }
  if (args.hasArg(OPT_version)) {
    outs() << "obelisk version " << OBELISK_VERSION_STRING << '\n'
           << obelisk::frontend::getSlangVersion() << '\n';
    return 0;
  }
  if (args.hasArg(OPT_print_resource_dir)) {
    outs() << OBELISK_RESOURCE_DIR << '\n';
    return 0;
  }

  return executeCompilation(args);
}
