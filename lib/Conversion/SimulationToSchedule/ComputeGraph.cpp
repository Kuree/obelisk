//===- ComputeGraph.cpp - Derive the late simulation schedule ------------===//
//
// The executable simulation CFG remains the source of truth. This analysis
// derives deterministic compiler metadata from it: precise descriptor-range
// summaries, fixed static sites, fragment ABI records, and an event-region
// graph with SCC convergence groups. It never mutates the design.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Conversion/SimulationToSchedule/ComputeGraph.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Analysis/NetConnectivityAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <tuple>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

namespace {

//===----------------------------------------------------------------------===//
// Descriptor provenance
//===----------------------------------------------------------------------===//

HandleFact widenDynamic(HandleFact provenance) {
  if (provenance.resource != schedule::ComputeResourceKind::Unknown)
    provenance.dynamic = true;
  return provenance;
}

HandleFact narrowProvenance(HandleFact provenance, uint64_t low,
                            uint64_t width) {
  if (provenance.resource == schedule::ComputeResourceKind::Unknown)
    return provenance;
  if (provenance.low > provenance.rootWidth ||
      low > provenance.rootWidth - provenance.low ||
      width > provenance.rootWidth - provenance.low - low)
    return widenDynamic(provenance);
  provenance.low += low;
  provenance.width = width;
  return provenance;
}

/// The complete statically known base range a slice belongs to. Formal handles
/// with no concrete descriptor widen all the way to unknown because two callers
/// may bind them to different roots.
HandleFact getRootProvenance(HandleFact provenance) {
  if (provenance.resource == schedule::ComputeResourceKind::Unknown)
    return {};
  if (provenance.formal && !provenance.descriptor)
    return {};
  provenance.low = 0;
  provenance.width = provenance.rootWidth;
  provenance.dynamic = false;
  return provenance;
}

bool provenanceLess(const HandleFact &lhs, const HandleFact &rhs) {
  auto key = [](const HandleFact &provenance) {
    return std::tuple<unsigned, uint64_t, unsigned, uint64_t, uint64_t,
                      uint64_t, bool>(
        static_cast<unsigned>(provenance.resource),
        provenance.descriptor.value_or(std::numeric_limits<uint64_t>::max()),
        provenance.formal.value_or(std::numeric_limits<unsigned>::max()),
        provenance.low, provenance.width, provenance.rootWidth,
        provenance.dynamic);
  };
  return key(lhs) < key(rhs);
}

/// Whether two resolved ranges may denote overlapping state.
bool provenancesAlias(const HandleFact &lhs, const HandleFact &rhs) {
  if (lhs.resource == schedule::ComputeResourceKind::Unknown ||
      rhs.resource == schedule::ComputeResourceKind::Unknown)
    return true;
  if (lhs.resource != rhs.resource)
    return false;
  if (lhs.descriptor && rhs.descriptor && lhs.descriptor != rhs.descriptor)
    return false;
  // Formal handles may alias each other or any concrete descriptor of their
  // resource class until a static caller/spawn specialization proves more.
  if (!lhs.descriptor || !rhs.descriptor)
    return true;
  if (lhs.dynamic || rhs.dynamic)
    return true;
  if (lhs.low > std::numeric_limits<uint64_t>::max() - lhs.width ||
      rhs.low > std::numeric_limits<uint64_t>::max() - rhs.width)
    return true;
  return lhs.low < rhs.low + rhs.width && rhs.low < lhs.low + lhs.width;
}

//===----------------------------------------------------------------------===//
// Effects
//===----------------------------------------------------------------------===//

struct ComputeEffect {
  schedule::ComputeEffectKind kind = schedule::ComputeEffectKind::Read;
  HandleFact target;
  schedule::ComputeTriggerKind trigger = schedule::ComputeTriggerKind::None;
  bool deferred = false;

  bool operator==(const ComputeEffect &other) const {
    return kind == other.kind && target == other.target &&
           trigger == other.trigger && deferred == other.deferred;
  }
};

bool effectLess(const ComputeEffect &lhs, const ComputeEffect &rhs) {
  if (lhs.kind != rhs.kind)
    return static_cast<unsigned>(lhs.kind) < static_cast<unsigned>(rhs.kind);
  if (!(lhs.target == rhs.target))
    return provenanceLess(lhs.target, rhs.target);
  if (lhs.trigger != rhs.trigger)
    return static_cast<unsigned>(lhs.trigger) <
           static_cast<unsigned>(rhs.trigger);
  return lhs.deferred < rhs.deferred;
}

void normalizeEffects(SmallVectorImpl<ComputeEffect> &effects) {
  llvm::sort(effects, effectLess);
  effects.erase(std::unique(effects.begin(), effects.end()), effects.end());
  if (effects.size() < 2)
    return;

  // The frontend commonly lowers packed assignments into one operation per
  // bit.  Keep source ordering where it matters (NBA journals and triggers),
  // but represent ordinary dependency/publication effects as maximal ranges.
  // Graph aliasing is range based, so this preserves all conflicts while
  // avoiding 32 or 64 copies of the same edge and downstream fanout work.
  auto compatible = [](const ComputeEffect &left, const ComputeEffect &right) {
    const HandleFact &lhs = left.target;
    const HandleFact &rhs = right.target;
    return left.kind == right.kind &&
           left.kind != schedule::ComputeEffectKind::NBA &&
           left.kind != schedule::ComputeEffectKind::Trigger &&
           left.trigger == right.trigger && left.deferred == right.deferred &&
           lhs.resource == rhs.resource && lhs.descriptor == rhs.descriptor &&
           lhs.formal == rhs.formal && lhs.rootWidth == rhs.rootWidth &&
           !lhs.dynamic && !rhs.dynamic && lhs.width != 0 && rhs.width != 0 &&
           lhs.low <= std::numeric_limits<uint64_t>::max() - lhs.width &&
           rhs.low <= std::numeric_limits<uint64_t>::max() - rhs.width &&
           rhs.low <= lhs.low + lhs.width;
  };
  SmallVector<ComputeEffect> coalesced;
  coalesced.reserve(effects.size());
  for (const ComputeEffect &effect : effects) {
    if (!coalesced.empty() && compatible(coalesced.back(), effect)) {
      HandleFact &range = coalesced.back().target;
      uint64_t end = std::max(range.low + range.width,
                              effect.target.low + effect.target.width);
      range.width = end - range.low;
      continue;
    }
    coalesced.push_back(effect);
  }
  effects.assign(coalesced.begin(), coalesced.end());
}

/// Effects that publish a new value in the active region. NBA staging and
/// sensitivity subscriptions are not active producers.
bool isActiveProducer(const ComputeEffect &effect) {
  return !effect.deferred &&
         (effect.kind == schedule::ComputeEffectKind::Write ||
          effect.kind == schedule::ComputeEffectKind::Drive ||
          effect.kind == schedule::ComputeEffectKind::Trigger);
}

bool activeEffectsConflict(const ComputeEffect &lhs, const ComputeEffect &rhs) {
  if (lhs.kind == schedule::ComputeEffectKind::Watch ||
      rhs.kind == schedule::ComputeEffectKind::Watch ||
      lhs.kind == schedule::ComputeEffectKind::NBA ||
      rhs.kind == schedule::ComputeEffectKind::NBA)
    return false;
  if (!isActiveProducer(lhs) && !isActiveProducer(rhs))
    return false;
  return provenancesAlias(lhs.target, rhs.target);
}

schedule::ComputeEffectAttr getEffectAttr(MLIRContext *context,
                                          const ComputeEffect &effect) {
  // An unresolved handle keeps no range: propagation can reach `unknown` while
  // still carrying the width of the type it started from, and publishing that
  // would build an attribute this dialect's own verifier rejects.
  HandleFact provenance = effect.target;
  if (provenance.resource == schedule::ComputeResourceKind::Unknown)
    provenance = {};
  schedule::ComputeTargetKind target = schedule::ComputeTargetKind::Unknown;
  if (provenance.resource != schedule::ComputeResourceKind::Unknown) {
    if (provenance.descriptor)
      target = schedule::ComputeTargetKind::Descriptor;
    else if (provenance.formal)
      target = schedule::ComputeTargetKind::Formal;
    else if (provenance.resource == schedule::ComputeResourceKind::Local)
      target = schedule::ComputeTargetKind::Local;
  }
  return schedule::ComputeEffectAttr::get(
      context, effect.kind, provenance.resource, target,
      provenance.descriptor.value_or(0), provenance.formal.value_or(0),
      provenance.low, provenance.width, provenance.dynamic, effect.deferred,
      effect.trigger);
}

ArrayAttr getEffectArrayAttr(Builder &builder,
                             ArrayRef<ComputeEffect> effects) {
  SmallVector<Attribute> attributes;
  for (const ComputeEffect &effect : effects)
    attributes.push_back(getEffectAttr(builder.getContext(), effect));
  return builder.getArrayAttr(attributes);
}

struct IndexedEffect {
  unsigned owner;
  const ComputeEffect *effect;
};

/// Index effects by resource class and concrete descriptor. Formal and unknown
/// handles stay in explicit wildcard buckets, so common closed-world RTL does
/// not require comparing every fragment pair.
class EffectIndex {
public:
  void add(unsigned owner, const ComputeEffect &effect) {
    IndexedEffect indexed{owner, &effect};
    all.push_back(indexed);
    if (effect.target.resource == schedule::ComputeResourceKind::Unknown) {
      unknown.push_back(indexed);
      return;
    }
    unsigned kind = static_cast<unsigned>(effect.target.resource);
    byKind[kind].push_back(indexed);
    if (effect.target.descriptor)
      exact[{kind, *effect.target.descriptor}].push_back(indexed);
    else
      wildcard[kind].push_back(indexed);
  }

  template <typename Callback>
  void forEachAlias(const HandleFact &target, Callback &&callback) const {
    auto visit = [&](ArrayRef<IndexedEffect> effects) {
      for (IndexedEffect effect : effects)
        callback(effect);
    };
    if (target.resource == schedule::ComputeResourceKind::Unknown) {
      visit(all);
      return;
    }
    unsigned kind = static_cast<unsigned>(target.resource);
    visit(unknown);
    if (!target.descriptor) {
      if (auto found = byKind.find(kind); found != byKind.end())
        visit(found->second);
      return;
    }
    if (auto found = wildcard.find(kind); found != wildcard.end())
      visit(found->second);
    if (auto found = exact.find({kind, *target.descriptor});
        found != exact.end())
      visit(found->second);
  }

private:
  SmallVector<IndexedEffect> all;
  SmallVector<IndexedEffect> unknown;
  // Buckets are only ever looked up, never iterated, so nothing depends on
  // key order. Resource kinds are small non-negative enumerators and cannot
  // collide with the reserved empty and tombstone keys.
  DenseMap<unsigned, SmallVector<IndexedEffect>> byKind;
  DenseMap<unsigned, SmallVector<IndexedEffect>> wildcard;
  DenseMap<std::pair<unsigned, uint64_t>, SmallVector<IndexedEffect>> exact;
};

//===----------------------------------------------------------------------===//
// Descriptor SSA facts
//===----------------------------------------------------------------------===//

struct FunctionInfo {
  explicit FunctionInfo(sim::SimFuncOp function) : function(function) {}
  /// Op handles have value semantics, so hand out a mutable copy: a const
  /// analysis reference still needs to query the IR it describes.
  sim::SimFuncOp getFunction() const { return function; }

