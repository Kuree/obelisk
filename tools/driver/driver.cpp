//===- driver.cpp - Obelisk compiler driver -------------------------------===//
//
// This executable owns compilation policy: source files, command files,
// include paths, macros, libraries, language revision, and the selected output
// action.
//
//===----------------------------------------------------------------------===//

#include "NativeInputs.h"
#include "Options.h"
#include "TargetBackend.h"

#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Conversion/SlangToObelisk.h"
#include "obelisk/Dialect/Obelisk/ObeliskDialect.h"
#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Simulation/SimulationDialect.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Slang/SlangDialect.h"
#include "obelisk/Frontend/Frontend.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/PassManager.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Process.h"
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

// Command files are the delivery format for real testbenches, and every other
// tool treats them as "my whole command line, in a file" rather than a
// frontend-only filelist. Expanding them into the driver's own argv before
// option parsing is what makes that true here: a `.f` may carry any obelisk
// option, not just the ones slang happens to understand. The frontend still
// receives everything, because buildSlangArguments() reconstructs slang's
// command line from the parsed options rather than forwarding the file.
//
// Tokenization mirrors slang's own command-file lexer (`#`, `//` and `/*`
// comments, `$VAR` expansion, backslash escapes, single and double quotes) so
// that a file accepted before is tokenized identically now.
static void tokenizeCommandFile(StringRef text, StringSaver &saver,
                                SmallVectorImpl<const char *> &tokens) {
  std::string current;
  bool pending = false;
  auto finish = [&]() {
    if (pending) {
      tokens.push_back(saver.save(StringRef(current)).data());
      current.clear();
      pending = false;
    }
  };

  const char *ptr = text.begin();
  const char *end = text.end();
  while (ptr != end) {
    char c = *ptr++;
    if (isSpace(static_cast<unsigned char>(c)) || c == '\0') {
      finish();
      continue;
    }

    // A '#' always starts a comment; '/' only when not already mid-argument,
    // so that path components like `a//b` survive.
    if (c == '#') {
      finish();
      while (ptr != end && *ptr != '\n' && *ptr != '\r')
        ++ptr;
      continue;
    }
    if (c == '/' && !pending && ptr != end) {
      if (*ptr == '/') {
        ++ptr;
        while (ptr != end && *ptr != '\n' && *ptr != '\r')
          ++ptr;
        continue;
      }
      if (*ptr == '*') {
        ++ptr;
        while (ptr != end) {
          char inner = *ptr++;
          if (inner == '*' && ptr != end && *ptr == '/') {
            ++ptr;
            break;
          }
        }
        continue;
      }
    }

    if (c == '$' && ptr != end) {
      // ${NAME} and $NAME both expand; an unset variable expands to nothing,
      // matching slang.
      const char *nameStart = ptr;
      bool braced = *ptr == '{';
      if (braced)
        ++nameStart;
      const char *scan = nameStart;
      while (scan != end &&
             (isAlnum(static_cast<unsigned char>(*scan)) || *scan == '_'))
        ++scan;
      if (scan != nameStart && (!braced || (scan != end && *scan == '}'))) {
        StringRef name(nameStart, scan - nameStart);
        ptr = braced ? scan + 1 : scan;
        if (std::optional<std::string> value = sys::Process::GetEnv(name)) {
          current.append(*value);
          pending = true;
        }
        continue;
      }
    }

    if (c == '\\') {
      if (ptr != end && *ptr != '\n' && *ptr != '\r') {
        current.push_back(*ptr++);
        pending = true;
      }
      continue;
    }

    // Any non-whitespace character starts an argument, so that a quoted empty
    // string still produces a token.
    pending = true;

    if (c == '\'') {
      while (ptr != end && *ptr != '\'')
        current.push_back(*ptr++);
      if (ptr != end)
        ++ptr;
      continue;
    }
    if (c == '"') {
      while (ptr != end && *ptr != '"') {
        char inner = *ptr++;
        if (inner == '\\' && ptr != end)
          inner = *ptr++;
        current.push_back(inner);
      }
      if (ptr != end)
        ++ptr;
      continue;
    }
    current.push_back(c);
  }
  finish();
}

