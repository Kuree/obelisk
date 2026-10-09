//===- ActivationIdempotenceAnalysis.cpp - Stable-input state proof
//--------===//
#include "obelisk/Analysis/ActivationIdempotenceAnalysis.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/SmallBitVector.h"

using namespace mlir;
namespace obelisk::analysis {
namespace {
using Range = ActivationMemoryAnalysis::Range;
bool overlaps(Range a, Range b) {
  return a.root == b.root && a.low < b.low + b.width && b.low < a.low + a.width;
}
struct Fact {
  llvm::SmallBitVector inputs;
  std::optional<unsigned> identity;
  explicit Fact(unsigned count) : inputs(count) {}
};
struct Path {
  Block *block;
  DenseMap<Value, Fact> values;
  SmallVector<Fact> cells;
  llvm::SmallBitVector controls;
};
} // namespace

bool hasIdempotentStateTransition(sim::SimFuncOp function,
                                  const ActivationMemoryAnalysis &memory,
                                  const HandleDataflowAnalysis &handles,
                                  unsigned maximumOperations) {
  if (!memory.isValid() || function.getBody().empty())
    return false;
  // Avoid path enumeration through even finite loops. The ordinary induction
  // and reaching-value analyses remain responsible for those activations.
  DenseMap<Block *, unsigned> incoming;
  SmallVector<Block *> ready;
  for (Block &block : function.getBody()) {
    unsigned count = std::distance(block.pred_begin(), block.pred_end());
    incoming[&block] = count;
    if (!count)
      ready.push_back(&block);
  }
  unsigned visited = 0;
  while (!ready.empty()) {
    Block *block = ready.pop_back_val();
    ++visited;
    for (Block *next : block->getSuccessors())
      if (!--incoming[next])
        ready.push_back(next);
  }
  if (visited != function.getBody().getBlocks().size())
    return false;

  SmallVector<Range> ranges;
  DenseMap<Operation *, unsigned> stores;
  auto references = handles.derive(function);
  bool valid = true;
  function.walk([&](Operation *op) {
    auto found = memory.getWrites().find(op);
    if (found == memory.getWrites().end())
      return;
    if (!isa<sim::SimRefStoreOp>(op) || found->second.size() != 1) {
      valid = false;
      return;
    }
    Range range = found->second.front();
    auto store = cast<sim::SimRefStoreOp>(op);
    auto reference = references.find(store.getReference());
    auto width = sim::getProvenanceSpan(store.getValue().getType());
    if (reference == references.end() || reference->second.dynamic ||
        !reference->second.descriptor || !width || *width != range.width ||
        reference->second.resource != schedule::ComputeResourceKind::Storage ||
        *reference->second.descriptor != range.root ||
        reference->second.low != range.low ||
        reference->second.width != range.width) {
      valid = false;
      return;
    }
    auto cell = llvm::find(ranges, range);
    if (cell == ranges.end()) {
      if (llvm::any_of(ranges,
                       [&](Range other) { return overlaps(range, other); })) {
        valid = false;
        return;
      }
      stores[op] = ranges.size();
      ranges.push_back(range);
    } else {
      stores[op] = cell - ranges.begin();
    }
  });
  if (!valid || ranges.empty())
    return false;
  unsigned count = ranges.size();
  Path entry;
  entry.block = &function.getBody().front();
  entry.controls.resize(count);
  for (unsigned i = 0; i < count; ++i) {
    Fact fact(count);
    fact.inputs.set(i);
    fact.identity = i;
    entry.cells.push_back(std::move(fact));
  }
  SmallVector<Path, 0> pending;
  pending.push_back(std::move(entry));
  unsigned operations = 0;
  while (!pending.empty()) {
    Path path = std::move(pending.back());
    pending.pop_back();
    auto valueOf = [&](Value value) {
      auto found = path.values.find(value);
      return found == path.values.end() ? Fact(count) : found->second;
    };
    for (Operation &op : path.block->without_terminator()) {
      if (++operations > maximumOperations || op.getNumRegions())
        return false;
      if (auto load = dyn_cast<sim::SimRefLoadOp>(op)) {
        auto found = memory.getReads().find(&op);
        if (found == memory.getReads().end() || found->second.empty() ||
            memory.getPrivateReads().contains(&op))
          return false;
        Fact fact(count);
        bool first = true;
        for (Range read : found->second) {
          Fact selected(count);
          for (auto [id, written] : llvm::enumerate(ranges)) {
            if (!overlaps(read, written))
              continue;
            if (!(read == written))
              return false;
            selected = path.cells[id];
          }
          fact.inputs |= selected.inputs;
          if (first)
            fact.identity = selected.identity;
          else if (fact.identity != selected.identity)
            fact.identity.reset();
          first = false;
        }
        auto selector = valueOf(load.getReference());
        fact.inputs |= selector.inputs;
        if (selector.inputs.any())
          fact.identity.reset();
        path.values.insert_or_assign(load.getResult(), std::move(fact));
      } else if (auto store = dyn_cast<sim::SimRefStoreOp>(op)) {
        auto found = stores.find(&op);
        if (found == stores.end())
          return false;
        path.cells[found->second] = valueOf(store.getValue());
      } else {
        if (!isMemoryEffectFree(&op))
          return false;
        Fact fact(count);
        for (Value operand : op.getOperands())
          fact.inputs |= valueOf(operand).inputs;
        // These wrappers preserve every logical lane, including X/Z planes.
        if (isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(op))
          fact = valueOf(op.getOperand(0));
        for (Value result : op.getResults())
          path.values.insert_or_assign(result, fact);
      }
    }
    Operation *term = path.block->getTerminator();
    if (++operations > maximumOperations)
      return false;
    if (isa<sim::SimReturnOp>(term)) {
      auto used = path.controls;
      for (Value value : term->getOperands())
        used |= valueOf(value).inputs;
      for (const auto &cell : path.cells)
        used |= cell.inputs;
      for (int id : used.set_bits())
        if (path.cells[id].identity != unsigned(id))
          return false;
      continue;
    }
    auto branch = dyn_cast<BranchOpInterface>(term);
    if (!branch || !isa<cf::BranchOp, cf::CondBranchOp, cf::SwitchOp>(term))
      return false;
    if (auto conditional = dyn_cast<cf::CondBranchOp>(term))
      path.controls |= valueOf(conditional.getCondition()).inputs;
    else if (auto cases = dyn_cast<cf::SwitchOp>(term))
      path.controls |= valueOf(cases.getFlag()).inputs;
    for (auto [index, successor] : llvm::enumerate(term->getSuccessors())) {
      auto operands = branch.getSuccessorOperands(index);
      if (operands.getProducedOperandCount())
        return false;
      Path next = path;
      next.block = successor;
      for (auto [argument, operand] : llvm::zip_equal(
               successor->getArguments(), operands.getForwardedOperands()))
        next.values.insert_or_assign(argument, valueOf(operand));
      pending.push_back(std::move(next));
    }
  }
  return true;
}
} // namespace obelisk::analysis