  sim::SimFuncOp function;
  DenseMap<Value, HandleFact> provenance;
  SmallVector<ComputeEffect> baseEffects;
  SmallVector<ComputeEffect> summary;
  SmallVector<sim::SimCallOp> calls;
  SmallVector<sim::SimTaskCallOp> taskCalls;
  SmallVector<sim::SimObserverBindOp> observerBindings;
  /// Functions this one starts as an independent process, directly or through
  /// a zero-time call. Indices into `ProgramAnalysis::functions`.
  SmallVector<unsigned> spawns;
};

schedule::ComputeTriggerKind getTriggerKind(sim::EdgeKind edge) {
  switch (edge) {
  case sim::EdgeKind::Change:
    return schedule::ComputeTriggerKind::Change;
  case sim::EdgeKind::Posedge:
    return schedule::ComputeTriggerKind::Posedge;
  case sim::EdgeKind::Negedge:
    return schedule::ComputeTriggerKind::Negedge;
  case sim::EdgeKind::Both:
    return schedule::ComputeTriggerKind::Both;
  }
  llvm_unreachable("unknown sensitivity edge");
}

schedule::ComputeTriggerKind getClockTriggerKind(int32_t edge) {
  // IEEE 1800-2017 31.5 custom descriptors name exact four-state transition
  // classes. The AOT graph must conservatively publish every value change;
  // the feature-local clock subscription classifies old/new values exactly.
  if ((edge & ~0x3f) == 0x100 && (edge & 0x3f) != 0)
    return schedule::ComputeTriggerKind::Change;
  return getTriggerKind(static_cast<sim::EdgeKind>(edge));
}

void appendEffect(
    const FunctionInfo &info, schedule::ComputeEffectKind kind, Value handle,
    SmallVectorImpl<ComputeEffect> &effects,
    schedule::ComputeTriggerKind trigger = schedule::ComputeTriggerKind::None,
    bool deferred = false) {
  auto provenance = info.provenance.find(handle);
  if (provenance == info.provenance.end()) {
    effects.push_back({kind, HandleFact{}, trigger, deferred});
    return;
  }
  // Process-local allocations are never shared scheduling resources.
  if (provenance->second.resource != schedule::ComputeResourceKind::Local)
    effects.push_back({kind, provenance->second, trigger, deferred});
}

SmallVector<ComputeEffect> collectDirectEffects(const FunctionInfo &info,
                                                Block *onlyBlock = nullptr) {
  SmallVector<ComputeEffect> effects;
  auto visit = [&](Operation *operation) {
    llvm::TypeSwitch<Operation *>(operation)
        .Case<sim::SimRefLoadOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Read,
                       op.getReference(), effects);
        })
        .Case<sim::SimRefStoreOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Write,
                       op.getReference(), effects);
        })
        .Case<sim::SimNetReadOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Read, op.getNet(),
                       effects);
        })
        .Case<sim::SimDriverDriveOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Drive, op.getDriver(),
                       effects);
        })
        .Case<sim::SimDriverDriveChangedOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Drive, op.getDriver(),
                       effects);
        })
        .Case<sim::SimNBAEnqueueOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::NBA,
                       op.getDestination(), effects,
                       schedule::ComputeTriggerKind::None,
                       static_cast<bool>(op.getDelay()));
        })
        .Case<sim::SimSuspendChangeOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Watch,
                       op.getWatched(), effects,
                       schedule::ComputeTriggerKind::Change);
        })
        .Case<sim::SimSuspendEdgeOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Watch,
                       op.getWatched(), effects, getTriggerKind(op.getEdge()));
        })
        .Case<sim::SimSuspendEdgeIffOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Watch,
                       op.getWatched(), effects, getTriggerKind(op.getEdge()));
          appendEffect(info, schedule::ComputeEffectKind::Read,
                       op.getCondition(), effects);
        })
        .Case<sim::SimSuspendLevelOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Watch,
                       op.getWatched(), effects,
                       schedule::ComputeTriggerKind::Change);
          appendEffect(info, schedule::ComputeEffectKind::Read, op.getWatched(),
                       effects);
        })
        .Case<sim::SimSuspendAnyOp>([&](auto op) {
          for (auto [watched, edge] : llvm::zip(op.getWatched(), op.getEdges()))
            appendEffect(info, schedule::ComputeEffectKind::Watch, watched,
                         effects,
                         getTriggerKind(static_cast<sim::EdgeKind>(edge)));
        })
        .Case<sim::SimSuspendClockSetOp>([&](auto op) {
          for (auto [watched, edge] :
               llvm::zip(op.getPrimaries(), op.getEdges()))
            appendEffect(info, schedule::ComputeEffectKind::Watch, watched,
                         effects, getClockTriggerKind(edge));
          for (Value condition : op.getConditions()) {
            if (auto binding =
                    condition.getDefiningOp<sim::SimObserverBindOp>()) {
              for (Value dependency : binding.getDependencies())
                appendEffect(info, schedule::ComputeEffectKind::Read,
                             dependency, effects);
              continue;
            }
            appendEffect(info, schedule::ComputeEffectKind::Read, condition,
                         effects);
          }
        })
        .Case<sim::SimCovergroupClockEventRegisterOp>([&](auto op) {
          for (auto [watched, edge] :
               llvm::zip(op.getPrimaries(), op.getEdges()))
            appendEffect(info, schedule::ComputeEffectKind::Watch, watched,
                         effects,
                         getTriggerKind(static_cast<sim::EdgeKind>(edge)));
          for (Value condition : op.getConditions())
            appendEffect(info, schedule::ComputeEffectKind::Read, condition,
                         effects);
        })
        .Case<sim::SimSuspendObserveOp>([&](auto op) {
          for (Value observerValue : op.getPrimaries()) {
            auto binding =
                observerValue.getDefiningOp<sim::SimObserverBindOp>();
            if (!binding)
              continue;
            for (Value dependency : binding.getDependencies())
              appendEffect(info, schedule::ComputeEffectKind::Watch, dependency,
                           effects,
                           isa<sim::EventType>(dependency.getType())
                               ? schedule::ComputeTriggerKind::Event
                               : schedule::ComputeTriggerKind::Change);
          }
          for (Value observerValue : op.getConditions()) {
            auto binding =
                observerValue.getDefiningOp<sim::SimObserverBindOp>();
            if (!binding)
              continue;
            for (Value dependency : binding.getDependencies())
              appendEffect(info, schedule::ComputeEffectKind::Read, dependency,
                           effects);
          }
        })
        .Case<sim::SimSuspendEventOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Watch, op.getEvent(),
                       effects, schedule::ComputeTriggerKind::Event);
        })
        .Case<sim::SimSuspendEventOrderOp>([&](auto op) {
          for (Value event : op.getEvents())
            appendEffect(info, schedule::ComputeEffectKind::Watch, event,
                         effects, schedule::ComputeTriggerKind::Event);
        })
        .Case<sim::SimEventTriggerOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Trigger,
                       op.getEvent(), effects,
                       schedule::ComputeTriggerKind::None, op.getNonblocking());
        })
        .Case<sim::SimEventTriggeredOp>([&](auto op) {
          appendEffect(info, schedule::ComputeEffectKind::Read, op.getEvent(),
                       effects);
        });
  };
  if (onlyBlock)
    for (Operation &operation : *onlyBlock)
      visit(&operation);
  else
    info.getFunction().walk([&](Operation *operation) { visit(operation); });
  normalizeEffects(effects);
  return effects;
}

/// Rewrite a callee effect on formal `n` into the caller's own address space.
ComputeEffect substituteEffect(const ComputeEffect &effect, sim::SimCallOp call,
                               const FunctionInfo &caller) {
  if (!effect.target.formal)
    return effect;
  // A site inside a shared zero-time function has one stable fragment ABI
  // identity, independent of which callers reach it. Until call-graph
  // specialization clones that site, a staged update through a callee formal
  // must therefore use the unknown-root frontier commit. Specializing only the
  // summary would create a concrete commit with no site while the operation
  // itself continued to name the unknown commit.
  if (effect.kind == schedule::ComputeEffectKind::NBA ||
      (effect.kind == schedule::ComputeEffectKind::Trigger && effect.deferred))
    return {effect.kind, HandleFact{}, effect.trigger, effect.deferred};
  unsigned index = *effect.target.formal;
  if (index >= call.getNumOperands())
    return {effect.kind, HandleFact{}, effect.trigger, effect.deferred};
  auto actual = caller.provenance.find(call.getOperand(index));
  if (actual == caller.provenance.end())
    return {effect.kind, HandleFact{}, effect.trigger, effect.deferred};
  HandleFact target = effect.target.dynamic || actual->second.dynamic
                          ? widenDynamic(actual->second)
                          : narrowProvenance(actual->second, effect.target.low,
                                             effect.target.width);
  return {effect.kind, target, effect.trigger, effect.deferred};
}

/// Rewrite an observer evaluator effect into the waiting process's address
/// space. Observer formal zero is the implicit context; serialized captures
/// begin at formal one.
ComputeEffect substituteObserverEffect(const ComputeEffect &effect,
                                       sim::SimObserverBindOp binding,
                                       const FunctionInfo &waiter) {
  if (!effect.target.formal)
    return effect;
  if (effect.kind == schedule::ComputeEffectKind::NBA ||
      (effect.kind == schedule::ComputeEffectKind::Trigger && effect.deferred))
    return {effect.kind, HandleFact{}, effect.trigger, effect.deferred};
  unsigned formal = *effect.target.formal;
  if (formal == 0 || formal - 1 >= binding.getCaptures().size())
    return {effect.kind, HandleFact{}, effect.trigger, effect.deferred};
  auto actual = waiter.provenance.find(binding.getCaptures()[formal - 1]);
  if (actual == waiter.provenance.end())
    return {effect.kind, HandleFact{}, effect.trigger, effect.deferred};
  HandleFact target = effect.target.dynamic || actual->second.dynamic
                          ? widenDynamic(actual->second)
                          : narrowProvenance(actual->second, effect.target.low,
                                             effect.target.width);
  return {effect.kind, target, effect.trigger, effect.deferred};
}

struct ProgramAnalysis {
  explicit ProgramAnalysis(sim::SimDesignOp design)
      : classDispatch(design), connectivity(design),
        descriptorProvenance(design) {}

  ::obelisk::analysis::ClassDispatchAnalysis classDispatch;
  ::obelisk::analysis::NetConnectivityAnalysis connectivity;
  ::obelisk::analysis::HandleDataflowAnalysis descriptorProvenance;
  SmallVector<FunctionInfo, 0> functions;
  llvm::StringMap<unsigned> functionIndex;
  DenseMap<Operation *, unsigned> indexForFunction;

  const FunctionInfo &operator[](sim::SimFuncOp function) const {
    return functions[indexForFunction.lookup(function.getOperation())];
  }

  void expandConnectivity(SmallVectorImpl<ComputeEffect> &effects) const {
    SmallVector<ComputeEffect> expanded;
    DenseMap<unsigned, DenseSet<std::pair<uint64_t, uint64_t>>>
        expandedComponents;
    for (const ComputeEffect &effect : effects) {
      if (effect.target.resource != schedule::ComputeResourceKind::Net ||
          !effect.target.descriptor || effect.target.width == 0 ||
          !isActiveProducer(effect)) {
        expanded.push_back(effect);
        continue;
      }
      // Expand publications, not subscriptions. A producer has to publish
      // every logical descriptor in a connected component so the generated
      // runtime transition ABI remains exact. A read or watch only needs the
      // descriptor it names: every matching producer is expanded to that
      // descriptor already. Expanding both sides makes a component with N
      // port-connected clock consumers carry N effects in each consumer and
      // turns summary construction quadratic without adding an edge. The
      // component set also makes expansion idempotent when a callee summary
      // already contains all producer aliases.
      uint64_t begin = effect.target.dynamic ? 0 : effect.target.low;
      uint64_t width =
          effect.target.dynamic ? effect.target.rootWidth : effect.target.width;
      for (uint64_t bit = 0; bit != width; ++bit) {
        ::obelisk::analysis::NetBit source{*effect.target.descriptor,
                                           begin + bit};
        ArrayRef<::obelisk::analysis::NetBit> component =
            connectivity.getComponent(source);
        ::obelisk::analysis::NetBit canonical =
            component.empty() ? source : component.front();
        auto &seen = expandedComponents[static_cast<unsigned>(effect.kind)];
        if (!seen.insert({canonical.net, canonical.offset}).second)
          continue;
        if (component.empty())
          component = ArrayRef(source);
        for (::obelisk::analysis::NetBit member : component) {
          std::optional<uint64_t> rootWidth =
              connectivity.getNetWidth(member.net);
          if (!rootWidth)
            continue;
          ComputeEffect alias = effect;
          alias.target.descriptor = member.net;
          alias.target.formal.reset();
          alias.target.low = member.offset;
          alias.target.width = 1;
          alias.target.rootWidth = *rootWidth;
          alias.target.dynamic = false;
          expanded.push_back(alias);
        }
      }
    }
    effects.assign(expanded.begin(), expanded.end());
    normalizeEffects(effects);
  }
};

