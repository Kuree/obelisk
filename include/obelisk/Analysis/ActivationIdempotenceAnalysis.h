//===- ActivationIdempotenceAnalysis.h - Stable-input state proof -*- C++
//-*-===//
#ifndef OBELISK_ANALYSIS_ACTIVATIONIDEMPOTENCEANALYSIS_H
#define OBELISK_ANALYSIS_ACTIVATIONIDEMPOTENCEANALYSIS_H

#include "obelisk/Analysis/ActivationMemoryAnalysis.h"

namespace obelisk::analysis {
/// Prove F(inputs, F(inputs, state)) == F(inputs, state) for a closed
/// activation. On each CFG path, incoming state used by a branch or final
/// output must be copied back unchanged on that path. This admits conditional
/// holds, while rejecting counters and outputs that expose a pre-update
/// snapshot.
///
/// This bounded, read-only proof currently requires an acyclic CFG and exact,
/// disjoint canonical storage cells. It does not authorize reordering, hiding
/// transitions, changing sensitivity, or executing an inactive process. Clients
/// separately establish exclusive state ownership and observer closure.
bool hasIdempotentStateTransition(sim::SimFuncOp function,
                                  const ActivationMemoryAnalysis &memory,
                                  const HandleDataflowAnalysis &handles,
                                  unsigned maximumOperations = 500000);
} // namespace obelisk::analysis
#endif
