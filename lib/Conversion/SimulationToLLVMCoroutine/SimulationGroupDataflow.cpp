//===- SimulationGroupDataflow.cpp - Predicated native computation --------===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Runtime/ReadySet.h"

#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/Mem2Reg.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"

#include <map>

using namespace mlir;

namespace obelisk::detail {

/// Prove and materialize a finite computation as dense, predicated dataflow.
/// The lattice at each CFG edge is (reachable, block arguments, memory slots).
/// Joins select the state of the actual predecessor, including pending bits.
/// Thus an inactive actor retains its old outputs, even after a foreign
/// deposit. No idempotence or inferred latch/always_comb invariant is assumed.
///
/// Only nontrapping pure expressions may execute speculatively. Exact static
/// loads/stores become SSA and publish at return. Calls, opaque effects,
/// loops and unproved memory accesses reject the candidate. Overlapping exact
/// byte windows share one canonical SSA slot using the target's byte order.
/// The original ranked executor remains authoritative on rejection.
bool materializeNativeGroupDataflow(LLVM::LLVMFuncOp function,
                                    SymbolTable &symbols, uint64_t budget) {
  // Publication lowering can leave unused context loads. Remove dead code
  // before the effect inventory; it is not part of the executable proof.
  IRRewriter rewriter(function.getContext());
  // Native lowering may leave private integer temporaries in stack slots.
  // Promote only allocations whose complete use/alias proof the standard
  // memory-slot interfaces establish. An escaping pointer stays a boundary.
  // Doing this before the effect inventory exposes the actual computation
  // without creating runtime children for a lowering artifact.
  SmallVector<PromotableAllocationOpInterface> allocations;
  function.walk([&](LLVM::AllocaOp allocation) {
    allocations.push_back(cast<PromotableAllocationOpInterface>(
        allocation.getOperation()));
  });
  if (!allocations.empty() &&
      function.getBody().getBlocks().size() <= budget / allocations.size()) {
    DominanceInfo dominance(function);
    (void)tryToPromoteMemorySlots(allocations, rewriter,
                                  DataLayout::closest(function), dominance);
  }
  (void)runRegionDCE(rewriter, function->getRegions());
  auto reject = [&](StringRef reason, Operation *operation = nullptr) {
    if (function->getParentOfType<ModuleOp>()->hasAttr(
            "obelisk.debug.native_timing")) {
      llvm::errs() << "obelisk group dataflow rejected: "
                   << function.getSymName() << " " << reason;
      if (operation)
        llvm::errs() << " (" << *operation << ")";
      llvm::errs() << '\n';
    }
    return false;
  };
  auto ingress =
      function->getAttrOfType<FlatSymbolRefAttr>("obelisk.eval.group_ingress");
  if (!ingress || function.empty())
    return false;
  struct Slot {
    std::string global;
    uint64_t offset;
    IntegerType type;
    bool written = false;
  };
  SmallVector<Slot> slots;
  std::map<std::tuple<std::string, uint64_t, unsigned>, unsigned> rangeIndex;
  DenseMap<Operation *, unsigned> accesses;
  auto identify = [&](Value pointer,
                      IntegerType type) -> std::optional<unsigned> {
    if (!type || type.getWidth() % 8 || type.getWidth() > 256)
      return {};
    uint64_t offset = 0;
    while (auto gep = pointer.getDefiningOp<LLVM::GEPOp>()) {
      if (!gep.getElemType().isInteger(8) || gep.getIndices().size() != 1)
        return {};
      APInt index;
      auto raw = gep.getIndices()[0];
      if (auto integer = dyn_cast<IntegerAttr>(raw))
        index = integer.getValue();
      else if (!matchPattern(cast<Value>(raw), m_ConstantInt(&index)))
        return {};
      if (index.isNegative() || index.getActiveBits() > 64 ||
          index.getZExtValue() > UINT64_MAX - offset)
        return {};
      offset += index.getZExtValue();
      pointer = gep.getBase();
    }
    auto address = pointer.getDefiningOp<LLVM::AddressOfOp>();
    if (!address || (address.getGlobalName() != ingress.getValue() &&
                     address.getGlobalName() != "__obelisk_state_value" &&
                     address.getGlobalName() != "__obelisk_state_unknown"))
      return {};
    auto global = symbols.lookup<LLVM::GlobalOp>(address.getGlobalName());
    auto array = global ? dyn_cast<LLVM::LLVMArrayType>(global.getGlobalType())
                        : LLVM::LLVMArrayType{};
    auto element =
        array ? dyn_cast<IntegerType>(array.getElementType()) : IntegerType{};
    if (!element || element.getWidth() % 8)
      return {};
    if (array.getNumElements() > UINT64_MAX / (element.getWidth() / 8))
      return {};
    uint64_t bytes = array.getNumElements() * (element.getWidth() / 8);
    if (offset > bytes || type.getWidth() / 8 > bytes - offset)
      return {};
    auto [it, inserted] = rangeIndex.try_emplace(
        std::tuple{address.getGlobalName().str(), offset, type.getWidth()},
        slots.size());
    if (inserted)
      slots.push_back({address.getGlobalName().str(), offset, type});
    return it->second;
  };

  DenseMap<Block *, unsigned> indegree;
  SmallVector<Block *> order;
  uint64_t originalOperations = 0;
  for (Block &block : function.getBody()) {
    indegree.try_emplace(&block, 0);
    if (&block != &function.getBody().front())
      for (BlockArgument arg : block.getArguments())
        if (!isa<IntegerType>(arg.getType()))
          return false;
    for (Operation &operation : block) {
      ++originalOperations;
      if (operation.getNumRegions())
        return false;
      if (auto load = dyn_cast<LLVM::LoadOp>(operation)) {
        if (load.getVolatile_() ||
            load.getOrdering() != LLVM::AtomicOrdering::not_atomic)
          return false;
        auto slot =
            identify(load.getAddr(), dyn_cast<IntegerType>(load.getType()));
        if (!slot)
          return reject("unproved load range", load);
        accesses[load] = *slot;
      } else if (auto store = dyn_cast<LLVM::StoreOp>(operation)) {
        if (store.getVolatile_() ||
            store.getOrdering() != LLVM::AtomicOrdering::not_atomic)
          return false;
        auto slot = identify(store.getAddr(),
                             dyn_cast<IntegerType>(store.getValue().getType()));
        if (!slot)
          return reject("unproved store range", store);
        accesses[store] = *slot;
        slots[*slot].written = true;
      } else if (isa<LLVM::BrOp, LLVM::CondBrOp, LLVM::ReturnOp>(operation)) {
        if (isa<LLVM::ReturnOp>(operation) && operation.getNumOperands())
          return reject("effect or unsafe speculation", &operation);
      } else if (!isa<LLVM::AddressOfOp, LLVM::GEPOp>(operation)) {
        // Pure is insufficient: e.g. division by zero has immediate UB.
        // Speculatability also excludes potentially trapping instructions.
        if (isa<LLVM::CallOp, LLVM::InvokeOp, LLVM::InlineAsmOp, LLVM::UDivOp,
                LLVM::SDivOp, LLVM::URemOp, LLVM::SRemOp>(operation) ||
            !isMemoryEffectFree(&operation) || !isSpeculatable(&operation) ||
            operation.hasTrait<OpTrait::IsTerminator>())
          return reject("effect or unsafe speculation", &operation);
      }
    }
    for (Block *next : block.getSuccessors())
      ++indegree[next];
  }
  if (slots.empty() || originalOperations > budget)
    return reject("empty memory or operation budget");
  // Canonicalize proven physical aliases with a sorted interval sweep. This
  // never unions copy-connected descriptors: both accesses must resolve to
  // literal overlapping bytes of the same authoritative global. Preserve the
  // other bytes of a partial store, including canonical X/Z in the mask plane.
  std::optional<bool> littleEndian;
  if (auto layout = function->getParentOfType<ModuleOp>()->getAttrOfType<StringAttr>(
          "llvm.data_layout")) {
    auto parsed = llvm::DataLayout::parse(layout.getValue());
    if (!parsed) {
      llvm::consumeError(parsed.takeError());
      return reject("invalid target layout");
    }
    littleEndian = parsed->isLittleEndian();
  }
  struct Access {
    unsigned slot, byteOffset, width;
  };
  SmallVector<Access> canonical(slots.size());
  SmallVector<Slot> merged;
  for (auto [key, id] : rangeIndex) {
    const Slot &slot = slots[id];
    if (!merged.empty() && merged.back().global == slot.global &&
        merged.back().offset + merged.back().type.getWidth() / 8 > slot.offset) {
      Slot &previous = merged.back();
      uint64_t end = std::max(previous.offset + previous.type.getWidth() / 8,
                              slot.offset + slot.type.getWidth() / 8);
      if (!littleEndian || end - previous.offset > 32)
        return reject("unproved or oversized overlapping physical ranges");
      previous.type = IntegerType::get(function.getContext(),
                                       (end - previous.offset) * 8);
      previous.written |= slot.written;
    } else
      merged.push_back(slot);
    canonical[id] = {unsigned(merged.size() - 1),
                     unsigned(slot.offset - merged.back().offset),
                     slot.type.getWidth()};
  }
  unsigned mergedRanges = slots.size() - merged.size();
  if (mergedRanges)
    slots = std::move(merged);
  else
    for (auto [id, slot] : llvm::enumerate(slots))
      canonical[id] = {unsigned(id), 0, slot.type.getWidth()};
  std::map<std::pair<std::string, uint64_t>, unsigned> slotIndex;
  for (auto [id, slot] : llvm::enumerate(slots))
    slotIndex.try_emplace(std::pair{slot.global, slot.offset}, id);
  if (indegree[&function.getBody().front()] != 0)
    return false;
  order.push_back(&function.getBody().front());
  for (size_t i = 0; i < order.size(); ++i)
    for (Block *next : order[i]->getSuccessors())
      if (--indegree[next] == 0)
        order.push_back(next);
  if (order.size() != function.getBody().getBlocks().size() ||
      order.size() > budget / slots.size())
    return reject("cyclic/unreachable CFG or analysis budget");

  // At a complete reconvergence, reachability equals that of the controlling
  // dominator. This structural proof also holds when an inactive branch's
  // expressions would produce poison. Reconstructing it as Boolean algebra
  // leaves large select/OR trees that ordinary constant folding cannot safely
  // reduce without the original control-flow fact.
  DominanceInfo dominance(function);
  PostDominanceInfo postDominance(function);
  DenseMap<Block *, Block *> reconvergence;
  for (Block *block : llvm::drop_begin(order)) {
    auto *parent = dominance.getNode(block)->getIDom();
    if (parent && postDominance.postDominates(block, parent->getBlock()))
      reconvergence[block] = parent->getBlock();
  }

  Region replacement;
  auto *entry = new Block;
  replacement.push_back(entry);
  OpBuilder builder(function.getContext());
  builder.setInsertionPointToStart(entry);
  Location loc = function.getLoc();
  IRMapping mapping;
  for (BlockArgument arg : function.getBody().front().getArguments())
    mapping.map(arg, entry->addArgument(arg.getType(), arg.getLoc()));
  Value yes = llvmConstant(builder, loc, builder.getI1Type(), 1);
  Value no = llvmConstant(builder, loc, builder.getI1Type(), 0);
  auto select = [&](Value predicate, Value lhs, Value rhs) -> Value {
    if (lhs == rhs)
      return lhs;
    APInt constant;
    if (matchPattern(predicate, m_ConstantInt(&constant)))
      return constant.isZero() ? rhs : lhs;
    return LLVM::SelectOp::create(builder, loc, predicate, lhs, rhs);
  };
  SmallVector<Value> initial, addresses;
  for (const Slot &slot : slots) {
    Value base = LLVM::AddressOfOp::create(
        builder, loc, LLVM::LLVMPointerType::get(function.getContext()),
        slot.global);
    Value address = byteGEP(builder, loc, base, slot.offset);
    addresses.push_back(address);
    initial.push_back(
        LLVM::LoadOp::create(builder, loc, slot.type, address, 1));
  }
  struct Edge {
    Value predicate;
    SmallVector<Value> arguments, memory;
  };
  DenseMap<Block *, SmallVector<Edge, 2>> incoming;
  DenseMap<Block *, Value> reachability;
  SmallVector<Edge> returns;
  for (Block *block : order) {
    auto &edges = incoming[block];
    Value active = yes;
    SmallVector<Value> memory = initial;
    if (block != order.front()) {
      active = edges.front().predicate;
      memory = edges.front().memory;
      SmallVector<Value> arguments = edges.front().arguments;
      for (const Edge &edge : llvm::drop_begin(edges)) {
        active = LLVM::OrOp::create(builder, loc, active, edge.predicate);
        for (size_t i = 0; i < memory.size(); ++i)
          memory[i] = select(edge.predicate, edge.memory[i], memory[i]);
        for (size_t i = 0; i < arguments.size(); ++i)
          arguments[i] =
              select(edge.predicate, edge.arguments[i], arguments[i]);
      }
      for (auto [arg, value] : llvm::zip(block->getArguments(), arguments))
        mapping.map(arg, value);
    }
    if (Block *parent = reconvergence.lookup(block))
      active = reachability.lookup(parent);
    reachability[block] = active;
    auto edge = [&](Block *target, Value predicate, ValueRange args) {
      Edge next{predicate, {}, memory};
      for (Value value : args)
        next.arguments.push_back(mapping.lookup(value));
      incoming[target].push_back(std::move(next));
    };
    for (Operation &operation : *block) {
      if (auto load = dyn_cast<LLVM::LoadOp>(operation)) {
        Access access = canonical[accesses.lookup(load)];
        Value value = memory[access.slot];
        auto type = slots[access.slot].type;
        unsigned shift = littleEndian.value_or(true)
                             ? access.byteOffset * 8
                             : type.getWidth() - access.byteOffset * 8 - access.width;
        if (shift)
          value = LLVM::LShrOp::create(builder, loc, value,
                                       llvmConstant(builder, loc, type, shift));
        if (access.width != type.getWidth())
          value = LLVM::TruncOp::create(builder, loc, load.getType(), value);
        mapping.map(load.getResult(), value);
      } else if (auto store = dyn_cast<LLVM::StoreOp>(operation)) {
        Access access = canonical[accesses.lookup(store)];
        Value value = mapping.lookup(store.getValue());
        auto type = slots[access.slot].type;
        if (access.width != type.getWidth()) {
          unsigned shift = littleEndian.value_or(true)
                               ? access.byteOffset * 8
                               : type.getWidth() - access.byteOffset * 8 - access.width;
          value = LLVM::ZExtOp::create(builder, loc, type, value);
          if (shift)
            value = LLVM::ShlOp::create(builder, loc, value,
                                        llvmConstant(builder, loc, type, shift));
          APInt keep = ~APInt::getBitsSet(type.getWidth(), shift, shift + access.width);
          Value preserved = LLVM::AndOp::create(
              builder, loc, memory[access.slot],
              LLVM::ConstantOp::create(builder, loc, type, keep));
          value = LLVM::OrOp::create(builder, loc, preserved, value);
        }
        memory[access.slot] = value;
      } else if (auto branch = dyn_cast<LLVM::BrOp>(operation))
        edge(branch.getDest(), active, branch.getDestOperands());
      else if (auto branch = dyn_cast<LLVM::CondBrOp>(operation)) {
        Value condition = mapping.lookup(branch.getCondition());
        Value inverse = LLVM::XOrOp::create(builder, loc, condition, yes);
        // Select (not AND) prevents an inactive path's poison condition from
        // poisoning the reachable-state predicate.
        edge(branch.getTrueDest(), select(active, condition, no),
             branch.getTrueDestOperands());
        edge(branch.getFalseDest(), select(active, inverse, no),
             branch.getFalseDestOperands());
      } else if (isa<LLVM::ReturnOp>(operation))
        returns.push_back({active, {}, memory});
      else
        builder.clone(operation, mapping);
    }
  }
  // A finite, fully covered CFG reaches exactly one return. The first return
  // is the default arm; no additional whole-function activation guard exists.
  SmallVector<Value> final = returns.front().memory;
  for (const Edge &edge : llvm::drop_begin(returns))
    for (size_t i = 0; i < final.size(); ++i)
      final[i] = select(edge.predicate, edge.memory[i], final[i]);
  // Internal publications can leave the minimum cache pointing at work that
  // this finite group has already consumed. Advance that lower bound only
  // through exact, final SSA words proved empty. Missing words stop progress;
  // no ready bit is consumed and no additional state load is introduced.
  auto rawCount = function->getAttrOfType<IntegerAttr>(
      "obelisk.eval.ready_word_count");
  if (rawCount && rawCount.getUInt() > 1 &&
      rawCount.getUInt() <= runtime::ReadySetLayout::flatWordLimit) {
    uint64_t count = rawCount.getUInt();
    auto readyGlobal = symbols.lookup<LLVM::GlobalOp>(ingress.getValue());
    auto readyType = readyGlobal
                         ? dyn_cast<LLVM::LLVMArrayType>(readyGlobal.getGlobalType())
                         : LLVM::LLVMArrayType{};
    auto cache = slotIndex.find({ingress.getValue().str(), count * 8});
    if (readyType && readyType.getElementType().isInteger(64) &&
        readyType.getNumElements() == count + 1 && cache != slotIndex.end() &&
        slots[cache->second].type.getWidth() == 64) {
      unsigned cacheID = cache->second;
      Value lower = final[cacheID];
      uint64_t hints = 0;
      for (auto [key, id] : slotIndex) {
        const Slot &slot = slots[id];
        if (slot.global != ingress.getValue() || slot.offset % 8 ||
            slot.offset / 8 >= count || slot.type.getWidth() != 64)
          continue;
        uint64_t word = slot.offset / 8;
        Value atWord = LLVM::ICmpOp::create(
            builder, loc, LLVM::ICmpPredicate::eq, lower,
            llvmConstant(builder, loc, builder.getI64Type(), word));
        Value empty = LLVM::ICmpOp::create(
            builder, loc, LLVM::ICmpPredicate::eq, final[id],
            llvmConstant(builder, loc, builder.getI64Type(), 0));
        Value advance = LLVM::AndOp::create(builder, loc, atWord, empty);
        lower = select(advance,
                       llvmConstant(builder, loc, builder.getI64Type(), word + 1),
                       lower);
        ++hints;
      }
      if (hints) {
        final[cacheID] = lower;
        slots[cacheID].written = true;
        function->setAttr("obelisk.eval.cache_hint_words",
                           builder.getI64IntegerAttr(hints));
      }
    }
  }
  for (size_t i = 0; i < slots.size(); ++i)
    if (slots[i].written)
      LLVM::StoreOp::create(builder, loc, final[i], addresses[i], 1);
  LLVM::ReturnOp::create(builder, loc, ValueRange{});
  // Bound materialized growth as well as the analysis work. Failed candidates
  // are discarded before connecting any call site to this representation.
  if (entry->getOperations().size() > budget)
    return reject("materialization budget");
  function.getBody().takeBody(replacement);
  function->setAttr("obelisk.eval.predicated_dataflow", builder.getUnitAttr());
  if (mergedRanges)
    function->setAttr("obelisk.eval.coalesced_slots",
                       builder.getI64IntegerAttr(mergedRanges));
  function->setAttr("obelisk.eval.dataflow_slots",
                    builder.getI64IntegerAttr(slots.size()));
  return true;
}

} // namespace obelisk::detail
