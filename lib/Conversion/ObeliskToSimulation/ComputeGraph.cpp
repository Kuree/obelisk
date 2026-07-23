//===- ComputeGraph.cpp - Derive the late simulation schedule ------------===//
//
// The executable obelisk_sim CFG remains the source of truth. This analysis
// derives deterministic compiler metadata from it: precise descriptor-range
// summaries, fixed static sites, fragment ABI records, and an event-region
// graph with SCC convergence groups. It never mutates the design.
//
//===----------------------------------------------------------------------===//

#include "ComputeGraph.h"

#include "obelisk/Analysis/NetConnectivityAnalysis.h"
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

DescriptorProvenance widenDynamic(DescriptorProvenance provenance) {
  if (provenance.resource != sim::ComputeResourceKind::Unknown)
    provenance.dynamic = true;
  return provenance;
}

DescriptorProvenance narrowProvenance(DescriptorProvenance provenance,
                                      uint64_t low, uint64_t width) {
  if (provenance.resource == sim::ComputeResourceKind::Unknown)
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
DescriptorProvenance getRootProvenance(DescriptorProvenance provenance) {
  if (provenance.resource == sim::ComputeResourceKind::Unknown)
    return {};
  if (provenance.formal && !provenance.descriptor)
    return {};
  provenance.low = 0;
  provenance.width = provenance.rootWidth;
  provenance.dynamic = false;
  return provenance;
}

bool provenanceLess(const DescriptorProvenance &lhs,
                    const DescriptorProvenance &rhs) {
  auto key = [](const DescriptorProvenance &provenance) {
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
bool provenancesAlias(const DescriptorProvenance &lhs,
                      const DescriptorProvenance &rhs) {
  if (lhs.resource == sim::ComputeResourceKind::Unknown ||
      rhs.resource == sim::ComputeResourceKind::Unknown)
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
  sim::ComputeEffectKind kind = sim::ComputeEffectKind::Read;
  DescriptorProvenance target;
  sim::ComputeTriggerKind trigger = sim::ComputeTriggerKind::None;
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
}

/// Effects that publish a new value in the active region. NBA staging and
/// sensitivity subscriptions are not active producers.
bool isActiveProducer(const ComputeEffect &effect) {
  return !effect.deferred && (effect.kind == sim::ComputeEffectKind::Write ||
                              effect.kind == sim::ComputeEffectKind::Drive ||
                              effect.kind == sim::ComputeEffectKind::Trigger);
}

bool activeEffectsConflict(const ComputeEffect &lhs, const ComputeEffect &rhs) {
  if (lhs.kind == sim::ComputeEffectKind::Watch ||
      rhs.kind == sim::ComputeEffectKind::Watch ||
      lhs.kind == sim::ComputeEffectKind::NBA ||
      rhs.kind == sim::ComputeEffectKind::NBA)
    return false;
  if (!isActiveProducer(lhs) && !isActiveProducer(rhs))
    return false;
  return provenancesAlias(lhs.target, rhs.target);
}

sim::ComputeEffectAttr getEffectAttr(MLIRContext *context,
                                     const ComputeEffect &effect) {
  // An unresolved handle keeps no range: propagation can reach `unknown` while
  // still carrying the width of the type it started from, and publishing that
  // would build an attribute this dialect's own verifier rejects.
  DescriptorProvenance provenance = effect.target;
  if (provenance.resource == sim::ComputeResourceKind::Unknown)
    provenance = {};
  sim::ComputeTargetKind target = sim::ComputeTargetKind::Unknown;
  if (provenance.resource != sim::ComputeResourceKind::Unknown) {
    if (provenance.descriptor)
      target = sim::ComputeTargetKind::Descriptor;
    else if (provenance.formal)
      target = sim::ComputeTargetKind::Formal;
    else if (provenance.resource == sim::ComputeResourceKind::Local)
      target = sim::ComputeTargetKind::Local;
  }
  return sim::ComputeEffectAttr::get(
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
    if (effect.target.resource == sim::ComputeResourceKind::Unknown) {
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
  void forEachAlias(const DescriptorProvenance &target,
                    Callback &&callback) const {
    auto visit = [&](ArrayRef<IndexedEffect> effects) {
      for (IndexedEffect effect : effects)
        callback(effect);
    };
    if (target.resource == sim::ComputeResourceKind::Unknown) {
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
  DenseMap<Value, DescriptorProvenance> provenance;
  SmallVector<ComputeEffect> baseEffects;
  SmallVector<ComputeEffect> summary;
  SmallVector<sim::SimCallOp> calls;
  SmallVector<sim::SimTaskCallOp> taskCalls;
  SmallVector<sim::SimObserverBindOp> observerBindings;
  /// Functions this one starts as an independent process, directly or through
  /// a zero-time call. Indices into `ProgramAnalysis::functions`.
  SmallVector<unsigned> spawns;
};

sim::ComputeTriggerKind getTriggerKind(sim::EdgeKind edge) {
  switch (edge) {
  case sim::EdgeKind::Change:
    return sim::ComputeTriggerKind::Change;
  case sim::EdgeKind::Posedge:
    return sim::ComputeTriggerKind::Posedge;
  case sim::EdgeKind::Negedge:
    return sim::ComputeTriggerKind::Negedge;
  case sim::EdgeKind::Both:
    return sim::ComputeTriggerKind::Both;
  }
  llvm_unreachable("unknown sensitivity edge");
}

void appendEffect(
    const FunctionInfo &info, sim::ComputeEffectKind kind, Value handle,
    SmallVectorImpl<ComputeEffect> &effects,
    sim::ComputeTriggerKind trigger = sim::ComputeTriggerKind::None,
    bool deferred = false) {
  auto provenance = info.provenance.find(handle);
  if (provenance == info.provenance.end()) {
    effects.push_back({kind, DescriptorProvenance{}, trigger, deferred});
    return;
  }
  // Process-local allocations are never shared scheduling resources.
  if (provenance->second.resource != sim::ComputeResourceKind::Local)
    effects.push_back({kind, provenance->second, trigger, deferred});
}

SmallVector<ComputeEffect> collectDirectEffects(const FunctionInfo &info,
                                                Block *onlyBlock = nullptr) {
  SmallVector<ComputeEffect> effects;
  auto visit = [&](Operation *operation) {
    llvm::TypeSwitch<Operation *>(operation)
        .Case<sim::SimRefLoadOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Read, op.getReference(),
                       effects);
        })
        .Case<sim::SimRefStoreOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Write, op.getReference(),
                       effects);
        })
        .Case<sim::SimNetReadOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Read, op.getNet(),
                       effects);
        })
        .Case<sim::SimDriverDriveOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Drive, op.getDriver(),
                       effects);
        })
        .Case<sim::SimNBAEnqueueOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::NBA, op.getDestination(),
                       effects, sim::ComputeTriggerKind::None,
                       static_cast<bool>(op.getDelay()));
        })
        .Case<sim::SimSuspendChangeOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Watch, op.getWatched(),
                       effects, sim::ComputeTriggerKind::Change);
        })
        .Case<sim::SimSuspendEdgeOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Watch, op.getWatched(),
                       effects, getTriggerKind(op.getEdge()));
        })
        .Case<sim::SimSuspendEdgeIffOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Watch, op.getWatched(),
                       effects, getTriggerKind(op.getEdge()));
          appendEffect(info, sim::ComputeEffectKind::Read, op.getCondition(),
                       effects);
        })
        .Case<sim::SimSuspendLevelOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Watch, op.getWatched(),
                       effects, sim::ComputeTriggerKind::Change);
          appendEffect(info, sim::ComputeEffectKind::Read, op.getWatched(),
                       effects);
        })
        .Case<sim::SimSuspendAnyOp>([&](auto op) {
          for (auto [watched, edge] : llvm::zip(op.getWatched(), op.getEdges()))
            appendEffect(info, sim::ComputeEffectKind::Watch, watched, effects,
                         getTriggerKind(static_cast<sim::EdgeKind>(edge)));
        })
        .Case<sim::SimSuspendObserveOp>([&](auto op) {
          for (Value observerValue : op.getPrimaries()) {
            auto binding =
                observerValue.getDefiningOp<sim::SimObserverBindOp>();
            if (!binding)
              continue;
            for (Value dependency : binding.getDependencies())
              appendEffect(info, sim::ComputeEffectKind::Watch, dependency,
                           effects,
                           isa<sim::EventType>(dependency.getType())
                               ? sim::ComputeTriggerKind::Event
                               : sim::ComputeTriggerKind::Change);
          }
          for (Value observerValue : op.getConditions()) {
            auto binding =
                observerValue.getDefiningOp<sim::SimObserverBindOp>();
            if (!binding)
              continue;
            for (Value dependency : binding.getDependencies())
              appendEffect(info, sim::ComputeEffectKind::Read, dependency,
                           effects);
          }
        })
        .Case<sim::SimSuspendEventOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Watch, op.getEvent(),
                       effects, sim::ComputeTriggerKind::Event);
        })
        .Case<sim::SimEventTriggerOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Trigger, op.getEvent(),
                       effects, sim::ComputeTriggerKind::None,
                       op.getNonblocking());
        })
        .Case<sim::SimEventTriggeredOp>([&](auto op) {
          appendEffect(info, sim::ComputeEffectKind::Read, op.getEvent(),
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
  if (effect.kind == sim::ComputeEffectKind::NBA ||
      (effect.kind == sim::ComputeEffectKind::Trigger && effect.deferred))
    return {effect.kind, DescriptorProvenance{}, effect.trigger,
            effect.deferred};
  unsigned index = *effect.target.formal;
  if (index >= call.getNumOperands())
    return {effect.kind, DescriptorProvenance{}, effect.trigger,
            effect.deferred};
  auto actual = caller.provenance.find(call.getOperand(index));
  if (actual == caller.provenance.end())
    return {effect.kind, DescriptorProvenance{}, effect.trigger,
            effect.deferred};
  DescriptorProvenance target =
      effect.target.dynamic || actual->second.dynamic
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
  if (effect.kind == sim::ComputeEffectKind::NBA ||
      (effect.kind == sim::ComputeEffectKind::Trigger && effect.deferred))
    return {effect.kind, DescriptorProvenance{}, effect.trigger,
            effect.deferred};
  unsigned formal = *effect.target.formal;
  if (formal == 0 || formal - 1 >= binding.getCaptures().size())
    return {effect.kind, DescriptorProvenance{}, effect.trigger,
            effect.deferred};
  auto actual = waiter.provenance.find(binding.getCaptures()[formal - 1]);
  if (actual == waiter.provenance.end())
    return {effect.kind, DescriptorProvenance{}, effect.trigger,
            effect.deferred};
  DescriptorProvenance target =
      effect.target.dynamic || actual->second.dynamic
          ? widenDynamic(actual->second)
          : narrowProvenance(actual->second, effect.target.low,
                             effect.target.width);
  return {effect.kind, target, effect.trigger, effect.deferred};
}

struct ProgramAnalysis {
  explicit ProgramAnalysis(sim::SimDesignOp design) : connectivity(design) {}

  ::obelisk::analysis::NetConnectivityAnalysis connectivity;
  SmallVector<FunctionInfo, 0> functions;
  llvm::StringMap<unsigned> functionIndex;
  DenseMap<Operation *, unsigned> indexForFunction;

  const FunctionInfo &operator[](sim::SimFuncOp function) const {
    return functions[indexForFunction.lookup(function.getOperation())];
  }

  void expandConnectivity(SmallVectorImpl<ComputeEffect> &effects) const {
    SmallVector<ComputeEffect> expanded;
    for (const ComputeEffect &effect : effects) {
      if (effect.target.resource != sim::ComputeResourceKind::Net ||
          !effect.target.descriptor || effect.target.width == 0) {
        expanded.push_back(effect);
        continue;
      }
      uint64_t begin = effect.target.dynamic ? 0 : effect.target.low;
      uint64_t width =
          effect.target.dynamic ? effect.target.rootWidth : effect.target.width;
      bool found = false;
      for (uint64_t bit = 0; bit != width; ++bit) {
        ArrayRef<::obelisk::analysis::NetBit> component = connectivity.getComponent(
            {*effect.target.descriptor, begin + bit});
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
          found = true;
        }
      }
      if (!found)
        expanded.push_back(effect);
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
      info.baseEffects = {{sim::ComputeEffectKind::Read},
                          {sim::ComputeEffectKind::Write}};
    } else {
      info.provenance =
          ::obelisk::analysis::deriveDescriptorProvenance(function);
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
        if (auto binding =
                observer.getDefiningOp<sim::SimObserverBindOp>())
          bindings.insert(binding);
      for (Value observer : observe.getConditions())
        if (auto binding =
                observer.getDefiningOp<sim::SimObserverBindOp>())
          bindings.insert(binding);
      for (Operation *binding : bindings)
        info.observerBindings.push_back(cast<sim::SimObserverBindOp>(binding));
    });
  }

  SmallVector<SmallVector<unsigned>> callsFrom(analysis.functions.size());
  for (unsigned caller = 0; caller != analysis.functions.size(); ++caller)
    for (sim::SimCallOp call : analysis.functions[caller].calls) {
      auto callee = analysis.functionIndex.find(call.getCallee());
      if (callee != analysis.functionIndex.end())
        callsFrom[caller].push_back(callee->second);
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

  // Publish a deterministic monotone fixed point. Calls within a recursive
  // component and unresolved external calls conservatively touch all state.
  // Spawn sets propagate through every call because they are finite symbol
  // sets, so recursion needs no extra conservatism there.
  for (unsigned index = 0; index != analysis.functions.size(); ++index)
    analysis.functions[index].function.walk([&](sim::SimSpawnOp spawn) {
      auto callee = analysis.functionIndex.find(spawn.getCallee());
      if (callee != analysis.functionIndex.end())
        analysis.functions[index].spawns.push_back(callee->second);
    });
  while (true) {
    bool changed = false;
    for (unsigned caller = 0; caller != analysis.functions.size(); ++caller) {
      FunctionInfo &info = analysis.functions[caller];
      SmallVector<ComputeEffect> nextEffects = info.baseEffects;
      SmallVector<unsigned> nextSpawns = info.spawns;
      for (sim::SimCallOp call : info.calls) {
        auto callee = analysis.functionIndex.find(call.getCallee());
        if (callee == analysis.functionIndex.end() ||
            callComponent[callee->second] == callComponent[caller]) {
          nextEffects.push_back({sim::ComputeEffectKind::Read});
          nextEffects.push_back({sim::ComputeEffectKind::Write});
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
          nextEffects.push_back({sim::ComputeEffectKind::Read});
          nextEffects.push_back({sim::ComputeEffectKind::Write});
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
    if (!changed)
      break;
  }
  return analysis;
}

SmallVector<ComputeEffect>
collectFragmentEffects(const ProgramAnalysis &analysis,
                       const FunctionInfo &info, Block &block) {
  SmallVector<ComputeEffect> effects = collectDirectEffects(info, &block);
  for (sim::SimCallOp call : block.getOps<sim::SimCallOp>()) {
    auto callee = analysis.functionIndex.find(call.getCallee());
    if (callee == analysis.functionIndex.end()) {
      effects.push_back({sim::ComputeEffectKind::Read});
      effects.push_back({sim::ComputeEffectKind::Write});
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
      if (auto binding =
              observer.getDefiningOp<sim::SimObserverBindOp>())
        bindings.insert(binding);
    for (Value observer : observe.getConditions())
      if (auto binding =
              observer.getDefiningOp<sim::SimObserverBindOp>())
        bindings.insert(binding);
    for (Operation *operation : bindings) {
      auto binding = cast<sim::SimObserverBindOp>(operation);
      auto evaluator = analysis.functionIndex.find(binding.getEvaluator());
      if (evaluator == analysis.functionIndex.end()) {
        effects.push_back({sim::ComputeEffectKind::Read});
        effects.push_back({sim::ComputeEffectKind::Write});
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
        !stateDomains.isTwoState(argument))
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
          !stateDomains.isTwoState(operand))
        return false;
    for (Value result : operation.getResults())
      if (containsFourStateLeaf(result.getType()) &&
          !stateDomains.isTwoState(result))
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
sim::ComputeNBAStorageKind
getNBAStorageKind(sim::SimNBAEnqueueOp nba, sim::SimFuncOp function,
                  const SpawnMultiplicity &multiplicity,
                  const DescriptorProvenance &destination,
                  sim::ComputeVPIMode vpi) {
  bool fixed = function.getEntryKind() != sim::EntryKind::Function &&
               !multiplicity.isDynamicallySpawned(function) &&
               !multiplicity.mayReexecute(function, nba->getBlock());
  if (fixed)
    return sim::ComputeNBAStorageKind::FixedSlot;
  if (!nba.getDelay() && destination.descriptor &&
      vpi != sim::ComputeVPIMode::Full)
    return sim::ComputeNBAStorageKind::RootAccumulator;
  return sim::ComputeNBAStorageKind::DynamicFrontier;
}

//===----------------------------------------------------------------------===//
// Graph shape
//===----------------------------------------------------------------------===//

/// Edges that constrain evaluation order *within* one activation of an event
/// region. Resume leaves the region by definition, and a spawn starts a fresh
/// actor rather than feeding a value back into the current one, so neither can
/// make a group cyclic.
bool isSchedulingEdge(sim::ComputeEdgeKind kind) {
  return kind != sim::ComputeEdgeKind::Resume &&
         kind != sim::ComputeEdgeKind::Spawn;
}

void normalizeEdges(SmallVectorImpl<sim::ComputeEdgeAttr> &edges) {
  llvm::sort(edges, [](sim::ComputeEdgeAttr lhs, sim::ComputeEdgeAttr rhs) {
    auto key = [](sim::ComputeEdgeAttr edge) {
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
    return key(lhs) < key(rhs);
  });
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
}

/// Whether the source-order edges inside one group form a cycle. A cyclic
/// group of procedural fragments is a loop to execute, not a value to converge.
bool hasProceduralControlCycle(ArrayRef<uint32_t> group,
                               ArrayRef<sim::ComputeEdgeAttr> edges) {
  DenseSet<uint32_t> members(group.begin(), group.end());
  DenseMap<uint32_t, SmallVector<uint32_t>> successors;
  DenseMap<uint32_t, unsigned> indegree;
  for (uint32_t member : group)
    indegree.try_emplace(member, 0);
  for (sim::ComputeEdgeAttr edge : edges) {
    if (edge.getKind() != sim::ComputeEdgeKind::ProcessOrder ||
        !members.contains(edge.getSource()) ||
        !members.contains(edge.getTarget()))
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
                   ArrayRef<sim::ComputeEdgeAttr> edges) {
  DenseMap<uint32_t, SmallVector<uint32_t>> adjacency;
  DenseSet<uint32_t> nodeSet(nodes.begin(), nodes.end());
  for (sim::ComputeEdgeAttr edge : edges)
    if (isSchedulingEdge(edge.getKind()) && nodeSet.count(edge.getSource()) &&
        nodeSet.count(edge.getTarget()))
      adjacency[edge.getSource()].push_back(edge.getTarget());
  for (auto &entry : adjacency) {
    llvm::sort(entry.second);
    entry.second.erase(std::unique(entry.second.begin(), entry.second.end()),
                       entry.second.end());
  }

  SmallVector<SmallVector<uint32_t>> components =
      computeStronglyConnectedComponents<uint32_t>(nodes, adjacency);

  DenseMap<uint32_t, unsigned> componentOf;
  for (unsigned component = 0; component != components.size(); ++component)
    for (uint32_t node : components[component])
      componentOf[node] = component;
  // Which successors exist matters; the order they are visited does not. Each
  // one only decrements an indegree, and the components that become ready are
  // re-ordered by the priority queue below.
  SmallVector<DenseSet<unsigned>> successors(components.size());
  SmallVector<unsigned> indegree(components.size());
  for (sim::ComputeEdgeAttr edge : edges) {
    if (!isSchedulingEdge(edge.getKind()) ||
        !componentOf.count(edge.getSource()) ||
        !componentOf.count(edge.getTarget()))
      continue;
    unsigned source = componentOf[edge.getSource()];
    unsigned target = componentOf[edge.getTarget()];
    if (source != target && successors[source].insert(target).second)
      ++indegree[target];
  }
  // Break ties on the lowest member so repeated builds are identical.
  using Ready = std::pair<uint32_t, unsigned>;
  std::priority_queue<Ready, std::vector<Ready>, std::greater<Ready>> ready;
  for (unsigned component = 0; component != components.size(); ++component)
    if (indegree[component] == 0)
      ready.emplace(components[component].front(), component);
  SmallVector<SmallVector<uint32_t>> schedule;
  while (!ready.empty()) {
    unsigned component = ready.top().second;
    ready.pop();
    schedule.push_back(components[component]);
    for (unsigned successor : successors[component])
      if (--indegree[successor] == 0)
        ready.emplace(components[successor].front(), successor);
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
  std::optional<uint32_t> findCommit(ArrayRef<DescriptorProvenance> roots,
                                     const DescriptorProvenance &root) const;

  sim::ComputeEffectAttr effectAttr(const ComputeEffect &effect) {
    return getEffectAttr(design.getContext(), effect);
  }
  void addEdge(uint32_t source, uint32_t target, sim::ComputeEdgeKind kind,
               sim::ComputeEffectAttr resource = {}) {
    edges.push_back(sim::ComputeEdgeAttr::get(design.getContext(), source,
                                              target, kind, resource));
  }

  LogicalResult buildFragments();
  void buildControlEdges();
  void buildDataEdges();
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
  SmallVector<sim::ComputeEdgeAttr> edges;
  EffectIndex watchedEffects;

  SmallVector<DescriptorProvenance> nbaRoots, eventRoots;
  SmallVector<uint32_t> nbaCommitIds, eventCommitIds;
  SmallVector<SmallVector<int64_t>> nbaSlots, nbaAccumulatorSites,
      nbaFrontierSites, eventSites;
};

bool isObserverCaptureBridge(Block *block) {
  if (!block || block->getOperations().size() != 1)
    return false;
  Operation *terminator = block->getTerminator();
  return isa<cf::BranchOp>(terminator) &&
         terminator->hasAttr("obelisk_sim.observer_capture_bridge") &&
         terminator->getNumSuccessors() == 1;
}

Block *skipObserverCaptureBridges(Block *block) {
  llvm::SmallPtrSet<Block *, 4> visited;
  while (isObserverCaptureBridge(block) && visited.insert(block).second)
    block = block->getTerminator()->getSuccessor(0);
  return block;
}

std::optional<uint32_t>
ComputeGraphBuilder::findCommit(ArrayRef<DescriptorProvenance> roots,
                                const DescriptorProvenance &root) const {
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
  auto addRoot = [](SmallVectorImpl<DescriptorProvenance> &roots,
                    const DescriptorProvenance &target) {
    roots.push_back(getRootProvenance(target));
  };
  for (Fragment &fragment : fragments)
    for (const ComputeEffect &effect : fragment.effects) {
      if (effect.kind == sim::ComputeEffectKind::NBA)
        addRoot(nbaRoots, effect.target);
      else if (effect.kind == sim::ComputeEffectKind::Trigger &&
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
  for (SmallVectorImpl<DescriptorProvenance> *roots :
       {&nbaRoots, &eventRoots}) {
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

void ComputeGraphBuilder::buildControlEdges() {
  auto fragmentID = [&](Block *block) {
    return fragmentForBlock.lookup(skipObserverCaptureBridges(block));
  };
  for (Fragment &fragment : fragments) {
    Operation *terminator = fragment.block->getTerminator();
    if (auto taskCall = dyn_cast<sim::SimTaskCallOp>(terminator)) {
      auto callee = analysis.functionIndex.find(taskCall.getCallee());
      if (callee != analysis.functionIndex.end()) {
        sim::SimFuncOp target = analysis.functions[callee->second].function;
        if (!target.getBody().empty()) {
          addEdge(fragment.id,
                  fragmentID(&target.getBody().front()),
                  sim::ComputeEdgeKind::ProcessOrder);
          uint32_t continuation = fragmentID(taskCall.getContinuation());
          for (Block &block : target.getBody())
            if (isa<sim::SimReturnOp>(block.getTerminator()))
              addEdge(fragmentID(&block), continuation,
                      sim::ComputeEdgeKind::ProcessOrder);
        }
      }
    } else {
      sim::ComputeEdgeKind controlKind =
          isSuspensionTerminator(terminator)
              ? sim::ComputeEdgeKind::Resume
              : sim::ComputeEdgeKind::ProcessOrder;
      for (Block *successor : terminator->getSuccessors())
        addEdge(fragment.id, fragmentID(successor), controlKind);
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
              sim::ComputeEdgeKind::Spawn);
    }
  }
}

void ComputeGraphBuilder::buildDataEdges() {
  for (unsigned fragment = 0; fragment != fragments.size(); ++fragment)
    for (const ComputeEffect &effect : fragments[fragment].effects)
      if (effect.kind == sim::ComputeEffectKind::Watch)
        watchedEffects.add(fragment, effect);

  // Static producers directly activate matching sensitivity consumers.
  for (Fragment &producer : fragments)
    for (const ComputeEffect &produced : producer.effects) {
      if (!isActiveProducer(produced))
        continue;
      watchedEffects.forEachAlias(produced.target, [&](IndexedEffect consumed) {
        if (provenancesAlias(produced.target, consumed.effect->target))
          addEdge(producer.id, fragments[consumed.owner].id,
                  sim::ComputeEdgeKind::Sensitivity,
                  effectAttr(*consumed.effect));
      });
    }

  // Select one stable ordering for conflicting active-region producers. This
  // is a legal SystemVerilog interleaving and makes repeated builds identical.
  EffectIndex activeEffects;
  for (unsigned fragment = 0; fragment != fragments.size(); ++fragment)
    for (const ComputeEffect &effect : fragments[fragment].effects)
      if (effect.kind != sim::ComputeEffectKind::Watch &&
          effect.kind != sim::ComputeEffectKind::NBA)
        activeEffects.add(fragment, effect);
  for (unsigned lhs = 0; lhs != fragments.size(); ++lhs)
    for (const ComputeEffect &left : fragments[lhs].effects) {
      if (left.kind == sim::ComputeEffectKind::Watch ||
          left.kind == sim::ComputeEffectKind::NBA)
        continue;
      activeEffects.forEachAlias(left.target, [&](IndexedEffect right) {
        if (right.owner <= lhs ||
            fragments[right.owner].function == fragments[lhs].function ||
            !activeEffectsConflict(left, *right.effect))
          return;
        addEdge(fragments[lhs].id, fragments[right.owner].id,
                sim::ComputeEdgeKind::Conflict,
                effectAttr(isActiveProducer(left) ? left : *right.effect));
      });
    }

  // Staged updates reach their consumers through a commit node instead of
  // activating them in the active region.
  for (Fragment &fragment : fragments)
    for (const ComputeEffect &effect : fragment.effects) {
      bool nba = effect.kind == sim::ComputeEffectKind::NBA;
      bool deferredTrigger =
          effect.kind == sim::ComputeEffectKind::Trigger && effect.deferred;
      if (!nba && !deferredTrigger)
        continue;
      ArrayRef<DescriptorProvenance> roots = nba ? nbaRoots : eventRoots;
      ArrayRef<uint32_t> ids = nba ? nbaCommitIds : eventCommitIds;
      uint32_t commit =
          ids[*findCommit(roots, getRootProvenance(effect.target))];
      addEdge(fragment.id, commit,
              nba ? sim::ComputeEdgeKind::NBAStage
                  : sim::ComputeEdgeKind::DeferredStage,
              effectAttr(effect));
    }
  for (auto [roots, ids, activate] :
       {std::tuple{ArrayRef<DescriptorProvenance>(nbaRoots),
                   ArrayRef<uint32_t>(nbaCommitIds),
                   sim::ComputeEdgeKind::NBAActivate},
        std::tuple{ArrayRef<DescriptorProvenance>(eventRoots),
                   ArrayRef<uint32_t>(eventCommitIds),
                   sim::ComputeEdgeKind::DeferredActivate}})
    for (auto [index, root] : llvm::enumerate(roots)) {
      uint32_t id = ids[index];
      watchedEffects.forEachAlias(
          root, [&, root = root](IndexedEffect consumed) {
            if (provenancesAlias(consumed.effect->target, root))
              addEdge(id, fragments[consumed.owner].id, activate,
                      effectAttr(*consumed.effect));
          });
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
            Block *continuation =
                skipObserverCaptureBridges(operation->getSuccessor(0));
            auto found = fragmentForBlock.find(continuation);
            if (found == fragmentForBlock.end())
              return operation->emitOpError(
                  "resumes into a block with no fragment");
            result.continuations[operation] = sim::ContinuationSiteAttr::get(
                design.getContext(), found->second);
          }
          if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(operation))
            result.timings[operation] = sim::TimingSiteAttr::get(
                design.getContext(), timingSite++,
                isConstantTimeValue(delay.getDelay())
                    ? sim::ComputeTimingKind::Calendar
                    : sim::ComputeTimingKind::DeadlineSlot);
          if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
            DescriptorProvenance destination =
                info.provenance.lookup(nba.getDestination());
            std::optional<uint32_t> commit =
                findCommit(nbaRoots, getRootProvenance(destination));
            if (!commit)
              return nba.emitOpError("has no generated commit node");
            uint64_t site = nbaSite++;
            sim::ComputeNBAStorageKind storage =
                getNBAStorageKind(nba, info.getFunction(), multiplicity,
                                  destination, options.vpi);
            switch (storage) {
            case sim::ComputeNBAStorageKind::FixedSlot:
              nbaSlots[*commit].push_back(site);
              break;
            case sim::ComputeNBAStorageKind::RootAccumulator:
              nbaAccumulatorSites[*commit].push_back(site);
              break;
            case sim::ComputeNBAStorageKind::DynamicFrontier:
              nbaFrontierSites[*commit].push_back(site);
              break;
            }
            sim::TimingSiteAttr delayedTiming;
            if (nba.getDelay())
              delayedTiming =
                  sim::TimingSiteAttr::get(design.getContext(), timingSite++,
                                           sim::ComputeTimingKind::DelayedNBA);
            result.nbaSites[operation] = sim::NBASiteAttr::get(
                design.getContext(), site, nbaCommitIds[*commit], storage,
                delayedTiming);
          }
          if (auto trigger = dyn_cast<sim::SimEventTriggerOp>(operation);
              trigger && trigger.getNonblocking()) {
            DescriptorProvenance destination =
                info.provenance.lookup(trigger.getEvent());
            std::optional<uint32_t> commit =
                findCommit(eventRoots, getRootProvenance(destination));
            if (!commit)
              return trigger.emitOpError(
                  "has no generated deferred-event commit node");
            uint64_t site = eventSite++;
            eventSites[*commit].push_back(site);
            sim::TimingSiteAttr delayedTiming;
            if (trigger.getDelay())
              delayedTiming =
                  sim::TimingSiteAttr::get(
                      design.getContext(), timingSite++,
                      sim::ComputeTimingKind::DelayedEvent);
            result.eventSites[operation] = sim::EventSiteAttr::get(
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

FailureOr<ArrayAttr> ComputeGraphBuilder::buildRegions() {
  auto plan =
      [&](sim::ComputeRegionKind kind,
          ArrayRef<SmallVector<uint32_t>> groups) -> FailureOr<Attribute> {
    SmallVector<Attribute> groupAttributes;
    for (ArrayRef<uint32_t> group : groups) {
      SmallVector<int64_t> ids(group.begin(), group.end());
      bool cyclic = group.size() > 1;
      if (!cyclic)
        cyclic = llvm::any_of(edges, [&](sim::ComputeEdgeAttr edge) {
          return isSchedulingEdge(edge.getKind()) &&
                 edge.getSource() == group.front() &&
                 edge.getTarget() == group.front();
        });
      sim::ComputeScheduleKind schedule = sim::ComputeScheduleKind::Acyclic;
      SmallVector<Attribute> feedback;
      if (cyclic) {
        if (hasProceduralControlCycle(group, edges)) {
          schedule = sim::ComputeScheduleKind::ControlLoop;
        } else {
          schedule = sim::ComputeScheduleKind::Convergence;
          DenseSet<uint32_t> members(group.begin(), group.end());
          llvm::SmallDenseSet<Attribute> unique;
          for (sim::ComputeEdgeAttr edge : edges)
            if (members.contains(edge.getSource()) &&
                members.contains(edge.getTarget()) &&
                edge.getKind() == sim::ComputeEdgeKind::Sensitivity &&
                edge.getResource() && unique.insert(edge.getResource()).second)
              feedback.push_back(edge.getResource());
          if (feedback.empty())
            return design.emitOpError(
                "cyclic schedule group has no state feedback to compare");
          // `edges` is already normalized, so this order is deterministic.
        }
      }
      groupAttributes.push_back(sim::ComputeGroupAttr::get(
          design.getContext(), builder.getDenseI64ArrayAttr(ids), schedule,
          builder.getArrayAttr(feedback)));
    }
    return static_cast<Attribute>(sim::ComputeRegionAttr::get(
        design.getContext(), kind, builder.getArrayAttr(groupAttributes)));
  };

  SmallVector<uint32_t> activeIds;
  SmallVector<SmallVector<uint32_t>> postponedGroups;
  for (Fragment &fragment : fragments) {
    if (fragment.function.getEntryKind() == sim::EntryKind::Final)
      postponedGroups.push_back({fragment.id});
    else
      activeIds.push_back(fragment.id);
  }
  SmallVector<SmallVector<uint32_t>> nbaGroups;
  for (uint32_t id : nbaCommitIds)
    nbaGroups.push_back({id});
  for (uint32_t id : eventCommitIds)
    nbaGroups.push_back({id});

  // Every standard event region is planned explicitly, even when a supported
  // design has no nodes in one of them.
  SmallVector<SmallVector<uint32_t>> activeGroups =
      computeSCCSchedule(activeIds, edges);
  std::pair<sim::ComputeRegionKind, ArrayRef<SmallVector<uint32_t>>> plans[] = {
      {sim::ComputeRegionKind::Active, activeGroups},
      {sim::ComputeRegionKind::NBA, nbaGroups},
      {sim::ComputeRegionKind::Observed, {}},
      {sim::ComputeRegionKind::Reactive, {}},
      {sim::ComputeRegionKind::Postponed, postponedGroups}};

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
  buildControlEdges();
  buildDataEdges();
  if (failed(buildSites(result)))
    return failure();

  SmallVector<Attribute> nodes;
  DenseMap<Operation *, SmallVector<int64_t>> functionFragments;
  for (Fragment &fragment : fragments) {
    nodes.push_back(sim::ComputeFragmentAttr::get(
        design.getContext(), fragment.id,
        FlatSymbolRefAttr::get(design.getContext(),
                               fragment.function.getSymName()),
        fragment.ordinal,
        fragment.function.getEntryKind() == sim::EntryKind::Final
            ? sim::ComputeRegionKind::Postponed
            : sim::ComputeRegionKind::Active,
        getFragmentActionKind(fragment.block->getTerminator()),
        sim::ComputeTierKind::Native, fragment.cost, fragment.lane,
        fragment.twoState, getEffectArrayAttr(builder, fragment.effects)));
    functionFragments[fragment.function.getOperation()].push_back(fragment.id);
  }
  for (auto [index, root] : llvm::enumerate(nbaRoots))
    nodes.push_back(sim::ComputeNBACommitAttr::get(
        design.getContext(), nbaCommitIds[index],
        builder.getDenseI64ArrayAttr(nbaSlots[index]),
        builder.getDenseI64ArrayAttr(nbaAccumulatorSites[index]),
        builder.getDenseI64ArrayAttr(nbaFrontierSites[index]),
        effectAttr({sim::ComputeEffectKind::Write, root})));
  for (auto [index, root] : llvm::enumerate(eventRoots))
    nodes.push_back(sim::ComputeEventCommitAttr::get(
        design.getContext(), eventCommitIds[index],
        builder.getDenseI64ArrayAttr(eventSites[index]),
        effectAttr({sim::ComputeEffectKind::Trigger, root,
                    sim::ComputeTriggerKind::None, true})));
  for (const FunctionInfo &info : analysis.functions) {
    result.effectSummaries[info.getFunction().getOperation()] =
        getEffectArrayAttr(builder, info.summary);
    result.fragmentAbis[info.getFunction().getOperation()] =
        sim::FragmentABIAttr::get(
            design.getContext(), 1,
            builder.getDenseI64ArrayAttr(
                functionFragments[info.getFunction().getOperation()]));
  }

  FailureOr<ArrayAttr> regions = buildRegions();
  if (failed(regions))
    return failure();

  result.observability = getObservability(options.vpi);
  result.graph = sim::ComputeGraphAttr::get(
      design.getContext(), 1, options.vpi, options.workers,
      builder.getArrayAttr(nodes),
      builder.getArrayAttr(SmallVector<Attribute>(edges.begin(), edges.end())),
      *regions);
  if (failed(validateComputeGraphStructure(design, result.graph)))
    return failure();
  return result;
}

} // namespace

LogicalResult validateComputeGraphStructure(sim::SimDesignOp design,
                                            sim::ComputeGraphAttr graph) {
  auto emitInvalid = [&]() {
    return design.emitOpError("contains invalid compute-graph metadata: ");
  };
  if (!graph ||
      failed(sim::ComputeGraphAttr::verifyInvariants(
          emitInvalid, graph.getVersion(), graph.getVpi(), graph.getWorkers(),
          graph.getNodes(), graph.getEdges(), graph.getRegions())))
    return failure();

  auto verifyEffect = [&](sim::ComputeEffectAttr effect) {
    return sim::ComputeEffectAttr::verifyInvariants(
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
  SmallVector<std::optional<sim::ComputeRegionKind>> nodeRegions(nodes.size());
  llvm::SmallDenseSet<Attribute> nbaCommitTargets;
  llvm::SmallDenseSet<Attribute> eventCommitTargets;
  llvm::SmallDenseSet<int64_t> nbaSites;
  llvm::SmallDenseSet<int64_t> deferredEventSites;
  for (auto [index, attribute] : llvm::enumerate(nodes)) {
    uint32_t id;
    sim::ComputeRegionKind region;
    if (auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute)) {
      if (failed(sim::ComputeFragmentAttr::verifyInvariants(
              emitInvalid, fragment.getId(), fragment.getFunction(),
              fragment.getBlock(), fragment.getRegion(), fragment.getAction(),
              fragment.getTier(), fragment.getCost(), fragment.getLane(),
              fragment.getTwoState(), fragment.getEffects())))
        return failure();
      for (Attribute effect : fragment.getEffects())
        if (failed(verifyEffect(cast<sim::ComputeEffectAttr>(effect))))
          return failure();
      if (fragment.getLane() >= graph.getWorkers())
        return design.emitOpError(
            "compute fragment lane exceeds the worker count");
      id = fragment.getId();
      region = fragment.getRegion();
      nodeKinds[index] = NodeKind::Fragment;
    } else if (auto commit = dyn_cast<sim::ComputeNBACommitAttr>(attribute)) {
      if (failed(sim::ComputeNBACommitAttr::verifyInvariants(
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
      region = sim::ComputeRegionKind::NBA;
      nodeKinds[index] = NodeKind::NBACommit;
    } else {
      auto eventCommit = cast<sim::ComputeEventCommitAttr>(attribute);
      if (failed(sim::ComputeEventCommitAttr::verifyInvariants(
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
      region = sim::ComputeRegionKind::NBA;
      nodeKinds[index] = NodeKind::EventCommit;
    }
    if (id != index)
      return design.emitOpError(
          "compute-graph nodes are not stored in dense ID order");
    nodeRegions[index] = region;
  }

  llvm::SmallDenseSet<Attribute> uniqueEdges;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (failed(sim::ComputeEdgeAttr::verifyInvariants(
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
    case sim::ComputeEdgeKind::ProcessOrder:
    case sim::ComputeEdgeKind::Resume:
    case sim::ComputeEdgeKind::Spawn:
    case sim::ComputeEdgeKind::Sensitivity:
    case sim::ComputeEdgeKind::Conflict:
      if (source != NodeKind::Fragment || target != NodeKind::Fragment)
        return design.emitOpError(
            "compute graph control/data edge does not connect fragments");
      break;
    case sim::ComputeEdgeKind::NBAStage:
      if (source != NodeKind::Fragment || target != NodeKind::NBACommit ||
          edge.getResource().getEffect() != sim::ComputeEffectKind::NBA)
        return design.emitOpError(
            "NBA stage edge has invalid endpoint or resource kinds");
      break;
    case sim::ComputeEdgeKind::NBAActivate:
      if (source != NodeKind::NBACommit || target != NodeKind::Fragment ||
          edge.getResource().getEffect() != sim::ComputeEffectKind::Watch)
        return design.emitOpError(
            "NBA activation edge has invalid endpoint or resource kinds");
      break;
    case sim::ComputeEdgeKind::DeferredStage:
      if (source != NodeKind::Fragment || target != NodeKind::EventCommit ||
          edge.getResource().getEffect() != sim::ComputeEffectKind::Trigger ||
          !edge.getResource().getDeferred())
        return design.emitOpError(
            "deferred-event stage edge has invalid endpoint or resource "
            "kinds");
      break;
    case sim::ComputeEdgeKind::DeferredActivate:
      if (source != NodeKind::EventCommit || target != NodeKind::Fragment ||
          edge.getResource().getEffect() != sim::ComputeEffectKind::Watch)
        return design.emitOpError(
            "deferred-event activation edge has invalid endpoint or resource "
            "kinds");
      break;
    }
  }

  SmallVector<bool> scheduled(nodes.size(), false);
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    if (failed(sim::ComputeRegionAttr::verifyInvariants(
            emitInvalid, region.getKind(), region.getGroups())))
      return failure();
    for (Attribute groupAttribute : region.getGroups()) {
      auto group = cast<sim::ComputeGroupAttr>(groupAttribute);
      if (failed(sim::ComputeGroupAttr::verifyInvariants(
              emitInvalid, group.getFragments(), group.getSchedule(),
              group.getFeedback())))
        return failure();
      for (Attribute effect : group.getFeedback())
        if (failed(verifyEffect(cast<sim::ComputeEffectAttr>(effect))))
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

sim::ComputeObservabilityKind getObservability(sim::ComputeVPIMode mode) {
  switch (mode) {
  case sim::ComputeVPIMode::Off:
    return sim::ComputeObservabilityKind::Invisible;
  case sim::ComputeVPIMode::Read:
    return sim::ComputeObservabilityKind::SafePoint;
  case sim::ComputeVPIMode::Full:
    return sim::ComputeObservabilityKind::ExternallyWritable;
  }
  llvm_unreachable("unknown VPI mode");
}

FailureOr<ComputeGraphResult> deriveComputeGraph(sim::SimDesignOp design,
                                                 ComputeGraphOptions options) {
  FailureOr<StateDomainAnalysis> stateDomains =
      StateDomainAnalysis::compute(design);
  if (failed(stateDomains))
    return failure();
  return ComputeGraphBuilder(design, options, *stateDomains).derive();
}

} // namespace obelisk::simlowering
