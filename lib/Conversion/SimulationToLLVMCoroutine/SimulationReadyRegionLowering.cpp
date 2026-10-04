#include "SimulationEvalReadySet.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/Mem2Reg.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringSet.h"
#include <map>

using namespace mlir;
namespace obelisk::detail {
namespace {
struct Key {
  StringRef global;
  uint64_t capacity, word;
  bool operator<(const Key &other) const {
    return std::tie(global, capacity, word) <
           std::tie(other.global, other.capacity, other.word);
  }
};
std::optional<Key> keyFor(Operation *op, SymbolTableCollection &symbols) {
  Value base;
  uint64_t capacity, word;
  if (auto update = dyn_cast<schedule::NativeReadyUpdateOp>(op)) {
    base = update.getBase();
    capacity = update.getCapacity();
    word = update.getWord();
  } else if (auto commit = dyn_cast<schedule::NativeReadyCommitOp>(op)) {
    base = commit.getBase();
    capacity = commit.getCapacity();
    word = commit.getWord();
  } else if (auto read = dyn_cast<schedule::NativeReadyReadOp>(op)) {
    base = read.getBase();
    capacity = read.getCapacity();
    word = read.getWord();
  } else {
    return std::nullopt;
  }
  auto address = base.getDefiningOp<LLVM::AddressOfOp>();
  if (!address || !address.getGlobal(symbols))
    return std::nullopt;
  return Key{address.getGlobalName(), capacity, word};
}

// Ordinary accesses to distinct LLVM globals or local allocas cannot inspect
// the ready set. All other memory/control effects materialize it first. This
// includes indirect aliases, atomics, callbacks, transitions and NBA actions.
bool isBoundary(Operation *op, const std::map<Key, unsigned> &keys,
                SymbolTableCollection &symbols) {
  if (isa<schedule::NativeReadyYieldOp, CallOpInterface>(op))
    return true;
  auto load = dyn_cast<LLVM::LoadOp>(op);
  auto store = dyn_cast<LLVM::StoreOp>(op);
  if (load || store) {
    bool plain =
        load ? !load.getVolatile_() &&
                   load.getOrdering() == LLVM::AtomicOrdering::not_atomic
             : !store.getVolatile_() &&
                   store.getOrdering() == LLVM::AtomicOrdering::not_atomic;
    if (!plain)
      return true;
    Value root = load ? load.getAddr() : store.getAddr();
    while (auto gep = root.getDefiningOp<LLVM::GEPOp>())
      root = gep.getBase();
    if (root.getDefiningOp<LLVM::AllocaOp>())
      return false;
    auto address = root.getDefiningOp<LLVM::AddressOfOp>();
    if (!address || !address.getGlobal(symbols))
      return true;
    return llvm::any_of(keys, [&](const auto &entry) {
      return entry.first.global == address.getGlobalName();
    });
  }
  return !isa<LLVM::AllocaOp>(op) && !isMemoryEffectFree(op);
}
} // namespace

void lowerNativeReadyRegion(schedule::NativeReadyRegionOp region,
                            SymbolTableCollection &symbols) {
  std::map<Key, unsigned> keys;
  SmallVector<Operation *> operations;
  llvm::StringMap<uint64_t> capacities;
  llvm::StringSet<> inconsistent;
  for (Block &block : region.getBody())
    for (Operation &op : block) {
      operations.push_back(&op);
      if (auto key = keyFor(&op, symbols)) {
        ++keys[*key];
        auto [prior, inserted] =
            capacities.try_emplace(key->global, key->capacity);
        if (!inserted && prior->second != key->capacity)
          inconsistent.insert(key->global);
      }
    }
  for (auto it = keys.begin(); it != keys.end();)
    if (inconsistent.contains(it->first.global))
      it = keys.erase(it);
    else
      ++it;
  // Bound generated flush code for bodies with many opaque call boundaries.
  unsigned boundaries = llvm::count_if(operations, [&](Operation *op) {
    auto key = keyFor(op, symbols);
    return !(key && keys.count(*key)) && isBoundary(op, keys, symbols);
  });
  if (keys.size() > 64 || keys.size() * uint64_t(boundaries + 1) > 4096)
    keys.clear();

  // Dense forward may-pending dataflow. A boundary only needs the words that
  // can reach it since the preceding boundary, including loop backedges. In
  // particular, do not multiply every helper call by every word in a group.
  unsigned index = 0;
  for (auto &[key, id] : keys)
    id = index++;
  DenseMap<Operation *, uint64_t> additions;
  llvm::SmallPtrSet<Operation *, 16> barriers;
  for (Operation *op : operations) {
    auto key = keyFor(op, symbols);
    if (key && keys.count(*key)) {
      if (!isa<schedule::NativeReadyReadOp>(op))
        additions[op] = uint64_t{1} << keys.at(*key);
    } else if (isBoundary(op, keys, symbols))
      barriers.insert(op);
  }
  DenseMap<Block *, uint64_t> incoming, outgoing;
  llvm::SetVector<Block *> worklist;
  for (Block &block : region.getBody())
    worklist.insert(&block);
  while (!worklist.empty()) {
    Block *block = worklist.pop_back_val();
    uint64_t bits = 0;
    for (Block *predecessor : block->getPredecessors())
      bits |= outgoing.lookup(predecessor);
    incoming[block] = bits;
    for (Operation &op : *block) {
      bits |= additions.lookup(&op);
      if (barriers.contains(&op))
        bits = 0;
    }
    if (outgoing.lookup(block) == bits)
      continue;
    outgoing[block] = bits;
    for (Block *successor : block->getSuccessors())
      worklist.insert(successor);
  }
  DenseMap<Operation *, uint64_t> flushMasks;
  uint64_t profitable = 0;
  for (Block &block : region.getBody()) {
    uint64_t bits = incoming.lookup(&block);
    for (Operation &op : block) {
      // A word benefits only if at least one path can combine two actions
      // before observation. This includes a single site on a loop backedge.
      profitable |= bits & additions.lookup(&op);
      bits |= additions.lookup(&op);
      if (barriers.contains(&op)) {
        flushMasks[&op] = bits;
        bits = 0;
      }
    }
  }

  OpBuilder b(region);
  auto loc = region.getLoc();
  auto i64 = b.getI64Type();
  auto ptr = LLVM::LLVMPointerType::get(b.getContext());
  Value zero = llvmConstant(b, loc, i64, 0);
  Value ones = llvmConstant(b, loc, i64, UINT64_MAX);
  struct Pending {
    Value base, remove, add;
  };
  std::map<Key, Pending> pending;
  SmallVector<PromotableAllocationOpInterface> allocations;
  auto load = [&](Value slot) -> Value {
    return LLVM::LoadOp::create(b, loc, i64, slot, 8);
  };
  auto store = [&](Value value, Value slot) {
    LLVM::StoreOp::create(b, loc, value, slot, 8);
  };
  for (auto &[key, id] : keys) {
    if (!(profitable & (uint64_t{1} << id)))
      continue;
    Value base = LLVM::AddressOfOp::create(b, loc, ptr, key.global);
    Value remove = entryAlloca(b, loc, i64, 1, 8);
    Value add = entryAlloca(b, loc, i64, 1, 8);
    for (Value slot : {remove, add})
      allocations.push_back(
          cast<PromotableAllocationOpInterface>(slot.getDefiningOp()));
    store(zero, remove);
    store(zero, add);
    pending.emplace(key, Pending{base, remove, add});
  }
  auto flush = [&](uint64_t mask) {
    for (auto &[key, value] : pending) {
      if (!(mask & (uint64_t{1} << keys.at(key))))
        continue;
      schedule::NativeReadyCommitOp::create(
          b, loc, value.base, load(value.remove), load(value.add),
          b.getI64IntegerAttr(key.capacity), b.getI64IntegerAttr(key.word));
      store(zero, value.remove);
      store(zero, value.add);
    }
  };
  for (Operation *op : operations) {
    b.setInsertionPoint(op);
    auto key = keyFor(op, symbols);
    if (key && pending.count(*key)) {
      auto &value = pending.at(*key);
      if (auto read = dyn_cast<schedule::NativeReadyReadOp>(op)) {
        Value previous = loadOwnerWord(b, loc, value.base, key->word);
        if (auto owner =
                schedule::get<schedule::Field::EvalActivationEntry>(read))
          schedule::set<schedule::Field::EvalActivationEntry>(
              previous.getDefiningOp(), owner);
        Value keep =
            b.createOrFold<arith::XOrIOp>(loc, load(value.remove), ones);
        Value current = b.createOrFold<arith::OrIOp>(
            loc, b.createOrFold<arith::AndIOp>(loc, previous, keep),
            load(value.add));
        read.replaceAllUsesWith(current);
        read.erase();
        continue;
      }
      Value remove, add;
      if (auto update = dyn_cast<schedule::NativeReadyUpdateOp>(op)) {
        remove = update.getConsume() ? update.getMask() : zero;
        add = update.getConsume() ? zero : update.getMask();
      } else {
        auto commit = cast<schedule::NativeReadyCommitOp>(op);
        remove = commit.getRemoveMask();
        add = commit.getAddMask();
      }
      store(b.createOrFold<arith::OrIOp>(loc, load(value.remove), remove),
            value.remove);
      Value keep = b.createOrFold<arith::XOrIOp>(loc, remove, ones);
      store(b.createOrFold<arith::OrIOp>(
                loc, b.createOrFold<arith::AndIOp>(loc, load(value.add), keep),
                add),
            value.add);
      op->erase();
    } else if (auto mask = flushMasks.lookup(op)) {
      flush(mask);
    }
  }
  // Inline the CFG only after representing every exit and observation point.
  Block *head = region->getBlock();
  Block *next = head->splitBlock(Block::iterator(region));
  Block *first = &region.getBody().front();
  for (Block &block : region.getBody())
    if (auto yield =
            dyn_cast<schedule::NativeReadyYieldOp>(block.getTerminator())) {
      b.setInsertionPoint(yield);
      cf::BranchOp::create(b, loc, next);
      yield.erase();
    }
  head->getParent()->getBlocks().splice(Region::iterator(next),
                                        region.getBody().getBlocks());
  b.setInsertionPointToEnd(head);
  cf::BranchOp::create(b, loc, first);
  auto function = region->getParentOfType<FunctionOpInterface>();
  region.erase();
  // Do not charge enclosing group formation for the region's artificial
  // entry/exit blocks. Retain real CFG joins and loop backedges.
  if (first->getSinglePredecessor() == head) {
    head->getTerminator()->erase();
    head->getOperations().splice(head->end(), first->getOperations());
    first->erase();
  }
  if (Block *predecessor = next->getSinglePredecessor()) {
    predecessor->getTerminator()->erase();
    predecessor->getOperations().splice(predecessor->end(),
                                        next->getOperations());
    next->erase();
  }
  // Expose the accumulated masks as SSA now, before group formation and its
  // code-growth accounting. Deferring this to LLVM would leave artificial
  // local memory effects that prevent otherwise eligible groups from forming.
  if (function && !allocations.empty()) {
    b.setInsertionPointToStart(&function.getFunctionBody().front());
    DominanceInfo dominance(function);
    (void)tryToPromoteMemorySlots(allocations, b, DataLayout::closest(function),
                                  dominance);
  }
}
} // namespace obelisk::detail
