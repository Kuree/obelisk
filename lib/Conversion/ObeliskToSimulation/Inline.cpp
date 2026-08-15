//===- Inline.cpp - Obelisk-owned simulation inlining policy ------------===//

#include "Detail.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"

#include "mlir/Analysis/CallGraph.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Inliner.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMINLINEPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

struct InlinePreset {
  uint64_t tinyCost;
  uint64_t specializationCost;
  uint64_t callerGrowthPercent;
  uint64_t callerGrowthConstant;
  uint64_t designGrowthPercent;
  uint64_t designGrowthConstant;
  unsigned iterations;
};

InlinePreset getPreset(unsigned level) {
  switch (level) {
  case 0:
    return {0, 0, 0, 0, 0, 0, 0};
  case 1:
    return {8, 0, 10, 8, 2, 32, 1};
  case 2:
    return {12, 48, 25, 24, 10, 128, 2};
  default:
    return {24, 96, 50, 64, 20, 512, 4};
  }
}

uint64_t addSaturating(uint64_t lhs, uint64_t rhs) {
  return rhs > std::numeric_limits<uint64_t>::max() - lhs
             ? std::numeric_limits<uint64_t>::max()
             : lhs + rhs;
}

uint64_t growthLimit(uint64_t baseline, uint64_t percent, uint64_t constant) {
  uint64_t proportional =
      percent && baseline > std::numeric_limits<uint64_t>::max() / percent
          ? std::numeric_limits<uint64_t>::max()
          : baseline * percent / 100;
  return addSaturating(baseline, addSaturating(proportional, constant));
}

uint64_t replaceCost(uint64_t current, uint64_t removed, uint64_t added) {
  if (removed > current)
    return std::numeric_limits<uint64_t>::max();
  return addSaturating(current - removed, added);
}

template <typename Callback>
void forEachDirectCall(sim::SimFuncOp function, Callback &&callback) {
  function.getBody().walk([&](Operation *operation) {
    if (isa<sim::SimFuncOp>(operation))
      return WalkResult::skip();
    if (auto call = dyn_cast<sim::SimCallOp>(operation))
      callback(call);
    return WalkResult::advance();
  });
}

llvm::DenseSet<Operation *>
computeRecursiveFunctions(const llvm::StringMap<sim::SimFuncOp> &functions) {
  DenseMap<Operation *, SmallVector<Operation *>> callees;
  for (const auto &entry : functions) {
    sim::SimFuncOp function = entry.getValue();
    if (function.isExternal())
      continue;
    forEachDirectCall(function, [&](sim::SimCallOp call) {
      auto callee = functions.find(call.getCallee());
      if (callee != functions.end())
        callees[function].push_back(callee->second);
    });
  }

  DenseMap<Operation *, unsigned> indices;
  DenseMap<Operation *, unsigned> lowlinks;
  llvm::SmallPtrSet<Operation *, 32> onStack;
  SmallVector<Operation *> stack;
  llvm::DenseSet<Operation *> recursive;
  unsigned nextIndex = 0;
  std::function<void(Operation *)> visit = [&](Operation *function) {
    unsigned index = nextIndex++;
    indices[function] = index;
    lowlinks[function] = index;
    stack.push_back(function);
    onStack.insert(function);

    for (Operation *callee : callees[function]) {
      auto found = indices.find(callee);
      if (found == indices.end()) {
        visit(callee);
        lowlinks[function] = std::min(lowlinks[function], lowlinks[callee]);
      } else if (onStack.contains(callee)) {
        lowlinks[function] = std::min(lowlinks[function], found->second);
      }
    }
    if (lowlinks[function] != index)
      return;

    SmallVector<Operation *> component;
    Operation *member;
    do {
      member = stack.pop_back_val();
      onStack.erase(member);
      component.push_back(member);
    } while (member != function);
    bool isRecursive = component.size() > 1;
    if (!isRecursive)
      for (Operation *callee : callees[function])
        isRecursive |= callee == function;
    if (isRecursive)
      recursive.insert(component.begin(), component.end());
  };

  for (const auto &entry : functions) {
    sim::SimFuncOp functionOp = entry.getValue();
    Operation *function = functionOp;
    if (!functionOp.isExternal() && !indices.contains(function))
      visit(function);
  }
  return recursive;
}

