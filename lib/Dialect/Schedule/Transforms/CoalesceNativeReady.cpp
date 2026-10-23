#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Threading.h"
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
LLVM::GlobalOp
NativeGlobalLookup::operator()(LLVM::AddressOfOp address) const {
  if (symbols)
    return address.getGlobal(*symbols);
  return dyn_cast_or_null<LLVM::GlobalOp>(
      module->lookup(address.getGlobalNameAttr().getAttr()));
}
namespace {
using Key = std::tuple<Value, SymbolRefAttr, uint64_t, uint64_t>;
Key readyKey(NativeReadyUpdateOp update, const NativeGlobalLookup &globals) {
  Value base = update.getBase();
  SymbolRefAttr global;
  if (auto address = base.getDefiningOp<LLVM::AddressOfOp>();
      address && globals(address)) {
    global = address.getGlobalNameAttr();
    base = {};
  }
  return {base, global, update.getCapacity(), update.getWord()};
}
using Pending = llvm::DenseMap<Key, Operation *>;
} // namespace
// Keep the existing straight-line rewrite, then preserve the remaining CFG as
// one explicit readiness lifetime. Lowering handles observation/alias barriers
// inside it rather than assuming that every operation is unobservable. A
// function result is carried out of the region by its yields, so returning a
// status does not keep a body out of the region.
static void formReadyRegion(Operation *operation) {
  auto function = dyn_cast<FunctionOpInterface>(operation);
  if (!function || function.isExternal())
    return;
  Region &body = function.getFunctionBody();
  TypeRange results = function.getResultTypes();
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
    if (end->hasTrait<OpTrait::ReturnLike>() &&
        end->getOperandTypes() == results)
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
  auto region =
      NativeReadyRegionOp::create(builder, operation->getLoc(), results);
  region.getBody().takeBody(saved);
  IRMapping mapping;
  mapping.map(returns.front()->getOperands(), region.getResults());
  builder.clone(*returns.front(), mapping);
  for (Operation *end : returns) {
    builder.setInsertionPoint(end);
    NativeReadyYieldOp::create(builder, end->getLoc(), end->getOperands());
    end->erase();
  }
}
/// Ready updates of one function and their symbol-resolved keys.
struct FunctionReadyUpdates {
  Operation *function = nullptr;
  SmallVector<NativeReadyUpdateOp> updates;
  DenseMap<Operation *, Key> keys;
};
static FunctionReadyUpdates
collectReadyUpdates(Operation *operation, const NativeGlobalLookup &globals) {
  FunctionReadyUpdates result;
  result.function = operation;
  operation->walk(
      [&](NativeReadyUpdateOp op) { result.updates.push_back(op); });
  if (result.updates.size() >= 2)
    for (auto update : result.updates)
      result.keys.try_emplace(update, readyKey(update, globals));
  return result;
}
/// Rewrites only `input.function`, so distinct functions are independent.
static void coalesceFunction(FunctionReadyUpdates &input) {
  Operation *operation = input.function;
  auto &updates = input.updates;
  auto &keys = input.keys;
  if (updates.size() < 2)
    return;
  // A chain links an update only to the pending update of its key earlier in
  // its own block: CFG joins materialize at block boundaries until
  // path-specific pending block arguments are available. Scanning each block
  // finds exactly those, without per-point state over a whole large body.
  DenseMap<Operation *, Operation *> previousUpdates;
  llvm::SmallPtrSet<Block *, 16> blocks;
  for (auto update : updates)
    blocks.insert(update->getBlock());
  for (Block *block : blocks) {
    Pending pending;
    for (Operation &op : *block) {
      auto update = dyn_cast<NativeReadyUpdateOp>(&op);
      if (!update) {
        if (!isMemoryEffectFree(&op) || !isSpeculatable(&op) ||
            op.getNumRegions())
          pending.clear();
        continue;
      }
      auto key = keys.lookup(update);
      auto [base, global, capacity, word] = key;
      // Distinct globals cannot alias. Opaque pointers and inconsistent
      // layouts require materialization rather than a guessed disjoint range.
      if (llvm::any_of(pending, [&](const auto &entry) {
            auto [otherBase, otherGlobal, otherCapacity, otherWord] =
                entry.first;
            if (global && otherGlobal && global != otherGlobal)
              return false;
            return base != otherBase || global != otherGlobal ||
                   capacity != otherCapacity;
          }))
        pending.clear();
      if (Operation *previous = pending.lookup(key))
        previousUpdates[update] = previous;
      pending[key] = update;
    }
  }
  SmallVector<SmallVector<NativeReadyUpdateOp>> chains;
  DenseMap<Operation *, unsigned> memberships;
  for (auto update : updates) {
    auto previous = dyn_cast_or_null<NativeReadyUpdateOp>(
        previousUpdates.lookup(update));
    auto member = memberships.find(previous);
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
  SymbolTableCollection symbols;
  coalesceNativeReadyUpdates(operation, symbols);
}
void coalesceNativeReadyUpdates(Operation *operation,
                                const NativeGlobalLookup &globals) {
  // Inventories are cheap; allocate solver state only for functions that
  // actually have executable ready actions, not millions of unrelated LLVM ops.
  if (isa<FunctionOpInterface>(operation)) {
    FunctionReadyUpdates input = collectReadyUpdates(operation, globals);
    return coalesceFunction(input);
  }
  // Resolve keys serially with one symbol cache. Each function's chains and
  // ready regions then stay inside its own body, so large generated bodies
  // are solved concurrently.
  SmallVector<FunctionReadyUpdates> inputs;
  operation->walk([&](Operation *op) {
    if (!isa<FunctionOpInterface>(op))
      return;
    FunctionReadyUpdates input = collectReadyUpdates(op, globals);
    if (input.updates.size() >= 2)
      inputs.push_back(std::move(input));
  });
  parallelForEach(operation->getContext(), inputs,
                  [](FunctionReadyUpdates &input) { coalesceFunction(input); });
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
