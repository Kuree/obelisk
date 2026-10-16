//===- PrepareLocals.h - Automatic declaration inventory ---------*- C++
//-*-===//
#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARELOCALS_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_PREPARELOCALS_H
#include "PrepareCaptures.h"
namespace obelisk::simlowering {
mlir::LogicalResult
materializeProceduralLocals(sim::SimDesignOp design, const PreparedUnits &units,
                            const PreparedCaptures &captures,
                            mlir::OpBuilder &builder);
} // namespace obelisk::simlowering
#endif
