//===- WideIntegerVectorization.h - Bound wide shift vectorization --------===//

#ifndef TALOS_LLVM_WIDEINTEGERVECTORIZATION_H
#define TALOS_LLVM_WIDEINTEGERVECTORIZATION_H

#include "llvm/IR/PassManager.h"

namespace talos {

/// Keep loops with variable shifts wider than 128 bits scalar. LLVM's
/// vectorizer can underestimate their legalization cost and create vectors of
/// multi-kilobit integers, making instruction selection and scheduling explode.
struct LimitWideIntegerVectorizationPass
    : llvm::PassInfoMixin<LimitWideIntegerVectorizationPass> {
  llvm::PreservedAnalyses run(llvm::Function &function,
                              llvm::FunctionAnalysisManager &analyses);
};

} // namespace talos

#endif // TALOS_LLVM_WIDEINTEGERVECTORIZATION_H
