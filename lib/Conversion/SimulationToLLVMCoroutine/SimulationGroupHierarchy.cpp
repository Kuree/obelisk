//===- SimulationGroupHierarchy.cpp - Refine collapsed execution groups ---===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseSet.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

constexpr StringLiteral entryAttr = "obelisk.eval.activation_entry";

bool reproducibleSetup(Operation *operation) {
  // Pure/speculatable alone does not authorize duplication: e.g. freeze of
  // poison captures one value which must remain correlated across children.
  // Generated entries need only constants and static address construction.
  return isa<LLVM::ConstantOp, LLVM::AddressOfOp, LLVM::GEPOp>(operation);
}

/// An entry is an executable readiness load, not an inferred graph rank.
/// Missing, duplicate, or reordered identities cannot certify a split.
SmallVector<Operation *> entries(LLVM::LLVMFuncOp function,
                                 DenseI32ArrayAttr members) {
  DenseMap<int32_t, Operation *> indexed;
  bool valid = true;
  function.walk([&](Operation *operation) {
    if (auto owner = operation->getAttrOfType<IntegerAttr>(entryAttr))
      valid &= !owner.getValue().isNegative() &&
               owner.getValue().getActiveBits() <= 31 &&
               isa<LLVM::LoadOp>(operation) &&
               indexed.try_emplace(owner.getInt(), operation).second;
  });
  SmallVector<Operation *> result;
  if (!valid || indexed.size() != members.size())
    return result;
  for (int32_t owner : members.asArrayRef()) {
    auto found = indexed.find(owner);
    if (found == indexed.end())
      return {};
    result.push_back(found->second);
  }
  return result;
}

