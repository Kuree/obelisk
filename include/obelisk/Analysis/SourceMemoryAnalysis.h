#ifndef OBELISK_ANALYSIS_SOURCEMEMORYANALYSIS_H
#define OBELISK_ANALYSIS_SOURCEMEMORYANALYSIS_H
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <optional>
namespace obelisk::sim {
class SimFuncOp;
}
namespace obelisk::analysis {
/// Dense must-value analysis over source-ordered reference accesses. Equal
/// values survive CFG joins; may-alias writes, calls, observers, suspensions,
/// overrides and external boundaries kill the relevant snapshot. Canonical
/// write forwarding requires a closed mutation/override contract.
class SourceMemoryAnalysis {
public:
  struct ElementSnapshot {
    mlir::Value aggregate, index;
  };
  struct SubelementSnapshot {
    mlir::Value aggregate;
    llvm::SmallVector<int64_t> indices;
  };
  struct SliceSnapshot {
    mlir::Value aggregate;
    mlir::OpFoldResult offset;
  };
  SourceMemoryAnalysis(sim::SimFuncOp function, bool canonicalWrites = false);
  mlir::Value getForwarded(mlir::Operation *load) const {
    return forwarded.lookup(load);
  }
  std::optional<ElementSnapshot>
  getElementSnapshot(mlir::Operation *load) const {
    auto found = elements.find(load);
    return found == elements.end() ? std::nullopt
                                   : std::optional(found->second);
  }
  std::optional<SubelementSnapshot>
  getSubelementSnapshot(mlir::Operation *load) const {
    auto found = subelements.find(load);
    return found == subelements.end() ? std::nullopt
                                      : std::optional(found->second);
  }
  std::optional<SliceSnapshot> getSliceSnapshot(mlir::Operation *load) const {
    auto found = slices.find(load);
    return found == slices.end() ? std::nullopt : std::optional(found->second);
  }

private:
  llvm::DenseMap<mlir::Operation *, mlir::Value> forwarded;
  llvm::DenseMap<mlir::Operation *, ElementSnapshot> elements;
  llvm::DenseMap<mlir::Operation *, SubelementSnapshot> subelements;
  llvm::DenseMap<mlir::Operation *, SliceSnapshot> slices;
};
/// Apply the frozen dense must-value facts, retaining all canonical stores.
/// canonicalWrites requires no external mutation or force/release boundary.
/// An optional filter limits rewrites to loads whose mutation contract the
/// caller has proved. Return the number of forwarded loads.
uint64_t forwardSourceMemoryLoads(
    sim::SimFuncOp function, bool canonicalWrites,
    llvm::function_ref<bool(mlir::Operation *)> canForward = {});
} // namespace obelisk::analysis
#endif
