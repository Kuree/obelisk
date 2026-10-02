#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Schedule/Transforms/NativeTransforms.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "llvm/ADT/DenseMap.h"

using namespace mlir;

namespace obelisk::schedule {
namespace {
using Key = std::tuple<Value, SymbolRefAttr, uint64_t, uint64_t>;
Key readyKey(NativeReadyUpdateOp update) {
  Value base = update.getBase();
  SymbolRefAttr global;
  if (auto address = base.getDefiningOp<LLVM::AddressOfOp>()) {
    global = address.getGlobalNameAttr();
    base = {};
  }
  return {base, global, update.getCapacity(), update.getWord()};
}
using Pending = llvm::DenseMap<Key, Operation *>;
class ReadyLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  bool reachable = false;
  Pending pending;
  ChangeResult merge(const Pending &incoming) {
    if (!reachable) {
      reachable = true;
      pending = incoming;
      return ChangeResult::Change;
    }
    SmallVector<Key> remove;
    for (const auto &entry : pending)
      if (incoming.lookup(entry.first) != entry.second)
        remove.push_back(entry.first);
    for (const auto &key : remove)
      pending.erase(key);
    return remove.empty() ? ChangeResult::NoChange : ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const ReadyLattice &>(rhs);
    return other.reachable ? merge(other.pending) : ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override { os << pending.size(); }
};
class ReadyDataflow final
    : public dataflow::DenseForwardDataFlowAnalysis<ReadyLattice> {
public:
  using DenseForwardDataFlowAnalysis::DenseForwardDataFlowAnalysis;
  LogicalResult visitOperation(Operation *op, const ReadyLattice &before,
                               ReadyLattice *after) override {
    if (!before.reachable)
      return success();
    Pending next = before.pending;
    if (auto update = dyn_cast<NativeReadyUpdateOp>(op)) {
      auto key = readyKey(update);
      auto [base, global, capacity, word] = key;
      // Distinct globals cannot alias. Opaque pointers and inconsistent
      // layouts require materialization rather than a guessed disjoint range.
      if (llvm::any_of(next, [&](const auto &entry) {
            auto [otherBase, otherGlobal, otherCapacity, otherWord] =
                entry.first;
            if (global && otherGlobal && global != otherGlobal)
              return false;
            return base != otherBase || global != otherGlobal ||
                   capacity != otherCapacity;
          }))
        next.clear();
      next[key] = op;
    } else if (!isMemoryEffectFree(op) || !isSpeculatable(op)) {
      next.clear();
    }
    propagateIfChanged(after, after->merge(next));
    return success();
  }
  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const ReadyLattice &before,
                                    ReadyLattice *after) override {
    (void)visitOperation(call, before, after);
  }

private:
  void setToEntryState(ReadyLattice *state) override {
    propagateIfChanged(state, state->merge(Pending{}));
  }
};
} // namespace
static void coalesceFunction(Operation *operation) {
  SmallVector<NativeReadyUpdateOp> updates;
  operation->walk([&](NativeReadyUpdateOp op) { updates.push_back(op); });
  if (updates.size() < 2)
    return;
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<ReadyDataflow>();
  if (failed(solver.initializeAndRun(operation)))
    return;
  SmallVector<SmallVector<NativeReadyUpdateOp>> chains;
  DenseMap<Operation *, unsigned> memberships;
  for (auto update : updates) {
    const auto *before =
        solver.lookupState<ReadyLattice>(solver.getProgramPointBefore(update));
    auto previous = before && before->reachable
                        ? dyn_cast_or_null<NativeReadyUpdateOp>(
                              before->pending.lookup(readyKey(update)))
                        : NativeReadyUpdateOp{};
    auto member = memberships.find(previous);
    // CFG joins retain only equal reaching updates. Materialize at block
    // boundaries until path-specific pending block arguments are available.
    unsigned chain;
    if (previous && previous->getBlock() == update->getBlock() &&
        member != memberships.end()) {
      chain = member->second;
    } else {
      chain = chains.size();
      chains.emplace_back();
    }
    chains[chain].push_back(update);
    memberships.try_emplace(update, chain);
  }
  for (auto &chain : chains) {
    if (chain.size() < 2)
      continue;
    auto last = chain.back();
    OpBuilder builder(last);
    auto loc = last.getLoc();
    bool sameKind = llvm::all_of(
        chain, [&](auto op) { return op.getConsume() == last.getConsume(); });
    if (sameKind) {
      Value mask = chain.front().getMask();
      for (auto op : llvm::drop_begin(chain))
        mask = builder.createOrFold<arith::OrIOp>(loc, mask, op.getMask());
      last.getMaskMutable().assign(mask);
      for (auto op : llvm::drop_end(chain))
        op.erase();
      continue;
    }
    Value remove = arith::ConstantOp::create(builder, loc, builder.getI64Type(),
                                             builder.getI64IntegerAttr(0));
    Value add = remove;
    for (auto op : chain) {
      if (op.getConsume()) {
        remove = builder.createOrFold<arith::OrIOp>(loc, remove, op.getMask());
        Value keep = builder.createOrFold<arith::XOrIOp>(
            loc, op.getMask(),
            arith::ConstantOp::create(builder, loc, builder.getI64Type(),
                                      builder.getI64IntegerAttr(-1)));
        add = builder.createOrFold<arith::AndIOp>(loc, add, keep);
      } else {
        add = builder.createOrFold<arith::OrIOp>(loc, add, op.getMask());
      }
    }
    NativeReadyCommitOp::create(builder, loc, last.getBase(), remove, add,
                                last.getCapacityAttr(), last.getWordAttr());
    for (auto op : chain)
      op.erase();
  }
}
void coalesceNativeReadyUpdates(Operation *operation) {
  // Inventories are cheap; allocate solver state only for functions that
  // actually have executable ready actions, not millions of unrelated LLVM ops.
  if (isa<FunctionOpInterface>(operation))
    return coalesceFunction(operation);
  operation->walk([&](Operation *op) {
    if (isa<FunctionOpInterface>(op))
      coalesceFunction(op);
  });
}

} // namespace obelisk::schedule

namespace obelisk {
#define GEN_PASS_DEF_COALESCENATIVEREADYPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"
namespace {
struct CoalesceNativeReadyPass
    : impl::CoalesceNativeReadyPassBase<CoalesceNativeReadyPass> {
  void runOnOperation() override {
    schedule::coalesceNativeReadyUpdates(getOperation());
  }
};
} // namespace
} // namespace obelisk