ProgramAnalysis analyzeProgram(sim::SimDesignOp design) {
  ProgramAnalysis analysis(design);

  SmallVector<sim::SimFuncOp> functions(
      design.getBody().front().getOps<sim::SimFuncOp>());
  llvm::sort(functions, [](sim::SimFuncOp lhs, sim::SimFuncOp rhs) {
    return lhs.getSymName() < rhs.getSymName();
  });
  for (sim::SimFuncOp function : functions) {
    analysis.functionIndex[function.getSymName()] = analysis.functions.size();
    analysis.indexForFunction[function.getOperation()] =
        analysis.functions.size();
    analysis.functions.emplace_back(function);
    FunctionInfo &info = analysis.functions.back();
    if (function.getBody().empty()) {
      // A declaration may touch anything.
      info.baseEffects = {{schedule::ComputeEffectKind::Read},
                          {schedule::ComputeEffectKind::Write}};
    } else {
      info.provenance = analysis.descriptorProvenance.derive(function);
      info.baseEffects = collectDirectEffects(info);
      analysis.expandConnectivity(info.baseEffects);
    }
    info.summary = info.baseEffects;
    function.walk([&](sim::SimCallOp call) { info.calls.push_back(call); });
    function.walk(
        [&](sim::SimTaskCallOp call) { info.taskCalls.push_back(call); });
    function.walk([&](sim::SimSuspendObserveOp observe) {
      llvm::SetVector<Operation *> bindings;
      for (Value observer : observe.getPrimaries())
        if (auto binding = observer.getDefiningOp<sim::SimObserverBindOp>())
          bindings.insert(binding);
      for (Value observer : observe.getConditions())
        if (auto binding = observer.getDefiningOp<sim::SimObserverBindOp>())
          bindings.insert(binding);
      for (Operation *binding : bindings)
        info.observerBindings.push_back(cast<sim::SimObserverBindOp>(binding));
    });
    function.walk([&](sim::SimSuspendClockSetOp clocks) {
      for (Value condition : clocks.getConditions())
        if (auto binding = condition.getDefiningOp<sim::SimObserverBindOp>())
          info.observerBindings.push_back(binding);
    });
  }

  SmallVector<SmallVector<unsigned>> callsFrom(analysis.functions.size());
  for (unsigned caller = 0; caller != analysis.functions.size(); ++caller) {
    for (sim::SimCallOp call : analysis.functions[caller].calls) {
      auto callee = analysis.functionIndex.find(call.getCallee());
      if (callee != analysis.functionIndex.end())
        callsFrom[caller].push_back(callee->second);
    }
    for (sim::SimObserverBindOp binding :
         analysis.functions[caller].observerBindings) {
      auto evaluator = analysis.functionIndex.find(binding.getEvaluator());
      if (evaluator != analysis.functionIndex.end())
        callsFrom[caller].push_back(evaluator->second);
    }
  }

  SmallVector<unsigned> callNodes;
  DenseMap<unsigned, SmallVector<unsigned>> callAdjacency;
  for (unsigned function = 0; function != analysis.functions.size();
       ++function) {
    callNodes.push_back(function);
    llvm::sort(callsFrom[function]);
    callsFrom[function].erase(
        std::unique(callsFrom[function].begin(), callsFrom[function].end()),
        callsFrom[function].end());
    callAdjacency.try_emplace(function, callsFrom[function]);
  }
  SmallVector<SmallVector<unsigned>> callComponents =
      computeStronglyConnectedComponents<unsigned>(callNodes, callAdjacency);
  SmallVector<int> callComponent(analysis.functions.size(), -1);
  for (auto [component, members] : llvm::enumerate(callComponents))
    for (unsigned member : members)
      callComponent[member] = static_cast<int>(component);

  // Publish summaries in callee-before-caller order over the SCC condensation
  // graph. The old whole-program fixed point revisited and re-sorted every
  // function for every level of a long call chain, which made class-heavy UVM
  // designs quadratic. Only a genuinely recursive component needs iteration.
  // Calls within such a component and unresolved external calls
  // conservatively touch all state. Spawn sets remain an exact finite-symbol
  // fixed point inside the component.
  SmallVector<SmallVector<unsigned>> directSpawns(analysis.functions.size());
  for (unsigned index = 0; index != analysis.functions.size(); ++index)
    analysis.functions[index].function.walk([&](sim::SimSpawnOp spawn) {
      auto callee = analysis.functionIndex.find(spawn.getCallee());
      if (callee != analysis.functionIndex.end())
        directSpawns[index].push_back(callee->second);
    });

  SmallVector<SmallVector<unsigned>> componentDependencies(
      callComponents.size());
  SmallVector<SmallVector<unsigned>> componentCallers(callComponents.size());
  for (unsigned caller = 0; caller != analysis.functions.size(); ++caller) {
    unsigned callerComponent = callComponent[caller];
    for (unsigned callee : callsFrom[caller]) {
      unsigned calleeComponent = callComponent[callee];
      if (callerComponent != calleeComponent)
        componentDependencies[callerComponent].push_back(calleeComponent);
    }
  }
  for (SmallVector<unsigned> &dependencies : componentDependencies) {
    llvm::sort(dependencies);
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                       dependencies.end());
  }
  for (auto [caller, dependencies] : llvm::enumerate(componentDependencies))
    for (unsigned callee : dependencies)
      componentCallers[callee].push_back(static_cast<unsigned>(caller));

  SmallVector<unsigned> pendingDependencies;
  pendingDependencies.reserve(callComponents.size());
  std::priority_queue<unsigned, SmallVector<unsigned>, std::greater<unsigned>>
      ready;
  for (auto [component, dependencies] :
       llvm::enumerate(componentDependencies)) {
    pendingDependencies.push_back(static_cast<unsigned>(dependencies.size()));
    if (dependencies.empty())
      ready.push(static_cast<unsigned>(component));
  }

  unsigned processedComponents = 0;
  while (!ready.empty()) {
    unsigned component = ready.top();
    ready.pop();
    ++processedComponents;

    bool hasInternalDependency = callComponents[component].size() > 1;
    if (!hasInternalDependency) {
      unsigned function = callComponents[component].front();
      hasInternalDependency = llvm::is_contained(callsFrom[function], function);
    }

    bool changed;
    do {
      changed = false;
      for (unsigned caller : callComponents[component]) {
        FunctionInfo &info = analysis.functions[caller];
        SmallVector<ComputeEffect> nextEffects = info.baseEffects;
        SmallVector<unsigned> nextSpawns = directSpawns[caller];
        for (sim::SimCallOp call : info.calls) {
          auto callee = analysis.functionIndex.find(call.getCallee());
          if (callee == analysis.functionIndex.end() ||
              callComponent[callee->second] == callComponent[caller]) {
            nextEffects.push_back({schedule::ComputeEffectKind::Read});
            nextEffects.push_back({schedule::ComputeEffectKind::Write});
            if (callee == analysis.functionIndex.end())
              continue;
          } else {
            for (const ComputeEffect &effect :
                 analysis.functions[callee->second].summary)
              nextEffects.push_back(substituteEffect(effect, call, info));
          }
          llvm::append_range(nextSpawns,
                             analysis.functions[callee->second].spawns);
        }
        for (sim::SimObserverBindOp binding : info.observerBindings) {
          auto evaluator = analysis.functionIndex.find(binding.getEvaluator());
          if (evaluator == analysis.functionIndex.end()) {
            nextEffects.push_back({schedule::ComputeEffectKind::Read});
            nextEffects.push_back({schedule::ComputeEffectKind::Write});
            continue;
          }
          for (const ComputeEffect &effect :
               analysis.functions[evaluator->second].summary)
            nextEffects.push_back(
                substituteObserverEffect(effect, binding, info));
          llvm::append_range(nextSpawns,
                             analysis.functions[evaluator->second].spawns);
        }
        analysis.expandConnectivity(nextEffects);
        normalizeEffects(nextEffects);
        llvm::sort(nextSpawns);
        nextSpawns.erase(std::unique(nextSpawns.begin(), nextSpawns.end()),
                         nextSpawns.end());
        if (nextEffects != info.summary) {
          info.summary = std::move(nextEffects);
          changed = true;
        }
        if (nextSpawns != info.spawns) {
          info.spawns = std::move(nextSpawns);
          changed = true;
        }
      }
    } while (hasInternalDependency && changed);

    for (unsigned caller : componentCallers[component])
      if (--pendingDependencies[caller] == 0)
        ready.push(caller);
  }
  assert(processedComponents == callComponents.size() &&
         "SCC condensation graph must be acyclic");
  return analysis;
}

SmallVector<ComputeEffect>
collectFragmentEffects(const ProgramAnalysis &analysis,
                       const FunctionInfo &info, Block &block) {
  SmallVector<ComputeEffect> effects = collectDirectEffects(info, &block);
  for (sim::SimCallOp call : block.getOps<sim::SimCallOp>()) {
    auto callee = analysis.functionIndex.find(call.getCallee());
    if (callee == analysis.functionIndex.end()) {
      effects.push_back({schedule::ComputeEffectKind::Read});
      effects.push_back({schedule::ComputeEffectKind::Write});
      continue;
    }
    for (const ComputeEffect &effect :
         analysis.functions[callee->second].summary)
      effects.push_back(substituteEffect(effect, call, info));
  }
  for (sim::SimSuspendObserveOp observe :
       block.getOps<sim::SimSuspendObserveOp>()) {
    llvm::SetVector<Operation *> bindings;
    for (Value observer : observe.getPrimaries())
      if (auto binding = observer.getDefiningOp<sim::SimObserverBindOp>())
        bindings.insert(binding);
    for (Value observer : observe.getConditions())
      if (auto binding = observer.getDefiningOp<sim::SimObserverBindOp>())
        bindings.insert(binding);
    for (Operation *operation : bindings) {
      auto binding = cast<sim::SimObserverBindOp>(operation);
      auto evaluator = analysis.functionIndex.find(binding.getEvaluator());
      if (evaluator == analysis.functionIndex.end()) {
        effects.push_back({schedule::ComputeEffectKind::Read});
        effects.push_back({schedule::ComputeEffectKind::Write});
        continue;
      }
      for (const ComputeEffect &effect :
           analysis.functions[evaluator->second].summary)
        effects.push_back(substituteObserverEffect(effect, binding, info));
    }
  }
  analysis.expandConnectivity(effects);
  normalizeEffects(effects);
  return effects;
}

