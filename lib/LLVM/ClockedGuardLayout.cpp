//===- ClockedGuardLayout.cpp - Event-guard arms of clocked kernels --------===//

#include "talos/LLVM/ClockedGuardLayout.h"
#include "talos/LLVM/WideIntegerVectorization.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"

using namespace llvm;

namespace talos {

// `if (c) { arm }` with no else: the arm is every block reached from one
// successor of the guard without passing the other successor (the join). It
// is an arm when this guard is the only edge into it from outside, so every
// path through it rejoins the fall-through. Nested guards and loops inside the
// arm belong to it; an arm that can leave the function is not one.
static bool isGuardArm(BasicBlock &guard, BasicBlock *entry, BasicBlock *join) {
  if (entry == join || entry->getSinglePredecessor() != &guard)
    return false;
  SmallPtrSet<BasicBlock *, 16> arm;
  SmallVector<BasicBlock *> work{entry};
  while (!work.empty()) {
    BasicBlock *current = work.pop_back_val();
    if (current == join || !arm.insert(current).second)
      continue;
    if (succ_empty(current))
      return false;
    llvm::append_range(work, successors(current));
  }
  for (BasicBlock *member : arm)
    for (BasicBlock *predecessor : predecessors(member))
      if (!arm.contains(predecessor) &&
          (member != entry || predecessor != &guard))
        return false;
  return true;
}

bool ClockedGuardLayoutPass::runOnFunction(Function &function) {
  if (!function.getName().contains("__talos_clocked_"))
    return false;
  MDBuilder metadata(function.getContext());
  bool changed = false;
  for (BasicBlock &block : function) {
    auto *branch = dyn_cast<BranchInst>(block.getTerminator());
    if (!branch || !branch->isConditional() ||
        branch->getMetadata(LLVMContext::MD_prof))
      continue;
    if (isGuardArm(block, branch->getSuccessor(0), branch->getSuccessor(1)))
      branch->setMetadata(LLVMContext::MD_prof,
                          metadata.createBranchWeights(1, 1000));
    else if (isGuardArm(block, branch->getSuccessor(1),
                        branch->getSuccessor(0)))
      branch->setMetadata(LLVMContext::MD_prof,
                          metadata.createBranchWeights(1000, 1));
    else
      continue;
    changed = true;
  }
  return changed;
}

PreservedAnalyses ClockedGuardLayoutPass::run(Module &module,
                                              ModuleAnalysisManager &) {
  for (Function &function : module)
    runOnFunction(function);
  // Metadata only: the CFG and every value are unchanged.
  return PreservedAnalyses::all();
}

void registerTalosLLVMPasses(PassBuilder &builder) {
  builder.registerVectorizerStartEPCallback(
      [](FunctionPassManager &passes, OptimizationLevel) {
        passes.addPass(LimitWideIntegerVectorizationPass());
      });
  builder.registerPipelineParsingCallback(
      [](StringRef name, FunctionPassManager &passes,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (name != "talos-limit-wide-integer-vectorization")
          return false;
        passes.addPass(LimitWideIntegerVectorizationPass());
        return true;
      });
  builder.registerPipelineParsingCallback(
      [](StringRef name, ModulePassManager &passes,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (name != "talos-clocked-guard-layout")
          return false;
        passes.addPass(ClockedGuardLayoutPass());
        return true;
      });
}

} // namespace talos
