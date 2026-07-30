//===- SimulationDPILowering.h - Shared native DPI lowering support ------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONDPILOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONDPILOWERING_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Support/LogicalResult.h"

#include <cstdint>

namespace obelisk::detail {

mlir::Value makeDPIPlaneStorage(mlir::OpBuilder &builder,
                                mlir::Location location, mlir::Value value,
                                unsigned alignment = 8);

struct DPIOperandABI {
  uint32_t direction = 0;
  uint32_t width = 0;
  uint32_t category = 0;
  bool fourState = false;
  bool isSigned = false;
};

inline mlir::FailureOr<DPIOperandABI>
parseDPIOperandABI(mlir::Attribute attribute, mlir::Operation *operation) {
  auto abi = mlir::dyn_cast<sim::DPIABIAttr>(attribute);
  if (!abi)
    return operation->emitError("has malformed DPI ABI metadata"),
           mlir::failure();
  return DPIOperandABI{
      static_cast<uint32_t>(abi.getDirection()),
      abi.getWidth(),
      static_cast<uint32_t>(abi.getKind()),
      abi.getFourState(),
      abi.getIsSigned(),
  };
}

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_SIMULATIONDPILOWERING_H
