//===- Utils.h - Shared simulation transformation helpers ----*- C++ -*-===//

#ifndef TALOS_CONVERSION_SIMULATIONTOSCHEDULE_UTILS_H
#define TALOS_CONVERSION_SIMULATIONTOSCHEDULE_UTILS_H

#include "mlir/IR/Builders.h"
#include "talos/Analysis/GraphAlgorithms.h"
#include "talos/Analysis/SimulationAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <optional>

namespace talos::simlowering {

namespace sim = ::talos::sim;

using ::talos::analysis::computeStronglyConnectedComponents;

/// True for the terminators that end a fragment and resume a continuation.
bool isSuspensionTerminator(::mlir::Operation *op);

/// Shared suspension/action metadata access. Keeping the operation family in
/// one place prevents graph construction and verification from drifting when
/// a new suspension form is introduced.
schedule::ComputeActionKind
getFragmentActionKind(::mlir::Operation *terminator);
schedule::ContinuationSiteAttr
getContinuationSite(::mlir::Operation *operation);
void setContinuationSite(::mlir::Operation *operation,
                         schedule::ContinuationSiteAttr site);

/// Blocks that control can return to later in the process lifetime, including
/// across suspension boundaries. Computed once per function in linear time.
using ReexecutingBlockSet = ::llvm::DenseSet<::mlir::Block *>;
ReexecutingBlockSet getReexecutingBlocks(sim::SimFuncOp function);

using HandleFact = ::talos::analysis::HandleFact;
using HandleFacts = ::talos::analysis::HandleFacts;

/// Erase processes that only read state and wait for changes (IEEE 1800-2023
/// 4.3: no user-visible effect) together with their result-less spawns, and
/// return how many. The caller guarantees the VPI-off contract and rebuilds
/// the compute graph when the result is nonzero; the design's own attributes
/// are not consulted.
unsigned removeEffectFreeProcesses(sim::SimDesignOp design);

} // namespace talos::simlowering

#endif // TALOS_CONVERSION_SIMULATIONTOSCHEDULE_UTILS_H
