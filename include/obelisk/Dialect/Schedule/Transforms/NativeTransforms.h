#ifndef OBELISK_DIALECT_SCHEDULE_TRANSFORMS_NATIVE_TRANSFORMS_H
#define OBELISK_DIALECT_SCHEDULE_TRANSFORMS_NATIVE_TRANSFORMS_H
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include <cstdint>
namespace mlir {
class ModuleOp;
class Operation;
class SymbolTable;
class SymbolTableCollection;
class StringAttr;
namespace LLVM {
class AddressOfOp;
class GlobalOp;
} // namespace LLVM
} // namespace mlir
namespace obelisk::sim {
class SimDesignOp;
}
namespace obelisk::analysis {
class HandleDataflowAnalysis;
}
namespace obelisk::detail {
struct NativeStateLayout;
void annotateCompactNBAMetadata(mlir::ModuleOp module);
void materializeCleanEvalBodies(sim::SimDesignOp design);
mlir::LogicalResult materializeEvalTwoStateVariants(
    mlir::ModuleOp module, sim::SimDesignOp design,
    const NativeStateLayout &stateLayout, bool enabled,
    const llvm::DenseMap<uint64_t, uint32_t> &actorSlots);
} // namespace obelisk::detail
namespace obelisk::schedule {
/// Source activations whose extra evaluation, with no changed input, cannot
/// change state or publish an event. Requires complete, exclusive, single
/// writes, complete sensitivity, equivalent initial/resume computation, and
/// exactly one spawn in the root initializer entry.
/// The caller must retain the source revision and exclude foreign mutation.
llvm::DenseSet<mlir::StringAttr> collectRepeatableCombinationalActivations(
    sim::SimDesignOp design, const analysis::HandleDataflowAnalysis &handles);

/// Resolves the module global an address-of names, like
/// `AddressOfOp::getGlobal`. Serial callers cache lazily through a
/// SymbolTableCollection; concurrent workers share one prebuilt module table,
/// which stays valid while they add or remove no symbols.
class NativeGlobalLookup {
public:
  NativeGlobalLookup(mlir::SymbolTableCollection &symbols)
      : symbols(&symbols) {}
  explicit NativeGlobalLookup(const mlir::SymbolTable &module)
      : module(&module) {}
  mlir::LLVM::GlobalOp operator()(mlir::LLVM::AddressOfOp address) const;

private:
  mlir::SymbolTableCollection *symbols = nullptr;
  const mlir::SymbolTable *module = nullptr;
};

// Function-local executable scheduling rewrites; no fallback/frame facts are
// recomputed by these actions. Callers rewriting many functions of one module
// pass one symbol cache rather than rebuilding the module table per function.
void coalesceNativeReadyUpdates(mlir::Operation *operation);
void coalesceNativeReadyUpdates(mlir::Operation *operation,
                                const NativeGlobalLookup &globals);
void cacheNativePureCones(mlir::ModuleOp module, uint64_t minimumCost = 256,
                          uint64_t maximumCost = 4096,
                          unsigned maximumCaches = 128);
} // namespace obelisk::schedule
#endif
