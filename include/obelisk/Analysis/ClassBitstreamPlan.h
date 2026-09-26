//===- ClassBitstreamPlan.h - Canonical object cast metadata -*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_CLASSBITSTREAMPLAN_H
#define OBELISK_ANALYSIS_CLASSBITSTREAMPLAN_H

#include "mlir/Support/LogicalResult.h"

namespace obelisk::sim {
class SimDesignOp;
}

namespace llvm {
class DataLayout;
}

namespace obelisk::analysis {

/// Assign dense cast-site IDs, validate every referenced closed-world class
/// closure, and serialize the pointer-free trusted runtime blob. Re-running
/// this after bytecode encoding incorporates exact function/site bindings
/// retained on the cast operations.
mlir::LogicalResult
materializeClassBitstreamPlan(sim::SimDesignOp design,
                              const llvm::DataLayout &dataLayout);

/// Patch exact bytecode `(function, intrinsic-site)` bindings into an already
/// materialized blob without rebuilding target-dependent class schemas.
mlir::LogicalResult bindClassBitstreamBytecodeSites(sim::SimDesignOp design);

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_CLASSBITSTREAMPLAN_H
