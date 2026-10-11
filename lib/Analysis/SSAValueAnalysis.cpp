//===- SSAValueAnalysis.cpp - Shared forwarding facts -------------------===//
#include "obelisk/Analysis/SSAValueAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "obelisk/Analysis/GraphAlgorithms.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SparseBitVector.h"

using namespace mlir;
namespace obelisk::analysis {
namespace {
struct InputOrigins {
  llvm::SparseBitVector<> inputs;
  bool operator==(const InputOrigins &other) const {
    return inputs == other.inputs;
  }
  static InputOrigins join(const InputOrigins &a, const InputOrigins &b) {
    InputOrigins result = a;
    result.inputs |= b.inputs;
    return result;
  }
  void print(raw_ostream &os) const { os << inputs.count() << " SSA inputs"; }
};
using OriginState = dataflow::Lattice<InputOrigins>;

/// The region interface defines the sparse forwarding graph. A separate edge
/// for every result lane is essential: unioning all yields would couple
/// independent loop-carried accumulators and manufacture feedback.
class RegionInputDataflow final : public DataFlowAnalysis {
public:
  RegionInputDataflow(DataFlowSolver &solver,
                      const llvm::MapVector<Value, SmallVector<Value>> &edges,
                      ArrayRef<Value> inputs)
      : DataFlowAnalysis(solver), edges(edges), inputs(inputs) {}
  LogicalResult initialize(Operation *) override {
    for (auto [value, operands] : edges) {
      ProgramPoint *point =
          isa<BlockArgument>(value)
              ? getProgramPointBefore(cast<BlockArgument>(value).getOwner())
              : getProgramPointAfter(value.getDefiningOp());
      targets[point].push_back(value);
      for (Value operand : operands)
        addDependency(getOrCreate<OriginState>(operand), point);
    }
    for (auto [index, input] : llvm::enumerate(inputs)) {
      InputOrigins origin;
      origin.inputs.set(index);
      auto *state = getOrCreate<OriginState>(input);
      propagateIfChanged(state, state->join(origin));
    }
    // Initial empty facts also matter for a loop that carries no source input.
    for (const auto &[point, values] : targets)
      if (failed(visit(point)))
        return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    for (Value value : targets.lookup(point)) {
      InputOrigins origins;
      for (Value operand : edges.lookup(value))
        origins.inputs |=
            getOrCreateFor<OriginState>(point, operand)->getValue().inputs;
      auto *state = getOrCreate<OriginState>(value);
      propagateIfChanged(state, state->join(origins));
    }
    return success();
  }

private:
  const llvm::MapVector<Value, SmallVector<Value>> &edges;
  ArrayRef<Value> inputs;
  llvm::MapVector<ProgramPoint *, SmallVector<Value>> targets;
};

template <typename T> struct ForwardedFact {
  bool initialized = false;
  T value;
  bool operator==(const ForwardedFact &rhs) const {
    return initialized == rhs.initialized && value == rhs.value;
  }
  static ForwardedFact join(ForwardedFact lhs, ForwardedFact rhs) {
    if (!lhs.initialized)
      return rhs;
    if (!rhs.initialized || lhs == rhs)
      return lhs;
    return {true, {}};
  }
  void print(raw_ostream &os) const {
    os << (!initialized ? "bottom" : value ? "exact" : "unknown");
  }
};
using TimeFact = ForwardedFact<IntegerAttr>;
using TimeLattice = dataflow::Lattice<TimeFact>;
class TimeAnalysis
    : public dataflow::SparseForwardDataFlowAnalysis<TimeLattice> {
public:
  using SparseForwardDataFlowAnalysis::SparseForwardDataFlowAnalysis;
  LogicalResult visitOperation(Operation *op,
                               ArrayRef<const TimeLattice *> operands,
                               ArrayRef<TimeLattice *> results) override {
    TimeFact fact{true, {}};
    if (auto constant = dyn_cast<sim::SimTimeConstantOp>(op))
      fact.value = constant.getValueAttr();
    for (TimeLattice *result : results)
      propagateIfChanged(result, result->join(fact));
    return success();
  }

private:
  void setToEntryState(TimeLattice *state) override {
    propagateIfChanged(state, state->join({true, {}}));
  }
};
} // namespace

RegionValueDependencyAnalysis::RegionValueDependencyAnalysis(
    Operation *root, ArrayRef<Value> inputs) {
  llvm::MapVector<Value, SmallVector<Value>> edges;
  bool supported = true;
  root->walk([&](Operation *op) {
    for (Value result : op->getResults())
      edges.try_emplace(result);
    for (Region &region : op->getRegions()) {
      if (!region.empty() && !llvm::hasSingleElement(region))
        supported = false;
      for (Block &block : region)
        for (Value argument : block.getArguments())
          edges.try_emplace(argument);
    }
  });
  if (!supported)
    return;
  root->walk([&](Operation *op) {
    if (op == root && isa<FunctionOpInterface>(op))
      return;
    auto branch = dyn_cast<RegionBranchOpInterface>(op);
    if (!branch) {
      if (op->getNumRegions()) {
        supported = false;
        return;
      }
      for (Value result : op->getResults())
        llvm::append_range(edges[result], op->getOperands());
      return;
    }
    RegionBranchSuccessorMapping forwarding;
    branch.getSuccessorOperandInputMapping(forwarding);
    for (const auto &[operand, targets] : forwarding)
      for (Value target : targets)
        edges[target].push_back(operand->get());
    // Operands not forwarded as region values control the selected successor
    // or invocation count: if/switch selectors, for bounds, while conditions.
    llvm::SetVector<Value> predicates;
    auto collect = [&](Operation *point) {
      for (OpOperand &operand : point->getOpOperands())
        if (!forwarding.contains(&operand))
          predicates.insert(operand.get());
    };
    collect(op);
    for (RegionBranchPoint point : branch.getAllRegionBranchPoints())
      if (auto *term = point.getTerminatorPredecessorOrNull())
        collect(term);
    controls[op].assign(predicates.begin(), predicates.end());
    for (Value result : op->getResults())
      llvm::append_range(edges[result], predicates);
    // Include non-forwarded region arguments, such as an scf.for induction
    // value. Control dependence of effects is queried separately below.
    for (Region &region : op->getRegions())
      if (!region.empty())
        for (BlockArgument argument : region.front().getArguments())
          llvm::append_range(edges[argument], predicates);
  });
  if (!supported)
    return;
  // Supplied inputs may be captures defined outside the queried region tree.
  // Keep their seeded facts available to direct operand/effect queries too.
  for (Value input : inputs)
    edges.try_emplace(input);
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<RegionInputDataflow>(edges, inputs);
  if (failed(solver.initializeAndRun(root)))
    return;
  for (const auto &[value, operands] : edges) {
    auto &origins = dependencies[value];
    if (auto *state = solver.lookupState<OriginState>(value))
      for (unsigned index : state->getValue().inputs)
        origins.push_back(inputs[index]);
  }
  valid = true;
}

ArrayRef<Value> RegionValueDependencyAnalysis::getInputs(Value value) const {
  auto found = dependencies.find(value);
  return found == dependencies.end() ? ArrayRef<Value>() : found->second;
}

SmallVector<Value>
RegionValueDependencyAnalysis::getInputs(Operation *operation) const {
  llvm::SetVector<Value> result;
  for (Value operand : operation->getOperands())
    result.insert_range(getInputs(operand));
  for (Operation *parent = operation->getParentOp(); parent;
       parent = parent->getParentOp()) {
    auto found = controls.find(parent);
    if (found != controls.end())
      for (Value predicate : found->second)
        result.insert_range(getInputs(predicate));
  }
  return {result.begin(), result.end()};
}

SemanticValueRootAnalysis::SemanticValueRootAnalysis(sim::SimFuncOp function) {
  struct Constraint {
    BlockArgument argument;
    SmallVector<Value> incoming;
    bool opaque = false;
  };
  SmallVector<Constraint> constraints;
  DenseMap<Value, unsigned> indices;
  function.walk([&](Block *block) {
    for (BlockArgument argument : block->getArguments()) {
      if (block->isEntryBlock() || block->hasNoPredecessors()) {
        roots[argument] = argument;
      } else {
        indices[argument] = constraints.size();
        constraints.push_back({argument, {}, false});
      }
    }
  });
  // Index edges once. Do not collapse duplicate predecessor/successor pairs:
  // their forwarded values can differ. All structural edges contribute, even
  // infeasible ones, because these facts also guide physical frame operands.
  function.walk([&](Block *block) {
    Operation *terminator = block->getTerminator();
    auto branch = dyn_cast<BranchOpInterface>(terminator);
    for (unsigned edge = 0; edge != terminator->getNumSuccessors(); ++edge)
      for (BlockArgument argument :
           terminator->getSuccessor(edge)->getArguments()) {
        auto found = indices.find(argument);
        if (found == indices.end())
          continue;
        Constraint &constraint = constraints[found->second];
        if (!branch) {
          constraint.opaque = true;
          continue;
        }
        SuccessorOperands operands = branch.getSuccessorOperands(edge);
        if (argument.getArgNumber() >= operands.size() ||
            operands.isOperandProduced(argument.getArgNumber()))
          constraint.opaque = true;
        else
          constraint.incoming.push_back(operands[argument.getArgNumber()]);
      }
  });
  // Collapse forwarding cycles in dependency order. A mixed phi defines its
  // own semantic value; forwarding that phi through a suspension still has
  // an exact identity even though its allocation/definition origin varies.
  // Propagating a generic unknown fact to all users would lose that identity.
  SmallVector<Value> nodes;
  DenseMap<Value, SmallVector<Value>> dependencies;
  for (const Constraint &constraint : constraints) {
    nodes.push_back(constraint.argument);
    for (Value incoming : constraint.incoming)
      if (indices.contains(incoming))
        dependencies[constraint.argument].push_back(incoming);
  }
  auto components = computeStronglyConnectedComponents<Value>(
      nodes, dependencies, [&](Value lhs, Value rhs) {
        return indices.lookup(lhs) < indices.lookup(rhs);
      });
  for (ArrayRef<Value> component : components) {
    DenseSet<Value> members(component.begin(), component.end());
    Value common;
    bool opaque = false, mixed = false;
    for (Value value : component) {
      const Constraint &constraint = constraints[indices.lookup(value)];
      opaque |= constraint.opaque;
      for (Value incoming : constraint.incoming) {
        if (members.contains(incoming))
          continue;
        Value root = lookup(incoming);
        if (!common)
          common = root;
        else
          mixed |= common != root;
      }
    }
    if (common && !mixed && !opaque) {
      for (Value value : component)
        roots[value] = common;
      continue;
    }
    // In a mixed component only single-input forwarding lanes are aliases.
    // Stop at a genuine merge, an opaque producer, or an ungrounded cycle.
    for (Value value : component) {
      Value cursor = value;
      DenseSet<Value> visited;
      while (members.contains(cursor) && visited.insert(cursor).second) {
        const Constraint &constraint = constraints[indices.lookup(cursor)];
        if (constraint.opaque || constraint.incoming.empty())
          break;
        Value next = constraint.incoming.front();
        if (llvm::any_of(constraint.incoming,
                         [&](Value incoming) { return incoming != next; }))
          break;
        cursor = next;
      }
      if (cursor != value && !visited.contains(cursor))
        roots[value] = lookup(cursor);
      else if (cursor != value && members.contains(cursor)) {
        const Constraint &constraint = constraints[indices.lookup(cursor)];
        bool merge = constraint.opaque || constraint.incoming.empty() ||
                     llvm::any_of(constraint.incoming, [&](Value incoming) {
                       return incoming != constraint.incoming.front();
                     });
        if (merge)
          roots[value] = cursor;
      }
    }
  }
}

bool propagateActivationValueRoots(sim::SimFuncOp function) {
  if (function
          .walk([](Operation *op) {
            return sim::isSuspensionOp(op) ? WalkResult::interrupt()
                                           : WalkResult::advance();
          })
          .wasInterrupted())
    return false;
  SemanticValueRootAnalysis forwarding(function);
  DominanceInfo dominance(function);
  bool changed = false;
  function.walk([&](Block *block) {
    for (BlockArgument argument : block->getArguments()) {
      Value root = forwarding.lookup(argument);
      if (root == argument)
        continue;
      argument.replaceUsesWithIf(root, [&](OpOperand &use) {
        if (!dominance.dominates(root, use.getOwner()))
          return false;
        changed = true;
        return true;
      });
    }
  });
  return changed;
}

ConstantTimeAnalysis::ConstantTimeAnalysis(sim::SimFuncOp function) {
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<TimeAnalysis>();
  if (failed(solver.initializeAndRun(function)))
    return;
  auto record = [&](Value value) {
    const auto *state = solver.lookupState<TimeLattice>(value);
    if (state && state->getValue().value)
      constants.insert(value);
  };
  function.walk([&](Operation *op) {
    for (Value value : op->getResults())
      record(value);
    for (Region &region : op->getRegions())
      for (Block &block : region)
        for (Value value : block.getArguments())
          record(value);
  });
}
const DenseSet<Block *> &ValueLiveness::getLiveInBlocks(Value value) {
  auto [found, inserted] = liveIns.try_emplace(value);
  DenseSet<Block *> &live = found->second;
  if (inserted) {
    // A block defines the value when it contains its definition, possibly
    // nested. Every other block containing a use has the value live in, and
    // so does each block from which such a block is reached without passing
    // a defining one. This is the least solution of mlir::Liveness's
    // equations, restricted to one value.
    SmallPtrSet<Block *, 8> defining;
    auto parentBlock = [](Block *child) -> Block * {
      Operation *parent = child->getParentOp();
      return parent ? parent->getBlock() : nullptr;
    };
    for (Block *ancestor = value.getParentBlock(); ancestor;
         ancestor = parentBlock(ancestor))
      defining.insert(ancestor);
    SmallVector<Block *> worklist;
    for (Operation *user : value.getUsers())
      for (Block *ancestor = user->getBlock();
           ancestor && !defining.contains(ancestor);
           ancestor = parentBlock(ancestor))
        worklist.push_back(ancestor);
    while (!worklist.empty()) {
      Block *current = worklist.pop_back_val();
      if (defining.contains(current) || !live.insert(current).second)
        continue;
      llvm::append_range(worklist, current->getPredecessors());
    }
  }
  return live;
}
bool ValueLiveness::isLiveOut(Value value, Block *block) {
  if (llvm::any_of(block->getSuccessors(), [&](Block *successor) {
        return isLiveIn(value, successor);
      }))
    return true;
  return value.getParentBlock() == block &&
         llvm::any_of(value.getUsers(), [&](Operation *user) {
           return block->getParent()->findAncestorBlockInRegion(
                      *user->getBlock()) != block;
         });
}
} // namespace obelisk::analysis
