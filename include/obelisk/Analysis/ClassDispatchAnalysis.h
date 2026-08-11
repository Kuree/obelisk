//===- ClassDispatchAnalysis.h - Managed class dispatch -------*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_CLASSDISPATCHANALYSIS_H
#define OBELISK_ANALYSIS_CLASSDISPATCHANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/StringMap.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace obelisk::analysis {

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
                                    uint64_t slot,
                                    uint64_t signatureId) const;

  /// Return every non-abstract, non-interface class compatible with the
  /// receiver's static class, ordered by class ID and then symbol name.
  mlir::SmallVector<sim::SimClassDeclOp>
  compatibleConcreteClasses(sim::SimClassDeclOp staticClass) const;

  mlir::ArrayRef<sim::SimClassDeclOp> getClasses() const { return classes; }

  static constexpr uint64_t getInterfaceDispatchSlot() {
    return std::numeric_limits<uint32_t>::max();
  }

private:
  mlir::SmallVector<sim::SimClassDeclOp> classes;
  llvm::StringMap<size_t> classIndices;
  llvm::StringMap<mlir::SmallVector<sim::SimClassMethodDeclOp>> methods;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_CLASSDISPATCHANALYSIS_H
