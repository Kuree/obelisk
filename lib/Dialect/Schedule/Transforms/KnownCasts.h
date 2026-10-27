//===- KnownCasts.h - Two-state views of known values -----------*- C++ -*-===//

#ifndef TALOS_LIB_DIALECT_SCHEDULE_TRANSFORMS_KNOWNCASTS_H
#define TALOS_LIB_DIALECT_SCHEDULE_TRANSFORMS_KNOWNCASTS_H

#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Dialect/Simulation/SimulationTypes.h"

#include "mlir/IR/Builders.h"

namespace talos::detail {

/// The LRM two-state conversion, exact for a known value (IEEE 1800-2023
/// 6.11.2 maps only X/Z bits to 0). `value` must be a four-state scalar or a
/// packed aggregate (KnownVersioningAnalysis::isCastable).
inline mlir::Value castKnown(mlir::OpBuilder &builder, mlir::Location loc,
                             mlir::Value value) {
  mlir::Type type = value.getType();
  if (auto logic = mlir::dyn_cast<sim::LogicType>(type)) {
    mlir::Value bits = sim::SimLogicToBitsOp::create(
        builder, loc, builder.getIntegerType(logic.getWidth()), value);
    return sim::SimLogicFromBitsOp::create(builder, loc, logic, bits);
  }
  mlir::Type scalar = sim::getPackedScalarType(type);
  mlir::Value flat =
      sim::SimPackedFlattenOp::create(builder, loc, scalar, value);
  return sim::SimPackedUnflattenOp::create(builder, loc, type,
                                           castKnown(builder, loc, flat));
}

} // namespace talos::detail

#endif // TALOS_LIB_DIALECT_SCHEDULE_TRANSFORMS_KNOWNCASTS_H