/// A fragment is two-state when no four-state value it produces or consumes can
/// hold X or Z. Continuation operands are excluded: they are the frame the
/// *next* fragment receives, and that fragment proves them itself.
bool fragmentIsTwoState(const StateDomainAnalysis &stateDomains, Block &block) {
  auto containsFourStateLeaf = [&](Type type) {
    std::function<bool(Type)> visit = [&](Type nested) {
      if (isa<sim::LogicType>(nested))
        return true;
      if (!sim::isAggregateType(nested))
        return false;
      for (unsigned index = 0; index < sim::getAggregateNumElements(nested);
           ++index)
        if (visit(sim::getAggregateElementType(nested, index)))
          return true;
      return false;
    };
    return visit(type);
  };
  for (BlockArgument argument : block.getArguments())
    if (containsFourStateLeaf(argument.getType()) &&
        !stateDomains.isTwoStateWithInductiveRoots(argument))
      return false;
  // Only the suspension terminator gets to pass an unproven value along: it
  // merely forwards the frame, and the resuming fragment proves it there. Any
  // other operation that *uses* the same value really does consume four-state
  // data, so the exemption must be scoped to the terminator alone.
  DenseSet<Value> forwarded;
  for (Operation &operation : block) {
    forwarded.clear();
    if (isSuspensionTerminator(&operation)) {
      auto branch = cast<BranchOpInterface>(&operation);
      for (unsigned successor = 0; successor != operation.getNumSuccessors();
           ++successor)
        for (Value value :
             branch.getSuccessorOperands(successor).getForwardedOperands())
          forwarded.insert(value);
    }
    for (Value operand : operation.getOperands())
      if (!forwarded.contains(operand) &&
          containsFourStateLeaf(operand.getType()) &&
          !stateDomains.isTwoStateWithInductiveRoots(operand))
        return false;
    for (Value result : operation.getResults())
      if (containsFourStateLeaf(result.getType()) &&
          !stateDomains.isTwoStateWithInductiveRoots(result))
        return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Process multiplicity
//===----------------------------------------------------------------------===//

/// Which processes may run more than once, and which blocks may re-execute.
/// Both facts decide whether a compiled site can own a fixed slot.
class SpawnMultiplicity {
public:
  explicit SpawnMultiplicity(const ProgramAnalysis &analysis) {
    for (const FunctionInfo &info : analysis.functions)
      reexecuting.try_emplace(info.getFunction().getOperation(),
                              getReexecutingBlocks(info.getFunction()));
    DenseMap<Operation *, unsigned> staticSpawnCounts;
    for (const FunctionInfo &info : analysis.functions) {
      sim::SimFuncOp function = info.getFunction();
      function.walk([&](sim::SimSpawnOp spawn) {
        auto callee = analysis.functionIndex.find(spawn.getCallee());
        if (callee == analysis.functionIndex.end())
          return;
        Operation *target =
            analysis.functions[callee->second].getFunction().getOperation();
        // Only a spawn executed exactly once from the root initializer bounds
        // its target to a single instance. A spawn reached through a zero-time
        // call has no such bound: the call may itself repeat.
        if (function.getEntryKind() != sim::EntryKind::RootInitializer ||
            spawn->getParentOfType<sim::SimFuncOp>() != function ||
            mayReexecute(function, spawn->getBlock())) {
          dynamic.insert(target);
          return;
        }
        if (++staticSpawnCounts[target] > 1)
          dynamic.insert(target);
      });
    }
  }

  bool mayReexecute(sim::SimFuncOp function, Block *block) const {
    auto found = reexecuting.find(function.getOperation());
    return found != reexecuting.end() && found->second.contains(block);
  }
  bool isDynamicallySpawned(sim::SimFuncOp function) const {
    return dynamic.contains(function.getOperation());
  }

private:
  DenseMap<Operation *, ReexecutingBlockSet> reexecuting;
  llvm::SmallDenseSet<Operation *> dynamic;
};

/// Where the compiler stages one nonblocking update.
///
/// A fixed entry is sound only when the site executes at most once over the
/// process lifetime. Repeated immediate assignments to one known root use
/// generated value/unknown/mask and transition accumulators; only delayed or
/// dynamically rooted multiplicity needs the frontier. Full VPI can rewrite a
/// visible object between staging and commit, so repeated sites cannot use the
/// root accumulator and must use the frontier instead.
schedule::ComputeNBAStorageKind
getNBAStorageKind(sim::SimNBAEnqueueOp nba, sim::SimFuncOp function,
                  const SpawnMultiplicity &multiplicity,
                  const HandleFact &destination) {
  bool fixed = function.getEntryKind() != sim::EntryKind::Function &&
               !multiplicity.isDynamicallySpawned(function) &&
               !multiplicity.mayReexecute(function, nba->getBlock());
  if (fixed)
    return schedule::ComputeNBAStorageKind::FixedSlot;
  // Writable VPI deposits are admitted only at scheduler boundaries. Native
  // AOT actors temporarily execute through bytecode until that slot
  // quiesces, so a deposit cannot interleave between accumulator staging and
  // commit. DPI reentrancy remains independently ineligible for AOT.
  if (!nba.getDelay() && destination.descriptor)
    return schedule::ComputeNBAStorageKind::RootAccumulator;
  return schedule::ComputeNBAStorageKind::DynamicFrontier;
}

//===----------------------------------------------------------------------===//
// Graph shape
//===----------------------------------------------------------------------===//

/// Edges that constrain evaluation order *within* one activation of an event
/// region. Resume leaves the region by definition, and a spawn starts a fresh
/// actor rather than feeding a value back into the current one, so neither can
/// make a group cyclic.
bool isSchedulingEdge(schedule::ComputeEdgeKind kind) {
  return kind != schedule::ComputeEdgeKind::Resume &&
         kind != schedule::ComputeEdgeKind::Spawn;
}

using analysis::isSettlingEntryKind;

void normalizeEdges(SmallVectorImpl<schedule::ComputeEdgeAttr> &edges) {
  auto key = [](schedule::ComputeEdgeAttr edge) {
    auto resource = edge.getResource();
    return std::tuple<uint32_t, uint32_t, unsigned, unsigned, unsigned,
                      unsigned, uint64_t, uint32_t, uint64_t, uint64_t, bool,
                      bool, unsigned>(
        edge.getSource(), edge.getTarget(),
        static_cast<unsigned>(edge.getKind()), resource ? 1u : 0u,
        resource ? static_cast<unsigned>(resource.getEffect()) : 0u,
        resource ? static_cast<unsigned>(resource.getResource()) : 0u,
        resource ? resource.getDescriptor() : 0,
        resource ? resource.getFormal() : 0, resource ? resource.getLow() : 0,
        resource ? resource.getWidth() : 0,
        resource ? resource.getDynamic() : false,
        resource ? resource.getDeferred() : false,
        resource ? static_cast<unsigned>(resource.getTrigger()) : 0u);
  };
  using Key = decltype(key(schedule::ComputeEdgeAttr{}));
  SmallVector<std::pair<Key, schedule::ComputeEdgeAttr>> keyedEdges;
  keyedEdges.reserve(edges.size());
  for (schedule::ComputeEdgeAttr edge : edges)
    keyedEdges.emplace_back(key(edge), edge);
  llvm::sort(keyedEdges, [](const auto &lhs, const auto &rhs) {
    return lhs.first < rhs.first;
  });
  for (auto [index, entry] : llvm::enumerate(keyedEdges))
    edges[index] = entry.second;
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
}

/// Whether the source-order edges inside one group form a cycle. A cyclic
/// group of procedural fragments is a loop to execute, not a value to converge.
bool hasProceduralControlCycle(
    ArrayRef<uint32_t> group, ArrayRef<schedule::ComputeEdgeAttr> edges,
    const DenseSet<std::pair<uint32_t, uint32_t>> &boundedBackedges) {
  DenseSet<uint32_t> members(group.begin(), group.end());
  DenseMap<uint32_t, SmallVector<uint32_t>> successors;
  DenseMap<uint32_t, unsigned> indegree;
  for (uint32_t member : group)
    indegree.try_emplace(member, 0);
  for (schedule::ComputeEdgeAttr edge : edges) {
    if (edge.getKind() != schedule::ComputeEdgeKind::ProcessOrder ||
        !members.contains(edge.getSource()) ||
        !members.contains(edge.getTarget()))
      continue;
    // A loop with a proven finite exit is not the IEEE 1800-2023 12.7.6 hazard
    // of a loop that can hang the scheduler. Unrolling such a loop would have
    // deleted this backedge outright; skipping it here reaches the same
    // classification without paying the replicated body.
    if (boundedBackedges.contains({edge.getSource(), edge.getTarget()}))
      continue;
    successors[edge.getSource()].push_back(edge.getTarget());
    ++indegree[edge.getTarget()];
  }
  SmallVector<uint32_t> ready;
  for (uint32_t member : group)
    if (indegree[member] == 0)
      ready.push_back(member);
  size_t visited = 0;
  while (!ready.empty()) {
    uint32_t member = ready.pop_back_val();
    ++visited;
    for (uint32_t successor : successors[member])
      if (--indegree[successor] == 0)
        ready.push_back(successor);
  }
  return visited != group.size();
}

/// Condensation of one event region, in a deterministic topological order.
SmallVector<SmallVector<uint32_t>>
computeSCCSchedule(ArrayRef<uint32_t> nodes,
                   ArrayRef<schedule::ComputeEdgeAttr> edges,
                   llvm::function_ref<unsigned(uint32_t)> priority = {},
                   ArrayRef<analysis::StartupPhase> startupPhases = {}) {
  DenseMap<uint32_t, SmallVector<uint32_t>> adjacency;
  DenseSet<uint32_t> nodeSet(nodes.begin(), nodes.end());
  for (schedule::ComputeEdgeAttr edge : edges)
    if (isSchedulingEdge(edge.getKind()) && nodeSet.count(edge.getSource()) &&
        nodeSet.count(edge.getTarget()))
      adjacency[edge.getSource()].push_back(edge.getTarget());
  // Virtual nodes exist only during scheduling. Drain a barrier as soon as
  // it becomes ready, so it cannot perturb the real components' tie-breaks.
  SmallVector<uint32_t> allNodes(nodes.begin(), nodes.end());
  uint32_t nextVirtual = nodes.empty() ? 0 : *llvm::max_element(nodes) + 1;
  for (const auto &phase : startupPhases) {
    SmallVector<uint32_t> sources, targets;
    for (uint32_t id : phase.startups)
      if (nodeSet.contains(id))
        sources.push_back(id);
    for (uint32_t id : phase.initials)
      if (nodeSet.contains(id))
        targets.push_back(id);
    if (sources.empty() || targets.empty())
      continue;
    uint32_t barrier = nextVirtual++;
    allNodes.push_back(barrier);
    for (uint32_t source : sources)
      adjacency[source].push_back(barrier);
    adjacency[barrier] = std::move(targets);
  }
  for (auto &entry : adjacency) {
    llvm::sort(entry.second);
    entry.second.erase(std::unique(entry.second.begin(), entry.second.end()),
                       entry.second.end());
  }

  SmallVector<SmallVector<uint32_t>> components =
      computeStronglyConnectedComponents<uint32_t>(allNodes, adjacency);

  DenseMap<uint32_t, unsigned> componentOf;
  for (unsigned component = 0; component != components.size(); ++component)
    for (uint32_t node : components[component])
      componentOf[node] = component;
  // Which successors exist matters; the order they are visited does not. Each
  // one only decrements an indegree, and the components that become ready are
  // re-ordered by the priority queue below.
  SmallVector<DenseSet<unsigned>> successors(components.size());
  SmallVector<unsigned> indegree(components.size());
  for (const auto &[node, targets] : adjacency)
    for (uint32_t targetNode : targets) {
      unsigned source = componentOf[node];
      unsigned target = componentOf[targetNode];
      if (source != target && successors[source].insert(target).second)
        ++indegree[target];
    }
  for (auto &component : components)
    llvm::erase_if(component,
                   [&](uint32_t id) { return !nodeSet.contains(id); });
  // Break ties on the caller's semantic priority and then the lowest member so
  // repeated builds are identical.
  using Ready = std::tuple<bool, unsigned, uint32_t, unsigned>;
  std::priority_queue<Ready, std::vector<Ready>, std::greater<Ready>> ready;
  auto enqueue = [&](unsigned component) {
    unsigned componentPriority = 0;
    if (priority) {
      componentPriority = std::numeric_limits<unsigned>::max();
      for (uint32_t member : components[component])
        componentPriority = std::min(componentPriority, priority(member));
    }
    bool real = !components[component].empty();
    ready.emplace(real, componentPriority,
                  real ? components[component].front() : 0, component);
  };
  for (unsigned component = 0; component != components.size(); ++component)
    if (indegree[component] == 0)
      enqueue(component);
  SmallVector<SmallVector<uint32_t>> schedule;
  while (!ready.empty()) {
    unsigned component = std::get<3>(ready.top());
    ready.pop();
    if (!components[component].empty())
      schedule.push_back(components[component]);
    for (unsigned successor : successors[component])
      if (--indegree[successor] == 0)
        enqueue(successor);
  }
  return schedule;
}

//===----------------------------------------------------------------------===//
// Derivation
//===----------------------------------------------------------------------===//

struct Fragment {
  uint32_t id;
  sim::SimFuncOp function;
  Block *block;
  uint32_t ordinal;
  SmallVector<ComputeEffect> effects;
  uint64_t cost;
  bool twoState;
  uint32_t lane = 0;
};

/// Balance fragments across lanes by descending static cost. This is a
/// placeholder for profile-guided coarsening, but it must stay deterministic.
void assignLanes(MutableArrayRef<Fragment> fragments, uint32_t workers) {
  SmallVector<uint64_t> laneCost(workers);
  SmallVector<unsigned> order(fragments.size());
  for (unsigned index = 0; index != order.size(); ++index)
    order[index] = index;
  llvm::stable_sort(order, [&](unsigned lhs, unsigned rhs) {
    return fragments[lhs].cost > fragments[rhs].cost;
  });
  for (unsigned index : order) {
    unsigned lane =
        std::min_element(laneCost.begin(), laneCost.end()) - laneCost.begin();
    fragments[index].lane = lane;
    laneCost[lane] += fragments[index].cost;
  }
}

class ComputeGraphBuilder {
public:
  ComputeGraphBuilder(sim::SimDesignOp design, ComputeGraphOptions options,
                      const StateDomainAnalysis &stateDomains)
      : design(design), options(options), builder(design.getContext()),
        analysis(analyzeProgram(design)), multiplicity(analysis),
        stateDomains(stateDomains) {}

  FailureOr<ComputeGraphResult> derive();

private:
  /// Commit nodes are keyed by the root range their sites share, so every
  /// slice of one descriptor commits through one ordered journal.
  std::optional<uint32_t> findCommit(ArrayRef<HandleFact> roots,
                                     const HandleFact &root) const;

  schedule::ComputeEffectAttr effectAttr(const ComputeEffect &effect) {
    return getEffectAttr(design.getContext(), effect);
  }
  void addEdge(uint32_t source, uint32_t target, schedule::ComputeEdgeKind kind,
               schedule::ComputeEffectAttr resource = {}) {
    edges.push_back(schedule::ComputeEdgeAttr::get(design.getContext(), source,
                                                   target, kind, resource));
  }

  LogicalResult buildFragments();
  void orderStartupSpawns();
  void buildControlEdges();
  void buildDataEdges();
  SmallVector<schedule::ComputeEdgeAttr> buildSchedulingEdges();
  LogicalResult buildSites(ComputeGraphResult &result);
  FailureOr<ArrayAttr> buildRegions();

  sim::SimDesignOp design;
  ComputeGraphOptions options;
  Builder builder;
  ProgramAnalysis analysis;
  SpawnMultiplicity multiplicity;
  const StateDomainAnalysis &stateDomains;

  SmallVector<Fragment> fragments;
  DenseMap<Block *, uint32_t> fragmentForBlock;
  SmallVector<schedule::ComputeEdgeAttr> edges;
  SmallVector<analysis::StartupPhase> startupPhases;
  EffectIndex watchedEffects;
  /// Process-order edges that close a loop already proven to terminate. They
  /// remain ordinary edges in the graph; only the control-loop classification
  /// skips them, exactly as an unrolled backedge would have been absent.
  DenseSet<std::pair<uint32_t, uint32_t>> boundedBackedges;

  SmallVector<HandleFact> nbaRoots, eventRoots;
  SmallVector<uint32_t> nbaCommitIds, eventCommitIds;
  SmallVector<SmallVector<int64_t>> nbaSlots, nbaAccumulatorSites,
      nbaFrontierSites, eventSites;
};

bool isObserverCaptureBridge(Block *block) {
  if (!block || block->getOperations().size() != 1)
    return false;
  Operation *terminator = block->getTerminator();
  return isa<cf::BranchOp>(terminator) &&
         ::obelisk::schedule::has<
             ::obelisk::schedule::Field::ObserverCaptureBridge>(terminator) &&
         terminator->getNumSuccessors() == 1;
}

Block *skipObserverCaptureBridges(Block *block) {
  llvm::SmallPtrSet<Block *, 4> visited;
  while (isObserverCaptureBridge(block) && visited.insert(block).second)
    block = block->getTerminator()->getSuccessor(0);
  return block;
}

std::optional<uint32_t>
ComputeGraphBuilder::findCommit(ArrayRef<HandleFact> roots,
                                const HandleFact &root) const {
  auto found = llvm::lower_bound(roots, root, provenanceLess);
  if (found == roots.end() || !(*found == root))
    return std::nullopt;
  return static_cast<uint32_t>(found - roots.begin());
}

/// Largest usable graph node ID. Node IDs are keys in dense hash containers,
/// whose top two `uint32_t` values are reserved as the empty and tombstone
/// sentinels, so the ABI stops two short of the type's range.
constexpr uint64_t maxNodeId = std::numeric_limits<uint32_t>::max() - 2;

LogicalResult ComputeGraphBuilder::buildFragments() {
  uint64_t nextId = 0;
  for (const FunctionInfo &info : analysis.functions) {
    // Zero-time functions execute in their caller and contribute a substituted
    // summary there; they are not independently schedulable actors.
    if (info.getFunction().getEntryKind() == sim::EntryKind::Function ||
        info.getFunction().getEntryKind() == sim::EntryKind::Observer)
      continue;
    uint32_t ordinal = 0;
    for (Block &block : info.getFunction().getBody()) {
      if (isObserverCaptureBridge(&block))
        continue;
      if (nextId > maxNodeId)
        return design.emitOpError(
            "compute graph exceeds the 32-bit fragment ABI");
      auto id = static_cast<uint32_t>(nextId++);
      uint64_t cost = 0;
      for (Operation &operation : block)
        cost += ::obelisk::analysis::getSimulationOperationCost(operation);
      fragmentForBlock[&block] = id;
      fragments.push_back({id, info.getFunction(), &block, ordinal++,
                           collectFragmentEffects(analysis, info, block), cost,
                           fragmentIsTwoState(stateDomains, block)});
    }
  }
  assignLanes(fragments, options.workers);

  // Every slice of one root descriptor commits through one node. Roots come
  // from fragment effects (which include substituted callee summaries) and
  // from every staging operation, including those inside zero-time functions
  // whose own root is not what any single caller sees.
  auto addRoot = [](SmallVectorImpl<HandleFact> &roots,
                    const HandleFact &target) {
    roots.push_back(getRootProvenance(target));
  };
  for (Fragment &fragment : fragments)
    for (const ComputeEffect &effect : fragment.effects) {
      if (effect.kind == schedule::ComputeEffectKind::NBA)
        addRoot(nbaRoots, effect.target);
      else if (effect.kind == schedule::ComputeEffectKind::Trigger &&
               effect.deferred)
        addRoot(eventRoots, effect.target);
    }
  for (const FunctionInfo &info : analysis.functions)
    info.getFunction().walk([&](Operation *operation) {
      if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation))
        addRoot(nbaRoots, info.provenance.lookup(nba.getDestination()));
      else if (auto trigger = dyn_cast<sim::SimEventTriggerOp>(operation);
               trigger && trigger.getNonblocking())
        addRoot(eventRoots, info.provenance.lookup(trigger.getEvent()));
    });
  for (SmallVectorImpl<HandleFact> *roots : {&nbaRoots, &eventRoots}) {
    llvm::sort(*roots, provenanceLess);
    roots->erase(std::unique(roots->begin(), roots->end()), roots->end());
  }
  if (nextId + nbaRoots.size() + eventRoots.size() > maxNodeId)
    return design.emitOpError("compute graph exceeds the 32-bit fragment ABI");
  for (size_t index = 0; index != nbaRoots.size(); ++index)
    nbaCommitIds.push_back(static_cast<uint32_t>(nextId++));
  for (size_t index = 0; index != eventRoots.size(); ++index)
    eventCommitIds.push_back(static_cast<uint32_t>(nextId++));
  nbaSlots.resize(nbaRoots.size());
  nbaAccumulatorSites.resize(nbaRoots.size());
  nbaFrontierSites.resize(nbaRoots.size());
  eventSites.resize(eventRoots.size());
  return success();
}

