//===- ComputeFusion.cpp - Static process-body fusion helpers ------------===//

#include "ComputeFusion.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>

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

/// Resolves a storage write through a subroutine formal to the physical
/// roots its call sites pass. IEEE 1800-2023 13.5 copies `output` and `inout`
/// values into the actuals when the subroutine returns, and 13.5.2 makes a
/// `ref` formal an alias of its actual. Either way the actual is the storage
/// written. The simulation IR passes all three as references, so a caller's
/// own effect summary does not record the write; only the callee's
/// formal-target effect does.
class FormalWriteResolver {
public:
  FormalWriteResolver(
      sim::SimDesignOp design,
      const analysis::DescriptorProvenanceAnalysis &provenanceAnalysis)
      : design(design), provenanceAnalysis(provenanceAnalysis) {}

  /// Calls `emit` with a descriptor-target copy of `effect` for every storage
  /// root that `owner`'s formal can reach. Returns false when some call path
  /// cannot be resolved, in which case the reported roots are incomplete.
  bool resolve(StringAttr owner, sim::ComputeEffectAttr effect,
               llvm::function_ref<void(uint64_t, sim::ComputeEffectAttr)>
                   emit) {
    if (!indexed)
      index();
    if (unsupportedCalls)
      return false;
    bool complete = true;
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
        complete = false;
        continue;
      }
      for (Operation *site : sites->second) {
        ValueRange arguments = site->getOperands();
        if (auto task = dyn_cast<sim::SimTaskCallOp>(site))
          arguments = task.getArguments();
        auto caller = site->getParentOfType<sim::SimFuncOp>();
        if (!caller || index >= arguments.size()) {
          complete = false;
          continue;
        }
        auto [facts, inserted] = provenance.try_emplace(caller.getOperation());
        if (inserted)
          facts->second = provenanceAnalysis.derive(caller);
        auto actual = facts->second.find(arguments[index]);
        if (actual == facts->second.end()) {
          complete = false;
          continue;
        }
        const auto &target = actual->second;
        if (target.resource == sim::ComputeResourceKind::Local)
          continue;
        if (target.resource != sim::ComputeResourceKind::Storage) {
          complete = false;
        } else if (target.descriptor) {
          // Resolve physical roots, not combinations of call paths. Widen
          // formal subranges within each root until an offset-sensitive call
          // proof is available; unrelated roots stay independent.
          emit(*target.descriptor,
               sim::ComputeEffectAttr::get(
                   design.getContext(), effect.getEffect(), target.resource,
                   sim::ComputeTargetKind::Descriptor, *target.descriptor, 0,
                   0, target.rootWidth, true, effect.getDeferred(),
                   effect.getTrigger()));
        } else if (target.formal) {
          pending.push_back({caller.getSymNameAttr(), *target.formal});
        } else {
          complete = false;
        }
      }
    }
    return complete;
  }