/// Splices the contents of every `-f`/`--filelist` command file into `argv`,
/// recursively. Returns false after reporting a read error or a cycle.
///
/// This runs before option parsing, so it matches `-f` lexically rather than
/// semantically: a literal `-f` supplied as some other option's separate value
/// (`-D -f`) would be taken as a command file. Response-file expansion has the
/// same ambiguity everywhere it exists, and the alternative is parsing twice.
static bool expandCommandFiles(SmallVectorImpl<const char *> &argv,
                               StringSaver &saver,
                               SmallVectorImpl<std::string> &activeFiles) {
  // A testbench that includes a shared `.f` twice is legitimate; only a file
  // that (transitively) includes itself is an error, so track the active
  // chain rather than every file ever visited.
  static constexpr unsigned kMaxDepth = 64;
  if (activeFiles.size() > kMaxDepth) {
    emitDriverError("command files nested more than " + Twine(kMaxDepth) +
                    " levels deep");
    return false;
  }

  SmallVector<const char *> expanded;
  for (size_t index = 0, size = argv.size(); index != size; ++index) {
    StringRef argument(argv[index]);
    if (argument != "-f" && argument != "--filelist") {
      expanded.push_back(argv[index]);
      continue;
    }
    if (index + 1 == size) {
      emitDriverError("missing argument to '" + argument +
                      "' (expected a command file)");
      return false;
    }
    StringRef path(argv[++index]);

    SmallString<256> canonical(path);
    if (std::error_code error = sys::fs::real_path(path, canonical))
      canonical = path;
    if (llvm::is_contained(activeFiles, StringRef(canonical))) {
      emitDriverError("command file '" + path + "' includes itself");
      return false;
    }

    ErrorOr<std::unique_ptr<MemoryBuffer>> buffer =
        MemoryBuffer::getFile(path, /*IsText=*/true);
    if (!buffer) {
      emitDriverError("could not read command file '" + path +
                      "': " + buffer.getError().message());
      return false;
    }

    SmallVector<const char *> nested;
    tokenizeCommandFile((*buffer)->getBuffer(), saver, nested);
    activeFiles.emplace_back(canonical);
    bool expandedNested = expandCommandFiles(nested, saver, activeFiles);
    activeFiles.pop_back();
    if (!expandedNested)
      return false;
    expanded.append(nested.begin(), nested.end());
  }

  argv.assign(expanded.begin(), expanded.end());
  return true;
}