/// Order the time-zero propagation spawns by what they carry.
///
/// IEEE 1800-2017 4.9.1 evaluates every continuous assignment at time zero "in
/// order to propagate constant values", and says the same of "implicit
/// continuous assignments inferred from port connections". The root
/// initializer already spawns those before any initial procedure, but it
/// spawns them in elaboration order, which walks a hierarchy outermost first
/// -- the opposite of the direction a value travels out of a nested instance.
/// One link of such a chain then propagates per time-zero pass, and an initial
/// procedure reading the far end sees a value nothing has produced yet.
///
/// Sorting them so a driver follows whatever writes what it reads makes the
/// whole chain propagate before the readers run. A combinational loop has no
/// such order; its members keep the order they had.
void ComputeGraphBuilder::orderStartupSpawns() {
  sim::SimFuncOp root;
  for (Fragment &fragment : fragments)
    if (fragment.function.getEntryKind() == sim::EntryKind::RootInitializer)
      root = fragment.function;
  if (!root || root.getBody().empty())
    return;
  bool hasComputedEventStartup =
      ::obelisk::schedule::has<sim::computedEventStartupAttrName>(design);

  // What each unit consumes and produces, keyed by the symbol a spawn names.
  struct Flow {
    SmallVector<uint64_t> reads;
    SmallVector<uint64_t> writes;
  };
  llvm::StringMap<Flow> flows;
  for (Fragment &fragment : fragments) {
    sim::EntryKind entryKind = fragment.function.getEntryKind();
    bool eventInput =
        (entryKind == sim::EntryKind::PortInput ||
         entryKind == sim::EntryKind::PortInitialize) &&
        llvm::any_of(fragment.function.getFunctionType().getInputs(),
                     [](Type type) {
                       auto reference = dyn_cast<sim::RefType>(type);
                       return reference &&
                              isa<sim::EventType>(reference.getElementType());
                     });
    // Prepare has already placed cell-backed event inputs in dependency order
    // ahead of event-wait actors. Keep them out of the generic settling-unit
    // regrouping, which would otherwise move them past an intervening wait.
    if (!isSettlingEntryKind(entryKind) || eventInput ||
        (hasComputedEventStartup &&
         ::obelisk::schedule::has<sim::computedEventStartupAttrName>(
             fragment.function)))
      continue;
    Flow &flow = flows[fragment.function.getSymName()];
    for (const ComputeEffect &effect : fragment.effects) {
      HandleFact provenance = getRootProvenance(effect.target);
      if (!provenance.descriptor)
        continue;
      switch (effect.kind) {
      case schedule::ComputeEffectKind::Read:
      case schedule::ComputeEffectKind::Watch:
        flow.reads.push_back(*provenance.descriptor);
        break;
      case schedule::ComputeEffectKind::Write:
      case schedule::ComputeEffectKind::Drive:
        flow.writes.push_back(*provenance.descriptor);
        break;
      default:
        break;
      }
    }
  }

  SmallVector<sim::SimSpawnOp> spawns;
  for (sim::SimSpawnOp spawn : root.getBody().front().getOps<sim::SimSpawnOp>())
    if (flows.contains(spawn.getCallee()))
      spawns.push_back(spawn);
  if (spawns.size() < 2)
    return;

  // Who writes each descriptor, so a consumer finds its producers by looking
  // up what it reads instead of asking every other spawn.
  DenseMap<uint64_t, SmallVector<unsigned>> producersOf;
  for (auto [index, spawn] : llvm::enumerate(spawns))
    for (uint64_t descriptor : flows[spawn.getCallee()].writes)
      producersOf[descriptor].push_back(static_cast<unsigned>(index));

  // Edge count into each spawn from the producers of what it reads. Kahn's
  // algorithm over that count, taking the earliest ready spawn each time, is
  // the original order wherever the flow does not constrain it.
  SmallVector<unsigned> waiting(spawns.size(), 0);
  SmallVector<SmallVector<unsigned>> consumers(spawns.size());
  for (auto [consumer, spawn] : llvm::enumerate(spawns)) {
    DenseSet<unsigned> seen;
    for (uint64_t descriptor : flows[spawn.getCallee()].reads)
      for (unsigned producer : producersOf.lookup(descriptor)) {
        if (producer == consumer || !seen.insert(producer).second)
          continue;
        consumers[producer].push_back(static_cast<unsigned>(consumer));
        ++waiting[consumer];
      }
  }

  std::priority_queue<unsigned, std::vector<unsigned>, std::greater<unsigned>>
      ready;
  for (unsigned index = 0; index != spawns.size(); ++index)
    if (waiting[index] == 0)
      ready.push(index);
  SmallVector<unsigned> order;
  order.reserve(spawns.size());
  SmallVector<bool> placed(spawns.size(), false);
  while (order.size() != spawns.size()) {
    if (ready.empty()) {
      // A loop among the remaining spawns: no order settles it, so keep the
      // one they already have and let the scheduler iterate them.
      for (unsigned index = 0; index != spawns.size(); ++index)
        if (!placed[index])
          ready.push(index);
    }
    unsigned next = ready.top();
    ready.pop();
    if (placed[next])
      continue;
    placed[next] = true;
    order.push_back(next);
    for (unsigned consumer : consumers[next])
      if (--waiting[consumer] == 0 && !placed[consumer])
        ready.push(consumer);
  }
  if (llvm::is_sorted(order))
    return;

  // Every spawn moves to just past where the last of them sat, so each one
  // still follows the operands materialized for it.
  Operation *anchor = spawns.back()->getNextNode();
  for (unsigned index : order)
    spawns[index]->moveBefore(anchor);
}

void ComputeGraphBuilder::buildControlEdges() {
  auto fragmentID = [&](Block *block) {
    return fragmentForBlock.lookup(skipObserverCaptureBridges(block));
  };
  using DispatchKey = std::pair<uint64_t, uint64_t>;
  DenseMap<Operation *, DenseMap<DispatchKey, SmallVector<unsigned>>>
      compatibleTaskImplementations;
  for (Fragment &fragment : fragments) {
    Operation *terminator = fragment.block->getTerminator();
    auto addTaskEdges = [&](unsigned callee, Block *continuationBlock) {
      sim::SimFuncOp target = analysis.functions[callee].function;
      if (target.getBody().empty())
        return;
      addEdge(fragment.id, fragmentID(&target.getBody().front()),
              schedule::ComputeEdgeKind::ProcessOrder);
      uint32_t continuation = fragmentID(continuationBlock);
      for (Block &block : target.getBody())
        if (isa<sim::SimReturnOp>(block.getTerminator()))
          addEdge(fragmentID(&block), continuation,
                  schedule::ComputeEdgeKind::ProcessOrder);
    };
    if (auto taskCall = dyn_cast<sim::SimTaskCallOp>(terminator)) {
      auto callee = analysis.functionIndex.find(taskCall.getCallee());
      if (callee != analysis.functionIndex.end())
        addTaskEdges(callee->second, taskCall.getContinuation());
    } else if (auto taskCall =
                   dyn_cast<sim::SimClassVirtualTaskCallOp>(terminator)) {
      auto staticType =
          cast<sim::ClassHandleType>(taskCall.getReceiver().getType());
      sim::SimClassDeclOp staticClass =
          analysis.classDispatch.lookup(staticType);
      auto key = std::make_pair(taskCall.getSlot(), taskCall.getSignatureId());
      auto &byDispatch =
          compatibleTaskImplementations[staticClass.getOperation()];
      auto implementations = byDispatch.find(key);
      if (implementations == byDispatch.end()) {
        DenseSet<unsigned> unique;
        for (sim::SimClassMethodDeclOp method :
             analysis.classDispatch.compatibleImplementations(
                 staticClass, taskCall.getSlot(), taskCall.getSignatureId(),
                 /*isTask=*/true)) {
          auto callee =
              analysis.functionIndex.find(*method.getImplementation());
          if (callee != analysis.functionIndex.end())
            unique.insert(callee->second);
        }
        SmallVector<unsigned> ordered(unique.begin(), unique.end());
        llvm::sort(ordered);
        implementations = byDispatch.try_emplace(key, std::move(ordered)).first;
      }
      for (unsigned callee : implementations->second)
        addTaskEdges(callee, taskCall.getContinuation());
    } else {
      schedule::ComputeEdgeKind controlKind =
          isSuspensionTerminator(terminator)
              ? schedule::ComputeEdgeKind::Resume
              : schedule::ComputeEdgeKind::ProcessOrder;
      bool boundedLatch =
          controlKind == schedule::ComputeEdgeKind::ProcessOrder &&
          ::obelisk::schedule::has<schedule::metadata::boundedLoopLatch>(
              terminator);
      for (Block *successor : terminator->getSuccessors()) {
        uint32_t target = fragmentID(successor);
        addEdge(fragment.id, target, controlKind);
        if (boundedLatch)
          boundedBackedges.insert({fragment.id, target});
      }
    }

    // A spawn reached through a zero-time call still creates an actor, so the
    // edge is derived from the call graph rather than from this block alone.
    SmallVector<unsigned> spawned;
    for (sim::SimSpawnOp spawn : fragment.block->getOps<sim::SimSpawnOp>()) {
      auto callee = analysis.functionIndex.find(spawn.getCallee());
      if (callee != analysis.functionIndex.end())
        spawned.push_back(callee->second);
    }
    for (sim::SimCallOp call : fragment.block->getOps<sim::SimCallOp>()) {
      auto callee = analysis.functionIndex.find(call.getCallee());
      if (callee != analysis.functionIndex.end())
        llvm::append_range(spawned, analysis.functions[callee->second].spawns);
    }
    llvm::sort(spawned);
    spawned.erase(std::unique(spawned.begin(), spawned.end()), spawned.end());
    for (unsigned callee : spawned) {
      sim::SimFuncOp target = analysis.functions[callee].function;
      if (target.getBody().empty())
        continue;
      addEdge(fragment.id, fragmentForBlock.lookup(&target.getBody().front()),
              schedule::ComputeEdgeKind::Spawn);
    }
  }
  startupPhases = analysis::collectStartupPhases(
      design, [&](sim::SimFuncOp function) -> std::optional<uint32_t> {
        auto found = fragmentForBlock.find(&function.getBody().front());
        if (found == fragmentForBlock.end())
          return std::nullopt;
        return found->second;
      });
}

