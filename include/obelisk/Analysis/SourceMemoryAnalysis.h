#ifndef OBELISK_ANALYSIS_SOURCEMEMORYANALYSIS_H
#define OBELISK_ANALYSIS_SOURCEMEMORYANALYSIS_H
#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseMap.h"
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

private:
  llvm::DenseMap<mlir::Operation *, mlir::Value> forwarded;
  llvm::DenseMap<mlir::Operation *, ElementSnapshot> elements;
};
} // namespace obelisk::analysis
#endif
