//===- ManagedClassLayoutAnalysis.h - Shared managed class layout -*- C++
//-*-===//

#ifndef OBELISK_ANALYSIS_MANAGEDCLASSLAYOUTANALYSIS_H
#define OBELISK_ANALYSIS_MANAGEDCLASSLAYOUTANALYSIS_H

#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/StringMap.h"

#include <cstdint>
#include <optional>

namespace llvm {
class DataLayout;
} // namespace llvm

namespace obelisk::analysis {

/// Target-dependent managed class layouts shared by native and bytecode
/// lowering. The analysis is read-only; consumers may materialize its field
/// offsets as IR attributes when required by later conversion patterns.
struct ManagedClassLayoutAnalysis {
  struct Field {
    sim::SimClassFieldDeclOp declaration;
    uint64_t offset;
    SimulationStorageProperties storage;
  };

  struct Class {
    sim::SimClassDeclOp declaration;
    uint64_t size;
    uint32_t alignment;
    std::optional<uint64_t> weakReferentOffset;
    mlir::SmallVector<Field> fields;
  };

  static mlir::FailureOr<ManagedClassLayoutAnalysis>
  compute(sim::SimDesignOp design, const llvm::DataLayout &dataLayout);

  const Class *lookup(llvm::StringRef name) const;

  mlir::SmallVector<Class> classes;

private:
  llvm::StringMap<unsigned> indices;
};

mlir::LogicalResult
materializeManagedClassFieldOffsets(const ManagedClassLayoutAnalysis &analysis);

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_MANAGEDCLASSLAYOUTANALYSIS_H
