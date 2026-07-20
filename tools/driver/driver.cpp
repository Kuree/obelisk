//===- driver.cpp - Obelisk compiler driver -------------------------------===//
//
// This executable owns compilation policy: source files, command files,
// include paths, macros, libraries, language revision, and the selected output
// action. Representation-only tools such as obelisk-translate remain separate.
//
//===----------------------------------------------------------------------===//

#include "Options.h"

#include "obelisk/Conversion/MooreToObelisk.h"
#include "obelisk/Dialect/Sim/ObeliskDialect.h"
#include "obelisk/Frontend/Frontend.h"

#include "circt/InitAllDialects.h"

#include "mlir/IR/AsmState.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/Timing.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::opt;
using namespace mlir;
using namespace obelisk::driver::options;

namespace {

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

static circt::ImportVerilogOptions
buildFrontendOptions(const InputArgList &args, bool &valid) {
  circt::ImportVerilogOptions options;
  options.mode = circt::ImportVerilogOptions::Mode::Full;
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

  if (args.hasArg(OPT_single_unit))
    options.singleUnit = true;
  if (args.hasArg(OPT_libraries_inherit_macros))
    options.librariesInheritMacros = true;
  if (args.hasArg(OPT_allow_use_before_declare))
    options.allowUseBeforeDeclare = true;
  if (args.hasArg(OPT_ignore_unknown_modules))
    options.ignoreUnknownModules = true;
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
  options.slangArgs.emplace_back("--std");
  options.slangArgs.emplace_back(standard);
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
  circt::ImportVerilogOptions frontendOptions =
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

  DialectRegistry registry;
  circt::registerAllDialects(registry);
  registry.insert<obelisk::ir::ObeliskDialect>();
  MLIRContext context(registry);
  context.loadAllAvailableDialects();

  DefaultTimingManager timingManager;
  TimingScope timingScope = timingManager.getRootScope();
  auto module = obelisk::frontend::importSystemVerilog(
      inputs, context, timingScope, frontendOptions);
  if (failed(module))
    return 1;

  const Arg *action = args.getLastArg(OPT_emit_moore, OPT_emit_obelisk);
  bool emitMoore = action && action->getOption().matches(OPT_emit_moore);
  if (!emitMoore) {
    PassManager passManager(&context);
    passManager.addPass(obelisk::createConvertMooreToObeliskPass());
    if (failed(passManager.run(**module)))
      return 1;
  }

  std::string outputFilename = args.getLastArgValue(OPT_o, "-").str();
  std::error_code error;
  ToolOutputFile output(outputFilename, error, sys::fs::OF_None);
  if (error) {
    emitDriverError(Twine("could not open output '") + outputFilename +
                    "': " + error.message());
    return 1;
  }

  OpPrintingFlags printingFlags;
  if (args.hasArg(OPT_mlir_print_debuginfo))
    printingFlags.enableDebugInfo();
  (*module)->print(output.os(), printingFlags);
  output.os() << '\n';
  output.keep();
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  InitLLVM initLLVM(argc, argv);
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
           << circt::getSlangVersion() << '\n';
    return 0;
  }

  return executeCompilation(args);
}
