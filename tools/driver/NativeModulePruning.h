//===- NativeModulePruning.h - Model reachability -----------------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_NATIVEMODULEPRUNING_H
#define OBELISK_TOOLS_DRIVER_NATIVEMODULEPRUNING_H

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/Internalize.h"

namespace obelisk::driver::detail {

/// Give SymbolDCE the closed-world visibility of a generated executable.
/// LLVM linkage is left intact for translation and subsequent partitioning.
/// Keep the state globals consumed by the LLVM-level lifecycle builder until
/// it has materialized its references; LLVM DCE can then prune any unused ones.
inline mlir::FailureOr<llvm::StringSet<>>
pruneNativeExecutableSymbols(mlir::ModuleOp module,
                             const llvm::StringSet<> &exports,
                             bool requiresLifecycle) {
  llvm::StringSet<> candidates;
  for (mlir::Operation &operation : module.getBody()->getOperations()) {
    auto symbol = mlir::dyn_cast<mlir::SymbolOpInterface>(&operation);
    if (!symbol)
      continue;
    llvm::StringRef name = symbol.getName();
    if (name == "main" || exports.contains(name) || name.starts_with("llvm."))
      continue;
    if (requiresLifecycle &&
        (name == "__obelisk_current_context" ||
         name == "__obelisk_state_value" || name == "__obelisk_state_unknown" ||
         name == "__obelisk_execution_descriptor_v1"))
      continue;
    if (auto function = mlir::dyn_cast<mlir::LLVM::LLVMFuncOp>(&operation)) {
      if (function.isExternal())
        continue;
    } else if (!mlir::isa<mlir::LLVM::GlobalOp>(&operation)) {
      continue;
    }
    candidates.insert(name);
    symbol.setPrivate();
  }
  mlir::PassManager passes(module.getContext());
  passes.addPass(mlir::createSymbolDCEPass());
  if (mlir::failed(passes.run(module)))
    return mlir::failure();
  for (mlir::Operation &operation : module.getBody()->getOperations())
    if (auto symbol = mlir::dyn_cast<mlir::SymbolOpInterface>(&operation))
      candidates.erase(symbol.getName());
  return candidates;
}

/// Only for a complete generated executable model, before linking foreign
/// inputs. Its external entry points are main and the explicitly inventoried
/// DPI exports; runtime callbacks remain reachable through address constants.
/// Object/IR emission must retain its open-world linkage contract.
inline llvm::StringSet<>
pruneNativeExecutableModel(llvm::Module &module,
                           const llvm::StringSet<> &exports) {
  llvm::StringSet<> definitions;
  for (const llvm::GlobalValue &value : module.global_values())
    if (!value.isDeclaration())
      definitions.insert(value.getName());
  llvm::internalizeModule(module, [&](const llvm::GlobalValue &value) {
    return value.getName() == "main" || exports.contains(value.getName());
  });
  llvm::ModuleAnalysisManager analyses;
  llvm::GlobalDCEPass().run(module, analyses);
  for (const llvm::GlobalValue &value : module.global_values())
    definitions.erase(value.getName());
  return definitions;
}

} // namespace obelisk::driver::detail

#endif
