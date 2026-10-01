//===- NativeSymbolUses.h - Immutable symbol-use inventory ----------------===//

#ifndef OBELISK_CONVERSION_SIMULATIONTOLLVM_NATIVE_SYMBOL_USES_H
#define OBELISK_CONVERSION_SIMULATIONTOLLVM_NATIVE_SYMBOL_USES_H

#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>
#include <vector>

namespace obelisk::detail {

/// Enumerate immutable symbol scopes at operation boundaries. MLIR's symbol
/// API includes each root's attributes and stops at nested symbol tables, just
/// as a region inventory does. Workers publish into separate result slots;
/// unknown symbol scopes retain the API's conservative nullopt result.
/// The supplied regions must remain immutable until this function returns.
/// Use enumeration order is unspecified; callers must treat this as an
/// inventory rather than an ordered traversal.
inline std::optional<mlir::SymbolTable::UseRange>
collectNativeSymbolUses(mlir::MLIRContext *context,
                        llvm::ArrayRef<mlir::Region *> regions) {
  llvm::SmallVector<std::optional<mlir::SymbolTable::UseRange>> results;
  if (!context->isMultithreadingEnabled()) {
    if (regions.size() == 1)
      return mlir::SymbolTable::getSymbolUses(regions.front());
    for (auto *region : regions)
      results.push_back(mlir::SymbolTable::getSymbolUses(region));
  } else {
    llvm::SmallVector<mlir::Operation *> roots;
    for (auto *region : regions)
      for (auto &operation : region->getOps())
        roots.push_back(&operation);
    results.resize(roots.size());
    mlir::parallelFor(context, 0, roots.size(), [&](size_t index) {
      results[index] = mlir::SymbolTable::getSymbolUses(roots[index]);
    });
  }
  std::vector<mlir::SymbolTable::SymbolUse> uses;
  for (auto &result : results) {
    if (!result)
      return std::nullopt;
    llvm::append_range(uses, *result);
  }
  return mlir::SymbolTable::UseRange(std::move(uses));
}

} // namespace obelisk::detail

#endif