/// Clone a contiguous sequence of whole source activations. Only pure,
/// speculatable setup may cross its boundary as a rematerialized value.
/// All state and pending work still cross through their authoritative storage.
/// In particular, no state load, capture, side effect, or suspension is
/// replayed.
LLVM::LLVMFuncOp outline(LLVM::LLVMFuncOp source,
                         ArrayRef<Operation *> boundaries, unsigned begin,
                         unsigned end, StringRef name, SymbolTable &symbols) {
  Operation *start = boundaries[begin];
  Operation *stop = end == boundaries.size() ? nullptr : boundaries[end];
  SmallVector<Block *> blocks{start->getBlock()};
  DenseSet<Block *> seen{start->getBlock()};
  DenseSet<Operation *> included;
  DenseSet<Operation *> allowed(boundaries.begin() + begin,
                                boundaries.begin() + end);
  bool reachedStop = false;
  for (size_t i = 0; i < blocks.size(); ++i) {
    Block *block = blocks[i];
    auto first =
        block == start->getBlock() ? start->getIterator() : block->begin();
    bool stopped = false;
    for (Operation &operation : llvm::make_range(first, block->end())) {
      if (&operation == stop) {
        reachedStop = stopped = true;
        break;
      }
      // A bypass of the next entry would execute a different activation
      // sequence after outlining. Reject it, even if the graph is acyclic.
      if (operation.hasAttr(entryAttr) && !allowed.contains(&operation))
        return {};
      included.insert(&operation);
      if (isa<LLVM::ReturnOp>(operation) && stop)
        return {};
    }
    if (stopped)
      continue;
    Operation *terminator = block->getTerminator();
    if (!isa<LLVM::BrOp, LLVM::CondBrOp, LLVM::ReturnOp>(terminator))
      return {};
    for (Block *next : block->getSuccessors()) {
      // Reentering the start block could replay setup before the entry.
      if (next == start->getBlock())
        return {};
      if (seen.insert(next).second)
        blocks.push_back(next);
    }
  }
  if (stop && !reachedStop)
    return {};
  for (Operation *boundary : boundaries.slice(begin, end - begin))
    if (!included.contains(boundary))
      return {};

  OpBuilder builder(source.getContext());
  builder.setInsertionPointAfter(source);
  auto child = LLVM::LLVMFuncOp::create(builder, source.getLoc(), name,
                                        source.getFunctionType());
  // SymbolTable owns unique names, including in user-authored pass fixtures.
  symbols.insert(child);
  auto members =
      source->getAttrOfType<DenseI32ArrayAttr>("obelisk.eval.ranked_members");
  child->setAttr("obelisk.eval.ranked_members",
                 builder.getDenseI32ArrayAttr(
                     members.asArrayRef().slice(begin, end - begin)));
  for (StringRef attr :
       {"obelisk.eval.group_ingress", "obelisk.eval.ready_word_count"})
    if (Attribute value = source->getAttr(attr))
      child->setAttr(attr, value);
  child->setAttr(sim::metadata::evalCallClosureRoot, builder.getUnitAttr());
  child->setAttr("obelisk.eval.group_parent", FlatSymbolRefAttr::get(source));
  Block *entry = child.addEntryBlock(builder);
  builder.setInsertionPointToStart(entry);
  IRMapping mapping;
  mapping.map(source.getBody().front().getArguments(), entry->getArguments());
  for (Block *block : blocks) {
    auto *copy = new Block;
    child.getBody().push_back(copy);
    mapping.map(block, copy);
    if (block != start->getBlock())
      for (BlockArgument arg : block->getArguments())
        mapping.map(arg, copy->addArgument(arg.getType(), arg.getLoc()));
    else if (block != &source.getBody().front() && block->getNumArguments()) {
      symbols.erase(child);
      return {};
    }
  }

  // Iterative postorder bounds stack use for large generated expressions.
  DenseSet<Value> visiting;
  auto rematerialize = [&](Value value) {
    SmallVector<std::pair<Value, bool>> pending{{value, false}};
    while (!pending.empty()) {
      auto [current, expanded] = pending.pop_back_val();
      if (mapping.contains(current))
        continue;
      Operation *definition = current.getDefiningOp();
      if (!definition || included.contains(definition) ||
          !reproducibleSetup(definition) || definition->getNumRegions() ||
          !isMemoryEffectFree(definition) || !isSpeculatable(definition))
        return false;
      if (expanded) {
        builder.clone(*definition, mapping);
        visiting.erase(current);
        continue;
      }
      if (!visiting.insert(current).second)
        return false;
      pending.emplace_back(current, true);
      for (Value operand : llvm::reverse(definition->getOperands()))
        pending.emplace_back(operand, false);
    }
    return true;
  };
  // Map all external operands before cloning CFG blocks. Internal forward
  // references are fixed by cloneInto-style remapping below.
  for (Block *block : blocks)
    for (Operation &operation : *block)
      if (included.contains(&operation))
        for (Value operand : operation.getOperands()) {
          Operation *definition = operand.getDefiningOp();
          if ((definition && included.contains(definition)) ||
              mapping.contains(operand))
            continue;
          if (!rematerialize(operand)) {
            symbols.erase(child);
            return {};
          }
        }
  LLVM::BrOp::create(builder, source.getLoc(), ValueRange{},
                     mapping.lookup(start->getBlock()));
  SmallVector<Operation *> copies;
  for (Block *block : blocks) {
    builder.setInsertionPointToEnd(mapping.lookup(block));
    for (Operation &operation : *block) {
      if (&operation == stop) {
        LLVM::ReturnOp::create(builder, operation.getLoc(), ValueRange{});
        break;
      }
      if (included.contains(&operation))
        copies.push_back(builder.clone(operation, mapping));
    }
  }
  for (Operation *copy : copies)
    for (OpOperand &operand : copy->getOpOperands())
      operand.set(mapping.lookupOrDefault(operand.get()));
  return child;
}

