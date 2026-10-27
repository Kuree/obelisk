//===- RuntimeToLLVMPatterns.h - Typed runtime rewrite patterns -*- C++ -*-===//

#ifndef TALOS_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMPATTERNS_H
#define TALOS_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMPATTERNS_H

#include "RuntimeToLLVMABI.h"

#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

namespace talos::runtimelowering {

void populateRuntimePatterns(const mlir::TypeConverter &converter,
                             mlir::RewritePatternSet &patterns,
                             const ABITypes &abi);

} // namespace talos::runtimelowering

#endif // TALOS_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMPATTERNS_H
