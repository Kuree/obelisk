//===- RuntimeToLLVMPatterns.h - Typed runtime rewrite patterns -*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMPATTERNS_H
#define OBELISK_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMPATTERNS_H

#include "RuntimeToLLVMABI.h"

#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

namespace obelisk::runtimelowering {

void populateRuntimePatterns(const mlir::TypeConverter &converter,
                             mlir::RewritePatternSet &patterns,
                             const ABITypes &abi);

} // namespace obelisk::runtimelowering

#endif // OBELISK_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMPATTERNS_H
