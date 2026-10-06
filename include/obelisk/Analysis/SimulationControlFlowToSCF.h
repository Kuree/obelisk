//===- SimulationControlFlowToSCF.h - CFG structuring -----------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_SIMULATIONCONTROLFLOWTOSCF_H
#define OBELISK_ANALYSIS_SIMULATIONCONTROLFLOWTOSCF_H
#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
namespace obelisk::analysis {
/// Upstream CFG structuring with fixed-width dispatch and representable dead
/// SSA slots. Simulation has no target-sized index type at its executable
/// boundary. Shared by scheduling analysis and native word predication.
class SimulationControlFlowToSCF : public mlir::ControlFlowToSCFTransformation {
public:
  mlir::FailureOr<mlir::Operation *> createStructuredBranchRegionOp(
      mlir::OpBuilder &builder, mlir::Operation *op, mlir::TypeRange types,
      mlir::MutableArrayRef<mlir::Region> regions) override;
  mlir::Value getUndefValue(mlir::Location loc, mlir::OpBuilder &builder,
                            mlir::Type type) override;
};
} // namespace obelisk::analysis
#endif
