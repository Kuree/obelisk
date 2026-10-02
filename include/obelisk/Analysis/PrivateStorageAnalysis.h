//===- PrivateStorageAnalysis.h - Exclusive canonical state ------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_PRIVATESTORAGEANALYSIS_H
#define OBELISK_ANALYSIS_PRIVATESTORAGEANALYSIS_H
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
namespace obelisk::analysis {
/// Canonical roots whose executable accesses belong to one function. Closed
/// reference views and call/spawn bindings require matching root dataflow at
/// both ends. Unknown users, waits, NBA destinations, escaping references and
/// foreign reentry prevent promotion. This proves visibility, not SSA aliasing.
class PrivateStorageAnalysis {
public:
  struct Accesses {
    sim::SimStorageDeclOp declaration;
    sim::SimFuncOp owner;
    llvm::SmallVector<sim::SimRefLoadOp> loads;
    llvm::SmallVector<sim::SimRefStoreOp> stores;
    llvm::SmallVector<mlir::Value> bases;
    bool promotableViews = true;
  };
  explicit PrivateStorageAnalysis(sim::SimDesignOp design);
  const llvm::DenseMap<uint64_t, Accesses> &getRoots() const { return roots; }

private:
  llvm::DenseMap<uint64_t, Accesses> roots;
};
} // namespace obelisk::analysis
#endif
