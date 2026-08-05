//===- ComputeFusion.cpp - Static process-body fusion helpers ------------===//

#include "ComputeFusion.h"

#include "obelisk/Analysis/SimulationAnalysis.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace obelisk {
namespace {

bool isStaticDigitalType(Type type) {
  if (isa<IntegerType, FloatType, sim::LogicType, sim::ContextType,
          sim::BytesType>(type))
    return true;
  if (auto reference = dyn_cast<sim::RefType>(type))
    return isStaticDigitalType(reference.getElementType());
  if (auto net = dyn_cast<sim::NetType>(type))
    return isStaticDigitalType(net.getElementType());
  if (auto driver = dyn_cast<sim::DriverType>(type))
    return isStaticDigitalType(driver.getElementType());
  if (auto packed = dyn_cast<sim::PackedArrayType>(type))
    return isStaticDigitalType(packed.getElementType());
  if (auto unpacked = dyn_cast<sim::UnpackedArrayType>(type))
    return isStaticDigitalType(unpacked.getElementType());
  return false;
}

bool hasOnlyStaticDigitalValues(Operation *operation) {
  return llvm::all_of(operation->getOperandTypes(), isStaticDigitalType) &&
         llvm::all_of(operation->getResultTypes(), isStaticDigitalType);
}

bool hasConcreteDescriptor(
    Value value, const analysis::DescriptorProvenanceMap &provenance) {
  auto found = provenance.find(value);
  if (found == provenance.end() || !found->second.descriptor)
    return false;
  return found->second.resource == sim::ComputeResourceKind::Storage ||
         found->second.resource == sim::ComputeResourceKind::Net;
}

bool hasConcreteHandleValues(
    Operation *operation, const analysis::DescriptorProvenanceMap &provenance) {
  auto check = [&](Value value) {
    Type type = value.getType();
    if (!isa<sim::RefType, sim::NetType, sim::DriverType>(type))
      return true;
    return hasConcreteDescriptor(value, provenance);
  };
  return llvm::all_of(operation->getOperands(), check) &&
         llvm::all_of(operation->getResults(), check);
}

bool isEligibleNBA(sim::SimNBAEnqueueOp enqueue) {
  sim::NBASiteAttr site = enqueue.getSiteAttr();
  return site && !enqueue.getDelay() && !site.getTiming() &&
         (site.getStorage() == sim::ComputeNBAStorageKind::FixedSlot ||
          site.getStorage() == sim::ComputeNBAStorageKind::RootAccumulator);
}

bool rangesOverlap(sim::ComputeEffectAttr lhs, sim::ComputeEffectAttr rhs) {
  if (lhs.getTarget() != sim::ComputeTargetKind::Descriptor ||
      rhs.getTarget() != sim::ComputeTargetKind::Descriptor ||
      lhs.getResource() != rhs.getResource() ||
      lhs.getDescriptor() != rhs.getDescriptor() || lhs.getDynamic() ||
      rhs.getDynamic() || lhs.getWidth() == 0 || rhs.getWidth() == 0)
    return false;
  uint64_t lhsEnd = lhs.getLow() + lhs.getWidth();
  uint64_t rhsEnd = rhs.getLow() + rhs.getWidth();
  if (lhsEnd < lhs.getLow() || rhsEnd < rhs.getLow())
    return false;
  return lhs.getLow() < rhsEnd && rhs.getLow() < lhsEnd;
}

bool isComputeBodyFusionEligibleImpl(
    sim::SimFuncOp function, llvm::DenseMap<Operation *, bool> &cache,
    llvm::SmallPtrSetImpl<Operation *> &active) {
  if (!function || function.isExternal() ||
      !llvm::all_of(function.getFunctionType().getInputs(),
                    isStaticDigitalType))
    return false;
  if (auto cached = cache.find(function.getOperation()); cached != cache.end())
    return cached->second;
  if (!active.insert(function.getOperation()).second)
    return false;

  sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
  analysis::DescriptorProvenanceMap provenance =
      analysis::deriveDescriptorProvenance(function);
  bool eligible = true;
  function.walk([&](Operation *operation) {
    if (!eligible || operation == function.getOperation())
      return;

    // These operations define the only control and scheduler interaction that
    // a fused process may retain. In particular, termination polling is
    // process-independent, while finish/fatal/stop and dynamic control are not.
    if (isa<cf::BranchOp, cf::CondBranchOp, sim::SimReturnOp,
            sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
            sim::SimTerminationRequestedOp>(operation)) {
      eligible = hasOnlyStaticDigitalValues(operation) &&
                 hasConcreteHandleValues(operation, provenance);
      return;
    }

    if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
      sim::SimFuncOp callee = design ? design.lookupSymbol<sim::SimFuncOp>(
                                           call.getCalleeAttr().getValue())
                                     : sim::SimFuncOp{};
      eligible = callee && callee.getEntryKind() == sim::EntryKind::Function &&
                 hasOnlyStaticDigitalValues(operation) &&
                 hasConcreteHandleValues(operation, provenance) &&
                 isComputeBodyFusionEligibleImpl(callee, cache, active);
      return;
    }

    if (auto display = dyn_cast<sim::SimDisplayOp>(operation)) {
      // Static formatting stays on a cold branch of the fused activation; it
      // is not executed by the ordinary clock path.
      eligible = display.getScopeAttr() &&
                 hasOnlyStaticDigitalValues(display) &&
                 hasConcreteHandleValues(display, provenance);
      return;
    }

    if (auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
      eligible = isEligibleNBA(enqueue) &&
                 hasOnlyStaticDigitalValues(operation) &&
                 hasConcreteHandleValues(operation, provenance);
      return;
    }

    if (isa<sim::SimRefLoadOp, sim::SimRefStoreOp, sim::SimNetReadOp,
            sim::SimDriverDriveOp, sim::SimDriverDriveChangedOp>(operation)) {
      eligible = hasOnlyStaticDigitalValues(operation) &&
                 hasConcreteHandleValues(operation, provenance);
      return;
    }

    // Pure arithmetic, aggregate manipulation, and descriptor views are safe
    // only when they remain within the checked static-digital type universe.
    // Every other operation carrying an actor, scheduler, RNG, heap, IO, or
    // managed effect is rejected instead of relying on an open-ended exclusion
    // list.
    eligible = isMemoryEffectFree(operation) &&
               hasOnlyStaticDigitalValues(operation) &&
               hasConcreteHandleValues(operation, provenance);
  });
  active.erase(function.getOperation());
  cache[function.getOperation()] = eligible;
  return eligible;
}

} // namespace

