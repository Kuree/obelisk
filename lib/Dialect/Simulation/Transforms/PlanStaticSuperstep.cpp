//===- PlanStaticSuperstep.cpp - Plan guarded native supersteps ----------===//

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Simulation/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPLANSTATICSUPERSTEPPASS
#include "obelisk/Dialect/Simulation/Transforms/Passes.h.inc"

namespace {

class ObeliskSimPlanStaticSuperstepPass
    : public impl::ObeliskSimPlanStaticSuperstepPassBase<
          ObeliskSimPlanStaticSuperstepPass> {
public:
  using Base::Base;
  void runOnOperation() override;
};

void ObeliskSimPlanStaticSuperstepPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph) {
    design.emitOpError("static superstep planning requires a verified "
                       "compute graph");
    return signalPassFailure();
  }
  design->removeAttr(sim::metadata::staticSuperstep);
  // This pass only publishes plan metadata; function identities are fixed.
  // Resolve every graph fragment through one symbol index.
  SymbolTable symbols(design);
  // Use the same admission proof as coroutine lowering. In particular, a
  // bytecode fragment can belong to an otherwise admitted actor, while an
  // excluded actor must never acquire a static slot here.
  analysis::NativeAOTAnalysis admission =
      analysis::NativeAOTAnalysis::compute(design->getParentOfType<ModuleOp>());
  const auto &actorSlots = admission.getActorSlots();
  const auto &bytecodeFragments = admission.getBytecodeFragments();

  std::string reason;
  auto reject = [&](StringRef message) {
    if (reason.empty())
      reason = message.str();
  };
  if (graph.getVersion() != sim::metadata::schemaVersion)
    reject("unsupported compute-graph version");
  if (graph.getWorkers() != 1)
    reject("static supersteps require one worker");
  bool hasRoot = false;
  design.walk([&](sim::SimFuncOp function) {
    hasRoot |= function.getEntryKind() == sim::EntryKind::RootInitializer;
  });
  if (!hasRoot)
    reject("missing root initializer");
  if (!admission.isEligible())
    reject("no statically schedulable process actors");

  auto isAdmittedFragment = [&](sim::ComputeFragmentAttr fragment) {
    sim::SimFuncOp function =
        symbols.lookup<sim::SimFuncOp>(fragment.getFunction().getValue());
    if (!function || !actorSlots.contains(function.getOperation()))
      return false;
    Block *block =
        analysis::lookupComputeGraphBlock(function, fragment.getBlock());
    auto bytecode = bytecodeFragments.find(function.getOperation());
    return block &&
           (bytecode == bytecodeFragments.end() ||
            !llvm::is_contained(bytecode->second, block));
  };

  for (Attribute attribute : graph.getNodes()) {
    if (auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute)) {
      if (!isAdmittedFragment(fragment))
        continue;
      if (fragment.getTier() != sim::ComputeTierKind::Native) {
        reject("bytecode-only compute fragment");
        continue;
      }
      for (Attribute effectAttribute : fragment.getEffects()) {
        auto effect = cast<sim::ComputeEffectAttr>(effectAttribute);
        if (effect.getEffect() != sim::ComputeEffectKind::Watch)
          continue;
        bool exact = effect.getTarget() == sim::ComputeTargetKind::Descriptor &&
                     !effect.getDynamic() && !effect.getDeferred() &&
                     effect.getWidth() != 0 &&
                     effect.getTrigger() != sim::ComputeTriggerKind::None;
        if (!exact)
          reject("dynamic or unsupported sensitivity");
      }
      continue;
    }
    if (isa<sim::ComputeNBACommitAttr>(attribute)) {
      continue;
    }
    if (isa<sim::ComputeEventCommitAttr>(attribute)) {
      continue;
    }
    reject("unknown compute node");
  }
  // NativeAOTAnalysis has already withheld procedural control-cycle fragments
  // while retaining state-driven members of the enclosing scheduling SCC.

  SmallVector<Attribute> actors(actorSlots.size());
  for (const auto &[operation, slot] : actorSlots) {
    auto function = cast<sim::SimFuncOp>(operation);
    if (slot >= actors.size() || actors[slot]) {
      reject("duplicate or invalid static actor slot");
      continue;
    }
    actors[slot] =
        FlatSymbolRefAttr::get(design.getContext(), function.getSymName());
  }
  if (llvm::any_of(actors, [](Attribute actor) { return !actor; }))
    reject("incomplete static actor inventory");

  if (!reason.empty()) {
    if (missedRemarks)
      design.emitRemark() << "static superstep not planned: " << reason;
    return;
  }
  design->setAttr(sim::metadata::staticSuperstep,
                  sim::StaticSuperstepAttr::get(
                      design.getContext(), sim::metadata::schemaVersion, graph,
                      ArrayAttr::get(design.getContext(), actors)));
}

} // namespace
} // namespace obelisk
