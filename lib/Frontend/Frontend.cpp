//===- Frontend.cpp - Obelisk SystemVerilog frontend support --------------===//

#include "obelisk/Frontend/Frontend.h"

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Verifier.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

using namespace mlir;

namespace obelisk::frontend {

FailureOr<OwningOpRef<ModuleOp>>
importSystemVerilog(llvm::ArrayRef<std::string> inputFilenames,
                    MLIRContext &context, TimingScope &timingScope,
                    const circt::ImportVerilogOptions &options, bool verifyIR) {
  llvm::SourceMgr sourceManager;
  for (const std::string &filename : inputFilenames) {
    auto buffer = llvm::MemoryBuffer::getFileOrSTDIN(filename);
    if (std::error_code error = buffer.getError()) {
      emitError(UnknownLoc::get(&context))
          << "could not open input '" << filename << "': " << error.message();
      return failure();
    }
    sourceManager.AddNewSourceBuffer(std::move(*buffer), llvm::SMLoc());
  }

  SourceMgrDiagnosticHandler diagnosticHandler(sourceManager, &context);
  OwningOpRef<ModuleOp> module(ModuleOp::create(UnknownLoc::get(&context)));
  if (failed(circt::importVerilog(sourceManager, &context, timingScope,
                                  module.get(), &options)))
    return failure();

  if (verifyIR && failed(verify(*module))) {
    emitError(UnknownLoc::get(&context))
        << "imported SystemVerilog IR failed verification";
    return failure();
  }
  return std::move(module);
}

} // namespace obelisk::frontend
