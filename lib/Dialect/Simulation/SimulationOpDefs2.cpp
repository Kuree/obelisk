//===- SimulationOpDefs2.cpp - Generated op definitions, shard 2 ---*- C++ -*-===//
//
// One shard of the TableGen'd op definitions for the simulation dialect.
// Compiling every op class in a single translation unit dominates the
// build's critical path; the shards compile in parallel instead. The
// shard count is set by OBELISK_OP_SHARD_COUNT.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/TypeUtilities.h"

using namespace mlir;

#define GET_OP_DEFS_2
#include "obelisk/Dialect/Simulation/SimulationOpDefs.cpp.inc"
