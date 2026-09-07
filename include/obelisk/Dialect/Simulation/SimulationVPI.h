//===- SimulationVPI.h - Simulation VPI classification --------*- C++ -*-===//
//
// Public helpers for mapping simulation dialect declarations and semantic
// types to their IEEE VPI object kinds.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONVPI_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONVPI_H

#include <cstdint>
#include <optional>

namespace obelisk::sim {

class SimCodeUnitDeclOp;
class SimScopeDeclOp;
class VPITypeSemanticsAttr;

bool isVPIVisibleCodeUnit(SimCodeUnitDeclOp codeUnit);
uint32_t vpiKindForCodeUnit(SimCodeUnitDeclOp codeUnit);
uint32_t vpiKindForScope(SimScopeDeclOp scope);
uint32_t vpiKindForStorage(VPITypeSemanticsAttr type);
uint32_t vpiKindForNet(VPITypeSemanticsAttr type);
std::optional<uint32_t> vpiKindForTypespec(VPITypeSemanticsAttr type);

} // namespace obelisk::sim

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONVPI_H
