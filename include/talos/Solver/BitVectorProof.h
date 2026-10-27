//===- BitVectorProof.h - Bounded compiler arithmetic proofs ------*- C++
//-*-===//
#ifndef TALOS_SOLVER_BITVECTORPROOF_H
#define TALOS_SOLVER_BITVECTORPROOF_H
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
namespace llvm {
class Value;
}
namespace talos::solver {
struct BitVectorRange {
  llvm::Value *value;
  llvm::APInt umin, umax, smin, smax;
};
/// Prove an inclusive signed bound from fixed-width expressions and may ranges.
/// Phi nodes and unsupported producers are free variables. Exhaustion, disabled
/// Z3 and unknown answers return false and preserve the original bounds check.
bool proveBitVectorBounds(llvm::Value *value, const llvm::APInt &lower,
                          const llvm::APInt &upper,
                          llvm::ArrayRef<BitVectorRange> ranges,
                          unsigned resourceLimit = 10000);
} // namespace talos::solver
#endif
