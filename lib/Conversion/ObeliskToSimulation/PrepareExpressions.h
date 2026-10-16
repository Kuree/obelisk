//===- PrepareExpressions.h - Immutable source expression relations -------===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPAREEXPRESSIONS_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPAREEXPRESSIONS_H

#include "PrepareTopology.h"
#include "PrepareUnits.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include <optional>

namespace obelisk::simlowering {
struct VPIExpressionEndpoint {
  sim::VPIObjectRefAttr reference;
  uint32_t kind;
};

/// Resolve a whole declaration use without replacing a selected, converted,
/// or computed expression with its underlying storage. The source symbol and
/// retained declaration metadata are authoritative, including local variables
/// whose immutable declaration has no design-lifetime value.
class VPIExpressionResolver {
public:
  VPIExpressionResolver(sim::SimDesignOp design,
                        const llvm::StringMap<DescriptorInfo> &descriptors);
  std::optional<VPIExpressionEndpoint> resolve(mlir::Operation *expression);

private:
  sim::SimDesignOp design;
  mlir::ModuleOp module;
  const llvm::StringMap<DescriptorInfo> &descriptors;
  mlir::SymbolTableCollection symbols;
  llvm::StringMap<sim::SimVPINetIdentityDeclOp> netIdentities;
  llvm::DenseMap<uint64_t, sim::SimStorageDeclOp> storages;
  llvm::DenseMap<uint64_t, sim::SimNetDeclOp> nets;
};

/// Materialize exact direct declaration operands after statement and local
/// inventories exist, before source expressions are lowered or optimized.
mlir::LogicalResult materializeProceduralExpressionRelations(
    sim::SimDesignOp design, const PreparedUnits &units,
    const llvm::StringMap<DescriptorInfo> &descriptors,
    mlir::OpBuilder &builder);
} // namespace obelisk::simlowering
#endif
