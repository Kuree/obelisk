//===- WideIntegerVectorization.cpp - Bound wide shift vectorization ------===//

#include "talos/LLVM/WideIntegerVectorization.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;

namespace talos {

PreservedAnalyses LimitWideIntegerVectorizationPass::run(
    Function &function, FunctionAnalysisManager &analyses) {
  LoopInfo &loops = analyses.getResult<LoopAnalysis>(function);
  for (Loop *loop : loops.getLoopsInPreorder()) {
    bool wideShift = false;
    for (BasicBlock *block : loop->blocks()) {
      for (Instruction &instruction : *block) {
        if (instruction.isShift() &&
            instruction.getType()->isIntegerTy() &&
            instruction.getType()->getIntegerBitWidth() > 128 &&
            !isa<ConstantInt>(instruction.getOperand(1))) {
          wideShift = true;
          break;
        }
      }
      if (wideShift)
        break;
    }
    if (wideShift)
      addStringMetadataToLoop(loop, "llvm.loop.vectorize.enable", 0);
  }
  // Only loop hints change; every analysis of the CFG and values stays valid.
  return PreservedAnalyses::all();
}

} // namespace talos