void ComputeGraphBuilder::buildDataEdges() {
  for (unsigned fragment = 0; fragment != fragments.size(); ++fragment)
    for (const ComputeEffect &effect : fragments[fragment].effects)
      if (effect.kind == schedule::ComputeEffectKind::Watch)
        watchedEffects.add(fragment, effect);

  // Static producers directly activate matching sensitivity consumers.
  for (Fragment &producer : fragments)
    for (const ComputeEffect &produced : producer.effects) {
      if (!isActiveProducer(produced))
        continue;
      watchedEffects.forEachAlias(produced.target, [&](IndexedEffect consumed) {
        Fragment &consumer = fragments[consumed.owner];
        // A nested procedural control might not have been reached when an
        // earlier statement writes its operand, and an implicit wildcard
        // excludes its controlled write from activating the inferred wait.
        // An outer explicit always control repeats continuously, so an event
        // enabled by one iteration can enqueue the next iteration.
        if (producer.function == consumer.function &&
            (::obelisk::schedule::has<schedule::metadata::topLevelWildcardWait>(
                 consumer.block->getTerminator()) ||
             ::obelisk::schedule::has<schedule::metadata::proceduralEventWait>(
                 consumer.block->getTerminator())) &&
            !::obelisk::schedule::has<schedule::metadata::repeatingAlwaysWait>(
                consumer.block->getTerminator()))
          return;
        if (provenancesAlias(produced.target, consumed.effect->target))
          addEdge(producer.id, consumer.id,
                  schedule::ComputeEdgeKind::Sensitivity,
                  effectAttr(*consumed.effect));
      });
    }

  // Select one stable ordering for conflicting active-region producers. This
  // is a legal SystemVerilog interleaving and makes repeated builds identical.
  EffectIndex activeEffects;
  for (unsigned fragment = 0; fragment != fragments.size(); ++fragment)
    for (const ComputeEffect &effect : fragments[fragment].effects)
      if (effect.kind != schedule::ComputeEffectKind::Watch &&
          effect.kind != schedule::ComputeEffectKind::NBA)
        activeEffects.add(fragment, effect);
  auto settles = [&](uint32_t id) {
    return isSettlingEntryKind(fragments[id].function.getEntryKind());
  };
  SmallVector<schedule::ComputeEdgeAttr> processEdges;
  for (schedule::ComputeEdgeAttr edge : edges) {
    if (edge.getKind() == schedule::ComputeEdgeKind::ProcessOrder &&
        fragments[edge.getSource()].function.getHomeRegion() ==
            fragments[edge.getTarget()].function.getHomeRegion())
      processEdges.push_back(edge);
  }
  SmallVector<uint32_t> fragmentIds;
  fragmentIds.reserve(fragments.size());
  for (Fragment &fragment : fragments)
    fragmentIds.push_back(fragment.id);
  auto settlingFirst = [&](uint32_t id) { return settles(id) ? 0u : 1u; };
  SmallVector<SmallVector<uint32_t>> controlGroups = computeSCCSchedule(
      fragmentIds, processEdges, settlingFirst, startupPhases);
  SmallVector<unsigned> controlGroupForFragment(fragments.size());
  for (auto [group, members] : llvm::enumerate(controlGroups))
    for (uint32_t member : members)
      controlGroupForFragment[member] = static_cast<unsigned>(group);

  // Choose race ordering along the SAME activation graph used for region
  // planning, before adding arbitrary conflict edges. In particular, account
  // for non-settling producers and the work resumed by a settling publication.
  // Ordering only the watch fragments can put a consumer before its producer
  // and manufacture a large convergence SCC in an otherwise acyclic cone.
  // Every conflict edge below is monotone in this condensation, so it cannot
  // merge distinct activation SCCs. Required process order remains intact;
  // cross-region order is provided by the event loop (IEEE 1800-2023 4.4-4.7).
  SmallVector<schedule::ComputeEdgeAttr> activationEdges =
      buildSchedulingEdges();
  llvm::erase_if(activationEdges, [&](schedule::ComputeEdgeAttr edge) {
    return fragments[edge.getSource()].function.getHomeRegion() !=
           fragments[edge.getTarget()].function.getHomeRegion();
  });
  SmallVector<unsigned> activationRank(fragments.size(), 0);
  for (auto [rank, members] : llvm::enumerate(computeSCCSchedule(
           fragmentIds, activationEdges, settlingFirst, startupPhases)))
    for (uint32_t member : members)
      activationRank[member] = static_cast<unsigned>(rank);
  SmallVector<unsigned> groupRank(controlGroups.size(),
                                  std::numeric_limits<unsigned>::max());
  for (auto [group, members] : llvm::enumerate(controlGroups))
    for (uint32_t member : members)
      groupRank[group] = std::min(groupRank[group], activationRank[member]);

  // Conflict edges only need to impose an order, not encode the complete
  // pairwise relation. Union control components connected by a conflict, then
  // chain each resulting set in the deterministic process-topological order
  // above. Every original conflict is ordered transitively, existing process
  // order cannot be reversed, and disconnected sets remain independent for
  // multi-worker scheduling.
  SmallVector<unsigned> conflictParent(controlGroups.size());
  SmallVector<bool> participatesInConflict(controlGroups.size(), false);
  for (unsigned group = 0; group != conflictParent.size(); ++group)
    conflictParent[group] = group;
  auto findConflictRoot = [&](unsigned group) {
    unsigned root = group;
    while (conflictParent[root] != root)
      root = conflictParent[root];
    while (conflictParent[group] != group) {
      unsigned parent = conflictParent[group];
      conflictParent[group] = root;
      group = parent;
    }
    return root;
  };
  auto uniteConflicts = [&](unsigned lhs, unsigned rhs) {
    lhs = findConflictRoot(lhs);
    rhs = findConflictRoot(rhs);
    if (lhs == rhs)
      return;
    if (rhs < lhs)
      std::swap(lhs, rhs);
    conflictParent[rhs] = lhs;
  };
  for (unsigned lhs = 0; lhs != fragments.size(); ++lhs)
    for (const ComputeEffect &left : fragments[lhs].effects) {
      if (left.kind == schedule::ComputeEffectKind::Watch ||
          left.kind == schedule::ComputeEffectKind::NBA)
        continue;
      activeEffects.forEachAlias(left.target, [&](IndexedEffect right) {
        if (right.owner <= lhs ||
            fragments[right.owner].function == fragments[lhs].function ||
            fragments[right.owner].function.getHomeRegion() !=
                fragments[lhs].function.getHomeRegion() ||
            !activeEffectsConflict(left, *right.effect))
          return;
        unsigned leftGroup = controlGroupForFragment[lhs];
        unsigned rightGroup = controlGroupForFragment[right.owner];
        if (leftGroup == rightGroup)
          return;
        participatesInConflict[leftGroup] = true;
        participatesInConflict[rightGroup] = true;
        uniteConflicts(leftGroup, rightGroup);
      });
    }
  llvm::MapVector<unsigned, SmallVector<unsigned>> conflictSets;
  for (unsigned group = 0; group != controlGroups.size(); ++group)
    if (participatesInConflict[group])
      conflictSets[findConflictRoot(group)].push_back(group);
  ComputeEffect mergedConflict{schedule::ComputeEffectKind::Write};
  for (auto &[root, groups] : conflictSets) {
    (void)root;
    llvm::sort(groups, [&](unsigned lhs, unsigned rhs) {
      return std::make_pair(groupRank[lhs], lhs) <
             std::make_pair(groupRank[rhs], rhs);
    });
    for (auto pair : llvm::zip(groups, llvm::drop_begin(groups))) {
      unsigned sourceGroup = std::get<0>(pair);
      unsigned targetGroup = std::get<1>(pair);
      addEdge(controlGroups[sourceGroup].front(),
              controlGroups[targetGroup].front(),
              schedule::ComputeEdgeKind::Conflict, effectAttr(mergedConflict));
    }
  }

  // Staged updates reach their consumers through a commit node instead of
  // activating them in the active region.
  for (Fragment &fragment : fragments)
    for (const ComputeEffect &effect : fragment.effects) {
      bool nba = effect.kind == schedule::ComputeEffectKind::NBA;
      bool deferredTrigger =
          effect.kind == schedule::ComputeEffectKind::Trigger &&
          effect.deferred;
      if (!nba && !deferredTrigger)
        continue;
      ArrayRef<HandleFact> roots = nba ? nbaRoots : eventRoots;
      ArrayRef<uint32_t> ids = nba ? nbaCommitIds : eventCommitIds;
      uint32_t commit =
          ids[*findCommit(roots, getRootProvenance(effect.target))];
      addEdge(fragment.id, commit,
              nba ? schedule::ComputeEdgeKind::NBAStage
                  : schedule::ComputeEdgeKind::DeferredStage,
              effectAttr(effect));
    }
  for (auto activationSet :
       {std::tuple{ArrayRef<HandleFact>(nbaRoots),
                   ArrayRef<uint32_t>(nbaCommitIds),
                   schedule::ComputeEdgeKind::NBAActivate},
        std::tuple{ArrayRef<HandleFact>(eventRoots),
                   ArrayRef<uint32_t>(eventCommitIds),
                   schedule::ComputeEdgeKind::DeferredActivate}}) {
    ArrayRef<HandleFact> roots = std::get<0>(activationSet);
    ArrayRef<uint32_t> ids = std::get<1>(activationSet);
    schedule::ComputeEdgeKind activate = std::get<2>(activationSet);
    for (auto indexedRoot : llvm::enumerate(roots)) {
      size_t index = indexedRoot.index();
      const HandleFact &root = indexedRoot.value();
      uint32_t id = ids[index];
      watchedEffects.forEachAlias(
          root, [&, root = root](IndexedEffect consumed) {
            if (provenancesAlias(consumed.effect->target, root))
              addEdge(id, fragments[consumed.owner].id, activate,
                      effectAttr(*consumed.effect));
          });
    }
  }
  normalizeEdges(edges);
}