// The compute graph is deliberately operation-independent, but the schedule
// inspector can still relate a fragment to the source location retained by
// its control-flow boundary or owning code unit. Keep this provenance next to
// the diagnostic instead of adding it to the versioned runtime graph schema.
static void printScheduleSourceLocations(obelisk::sim::SimDesignOp design,
                                         obelisk::sim::ComputeGraphAttr graph,
                                         raw_ostream &output) {
  SymbolTable symbols(design);
  bool first = true;
  output << " source_locations = [";
  for (Attribute node : graph.getNodes()) {
    auto fragment = dyn_cast<obelisk::sim::ComputeFragmentAttr>(node);
    if (!fragment)
      continue;
    auto function = symbols.lookup<obelisk::sim::SimFuncOp>(
        fragment.getFunction().getValue());
    if (!function)
      continue;
    Block *block = obelisk::analysis::lookupComputeGraphBlock(
        function, fragment.getBlock());
    Location source =
        block ? block->getTerminator()->getLoc() : function.getLoc();
    auto location = source->findInstanceOf<FileLineColLoc>();
    if (!location)
      location = function.getLoc()->findInstanceOf<FileLineColLoc>();
    if (!location)
      continue;
    if (!first)
      output << ", ";
    first = false;
    output << '#' << fragment.getId() << " = ";
    location.getFilename().print(output);
    output << ':' << location.getLine() << ':' << location.getColumn();
  }
  output << ']';
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
  return DPIHeaderType{obelisk::getDPICTypeSpelling(*abi).str(),
                       abi->isVector()};
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
      auto formal = dyn_cast<obelisk::ir::SVFormalArgumentSymbolOp>(child);
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

    bool task = op.getSubroutineKind() == obelisk::ir::SVSubroutineKind::Task;
    std::string returnType = task ? "int" : "";
    if (!task) {
      auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
      auto subroutine =
          semanticType
              ? dyn_cast<obelisk::ir::SubroutineType>(semanticType.getValue())
              : obelisk::ir::SubroutineType{};
      auto signature = subroutine
                           ? dyn_cast<FunctionType>(subroutine.getSignature())
                           : FunctionType{};
      if (!signature || signature.getNumResults() != 1) {
        op.emitError("DPI function has no resolved result signature");
        return WalkResult::interrupt();
      }
      if (isa<obelisk::ir::VoidType>(signature.getResult(0))) {
        returnType = "void";
      } else {
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
    auto inserted = prototypes.try_emplace(cIdentifier.getValue(), prototype);
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
  // Command files were already spliced into argv, so anything they contributed
  // is present here as an ordinary input.
  if (inputs.empty()) {
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
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  if (compilerThreads && *compilerThreads != 1) {
    emitDriverError(
        "--compile-threads must be 1 in a single-threaded wasm compiler");
    valid = false;
  }
  // Construct MLIRContext with threading disabled. Its native default creates
  // a worker pool before any pass runs, which Emscripten cannot provide in a
  // module built without pthread support.
  compilerThreads = 1;
#endif
  StringRef vpiMode = args.getLastArgValue(OPT_vpi_EQ, "off");
  if (vpiMode != "off" && vpiMode != "read" && vpiMode != "full") {
    emitDriverError(Twine("unsupported VPI mode '") + vpiMode +
                    "'; expected off, read, or full");
    valid = false;
  }
  // The default is whichever target this build can produce; a wasm build of
  // the compiler has no x86-64 backend linked in.
  StringRef targetName =
      args.getLastArgValue(OPT_target_EQ, OBELISK_DEFAULT_TARGET);
  if (targetName != "native" && targetName != "wasm64") {
    emitDriverError(Twine("unsupported target '") + targetName +
                    "'; expected native or wasm64");
    valid = false;
  }
  StringRef executionTier =
      args.getLastArgValue(OPT_execution_tier_EQ, "native");
  if (executionTier != "native" && executionTier != "bytecode") {
    emitDriverError(Twine("unsupported execution tier '") + executionTier +
                    "'; expected native or bytecode");
    valid = false;
  }
  StringRef nativeScheduler =
      args.getLastArgValue(OPT_native_scheduler_EQ, "auto");
  if (!obelisk::sim::symbolizeNativeSchedulerMode(nativeScheduler)) {
    emitDriverError(Twine("unsupported native scheduler '") + nativeScheduler +
                    "'; expected auto, generic, aot, or eval");
    valid = false;
  }
  StringRef staticSpecialization =
      args.getLastArgValue(OPT_static_specialization_EQ, "auto");
  if (staticSpecialization != "auto" && staticSpecialization != "off" &&
      staticSpecialization != "on") {
    emitDriverError(Twine("unsupported static specialization '") +
                    staticSpecialization + "'; expected auto, off, or on");
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
  frontendOptions.numThreads = resolvedCompilerThreads;

  const Arg *action = args.getLastArg(OPT_E, OPT_emit_slang, OPT_emit_obelisk,
                                      OPT_emit_sim, OPT_emit_schedule, OPT_c,
                                      OPT_emit_llvm, OPT_emit_dpi_header);
  bool preprocess = action && action->getOption().matches(OPT_E);
  bool emitSlang = action && action->getOption().matches(OPT_emit_slang);
  bool emitSim = action && action->getOption().matches(OPT_emit_sim);
  bool emitSchedule = action && action->getOption().matches(OPT_emit_schedule);
  bool emitObject = action && action->getOption().matches(OPT_c);
  bool emitLLVM = action && action->getOption().matches(OPT_emit_llvm);
  bool emitDPIHeader =
      action && action->getOption().matches(OPT_emit_dpi_header);
  bool native = !action || emitObject || emitLLVM;
  if (native && requestedWorkers.value_or(1) != 1) {
    emitDriverError(
        "native executable generation currently requires --threads=1");
    valid = false;
  }
  if (!native && executionTier != "native") {
    emitDriverError(
        "--execution-tier is only valid for native executable generation");
    valid = false;
  }
  if (!native && args.hasArg(OPT_native_scheduler_EQ)) {
    emitDriverError(
        "--native-scheduler is only valid for native executable generation");
    valid = false;
  }
  if (!valid)
    return 1;

  obelisk::driver::ClassifiedInputs classifiedInputs;
  if (failed(obelisk::driver::classifyDirectInputs(inputs, action == nullptr,
                                                   vpiMode, classifiedInputs)))
    return 1;
  if (classifiedInputs.systemVerilog.empty()) {
    emitDriverError(
        "at least one SystemVerilog input or command file is required");
    return 1;
  }
  inputs.assign(classifiedInputs.systemVerilog.begin(),
                classifiedInputs.systemVerilog.end());

  if (preprocess) {
    FailureOr<std::string> preprocessed =
        obelisk::frontend::preprocessSystemVerilog(inputs, frontendOptions);
    if (failed(preprocessed))
      return 1;
    std::string outputFilename = args.getLastArgValue(OPT_o, "-").str();
    std::error_code error;
    ToolOutputFile output(outputFilename, error, sys::fs::OF_None);
    if (error) {
      emitDriverError(Twine("could not open output '") + outputFilename +
                      "': " + error.message());
      return 1;
    }
    output.os() << *preprocessed;
    output.keep();
    return 0;
  }

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

  if (native) {
    obelisk::sim::NativeSchedulerMode pipelineScheduler =
        *obelisk::sim::symbolizeNativeSchedulerMode(nativeScheduler);
    if (pipelineScheduler == obelisk::sim::NativeSchedulerMode::Auto) {
      (*module)->setAttr("obelisk.native_scheduler.auto_requested",
                         UnitAttr::get(&context));
      pipelineScheduler = executionTier == "bytecode"
                              ? obelisk::sim::NativeSchedulerMode::Generic
                              : obelisk::sim::NativeSchedulerMode::Eval;
    }
    (*module)->setAttr("obelisk.native_scheduler",
                       obelisk::sim::NativeSchedulerModeAttr::get(
                           &context, pipelineScheduler));
  }

  if (!emitSlang) {
    PassManager passManager(&context);
    if (args.hasArg(OPT_mlir_timing))
      passManager.enableTiming();
    passManager.addPass(obelisk::createConvertSlangToObeliskPass());
    if (emitSim || emitSchedule || native)
      obelisk::buildObeliskToSimulationPipeline(
          passManager, requestedWorkers.value_or(1), vpiMode, optLevel,
          staticSpecialization);
    if (failed(passManager.run(*module)))
      return 1;
  }

  if (native) {
    obelisk::driver::NativeOutputOptions nativeOptions;
    nativeOptions.kind = emitObject ? obelisk::driver::NativeOutputKind::Object
                         : emitLLVM
                             ? obelisk::driver::NativeOutputKind::LLVMIR
                             : obelisk::driver::NativeOutputKind::Executable;
    nativeOptions.outputPath = args.getLastArgValue(OPT_o, emitObject ? "a.o"
                                                           : emitLLVM ? "-"
                                                                      : "a.out")
                                   .str();
    nativeOptions.explicitSysroot = args.getLastArgValue(OPT_sysroot_EQ).str();
    nativeOptions.executablePath = driverExecutablePath;
    nativeOptions.nativeLinkInputs =
        std::move(classifiedInputs.nativeLinkInputs);
    nativeOptions.sharedLibraryInputs =
        std::move(classifiedInputs.sharedLibraries);
    nativeOptions.vpi = vpiMode.str();
    nativeOptions.nativeScheduler = nativeScheduler.str();
    nativeOptions.thinLTOCacheDir =
        args.getLastArgValue(OPT_thinlto_cache_dir_EQ).str();
    nativeOptions.bytecode = executionTier == "bytecode";
    nativeOptions.optLevel = optLevel;
    nativeOptions.noLTO = args.hasFlag(OPT_fno_lto, OPT_flto, false);
    nativeOptions.timing = args.hasArg(OPT_mlir_timing);
    nativeOptions.compileThreads = resolvedCompilerThreads;
    nativeOptions.target = targetName == "wasm64"
                               ? obelisk::driver::TargetKind::Wasm
                               : obelisk::driver::TargetKind::Native;
    return succeeded(obelisk::driver::emitTargetOutput(*module, nativeOptions))
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
      obelisk::sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
      if (!graph) {
        emitDriverError("simulation lowering produced no compute graph");
        return 1;
      }
      Attribute(graph).print(output.os());
      if (args.hasArg(OPT_mlir_print_debuginfo))
        printScheduleSourceLocations(design, graph, output.os());
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

  SmallVector<const char *> arguments(argv, argv + argc);
  SmallVector<std::string> activeCommandFiles;
  if (!expandCommandFiles(arguments, saver, activeCommandFiles))
    return 1;

  bool parseFailed = false;
  InputArgList args =
      optionTable.parseArgs(static_cast<int>(arguments.size()),
                            const_cast<char *const *>(arguments.data()),
                            OPT_UNKNOWN, saver, [&](StringRef msg) {
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

  int status = executeCompilation(args);
  // A reusable Emscripten module does not exit after callMain(), so its libc
  // streams do not get the process-exit flush a native invocation receives.
  // Flush explicitly to deliver linker diagnostics to print/printErr.
  outs().flush();
  errs().flush();
  return status;
}