private:
  void index() {
    indexed = true;
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
        unsupportedCalls = true;
    });
  }

  sim::SimDesignOp design;
  const analysis::DescriptorProvenanceAnalysis &provenanceAnalysis;
  bool indexed = false;
  bool unsupportedCalls = false;
  llvm::StringMap<sim::SimFuncOp> functions;
  llvm::StringMap<SmallVector<Operation *>> callers;
  DenseMap<Operation *, analysis::DescriptorProvenanceMap> provenance;
};

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
  FormalWriteResolver formals(design, provenanceAnalysis);
  auto resolveFormal = [&](StringAttr owner, sim::ComputeEffectAttr effect) {
    unsupported |= !formals.resolve(
        owner, effect, [&](uint64_t descriptor, sim::ComputeEffectAttr write) {
          storageWriters[descriptor].push_back({owner, write});
        });
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

namespace {

bool overlaps(sim::ComputeEffectAttr a, sim::ComputeEffectAttr b) {
  if (a.getResource() != b.getResource() ||
      a.getDescriptor() != b.getDescriptor())
    return false;
  if (a.getDynamic() || b.getDynamic() || !a.getWidth() || !b.getWidth())
    return true;
  return a.getLow() < b.getLow() + b.getWidth() &&
         b.getLow() < a.getLow() + a.getWidth();
}

bool covers(sim::ComputeEffectAttr watch, sim::ComputeEffectAttr read) {
  return watch.getResource() == read.getResource() &&
         watch.getDescriptor() == read.getDescriptor() &&
         !watch.getDynamic() && watch.getLow() <= read.getLow() &&
         read.getLow() + read.getWidth() <= watch.getLow() + watch.getWidth();
}

/// An extra activation of `function` after the NBA region is unobservable
/// when it recomputes exactly the values it already holds. That needs a
/// looping process with one change-only wait; every read watched, so a wake
/// that changes no watched value sees the inputs of its previous activation;
/// no write it reads back; exclusive stored outputs; and a first activation
/// before the first wait, so the outputs are current from time zero.
bool isIdempotentObserver(
    sim::SimFuncOp function,
    const llvm::DenseMap<uint64_t, SmallVector<std::pair<Operation *,
                                                         sim::ComputeEffectAttr>>>
        &storageWriters) {
  if (function.isExternal() ||
      function.getEntryKind() == sim::EntryKind::Observer ||
      function.getHomeRegion() != sim::EventRegion::Active)
    return false;
  ArrayAttr summary = function.getEffectSummaryAttr();
  if (!summary)
    return false;
  Operation *suspension = nullptr;
  bool valid = true;
  function.walk([&](Operation *op) {
    if (op == function.getOperation() || !valid)
      return;
    if (sim::isSuspensionOp(op)) {
      auto any = dyn_cast<sim::SimSuspendAnyOp>(op);
      valid = !suspension &&
              (isa<sim::SimSuspendChangeOp>(op) ||
               (any && llvm::all_of(any.getEdges(), [](int32_t edge) {
                  return edge == static_cast<int32_t>(sim::EdgeKind::Change);
                })));
      suspension = op;
      return;
    }
    if (isa<sim::SimRefLoadOp, sim::SimNetReadOp, sim::SimRefStoreOp,
            sim::SimDriverDriveOp, sim::SimDriverDriveChangedOp,
            cf::BranchOp, cf::CondBranchOp>(op))
      return;
    valid = op->getNumRegions() == 0 && isMemoryEffectFree(op);
  });
  if (!valid || !suspension || suspension->getNumSuccessors() != 1)
    return false;
  if (auto region =
          suspension->getAttrOfType<sim::EventRegionAttr>("resume_region");
      region && region.getValue() != sim::EventRegion::Active)
    return false;
  // Every path from entry to the wait must first run the activation, unless
  // the process is evaluated at time zero by definition: continuous
  // assignments, including those inferred from ports (LRM 4.9.1).
  // A process that writes nothing cannot be out of date either.
  Block *activation = suspension->getSuccessor(0);
  Block *wait = suspension->getBlock();
  bool writesAnything = llvm::any_of(
      summary.getAsRange<sim::ComputeEffectAttr>(),
      [](sim::ComputeEffectAttr effect) {
        return effect.getEffect() != sim::ComputeEffectKind::Read &&
               effect.getEffect() != sim::ComputeEffectKind::Watch;
      });
  bool evaluatedAtTimeZero =
      !writesAnything ||
      function.getEntryKind() == sim::EntryKind::Continuous ||
      function.getEntryKind() == sim::EntryKind::PortInput ||
      function.getEntryKind() == sim::EntryKind::PortOutput;
  SmallVector<Block *> pending;
  if (!evaluatedAtTimeZero)
    pending.push_back(&function.getBody().front());
  llvm::SmallPtrSet<Block *, 16> visited;
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (block == activation || !visited.insert(block).second)
      continue;
    if (block == wait)
      return false;
    llvm::append_range(pending, block->getSuccessors());
  }
  // The activation must return to the same wait on every path.
  pending.assign({activation});
  visited.clear();
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (block == wait || !visited.insert(block).second)
      continue;
    if (block->getNumSuccessors() == 0)
      return false;
    llvm::append_range(pending, block->getSuccessors());
  }

  SmallVector<sim::ComputeEffectAttr> reads, watches, writes;
  for (auto effect : summary.getAsRange<sim::ComputeEffectAttr>()) {
    if (effect.getTarget() != sim::ComputeTargetKind::Descriptor ||
        effect.getDeferred())
      return false;
    switch (effect.getEffect()) {
    case sim::ComputeEffectKind::Read:
      reads.push_back(effect);
      break;
    case sim::ComputeEffectKind::Watch:
      if (effect.getDynamic() ||
          (effect.getTrigger() != sim::ComputeTriggerKind::None &&
           effect.getTrigger() != sim::ComputeTriggerKind::Change))
        return false;
      watches.push_back(effect);
      break;
    case sim::ComputeEffectKind::Write:
    case sim::ComputeEffectKind::Drive:
      if (effect.getDynamic())
        return false;
      writes.push_back(effect);
      break;
    default:
      return false;
    }
  }
  for (sim::ComputeEffectAttr read : reads)
    if (!llvm::any_of(watches, [&](sim::ComputeEffectAttr watch) {
          return covers(watch, read);
        }))
      return false;
  for (sim::ComputeEffectAttr write : writes) {
    if (llvm::any_of(reads, [&](sim::ComputeEffectAttr read) {
          return overlaps(write, read);
        }))
      return false;
    // A driver belongs to this process. A stored output must not be
    // replaced by another writer between two activations.
    if (write.getEffect() != sim::ComputeEffectKind::Write)
      continue;
    auto others = storageWriters.find(write.getDescriptor());
    if (others != storageWriters.end())
      for (auto [owner, effect] : others->second)
        if (owner != function.getOperation() && overlaps(write, effect))
          return false;
  }
  return true;
}