LogicalResult ComputeGraphBuilder::buildSites(ComputeGraphResult &result) {
  uint64_t timingSite = 0, nbaSite = 0, eventSite = 0;
  // Sites are numbered in a deterministic walk over every function, including
  // zero-time ones: a nonblocking assignment inside a function is legal and
  // needs a site even though the function is not itself a graph node.
  for (const FunctionInfo &info : analysis.functions) {
    auto walkResult =
        info.getFunction().walk([&](Operation *operation) -> WalkResult {
          if (isSuspensionTerminator(operation)) {
            if (info.getFunction().getEntryKind() == sim::EntryKind::Function &&
                isa<sim::SimProcessControlOp>(operation))
              return WalkResult::advance();
            Block *continuation =
                skipObserverCaptureBridges(operation->getSuccessor(0));
            auto found = fragmentForBlock.find(continuation);
            if (found == fragmentForBlock.end())
              return operation->emitOpError(
                  "resumes into a block with no fragment");
            result.continuations[operation] =
                schedule::ContinuationSiteAttr::get(design.getContext(),
                                                    found->second);
          }
          if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation))
            result.timings[operation] = schedule::TimingSiteAttr::get(
                design.getContext(), timingSite++,
                isConstantTimeValue(delay.getDelay())
                    ? schedule::ComputeTimingKind::Calendar
                    : schedule::ComputeTimingKind::DeadlineSlot);
          if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
            HandleFact destination =
                info.provenance.lookup(nba.getDestination());
            std::optional<uint32_t> commit =
                findCommit(nbaRoots, getRootProvenance(destination));
            if (!commit)
              return nba.emitOpError("has no generated commit node");
            uint64_t site = nbaSite++;
            schedule::ComputeNBAStorageKind storage = getNBAStorageKind(
                nba, info.getFunction(), multiplicity, destination);
            switch (storage) {
            case schedule::ComputeNBAStorageKind::FixedSlot:
              nbaSlots[*commit].push_back(site);
              break;
            case schedule::ComputeNBAStorageKind::RootAccumulator:
              nbaAccumulatorSites[*commit].push_back(site);
              break;
            case schedule::ComputeNBAStorageKind::DynamicFrontier:
              nbaFrontierSites[*commit].push_back(site);
              break;
            }
            schedule::TimingSiteAttr delayedTiming;
            if (nba.getDelay())
              delayedTiming = schedule::TimingSiteAttr::get(
                  design.getContext(), timingSite++,
                  schedule::ComputeTimingKind::DelayedNBA);
            result.nbaSites[operation] = schedule::NBASiteAttr::get(
                design.getContext(), site, nbaCommitIds[*commit], storage,
                delayedTiming);
          }
          if (auto trigger = dyn_cast<sim::SimEventTriggerOp>(operation);
              trigger && trigger.getNonblocking()) {
            HandleFact destination = info.provenance.lookup(trigger.getEvent());
            std::optional<uint32_t> commit =
                findCommit(eventRoots, getRootProvenance(destination));
            if (!commit)
              return trigger.emitOpError(
                  "has no generated deferred-event commit node");
            uint64_t site = eventSite++;
            eventSites[*commit].push_back(site);
            schedule::TimingSiteAttr delayedTiming;
            if (trigger.getDelay())
              delayedTiming = schedule::TimingSiteAttr::get(
                  design.getContext(), timingSite++,
                  schedule::ComputeTimingKind::DelayedEvent);
            result.eventSites[operation] = schedule::EventSiteAttr::get(
                design.getContext(), site, eventCommitIds[*commit],
                delayedTiming);
          }
          return WalkResult::advance();
        });
    if (walkResult.wasInterrupted())
      return failure();
  }
  return success();
}

SmallVector<schedule::ComputeEdgeAttr>
ComputeGraphBuilder::buildSchedulingEdges() {
  // Sensitivity edges terminate at the suspension that owns the watch, while
  // the work activated by that edge starts at the suspension's resume
  // continuation. Resume edges themselves are intentionally not scheduling
  // edges: including them would turn every repeating process into a
  // procedural cycle. Project only settling-process sensitivity through the
  // suspension so cross-process combinational feedback is visible to SCC
  // planning without changing the executable graph or clocked-process
  // semantics. Settling publications must nevertheless precede every kind of
  // resumed consumer: a procedural observer cannot run on one settled sibling
  // while another sibling publication is still pending.
  auto schedulingEdges =
      analysis::projectActivationSchedulingEdges(edges, [&](uint32_t source) {
        return isSettlingEntryKind(fragments[source].function.getEntryKind());
      });
  // A backedge whose loop is proven to terminate is not a scheduling cycle.
  // Unrolling such a loop deletes this edge outright, so removing it here keeps
  // the same schedule the unroller would have produced without paying for the
  // replicated body. The edge remains in the executable graph; only schedule
  // grouping ignores it.
  if (!boundedBackedges.empty())
    llvm::erase_if(schedulingEdges, [&](schedule::ComputeEdgeAttr edge) {
      return edge.getKind() == schedule::ComputeEdgeKind::ProcessOrder &&
             boundedBackedges.contains({edge.getSource(), edge.getTarget()});
    });
  normalizeEdges(schedulingEdges);
  return schedulingEdges;
}

FailureOr<ArrayAttr> ComputeGraphBuilder::buildRegions() {
  SmallVector<schedule::ComputeEdgeAttr> schedulingEdges =
      buildSchedulingEdges();

  auto plan =
      [&](schedule::ComputeRegionKind kind,
          ArrayRef<SmallVector<uint32_t>> groups) -> FailureOr<Attribute> {
    // Classify only the edges internal to each already-computed SCC. Scanning
    // the full design for every singleton made an acyclic graph cost O(V*E),
    // and repeated the same work for each feedback component. Dense node IDs
    // and a stable CSR partition keep this phase O(V+E) per event region.
    // The SCC order, edge kinds and normalized feedback-resource order remain
    // unchanged; resume/spawn edges still cannot make an activation cyclic.
    constexpr uint32_t absent = std::numeric_limits<uint32_t>::max();
    SmallVector<uint32_t> groupForNode;
    SmallVector<size_t> offsets(groups.size() + 1, 0);
    SmallVector<schedule::ComputeEdgeAttr> internalEdges;
    if (!groups.empty()) {
      groupForNode.assign(fragments.size() + nbaCommitIds.size() +
                              eventCommitIds.size(),
                          absent);
      for (auto [index, group] : llvm::enumerate(groups))
        for (uint32_t node : group)
          groupForNode[node] = index;
      auto internalGroup = [&](schedule::ComputeEdgeAttr edge) {
        uint32_t source = groupForNode[edge.getSource()];
        return source != absent && source == groupForNode[edge.getTarget()]
                   ? source
                   : absent;
      };
      for (schedule::ComputeEdgeAttr edge : schedulingEdges)
        if (uint32_t group = internalGroup(edge); group != absent)
          ++offsets[group + 1];
      for (size_t index = 1; index < offsets.size(); ++index)
        offsets[index] += offsets[index - 1];
      internalEdges.resize(offsets.back());
      SmallVector<size_t> cursor(offsets);
      for (schedule::ComputeEdgeAttr edge : schedulingEdges)
        if (uint32_t group = internalGroup(edge); group != absent)
          internalEdges[cursor[group]++] = edge;
    }
    if (!boundedBackedges.empty() &&
        design->getParentOfType<ModuleOp>()->hasAttr(
            "obelisk.debug.native_timing"))
      llvm::errs() << "obelisk bounded backedges in graph: "
                   << boundedBackedges.size() << '\n';
    SmallVector<Attribute> groupAttributes;
    for (auto [index, group] : llvm::enumerate(groups)) {
      ArrayRef<schedule::ComputeEdgeAttr> groupEdges =
          ArrayRef(internalEdges)
              .slice(offsets[index], offsets[index + 1] - offsets[index]);
      SmallVector<int64_t> ids(group.begin(), group.end());
      bool cyclic = group.size() > 1;
      if (!cyclic)
        cyclic = llvm::any_of(groupEdges, [&](schedule::ComputeEdgeAttr edge) {
          return isSchedulingEdge(edge.getKind()) &&
                 edge.getSource() == group.front() &&
                 edge.getTarget() == group.front();
        });
      schedule::ComputeScheduleKind schedule =
          schedule::ComputeScheduleKind::Acyclic;
      SmallVector<Attribute> feedback;
      if (cyclic) {
        if (hasProceduralControlCycle(group, groupEdges, boundedBackedges)) {
          schedule = schedule::ComputeScheduleKind::ControlLoop;
        } else {
          schedule = schedule::ComputeScheduleKind::Convergence;
          DenseSet<uint32_t> members(group.begin(), group.end());
          llvm::SmallDenseSet<Attribute> unique;
          for (schedule::ComputeEdgeAttr edge : groupEdges)
            if (members.contains(edge.getSource()) &&
                members.contains(edge.getTarget()) &&
                edge.getKind() == schedule::ComputeEdgeKind::Sensitivity &&
                edge.getResource() && unique.insert(edge.getResource()).second)
              feedback.push_back(edge.getResource());
          // Convergence compares state feedback on a cut. A group whose only
          // remaining cycle is a proven-bounded backedge has no such feedback
          // to compare, so it cannot use that schedule; keep the conservative
          // control-loop handoff instead of failing the build.
          if (feedback.empty())
            schedule = schedule::ComputeScheduleKind::ControlLoop;
          // `edges` is already normalized, so this order is deterministic.
        }
      }
      groupAttributes.push_back(schedule::ComputeGroupAttr::get(
          design.getContext(), builder.getDenseI64ArrayAttr(ids), schedule,
          builder.getArrayAttr(feedback)));
    }
    return static_cast<Attribute>(schedule::ComputeRegionAttr::get(
        design.getContext(), kind, builder.getArrayAttr(groupAttributes)));
  };

  SmallVector<uint32_t> activeIds;
  SmallVector<uint32_t> observedIds;
  SmallVector<uint32_t> reactiveIds;
  SmallVector<uint32_t> postponedIds;
  SmallVector<SmallVector<uint32_t>> postponedGroups;
  for (Fragment &fragment : fragments) {
    bool assertionEndOfSimulationCoordinator = ::obelisk::schedule::has<
        ::obelisk::schedule::Field::ConcurrentEosCoordinator>(
        fragment.function);
    if (fragment.function.getEntryKind() == sim::EntryKind::Final &&
        !assertionEndOfSimulationCoordinator)
      postponedGroups.push_back({fragment.id});
    else if (fragment.function.getHomeRegion() == sim::EventRegion::Active)
      activeIds.push_back(fragment.id);
    else if (fragment.function.getHomeRegion() == sim::EventRegion::Observed)
      observedIds.push_back(fragment.id);
    else if (fragment.function.getHomeRegion() == sim::EventRegion::Reactive)
      reactiveIds.push_back(fragment.id);
    else if (fragment.function.getHomeRegion() == sim::EventRegion::Postponed)
      postponedIds.push_back(fragment.id);
    else
      return fragment.function.emitOpError(
          "has no executable compute-graph home region");
  }
  SmallVector<SmallVector<uint32_t>> nbaGroups;
  for (uint32_t id : nbaCommitIds)
    nbaGroups.push_back({id});
  for (uint32_t id : eventCommitIds)
    nbaGroups.push_back({id});

  // Every standard event region is planned explicitly, even when a supported
  // design has no nodes in one of them.
  auto activePriority = [&](uint32_t id) {
    return isSettlingEntryKind(fragments[id].function.getEntryKind()) ? 0u : 1u;
  };
  SmallVector<SmallVector<uint32_t>> activeGroups = computeSCCSchedule(
      activeIds, schedulingEdges, activePriority, startupPhases);
  SmallVector<SmallVector<uint32_t>> observedGroups =
      computeSCCSchedule(observedIds, schedulingEdges, {}, startupPhases);
  SmallVector<SmallVector<uint32_t>> reactiveGroups =
      computeSCCSchedule(reactiveIds, schedulingEdges, {}, startupPhases);
  SmallVector<SmallVector<uint32_t>> ordinaryPostponedGroups =
      computeSCCSchedule(postponedIds, schedulingEdges, {}, startupPhases);
  llvm::append_range(ordinaryPostponedGroups, postponedGroups);
  std::pair<schedule::ComputeRegionKind, ArrayRef<SmallVector<uint32_t>>>
      plans[] = {
          {schedule::ComputeRegionKind::Active, activeGroups},
          {schedule::ComputeRegionKind::NBA, nbaGroups},
          {schedule::ComputeRegionKind::Observed, observedGroups},
          {schedule::ComputeRegionKind::Reactive, reactiveGroups},
          {schedule::ComputeRegionKind::Postponed, ordinaryPostponedGroups}};

  SmallVector<Attribute> regions;
  for (auto [kind, groups] : plans) {
    FailureOr<Attribute> region = plan(kind, groups);
    if (failed(region))
      return failure();
    regions.push_back(*region);
  }
  return builder.getArrayAttr(regions);
}

