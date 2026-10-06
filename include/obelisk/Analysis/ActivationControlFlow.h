//===- ActivationControlFlow.h - Normalize activation proof CFGs -*- C++
//-*-===//
#ifndef OBELISK_ANALYSIS_ACTIVATIONCONTROLFLOW_H
#define OBELISK_ANALYSIS_ACTIVATIONCONTROLFLOW_H

#include "mlir/IR/Region.h"

namespace obelisk::analysis {
/// Thread proved constant edges through pure dispatch blocks in a disposable
/// activation CFG. Dense must-predicate facts meet at joins; sparse folding
/// translates the dispatch condition through each predecessor's SSA operands.
/// No memory operation, loop body or unknown branch is duplicated or removed.
/// Returns whether any edge changed. Recompute all analyses after a change.
bool threadActivationControlFlow(mlir::Region &region);
} // namespace obelisk::analysis
#endif
