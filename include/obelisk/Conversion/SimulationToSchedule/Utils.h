//===- Utils.h - Shared simulation transformation helpers ----*- C++ -*-===//

#ifndef OBELISK_CONVERSION_SIMULATIONTOSCHEDULE_UTILS_H
#define OBELISK_CONVERSION_SIMULATIONTOSCHEDULE_UTILS_H

#include "mlir/IR/Builders.h"
#include "obelisk/Analysis/GraphAlgorithms.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <optional>

namespace obelisk::simlowering {

namespace sim = ::obelisk::sim;

using ::obelisk::analysis::computeStronglyConnectedComponents;

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

using HandleFact = ::obelisk::analysis::HandleFact;
using HandleFacts = ::obelisk::analysis::HandleFacts;

} // namespace obelisk::simlowering

#endif // OBELISK_CONVERSION_SIMULATIONTOSCHEDULE_UTILS_H