FailureOr<ComputeGraphResult> ComputeGraphBuilder::derive() {
  ComputeGraphResult result;
  if (options.workers == 0 || options.workers > 65535)
    return design.emitOpError(
        "requested worker count exceeds the lane ID range");
  if (failed(buildFragments()))
    return failure();
  orderStartupSpawns();
  buildControlEdges();
  if (uint64_t(fragments.size()) + startupPhases.size() > maxNodeId + 1)
    return design.emitOpError(
        "startup scheduling exceeds the 32-bit node range");
  buildDataEdges();
  if (failed(buildSites(result)))
    return failure();

  // Dozens of scalar built-in primitive actors are substantially more
  // compact in the bytecode image than as independent LLVM coroutines. Keep
  // small cohorts native, but demote a large cohort under the default auto
  // policy so the backend can select a compact bytecode executable. This is a
  // compile-space guard, not a semantic distinction: both tiers consume the
  // same graph and exact descriptor-range subscriptions.
  constexpr uint64_t primitiveBytecodeThreshold = 32;
  uint64_t primitiveActors =
      llvm::count_if(analysis.functions, [](const FunctionInfo &info) {
        return info.getFunction().getEntryKind() ==
                   sim::EntryKind::Continuous &&
               ::obelisk::schedule::has<
                   ::obelisk::schedule::Field::PrimitiveName>(
                   info.getFunction());
      });
  ModuleOp module = design->getParentOfType<ModuleOp>();
  auto scheduler =
      module ? ::obelisk::schedule::get<
                   ::obelisk::schedule::Field::NativeScheduler>(module)
             : schedule::NativeSchedulerModeAttr{};
  bool demotePrimitiveActors =
      primitiveActors >= primitiveBytecodeThreshold && scheduler &&
      scheduler.getValue() == schedule::NativeSchedulerMode::Eval && module &&
      ::obelisk::schedule::has<
          ::obelisk::schedule::Field::NativeSchedulerAutoRequested>(module);

  SmallVector<Attribute> nodes;
  DenseMap<Operation *, SmallVector<int64_t>> functionFragments;
  for (Fragment &fragment : fragments) {
    schedule::ComputeRegionKind region = schedule::ComputeRegionKind::Active;
    bool assertionEndOfSimulationCoordinator = ::obelisk::schedule::has<
        ::obelisk::schedule::Field::ConcurrentEosCoordinator>(
        fragment.function);
    if ((fragment.function.getEntryKind() == sim::EntryKind::Final &&
         !assertionEndOfSimulationCoordinator) ||
        fragment.function.getHomeRegion() == sim::EventRegion::Postponed)
      region = schedule::ComputeRegionKind::Postponed;
    else if (fragment.function.getHomeRegion() == sim::EventRegion::Observed)
      region = schedule::ComputeRegionKind::Observed;
    else if (fragment.function.getHomeRegion() == sim::EventRegion::Reactive)
      region = schedule::ComputeRegionKind::Reactive;
    schedule::ComputeTierKind tier =
        demotePrimitiveActors && ::obelisk::schedule::has<
                                     ::obelisk::schedule::Field::PrimitiveName>(
                                     fragment.function)
            ? schedule::ComputeTierKind::Bytecode
            : schedule::ComputeTierKind::Native;
    nodes.push_back(schedule::ComputeFragmentAttr::get(
        design.getContext(), fragment.id,
        FlatSymbolRefAttr::get(design.getContext(),
                               fragment.function.getSymName()),
        fragment.ordinal, region,
        getFragmentActionKind(fragment.block->getTerminator()), tier,
        fragment.cost, fragment.lane, fragment.twoState,
        getEffectArrayAttr(builder, fragment.effects)));
    functionFragments[fragment.function.getOperation()].push_back(fragment.id);
  }
  for (auto [index, root] : llvm::enumerate(nbaRoots))
    nodes.push_back(schedule::ComputeNBACommitAttr::get(
        design.getContext(), nbaCommitIds[index],
        builder.getDenseI64ArrayAttr(nbaSlots[index]),
        builder.getDenseI64ArrayAttr(nbaAccumulatorSites[index]),
        builder.getDenseI64ArrayAttr(nbaFrontierSites[index]),
        effectAttr({schedule::ComputeEffectKind::Write, root})));
  for (auto [index, root] : llvm::enumerate(eventRoots))
    nodes.push_back(schedule::ComputeEventCommitAttr::get(
        design.getContext(), eventCommitIds[index],
        builder.getDenseI64ArrayAttr(eventSites[index]),
        effectAttr({schedule::ComputeEffectKind::Trigger, root,
                    schedule::ComputeTriggerKind::None, true})));
  for (const FunctionInfo &info : analysis.functions) {
    result.effectSummaries[info.getFunction().getOperation()] =
        getEffectArrayAttr(builder, info.summary);
    result.fragmentAbis[info.getFunction().getOperation()] =
        schedule::FragmentABIAttr::get(
            design.getContext(), schedule::metadata::schemaVersion,
            builder.getDenseI64ArrayAttr(
                functionFragments[info.getFunction().getOperation()]));
  }

  FailureOr<ArrayAttr> regions = buildRegions();
  if (failed(regions))
    return failure();

  result.observability =
      analysis::SimulationVPIAnalysis::forMode(options.vpi).getObservability();
  result.graph = schedule::ComputeGraphAttr::get(
      design.getContext(), schedule::metadata::schemaVersion, options.vpi,
      options.workers, builder.getArrayAttr(nodes),
      builder.getArrayAttr(SmallVector<Attribute>(edges.begin(), edges.end())),
      *regions);
  if (failed(validateComputeGraphStructure(design, result.graph)))
    return failure();
  return result;
}

} // namespace

LogicalResult validateComputeGraphStructure(sim::SimDesignOp design,
                                            schedule::ComputeGraphAttr graph) {
  auto emitInvalid = [&]() {
    return design.emitOpError("contains invalid compute-graph metadata: ");
  };
  if (!graph ||
      failed(schedule::ComputeGraphAttr::verifyInvariants(
          emitInvalid, graph.getVersion(), graph.getVpi(), graph.getWorkers(),
          graph.getNodes(), graph.getEdges(), graph.getRegions())))
    return failure();

  auto verifyEffect = [&](schedule::ComputeEffectAttr effect) {
    return schedule::ComputeEffectAttr::verifyInvariants(
        emitInvalid, effect.getEffect(), effect.getResource(),
        effect.getTarget(), effect.getDescriptor(), effect.getFormal(),
        effect.getLow(), effect.getWidth(), effect.getDynamic(),
        effect.getDeferred(), effect.getTrigger());
  };

  ArrayAttr nodes = graph.getNodes();
  if (nodes.size() > std::numeric_limits<uint32_t>::max())
    return design.emitOpError("compute graph exceeds the 32-bit fragment ABI");
  enum class NodeKind { Fragment, NBACommit, EventCommit };
  SmallVector<NodeKind> nodeKinds(nodes.size());
  SmallVector<std::optional<schedule::ComputeRegionKind>> nodeRegions(
      nodes.size());
  llvm::SmallDenseSet<Attribute> nbaCommitTargets;
  llvm::SmallDenseSet<Attribute> eventCommitTargets;
  llvm::SmallDenseSet<int64_t> nbaSites;
  llvm::SmallDenseSet<int64_t> deferredEventSites;
  for (auto [index, attribute] : llvm::enumerate(nodes)) {
    uint32_t id;
    schedule::ComputeRegionKind region;
    if (auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute)) {
      if (failed(schedule::ComputeFragmentAttr::verifyInvariants(
              emitInvalid, fragment.getId(), fragment.getFunction(),
              fragment.getBlock(), fragment.getRegion(), fragment.getAction(),
              fragment.getTier(), fragment.getCost(), fragment.getLane(),
              fragment.getTwoState(), fragment.getEffects())))
        return failure();
      for (Attribute effect : fragment.getEffects())
        if (failed(verifyEffect(cast<schedule::ComputeEffectAttr>(effect))))
          return failure();
      if (fragment.getLane() >= graph.getWorkers())
        return design.emitOpError(
            "compute fragment lane exceeds the worker count");
      id = fragment.getId();
      region = fragment.getRegion();
      nodeKinds[index] = NodeKind::Fragment;
    } else if (auto commit =
                   dyn_cast<schedule::ComputeNBACommitAttr>(attribute)) {
      if (failed(schedule::ComputeNBACommitAttr::verifyInvariants(
              emitInvalid, commit.getId(), commit.getSlots(),
              commit.getAccumulatorSites(), commit.getFrontierSites(),
              commit.getEffect())) ||
          failed(verifyEffect(commit.getEffect())))
        return failure();
      for (DenseI64ArrayAttr inventory :
           {commit.getSlots(), commit.getAccumulatorSites(),
            commit.getFrontierSites()})
        for (int64_t site : inventory.asArrayRef())
          if (!nbaSites.insert(site).second)
            return design.emitOpError(
                "compute graph inventories an NBA site more than once");
      if (!nbaCommitTargets.insert(commit.getEffect()).second)
        return design.emitOpError(
            "compute graph has a duplicate NBA commit target");
      id = commit.getId();
      region = schedule::ComputeRegionKind::NBA;
      nodeKinds[index] = NodeKind::NBACommit;
    } else {
      auto eventCommit = cast<schedule::ComputeEventCommitAttr>(attribute);
      if (failed(schedule::ComputeEventCommitAttr::verifyInvariants(
              emitInvalid, eventCommit.getId(), eventCommit.getSites(),
              eventCommit.getEffect())) ||
          failed(verifyEffect(eventCommit.getEffect())))
        return failure();
      for (int64_t site : eventCommit.getSites().asArrayRef())
        if (!deferredEventSites.insert(site).second)
          return design.emitOpError(
              "compute graph inventories a deferred-event site more than "
              "once");
      if (!eventCommitTargets.insert(eventCommit.getEffect()).second)
        return design.emitOpError(
            "compute graph has a duplicate deferred-event commit target");
      id = eventCommit.getId();
      region = schedule::ComputeRegionKind::NBA;
      nodeKinds[index] = NodeKind::EventCommit;
    }
    if (id != index)
      return design.emitOpError(
          "compute-graph nodes are not stored in dense ID order");
    nodeRegions[index] = region;
  }

  llvm::SmallDenseSet<Attribute> uniqueEdges;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
    if (failed(schedule::ComputeEdgeAttr::verifyInvariants(
            emitInvalid, edge.getSource(), edge.getTarget(), edge.getKind(),
            edge.getResource())))
      return failure();
    if (edge.getResource() && failed(verifyEffect(edge.getResource())))
      return failure();
    if (edge.getSource() >= nodes.size() || edge.getTarget() >= nodes.size())
      return design.emitOpError(
          "compute graph contains an invalid edge endpoint");
    if (!uniqueEdges.insert(edge).second)
      return design.emitOpError("compute graph contains a duplicate edge");
    NodeKind source = nodeKinds[edge.getSource()];
    NodeKind target = nodeKinds[edge.getTarget()];
    switch (edge.getKind()) {
    case schedule::ComputeEdgeKind::ProcessOrder:
    case schedule::ComputeEdgeKind::Resume:
    case schedule::ComputeEdgeKind::Spawn:
    case schedule::ComputeEdgeKind::Sensitivity:
    case schedule::ComputeEdgeKind::Conflict:
      if (source != NodeKind::Fragment || target != NodeKind::Fragment)
        return design.emitOpError(
            "compute graph control/data edge does not connect fragments");
      break;
    case schedule::ComputeEdgeKind::NBAStage:
      if (source != NodeKind::Fragment || target != NodeKind::NBACommit ||
          edge.getResource().getEffect() != schedule::ComputeEffectKind::NBA)
        return design.emitOpError(
            "NBA stage edge has invalid endpoint or resource kinds");
      break;
    case schedule::ComputeEdgeKind::NBAActivate:
      if (source != NodeKind::NBACommit || target != NodeKind::Fragment ||
          edge.getResource().getEffect() != schedule::ComputeEffectKind::Watch)
        return design.emitOpError(
            "NBA activation edge has invalid endpoint or resource kinds");
      break;
    case schedule::ComputeEdgeKind::DeferredStage:
      if (source != NodeKind::Fragment || target != NodeKind::EventCommit ||
          edge.getResource().getEffect() !=
              schedule::ComputeEffectKind::Trigger ||
          !edge.getResource().getDeferred())
        return design.emitOpError(
            "deferred-event stage edge has invalid endpoint or resource "
            "kinds");
      break;
    case schedule::ComputeEdgeKind::DeferredActivate:
      if (source != NodeKind::EventCommit || target != NodeKind::Fragment ||
          edge.getResource().getEffect() != schedule::ComputeEffectKind::Watch)
        return design.emitOpError(
            "deferred-event activation edge has invalid endpoint or resource "
            "kinds");
      break;
    }
  }

  SmallVector<bool> scheduled(nodes.size(), false);
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<schedule::ComputeRegionAttr>(regionAttribute);
    if (failed(schedule::ComputeRegionAttr::verifyInvariants(
            emitInvalid, region.getKind(), region.getGroups())))
      return failure();
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = cast<schedule::ComputeGroupAttr>(groupAttribute);
      if (failed(schedule::ComputeGroupAttr::verifyInvariants(
              emitInvalid, group.getFragments(), group.getSchedule(),
              group.getFeedback())))
        return failure();
      for (Attribute effect : group.getFeedback())
        if (failed(verifyEffect(cast<schedule::ComputeEffectAttr>(effect))))
          return failure();
      for (int64_t member : group.getFragments().asArrayRef()) {
        if (member < 0 || static_cast<uint64_t>(member) >= nodes.size())
          return design.emitOpError(
              "event-region group references an invalid node");
        size_t index = static_cast<size_t>(member);
        if (scheduled[index])
          return design.emitOpError(
              "compute-graph node is scheduled more than once");
        if (nodeRegions[index] != region.getKind())
          return design.emitOpError(
              "compute-graph node is scheduled in the wrong event region");
        scheduled[index] = true;
      }
    }
  }
  if (llvm::is_contained(scheduled, false))
    return design.emitOpError(
        "event-region plans do not schedule every compute-graph node");
  return success();
}

FailureOr<ComputeGraphResult> deriveComputeGraph(sim::SimDesignOp design,
                                                 ComputeGraphOptions options) {
  FailureOr<StateDomainAnalysis> stateDomains =
      StateDomainAnalysis::computeInductiveOnly(design);
  if (failed(stateDomains))
    return failure();
  ComputeGraphBuilder builder(design, options, *stateDomains);
  return builder.derive();
}

} // namespace obelisk::simlowering
