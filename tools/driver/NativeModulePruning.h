//===- NativeModulePruning.h - Model reachability ------------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_NATIVEMODULEPRUNING_H
#define OBELISK_TOOLS_DRIVER_NATIVEMODULEPRUNING_H

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/Internalize.h"

namespace obelisk::driver::detail {

inline llvm::StringRef nativePrunedSymbolCategory(llvm::StringRef name) {
  if (name.ends_with(".__obelisk_bytecode_entry"))
    return "bytecode-entry";
  if (name.starts_with("__obelisk_eval_variant_dispatch_"))
    return "variant-dispatch";
  if (name.contains(".__obelisk_frame_"))
    return "frame";
  if (name.contains(".__obelisk_process_descriptor"))
    return "process-descriptor";
  if (name.contains(".__obelisk_spawn") ||
      name.contains(".__obelisk_schedule_") ||
      name.contains(".__obelisk_bytecode_continuations"))
    return "spawn";
  if (name.contains(".__obelisk_activate"))
    return "activation";
  if (name.contains(".__obelisk_coro_"))
    return "coroutine";
  if (name.contains(".__obelisk_eval_") || name.contains(".__obelisk_clean"))
    return "eval";
  if (name.contains(".__obelisk_native_execute") ||
      name.contains(".__obelisk_execute"))
    return "execute";
  return "other";
}

inline void reportNativePrunedSymbols(const llvm::StringSet<> &removed,
                                      llvm::StringRef stage) {
  llvm::SmallVector<llvm::StringRef> names;
  llvm::SmallMapVector<llvm::StringRef, size_t, 8> counts;
  names.reserve(removed.size());
  for (const auto &entry : removed)
    names.push_back(entry.getKey());
  llvm::sort(names);
  for (llvm::StringRef name : names) {
    llvm::StringRef category = nativePrunedSymbolCategory(name);
    ++counts[category];
    llvm::errs() << "obelisk native pruned symbol: stage=" << stage
                 << " category=" << category << " name=" << name << '\n';
  }
  for (const auto &[category, count] : counts)
    llvm::errs() << "obelisk native pruning category: stage=" << stage
                 << " category=" << category << " symbols=" << count << '\n';
}

/// Give SymbolDCE the closed-world visibility of a generated executable.
/// LLVM linkage is left intact for translation and subsequent partitioning.
/// Keep the state globals consumed by the LLVM-level lifecycle builder until
/// it has materialized its references; LLVM DCE can then prune any unused ones.
inline mlir::FailureOr<llvm::StringSet<>>
pruneNativeExecutableSymbols(mlir::ModuleOp module,
                             const llvm::StringSet<> &exports,
                             bool requiresLifecycle, bool verifyEach = true) {
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
  passes.enableVerifier(verifyEach);
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
