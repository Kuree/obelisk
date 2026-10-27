//===- PrepareLocals.h - Automatic declaration inventory ---------*- C++
//-*-===//
#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARELOCALS_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPARELOCALS_H
#include "PrepareCaptures.h"
namespace talos::simlowering {
mlir::LogicalResult
materializeProceduralLocals(sim::SimDesignOp design, const PreparedUnits &units,
                            const PreparedCaptures &captures,
                            llvm::StringMap<DescriptorInfo> &descriptors,
                            mlir::OpBuilder &builder);
} // namespace talos::simlowering
#endif
