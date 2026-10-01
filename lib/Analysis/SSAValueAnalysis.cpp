//===- SSAValueAnalysis.cpp - Shared forwarding facts -------------------===//
#include "obelisk/Analysis/SSAValueAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "obelisk/Analysis/GraphAlgorithms.h"

using namespace mlir;
namespace obelisk::analysis {
namespace {
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
} // namespace obelisk::analysis
