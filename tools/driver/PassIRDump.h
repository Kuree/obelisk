//===- PassIRDump.h - Stop a compile around one pass for replay -*- C++ -*-===//

#ifndef OBELISK_DRIVER_PASSIRDUMP_H
#define OBELISK_DRIVER_PASSIRDUMP_H

#include "TargetBackend.h"
#include "mlir/Bytecode/BytecodeWriter.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassInstrumentation.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>

namespace obelisk::driver {

/// Implements PassIRDumpOptions for one pass manager.
class PassIRDump final : public mlir::PassInstrumentation {
public:
  explicit PassIRDump(PassIRDumpOptions options)
      : options(std::move(options)) {}

  void runBeforePass(mlir::Pass *pass, mlir::Operation *op) override {
    if (!options.before.empty() && pass->getArgument() == options.before)
      dump(pass, op, "before");
  }
  void runAfterPass(mlir::Pass *pass, mlir::Operation *op) override {
    if (!options.after.empty() && pass->getArgument() == options.after)
      dump(pass, op, "after");
  }

private:
  void dump(mlir::Pass *pass, mlir::Operation *op, llvm::StringRef when) {
    std::lock_guard<std::mutex> lock(mutex);
    mlir::Operation *top = op;
    while (mlir::Operation *parent = top->getParentOp())
      top = parent;
    // Sibling workers may be rewriting other parts of the module while a
    // nested pass runs. As for MLIR's module-scope IR printing, printing the
    // module is only safe without threading.
    if (top != op && op->getContext()->isMultithreadingEnabled()) {
      llvm::errs() << "error: --dump-ir-" << when << '=' << pass->getArgument()
                   << " names a pass nested on '" << op->getName()
                   << "'; add --compile-threads=1\n";
      std::_Exit(1);
    }
    std::error_code error;
    llvm::raw_fd_ostream os(options.path, error);
    if (!error) {
      if (!llvm::StringRef(options.path).ends_with(".mlirbc"))
        top->print(os);
      else if (mlir::failed(mlir::writeBytecodeToFile(top, os)))
        error = std::make_error_code(std::errc::invalid_argument);
      os.close();
      if (!error)
        error = os.error();
    }
    if (error) {
      llvm::errs() << "error: cannot write '" << options.path
                   << "': " << error.message() << '\n';
      std::_Exit(1);
    }
    llvm::errs() << "dumped IR " << when << ' ' << pass->getArgument()
                 << " to '" << options.path << "'\n";
    // Skip destroying a module that may hold millions of operations.
    std::_Exit(0);
  }

  PassIRDumpOptions options;
  std::mutex mutex;
};

inline void addPassIRDump(mlir::PassManager &manager,
                          const PassIRDumpOptions &options) {
  if (options.enabled())
    manager.addInstrumentation(std::make_unique<PassIRDump>(options));
}

} // namespace obelisk::driver

#endif // OBELISK_DRIVER_PASSIRDUMP_H
