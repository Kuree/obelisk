//===- ClassDispatchAnalysis.h - Managed class dispatch -------*- C++ -*-===//

#ifndef TALOS_ANALYSIS_CLASSDISPATCHANALYSIS_H
#define TALOS_ANALYSIS_CLASSDISPATCHANALYSIS_H

#include "talos/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/StringMap.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace talos::analysis {

/// Immutable, design-scoped class hierarchy and effective-method inventory.
/// The ordering exposed by this analysis is deterministic across runs.
class ClassDispatchAnalysis {
public:
  explicit ClassDispatchAnalysis(sim::SimDesignOp design);

  sim::SimClassDeclOp lookup(llvm::StringRef name) const;
  sim::SimClassDeclOp lookup(sim::ClassHandleType type) const;

  bool isInstanceOf(sim::SimClassDeclOp dynamicClass,
                    sim::SimClassDeclOp target) const;

  /// Resolve the effective descriptor entry for a concrete dynamic class.
  /// Ordinary dispatch selects `slot`; interface dispatch selects the lowest
  /// effective slot carrying `signatureId`, matching the runtime table scan.
  sim::SimClassMethodDeclOp resolve(sim::SimClassDeclOp dynamicClass,
                                    uint64_t slot, uint64_t signatureId) const;

  /// Return every non-abstract, non-interface class compatible with the
  /// receiver's static class, ordered by class ID. The traversal visits each
  /// reachable inheritance edge once; results are not retained as closures.
  mlir::SmallVector<sim::SimClassDeclOp>
  compatibleConcreteClasses(sim::SimClassDeclOp staticClass) const;

  /// Resolve and deduplicate every concrete implementation reachable from a
  /// receiver's static class. Entries are ordered by the first compatible
  /// concrete class that selects them.
  mlir::SmallVector<sim::SimClassMethodDeclOp>
  compatibleImplementations(sim::SimClassDeclOp staticClass, uint64_t slot,
                            uint64_t signatureId, bool isTask) const;

  mlir::ArrayRef<sim::SimClassDeclOp> getClasses() const { return classes; }

  /// Count distinct shared method-table nodes for storage diagnostics.
  /// This visits each retained node once, including signature sub-tables.
  size_t getNumMethodTableNodes() const;

  static constexpr uint64_t getInterfaceDispatchSlot() {
    return std::numeric_limits<uint32_t>::max();
  }

private:
  struct Index;
  mlir::SmallVector<sim::SimClassDeclOp> classes;
  llvm::StringMap<size_t> classIndices;
  std::shared_ptr<const Index> index;
};

} // namespace talos::analysis

#endif // TALOS_ANALYSIS_CLASSDISPATCHANALYSIS_H
