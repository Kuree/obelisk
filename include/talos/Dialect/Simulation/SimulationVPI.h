//===- SimulationVPI.h - Simulation VPI classification --------*- C++ -*-===//
//
// Public helpers for mapping simulation dialect declarations and semantic
// types to their IEEE VPI object kinds.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_DIALECT_SIMULATION_SIMULATIONVPI_H
#define TALOS_DIALECT_SIMULATION_SIMULATIONVPI_H

#include "mlir/Support/LogicalResult.h"
#include <cstdint>
#include <optional>

namespace mlir {
class Operation;
class SymbolTableCollection;
} // namespace mlir

namespace talos::sim {

enum class EntryKind : uint32_t;
class SimCodeUnitDeclOp;
class SimDesignOp;
class SimScopeDeclOp;
class VPITypeSemanticsAttr;
class VPIObjectRefAttr;

/// Prove a shared declaration belongs to the exact instance definition.
mlir::LogicalResult
verifyVPIDefinitionMemberRef(mlir::Operation *use, SimDesignOp design,
                             VPIObjectRefAttr reference,
                             mlir::SymbolTableCollection &symbols);

mlir::LogicalResult verifyVPIExpressionInventory(SimDesignOp design);

/// Original builtin source types with no independently named declaration.
bool isVPIOriginalBuiltinTypeDefault(VPITypeSemanticsAttr type);

bool isVPIVisibleCodeUnit(SimCodeUnitDeclOp codeUnit);
/// The source callable kind may differ from its executable ABI entry kind:
/// imported tasks run through synchronous function thunks.
std::optional<EntryKind> vpiCallableSourceKind(SimCodeUnitDeclOp codeUnit);
uint32_t vpiKindForCodeUnit(SimCodeUnitDeclOp codeUnit);
uint32_t vpiKindForScope(SimScopeDeclOp scope);
uint32_t vpiKindForStorage(VPITypeSemanticsAttr type);
uint32_t vpiKindForClassMember(VPITypeSemanticsAttr type);
uint32_t vpiKindForNet(VPITypeSemanticsAttr type);
std::optional<uint32_t> vpiKindForTypespec(VPITypeSemanticsAttr type);
/// Compare SystemVerilog ref-port type identity while ignoring typedef
/// spelling layers retained only for VPI traversal.
bool areEquivalentVPIRefTypes(VPITypeSemanticsAttr left,
                              VPITypeSemanticsAttr right);
/// Remove typedef spelling layers recursively while preserving semantic type
/// identity. Canonical class types use this key; declarations retain aliases.
VPITypeSemanticsAttr stripVPITypedefAliases(VPITypeSemanticsAttr type);

} // namespace talos::sim

#endif // TALOS_DIALECT_SIMULATION_SIMULATIONVPI_H
