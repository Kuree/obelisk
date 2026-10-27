//===- PrepareExpressions.h - Immutable source expression relations -------===//

#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPAREEXPRESSIONS_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPAREEXPRESSIONS_H

#include "PrepareTopology.h"
#include "PrepareUnits.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/StringSet.h"
#include <optional>

namespace talos::simlowering {
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

/// Retain an exact expression occurrence in an existing owner's relation slot.
/// Planning is atomic: unsupported syntax returns no endpoint and emits no
/// partial inventory. Declaration leaves keep their canonical identities.
class VPIExpressionMaterializer {
public:
  VPIExpressionMaterializer(sim::SimDesignOp design,
                            const llvm::StringMap<DescriptorInfo> &descriptors,
                            mlir::OpBuilder &builder);
  mlir::FailureOr<std::optional<VPIExpressionEndpoint>>
  resolve(mlir::Operation *expression, sim::VPIObjectRefAttr owner,
          uint32_t ownerKind, uint64_t scope, uint32_t selector,
          sim::VPIRelationMode mode = sim::VPIRelationMode::Handle,
          uint64_t ordinal = 0);

  /// Validate every expression before emitting an iterated relation group.
  /// Unsupported labels leave the entire group unavailable, without gaps.
  mlir::FailureOr<std::optional<mlir::SmallVector<VPIExpressionEndpoint>>>
  resolveGroup(mlir::ArrayRef<mlir::Operation *> expressions,
               sim::VPIObjectRefAttr owner, uint32_t ownerKind, uint64_t scope,
               uint32_t selector);

private:
  mlir::FailureOr<std::optional<VPIExpressionEndpoint>>
  resolveImpl(mlir::Operation *expression, sim::VPIObjectRefAttr owner,
              uint32_t ownerKind, uint64_t scope, uint32_t selector,
              sim::VPIRelationMode mode, uint64_t ordinal, bool materialize);
  sim::SimDesignOp design;
  VPIExpressionResolver declarations;
  mlir::OpBuilder &builder;
  uint64_t nextID = 0;
  /// Source declarations referenced by expression leaves; the source tree is
  /// not modified while relations are materialized.
  mlir::SymbolTableCollection sourceSymbols;
  /// Symbol names in the design, including the expressions created so far.
  llvm::StringSet<> designSymbols;
};

/// Materialize exact direct declaration operands after statement and local
/// inventories exist, before source expressions are lowered or optimized.
mlir::LogicalResult materializeProceduralExpressionRelations(
    sim::SimDesignOp design, const PreparedUnits &units,
    const llvm::StringMap<DescriptorInfo> &descriptors,
    mlir::OpBuilder &builder);
} // namespace talos::simlowering
#endif