bool isComputeBodyFusionEligible(sim::SimFuncOp function) {
  llvm::DenseMap<Operation *, bool> cache;
  llvm::SmallPtrSet<Operation *, 8> active;
  return isComputeBodyFusionEligibleImpl(function, cache, active);
}

SmallVector<uint32_t>
getComputeFusionReadyTargets(sim::ComputeGraphAttr graph,
                             sim::ComputeEffectAttr sensitivity) {
  SmallVector<uint32_t> targets;
  ArrayAttr nodes = graph.getNodes();

  // Sensitivity and NBA-activation edges enumerate every statically known
  // producer for a watched descriptor range. A delayed continuation may be
  // omitted from the co-ready set only when it is the graph's sole producer:
  // in that case it must be the fragment currently publishing the transition.
  // With multiple possible producers the deadline can be independently ready
  // and must remain an ordering barrier.
  llvm::SmallDenseSet<uint32_t> producers;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if ((edge.getKind() == sim::ComputeEdgeKind::Sensitivity ||
         edge.getKind() == sim::ComputeEdgeKind::NBAActivate) &&
        edge.getResource() && rangesOverlap(edge.getResource(), sensitivity))
      producers.insert(edge.getSource());
  }
  std::optional<uint32_t> uniqueProducer;
  if (producers.size() == 1)
    uniqueProducer = *producers.begin();

  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (edge.getKind() != sim::ComputeEdgeKind::Resume ||
        edge.getSource() >= nodes.size() || edge.getTarget() >= nodes.size())
      continue;
    auto source = dyn_cast<sim::ComputeFragmentAttr>(nodes[edge.getSource()]);
    auto target = dyn_cast<sim::ComputeFragmentAttr>(nodes[edge.getTarget()]);
    if (!source || !target)
      continue;
    if (source.getAction() == sim::ComputeActionKind::SuspendDelay &&
        uniqueProducer == edge.getTarget())
      continue;
    targets.push_back(edge.getTarget());
  }
  return targets;
}

} // namespace obelisk
