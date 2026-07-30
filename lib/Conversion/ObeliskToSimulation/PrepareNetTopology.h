//===- PrepareNetTopology.h - Net connection and driver planning -*- C++
//-*-===//
//
// Private net-topology materialization interface for semantic preparation.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARENETTOPOLOGY_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARENETTOPOLOGY_H

#include "PrepareTopology.h"

#include "mlir/IR/Builders.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"

namespace obelisk::simlowering {

struct DriverInfo {
  std::string path;
  DescriptorInfo descriptor;
  std::optional<uint64_t> nodeId;
  uint64_t drivenLow;
  uint64_t drivenWidth;
};

using ContinuousDriverMap =
    llvm::DenseMap<mlir::Operation *, mlir::SmallVector<DriverInfo>>;

/// Materialize static net connections and immutable driver descriptors.
mlir::FailureOr<ContinuousDriverMap> materializeNetTopology(
    mlir::SmallVectorImpl<mlir::Operation *> &sourceUnits,
    mlir::ArrayRef<ir::SVPortConnectionOp> portConnections,
    const llvm::StringMap<mlir::Operation *> &semanticSymbols,
    const llvm::StringMap<DescriptorInfo> &descriptors,
    const PreparedScopeDeclarations &scopes, mlir::OpBuilder &builder);

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARENETTOPOLOGY_H
