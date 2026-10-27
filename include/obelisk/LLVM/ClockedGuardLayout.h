//===- ClockedGuardLayout.h - Event-guard arms of clocked kernels ----------===//
//
// LLVM IR passes that run on the generated design during optimization and
// codegen. They are linked into the driver and also built as an `opt` plugin
// (ObeliskLLVMPassPlugin) so each pass is tested on LLVM IR directly.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LLVM_CLOCKEDGUARDLAYOUT_H
#define OBELISK_LLVM_CLOCKEDGUARDLAYOUT_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class PassBuilder;
} // namespace llvm

namespace obelisk {

/// A fused clocked kernel (FuseClockedKernels names it `__obelisk_clocked_*`,
/// and every body derived from it keeps the prefix) is a chain of event
/// guards: each one-armed `if` holds the register updates of one event, and the
/// kernel runs the whole chain once per clock edge. Laid out in source order
/// an arm follows its guard, so every edge on which the event does not occur
/// jumps over the arm, the fetch stream touches the first line of every arm and
/// the hot path uses a fraction of each line it fetches. The pass gives the arm
/// edge of every such branch an unlikely weight, so block placement keeps the
/// guard chain contiguous and moves the arms out of line. A weight orders code
/// and changes no instruction. Combinational kernels keep the default layout:
/// their arms are value selections taken on most evaluations.
struct ClockedGuardLayoutPass : llvm::PassInfoMixin<ClockedGuardLayoutPass> {
  llvm::PreservedAnalyses run(llvm::Module &module,
                              llvm::ModuleAnalysisManager &analyses);
  /// Marks the arms of one function; true when a branch changed.
  static bool runOnFunction(llvm::Function &function);
};

/// Registers the LLVM passes under their `opt` names and installs the default
/// pipeline's wide-integer vectorization guard.
void registerObeliskLLVMPasses(llvm::PassBuilder &builder);

} // namespace obelisk

#endif // OBELISK_LLVM_CLOCKEDGUARDLAYOUT_H
