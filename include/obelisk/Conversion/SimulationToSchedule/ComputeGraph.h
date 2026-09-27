//===- ComputeGraph.h - Derive the late simulation schedule ------*- C++
//-*-===//
//
// One implementation of the late descriptor-range analysis. The builder pass
// applies its result to the IR and the verifier pass re-derives it and compares
// it against the persisted metadata, so there is exactly one place where the
// alias, staging, and scheduling rules live. A second hand-written copy of
// these rules in the verifier would drift silently instead of failing.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_CONVERSION_SIMULATIONTOSCHEDULE_COMPUTEGRAPH_H
#define OBELISK_CONVERSION_SIMULATIONTOSCHEDULE_COMPUTEGRAPH_H

#include "obelisk/Conversion/SimulationToSchedule/Utils.h"

#include "mlir/IR/Attributes.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/MapVector.h"

#include <cstdint>

namespace obelisk::simlowering {

/// Options that select among legal schedules. They are recorded in the derived
/// graph so the verifier can re-derive the same result from reloaded IR alone.
struct ComputeGraphOptions {
  uint32_t workers = 1;
  schedule::ComputeVPIMode vpi = schedule::ComputeVPIMode::Off;
};

/// Everything the analysis derives. The builder writes it onto the IR; the
/// verifier compares it against what the IR already carries.
struct ComputeGraphResult {
  schedule::ComputeGraphAttr graph;
  schedule::ComputeObservabilityKind observability =
      schedule::ComputeObservabilityKind::Invisible;
  /// Keyed by `sim::SimFuncOp`.
  ::llvm::MapVector<::mlir::Operation *, ::mlir::ArrayAttr> effectSummaries;
  ::llvm::MapVector<::mlir::Operation *, schedule::FragmentABIAttr>
      fragmentAbis;
  /// Keyed by the suspension, delay, NBA, and nonblocking-trigger operations
  /// that own a fixed compiled site.
  ::llvm::MapVector<::mlir::Operation *, schedule::ContinuationSiteAttr>
      continuations;
  ::llvm::MapVector<::mlir::Operation *, schedule::TimingSiteAttr> timings;
  ::llvm::MapVector<::mlir::Operation *, schedule::NBASiteAttr> nbaSites;
  ::llvm::MapVector<::mlir::Operation *, schedule::EventSiteAttr> eventSites;
};

/// Derive the whole late schedule from executable `simulation` SSA. This is a
/// pure function of the design and the options: two runs over equal IR produce
/// equal results, which is what lets the verifier compare rather than trust.
/// Diagnostics are emitted on failure.
::mlir::FailureOr<ComputeGraphResult>
deriveComputeGraph(sim::SimDesignOp design, ComputeGraphOptions options);

/// Validate representation invariants that every consumer of a persisted
/// compute graph relies on. This is intentionally independent of schedule
/// derivation: re-derivation detects stale metadata, while this catches a
/// malformed result produced consistently by both the builder and verifier.
::mlir::LogicalResult
validateComputeGraphStructure(sim::SimDesignOp design,
                              schedule::ComputeGraphAttr graph);

} // namespace obelisk::simlowering

#endif // OBELISK_CONVERSION_SIMULATIONTOSCHEDULE_COMPUTEGRAPH_H
