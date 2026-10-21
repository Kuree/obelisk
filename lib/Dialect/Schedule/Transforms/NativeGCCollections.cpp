//===- NativeGCCollections.cpp - Designated GC collection points ---------===//
//
// Generated schedules mark where the managed heap may reclaim deferred
// allocation pressure with schedule.gc.collect: between time slots, where no
// activation is running. The periodic-loop materializer places one after
// every clock edge and at every slot end. IEEE 1800-2023 8.29 leaves the
// timing of reclamation to the implementation, so a proven periodic clock
// lets every collection but one per cycle go; the survivors then lower to the
// runtime dialect.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Runtime/RuntimeTypes.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_THINNATIVEGCCOLLECTIONSPASS
#define GEN_PASS_DEF_LOWERNATIVEGCCOLLECTIONSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {

/// Remove a collection point; it reports success. Fold the status checks that
/// consumed it, and rejoin the continuation their split left behind, so no
/// dead control flow remains in the generated loop.
void removeCollection(schedule::NativeGCCollectOp collection) {
  OpBuilder builder(collection);
  Value ok = arith::ConstantOp::create(
      builder, collection.getLoc(), builder.getI32IntegerAttr(OBELISK_RT_OK));
  collection.getStatus().replaceAllUsesWith(ok);
  collection.erase();
  // A folded branch is erased, and it may also forward the status or the
  // check, so gather each set of users before rewriting any of them.
  llvm::SetVector<arith::CmpIOp> compares;
  for (Operation *user : ok.getUsers())
    if (auto compare = dyn_cast<arith::CmpIOp>(user))
      compares.insert(compare);
  for (arith::CmpIOp compare : compares) {
    APInt lhs, rhs;
    if (!matchPattern(compare.getLhs(), m_ConstantInt(&lhs)) ||
        !matchPattern(compare.getRhs(), m_ConstantInt(&rhs)))
      continue;
    bool taken = arith::applyCmpPredicate(compare.getPredicate(), lhs, rhs);
    llvm::SetVector<cf::CondBranchOp> branches;
    for (Operation *user : compare->getUsers())
      if (auto branch = dyn_cast<cf::CondBranchOp>(user))
        if (branch.getCondition() == compare.getResult())
          branches.insert(branch);
    for (cf::CondBranchOp branch : branches) {
      Block *block = branch->getBlock();
      Block *next = taken ? branch.getTrueDest() : branch.getFalseDest();
      SmallVector<Value> operands(taken ? branch.getTrueDestOperands()
                                        : branch.getFalseDestOperands());
      Location location = branch.getLoc();
      branch.erase();
      if (next != block && next->hasNoPredecessors() &&
          next->getNumArguments() == 0) {
        block->getOperations().splice(block->end(), next->getOperations());
        next->erase();
        continue;
      }
      OpBuilder branchBuilder = OpBuilder::atBlockEnd(block);
      cf::BranchOp::create(branchBuilder, location, next, operands);
    }
    if (compare->use_empty())
      compare.erase();
  }
  if (ok.use_empty())
    ok.getDefiningOp()->erase();
}

class ThinNativeGCCollectionsPass final
    : public impl::ThinNativeGCCollectionsPassBase<
          ThinNativeGCCollectionsPass> {
  void runOnOperation() override {
    // Group the points of each function: a generated loop collects within
    // one function, so its own periodic points govern its slot points.
    llvm::DenseMap<Operation *, SmallVector<schedule::NativeGCCollectOp>>
        byFunction;
    getOperation().walk([&](schedule::NativeGCCollectOp collection) {
      Operation *function =
          collection->getParentWithTrait<OpTrait::IsIsolatedFromAbove>();
      byFunction[function].push_back(collection);
    });
    for (auto &[function, collections] : byFunction) {
      (void)function;
      // The rising edge of the fastest clock: the most frequent cycle bounds
      // deferred pressure best. Ties go to the lowest clock index.
      schedule::NativeGCCollectOp kept;
      for (schedule::NativeGCCollectOp collection : collections) {
        if (!collection.getPeriodicClock() || !collection.getPosedge())
          continue;
        if (!kept ||
            std::make_pair(*collection.getPeriod(),
                           *collection.getPeriodicClock()) <
                std::make_pair(*kept.getPeriod(), *kept.getPeriodicClock()))
          kept = collection;
      }
      if (!kept)
        continue;
      for (schedule::NativeGCCollectOp collection : collections)
        if (collection != kept)
          removeCollection(collection);
    }
    // Only single operations change; the native pipeline's cached plan,
    // symbols and CFGs stay valid.
    markAllAnalysesPreserved();
  }
};

class LowerNativeGCCollectionsPass final
    : public impl::LowerNativeGCCollectionsPassBase<
          LowerNativeGCCollectionsPass> {
  void runOnOperation() override {
    SmallVector<schedule::NativeGCCollectOp> collections;
    getOperation().walk([&](schedule::NativeGCCollectOp collection) {
      collections.push_back(collection);
    });
    MLIRContext *context = &getContext();
    for (schedule::NativeGCCollectOp collection : collections) {
      Location location = collection.getLoc();
      OpBuilder builder(collection);
      Block *join = nullptr;
      if (Value edge = collection.getEdge()) {
        // Collect only in a slot where the edge occurred.
        Block *head = collection->getBlock();
        join = head->splitBlock(std::next(collection->getIterator()));
        Value status = join->addArgument(builder.getI32Type(), location);
        collection.getStatus().replaceAllUsesWith(status);
        Block *collect = new Block;
        head->getParent()->getBlocks().insert(Region::iterator(join), collect);
        builder.setInsertionPoint(collection);
        Value ok = arith::ConstantOp::create(
            builder, location, builder.getI32IntegerAttr(OBELISK_RT_OK));
        cf::CondBranchOp::create(builder, location, edge, collect, ValueRange{},
                                 join, ValueRange{ok});
        builder.setInsertionPointToStart(collect);
      }
      Value runtimeContext =
          UnrealizedConversionCastOp::create(builder, location,
                                             runtime::ContextType::get(context),
                                             collection.getRuntimeContext())
              .getResult(0);
      Value status = runtime::RTGCCollectOp::create(
          builder, location, runtime::StatusType::get(context), runtimeContext);
      Value bits = runtime::RTStatusToBitsOp::create(
          builder, location, builder.getI32Type(), status);
      if (join)
        cf::BranchOp::create(builder, location, join, ValueRange{bits});
      else
        collection.getStatus().replaceAllUsesWith(bits);
      collection.erase();
    }
    markAllAnalysesPreserved();
  }
};

} // namespace
} // namespace obelisk
