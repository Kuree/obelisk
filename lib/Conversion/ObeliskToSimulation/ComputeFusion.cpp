//===- ComputeFusion.cpp - Static process-body fusion helpers ------------===//

#include "ComputeFusion.h"

#include "obelisk/Analysis/SimulationAnalysis.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
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
  if (isa<sim::PackedStructType, sim::PackedUnionType>(type)) {
    for (unsigned index = 0, count = sim::getAggregateNumElements(type);
         index != count; ++index)
      if (!isStaticDigitalType(sim::getAggregateElementType(type, index)))
        return false;
    return true;
  }
  return false;
}

bool hasOnlyStaticDigitalValues(Operation *operation) {
  return llvm::all_of(operation->getOperandTypes(), isStaticDigitalType) &&
         llvm::all_of(operation->getResultTypes(), isStaticDigitalType);
}

bool hasOnlyStaticDigitalOrTimeValues(Operation *operation) {
  auto eligible = [](Type type) {
    return isStaticDigitalType(type) || isa<sim::TimeType>(type);
  };
  return llvm::all_of(operation->getOperandTypes(), eligible) &&
         llvm::all_of(operation->getResultTypes(), eligible);
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
  return site && !enqueue.getClockingOutputAttr() && !enqueue.getDelay() &&
         !site.getTiming() &&
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
    sim::SimFuncOp function,
    const analysis::DescriptorProvenanceAnalysis &provenanceAnalysis,
    llvm::DenseMap<Operation *, bool> &cache,
    llvm::SmallPtrSetImpl<Operation *> &active, bool primitiveDriverOps) {
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
      provenanceAnalysis.derive(function);
  bool eligible = true;
  function.walk([&](Operation *operation) {
    if (!eligible || operation == function.getOperation())
      return;

    // These operations define the only control and scheduler interaction that
    // a fused process may retain. In particular, termination polling is
    // process-independent, while finish/fatal/stop and dynamic control are not.
    if (isa<cf::BranchOp, cf::CondBranchOp, sim::SimReturnOp,
            sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
            sim::SimSuspendAnyOp, sim::SimTerminationRequestedOp,
            sim::SimCoveragePointHitOp>(operation)) {
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
                 isComputeBodyFusionEligibleImpl(callee, provenanceAnalysis,
                                                 cache, active, false);
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

    if (primitiveDriverOps && isa<sim::SimTimeConstantOp>(operation)) {
      eligible = hasOnlyStaticDigitalOrTimeValues(operation);
      return;
    }

    if (primitiveDriverOps && isa<sim::SimDriverDriveInertialOp>(operation)) {
      eligible = hasOnlyStaticDigitalOrTimeValues(operation) &&
                 hasConcreteHandleValues(operation, provenance);
      return;
    }

    if (isa<sim::SimRefLoadOp, sim::SimRefStoreOp, sim::SimNetReadOp,
            sim::SimDriverDriveOp, sim::SimDriverDriveChangedOp>(operation) ||
        (primitiveDriverOps && isa<sim::SimDriverReadOp>(operation))) {
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

CombinationalFusionAnalysis::CombinationalFusionAnalysis(
    sim::SimDesignOp design,
    const analysis::DescriptorProvenanceAnalysis &provenanceAnalysis) {
  auto graph = design.getComputeGraphAttr();
  // Full foreign mutation needs a range-scoped invalidation route for this
  // idempotent kernel. Existing externally driven evaluators retain their
  // own admission; do not silently extend their contract here.
  unsupported = !graph || graph.getVpi() == sim::ComputeVPIMode::Full;
  if (!graph)
    return;
  design.walk([&](Operation *op) {
    unsupported |= isa<sim::SimDPICallOp, sim::SimOverrideOp,
                       sim::SimReleaseOverrideOp, sim::SimDynamicOverrideOp,
                       sim::SimProcessControlOp, sim::SimControlDisableOp>(op);
  });
  if (unsupported)
    return;
  llvm::StringMap<sim::SimFuncOp> functions;
  llvm::StringMap<SmallVector<Operation *>> callers;
  DenseMap<Operation *, analysis::DescriptorProvenanceMap> provenance;
  bool haveCallers = false;
  auto resolveFormal = [&](StringAttr owner, sim::ComputeEffectAttr effect) {
    if (!haveCallers) {
      for (sim::SimFuncOp function :
           design.getBody().front().getOps<sim::SimFuncOp>())
        functions[function.getSymName()] = function;
      design.walk([&](Operation *op) {
        if (auto call = dyn_cast<sim::SimTaskCallOp>(op))
          callers[call.getCallee()].push_back(op);
        else if (auto call = dyn_cast<sim::SimCallOp>(op))
          callers[call.getCallee()].push_back(op);
        else if (auto spawn = dyn_cast<sim::SimSpawnOp>(op))
          callers[spawn.getCallee()].push_back(op);
        // Descriptor-dispatched call targets need their own complete binding
        // inventory. A direct-call inventory cannot certify those mutations.
        else if (isa<sim::SimClassVirtualTaskCallOp, sim::SimClassVirtualCallOp,
                     sim::SimClassDirectCallOp>(op))
          unsupported = true;
      });
      haveCallers = true;
    }
    using Formal = std::pair<StringAttr, unsigned>;
    SmallVector<Formal> pending{
        {owner, static_cast<unsigned>(effect.getFormal())}};
    DenseSet<Formal> seen;
    while (!pending.empty()) {
      auto [name, index] = pending.pop_back_val();
      if (!seen.insert({name, index}).second)
        continue;
      auto function = functions.lookup(name.getValue());
      auto sites = callers.find(name.getValue());
      if (!function || function->hasAttr("obelisk_sim.dpi_export") ||
          sites == callers.end()) {
        unsupported = true;
        continue;
      }
      for (Operation *site : sites->second) {
        ValueRange arguments = site->getOperands();
        if (auto task = dyn_cast<sim::SimTaskCallOp>(site))
          arguments = task.getArguments();
        auto caller = site->getParentOfType<sim::SimFuncOp>();
        if (!caller || index >= arguments.size()) {
          unsupported = true;
          continue;
        }
        auto [facts, inserted] = provenance.try_emplace(caller.getOperation());
        if (inserted)
          facts->second = provenanceAnalysis.derive(caller);
        auto actual = facts->second.find(arguments[index]);
        if (actual == facts->second.end()) {
          unsupported = true;
          continue;
        }
        const auto &target = actual->second;
        if (target.resource == sim::ComputeResourceKind::Local)
          continue;
        if (target.resource != sim::ComputeResourceKind::Storage) {
          unsupported = true;
        } else if (target.descriptor) {
          // Resolve physical roots, not combinations of call paths. Widen
          // formal subranges within each root until an offset-sensitive call
          // proof is available; unrelated roots stay independent.
          auto resolved = sim::ComputeEffectAttr::get(
              design.getContext(), effect.getEffect(), target.resource,
              sim::ComputeTargetKind::Descriptor, *target.descriptor, 0, 0,
              target.rootWidth, true, effect.getDeferred(),
              effect.getTrigger());
          storageWriters[*target.descriptor].push_back({owner, resolved});
        } else if (target.formal) {
          pending.push_back({caller.getSymNameAttr(), *target.formal});
        } else {
          unsupported = true;
        }
      }
    }
  };
  for (Attribute attr : graph.getNodes()) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attr);
    if (!fragment)
      continue;
    if (fragment.getTier() != sim::ComputeTierKind::Native)
      nonNativeOwners.insert(fragment.getFunction().getAttr());
    for (Attribute attr : fragment.getEffects()) {
      auto effect = cast<sim::ComputeEffectAttr>(attr);
      if (effect.getEffect() != sim::ComputeEffectKind::Write &&
          effect.getEffect() != sim::ComputeEffectKind::NBA)
        continue;
      if (effect.getResource() != sim::ComputeResourceKind::Storage &&
          effect.getResource() != sim::ComputeResourceKind::Unknown)
        continue;
      if (effect.getResource() == sim::ComputeResourceKind::Storage &&
          effect.getTarget() == sim::ComputeTargetKind::Formal) {
        resolveFormal(fragment.getFunction().getAttr(), effect);
        continue;
      }
      if (effect.getTarget() != sim::ComputeTargetKind::Descriptor ||
          effect.getResource() == sim::ComputeResourceKind::Unknown) {
        unsupported = true;
        continue;
      }
      storageWriters[effect.getDescriptor()].push_back(
          {fragment.getFunction().getAttr(), effect});
    }
  }
}

std::optional<CombinationalFusionBody> CombinationalFusionAnalysis::analyze(
    sim::SimFuncOp function,
    const analysis::DescriptorProvenanceAnalysis &analysis) const {
  if (unsupported || !function || function.isExternal() ||
      function.getEntryKind() != sim::EntryKind::AlwaysComb ||
      function.getHomeRegion() != sim::EventRegion::Active ||
      nonNativeOwners.contains(function.getSymNameAttr()) ||
      !isComputeBodyFusionEligible(function, analysis))
    return std::nullopt;
  auto &entry = function.getBody().front();
  auto start = dyn_cast<cf::BranchOp>(entry.getTerminator());
  if (!start || !start.getDestOperands().empty())
    return std::nullopt;
  for (Operation &op : entry.without_terminator())
    if (!isMemoryEffectFree(&op) || !isSpeculatable(&op))
      return std::nullopt;
  CombinationalFusionBody result{start.getDest(), nullptr, {}, {}};
  SmallVector<Value> reads, watches;
  SmallVector<Operation *> stores;
  auto provenance = analysis.derive(function);
  bool valid = true;
  function.walk([&](Operation *op) {
    if (op == function.getOperation())
      return;
    if (auto change = dyn_cast<sim::SimSuspendChangeOp>(op)) {
      valid &= !result.suspend;
      result.suspend = op;
      watches.push_back(change.getWatched());
    } else if (auto any = dyn_cast<sim::SimSuspendAnyOp>(op)) {
      valid &= !result.suspend && llvm::all_of(any.getEdges(), [](int32_t e) {
        return e == static_cast<int32_t>(sim::EdgeKind::Change);
      });
      result.suspend = op;
      watches.append(any.getWatched().begin(), any.getWatched().end());
    } else if (auto load = dyn_cast<sim::SimRefLoadOp>(op)) {
      reads.push_back(load.getReference());
    } else if (auto load = dyn_cast<sim::SimNetReadOp>(op)) {
      reads.push_back(load.getNet());
    } else if (auto store = dyn_cast<sim::SimRefStoreOp>(op)) {
      result.outputs.push_back(store.getReference());
      stores.push_back(op);
    } else if (!isa<cf::BranchOp, cf::CondBranchOp>(op)) {
      valid &= op->getNumRegions() == 0 && isMemoryEffectFree(op);
    }
  });
  if (!valid || !result.suspend || result.outputs.empty() || watches.empty() ||
      result.suspend->getSuccessor(0) != result.activation ||
      !cast<BranchOpInterface>(result.suspend)
           .getSuccessorOperands(0)
           .getForwardedOperands()
           .empty())
    return std::nullopt;
  if (auto region =
          result.suspend->getAttrOfType<sim::EventRegionAttr>("resume_region");
      region && region.getValue() != sim::EventRegion::Active)
    return std::nullopt;
  // An always_comb spelling is not a proof that every path assigns its
  // outputs. Retained-state behavior keeps the original activation protocol.
  DominanceInfo dominance(function);
  for (Operation *store : stores)
    if (!dominance.dominates(store, result.suspend))
      return std::nullopt;
  auto precise = [&](Value value) -> const analysis::DescriptorProvenance * {
    auto found = provenance.find(value);
    if (found == provenance.end() || !found->second.descriptor ||
        found->second.dynamic || !found->second.width ||
        found->second.low > UINT64_MAX - found->second.width)
      return nullptr;
    return &found->second;
  };
  auto overlap = [](const auto &a, const auto &b) {
    return a.resource == b.resource && a.descriptor == b.descriptor &&
           a.low < b.low + b.width && b.low < a.low + a.width;
  };
  for (Value read : reads) {
    const auto *r = precise(read);
    if (!r || !llvm::any_of(watches, [&](Value watch) {
          const auto *w = precise(watch);
          return w && r->resource == w->resource &&
                 r->descriptor == w->descriptor && w->low <= r->low &&
                 r->low + r->width <= w->low + w->width;
        }))
      return std::nullopt;
  }
  for (auto [index, output] : llvm::enumerate(result.outputs)) {
    const auto *w = precise(output);
    auto type = dyn_cast<sim::RefType>(output.getType());
    if (!w || !type || !sim::getPackedScalarType(type.getElementType()))
      return std::nullopt;
    for (Value read : reads)
      if (overlap(*w, *precise(read)))
        return std::nullopt;
    for (Value previous : ArrayRef(result.outputs).take_front(index)) {
      const auto *p = precise(previous);
      if (!p || overlap(*w, *p))
        return std::nullopt;
    }
    auto writers = storageWriters.find(*w->descriptor);
    if (writers == storageWriters.end())
      return std::nullopt;
    for (auto [owner, effect] : writers->second) {
      if (owner == function.getSymNameAttr())
        continue;
      if (effect.getDynamic() || !effect.getWidth() ||
          effect.getLow() > UINT64_MAX - effect.getWidth() ||
          (w->low < effect.getLow() + effect.getWidth() &&
           effect.getLow() < w->low + w->width))
        return std::nullopt;
    }
  }
  // Cut the implicit wait before topological sorting. The process backedge is
  // not combinational feedback. Every other CFG cycle stays outside the group.
  DenseMap<Block *, unsigned> incoming;
  for (Block &block : llvm::drop_begin(function.getBody()))
    incoming[&block] = 0;
  for (auto &[block, count] : incoming) {
    (void)count;
    if (block->getTerminator() == result.suspend)
      continue;
    if (!isa<cf::BranchOp, cf::CondBranchOp>(block->getTerminator()))
      return std::nullopt;
    for (Block *next : block->getSuccessors()) {
      auto found = incoming.find(next);
      if (found == incoming.end())
        return std::nullopt;
      ++found->second;
    }
  }
  SmallVector<Block *> ready;
  for (Block &block : llvm::drop_begin(function.getBody()))
    if (!incoming.lookup(&block))
      ready.push_back(&block);
  if (ready.size() != 1 || ready.front() != result.activation)
    return std::nullopt;
  for (size_t cursor = 0; cursor < ready.size(); ++cursor) {
    Block *block = ready[cursor];
    if (block->getTerminator() != result.suspend)
      for (Block *next : block->getSuccessors())
        if (--incoming[next] == 0)
          ready.push_back(next);
  }
  if (ready.size() != incoming.size())
    return std::nullopt;
  result.blocks = std::move(ready);
  return result;
}

bool isComputeBodyFusionEligible(
    sim::SimFuncOp function,
    const analysis::DescriptorProvenanceAnalysis &provenance) {
  llvm::DenseMap<Operation *, bool> cache;
  llvm::SmallPtrSet<Operation *, 8> active;
  return isComputeBodyFusionEligibleImpl(function, provenance, cache, active,
                                         false);
}

bool isPrimitiveComputeBodyFusionEligible(
    sim::SimFuncOp function,
    const analysis::DescriptorProvenanceAnalysis &provenance) {
  llvm::DenseMap<Operation *, bool> cache;
  llvm::SmallPtrSet<Operation *, 8> active;
  return isComputeBodyFusionEligibleImpl(function, provenance, cache, active,
                                         true);
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
