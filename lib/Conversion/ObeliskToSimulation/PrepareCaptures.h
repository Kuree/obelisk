//===- PrepareCaptures.h - Code-unit capture analysis -----------*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARECAPTURES_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARECAPTURES_H

#include "PrepareTopology.h"
#include "PrepareUnits.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

namespace obelisk::simlowering {

struct PreparedLocal {
  std::string path;
  mlir::Type type;
  bool automatic = false;
  bool patternVariable = false;
};

struct PreparedConstant {
  std::string path;
  sim::FrozenConstantAttr value;
};

struct PreparedCaptures {
  llvm::DenseMap<mlir::Operation *,
                 mlir::SmallVector<std::pair<std::string, DescriptorInfo>>>
      descriptors;
  llvm::DenseMap<mlir::Operation *, llvm::StringSet<>> readDescriptors;
  llvm::DenseMap<mlir::Operation *, mlir::SmallVector<PreparedLocal>> locals;
  llvm::DenseMap<mlir::Operation *, mlir::SmallVector<PreparedConstant>>
      constants;
  llvm::DenseMap<mlir::Operation *, mlir::SmallVector<PreparedLocal>>
      observerLocals;
  llvm::DenseMap<mlir::Operation *, mlir::SmallVector<PreparedLocal>>
      observerValues;
  llvm::DenseMap<mlir::Operation *, llvm::StringSet<>> observerReadLocals;
  llvm::DenseSet<mlir::Operation *> indirectRefTasks;
};

/// Analyze explicit unit captures and close descriptor captures over direct
/// calls and virtual method families.
mlir::FailureOr<PreparedCaptures> analyzeCodeUnitCaptures(
    const PreparedUnits &units,
    const llvm::StringMap<DescriptorInfo> &descriptors,
    const llvm::StringMap<mlir::Operation *> &semanticSymbols,
    mlir::ArrayRef<ir::SVClassTypeOp> classSources);

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARECAPTURES_H
