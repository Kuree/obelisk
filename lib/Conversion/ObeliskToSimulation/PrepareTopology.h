//===- PrepareTopology.h - Static design topology analysis ------*- C++ -*-===//
//
// Private topology planning interfaces for semantic preparation.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARETOPOLOGY_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARETOPOLOGY_H

#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "PrepareDeclarations.h"

#include "mlir/IR/Operation.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/StringMap.h"

namespace obelisk::simlowering {

struct DescriptorInfo {
  enum class Kind { Storage, Net, Driver, Event } kind;
  uint64_t id;
  uint64_t scopeId;
  mlir::Type type;
  sim::NetResolutionKind netKind = sim::NetResolutionKind::Wire;
  mlir::Type rootType;
  uint64_t viewOffset = 0;
  uint64_t packedViewOffset = 0;
  mlir::SmallVector<int64_t> viewIndices;
  mlir::Type aggregateViewType;
};

struct StaticStorageView {
  std::string path;
  mlir::Type rootType;
  mlir::Type viewType;
  uint64_t offset = 0;
  uint64_t packedOffset = 0;
  mlir::SmallVector<int64_t> indices;
  mlir::Type aggregateType;
};

struct PreparedPortAliases {
  llvm::StringMap<std::string> aliases;
  llvm::StringMap<StaticStorageView> refViews;
  llvm::StringMap<std::string> interfaceAliases;
  mlir::SmallVector<ir::SVPortConnectionOp> connections;
};

/// The sole operation in the first block of a semantic inventory region.
mlir::Operation *getSingleRegionRoot(mlir::Region &region);

/// Resolve an input/output port actual to its assignment lvalue.
mlir::Operation *getPortActualLValue(ir::SVPortConnectionOp connection);

/// Analyze ref/interface aliases without materializing target descriptors.
mlir::FailureOr<PreparedPortAliases>
analyzePortAliases(ir::SVRootSymbolOp semanticRoot);

/// Whether a semantic symbol uses activation-local automatic storage.
bool isAutomaticLocalSymbol(mlir::Operation *operation);

/// Whether a formal belongs to a subroutine with static default lifetime.
bool isStaticFormal(mlir::Operation *operation);

/// Whether an operation is nested inside an executable semantic code unit.
bool isNestedInCodeUnit(mlir::Operation *operation);

/// Materialize design storage/net/event descriptors and resolve all aliases.
mlir::FailureOr<llvm::StringMap<DescriptorInfo>> materializeDesignDescriptors(
    mlir::ModuleOp module, ir::SVRootSymbolOp semanticRoot,
    const PreparedPortAliases &portAliases,
    const PreparedScopeDeclarations &scopes, mlir::OpBuilder &builder);

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARETOPOLOGY_H
