//===- DPIEffectAnalysis.cpp - Design state reachable from DPI calls ------===//

#include "obelisk/Analysis/DPIEffectAnalysis.h"
#include "obelisk/Dialect/DPI/DPIDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationResources.h"

#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseSet.h"

using namespace mlir;

namespace obelisk::analysis {

namespace {

/// Effects a transparent export closure may have: values, its own locals and
/// arguments, output, and state private to the foreign side.
bool isTransparentResource(SideEffects::Resource *resource) {
  return isa<sim::IOResource, sim::ExternalResource, sim::InventoryResource,
             sim::StorageResource, sim::NetResource,
             SideEffects::AutomaticAllocationScopeResource>(resource);
}

/// The compute graph's summary of `function`, including its callees, names
/// every design root it touches; a formal target is one of its own
/// arguments, which a DPI export receives and returns by value.
bool touchesOnlyLocals(sim::SimFuncOp function) {
  ArrayAttr summary = function.getEffectSummaryAttr();
  if (!summary)
    return false;
  return llvm::all_of(summary, [](Attribute attribute) {
    auto effect = dyn_cast<schedule::ComputeEffectAttr>(attribute);
    return effect &&
           (effect.getTarget() == schedule::ComputeTargetKind::Local ||
            effect.getTarget() == schedule::ComputeTargetKind::Formal);
  });
}

bool isTransparentOperation(Operation *operation) {
  if (sim::isSuspensionOp(operation) ||
      isa<sim::SimSpawnOp, sim::SimProcessControlOp, sim::SimControlDisableOp,
          sim::SimDPICallOp, sim::SimTaskCallOp, sim::SimClassDirectCallOp,
          sim::SimClassVirtualCallOp, sim::SimClassVirtualTaskCallOp>(
          operation))
    return false;
  auto interface = dyn_cast<MemoryEffectOpInterface>(operation);
  if (!interface)
    return isa<sim::SimCallOp>(operation) || operation->getNumRegions() == 0 ||
           operation->hasTrait<OpTrait::IsTerminator>();
  SmallVector<MemoryEffects::EffectInstance> effects;
  interface.getEffects(effects);
  return llvm::all_of(effects, [](const MemoryEffects::EffectInstance &e) {
    return isTransparentResource(e.getResource());
  });
}

} // namespace

DPIEffectAnalysis::DPIEffectAnalysis(sim::SimDesignOp design) {
  schedule::ComputeGraphAttr graph = design.getComputeGraphAttr();
  bool vpiOff = graph && graph.getVpi() == schedule::ComputeVPIMode::Off;
  SymbolTable symbols(design);
  SmallVector<sim::SimFuncOp> pending;
  for (sim::SimFuncOp function : design.getOps<sim::SimFuncOp>())
    if (dpi::has<dpi::Export>(function))
      pending.push_back(function);
  llvm::DenseSet<Operation *> visited;
  bool transparent = vpiOff;
  while (transparent && !pending.empty()) {
    sim::SimFuncOp function = pending.pop_back_val();
    if (!visited.insert(function).second)
      continue;
    if (function.isExternal() || !touchesOnlyLocals(function)) {
      transparent = false;
      break;
    }
    function.walk([&](Operation *operation) {
      if (!transparent || operation == function.getOperation())
        return;
      if (!isTransparentOperation(operation)) {
        transparent = false;
        return;
      }
      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        auto callee = symbols.lookup<sim::SimFuncOp>(call.getCallee());
        if (callee)
          pending.push_back(callee);
        else
          transparent = false;
      }
    });
  }
  transparentContext = transparent;
  design.walk([&](sim::SimDPICallOp call) {
    opaqueCalls |= mayAccessDesign(call);
  });
}

bool DPIEffectAnalysis::mayAccessDesign(sim::SimDPICallOp call) const {
  return call.getIsContext() && !transparentContext;
}

} // namespace obelisk::analysis
