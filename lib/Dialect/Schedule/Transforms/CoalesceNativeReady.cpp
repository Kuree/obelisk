#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
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
Key readyKey(NativeReadyUpdateOp update, SymbolTableCollection &symbols) {
  Value base = update.getBase();
  SymbolRefAttr global;
  if (auto address = base.getDefiningOp<LLVM::AddressOfOp>();
      address && address.getGlobal(symbols)) {
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
  ReadyDataflow(DataFlowSolver &solver, const DenseMap<Operation *, Key> &keys)
      : DenseForwardDataFlowAnalysis(solver), keys(keys) {}
  LogicalResult visitOperation(Operation *op, const ReadyLattice &before,
                               ReadyLattice *after) override {
    if (!before.reachable)
      return success();
    Pending next = before.pending;
    if (auto update = dyn_cast<NativeReadyUpdateOp>(op)) {
      auto key = keys.lookup(update);
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
  const DenseMap<Operation *, Key> &keys;
  void setToEntryState(ReadyLattice *state) override {
    propagateIfChanged(state, state->merge(Pending{}));
  }
};
} // namespace
// Keep the existing straight-line rewrite, then preserve the remaining CFG as
// one explicit readiness lifetime. Lowering handles observation/alias barriers
// inside it rather than assuming that every operation is unobservable.
static void formReadyRegion(Operation *operation) {
  auto function = dyn_cast<FunctionOpInterface>(operation);
  if (!function || function.getNumResults() || function.isExternal())
    return;
  Region &body = function.getFunctionBody();
  SmallVector<Operation *> returns;
  unsigned updates = 0;
  bool supported = true;
  for (Block &block : body) {
    for (Operation &op : block) {
      if (op.getNumRegions())
        supported = false;
      if (auto update = dyn_cast<NativeReadyUpdateOp>(op))
        updates += bool(update.getBase().getDefiningOp<LLVM::AddressOfOp>());
      if (auto commit = dyn_cast<NativeReadyCommitOp>(op))
        updates += bool(commit.getBase().getDefiningOp<LLVM::AddressOfOp>());
    }
    auto *end = block.getTerminator();
    if (end->hasTrait<OpTrait::ReturnLike>() && !end->getNumOperands())
      returns.push_back(end);
    else if (!isa<BranchOpInterface>(end) || !isMemoryEffectFree(end))
      supported = false;
  }
  if (!supported || updates < 2 || returns.empty())
    return;
  Region saved;
  saved.takeBody(body);
  auto *entry = new Block;
  body.push_back(entry);
  for (BlockArgument old : saved.front().getArguments()) {
    Value argument = entry->addArgument(old.getType(), old.getLoc());
    old.replaceAllUsesWith(argument);
  }
  saved.front().eraseArguments([](BlockArgument) { return true; });
  OpBuilder builder(entry, entry->end());
  auto region = NativeReadyRegionOp::create(builder, operation->getLoc());
  region.getBody().takeBody(saved);
  builder.clone(*returns.front());
  for (Operation *end : returns) {
    builder.setInsertionPoint(end);
    NativeReadyYieldOp::create(builder, end->getLoc());
    end->erase();
  }
}
static void coalesceFunction(Operation *operation,
                             SymbolTableCollection &symbols) {
  SmallVector<NativeReadyUpdateOp> updates;
  operation->walk([&](NativeReadyUpdateOp op) { updates.push_back(op); });
  if (updates.size() < 2)
    return;
  DenseMap<Operation *, Key> keys;
  for (auto update : updates)
    keys.try_emplace(update, readyKey(update, symbols));
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<ReadyDataflow>(keys);
  if (failed(solver.initializeAndRun(operation)))
    return;
  SmallVector<SmallVector<NativeReadyUpdateOp>> chains;
  DenseMap<Operation *, unsigned> memberships;
  for (auto update : updates) {
    const auto *before =
        solver.lookupState<ReadyLattice>(solver.getProgramPointBefore(update));
    auto previous = before && before->reachable
                        ? dyn_cast_or_null<NativeReadyUpdateOp>(
                              before->pending.lookup(keys.lookup(update)))
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
  formReadyRegion(operation);
}
void coalesceNativeReadyUpdates(Operation *operation) {
  // Inventories are cheap; allocate solver state only for functions that
  // actually have executable ready actions, not millions of unrelated LLVM ops.
  SymbolTableCollection symbols;
  if (isa<FunctionOpInterface>(operation))
    return coalesceFunction(operation, symbols);
  operation->walk([&](Operation *op) {
    if (isa<FunctionOpInterface>(op))
      coalesceFunction(op, symbols);
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