/// The most `counted` operations on any control-flow path that starts at
/// `from` and ends at the end of `until` (or at a block without successors).
/// std::nullopt when a cycle avoids `until` or a counted operation is nested
/// in a region, since neither has a static bound.
std::optional<unsigned>
maxCountOnPaths(Block *from, Block *until,
                llvm::function_ref<bool(Operation *)> counted) {
  llvm::DenseMap<Block *, unsigned> memo;
  llvm::SmallPtrSet<Block *, 16> active;
  bool bounded = true;
  std::function<unsigned(Block *)> visit = [&](Block *block) -> unsigned {
    if (auto found = memo.find(block); found != memo.end())
      return found->second;
    if (!active.insert(block).second) {
      bounded = false;
      return 0;
    }
    unsigned here = 0;
    for (Operation &op : *block) {
      if (counted(&op))
        ++here;
      else if (op.getNumRegions() != 0 &&
               op.walk([&](Operation *nested) {
                   return counted(nested) ? WalkResult::interrupt()
                                          : WalkResult::advance();
                 }).wasInterrupted())
        bounded = false;
    }
    unsigned best = 0;
    if (block != until)
      for (Block *next : block->getSuccessors())
        best = std::max(best, visit(next));
    active.erase(block);
    return memo[block] = here + best;
  };
  unsigned count = visit(from);
  if (!bounded)
    return std::nullopt;
  return count;
}

/// The single suspension of a looping process, or null.
Operation *singleSuspension(sim::SimFuncOp function) {
  Operation *suspension = nullptr;
  bool unique = true;
  function.walk([&](Operation *op) {
    if (!sim::isSuspensionOp(op))
      return;
    unique &= suspension == nullptr;
    suspension = op;
  });
  return unique && suspension && suspension->getNumSuccessors() == 1
             ? suspension
             : nullptr;
}

} // namespace

