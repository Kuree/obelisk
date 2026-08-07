//===- SlangOpDefs2.cpp - Generated op definitions, shard 2 ---*- C++ -*-===//
//
// One shard of the TableGen'd op definitions for the slang dialect.
// Compiling every op class in a single translation unit dominates the
// build's critical path; the shards compile in parallel instead. The
// shard count is set by OBELISK_OP_SHARD_COUNT.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Dialect/Slang/SlangOps.h"
#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "mlir/IR/Diagnostics.h"

using namespace mlir;

#define GET_OP_DEFS_2
#include "obelisk/Dialect/Slang/SlangOpDefs.cpp.inc"
