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
  llvm::APInt init;
  llvm::APInt limit;
  llvm::APInt stride;
  mlir::arith::CmpIPredicate predicate = mlir::arith::CmpIPredicate::slt;
  bool isAdd = false;
  bool signedCompare = false;
};
/// Validate the CFG, constant induction and finite fixed-width termination.
/// Marking admits early exits; replication requires all values to be
/// repairable.
std::optional<BoundedLoop>
recognizeBoundedLoop(mlir::Block *header,
                     const llvm::DenseSet<mlir::Operation *> &provenBackedges,
                     bool forMarking = false);
/// Global induction bounds include the final header value. Body bounds exclude
/// that value. Modular equality loops have no narrower interval certificate.
std::optional<mlir::ConstantIntRanges>
getInductionRange(const BoundedLoop &loop, bool includeExit);
llvm::SmallVector<BoundedLoop> analyzeBoundedLoops(mlir::Region &region);
} // namespace obelisk::analysis
#endif
