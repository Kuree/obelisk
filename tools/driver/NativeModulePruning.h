//===- NativeModulePruning.h - Model reachability ------------*- C++ -*-===//

#ifndef TALOS_TOOLS_DRIVER_NATIVEMODULEPRUNING_H
#define TALOS_TOOLS_DRIVER_NATIVEMODULEPRUNING_H

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "talos/Analysis/ParallelWalk.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/Internalize.h"

namespace talos::driver::detail {

inline llvm::StringRef nativePrunedSymbolCategory(llvm::StringRef name) {
  if (name.ends_with(".__talos_bytecode_entry"))
    return "bytecode-entry";
  if (name.starts_with("__talos_eval_variant_dispatch_"))
    return "variant-dispatch";
  if (name.contains(".__talos_frame_"))
    return "frame";
  if (name.contains(".__talos_process_descriptor"))
    return "process-descriptor";
  if (name.contains(".__talos_spawn") ||
      name.contains(".__talos_schedule_") ||
      name.contains(".__talos_bytecode_continuations"))
    return "spawn";
  if (name.contains(".__talos_activate"))
    return "activation";
  if (name.contains(".__talos_coro_"))
    return "coroutine";
  if (name.contains(".__talos_eval_") || name.contains(".__talos_clean"))
    return "eval";
  if (name.contains(".__talos_native_execute") ||
      name.contains(".__talos_execute"))
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
    llvm::errs() << "talos native pruned symbol: stage=" << stage
                 << " category=" << category << " name=" << name << '\n';
  }
  for (const auto &[category, count] : counts)
    llvm::errs() << "talos native pruning category: stage=" << stage
                 << " category=" << category << " symbols=" << count << '\n';
}

/// SymbolDCE (mlir/lib/Transforms/SymbolDCE.cpp) for a module without nested
/// symbol tables. Each top-level operation's symbol uses are gathered
/// concurrently; liveness then propagates from the same roots, and dead
/// symbols are erased in module order. Returns false without changing
/// anything when SymbolDCE itself must run: for nested symbol tables, or for a
/// live operation whose uses cannot be enumerated, which it reports.
inline bool eraseDeadModuleSymbols(mlir::ModuleOp module) {
  if (talos::analysis::anyNested<mlir::Operation *>(
          module, [&](mlir::Operation *operation) {
            return operation != module.getOperation() &&
                   operation->hasTrait<mlir::OpTrait::SymbolTable>();
          }))
    return false;
  llvm::SmallVector<mlir::Operation *> operations;
  for (mlir::Operation &operation : *module.getBody())
    operations.push_back(&operation);
  // Without nested symbol tables only flat references can resolve.
  llvm::SmallVector<llvm::SmallVector<mlir::StringAttr, 2>> uses(
      operations.size());
  llvm::SmallVector<char> unknown(operations.size(), false);
  talos::analysis::parallelForChunked(
      module.getContext(), operations.size(), [&](size_t index) {
        auto found = mlir::SymbolTable::getSymbolUses(operations[index]);
        if (!found) {
          unknown[index] = true;
          return;
        }
        for (const auto &use : *found)
          if (use.getSymbolRef().getNestedReferences().empty())
            uses[index].push_back(use.getSymbolRef().getRootReference());
      });
  // Resolve like mlir::SymbolTable: the first operation with a name.
  llvm::DenseMap<mlir::StringAttr, unsigned> symbols;
  for (auto [index, operation] : llvm::enumerate(operations))
    if (auto name = operation->getAttrOfType<mlir::StringAttr>(
            mlir::SymbolTable::getSymbolAttrName()))
      symbols.try_emplace(name, index);
  bool hidden = true;
  if (auto self =
          llvm::dyn_cast<mlir::SymbolOpInterface>(module.getOperation());
      self && module->getParentOp())
    hidden = self.isPrivate();
  llvm::SmallVector<char> live(operations.size(), false);
  llvm::SmallVector<unsigned> worklist;
  for (auto [index, operation] : llvm::enumerate(operations)) {
    auto symbol = llvm::dyn_cast<mlir::SymbolOpInterface>(operation);
    if (!symbol) {
      worklist.push_back(index);
      continue;
    }
    bool discardable =
        (hidden || symbol.isPrivate()) && symbol.canDiscardOnUseEmpty();
    if (!discardable && !live[index]) {
      live[index] = true;
      worklist.push_back(index);
    }
  }
  while (!worklist.empty()) {
    unsigned index = worklist.pop_back_val();
    if (unknown[index])
      return false;
    for (mlir::StringAttr name : uses[index]) {
      auto found = symbols.find(name);
      if (found == symbols.end() || live[found->second])
        continue;
      live[found->second] = true;
      worklist.push_back(found->second);
    }
  }
  for (auto [index, operation] : llvm::enumerate(operations))
    if (llvm::isa<mlir::SymbolOpInterface>(operation) && !live[index])
      operation->erase();
  return true;
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
        (name == "__talos_current_context" ||
         name == "__talos_state_value" || name == "__talos_state_unknown" ||
         name == "__talos_execution_descriptor_v1"))
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
  if (eraseDeadModuleSymbols(module)) {
    if (verifyEach && mlir::failed(mlir::verify(module)))
      return mlir::failure();
  } else {
    mlir::PassManager passes(module.getContext());
    passes.enableVerifier(verifyEach);
    passes.addPass(mlir::createSymbolDCEPass());
    if (mlir::failed(passes.run(module)))
      return mlir::failure();
  }
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

} // namespace talos::driver::detail

#endif
