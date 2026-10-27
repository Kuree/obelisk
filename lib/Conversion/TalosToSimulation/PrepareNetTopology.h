//===- PrepareNetTopology.h - Net connection and driver planning -*- C++
//-*-===//
//
// Private net-topology materialization interface for semantic preparation.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARENETTOPOLOGY_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARENETTOPOLOGY_H

#include "PrepareTopology.h"

#include "mlir/IR/Builders.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"

namespace talos::simlowering {

struct DriverInfo {
  std::string path;
  DescriptorInfo descriptor;
  std::optional<uint64_t> nodeId;
  uint64_t drivenLow;
  uint64_t drivenWidth;
  /// For conditional gates, identifies the polarity-specific low/high driver.
  std::optional<unsigned> strengthBank{};
  /// Exact primitive output assignment occurrence, independent of net aliasing.
  mlir::Operation *primitiveTerminalSource = nullptr;
};

using ContinuousDriverMap =
    llvm::DenseMap<mlir::Operation *, mlir::SmallVector<DriverInfo>>;

/// Materialize static net connections and immutable driver descriptors.
mlir::FailureOr<ContinuousDriverMap> materializeNetTopology(
    mlir::SmallVectorImpl<mlir::Operation *> &sourceUnits,
    mlir::ArrayRef<ir::SVPortConnectionOp> portConnections,
    mlir::ArrayRef<ir::SVNetAliasSymbolOp> netAliases,
    const llvm::StringMap<mlir::Operation *> &semanticSymbols,
    llvm::StringMap<DescriptorInfo> &descriptors,
    const PreparedScopeDeclarations &scopes, mlir::OpBuilder &builder);

} // namespace talos::simlowering

#endif // TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARENETTOPOLOGY_H