std::optional<NBATransientObservers>
computeNBATransientObservers(sim::SimDesignOp design) {
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  // VPI value-change callbacks run at each update.
  if (!graph || graph.getVpi() != sim::ComputeVPIMode::Off)
    return std::nullopt;
  llvm::DenseMap<uint64_t,
                 SmallVector<std::pair<Operation *, sim::ComputeEffectAttr>>>
      storageWriters;
  SmallVector<sim::SimFuncOp> functions;
  bool complete = true;
  // Proofs that remove a root from `changeWatched` or skip an observer need
  // every writer of a root. A write through a formal is attributed to the
  // actuals its call sites pass (IEEE 1800-2023 13.5: output and inout
  // actuals are written on return, ref actuals directly). A write that cannot
  // be resolved leaves the inventory incomplete; that disables those proofs,
  // not the whole analysis.
  bool writersComplete = true;
  analysis::DescriptorProvenanceAnalysis provenanceAnalysis(design);
  FormalWriteResolver formals(design, provenanceAnalysis);
  design.walk([&](sim::SimFuncOp function) {
    functions.push_back(function);
    ArrayAttr summary = function.getEffectSummaryAttr();
    if (!summary) {
      // A body without a summary may still wait on storage.
      complete &= function.isExternal() ||
                  !function
                       .walk([](Operation *op) {
                         return sim::isSuspensionOp(op) ? WalkResult::interrupt()
                                                        : WalkResult::advance();
                       })
                       .wasInterrupted();
      return;
    }
    for (auto effect : summary.getAsRange<sim::ComputeEffectAttr>()) {
      if (effect.getEffect() != sim::ComputeEffectKind::Write &&
          effect.getEffect() != sim::ComputeEffectKind::NBA)
        continue;
      // A write through an unresolved handle can reach any storage root.
      if (effect.getResource() == sim::ComputeResourceKind::Unknown)
        writersComplete = false;
      if (effect.getResource() == sim::ComputeResourceKind::Storage) {
        if (effect.getTarget() == sim::ComputeTargetKind::Formal) {
          writersComplete &= formals.resolve(
              function.getSymNameAttr(), effect,
              [&](uint64_t descriptor, sim::ComputeEffectAttr write) {
                storageWriters[descriptor].push_back(
                    {function.getOperation(), write});
              });
          continue;
        }
        if (effect.getTarget() != sim::ComputeTargetKind::Descriptor) {
          writersComplete = false;
          continue;
        }
        storageWriters[effect.getDescriptor()].push_back(
            {function.getOperation(), effect});
      }
    }
  });
  design.walk([&](Operation *op) {
    complete &= !isa<sim::SimDPICallOp>(op);
  });
  if (!complete)
    return std::nullopt;

  NBATransientObservers result;
  llvm::DenseSet<uint64_t> &observable = result.observable;
  design.walk([&](sim::SimStorageDeclOp storage) {
    if (storage->hasAttr(sim::metadata::coverageToggleObservable))
      observable.insert(storage.getId());
  });
  for (sim::SimFuncOp function : functions) {
    ArrayAttr summary = function.getEffectSummaryAttr();
    if (!summary)
      continue;
    SmallVector<sim::ComputeEffectAttr> watches;
    for (auto effect : summary.getAsRange<sim::ComputeEffectAttr>())
      if (effect.getEffect() == sim::ComputeEffectKind::Watch &&
          effect.getResource() != sim::ComputeResourceKind::Net)
        watches.push_back(effect);
    if (watches.empty() ||
        (writersComplete && isIdempotentObserver(function, storageWriters)))
      continue;
    // A process that waits only for a change of whole references wakes when
    // any update changes a watched bit. A merged commit reproduces exactly
    // that if it also reports bits rewritten with a different value in one
    // barrier, so such a watch needs a transient mask, not ordered commits.
    // Edge, level, conditional and expression waits see the value sequence.
    bool changeWaitsOnly =
        function.getEntryKind() != sim::EntryKind::Observer &&
        !function
             .walk([](Operation *op) {
               if (!sim::isSuspensionOp(op) ||
                   isa<sim::SimSuspendChangeOp, sim::SimSuspendDelayOp>(op))
                 return WalkResult::advance();
               auto any = dyn_cast<sim::SimSuspendAnyOp>(op);
               bool change =
                   any && llvm::all_of(any.getEdges(), [](int32_t edge) {
                     return edge ==
                            static_cast<int32_t>(sim::EdgeKind::Change);
                   });
               return change ? WalkResult::advance() : WalkResult::interrupt();
             })
             .wasInterrupted();
    for (sim::ComputeEffectAttr watch : watches) {
      // A watch through a subroutine formal or an unresolved handle can
      // reach any root.
      if (watch.getTarget() != sim::ComputeTargetKind::Descriptor)
        return std::nullopt;
      if (watch.getResource() != sim::ComputeResourceKind::Storage)
        continue;
      // A dynamic watch wakes on a change anywhere in its root, which the
      // commit's per-bit change report covers as well.
      bool changeWatch = changeWaitsOnly &&
                         (watch.getTrigger() == sim::ComputeTriggerKind::None ||
                          watch.getTrigger() == sim::ComputeTriggerKind::Change);
      (changeWatch ? result.changeWatched : observable)
          .insert(watch.getDescriptor());
    }
  }
  for (uint64_t descriptor : observable)
    result.changeWatched.erase(descriptor);

  // A change-watched root that receives at most one NBA per bit in any time
  // slot has no round trip to report, so it needs no transient mask. Prove it
  // on source processes: one writer process, at most one write per activation
  // path, activated only by an edge of a clock that changes at most once per
  // slot (its sole writer toggles it after each positive delay).
  llvm::DenseMap<uint64_t, SmallVector<sim::SimFuncOp>> netDrivers;
  for (sim::SimFuncOp function : functions)
    if (ArrayAttr summary = function.getEffectSummaryAttr())
      for (auto effect : summary.getAsRange<sim::ComputeEffectAttr>())
        if (effect.getEffect() == sim::ComputeEffectKind::Drive &&
            effect.getResource() == sim::ComputeResourceKind::Net)
          netDrivers[effect.getDescriptor()].push_back(function);
  bool processControl = false;
  llvm::StringMap<unsigned> spawns;
  llvm::StringSet<> spawnedOutsideRoot, called, calledOutsideRoot;
  design.walk([&](Operation *op) {
    processControl |=
        isa<sim::SimProcessControlOp, sim::SimControlDisableOp,
            sim::SimOverrideOp, sim::SimReleaseOverrideOp,
            sim::SimDynamicOverrideOp>(op);
    if (auto spawn = dyn_cast<sim::SimSpawnOp>(op)) {
      ++spawns[spawn.getCallee()];
      auto owner = spawn->getParentOfType<sim::SimFuncOp>();
      if (!owner || owner.getEntryKind() != sim::EntryKind::RootInitializer ||
          spawn->getParentRegion() != &owner.getBody())
        spawnedOutsideRoot.insert(spawn.getCallee());
    } else if (isa<sim::SimCallOp, sim::SimTaskCallOp>(op)) {
      StringRef callee = isa<sim::SimCallOp>(op)
                             ? cast<sim::SimCallOp>(op).getCallee()
                             : cast<sim::SimTaskCallOp>(op).getCallee();
      called.insert(callee);
      auto owner = op->getParentOfType<sim::SimFuncOp>();
      if (!owner || owner.getEntryKind() != sim::EntryKind::RootInitializer)
        calledOutsideRoot.insert(callee);
    }
  });
  // Declaration initializers run from the root initializer. IEEE 1800-2017
  // 6.8 sets these values before any initial or always procedure starts, so
  // no wait exists yet to observe them.
  auto initializesOnly = [&](sim::SimFuncOp function) {
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      return true;
    StringRef name = function.getSymName();
    return function.getEntryKind() == sim::EntryKind::Function &&
           called.contains(name) && !calledOutsideRoot.contains(name) &&
           !spawns.contains(name) &&
           !function
                .walk([](Operation *op) {
                  return sim::isSuspensionOp(op) ? WalkResult::interrupt()
                                                 : WalkResult::advance();
                })
                .wasInterrupted();
  };
  if (processControl || !writersComplete || result.changeWatched.empty())
    return result;
  auto singleInstance = [&](sim::SimFuncOp function) {
    StringRef name = function.getSymName();
    return spawns.lookup(name) == 1 && !spawnedOutsideRoot.contains(name) &&
           !called.contains(name);
  };
  llvm::DenseMap<Operation *, analysis::DescriptorProvenanceMap> provenance;
  using Resource = std::pair<sim::ComputeResourceKind, uint64_t>;
  auto resourceOf = [&](sim::SimFuncOp function,
                        Value value) -> std::optional<Resource> {
    auto [entry, inserted] = provenance.try_emplace(function.getOperation());
    if (inserted)
      entry->second = provenanceAnalysis.derive(function);
    auto found = entry->second.find(value);
    if (found == entry->second.end() || !found->second.descriptor ||
        found->second.dynamic ||
        (found->second.resource != sim::ComputeResourceKind::Storage &&
         found->second.resource != sim::ComputeResourceKind::Net))
      return std::nullopt;
    return Resource{found->second.resource, *found->second.descriptor};
  };
  auto descriptorOf = [&](sim::SimFuncOp function,
                          Value value) -> std::optional<uint64_t> {
    std::optional<Resource> resource = resourceOf(function, value);
    if (!resource || resource->first != sim::ComputeResourceKind::Storage)
      return std::nullopt;
    return resource->second;
  };
  // Every path through one activation writes `descriptor` at most `limit`
  // times, and the path from process start to the first wait at most
  // `startLimit` times.
  auto boundedWrites = [&](sim::SimFuncOp function, Operation *suspension,
                           uint64_t descriptor, unsigned limit,
                           unsigned startLimit,
                           llvm::function_ref<Value(Operation *)> target) {
    auto counted = [&](Operation *op) {
      Value destination = target(op);
      if (!destination)
        return false;
      std::optional<uint64_t> written = descriptorOf(function, destination);
      return !written || *written == descriptor;
    };
    Block *wait = suspension->getBlock();
    std::optional<unsigned> activation =
        maxCountOnPaths(suspension->getSuccessor(0), wait, counted);
    std::optional<unsigned> start =
        maxCountOnPaths(&function.getBody().front(), wait, counted);
    return activation && start && *activation <= limit && *start <= startLimit;
  };
  // The resource a copy process forwards to `written`, such as a port
  // connection: it waits for any change, reads one resource and writes
  // `written` once per activation, so `written` changes at most once per
  // change of that resource.
  auto copiedBy = [&](sim::SimFuncOp copy,
                      Resource written) -> std::optional<Resource> {
    Operation *wait = singleSuspension(copy);
    auto any = dyn_cast_or_null<sim::SimSuspendAnyOp>(wait);
    ArrayAttr summary = copy.getEffectSummaryAttr();
    if (!wait || !summary || !singleInstance(copy) ||
        !(isa<sim::SimSuspendChangeOp>(wait) ||
          (any && llvm::all_of(any.getEdges(), [](int32_t edge) {
             return edge == static_cast<int32_t>(sim::EdgeKind::Change);
           }))))
      return std::nullopt;
    std::optional<Resource> copied;
    for (auto effect : summary.getAsRange<sim::ComputeEffectAttr>()) {
      if (effect.getTarget() != sim::ComputeTargetKind::Descriptor ||
          effect.getDynamic())
        return std::nullopt;
      Resource resource{effect.getResource(), effect.getDescriptor()};
      switch (effect.getEffect()) {
      case sim::ComputeEffectKind::Read:
      case sim::ComputeEffectKind::Watch:
        if (copied && *copied != resource)
          return std::nullopt;
        copied = resource;
        break;
      case sim::ComputeEffectKind::Drive:
      case sim::ComputeEffectKind::Write:
        if (resource != written)
          return std::nullopt;
        break;
      default:
        return std::nullopt;
      }
    }
    unsigned writes = 0;
    copy.walk([&](Operation *op) {
      writes += isa<sim::SimDriverDriveOp, sim::SimDriverDriveChangedOp,
                    sim::SimRefStoreOp>(op);
    });
    if (writes > 1)
      return std::nullopt;
    return copied;
  };
  std::function<bool(Resource, unsigned)> freeRunningClock =
      [&](Resource source, unsigned depth) -> bool {
    if (depth > 8)
      return false;
    if (source.first == sim::ComputeResourceKind::Net) {
      auto drivers = netDrivers.find(source.second);
      if (drivers == netDrivers.end() || drivers->second.size() != 1)
        return false;
      std::optional<Resource> copied = copiedBy(drivers->second.front(), source);
      return copied && freeRunningClock(*copied, depth + 1);
    }
    uint64_t clock = source.second;
    auto writers = storageWriters.find(clock);
    if (writers == storageWriters.end())
      return false;
    sim::SimFuncOp toggler;
    for (auto [owner, effect] : writers->second) {
      auto function = cast<sim::SimFuncOp>(owner);
      if (effect.getEffect() != sim::ComputeEffectKind::Write)
        return false;
      if (initializesOnly(function))
        continue;
      if (toggler && toggler != function)
        return false;
      toggler = function;
    }
    if (!toggler || !singleInstance(toggler))
      return false;
    if (std::optional<Resource> copied = copiedBy(toggler, source))
      return freeRunningClock(*copied, depth + 1);
    auto delay = dyn_cast_or_null<sim::SimSuspendDelayOp>(
        singleSuspension(toggler));
    auto constant =
        delay ? delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>()
              : sim::SimTimeConstantOp{};
    if (!constant || constant.getValue() == 0)
      return false;
    // No write before the first delay, so at time zero only declaration
    // initialization, which precedes every procedure (6.8), sets the clock.
    return boundedWrites(toggler, delay, clock, 1, 0, [](Operation *op) {
      auto store = dyn_cast<sim::SimRefStoreOp>(op);
      return store ? store.getReference() : Value{};
    });
  };
  SmallVector<uint64_t> candidates(result.changeWatched.begin(),
                                   result.changeWatched.end());
  for (uint64_t root : candidates) {
    auto writers = storageWriters.find(root);
    if (writers == storageWriters.end())
      continue;
    sim::SimFuncOp process;
    bool single = true;
    for (auto [owner, effect] : writers->second) {
      if (effect.getEffect() != sim::ComputeEffectKind::NBA)
        continue;
      auto function = cast<sim::SimFuncOp>(owner);
      single &= !process || process == function;
      process = function;
    }
    if (!single || !process || !singleInstance(process) ||
        (process.getEntryKind() != sim::EntryKind::Always &&
         process.getEntryKind() != sim::EntryKind::AlwaysFF))
      continue;
    auto edge =
        dyn_cast_or_null<sim::SimSuspendEdgeOp>(singleSuspension(process));
    if (!edge)
      continue;
    std::optional<Resource> clock = resourceOf(process, edge.getWatched());
    if (!clock ||
        (clock->first == sim::ComputeResourceKind::Storage &&
         clock->second == root) ||
        !freeRunningClock(*clock, 0))
      continue;
    if (boundedWrites(process, edge, root, 1, 0, [](Operation *op) {
          auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(op);
          return enqueue ? enqueue.getDestination() : Value{};
        }))
      result.changeWatched.erase(root);
  }
  return result;
}

} // namespace obelisk