class ObeliskSimInlinePass final
    : public impl::ObeliskSimInlinePassBase<ObeliskSimInlinePass> {
public:
  using Base = impl::ObeliskSimInlinePassBase<ObeliskSimInlinePass>;
  using Base::Base;
  ObeliskSimInlinePass(const ObeliskSimInlinePass &other) : Base(other) {}

  void runOnOperation() override;

  static LogicalResult runPipelineHelper(Pass &pass, OpPassManager &pipeline,
                                         Operation *operation) {
    return cast<ObeliskSimInlinePass>(pass).runPipeline(pipeline, operation);
  }

private:
  Statistic considered{this, "considered", "direct calls considered"};
  Statistic inlined{this, "inlined", "calls selected and inlined"};
  Statistic emptyTasksEliminated{
      this, "empty-tasks-eliminated",
      "effect-free task transfers replaced by direct continuations"};
  Statistic legalityRejected{this, "legality-rejected",
                             "calls rejected by simulation legality"};
  Statistic unprofitable{this, "unprofitable",
                         "legal calls rejected by profitability"};
  Statistic budgetRejected{this, "budget-rejected",
                           "profitable calls rejected by growth budgets"};
};

void ObeliskSimInlinePass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  if (optLevel > 3) {
    design.emitOpError("inliner optimization level must be between 0 and 3");
    return signalPassFailure();
  }
  // A retained graph proves that body fusion made no executable change. Late
  // site metadata makes every call intentionally ineligible for inlining, so
  // avoid rebuilding the call graph and rescanning every function only to
  // reject the complete inventory.
  if (design.getComputeGraphAttr())
    return;
  // A task call is a scheduler boundary, so MLIR's ordinary call inliner does
  // not see it.  An empty task has no activation state or effects to preserve:
  // transfer directly to the caller continuation before compute-graph
  // construction.  This also keeps harmless assertion stubs from forcing an
  // otherwise native actor through bytecode scheduling.
  if (optLevel != 0) {
    SymbolTable symbols(design);
    SmallVector<sim::SimTaskCallOp> emptyTaskCalls;
    design.walk([&](sim::SimTaskCallOp call) {
      sim::SimFuncOp callee = symbols.lookup<sim::SimFuncOp>(call.getCallee());
      if (!callee || callee.isExternal() ||
          callee.getEntryKind() != sim::EntryKind::Task ||
          callee.getBody().getBlocks().size() != 1)
        return;
      Block &entry = callee.getBody().front();
      if (entry.getOperations().size() == 1 &&
          isa<sim::SimReturnOp>(entry.getTerminator()))
        emptyTaskCalls.push_back(call);
    });
    for (sim::SimTaskCallOp call : emptyTaskCalls) {
      OpBuilder builder(call);
      cf::BranchOp::create(builder, call.getLoc(), call.getContinuation(),
                           call.getContinuationOperands());
      call.erase();
      ++emptyTasksEliminated;
    }
  }

  InlinePreset preset = getPreset(optLevel);
  auto overrideValue = [](int64_t option, uint64_t &value) {
    if (option >= 0)
      value = static_cast<uint64_t>(option);
  };
  overrideValue(tinyCost, preset.tinyCost);
  overrideValue(specializationCost, preset.specializationCost);
  overrideValue(callerGrowthPercent, preset.callerGrowthPercent);
  overrideValue(callerGrowthConstant, preset.callerGrowthConstant);
  overrideValue(designGrowthPercent, preset.designGrowthPercent);
  overrideValue(designGrowthConstant, preset.designGrowthConstant);
  if (maxIterations >= 0) {
    if (static_cast<uint64_t>(maxIterations) >
        std::numeric_limits<unsigned>::max()) {
      design.emitOpError("inliner max-iterations exceeds unsigned range");
      return signalPassFailure();
    }
    preset.iterations = static_cast<unsigned>(maxIterations);
  }

  llvm::StringMap<sim::SimFuncOp> functions;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>())
    functions[function.getSymName()] = function;

  // A process control can dynamically suspend or kill the caller. It must be
  // moved into the process CFG before backend lowering, independent of the
  // requested optimization level. Compute the transitive zero-time call
  // closure so every boundary on a path to process.control is mandatory.
  llvm::SmallPtrSet<Operation *, 32> controlFunctions;
  DenseMap<Operation *, SmallVector<Operation *>> directCallers;
  SmallVector<Operation *> controlWorklist;
  for (auto &entry : functions) {
    sim::SimFuncOp function = entry.getValue();
    if (function.isExternal())
      continue;
    bool containsControl = false;
    function.getBody().walk([&](Operation *operation) {
      if (isa<sim::SimFuncOp>(operation))
        return WalkResult::skip();
      if (auto control = dyn_cast<sim::SimProcessControlOp>(operation))
        containsControl |=
            control.getKind() == sim::ProcessControlKind::Suspend;
      FlatSymbolRefAttr calleeAttr;
      if (auto call = dyn_cast<sim::SimCallOp>(operation))
        calleeAttr = call.getCalleeAttr();
      else if (auto call = dyn_cast<sim::SimClassDirectCallOp>(operation))
        calleeAttr = call.getCalleeAttr();
      if (calleeAttr) {
        auto callee = functions.find(calleeAttr.getValue());
        if (callee != functions.end())
          directCallers[callee->second.getOperation()].push_back(
              function.getOperation());
      }
      return WalkResult::advance();
    });
    if (containsControl && controlFunctions.insert(function).second)
      controlWorklist.push_back(function);
  }
  while (!controlWorklist.empty()) {
    Operation *callee = controlWorklist.pop_back_val();
    for (Operation *caller : directCallers[callee])
      if (controlFunctions.insert(caller).second)
        controlWorklist.push_back(caller);
  }
  llvm::StringSet<> controlFunctionNames;
  for (auto &entry : functions)
    if (controlFunctions.contains(entry.getValue()))
      controlFunctionNames.insert(entry.getKey());
  if (optLevel == 0 && controlFunctions.empty())
    return;

  if (optLevel != 0) {
    if (failed(sim::normalizeClassDirectCalls(design)))
      return signalPassFailure();
  } else {
    SmallVector<sim::SimClassDirectCallOp> mandatoryDirectCalls;
    design.walk([&](sim::SimClassDirectCallOp call) {
      auto callee = functions.find(call.getCallee());
      if (callee != functions.end() &&
          controlFunctions.contains(callee->second.getOperation()))
        mandatoryDirectCalls.push_back(call);
    });
    for (sim::SimClassDirectCallOp call : mandatoryDirectCalls)
      if (failed(sim::normalizeClassDirectCall(call)))
        return signalPassFailure();
  }

  DenseMap<Operation *, uint64_t> callerBaselines;
  for (auto &entry : functions) {
    sim::SimFuncOp function = entry.getValue();
    if (!function.isExternal())
      callerBaselines[function.getOperation()] =
          analysis::getSimulationRegionCost(function.getBody());
  }

  uint64_t designBaseline = 0;
  for (auto [function, cost] : callerBaselines)
    designBaseline = addSaturating(designBaseline, cost);
  DenseMap<Operation *, uint64_t> callerCurrentCosts = callerBaselines;
  uint64_t currentDesignCost = designBaseline;
  DenseMap<Operation *, analysis::DescriptorProvenanceMap> provenanceCache;
  DenseMap<Operation *, uint64_t> regionCostCache;
  DenseMap<Operation *, bool> leafCache;
  llvm::DenseSet<uint64_t> selectedInlineIDs;
  static constexpr StringLiteral selectedInlineAttr =
      "__obelisk_inline_selection";

  CallGraph &callGraph = getAnalysis<CallGraph>();
  llvm::DenseSet<Operation *> recursiveFunctions =
      computeRecursiveFunctions(functions);
  bool designHasLateMetadata = sim::hasLateInlineMetadata(design);
  DenseMap<Operation *, sim::InlineLegality> calleeLegalities;
  for (auto &entry : functions) {
    sim::SimFuncOp function = entry.getValue();
    if (function.isExternal())
      continue;
    sim::InlineLegality legality = sim::getInlineCalleeLegality(
        function, recursiveFunctions.contains(function), designHasLateMetadata);
    calleeLegalities[function] = legality;
    function->setDiscardableAttr(
        sim::inlineLegalityCacheAttrName,
        IntegerAttr::get(IntegerType::get(function.getContext(), 32),
                         static_cast<uint32_t>(legality)));
  }
  struct SymbolUseSummary {
    uint64_t count = 0;
    Operation *onlyUser = nullptr;
    bool invalidated = false;
  };
  DenseMap<Operation *, SymbolUseSummary> symbolUses;
  design.walk([&](Operation *user) {
    for (NamedAttribute named : user->getAttrs())
      named.getValue().walk([&](SymbolRefAttr reference) {
        if (!reference.getNestedReferences().empty())
          return;
        auto function = functions.find(reference.getRootReference());
        if (function == functions.end())
          return;
        SymbolUseSummary &summary = symbolUses[function->second];
        ++summary.count;
        summary.onlyUser = summary.count == 1 ? user : nullptr;
      });
  });
  auto isOnlyDiscardableUse = [&](sim::SimFuncOp callee, sim::SimCallOp call) {
    if (SymbolTable::getSymbolVisibility(callee) !=
        SymbolTable::Visibility::Private)
      return false;
    SymbolUseSummary &summary = symbolUses[callee];
    return !summary.invalidated && summary.count == 1 &&
           summary.onlyUser == call;
  };
  auto invalidateClonedUses = [&](sim::SimFuncOp callee) {
    forEachDirectCall(callee, [&](sim::SimCallOp nestedCall) {
      auto nested = functions.find(nestedCall.getCallee());
      if (nested != functions.end())
        symbolUses[nested->second].invalidated = true;
    });
  };
  InlinerConfig config;
  unsigned mandatoryIterations =
      controlFunctions.empty() ? 0
                               : static_cast<unsigned>(std::min<size_t>(
                                     controlFunctions.size() + 1,
                                     std::numeric_limits<unsigned>::max()));
  config.setMaxInliningIterations(
      std::max(preset.iterations, mandatoryIterations));

  auto remark = [&](sim::SimCallOp call, const Twine &reason) {
    if (missedRemarks)
      call.emitRemark() << "not inlined: " << reason;
  };

  auto getLegality = [&](sim::SimCallOp call) {
    auto calleeIt = functions.find(call.getCallee());
    sim::SimFuncOp callee =
        calleeIt == functions.end() ? sim::SimFuncOp{} : calleeIt->second;
    auto legality = callee ? calleeLegalities.find(callee.getOperation())
                           : calleeLegalities.end();
    return sim::getInlineLegality(call, callee,
                                  legality == calleeLegalities.end()
                                      ? sim::InlineLegality::NotDefinedFunction
                                      : legality->second);
  };

  // MLIR rejects some calls (notably self-recursive calls and declarations)
  // before consulting the profitability callback. Classify the original call
  // inventory up front so every SimCall contributes deterministic policy
  // statistics and every legality miss can produce an Obelisk remark.
  llvm::SmallPtrSet<Operation *, 32> initialCalls;
  llvm::SmallPtrSet<Operation *, 32> initiallyRejected;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>()) {
    if (function.isExternal())
      continue;
    forEachDirectCall(function, [&](sim::SimCallOp call) {
      initialCalls.insert(call);
      ++considered;
      sim::InlineLegality legality = getLegality(call);
      if (legality == sim::InlineLegality::Legal)
        return;
      initiallyRejected.insert(call);
      ++legalityRejected;
      remark(call, sim::getInlineLegalityReason(legality));
    });
  }

  auto profitability = [&](const Inliner::ResolvedCall &resolved) {
    CallOpInterface callInterface = resolved.call;
    auto call = dyn_cast<sim::SimCallOp>(callInterface.getOperation());
    if (!call)
      return false; // A spawn is an independent process edge.
    if (!initialCalls.contains(call))
      ++considered;
    if (initiallyRejected.contains(call))
      return false;
    sim::SimFuncOp caller = call->getParentOfType<sim::SimFuncOp>();
    auto calleeIt = functions.find(call.getCallee());
    sim::SimFuncOp callee =
        calleeIt == functions.end() ? sim::SimFuncOp{} : calleeIt->second;

    sim::InlineLegality legality = getLegality(call);
    if (legality != sim::InlineLegality::Legal) {
      ++legalityRejected;
      remark(call, sim::getInlineLegalityReason(legality));
      return false;
    }

    // At O0 only process-control propagation is enabled. Reject every other
    // call before computing region costs or walking the callee body; doing
    // that work for a large support library dominates mandatory inlining.
    if (optLevel == 0 && !controlFunctions.contains(callee.getOperation())) {
      ++unprofitable;
      remark(call, "optional inlining is disabled at optimization level 0");
      return false;
    }

    auto cost = regionCostCache.find(callee.getOperation());
    if (cost == regionCostCache.end())
      cost =
          regionCostCache
              .try_emplace(callee.getOperation(),
                           analysis::getSimulationRegionCost(callee.getBody()))
              .first;
    uint64_t calleeCost = cost->second;
    if (controlFunctions.contains(callee.getOperation())) {
      uint64_t callCost = analysis::getSimulationOperationCost(*call);
      uint64_t callerCurrent = callerCurrentCosts.lookup(caller.getOperation());
      uint64_t projectedCaller =
          replaceCost(callerCurrent, callCost, calleeCost);
      uint64_t projectedDesign =
          replaceCost(currentDesignCost, callCost, calleeCost);
      if (isOnlyDiscardableUse(callee, call) && calleeCost <= projectedDesign)
        projectedDesign -= calleeCost;
      uint64_t selectionID = selectedInlineIDs.size() + 1;
      selectedInlineIDs.insert(selectionID);
      call->setAttr(selectedInlineAttr,
                    IntegerAttr::get(IntegerType::get(call.getContext(), 64),
                                     selectionID));
      callerCurrentCosts[caller.getOperation()] = projectedCaller;
      currentDesignCost = projectedDesign;
      invalidateClonedUses(callee);
      regionCostCache.erase(caller.getOperation());
      leafCache.erase(caller.getOperation());
      provenanceCache.erase(caller.getOperation());
      return true;
    }

    auto [leafEntry, insertedLeaf] = leafCache.try_emplace(callee, true);
    if (insertedLeaf)
      forEachDirectCall(callee,
                        [&](sim::SimCallOp) { leafEntry->second = false; });
    bool leaf = leafEntry->second;
    bool tiny = leaf && calleeCost <= preset.tinyCost;
    bool specializes = false;
    if (preset.specializationCost && calleeCost <= preset.specializationCost) {
      auto cached = provenanceCache.find(caller.getOperation());
      if (cached == provenanceCache.end())
        cached = provenanceCache
                     .try_emplace(caller.getOperation(),
                                  analysis::deriveDescriptorProvenance(caller))
                     .first;
      const analysis::DescriptorProvenanceMap &actuals = cached->second;
      Block &entry = callee.getBody().front();
      for (auto [index, argument] : llvm::enumerate(entry.getArguments())) {
        if (argument.use_empty() || index >= call.getNumOperands())
          continue;
        Value actual = call.getOperand(index);
        auto provenance = actuals.find(actual);
        bool concreteDescriptor =
            provenance != actuals.end() &&
            (provenance->second.descriptor ||
             provenance->second.resource == sim::ComputeResourceKind::Local);
        if (matchPattern(actual, m_Constant()) || concreteDescriptor) {
          specializes = true;
          break;
        }
      }
    }
    if (!tiny && !specializes) {
      ++unprofitable;
      remark(call,
             "callee is neither a tiny leaf nor a specialization candidate");
      return false;
    }

    uint64_t callCost = analysis::getSimulationOperationCost(*call);
    uint64_t callerCurrent = callerCurrentCosts.lookup(caller.getOperation());
    uint64_t projectedCaller = replaceCost(callerCurrent, callCost, calleeCost);
    uint64_t projectedDesign =
        replaceCost(currentDesignCost, callCost, calleeCost);
    bool eraseCallee = isOnlyDiscardableUse(callee, call);
    if (eraseCallee) {
      if (calleeCost > projectedDesign) {
        ++budgetRejected;
        remark(call, "weighted design cost accounting overflowed");
        return false;
      }
      projectedDesign -= calleeCost;
    }
    uint64_t callerLimit =
        growthLimit(callerBaselines.lookup(caller.getOperation()),
                    preset.callerGrowthPercent, preset.callerGrowthConstant);
    // An instance coordinator starts as a short call list, so a percentage of
    // its syntactic baseline is not a meaningful growth budget.  Individual
    // callees have already passed the normal tiny/specialization threshold;
    // allow a bounded instance body to form while leaving genuinely large RTL
    // processes outlined.
    if (caller->hasAttr("obelisk.eval.instance_coordinator"))
      callerLimit = std::max(
          callerLimit,
          addSaturating(callerBaselines.lookup(caller.getOperation()), 4096));
    uint64_t designLimit =
        growthLimit(designBaseline, preset.designGrowthPercent,
                    preset.designGrowthConstant);
    if (projectedCaller > callerLimit || projectedDesign > designLimit) {
      ++budgetRejected;
      remark(call, "caller or design weighted-growth budget is exhausted");
      return false;
    }

    uint64_t selectionID = selectedInlineIDs.size() + 1;
    selectedInlineIDs.insert(selectionID);
    call->setAttr(
        selectedInlineAttr,
        IntegerAttr::get(IntegerType::get(call.getContext(), 64), selectionID));
    callerCurrentCosts[caller.getOperation()] = projectedCaller;
    currentDesignCost = projectedDesign;
    invalidateClonedUses(callee);
    regionCostCache.erase(caller.getOperation());
    leafCache.erase(caller.getOperation());
    provenanceCache.erase(caller.getOperation());
    return true;
  };

  Inliner inliner(design, callGraph, *this, getAnalysisManager(),
                  runPipelineHelper, config, profitability);
  LogicalResult result = inliner.doInlining();
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>())
    function->removeAttr(sim::inlineLegalityCacheAttrName);
  llvm::DenseSet<uint64_t> remainingSelections;
  design.walk([&](sim::SimCallOp call) {
    if (auto selection = call->getAttrOfType<IntegerAttr>(selectedInlineAttr)) {
      remainingSelections.insert(selection.getValue().getZExtValue());
      call->removeAttr(selectedInlineAttr);
    }
  });
  for (uint64_t selection : selectedInlineIDs)
    if (!remainingSelections.contains(selection))
      ++inlined;
  bool residualControl = false;
  design.walk([&](sim::SimCallOp call) {
    if (!controlFunctionNames.contains(call.getCallee()))
      return;
    residualControl = true;
    sim::InlineLegality legality = getLegality(call);
    StringRef reason = sim::getInlineLegalityReason(legality);
    call.emitOpError("cannot safely propagate process control through this "
                     "zero-time call: ")
        << (reason.empty() ? "mandatory inlining did not converge" : reason);
  });
  design.walk([&](sim::SimProcessControlOp control) {
    sim::SimFuncOp function = control->getParentOfType<sim::SimFuncOp>();
    if (!function || function.getEntryKind() != sim::EntryKind::Function)
      return;
    // Kill can unwind the bytecode call stack and resume always continues
    // synchronously. Suspend alone requires a persistent callable CPS frame.
    if (control.getKind() != sim::ProcessControlKind::Suspend)
      return;
    residualControl = true;
    control.emitOpError("cannot remain in a zero-time function after mandatory "
                        "process-control inlining; callable boundary requires "
                        "process-control CPS lowering");
  });
  if (failed(result) || residualControl)
    signalPassFailure();
}

} // namespace
} // namespace obelisk
