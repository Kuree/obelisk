//===- ClassBitstreamPlan.h - Canonical object cast metadata -*- C++ -*-===//

#ifndef TALOS_ANALYSIS_CLASSBITSTREAMPLAN_H
#define TALOS_ANALYSIS_CLASSBITSTREAMPLAN_H

#include "mlir/Support/LogicalResult.h"

namespace talos::sim {
class SimDesignOp;
}

namespace llvm {
class DataLayout;
}

namespace talos::analysis {

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

} // namespace talos::analysis

#endif // TALOS_ANALYSIS_CLASSBITSTREAMPLAN_H
