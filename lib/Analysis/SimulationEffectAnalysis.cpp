#include "obelisk/Analysis/SimulationEffectAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Analysis/ParallelWalk.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk::analysis {
bool isPlainValueType(Type type) {
  if (isa<IntegerType, sim::LogicType>(type))
    return true;
  if (auto packed = dyn_cast<sim::PackedArrayType>(type))
    return isPlainValueType(packed.getElementType());
  if (auto unpacked = dyn_cast<sim::UnpackedArrayType>(type))
    return isPlainValueType(unpacked.getElementType());
  if (isa<sim::PackedStructType, sim::PackedUnionType, sim::UnpackedStructType>(
          type)) {
    for (unsigned index = 0, count = sim::getAggregateNumElements(type);
         index != count; ++index)
      if (!isPlainValueType(sim::getAggregateElementType(type, index)))
        return false;
    return true;
  }
  return false;
}
Operation *getLocalSlot(Value reference) {
  while (Operation *definition = reference.getDefiningOp()) {
    if (auto alloc = dyn_cast<sim::SimRefAllocOp>(definition))
      return isPlainValueType(alloc.getType().getElementType()) ? alloc
                                                                : nullptr;
    if (!isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
             sim::SimRefArrayElementOp, sim::SimRefSubelementOp>(definition))
      return nullptr;
    reference = definition->getOperand(0);
  }
  return nullptr;
}
bool isLocalSlotReference(Value reference) {
  return getLocalSlot(reference) != nullptr;
}
namespace {
constexpr unsigned allEffects =
    SimulationEffectAnalysis::Write | SimulationEffectAnalysis::Terminate |
    SimulationEffectAnalysis::External | SimulationEffectAnalysis::Suspend |
    SimulationEffectAnalysis::Unknown;
class ClosureState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  unsigned effects = 0;
  ChangeResult join(unsigned incoming) {
    unsigned next = effects | incoming;
    if (next == effects)
      return ChangeResult::NoChange;
    effects = next;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override { os << effects; }
};
struct LocalEffects {
  unsigned effects = 0;
  SmallVector<Operation *> callees;
};
class ClosureDataflow final : public DataFlowAnalysis {
public:
  using DataFlowAnalysis::DataFlowAnalysis;
  LogicalResult initialize(Operation *root) override {
    SymbolTableCollection symbols;
    // Index each design's drivers once rather than once per function.
    DenseMap<Operation *, std::unique_ptr<HandleDataflowAnalysis>> designs;
    auto summarize = [&](sim::SimFuncOp function) {
      auto &summary = local[function];
      if (function.isExternal())
        summary.effects = allEffects;
      auto design = function->getParentOfType<sim::SimDesignOp>();
      auto &index = designs[design];
      if (!index)
        index = std::make_unique<HandleDataflowAnalysis>(design);
      auto handleResult = index->analyze(function);
      const auto &handles = handleResult.facts;
      function.walk([&](Operation *op) {
        if (op == function || !handleResult.isExecutable(op->getBlock()) ||
            isa<sim::SimReturnOp, cf::BranchOp, cf::CondBranchOp,
                sim::SimTerminationRequestedOp>(op))
          return;
        if (auto call = dyn_cast<sim::SimCallOp>(op)) {
          auto callee = symbols.lookupNearestSymbolFrom<sim::SimFuncOp>(
              call, call.getCalleeAttr());
          if (callee && (callee == root || root->isProperAncestor(callee)))
            summary.callees.push_back(callee);
          else
            summary.effects |= allEffects;
          return;
        }
        if (auto store = dyn_cast<sim::SimRefStoreOp>(op)) {
          auto allocation =
              store.getReference().getDefiningOp<sim::SimRefAllocOp>();
          bool privateSlot =
              (allocation &&
               llvm::all_of(allocation->getUsers(),
                            [](Operation *user) {
                              return isa<sim::SimRefLoadOp, sim::SimRefStoreOp>(
                                  user);
                            })) ||
              isLocalSlotReference(store.getReference());
          if (!privateSlot)
            summary.effects |= Write | Terminate;
          return;
        }
        if (auto load = dyn_cast<sim::SimRefLoadOp>(op);
            load && isLocalSlotReference(load.getReference()))
          return;
        if (auto alloc = dyn_cast<sim::SimRefAllocOp>(op);
            alloc && isPlainValueType(alloc.getType().getElementType()))
          return;
        if (isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op)) {
          auto found = handles.find(op->getOperand(0));
          if (found != handles.end() && found->second.descriptor &&
              (found->second.resource ==
                   schedule::ComputeResourceKind::Storage ||
               found->second.resource == schedule::ComputeResourceKind::Net))
            return;
        }
        if (isa<sim::SimRefLoadOp, sim::SimNetReadOp, sim::SimRefAllocOp,
                sim::SimStatusCheckOp>(op)) {
          summary.effects |= Terminate;
          return;
        }
        if (sim::isSuspensionOp(op)) {
          // Task calls, forks and joins have additional execution effects.
          if (isa<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
                  sim::SimSuspendEventOp, sim::SimSuspendEdgeOp,
                  sim::SimSuspendForeverOp>(op))
            summary.effects |= Suspend;
          else
            summary.effects |= allEffects;
          return;
        }
        if (!isMemoryEffectFree(op))
          summary.effects |= allEffects;
      });
    };
    for (sim::SimFuncOp function : collectInWalkOrder<sim::SimFuncOp>(root))
      summarize(function);
    for (auto &entry : local)
      if (failed(visit(getProgramPointBefore(entry.first))))
        return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    Operation *function = point->getNextOp();
    auto found = local.find(function);
    if (found == local.end())
      return success();
    unsigned effects = found->second.effects;
    for (Operation *callee : found->second.callees)
      effects |=
          getOrCreateFor<ClosureState>(point, getProgramPointBefore(callee))
              ->effects;
    auto *state = getOrCreate<ClosureState>(point);
    propagateIfChanged(state, state->join(effects));
    return success();
  }

private:
  DenseMap<Operation *, LocalEffects> local;
  static constexpr unsigned Write = SimulationEffectAnalysis::Write;
  static constexpr unsigned Terminate = SimulationEffectAnalysis::Terminate;
  static constexpr unsigned Suspend = SimulationEffectAnalysis::Suspend;
};
} // namespace
SimulationEffectAnalysis::SimulationEffectAnalysis(Operation *root) {
  DataFlowSolver solver;
  solver.load<ClosureDataflow>();
  if (failed(solver.initializeAndRun(root)))
    return;
  root->walk([&](sim::SimFuncOp function) {
    if (auto *state = solver.lookupState<ClosureState>(
            solver.getProgramPointBefore(function)))
      effects.try_emplace(function, state->effects);
  });
}
unsigned SimulationEffectAnalysis::get(sim::SimFuncOp function) const {
  auto found = effects.find(function);
  return found == effects.end() ? allEffects : found->second;
}
bool SimulationEffectAnalysis::isReadOnly(sim::SimFuncOp function) const {
  return !(get(function) & (Write | External | Unknown));
}
bool SimulationEffectAnalysis::isHarmless(sim::SimFuncOp function) const {
  return get(function) == 0;
}
} // namespace obelisk::analysis