bool purePrefix(LLVM::LLVMFuncOp function, Operation *entry) {
  // Generated group setup lives in the entry block. Do not infer that an
  // arbitrary predecessor path is harmless from source-owner metadata alone.
  Block *block = &function.getBody().front();
  DenseSet<Block *> seen;
  while (seen.insert(block).second) {
    for (Operation &operation : *block) {
      if (&operation == entry)
        return true;
      if (auto branch = dyn_cast<LLVM::BrOp>(operation)) {
        if (!branch.getDestOperands().empty())
          return false;
        block = branch.getDest();
        break;
      }
      if (!reproducibleSetup(&operation) || operation.getNumRegions() ||
          !isMemoryEffectFree(&operation) || !isSpeculatable(&operation))
        return false;
    }
  }
  return false;
}

} // namespace

SmallVector<LLVM::LLVMFuncOp> splitNativeEvalGroup(LLVM::LLVMFuncOp original,
                                                   LLVM::LLVMFuncOp candidate,
                                                   SymbolTable &symbols,
                                                   uint64_t &budget) {
  auto members =
      original->getAttrOfType<DenseI32ArrayAttr>("obelisk.eval.ranked_members");
  if (!members || members.size() < 2 ||
      !isa<LLVM::LLVMVoidType>(original.getFunctionType().getReturnType()) ||
      original->hasAttr("obelisk.eval.group_children"))
    return {};
  bool executionBoundary = false;
  candidate.walk([&](Operation *operation) {
    executionBoundary |=
        isa<LLVM::CallOp, LLVM::InvokeOp, LLVM::InlineAsmOp, LLVM::UDivOp,
            LLVM::SDivOp, LLVM::URemOp, LLVM::SRemOp>(operation);
    if (auto load = dyn_cast<LLVM::LoadOp>(operation))
      executionBoundary |=
          load.getVolatile_() ||
          load.getOrdering() != LLVM::AtomicOrdering::not_atomic;
    if (auto store = dyn_cast<LLVM::StoreOp>(operation))
      executionBoundary |=
          store.getVolatile_() ||
          store.getOrdering() != LLVM::AtomicOrdering::not_atomic;
  });
  // A memory representation or analysis-budget failure is not a new execution
  // boundary. Splitting solely to bypass that failure trades existing local
  // SSA for extra state publications and helper calls. Keep the original
  // group until its memory proof can be improved directly.
  if (!executionBoundary)
    return {};
  uint64_t cost = 0;
  for (auto function : {original, candidate})
    function.walk([&](Operation *) { ++cost; });
  if (cost > budget)
    return {};
  auto slowEntries = entries(original, members);
  auto fastEntries = entries(candidate, members);
  if (slowEntries.empty() || fastEntries.empty() ||
      !purePrefix(original, slowEntries.front()) ||
      !purePrefix(candidate, fastEntries.front()))
    return {};
  unsigned middle = members.size() / 2;
  SmallVector<LLVM::LLVMFuncOp> children;
  for (auto [begin, end] :
       {std::pair{0u, middle}, std::pair{middle, unsigned(members.size())}}) {
    std::string name =
        original.getSymName().str() + ".child" + std::to_string(begin);
    auto slow = outline(original, slowEntries, begin, end, name, symbols);
    if (slow)
      children.push_back(slow);
    auto fast = slow ? outline(candidate, fastEntries, begin, end,
                               name + ".dataflow", symbols)
                     : LLVM::LLVMFuncOp{};
    if (!fast) {
      for (auto child : children)
        symbols.erase(child);
      return {};
    }
    children.push_back(fast);
    fast->setAttr("obelisk.eval.dataflow_candidate",
                  FlatSymbolRefAttr::get(slow));
    fast->setAttr("obelisk.eval.group_parent",
                  FlatSymbolRefAttr::get(original));
  }

  cost = 0;
  for (auto child : children)
    child.walk([&](Operation *) { ++cost; });
  if (cost > budget) {
    for (auto child : children)
      symbols.erase(child);
    return {};
  }
  budget -= cost;
  // The caller commits bottom-up only if a descendant actually collapses.
  // Otherwise it discards the trial children and keeps this exact executor.
  return children;
}

} // namespace obelisk::detail
