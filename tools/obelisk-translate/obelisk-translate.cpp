//===- obelisk-translate.cpp - SystemVerilog to IR translator -------------===//
//
// obelisk-translate reads SystemVerilog and emits obelisk's intermediate
// representation.
//
// Stage 1 (current): SystemVerilog -> Moore dialect IR, using CIRCT's
// slang-based ImportVerilog frontend linked in-process. Later stages will lower
// the Moore IR to LLVM with `llvm.coro.*` coroutines for simulation (see
// DESIGN.md). Keeping the frontend in-process (rather than shelling out to
// circt-verilog) is what lets us hang obelisk's own passes off the same
// MLIRContext.
//
//===----------------------------------------------------------------------===//

#include "circt/Conversion/ImportVerilog.h"
#include "circt/InitAllDialects.h"

#include "obelisk/Dialect/Sim/ObeliskDialect.h"

#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Support/Timing.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace mlir;

//===----------------------------------------------------------------------===//
// Command line options
//===----------------------------------------------------------------------===//

static cl::opt<std::string>
    inputFilename(cl::Positional, cl::desc("<input SystemVerilog file>"),
                  cl::init("-"));

static cl::opt<std::string> outputFilename("o", cl::desc("Output filename"),
                                           cl::value_desc("filename"),
                                           cl::init("-"));

/// What IR to emit. For now only the Moore (AST-level) IR is available; the
/// coroutine-lowered LLVM stage will be added as `--emit=llvm` etc.
namespace {
enum class EmitKind { Moore };
} // namespace

static cl::opt<EmitKind>
    emitKind("emit", cl::desc("Output IR to emit"), cl::init(EmitKind::Moore),
             cl::values(clEnumValN(EmitKind::Moore, "moore",
                                   "AST-level Moore dialect IR (default)")));

static cl::opt<bool> verifyDiagnostics("verify-diagnostics",
                                       cl::desc("Verify the IR after import"),
                                       cl::init(true));

//===----------------------------------------------------------------------===//
// Driver
//===----------------------------------------------------------------------===//

int main(int argc, char **argv) {
  InitLLVM y(argc, argv);
  cl::ParseCommandLineOptions(
      argc, argv,
      "obelisk-translate - translate SystemVerilog to obelisk IR\n");

  // Register every dialect the CIRCT frontend can emit (moore, hw, comb, seq,
  // llhd, cf, func, ...), so the imported module parses and prints cleanly.
  DialectRegistry registry;
  circt::registerAllDialects(registry);
  registry.insert<obelisk::ir::ObeliskDialect>();
  MLIRContext context(registry);
  context.loadAllAvailableDialects();

  // Slurp the input into an llvm::SourceMgr, wired to an MLIR diagnostic
  // handler so slang/import errors surface with source locations.
  auto fileOrErr = MemoryBuffer::getFileOrSTDIN(inputFilename);
  if (std::error_code ec = fileOrErr.getError()) {
    WithColor::error(errs(), "obelisk-translate")
        << "could not open '" << inputFilename << "': " << ec.message() << "\n";
    return 1;
  }

  auto sourceMgr = std::make_shared<SourceMgr>();
  sourceMgr->AddNewSourceBuffer(std::move(*fileOrErr), SMLoc());
  SourceMgrDiagnosticHandler diagHandler(*sourceMgr, &context);

  // Open the output before doing work so we fail fast on a bad path.
  std::error_code ec;
  ToolOutputFile output(outputFilename, ec, sys::fs::OF_None);
  if (ec) {
    WithColor::error(errs(), "obelisk-translate")
        << "could not open output '" << outputFilename << "': " << ec.message()
        << "\n";
    return 1;
  }

  // Run the slang frontend: SystemVerilog -> Moore dialect.
  DefaultTimingManager tm;
  TimingScope ts = tm.getRootScope();

  OwningOpRef<ModuleOp> module(ModuleOp::create(UnknownLoc::get(&context)));

  circt::ImportVerilogOptions options;
  options.mode = circt::ImportVerilogOptions::Mode::Full;

  if (failed(circt::importVerilog(*sourceMgr, &context, ts, module.get(),
                                  &options)))
    return 1;

  if (verifyDiagnostics && failed(verify(*module))) {
    WithColor::error(errs(), "obelisk-translate")
        << "imported IR failed verification\n";
    return 1;
  }

  // Emit. Only Moore IR for now; this switch is where the coroutine-lowered
  // stages will plug in.
  switch (emitKind) {
  case EmitKind::Moore:
    module->print(output.os());
    output.os() << "\n";
    break;
  }

  output.keep();
  return 0;
}
