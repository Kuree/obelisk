//===- BoundedLoopAnalysis.h - Finite SSA induction proofs --------*- C++
//-*-===//
#ifndef OBELISK_ANALYSIS_BOUNDEDLOOPANALYSIS_H
#define OBELISK_ANALYSIS_BOUNDEDLOOPANALYSIS_H
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Interfaces/InferIntRangeInterface.h"
#include "llvm/ADT/DenseSet.h"
#include <optional>
namespace obelisk::analysis {
struct BoundedLoop {
  mlir::Block *header;
  mlir::BlockArgument induction;
  mlir::cf::CondBranchOp condition;
  mlir::cf::BranchOp entry;
  mlir::cf::BranchOp latch;
  mlir::Block *exit;
  llvm::SmallVector<mlir::Block *> blocks;
  llvm::DenseSet<mlir::Block *> members;
  size_t operations = 0;
  /// Exact initializer, or the earliest possible start along the recurrence.
  llvm::APInt init;
  mlir::Value initValue;
  llvm::APInt initMin, initMax;
  /// Exact constant bound, or the most permissive endpoint of an invariant
  /// bound (maximum for increasing loops, minimum for decreasing loops).
  llvm::APInt limit;
  /// Non-null only for an invariant bound. Consumers requiring an exact trip
  /// count must reject both initValue and limitValue; range/termination
  /// consumers may use the interval certificate.
  mlir::Value limitValue;
  llvm::APInt limitMin, limitMax;
  llvm::APInt stride;
  mlir::arith::CmpIPredicate predicate = mlir::arith::CmpIPredicate::slt;
  bool isAdd = false;
  bool signedCompare = false;
};
/// Validate the CFG, invariant induction and finite fixed-width termination.
/// Marking admits early exits; replication requires all values to be
/// repairable. Either mode may return invariant initValue/limitValue envelopes;
/// replication must resolve them to exact constants before removing any test.
std::optional<BoundedLoop>
recognizeBoundedLoop(mlir::Block *header,
                     const llvm::DenseSet<mlir::Operation *> &provenBackedges,
                     bool forMarking = false,
                     llvm::ArrayRef<BoundedLoop> enclosing = {});
/// Global induction bounds include the final header value. Body bounds exclude
/// that value. Modular equality loops have no narrower interval certificate.
std::optional<mlir::ConstantIntRanges>
getInductionRange(const BoundedLoop &loop, bool includeExit);
llvm::SmallVector<BoundedLoop, 0> analyzeBoundedLoops(mlir::Region &region);
/// A closed, nonsuspending CFG whose every cycle has a validated induction
/// latch. This is a control-flow proof only; callers check callee effects and
/// callee termination separately.
bool hasFiniteControlFlow(mlir::Region &region);
/// Discover loops in the initial nonsuspending activation, cutting suspension
/// edges on a disposable CFG. Results refer to the original IR. This does not
/// prove termination or identical induction bounds on subsequent activations;
/// a caller applying the facts there must separately prove resume equivalence.
llvm::SmallVector<BoundedLoop, 0>
analyzeActivationBoundedLoops(mlir::Region &region);
} // namespace obelisk::analysis
#endif
