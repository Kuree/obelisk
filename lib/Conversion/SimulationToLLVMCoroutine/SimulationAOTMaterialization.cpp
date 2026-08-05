//===- SimulationAOTMaterialization.cpp - Native AOT LLVM plan --------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"
#include <numeric>

using namespace mlir;

namespace obelisk::detail {

LogicalResult makeNativeEvalPlan(
    ModuleOp module, uint32_t actorCount,
    ArrayRef<obelisk_rt_native_schedule_node> executableNodes,
    const NativeStateLayout &stateLayout,
    const NativeStaticNBAPlan &staticNBAPlan,
    const NativeStaticFanoutPlan &staticFanoutPlan,
    ArrayRef<obelisk_rt_static_actor_root> actorRoots,
    ArrayRef<NativeDirectFragment> directFragments,
    sim::ComputeGraphAttr computeGraph,
    ArrayRef<NativePeriodicClock> periodicClocks,
    ArrayRef<NativePeriodicAlias> periodicAliases,
    ArrayRef<NativePromotionRange> evalPromotionRanges, bool enableDirectState,
    bool enableStaticNBA, bool enableStaticControl, bool enableStaticFanout,
    bool enableCleanSuperstep, bool evalScheduler, bool fullyStatic,
    bool rootSlotZero, const analysis::SimulationVPIAnalysis &vpi) {
  if (actorCount == 0 || executableNodes.empty())
    return module.emitError("AOT schedule has no executable actor nodes");
  if (evalScheduler)
    module->setAttr("obelisk.eval.generated", UnitAttr::get(module.getContext()));
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = module.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  SmallVector<NativePromotionRange> promotionRanges(evalPromotionRanges);
  bool promotionRangesComplete = true;
  if (evalScheduler) {
    // Remaining unknown-plane loads belong to deliberately four-state-local
    // computations.  Promotion guards exactly the source accesses whose
    // unknown planes the selected variant suppresses; treating surviving
    // implementation loads as additional preconditions recreates a
    // whole-model scan and prevents independent promotion in the presence of
    // dormant X state.
    llvm::sort(promotionRanges, [](const auto &lhs, const auto &rhs) {
      return std::tie(lhs.bitOffset, lhs.bitWidth) <
             std::tie(rhs.bitOffset, rhs.bitWidth);
    });
    promotionRanges.erase(
        std::unique(promotionRanges.begin(), promotionRanges.end(),
                    [](const auto &lhs, const auto &rhs) {
                      return lhs.bitOffset == rhs.bitOffset &&
                             lhs.bitWidth == rhs.bitWidth;
                    }),
        promotionRanges.end());
  }
  ArrayRef<obelisk_rt_static_nba_root> nbaRoots = staticNBAPlan.roots;
  ArrayRef<obelisk_rt_static_nba_site> nbaSites = staticNBAPlan.sites;
  SmallVector<obelisk_rt_static_fanout_entry> indexedFanoutEntries;
  if (staticFanoutPlan.exact) {
    indexedFanoutEntries.assign(staticFanoutPlan.entries.begin(),
                                staticFanoutPlan.entries.end());
    for (obelisk_rt_static_fanout_entry &entry : indexedFanoutEntries) {
      auto node = llvm::find_if(executableNodes, [&](const auto &candidate) {
        return candidate.actor_slot == entry.actor_slot &&
               candidate.continuation == entry.continuation;
      });
      if (node == executableNodes.end())
        return module.emitError(
            "static fanout has no indexed compute fragment");
      entry.compute_node =
          static_cast<uint32_t>(node - executableNodes.begin());
      entry.reserved = 0;
    }
  }
  ArrayRef<obelisk_rt_static_fanout_entry> fanoutEntries = indexedFanoutEntries;
  struct GeneratedClockKernel {
    uint32_t staticState;
    uint32_t edge;
    uint64_t lowBit;
    uint64_t bitWidth;
    SmallVector<uint32_t> computeNodes;
    std::string ingressName;
    std::string activeName;

    auto key() const { return std::tuple{staticState, lowBit, bitWidth, edge}; }
  };
  SmallVector<GeneratedClockKernel> clockKernels;
  SmallVector<obelisk_rt_native_merged_fragment> mergedFragments;
  SmallVector<std::string> mergedExecutors;
  SmallVector<std::string> mergedTwoStateExecutors;
  SmallVector<SmallVector<NativePromotionRange>> mergedPromotionRanges;
  SmallVector<unsigned> periodicClosureRecords;
  SmallVector<unsigned> periodicEntryRecords;
  struct DynamicEvalNBA {
    uint32_t rootIndex;
    uint64_t site;
    uint64_t width;
    std::string offsetName;
    std::string valueName;
    std::string unknownName;
    std::string validName;
  };
  SmallVector<DynamicEvalNBA> dynamicEvalNBAs;
  if (staticFanoutPlan.exact) {
    for (const obelisk_rt_static_fanout_entry &entry : fanoutEntries)
      clockKernels.push_back({entry.static_state,
                              entry.edge,
                              entry.low_bit,
                              entry.bit_width,
                              {},
                              {}});
    llvm::sort(clockKernels, [](const auto &lhs, const auto &rhs) {
      return lhs.key() < rhs.key();
    });
    clockKernels.erase(std::unique(clockKernels.begin(), clockKernels.end(),
                                   [](const auto &lhs, const auto &rhs) {
                                     return lhs.key() == rhs.key();
                                   }),
                       clockKernels.end());
    for (auto [index, kernel] : llvm::enumerate(clockKernels))
      kernel.ingressName =
          evalScheduler
              ? "__obelisk_aot_model_ingress_v1"
              : (Twine("__obelisk_aot_clock_ingress_v1_") + Twine(index))
                    .str();
    for (auto [index, kernel] : llvm::enumerate(clockKernels))
      kernel.activeName =
          (Twine("__obelisk_aot_model_active_v1_") + Twine(index)).str();

    for (obelisk_rt_static_fanout_entry &entry : indexedFanoutEntries) {
      if (evalScheduler) {
        const auto &executable = executableNodes[entry.compute_node];
        auto owner = llvm::lower_bound(
            clockKernels,
            std::tuple{entry.static_state, entry.low_bit, entry.bit_width,
                       static_cast<uint32_t>(entry.edge)},
            [](const GeneratedClockKernel &kernel, const auto &key) {
              return kernel.key() < key;
            });
        if (owner == clockKernels.end() ||
            owner->key() !=
                std::tuple{entry.static_state, entry.low_bit, entry.bit_width,
                           static_cast<uint32_t>(entry.edge)})
          return module.emitError("could not index generated eval trigger"),
                 failure();
        entry.kernel = static_cast<uint32_t>(owner - clockKernels.begin());
        bool forwardingAlias = llvm::any_of(
            periodicAliases, [&](const NativePeriodicAlias &alias) {
              if (alias.sourceStaticState != entry.static_state ||
                  alias.forwardingActorSlot != executable.actor_slot ||
                  alias.forwardingContinuation != executable.continuation)
                return false;
              auto bound = llvm::find_if(
                  stateLayout.bounds, [&](const auto &candidate) {
                    return candidate.handleID == entry.static_state;
                  });
              if (bound == stateLayout.bounds.end() ||
                  alias.sourceBitOffset < bound->offset)
                return false;
              uint64_t localBit = alias.sourceBitOffset - bound->offset;
              return entry.low_bit <= localBit &&
                     localBit - entry.low_bit < entry.bit_width;
            });
        if (forwardingAlias) {
          // run_until mirrors this proven continuous clock alias before
          // seeding target fanout. The forwarding process has no independent
          // activation body and therefore must not consume a merged-ready bit.
          entry.reserved = 2;
          entry.merged_bit = 0;
          continue;
        }
        auto direct = llvm::find_if(
            directFragments, [&](const NativeDirectFragment &candidate) {
              return llvm::is_contained(
                  candidate.sourceOwners,
                  std::pair{executable.actor_slot, executable.continuation});
            });
        if (direct != directFragments.end() &&
            llvm::count_if(
                directFragments, [&](const NativeDirectFragment &candidate) {
                  return llvm::is_contained(
                      candidate.sourceOwners,
                      std::pair{executable.actor_slot,
                                executable.continuation});
                }) != 1)
          return module.emitError(
              "stable eval source owner maps to multiple generated bodies");
        if (direct == directFragments.end())
          direct =
              llvm::find_if(directFragments, [&](const auto &candidate) {
                return candidate.actorSlot == executable.actor_slot &&
                       candidate.continuation == executable.continuation;
              });
        if (direct == directFragments.end()) {
          auto fragments = staticFanoutPlan.fragments.find(
              {executable.actor_slot, executable.continuation});
          if (fragments != staticFanoutPlan.fragments.end() &&
              !fragments->second.empty()) {
            auto best = directFragments.end();
            size_t bestSize = SIZE_MAX;
            bool ambiguous = false;
            for (auto candidate = directFragments.begin();
                 candidate != directFragments.end(); ++candidate) {
              // Continuation IDs can be rebuilt after the static fanout plan,
              // but current-epoch fragment containment within the same actor
              // still identifies the exact activation body.  Cross-actor
              // fusion requires an explicit fusion-group certificate below;
              // numeric fragment containment is not one.
              if (candidate->actorSlot != executable.actor_slot)
                continue;
              if (!llvm::all_of(fragments->second, [&](uint32_t fragment) {
                    return llvm::is_contained(candidate->fragmentIDs,
                                              fragment);
                  }))
                continue;
              if (best == directFragments.end() ||
                  candidate->fragmentIDs.size() < bestSize) {
                best = candidate;
                bestSize = candidate->fragmentIDs.size();
                ambiguous = false;
              } else if (candidate->fragmentIDs.size() == bestSize)
                ambiguous = true;
            }
            direct = ambiguous ? directFragments.end() : best;
          }
        }
        if (direct == directFragments.end()) {
          auto fragments = staticFanoutPlan.fragments.find(
              {executable.actor_slot, executable.continuation});
          if (fragments != staticFanoutPlan.fragments.end()) {
            auto best = directFragments.end();
            bool ambiguous = false;
            for (auto candidate = directFragments.begin();
                 candidate != directFragments.end(); ++candidate) {
              if (fragments->second.empty() ||
                  !llvm::all_of(fragments->second, [&](uint32_t fragment) {
                    return llvm::is_contained(candidate->ownershipAnchors,
                                              fragment);
                  }))
                continue;
              if (best != directFragments.end() &&
                  best->wrapper != candidate->wrapper) {
                ambiguous = true;
                break;
              }
              best = candidate;
            }
            direct = ambiguous ? directFragments.end() : best;
          }
        }
        auto merged = llvm::find_if(
            mergedFragments, [&](const auto &candidate) {
              size_t index = static_cast<size_t>(&candidate -
                                                 mergedFragments.data());
              if (direct != directFragments.end())
                return index < mergedExecutors.size() &&
                       mergedExecutors[index] == direct->wrapper;
              return candidate.actor_slot == executable.actor_slot &&
                     candidate.continuation == executable.continuation;
            });
        if (merged != mergedFragments.end()) {
          entry.merged_bit = merged->bit;
          size_t mergedIndex =
              static_cast<size_t>(merged - mergedFragments.begin());
          entry.reserved = !mergedExecutors[mergedIndex].empty() ? 1u : 0u;
          merged->compute_node =
              std::min(merged->compute_node, entry.compute_node);
          continue;
        }
        entry.merged_bit = static_cast<uint32_t>(mergedFragments.size());
        owner->computeNodes.push_back(entry.compute_node);
        entry.reserved = direct != directFragments.end() ? 1u : 0u;
        uint32_t ownerActor = direct != directFragments.end()
                                  ? direct->actorSlot
                                  : executable.actor_slot;
        uint32_t ownerContinuation =
            direct != directFragments.end() ? direct->continuation
                                            : executable.continuation;
        mergedFragments.push_back(
            {ownerActor, ownerContinuation, entry.kernel,
             entry.merged_bit, entry.compute_node, 0, nullptr});
        mergedExecutors.push_back(
            direct != directFragments.end() ? direct->wrapper : std::string{});
        mergedTwoStateExecutors.push_back(
            direct != directFragments.end() ? direct->twoStateWrapper
                                            : std::string{});
        mergedPromotionRanges.emplace_back();
        if (direct != directFragments.end())
          llvm::append_range(mergedPromotionRanges.back(),
                             direct->promotionRanges);
        continue;
      }
      auto found = llvm::lower_bound(
          clockKernels,
          std::tuple{entry.static_state, entry.low_bit, entry.bit_width,
                     static_cast<uint32_t>(entry.edge)},
          [](const GeneratedClockKernel &kernel, const auto &key) {
            return kernel.key() < key;
          });
      if (found == clockKernels.end() ||
          found->key() != std::tuple{entry.static_state, entry.low_bit,
                                     entry.bit_width,
                                     static_cast<uint32_t>(entry.edge)})
        return module.emitError("could not index generated clock kernel");
      entry.kernel = static_cast<uint32_t>(found - clockKernels.begin());
      auto node = llvm::find(found->computeNodes, entry.compute_node);
      if (node == found->computeNodes.end()) {
        entry.merged_bit = static_cast<uint32_t>(found->computeNodes.size());
        found->computeNodes.push_back(entry.compute_node);
        const auto &executable = executableNodes[entry.compute_node];
        auto direct =
            llvm::find_if(directFragments, [&](const auto &candidate) {
              return candidate.actorSlot == executable.actor_slot &&
                     candidate.continuation == executable.continuation;
            });
        mergedFragments.push_back(
            {executable.actor_slot, executable.continuation, entry.kernel,
             entry.merged_bit, entry.compute_node, 0, nullptr});
        mergedExecutors.push_back(
            direct != directFragments.end() ? direct->wrapper
                                            : std::string{});
        mergedTwoStateExecutors.push_back(
            direct != directFragments.end() ? direct->twoStateWrapper
                                            : std::string{});
        mergedPromotionRanges.emplace_back();
        if (direct != directFragments.end())
          llvm::append_range(mergedPromotionRanges.back(),
                             direct->promotionRanges);
      } else {
        entry.merged_bit =
            static_cast<uint32_t>(node - found->computeNodes.begin());
      }
    }
    if (evalScheduler && !mergedFragments.empty()) {
      SmallVector<unsigned> order(mergedFragments.size());
      std::iota(order.begin(), order.end(), 0u);
      llvm::sort(order, [&](unsigned lhs, unsigned rhs) {
        const auto &left = mergedFragments[lhs];
        const auto &right = mergedFragments[rhs];
        return std::tuple{left.compute_node, left.actor_slot,
                          left.continuation} <
               std::tuple{right.compute_node, right.actor_slot,
                          right.continuation};
      });
      SmallVector<uint32_t> remap(mergedFragments.size());
      SmallVector<obelisk_rt_native_merged_fragment> rankedFragments;
      SmallVector<std::string> rankedExecutors;
      SmallVector<std::string> rankedTwoStateExecutors;
      SmallVector<SmallVector<NativePromotionRange>> rankedPromotionRanges;
      rankedFragments.reserve(order.size());
      rankedExecutors.reserve(order.size());
      rankedTwoStateExecutors.reserve(order.size());
      rankedPromotionRanges.reserve(order.size());
      for (auto [rank, oldIndex] : llvm::enumerate(order)) {
        remap[oldIndex] = static_cast<uint32_t>(rank);
        auto fragment = mergedFragments[oldIndex];
        fragment.bit = static_cast<uint32_t>(rank);
        rankedFragments.push_back(fragment);
        rankedExecutors.push_back(std::move(mergedExecutors[oldIndex]));
        rankedTwoStateExecutors.push_back(
            std::move(mergedTwoStateExecutors[oldIndex]));
        rankedPromotionRanges.push_back(
            std::move(mergedPromotionRanges[oldIndex]));
      }
      mergedFragments = std::move(rankedFragments);
      mergedExecutors = std::move(rankedExecutors);
      mergedTwoStateExecutors = std::move(rankedTwoStateExecutors);
      mergedPromotionRanges = std::move(rankedPromotionRanges);
      for (auto &entry : indexedFanoutEntries)
        if (entry.reserved != 2)
          entry.merged_bit = remap[entry.merged_bit];
    }
    for (const NativeDirectFragment &direct : directFragments)
      if (direct.tier2Convergence)
        for (StringRef name : {StringRef(direct.wrapper),
                               StringRef(direct.twoStateWrapper)})
          if (!name.empty())
            if (auto function =
                    module.lookupSymbol<LLVM::LLVMFuncOp>(name))
              function->setAttr("obelisk.eval.tier2_convergence",
                                UnitAttr::get(context));
    fanoutEntries = indexedFanoutEntries;
  }
  // Promotion is a property of the periodic execution closure, not of every
  // four-state object in the design. Seed the closure from every physical
  // periodic trigger (including proven port aliases), then follow exact
  // activation edges, preserving packed ranges across root-specific NBA
  // commit nodes. Dormant asynchronous owners remain outside this scan and
  // are handled by the runtime handoff/invalidation path.
  if (evalScheduler && !mergedFragments.empty() && computeGraph) {
    llvm::SmallDenseSet<unsigned, 32> closure;
    auto clockTouches = [&](const NativePeriodicClock &clock,
                            const obelisk_rt_static_fanout_entry &entry) {
      if (entry.static_state != clock.staticState || entry.bit_width == 0)
        return false;
      auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
        return candidate.handleID == clock.staticState;
      });
      if (bound == stateLayout.bounds.end() ||
          clock.bitOffset < bound->offset ||
          clock.bitOffset - bound->offset >= bound->width)
        return false;
      uint64_t bit = clock.bitOffset - bound->offset;
      return entry.low_bit <= bit && bit - entry.low_bit < entry.bit_width;
    };
    for (const auto &entry : fanoutEntries) {
      if (entry.reserved == 0 || entry.reserved == 2 ||
          entry.merged_bit >= mergedFragments.size())
        continue;
      bool periodic = llvm::any_of(periodicClocks, [&](const auto &clock) {
        return clockTouches(clock, entry);
      });
      periodic |= llvm::any_of(periodicAliases, [&](const auto &alias) {
        return entry.static_state == alias.targetStaticState &&
               entry.low_bit == 0 && entry.bit_width != 0;
      });
      if (periodic)
        closure.insert(entry.merged_bit);
    }
    const llvm::SmallDenseSet<unsigned, 32> periodicSeeds = closure;
    periodicEntryRecords.assign(periodicSeeds.begin(), periodicSeeds.end());
    llvm::sort(periodicEntryRecords);
    llvm::DenseMap<uint32_t, unsigned> fragmentOwners;
    SmallVector<SmallVector<uint32_t>> ownerFragments(mergedFragments.size());
    llvm::SmallDenseSet<uint32_t, 16> ambiguousFragments;
    for (auto [recordIndex, executor] : llvm::enumerate(mergedExecutors)) {
      if (executor.empty())
        continue;
      auto direct = llvm::find_if(directFragments, [&](const auto &candidate) {
        return candidate.wrapper == executor;
      });
      if (direct == directFragments.end())
        continue;
      SmallVector<uint32_t> ownedFragments;
      llvm::append_range(ownedFragments, direct->fragmentIDs);
      llvm::append_range(ownedFragments, direct->ownershipAnchors);
      llvm::sort(ownedFragments);
      ownedFragments.erase(
          std::unique(ownedFragments.begin(), ownedFragments.end()),
          ownedFragments.end());
      for (uint32_t fragment : ownedFragments) {
        ownerFragments[recordIndex].push_back(fragment);
        auto [found, inserted] = fragmentOwners.try_emplace(fragment,
                                                            recordIndex);
        if (!inserted && found->second != recordIndex)
          ambiguousFragments.insert(fragment);
      }
    }
    for (uint32_t fragment : ambiguousFragments)
      fragmentOwners.erase(fragment);

    llvm::SmallDenseSet<uint32_t, 64> reachableNodes;
    for (unsigned owner : closure)
      for (uint32_t fragment : ownerFragments[owner])
        reachableNodes.insert(fragment);
    auto rangesOverlap = [](sim::ComputeEffectAttr lhs,
                            sim::ComputeEffectAttr rhs) {
      if (!lhs || !rhs || lhs.getResource() != rhs.getResource() ||
          lhs.getTarget() != rhs.getTarget() ||
          lhs.getDescriptor() != rhs.getDescriptor() ||
          lhs.getFormal() != rhs.getFormal() || lhs.getWidth() == 0 ||
          rhs.getWidth() == 0)
        return false;
      return lhs.getLow() < rhs.getLow() + rhs.getWidth() &&
             rhs.getLow() < lhs.getLow() + lhs.getWidth();
    };
    auto reachOwner = [&](uint32_t fragment) {
      auto target = fragmentOwners.find(fragment);
      if (target == fragmentOwners.end())
        return false;
      if (!closure.insert(target->second).second)
        return false;
      for (uint32_t owned : ownerFragments[target->second])
        reachableNodes.insert(owned);
      return true;
    };
    bool changed;
    do {
      changed = false;
      for (Attribute attribute : computeGraph.getEdges()) {
        auto edge = cast<sim::ComputeEdgeAttr>(attribute);
        if (!reachableNodes.contains(edge.getSource()))
          continue;
        if (edge.getKind() == sim::ComputeEdgeKind::Sensitivity ||
            edge.getKind() == sim::ComputeEdgeKind::Resume) {
          changed |= reachableNodes.insert(edge.getTarget()).second;
          changed |= reachOwner(edge.getTarget());
          continue;
        }
        if (edge.getKind() != sim::ComputeEdgeKind::NBAStage)
          continue;
        for (Attribute candidate : computeGraph.getEdges()) {
          auto activation = cast<sim::ComputeEdgeAttr>(candidate);
          if (activation.getKind() != sim::ComputeEdgeKind::NBAActivate ||
              activation.getSource() != edge.getTarget() ||
              !rangesOverlap(edge.getResource(), activation.getResource()))
            continue;
          changed |= reachableNodes.insert(activation.getTarget()).second;
          changed |= reachOwner(activation.getTarget());
        }
      }
    } while (changed);

    periodicClosureRecords.assign(closure.begin(), closure.end());
    llvm::sort(periodicClosureRecords);

    SmallVector<NativePromotionRange> periodicRanges;
    // Entry owners select the outer periodic variant. Downstream owners retain
    // independent promotion latches in the transient coordinator.
    for (unsigned recordIndex : periodicSeeds) {
      if (recordIndex >= mergedExecutors.size())
        continue;
      auto direct = llvm::find_if(directFragments, [&](const auto &candidate) {
        return candidate.wrapper == mergedExecutors[recordIndex];
      });
      if (direct != directFragments.end())
        llvm::append_range(periodicRanges, direct->promotionRanges);
    }
    if (!periodicRanges.empty()) {
      llvm::sort(periodicRanges, [](const auto &lhs, const auto &rhs) {
        return std::tie(lhs.bitOffset, lhs.bitWidth) <
               std::tie(rhs.bitOffset, rhs.bitWidth);
      });
      periodicRanges.erase(
          std::unique(periodicRanges.begin(), periodicRanges.end(),
                      [](const auto &lhs, const auto &rhs) {
                        return lhs.bitOffset == rhs.bitOffset &&
                               lhs.bitWidth == rhs.bitWidth;
                      }),
          periodicRanges.end());
      promotionRanges = std::move(periodicRanges);
    }
  }
  // A four-state owner only taints the common NBA barrier when its activation
  // can reach an NBA staging fragment before that barrier.  Read-only
  // observers (including termination/checkpoint monitors) may remain on a
  // four-state route without forcing every fixed root through unknown-plane
  // merge and edge detection.  Compute this property on physical graph
  // fragments, then project it onto exclusive generated owners.  Reverse
  // sensitivity closure handles an upstream four-state writer waking a later
  // NBA producer; control/conflict ordering alone does not carry data and must
  // not create false taint.
  uint32_t nbaTaintWordCount = static_cast<uint32_t>((nbaRoots.size() + 63) / 64);
  SmallVector<SmallVector<uint64_t>> recordNBATaintMasks(
      mergedFragments.size(), SmallVector<uint64_t>(nbaTaintWordCount, 0));
  llvm::SmallDenseSet<unsigned, 32> nbaTaintedRecords;
  if (evalScheduler && !mergedFragments.empty()) {
    auto fillAllRoots = [&](MutableArrayRef<uint64_t> mask) {
      llvm::fill(mask, UINT64_MAX);
      if (!mask.empty() && (nbaRoots.size() & 63) != 0)
        mask.back() = (uint64_t{1} << (nbaRoots.size() & 63)) - 1;
    };
    if (!computeGraph) {
      for (unsigned index = 0; index != mergedFragments.size(); ++index) {
        fillAllRoots(recordNBATaintMasks[index]);
        nbaTaintedRecords.insert(index);
      }
    } else {
      llvm::DenseMap<uint32_t, SmallVector<uint64_t>> fragmentNBARoots;
      for (Attribute attribute : computeGraph.getNodes()) {
        auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
        if (!fragment)
          continue;
        auto &mask = fragmentNBARoots[fragment.getId()];
        mask.resize(nbaTaintWordCount, 0);
        for (Attribute raw : fragment.getEffects()) {
          auto effect = cast<sim::ComputeEffectAttr>(raw);
          if (effect.getEffect() != sim::ComputeEffectKind::NBA)
            continue;
          auto handle = stateLayout.storage.find(effect.getDescriptor());
          obelisk_rt_stable_handle_v1 decoded{};
          if (handle == stateLayout.storage.end() ||
              !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
              decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC) {
            fillAllRoots(mask);
            continue;
          }
          auto root = llvm::find_if(nbaRoots, [&](const auto &candidate) {
            return candidate.static_state == decoded.id;
          });
          if (root == nbaRoots.end()) {
            fillAllRoots(mask);
            continue;
          }
          size_t rootIndex = static_cast<size_t>(root - nbaRoots.begin());
          mask[rootIndex / 64] |= uint64_t{1} << (rootIndex % 64);
        }
      }
      bool changed;
      do {
        changed = false;
        for (Attribute attribute : computeGraph.getEdges()) {
          auto edge = cast<sim::ComputeEdgeAttr>(attribute);
          if (edge.getKind() != sim::ComputeEdgeKind::Sensitivity &&
              edge.getKind() != sim::ComputeEdgeKind::Resume &&
              edge.getKind() != sim::ComputeEdgeKind::Spawn)
            continue;
          auto target = fragmentNBARoots.find(edge.getTarget());
          if (target == fragmentNBARoots.end())
            continue;
          auto &source = fragmentNBARoots[edge.getSource()];
          source.resize(nbaTaintWordCount, 0);
          for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
            uint64_t merged = source[word] | target->second[word];
            changed |= merged != source[word];
            source[word] = merged;
          }
        }
      } while (changed);
      for (auto [recordIndex, executor] : llvm::enumerate(mergedExecutors)) {
        auto direct = llvm::find_if(directFragments, [&](const auto &candidate) {
          return candidate.wrapper == executor;
        });
        if (direct == directFragments.end()) {
          fillAllRoots(recordNBATaintMasks[recordIndex]);
          nbaTaintedRecords.insert(static_cast<unsigned>(recordIndex));
          continue;
        }
        for (uint32_t fragment : direct->fragmentIDs)
          if (auto roots = fragmentNBARoots.find(fragment);
              roots != fragmentNBARoots.end())
            for (uint32_t word = 0; word != nbaTaintWordCount; ++word)
              recordNBATaintMasks[recordIndex][word] |= roots->second[word];
        if (llvm::any_of(recordNBATaintMasks[recordIndex],
                         [](uint64_t word) { return word != 0; }))
          nbaTaintedRecords.insert(static_cast<unsigned>(recordIndex));
      }
    }
  }
  auto ownerMayTaintNBA = [&](unsigned recordIndex) {
    return nbaTaintedRecords.contains(recordIndex);
  };
  auto markOwnerNBATaint = [&](unsigned recordIndex) {
    if (!ownerMayTaintNBA(recordIndex) || nbaTaintWordCount == 0)
      return;
    Value taintBase = LLVM::AddressOfOp::create(
        builder, location, pointer,
        "__obelisk_eval_step_four_state_nba_roots_v1");
    for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
      uint64_t mask = recordNBATaintMasks[recordIndex][word];
      if (mask == 0)
        continue;
      Value address = byteGEP(builder, location, taintBase,
                              uint64_t{word} * sizeof(uint64_t));
      Value old = LLVM::LoadOp::create(builder, location, i64, address, 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::OrIOp::create(builder, location, old,
                               llvmConstant(builder, location, i64, mask)),
          address, 8);
    }
  };
  // Static time/region control and exact actor fanout are independent of VPI
  // reads. Writable VPI hands dirty roots and the affected event slot to the
  // existing guarded state/control paths; the exact dependency table remains
  // valid and can continue to wake native actors without subscriptions.
  bool staticControlEnabled =
      enableStaticControl && fullyStatic && vpi.hasComputeGraph();
  bool staticFanoutEnabled = enableStaticFanout && staticFanoutPlan.exact &&
                             fullyStatic && vpi.hasComputeGraph();
  bool guardedFanoutEnabled = !staticFanoutEnabled && staticFanoutPlan.exact &&
                              fullyStatic && vpi.hasComputeGraph();
  bool guardedSpecializationEnabled =
      vpi.allowsWrite() && (enableDirectState || enableStaticNBA);
  bool cleanSuperstepEnabled = enableCleanSuperstep && staticControlEnabled &&
                               staticFanoutPlan.exact && fullyStatic;
  // The coordinator is selected only for a certified clean superstep. Hybrid
  // or guarded schedules keep the same static fanout table but use its exact
  // compute-node fallback identities transactionally.
  if (!cleanSuperstepEnabled) {
    clockKernels.clear();
    mergedFragments.clear();
    mergedExecutors.clear();
    mergedTwoStateExecutors.clear();
    mergedPromotionRanges.clear();
    for (obelisk_rt_static_fanout_entry &entry : indexedFanoutEntries) {
      entry.kernel = 0;
      entry.merged_bit = 0;
    }
  }
  uint64_t graphLayoutChecksum = 0;
  if (auto image =
          module->getAttrOfType<DenseI8ArrayAttr>("obelisk.bytecode.image")) {
    ArrayRef<int8_t> bytes = image.asArrayRef();
    if (bytes.size() < 40)
      return module.emitError("embedded bytecode checksum is truncated");
    for (unsigned byte = 0; byte != 8; ++byte)
      graphLayoutChecksum |= uint64_t{static_cast<uint8_t>(bytes[32 + byte])}
                             << (byte * 8);
  }
  Type stateType = LLVM::LLVMArrayType::get(pointer, actorCount);
  constexpr StringLiteral stateName = "__obelisk_aot_schedule_state_v1";
  constexpr StringLiteral nodesName = "__obelisk_aot_schedule_nodes_v1";
  constexpr StringLiteral nbaRootsName = "__obelisk_aot_nba_roots_v1";
  constexpr StringLiteral nbaSitesName = "__obelisk_aot_nba_sites_v1";
  constexpr StringLiteral nbaDirtyRootsName =
      "__obelisk_aot_nba_dirty_roots_v1";
  constexpr StringLiteral nbaDirtySummaryName =
      "__obelisk_aot_nba_dirty_summary_v1";
  constexpr StringLiteral fanoutName = "__obelisk_aot_static_fanout_v1";
  constexpr StringLiteral actorRootsName =
      "__obelisk_aot_static_actor_roots_v1";
  constexpr StringLiteral clockKernelsName = "__obelisk_aot_clock_kernels_v1";
  constexpr StringLiteral mergedFragmentsName =
      "__obelisk_aot_merged_fragments_v1";
  constexpr StringLiteral bindName = "__obelisk_aot_schedule_bind_v1";
  constexpr StringLiteral runName = "__obelisk_aot_schedule_run_v1";
  constexpr StringLiteral snapshotName = "__obelisk_aot_schedule_snapshot_v1";
  constexpr StringLiteral nbaCommitName = "__obelisk_aot_static_nba_commit_v1";
  constexpr StringLiteral nbaKnownName = "__obelisk_eval_nba_known_v1";
  constexpr StringLiteral coordinatorName =
      "__obelisk_aot_timeslot_coordinator_v1";
  constexpr StringLiteral evalCoordinatorName =
      "__obelisk_eval_fast_coordinator_v1";
  constexpr StringLiteral evalTwoStateCoordinatorName =
      "__obelisk_eval_fast_coordinator_two_state_v1";
  constexpr StringLiteral evalSteadyTwoStateCoordinatorName =
      "__obelisk_eval_steady_two_state_coordinator_v1";
  constexpr StringLiteral evalPeriodicTwoStateCoordinatorName =
      "__obelisk_eval_periodic_two_state_coordinator_v1";
  constexpr StringLiteral evalHybridCoordinatorName =
      "__obelisk_eval_fast_coordinator_hybrid_v1";
  constexpr StringLiteral evalStepFourStateFallbackName =
      "__obelisk_eval_step_four_state_fallback_v1";
  constexpr StringLiteral evalStepFourStateNBARootsName =
      "__obelisk_eval_step_four_state_nba_roots_v1";
  constexpr StringLiteral evalFastNBALatchedName =
      "__obelisk_eval_fast_nba_latched_v1";
  constexpr StringLiteral evalFastNBARootsName =
      "__obelisk_eval_fast_nba_roots_v1";
  constexpr StringLiteral periodicTerminationName =
      "__obelisk_periodic_termination_v1";
  constexpr StringLiteral promotionReadyName =
      "__obelisk_eval_promotion_ready_v1";
  constexpr StringLiteral promotionLatchedName =
      "__obelisk_eval_promotion_latched_v1";
  constexpr StringLiteral periodicPromotionLatchedName =
      "__obelisk_eval_periodic_promotion_latched_v1";
  constexpr StringLiteral periodicEntryPromotionLatchedName =
      "__obelisk_eval_periodic_entry_promotion_latched_v1";
  constexpr StringLiteral periodicPromotionReadyName =
      "__obelisk_eval_periodic_promotion_ready_v1";
  constexpr StringLiteral promotionKernelLatchedName =
      "__obelisk_eval_kernel_promotion_latched_v1";
  constexpr StringLiteral promotionPendingMaskName =
      "__obelisk_eval_promotion_pending_mask_v1";
  constexpr StringLiteral promotionInvalidateName =
      "__obelisk_eval_promotion_invalidate_v1";
  constexpr StringLiteral promotionQueryName =
      "__obelisk_eval_promotion_query_v1";
  constexpr StringLiteral planName = "__obelisk_aot_schedule_plan_v1";
  SmallVector<std::string> promotionKernelReadyNames(mergedFragments.size());
  bool periodicPromotionComplete = false;
  bool periodicPromotionHasPathGuards = false;
  uint64_t periodicPromotionMask = 0;
  bool periodicEntryPromotionComplete = false;

  builder.setInsertionPointToStart(module.getBody());
  auto state = LLVM::GlobalOp::create(builder, location, stateType, false,
                                      LLVM::Linkage::Internal, stateName,
                                      Attribute{}, 8);
  Block *initializer = new Block;
  state.getInitializerRegion().push_back(initializer);
  builder.setInsertionPointToStart(initializer);
  LLVM::ReturnOp::create(builder, location,
                         LLVM::ZeroOp::create(builder, location, stateType));

  builder.setInsertionPointToStart(module.getBody());
  auto periodicTermination = LLVM::GlobalOp::create(
      builder, location, pointer, false, LLVM::Linkage::Internal,
      periodicTerminationName, Attribute{}, 8);
  Block *periodicTerminationInitializer = new Block;
  periodicTermination.getInitializerRegion().push_back(
      periodicTerminationInitializer);
  builder.setInsertionPointToStart(periodicTerminationInitializer);
  LLVM::ReturnOp::create(
      builder, location,
      LLVM::ZeroOp::create(builder, location, pointer));

  if (evalScheduler) {
    builder.setInsertionPointToStart(module.getBody());
    (void)LLVM::GlobalOp::create(
        builder, location, builder.getI8Type(), false,
        LLVM::Linkage::Internal, evalStepFourStateFallbackName,
        builder.getI8IntegerAttr(0), 1);
    builder.setInsertionPointToStart(module.getBody());
    (void)LLVM::GlobalOp::create(
        builder, location, builder.getI8Type(), false,
        LLVM::Linkage::Internal, evalFastNBALatchedName,
        builder.getI8IntegerAttr(0), 1);
    if (nbaTaintWordCount != 0) {
      Type taintType =
          LLVM::LLVMArrayType::get(i64, nbaTaintWordCount);
      builder.setInsertionPointToStart(module.getBody());
      auto taint = LLVM::GlobalOp::create(
          builder, location, taintType, false, LLVM::Linkage::Internal,
          evalStepFourStateNBARootsName, Attribute{}, 8);
      Block *initializer = new Block;
      taint.getInitializerRegion().push_back(initializer);
      builder.setInsertionPointToStart(initializer);
      LLVM::ReturnOp::create(
          builder, location,
          LLVM::ZeroOp::create(builder, location, taintType));
      builder.setInsertionPointToStart(module.getBody());
      auto fastRoots = LLVM::GlobalOp::create(
          builder, location, taintType, false, LLVM::Linkage::Internal,
          evalFastNBARootsName, Attribute{}, 8);
      Block *fastRootsInitializer = new Block;
      fastRoots.getInitializerRegion().push_back(fastRootsInitializer);
      builder.setInsertionPointToStart(fastRootsInitializer);
      LLVM::ReturnOp::create(
          builder, location,
          LLVM::ZeroOp::create(builder, location, taintType));
    }
    builder.setInsertionPointToStart(module.getBody());
    auto promotionLatched = LLVM::GlobalOp::create(
        builder, location, builder.getI8Type(), false,
        LLVM::Linkage::Internal, promotionLatchedName,
        builder.getI8IntegerAttr(0), 1);

    builder.setInsertionPointToStart(module.getBody());
    auto periodicPromotionLatched = LLVM::GlobalOp::create(
        builder, location, builder.getI8Type(), false,
        LLVM::Linkage::Internal, periodicPromotionLatchedName,
        builder.getI8IntegerAttr(0), 1);
    builder.setInsertionPointToStart(module.getBody());
    auto periodicEntryPromotionLatched = LLVM::GlobalOp::create(
        builder, location, builder.getI8Type(), false,
        LLVM::Linkage::Internal, periodicEntryPromotionLatchedName,
        builder.getI8IntegerAttr(0), 1);

    Type kernelLatchType =
        LLVM::LLVMArrayType::get(builder.getI8Type(), mergedFragments.size());
    builder.setInsertionPointToStart(module.getBody());
    auto promotionKernelLatched = LLVM::GlobalOp::create(
        builder, location, kernelLatchType, false, LLVM::Linkage::Internal,
        promotionKernelLatchedName, Attribute{}, 1);
    Block *kernelLatchInitializer = new Block;
    promotionKernelLatched.getInitializerRegion().push_back(
        kernelLatchInitializer);
    builder.setInsertionPointToStart(kernelLatchInitializer);
    LLVM::ReturnOp::create(
        builder, location,
        LLVM::ZeroOp::create(builder, location, kernelLatchType));

    uint64_t initialPromotionPendingMask = 0;
    for (auto [index, executor] : llvm::enumerate(mergedTwoStateExecutors))
      if (!executor.empty() && mergedFragments[index].bit < 64)
        initialPromotionPendingMask |=
            uint64_t{1} << mergedFragments[index].bit;
    builder.setInsertionPointToStart(module.getBody());
    auto promotionPendingMask = LLVM::GlobalOp::create(
        builder, location, i64, false, LLVM::Linkage::Internal,
        promotionPendingMaskName,
        builder.getI64IntegerAttr(initialPromotionPendingMask), 8);

    // Scan an outlined owner's exact canonical closure independently. A
    // dormant X-valued instance therefore cannot keep unrelated clock owners
    // on their four-state route.
    for (auto [index, twoStateExecutor] :
         llvm::enumerate(mergedTwoStateExecutors)) {
      // The current generated dispatcher and promotion ABI use one ready
      // word. Owners outside that word remain on the runtime/fallback route;
      // never form a C++ shift for them while emitting the compact hot path.
      if (twoStateExecutor.empty() || mergedFragments[index].bit >= 64)
        continue;
      std::string readyName =
          (Twine("__obelisk_eval_kernel_promotion_ready_v1_") + Twine(index))
              .str();
      promotionKernelReadyNames[index] = readyName;
      builder.setInsertionPointToEnd(module.getBody());
      auto ready = LLVM::LLVMFuncOp::create(
          builder, location, readyName,
          LLVM::LLVMFunctionType::get(builder.getI1Type(), {}, false));
      ready->setAttr(
          "passthrough",
          builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
      Block *readyEntry = ready.addEntryBlock(builder);
      Block *readyScan = new Block;
      Block *readyLatched = new Block;
      ready.getBody().push_back(readyScan);
      ready.getBody().push_back(readyLatched);
      builder.setInsertionPointToStart(readyEntry);
      Value latches = LLVM::AddressOfOp::create(
          builder, location, pointer, promotionKernelLatched.getSymName());
      Value latchAddress = byteGEP(builder, location, latches, index);
      Value latch = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(), latchAddress, 1);
      Value isLatched = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, latch,
          llvmConstant(builder, location, builder.getI8Type(), 0));
      cf::CondBranchOp::create(builder, location, isLatched, readyLatched,
                               ValueRange{}, readyScan, ValueRange{});
      builder.setInsertionPointToStart(readyLatched);
      LLVM::ReturnOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI1Type(), 1));

      builder.setInsertionPointToStart(readyScan);
      Value unknown = LLVM::AddressOfOp::create(
          builder, location, pointer, "__obelisk_state_unknown");
      llvm::SmallDenseMap<uint64_t, uint8_t, 16> scanByteMasks;
      for (const NativePromotionRange &range : mergedPromotionRanges[index]) {
        if (range.bitWidth == 0 || range.bitOffset > stateLayout.bitCount ||
            range.bitWidth > stateLayout.bitCount - range.bitOffset)
          return module.emitError(
              "kernel promotion range exceeds native state");
        uint64_t firstByte = range.bitOffset / 8;
        uint64_t lastBit = range.bitOffset + range.bitWidth;
        uint64_t lastByte = (lastBit + 7) / 8;
        for (uint64_t byte = firstByte; byte != lastByte; ++byte) {
          uint8_t mask = UINT8_MAX;
          if (byte == firstByte && range.bitOffset % 8 != 0)
            mask &= static_cast<uint8_t>(UINT8_MAX
                                         << (range.bitOffset % 8));
          if (byte + 1 == lastByte && lastBit % 8 != 0)
            mask &= static_cast<uint8_t>(
                (uint16_t{1} << (lastBit % 8)) - 1);
          scanByteMasks[byte] |= mask;
        }
      }
      SmallVector<std::pair<uint64_t, uint8_t>> orderedScanBytes(
          scanByteMasks.begin(), scanByteMasks.end());
      llvm::sort(orderedScanBytes, [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
      });
      Block *readyUnknown = new Block;
      ready.getBody().push_back(readyUnknown);
      for (auto [byte, mask] : orderedScanBytes) {
        Value bits = LLVM::LoadOp::create(
            builder, location, builder.getI8Type(),
            byteGEP(builder, location, unknown, byte), 1);
        if (mask != UINT8_MAX)
          bits = arith::AndIOp::create(
              builder, location, bits,
              llvmConstant(builder, location, builder.getI8Type(), mask));
        Value byteUnknown = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, bits,
            llvmConstant(builder, location, builder.getI8Type(), 0));
        Block *nextByte = new Block;
        ready.getBody().push_back(nextByte);
        cf::CondBranchOp::create(builder, location, byteUnknown, readyUnknown,
                                 ValueRange{}, nextByte, ValueRange{});
        builder.setInsertionPointToStart(nextByte);
      }
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI8Type(), 1),
          latchAddress, 1);
      Value pendingAddress = LLVM::AddressOfOp::create(
          builder, location, pointer, promotionPendingMask.getSymName());
      Value pending =
          LLVM::LoadOp::create(builder, location, i64, pendingAddress, 8);
      Value clearedPending = arith::AndIOp::create(
          builder, location, pending,
          llvmConstant(builder, location, i64,
                       ~(uint64_t{1} << mergedFragments[index].bit)));
      LLVM::StoreOp::create(
          builder, location, clearedPending,
          pendingAddress, 8);
      LLVM::ReturnOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI1Type(), 1));
      builder.setInsertionPointToStart(readyUnknown);
      LLVM::ReturnOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI1Type(), 0));
    }

    // A periodic transaction may use the guard-free Tier-1 coordinator only
    // after every owner in its exact graph closure has selected a two-state
    // body. Per-owner scans run only when the hybrid coordinator selects that
    // owner; this aggregate quiescent-boundary check is a constant-time mask
    // test. Owners outside the closure cannot block promotion, and asynchronous
    // intervention clears the latch before generated execution resumes.
    periodicPromotionComplete = !periodicClosureRecords.empty();
    periodicPromotionMask = 0;
    for (unsigned recordIndex : periodicClosureRecords) {
      if (recordIndex >= mergedFragments.size() ||
          mergedFragments[recordIndex].bit >= 64 ||
          mergedTwoStateExecutors[recordIndex].empty() ||
          promotionKernelReadyNames[recordIndex].empty()) {
        periodicPromotionComplete = false;
        break;
      }
      auto executor = module.lookupSymbol<LLVM::LLVMFuncOp>(
          mergedTwoStateExecutors[recordIndex]);
      if (executor &&
          executor->hasAttr("obelisk.eval.path_guarded_two_state")) {
        // This owner contributes a stable guarded route rather than an
        // unconditional two-state invariant.  It can still participate in the
        // periodic certificate provided the coordinator observes its exact-CFG
        // rejection before choosing the NBA barrier.
        periodicPromotionHasPathGuards = true;
        periodicPromotionMask |=
            uint64_t{1} << mergedFragments[recordIndex].bit;
        continue;
      }
      periodicPromotionMask |=
          uint64_t{1} << mergedFragments[recordIndex].bit;
    }
    periodicEntryPromotionComplete = !periodicEntryRecords.empty();
    for (unsigned recordIndex : periodicEntryRecords) {
      if (recordIndex >= mergedFragments.size() ||
          mergedFragments[recordIndex].bit >= 64 ||
          mergedTwoStateExecutors[recordIndex].empty() ||
          promotionKernelReadyNames[recordIndex].empty()) {
        periodicEntryPromotionComplete = false;
        break;
      }
    }
    builder.setInsertionPointToEnd(module.getBody());
    auto periodicPromotionReady = LLVM::LLVMFuncOp::create(
        builder, location, periodicPromotionReadyName,
        LLVM::LLVMFunctionType::get(builder.getI1Type(), {}, false));
    periodicPromotionReady->setAttr(
        "passthrough",
        builder.getArrayAttr({builder.getStringAttr("noinline")}));
    Block *periodicReadyEntry = periodicPromotionReady.addEntryBlock(builder);
    Block *periodicReadyScan = new Block;
    Block *periodicAlreadyKnown = new Block;
    periodicPromotionReady.getBody().push_back(periodicReadyScan);
    periodicPromotionReady.getBody().push_back(periodicAlreadyKnown);
    builder.setInsertionPointToStart(periodicReadyEntry);
    Value periodicLatchAddress = LLVM::AddressOfOp::create(
        builder, location, pointer, periodicPromotionLatched.getSymName());
    Value periodicLatch = LLVM::LoadOp::create(
        builder, location, builder.getI8Type(), periodicLatchAddress, 1);
    Value periodicIsLatched = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, periodicLatch,
        llvmConstant(builder, location, builder.getI8Type(), 0));
    cf::CondBranchOp::create(builder, location, periodicIsLatched,
                             periodicAlreadyKnown, ValueRange{},
                             periodicReadyScan, ValueRange{});
    builder.setInsertionPointToStart(periodicAlreadyKnown);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI1Type(), 1));
    builder.setInsertionPointToStart(periodicReadyScan);
    Value periodicKnown = llvmConstant(
        builder, location, builder.getI1Type(), periodicPromotionComplete);
    Value periodicEntryKnown = llvmConstant(
        builder, location, builder.getI1Type(),
        periodicEntryPromotionComplete);
    if (periodicPromotionComplete) {
      // Owners clear their own bits only when selected in the hybrid path.
      // Dormant unknown owners therefore prevent guard-free full-closure
      // promotion without forcing an unknown-plane scan on every clock edge.
      Value periodicPending = LLVM::LoadOp::create(
          builder, location, i64,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    promotionPendingMask.getSymName()),
          8);
      Value noPeriodicPending = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq,
          arith::AndIOp::create(
              builder, location, periodicPending,
              llvmConstant(builder, location, i64, periodicPromotionMask)),
          llvmConstant(builder, location, i64, 0));
      periodicKnown = arith::AndIOp::create(builder, location, periodicKnown,
                                            noPeriodicPending);
    }
    // The guarded steady coordinator carries a local four-/two-state route for
    // every structurally covered entry owner.  Entering it therefore requires
    // coverage, not that every entry owner has already promoted.  Each selected
    // pending owner retains its four-state body until its own masked scan
    // succeeds; owners outside the exact periodic closure still take the
    // explicit local hybrid fallback.
    Value periodicEntryLatchAddress = LLVM::AddressOfOp::create(
        builder, location, pointer,
        periodicEntryPromotionLatched.getSymName());
    Value periodicEntryLatch = LLVM::LoadOp::create(
        builder, location, builder.getI8Type(), periodicEntryLatchAddress, 1);
    LLVM::StoreOp::create(
        builder, location,
        arith::SelectOp::create(
            builder, location, periodicEntryKnown,
            llvmConstant(builder, location, builder.getI8Type(), 1),
            periodicEntryLatch),
        periodicEntryLatchAddress, 1);
    // The entry promotion is observed only after the hybrid coordinator has
    // committed and reached quiescence. Its transient four-state accounting
    // belongs to that completed slot; clear it once here so the steady route
    // starts with canonical two-state NBA selection without a per-edge reset.
    Value stepFallbackAddress = LLVM::AddressOfOp::create(
        builder, location, pointer, evalStepFourStateFallbackName);
    Value stepFallback = LLVM::LoadOp::create(
        builder, location, builder.getI8Type(), stepFallbackAddress, 1);
    LLVM::StoreOp::create(
        builder, location,
        arith::SelectOp::create(
            builder, location, periodicEntryKnown,
            llvmConstant(builder, location, builder.getI8Type(), 0),
            stepFallback),
        stepFallbackAddress, 1);
    if (nbaTaintWordCount != 0) {
      Value taint = LLVM::AddressOfOp::create(
          builder, location, pointer, evalStepFourStateNBARootsName);
      for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
        Value address = byteGEP(builder, location, taint,
                                uint64_t{word} * sizeof(uint64_t));
        Value old = LLVM::LoadOp::create(builder, location, i64, address, 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::SelectOp::create(
                builder, location, periodicEntryKnown,
                llvmConstant(builder, location, i64, 0), old),
            address, 8);
      }
    }
    LLVM::StoreOp::create(
        builder, location,
        arith::SelectOp::create(
            builder, location, periodicKnown,
            llvmConstant(builder, location, builder.getI8Type(), 1),
            periodicLatch),
        periodicLatchAddress, 1);
    // Only the complete closure proof establishes the invariant required by
    // the value-plane-only NBA barrier.  The guarded entry route still uses
    // the canonical/four-state selection until that stronger proof succeeds.
    Value fastNBALatchAddress = LLVM::AddressOfOp::create(
        builder, location, pointer, evalFastNBALatchedName);
    Value fastNBALatch = LLVM::LoadOp::create(
        builder, location, builder.getI8Type(), fastNBALatchAddress, 1);
    LLVM::StoreOp::create(
        builder, location,
        arith::SelectOp::create(
            builder, location, periodicKnown,
            llvmConstant(builder, location, builder.getI8Type(), 1),
            fastNBALatch),
        fastNBALatchAddress, 1);
    LLVM::ReturnOp::create(builder, location, periodicKnown);

    builder.setInsertionPointToEnd(module.getBody());
    auto promotionReady = LLVM::LLVMFuncOp::create(
        builder, location, promotionReadyName,
        LLVM::LLVMFunctionType::get(builder.getI1Type(), {}, false));
    promotionReady->setAttr(
        "passthrough",
        builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
    Block *entry = promotionReady.addEntryBlock(builder);
    Block *scan = new Block;
    Block *alreadyKnown = new Block;
    promotionReady.getBody().push_back(scan);
    promotionReady.getBody().push_back(alreadyKnown);
    builder.setInsertionPointToStart(entry);
    Value latchedAddress = LLVM::AddressOfOp::create(
        builder, location, pointer, promotionLatched.getSymName());
    Value latched = LLVM::LoadOp::create(
        builder, location, builder.getI8Type(), latchedAddress, 1);
    Value isLatched = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, latched,
        llvmConstant(builder, location, builder.getI8Type(), 0));
    cf::CondBranchOp::create(builder, location, isLatched, alreadyKnown,
                             ValueRange{}, scan, ValueRange{});

    builder.setInsertionPointToStart(alreadyKnown);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI1Type(), 1));

    builder.setInsertionPointToStart(scan);
    Value pending = LLVM::LoadOp::create(
        builder, location, i64,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  promotionPendingMask.getSymName()),
        8);
    Value known = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, pending,
        llvmConstant(builder, location, i64, 0));
    LLVM::StoreOp::create(
        builder, location,
        arith::SelectOp::create(
            builder, location, known,
            llvmConstant(builder, location, builder.getI8Type(), 1), latched),
        latchedAddress, 1);
    LLVM::ReturnOp::create(builder, location, known);

    // Runtime intervention invalidates the selected two-state closure without
    // scanning it. The next quiescent generated dispatch performs the single
    // masked unknown-plane scan and selects the appropriate variant again.
    builder.setInsertionPointToEnd(module.getBody());
    auto promotionInvalidate = LLVM::LLVMFuncOp::create(
        builder, location, promotionInvalidateName,
        LLVM::LLVMFunctionType::get(LLVM::LLVMVoidType::get(context), {},
                                    false));
    promotionInvalidate->setAttr(
        "passthrough",
        builder.getArrayAttr(
            {builder.getStringAttr("noinline"),
             builder.getStringAttr("cold")}));
    Block *invalidateEntry = promotionInvalidate.addEntryBlock(builder);
    builder.setInsertionPointToStart(invalidateEntry);
    LLVM::StoreOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI8Type(), 0),
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  promotionLatched.getSymName()),
        1);
    LLVM::StoreOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI8Type(), 0),
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  periodicPromotionLatched.getSymName()),
        1);
    LLVM::StoreOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI8Type(), 0),
        LLVM::AddressOfOp::create(
            builder, location, pointer,
            periodicEntryPromotionLatched.getSymName()),
        1);
    LLVM::MemsetOp::create(
        builder, location,
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  promotionKernelLatched.getSymName()),
        llvmConstant(builder, location, builder.getI8Type(), 0),
        llvmConstant(builder, location, i64, mergedFragments.size()),
        /*isVolatile=*/false);
    LLVM::StoreOp::create(
        builder, location,
        llvmConstant(builder, location, i64, initialPromotionPendingMask),
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  promotionPendingMask.getSymName()),
        8);
    LLVM::StoreOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI8Type(), 0),
        LLVM::AddressOfOp::create(builder, location, pointer,
                                  evalFastNBALatchedName),
        1);
    if (nbaTaintWordCount != 0)
      LLVM::MemsetOp::create(
          builder, location,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    evalFastNBARootsName),
          llvmConstant(builder, location, builder.getI8Type(), 0),
          llvmConstant(builder, location, i64,
                       uint64_t{nbaTaintWordCount} * sizeof(uint64_t)),
          /*isVolatile=*/false);
    LLVM::ReturnOp::create(builder, location, ValueRange{});

    // The runtime calls this only while its transient fine scheduler is
    // quiescent. Keep the internal i1 promotion predicate convenient for the
    // generated coordinator and expose an ABI-sized result in the plan.
    builder.setInsertionPointToEnd(module.getBody());
    auto promotionQuery = LLVM::LLVMFuncOp::create(
        builder, location, promotionQueryName,
        LLVM::LLVMFunctionType::get(i32, {}, false));
    Block *queryEntry = promotionQuery.addEntryBlock(builder);
    builder.setInsertionPointToStart(queryEntry);
    Value queryReady =
        LLVM::CallOp::create(builder, location, TypeRange{builder.getI1Type()},
                             SymbolRefAttr::get(context, promotionReadyName),
                             ValueRange{})
            .getResult();
    LLVM::ReturnOp::create(
        builder, location,
        arith::ExtUIOp::create(builder, location, i32, queryReady));
  }

  Type nodeType = LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32});
  Type nodesType = LLVM::LLVMArrayType::get(nodeType, executableNodes.size());
  makeConstantGlobal(
      module, location, nodesType, nodesName, LLVM::Linkage::Internal, 4,
      [&](OpBuilder &initializerBuilder) {
        Value nodes =
            LLVM::ZeroOp::create(initializerBuilder, location, nodesType);
        for (auto [index, node] : llvm::enumerate(executableNodes)) {
          Value value =
              LLVM::ZeroOp::create(initializerBuilder, location, nodeType);
          value = insertValue(
              initializerBuilder, location, value,
              llvmConstant(initializerBuilder, location, i32, node.actor_slot),
              0);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.continuation),
                              1);
          value = insertValue(initializerBuilder, location, value,
                              llvmConstant(initializerBuilder, location, i32,
                                           node.fusion_group),
                              2);
          nodes = LLVM::InsertValueOp::create(
              initializerBuilder, location, nodes, value,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return nodes;
      });

  Type nbaRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, pointer});
  if (!nbaRoots.empty()) {
    Type rootsType = LLVM::LLVMArrayType::get(nbaRootType, nbaRoots.size());
    makeConstantGlobal(
        module, location, rootsType, nbaRootsName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value roots =
              LLVM::ZeroOp::create(initializerBuilder, location, rootsType);
          for (auto [index, root] : llvm::enumerate(nbaRoots)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.commit_node),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             root.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, root.bit_width),
                2);
            Value accumulator =
                index < staticNBAPlan.generatedAccumulators.size() &&
                        !staticNBAPlan.generatedAccumulators[index].empty()
                    ? LLVM::AddressOfOp::create(
                          initializerBuilder, location, pointer,
                          staticNBAPlan.generatedAccumulators[index])
                          .getResult()
                    : LLVM::ZeroOp::create(initializerBuilder, location,
                                           pointer)
                          .getResult();
            value = insertValue(initializerBuilder, location, value,
                                accumulator, 3);
            roots = LLVM::InsertValueOp::create(
                initializerBuilder, location, roots, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return roots;
        });
  }
  uint32_t nbaDirtyWordCount =
      static_cast<uint32_t>((nbaRoots.size() + 63) / 64);
  uint32_t nbaDirtySummaryWordCount = (nbaDirtyWordCount + 63) / 64;
  if (nbaDirtyWordCount != 0) {
    Type dirtyType = LLVM::LLVMArrayType::get(i64, nbaDirtyWordCount);
    builder.setInsertionPointToStart(module.getBody());
    auto dirty = LLVM::GlobalOp::create(builder, location, dirtyType, false,
                                        LLVM::Linkage::Internal,
                                        nbaDirtyRootsName, Attribute{}, 8);
    Block *dirtyInitializer = new Block;
    dirty.getInitializerRegion().push_back(dirtyInitializer);
    builder.setInsertionPointToStart(dirtyInitializer);
    LLVM::ReturnOp::create(builder, location,
                           LLVM::ZeroOp::create(builder, location, dirtyType));
  }
  if (nbaDirtySummaryWordCount != 0) {
    Type summaryType = LLVM::LLVMArrayType::get(i64, nbaDirtySummaryWordCount);
    builder.setInsertionPointToStart(module.getBody());
    auto summary = LLVM::GlobalOp::create(builder, location, summaryType, false,
                                          LLVM::Linkage::Internal,
                                          nbaDirtySummaryName, Attribute{}, 8);
    Block *summaryInitializer = new Block;
    summary.getInitializerRegion().push_back(summaryInitializer);
    builder.setInsertionPointToStart(summaryInitializer);
    LLVM::ReturnOp::create(
        builder, location,
        LLVM::ZeroOp::create(builder, location, summaryType));
  }
  Type nbaSiteType = LLVM::LLVMStructType::getLiteral(context, {i64, i32, i32});
  if (!nbaSites.empty()) {
    Type sitesType = LLVM::LLVMArrayType::get(nbaSiteType, nbaSites.size());
    makeConstantGlobal(
        module, location, sitesType, nbaSitesName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value sites =
              LLVM::ZeroOp::create(initializerBuilder, location, sitesType);
          for (auto [index, site] : llvm::enumerate(nbaSites)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, nbaSiteType);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, site.site), 0);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.root), 1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, site.storage),
                2);
            sites = LLVM::InsertValueOp::create(
                initializerBuilder, location, sites, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return sites;
        });
  }
  Type fanoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i32, i32, i64, i64, i32, i32});
  if (!fanoutEntries.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(fanoutType, fanoutEntries.size());
    makeConstantGlobal(
        module, location, entriesType, fanoutName, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(fanoutEntries)) {
            Value value =
                LLVM::ZeroOp::create(initializerBuilder, location, fanoutType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                1);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.continuation),
                                2);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.edge), 3);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.compute_node),
                                4);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.reserved),
                5);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, entry.low_bit),
                6);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             entry.bit_width),
                                7);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.kernel),
                8);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.merged_bit),
                                9);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }
  auto ingressWordCount = [&](const GeneratedClockKernel &kernel) {
    size_t bits =
        evalScheduler ? mergedFragments.size() : kernel.computeNodes.size();
    return static_cast<uint32_t>((bits + 63) / 64);
  };
  builder.setInsertionPointToStart(module.getBody());
  llvm::SmallDenseSet<StringRef, 4> emittedIngress;
  for (const GeneratedClockKernel &kernel : clockKernels) {
    uint32_t words = ingressWordCount(kernel);
    Type ingressType = LLVM::LLVMArrayType::get(i64, words);
    if (emittedIngress.insert(kernel.ingressName).second) {
      auto ingress = LLVM::GlobalOp::create(
          builder, location, ingressType, false, LLVM::Linkage::Internal,
          kernel.ingressName, Attribute{}, 8);
      Block *initializer = new Block;
      ingress.getInitializerRegion().push_back(initializer);
      OpBuilder initializerBuilder = OpBuilder::atBlockBegin(initializer);
      LLVM::ReturnOp::create(
          initializerBuilder, location,
          LLVM::ZeroOp::create(initializerBuilder, location, ingressType));
    }
    auto active = LLVM::GlobalOp::create(builder, location, ingressType, false,
                                         LLVM::Linkage::Internal,
                                         kernel.activeName, Attribute{}, 8);
    Block *activeInitializer = new Block;
    active.getInitializerRegion().push_back(activeInitializer);
    OpBuilder activeBuilder = OpBuilder::atBlockBegin(activeInitializer);
    LLVM::ReturnOp::create(
        activeBuilder, location,
        LLVM::ZeroOp::create(activeBuilder, location, ingressType));
  }

  // The eval experiment is a closed generated call graph. Replace scalar
  // transition publication in private eval bodies with direct ingress-bit
  // updates. No actor lookup, continuation validation, subscription scan, or
  // runtime callback remains on this path; records without a generated body
  // are intentionally outside the experiment.
  if (evalScheduler && !clockKernels.empty()) {
    auto isGeneratedEvalBody = [](sim::SimFuncOp function) {
      return function->hasAttr("obelisk.eval.raw_captures") ||
             function->hasAttr("obelisk.eval.selected_two_state");
    };
    SmallVector<std::pair<LLVM::CallOp, bool>> transitions;
    module.walk([&](sim::SimFuncOp function) {
      if (!isGeneratedEvalBody(function))
        return;
      bool periodicTwoState =
          function->hasAttr("obelisk.eval.selected_two_state");
      function.walk([&](LLVM::CallOp call) {
        if (call.getCallee() &&
            *call.getCallee() == "obelisk_rt_v1_scheduler_static_transition")
          transitions.push_back({call, periodicTwoState});
      });
    });
    auto constantU64 = [](Value value) -> std::optional<uint64_t> {
      auto constant = value.getDefiningOp<LLVM::ConstantOp>();
      auto integer =
          constant ? dyn_cast<IntegerAttr>(constant.getValue()) : IntegerAttr{};
      return integer
                 ? std::optional<uint64_t>{integer.getValue().getZExtValue()}
                 : std::nullopt;
    };
    auto packedMask = [](uint64_t width) {
      return width >= 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
    };
    for (auto [call, periodicTwoState] : transitions) {
      ValueRange arguments = call.getArgOperands();
      if (arguments.size() != 8)
        return call.emitError("malformed static transition ABI"), failure();
      std::optional<uint64_t> staticState = constantU64(arguments[1]);
      std::optional<uint64_t> lowBit = constantU64(arguments[2]);
      std::optional<uint64_t> bitWidth = constantU64(arguments[3]);
      if (!staticState || !lowBit || !bitWidth || *bitWidth == 0 ||
          *bitWidth > 64)
        return call.emitError("eval transition is not a fixed scalar range"),
               failure();
      OpBuilder transitionBuilder(call);
      Value oldValue = arguments[4];
      Value oldUnknown = arguments[5];
      Value newValue = arguments[6];
      Value newUnknown = arguments[7];
      Value changed = arith::OrIOp::create(
          transitionBuilder, call.getLoc(),
          arith::XOrIOp::create(transitionBuilder, call.getLoc(), oldValue,
                                newValue),
          arith::XOrIOp::create(transitionBuilder, call.getLoc(), oldUnknown,
                                newUnknown));
      struct MergedPublication {
        uint32_t bit;
        uint64_t changeMask = 0;
        uint64_t posedgeMask = 0;
        uint64_t negedgeMask = 0;
      };
      SmallVector<MergedPublication, 4> publications;
      uint64_t rangeEnd = *lowBit + *bitWidth;
      for (const obelisk_rt_static_fanout_entry &entry : fanoutEntries) {
        if (entry.reserved != 1 || entry.static_state != *staticState ||
            entry.kernel >= clockKernels.size() || entry.merged_bit >= 64)
          continue;
        if (periodicTwoState && !periodicClosureRecords.empty() &&
            !llvm::is_contained(periodicClosureRecords,
                                static_cast<unsigned>(entry.merged_bit)))
          continue;
        uint64_t overlapLow = std::max(*lowBit, entry.low_bit);
        uint64_t overlapHigh =
            std::min(rangeEnd, entry.low_bit + entry.bit_width);
        if (overlapLow >= overlapHigh)
          continue;
        uint64_t overlapMask = packedMask(overlapHigh - overlapLow)
                               << (overlapLow - *lowBit);
        auto publication = llvm::find_if(
            publications, [&](const MergedPublication &candidate) {
              return candidate.bit == entry.merged_bit;
            });
        if (publication == publications.end()) {
          publications.push_back({entry.merged_bit});
          publication = std::prev(publications.end());
        }
        if (entry.edge == OBELISK_RT_WAIT_EDGE_POSEDGE)
          publication->posedgeMask |= overlapMask;
        else if (entry.edge == OBELISK_RT_WAIT_EDGE_NEGEDGE)
          publication->negedgeMask |= overlapMask;
        else if (entry.edge == OBELISK_RT_WAIT_EDGE_BOTH) {
          publication->posedgeMask |= overlapMask;
          publication->negedgeMask |= overlapMask;
        } else {
          publication->changeMask |= overlapMask;
        }
      }
      if (publications.empty()) {
        call.erase();
        continue;
      }
      Value widthMask = llvmConstant(transitionBuilder, call.getLoc(), i64,
                                     packedMask(*bitWidth));
      auto invert = [&](Value value) {
        return arith::XOrIOp::create(transitionBuilder, call.getLoc(), value,
                                     llvmConstant(transitionBuilder,
                                                  call.getLoc(), i64,
                                                  UINT64_MAX))
            .getResult();
      };
      bool needsPosedge = llvm::any_of(publications, [](const auto &entry) {
        return entry.posedgeMask != 0;
      });
      bool needsNegedge = llvm::any_of(publications, [](const auto &entry) {
        return entry.negedgeMask != 0;
      });
      Value posedge;
      Value negedge;
      if (needsPosedge || needsNegedge) {
        Value oldKnown = arith::AndIOp::create(
            transitionBuilder, call.getLoc(), invert(oldUnknown), widthMask);
        Value newKnown = arith::AndIOp::create(
            transitionBuilder, call.getLoc(), invert(newUnknown), widthMask);
        Value oldZero = arith::AndIOp::create(
            transitionBuilder, call.getLoc(), oldKnown, invert(oldValue));
        Value oldOne = arith::AndIOp::create(
            transitionBuilder, call.getLoc(), oldKnown, oldValue);
        Value newZero = arith::AndIOp::create(
            transitionBuilder, call.getLoc(), newKnown, invert(newValue));
        Value newOne = arith::AndIOp::create(
            transitionBuilder, call.getLoc(), newKnown, newValue);
        if (needsPosedge)
          posedge = arith::AndIOp::create(
              transitionBuilder, call.getLoc(),
              arith::OrIOp::create(
                  transitionBuilder, call.getLoc(),
                  arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                        oldZero, invert(newZero)),
                  arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                        oldUnknown, newOne)),
              widthMask);
        if (needsNegedge)
          negedge = arith::AndIOp::create(
              transitionBuilder, call.getLoc(),
              arith::OrIOp::create(
                  transitionBuilder, call.getLoc(),
                  arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                        oldOne, invert(newOne)),
                  arith::AndIOp::create(transitionBuilder, call.getLoc(),
                                        oldUnknown, newZero)),
              widthMask);
      }
      for (const MergedPublication &publication : publications) {
        Value observed = llvmConstant(transitionBuilder, call.getLoc(), i64, 0);
        auto addObserved = [&](Value edges, uint64_t mask) {
          if (mask == 0)
            return;
          Value masked = arith::AndIOp::create(
              transitionBuilder, call.getLoc(), edges,
              llvmConstant(transitionBuilder, call.getLoc(), i64, mask));
          observed = arith::OrIOp::create(transitionBuilder, call.getLoc(),
                                          observed, masked);
        };
        addObserved(changed, publication.changeMask);
        addObserved(posedge, publication.posedgeMask);
        addObserved(negedge, publication.negedgeMask);
        Value triggered = arith::CmpIOp::create(
            transitionBuilder, call.getLoc(), arith::CmpIPredicate::ne,
            observed,
            llvmConstant(transitionBuilder, call.getLoc(), i64, 0));
        Value ingress = LLVM::AddressOfOp::create(
            transitionBuilder, call.getLoc(), pointer,
            clockKernels.front().ingressName);
        Value address =
            byteGEP(transitionBuilder, call.getLoc(), ingress,
                    uint64_t{publication.bit / 64} * sizeof(uint64_t));
        Value previous = LLVM::LoadOp::create(transitionBuilder, call.getLoc(),
                                              i64, address, 8);
        Value selected = arith::SelectOp::create(
            transitionBuilder, call.getLoc(), triggered,
            llvmConstant(transitionBuilder, call.getLoc(), i64,
                         uint64_t{1} << (publication.bit % 64)),
            llvmConstant(transitionBuilder, call.getLoc(), i64, 0));
        LLVM::StoreOp::create(transitionBuilder, call.getLoc(),
                              arith::OrIOp::create(transitionBuilder,
                                                   call.getLoc(), previous,
                                                   selected),
                              address, 8);
      }
      call.erase();
    }

    // Dynamic writes into a fixed packed root use a generated one-entry NBA
    // latch. This is the common register-file shape: the clock body records
    // offset/value locally and the generated NBA epilogue publishes it after
    // the activation returns, without constructing a runtime NBA object.
    SmallVector<std::pair<LLVM::CallOp, bool>> runtimeEscapes;
    module.walk([&](sim::SimFuncOp function) {
      if (!isGeneratedEvalBody(function))
        return;
      bool twoState = function->hasAttr("obelisk.eval.selected_two_state");
      function.walk([&](LLVM::CallOp call) {
        if (!call.getCallee())
          return;
        if (*call.getCallee() == "obelisk_rt_v1_scheduler_static_nba" ||
            *call.getCallee() == "obelisk_rt_v1_scheduler_fail")
          runtimeEscapes.push_back({call, twoState});
      });
    });
    for (auto [call, twoState] : runtimeEscapes) {
      LLVM::CallOp handleOffsetToErase;
      arith::SelectOp handleSelectToErase;
      if (call.getCallee() &&
          *call.getCallee() == "obelisk_rt_v1_scheduler_static_nba") {
        ValueRange arguments = call.getArgOperands();
        if (arguments.size() != 9)
          return call.emitError("malformed static NBA ABI"), failure();
        std::optional<uint64_t> site = constantU64(arguments[1]);
        std::optional<uint64_t> width = constantU64(arguments[6]);
        auto root = site ? staticNBAPlan.siteRoots.find(*site)
                         : staticNBAPlan.siteRoots.end();
        auto offsetCall = arguments[5].getDefiningOp<LLVM::CallOp>();
        if (!offsetCall)
          if (auto selected = arguments[5].getDefiningOp<arith::SelectOp>()) {
            handleSelectToErase = selected;
            offsetCall = selected.getTrueValue().getDefiningOp<LLVM::CallOp>();
          }
        bool dynamicRoot =
            site && width && *width != 0 && *width <= 64 &&
            root != staticNBAPlan.siteRoots.end() &&
            root->second < staticNBAPlan.roots.size() &&
            *width <= staticNBAPlan.roots[root->second].bit_width &&
            root->second < staticNBAPlan.generatedOffsets.size() &&
            offsetCall && offsetCall.getCallee() &&
            *offsetCall.getCallee() == "obelisk_rt_v1_native_handle_offset" &&
            offsetCall.getArgOperands().size() == 2 &&
            (staticNBAPlan.generatedOffsets[root->second] & 7) == 0;
        if (!dynamicRoot) {
          return call.emitError(
                     "runtime-free eval cannot lower dynamic NBA site"),
                 failure();
        }
        handleOffsetToErase = offsetCall;
        OpBuilder nbaBuilder(call);
        Value dynamicBit = offsetCall.getArgOperands()[1];
        IntegerType valueType = IntegerType::get(context, *width);
        Value staged = LLVM::LoadOp::create(nbaBuilder, call.getLoc(),
                                            valueType, arguments[7], 1);
        auto existing =
            llvm::find_if(dynamicEvalNBAs, [&](const DynamicEvalNBA &entry) {
              return entry.site == *site;
            });
        if (existing == dynamicEvalNBAs.end()) {
          DynamicEvalNBA entry{root->second, *site, *width};
          entry.offsetName =
              (Twine("__obelisk_eval_nba_offset_") + Twine(*site)).str();
          entry.valueName =
              (Twine("__obelisk_eval_nba_value_") + Twine(*site)).str();
          entry.unknownName =
              (Twine("__obelisk_eval_nba_unknown_") + Twine(*site)).str();
          entry.validName =
              (Twine("__obelisk_eval_nba_valid_") + Twine(*site)).str();
          auto makeZero = [&](StringRef name, Type type, unsigned alignment) {
            OpBuilder globalBuilder = OpBuilder::atBlockBegin(module.getBody());
            auto global = LLVM::GlobalOp::create(
                globalBuilder, call.getLoc(), type, false,
                LLVM::Linkage::Internal, name, Attribute{}, alignment);
            Block *initializer = new Block;
            global.getInitializerRegion().push_back(initializer);
            OpBuilder initBuilder = OpBuilder::atBlockBegin(initializer);
            LLVM::ReturnOp::create(
                initBuilder, call.getLoc(),
                LLVM::ZeroOp::create(initBuilder, call.getLoc(), type));
          };
          makeZero(entry.offsetName, i64, 8);
          makeZero(entry.valueName, i64, 8);
          makeZero(entry.unknownName, i64, 8);
          makeZero(entry.validName, i32, 4);
          dynamicEvalNBAs.push_back(std::move(entry));
          existing = std::prev(dynamicEvalNBAs.end());
        }
        auto storeGlobal = [&](StringRef name, Value value,
                               unsigned alignment) {
          Value address = LLVM::AddressOfOp::create(nbaBuilder, call.getLoc(),
                                                    pointer, name);
          LLVM::StoreOp::create(nbaBuilder, call.getLoc(), value, address,
                                alignment);
        };
        storeGlobal(existing->offsetName, dynamicBit, 8);
        Value staged64 =
            *width == 64
                ? staged
                : LLVM::ZExtOp::create(nbaBuilder, call.getLoc(), i64, staged)
                      .getResult();
        storeGlobal(existing->valueName, staged64, 8);
        Value stagedUnknown64 =
            llvmConstant(nbaBuilder, call.getLoc(), i64, 0);
        if (!twoState) {
          Value stagedUnknown = LLVM::LoadOp::create(
              nbaBuilder, call.getLoc(), valueType, arguments[8], 1);
          stagedUnknown64 =
              *width == 64
                  ? stagedUnknown
                  : LLVM::ZExtOp::create(nbaBuilder, call.getLoc(), i64,
                                         stagedUnknown)
                        .getResult();
        }
        storeGlobal(existing->unknownName, stagedUnknown64, 8);
        storeGlobal(existing->validName,
                    llvmConstant(nbaBuilder, call.getLoc(), i32, 1), 4);
      }
      if (call.getNumResults() != 0) {
        if (call.getNumResults() != 1 || call.getResult().getType() != i32)
          return call.emitError("unsupported eval runtime escape ABI"),
                 failure();
        OpBuilder escapeBuilder(call);
        call.getResult().replaceAllUsesWith(
            llvmConstant(escapeBuilder, call.getLoc(), i32, OBELISK_RT_OK));
      }
      call.erase();
      if (handleSelectToErase && handleSelectToErase->use_empty())
        handleSelectToErase.erase();
      if (handleOffsetToErase && handleOffsetToErase->use_empty())
        handleOffsetToErase.erase();
    }
    SmallVector<LLVM::CallOp> deadHandleOffsets;
    module.walk([&](sim::SimFuncOp function) {
      if (!isGeneratedEvalBody(function))
        return;
      function.walk([&](LLVM::CallOp call) {
        if (call.getCallee() &&
            *call.getCallee() == "obelisk_rt_v1_native_handle_offset" &&
            call->use_empty())
          deadHandleOffsets.push_back(call);
      });
    });
    for (LLVM::CallOp call : deadHandleOffsets)
      call.erase();
    llvm::SmallDenseSet<uint32_t, 8> dynamicRoots;
    for (const DynamicEvalNBA &entry : dynamicEvalNBAs)
      if (!dynamicRoots.insert(entry.rootIndex).second)
        return module.emitError(
            "runtime-free eval has multiple ordered dynamic NBA sites for "
            "one root"),
               failure();
  }
  Type clockKernelType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, pointer, i32, i32, pointer});
  if (!clockKernels.empty()) {
    Type clocksType =
        LLVM::LLVMArrayType::get(clockKernelType, clockKernels.size());
    makeConstantGlobal(
        module, location, clocksType, clockKernelsName, LLVM::Linkage::Internal,
        8, [&](OpBuilder &initializerBuilder) {
          Value clocks =
              LLVM::ZeroOp::create(initializerBuilder, location, clocksType);
          for (auto [index, kernel] : llvm::enumerate(clockKernels)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               clockKernelType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             kernel.staticState),
                                0);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, kernel.edge),
                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64, kernel.lowBit),
                2);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i64,
                                             kernel.bitWidth),
                                3);
            value = insertValue(initializerBuilder, location, value,
                                LLVM::AddressOfOp::create(initializerBuilder,
                                                          location, pointer,
                                                          kernel.ingressName),
                                4);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32,
                             ingressWordCount(kernel)),
                5);
            value = insertValue(initializerBuilder, location, value,
                                LLVM::AddressOfOp::create(initializerBuilder,
                                                          location, pointer,
                                                          kernel.activeName),
                                7);
            clocks = LLVM::InsertValueOp::create(
                initializerBuilder, location, clocks, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return clocks;
        });
  }
  Type mergedFragmentType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i32, i32, pointer});
  if (!mergedFragments.empty()) {
    Type mergedType =
        LLVM::LLVMArrayType::get(mergedFragmentType, mergedFragments.size());
    makeConstantGlobal(
        module, location, mergedType, mergedFragmentsName,
        LLVM::Linkage::Internal, 8, [&](OpBuilder &initializerBuilder) {
          Value merged =
              LLVM::ZeroOp::create(initializerBuilder, location, mergedType);
          for (auto [index, record] : llvm::enumerate(mergedFragments)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               mergedFragmentType);
            const uint32_t fields[] = {record.actor_slot,   record.continuation,
                                       record.kernel,       record.bit,
                                       record.compute_node, record.flags};
            for (unsigned field = 0; field != std::size(fields); ++field)
              value = insertValue(initializerBuilder, location, value,
                                  llvmConstant(initializerBuilder, location,
                                               i32, fields[field]),
                                  field);
            Value execute =
                mergedExecutors[index].empty()
                    ? LLVM::ZeroOp::create(initializerBuilder, location,
                                           pointer)
                          .getResult()
                    : LLVM::AddressOfOp::create(initializerBuilder, location,
                                                pointer, mergedExecutors[index])
                          .getResult();
            value =
                insertValue(initializerBuilder, location, value, execute, 6);
            merged = LLVM::InsertValueOp::create(
                initializerBuilder, location, merged, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return merged;
        });
  }
  Type actorRootType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32, i32});
  if (!actorRoots.empty()) {
    Type entriesType =
        LLVM::LLVMArrayType::get(actorRootType, actorRoots.size());
    makeConstantGlobal(
        module, location, entriesType, actorRootsName, LLVM::Linkage::Internal,
        4, [&](OpBuilder &initializerBuilder) {
          Value entries =
              LLVM::ZeroOp::create(initializerBuilder, location, entriesType);
          for (auto [index, entry] : llvm::enumerate(actorRoots)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               actorRootType);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.actor_slot),
                                0);
            value = insertValue(initializerBuilder, location, value,
                                llvmConstant(initializerBuilder, location, i32,
                                             entry.static_state),
                                1);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i32, entry.flags),
                2);
            entries = LLVM::InsertValueOp::create(
                initializerBuilder, location, entries, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return entries;
        });
  }

  constexpr StringLiteral periodicAliasesName =
      "__obelisk_periodic_alias_plan_v1";
  Type periodicAliasType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i64, i64, i64});
  if (!periodicAliases.empty()) {
    Type aliasesType =
        LLVM::LLVMArrayType::get(periodicAliasType, periodicAliases.size());
    makeConstantGlobal(
        module, location, aliasesType, periodicAliasesName,
        LLVM::Linkage::Internal, 8, [&](OpBuilder &initializerBuilder) {
          Value aliases =
              LLVM::ZeroOp::create(initializerBuilder, location, aliasesType);
          for (auto [index, alias] : llvm::enumerate(periodicAliases)) {
            Value value = LLVM::ZeroOp::create(initializerBuilder, location,
                                               periodicAliasType);
            const uint32_t fields[] = {
                alias.sourceStaticState, alias.forwardingActorSlot,
                alias.forwardingContinuation, alias.targetStaticState};
            for (unsigned field = 0; field != std::size(fields); ++field)
              value = insertValue(
                  initializerBuilder, location, value,
                  llvmConstant(initializerBuilder, location, i32,
                               fields[field]),
                  field);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64,
                             alias.sourceBitOffset),
                4);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64,
                             alias.targetBitOffset),
                5);
            value = insertValue(
                initializerBuilder, location, value,
                llvmConstant(initializerBuilder, location, i64,
                             alias.driverBitOffset),
                6);
            aliases = LLVM::InsertValueOp::create(
                initializerBuilder, location, aliases, value,
                ArrayRef<int64_t>{static_cast<int64_t>(index)});
          }
          return aliases;
        });
  }

  builder.setInsertionPointToEnd(module.getBody());
  auto bind = LLVM::LLVMFuncOp::create(
      builder, location, bindName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  Block *bindEntry = bind.addEntryBlock(builder);
  builder.setInsertionPointToStart(bindEntry);
  Value slot =
      LLVM::ZExtOp::create(builder, location, i64, bindEntry->getArgument(2));
  Value actorAddress =
      LLVM::GEPOp::create(builder, location, pointer, pointer,
                          bindEntry->getArgument(0), ValueRange{slot});
  LLVM::StoreOp::create(builder, location, bindEntry->getArgument(3),
                        actorAddress, 8);
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, OBELISK_RT_OK));

  builder.setInsertionPointToEnd(module.getBody());
  auto run = LLVM::LLVMFuncOp::create(
      builder, location, runName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
  Block *runEntry = run.addEntryBlock(builder);
  builder.setInsertionPointToStart(runEntry);
  bool generatedEvalLoop =
      evalScheduler && !periodicClocks.empty() && !clockKernels.empty() &&
      !mergedFragments.empty() && mergedFragments.size() <= 64 &&
      promotionRangesComplete &&
      mergedExecutors.size() == mergedFragments.size() &&
      mergedTwoStateExecutors.size() == mergedFragments.size();
  if (evalScheduler && !generatedEvalLoop) {
    auto firstEmpty = llvm::find_if(
        mergedExecutors,
        [](const std::string &name) { return name.empty(); });
    auto diagnostic = module.emitError();
    diagnostic
        << "cannot materialize generated eval loop: clocks="
        << periodicClocks.size()
        << " kernels=" << clockKernels.size()
        << " fragments=" << mergedFragments.size()
        << " executors=" << mergedTwoStateExecutors.size()
        << " promotion-complete=" << promotionRangesComplete
        << " empty-executors="
        << llvm::count_if(mergedExecutors,
                          [](const std::string &name) { return name.empty(); });
    if (firstEmpty != mergedExecutors.end()) {
      size_t index = static_cast<size_t>(firstEmpty -
                                         mergedExecutors.begin());
      diagnostic << " first-empty-actor=" << mergedFragments[index].actor_slot
                 << " continuation=" << mergedFragments[index].continuation;
      if (auto planned = staticFanoutPlan.fragments.find(
              {mergedFragments[index].actor_slot,
               mergedFragments[index].continuation});
          planned != staticFanoutPlan.fragments.end()) {
        diagnostic << " planned-fragments=";
        for (uint32_t fragment : planned->second)
          diagnostic << fragment << ",";
        diagnostic << " intersecting-direct=";
        for (const NativeDirectFragment &candidate : directFragments)
          if (llvm::any_of(planned->second, [&](uint32_t fragment) {
                return llvm::is_contained(candidate.fragmentIDs, fragment);
              })) {
            diagnostic << "[" << candidate.actorSlot << ":"
                       << candidate.continuation << ":";
            for (uint32_t fragment : candidate.fragmentIDs)
              diagnostic << fragment << ",";
            diagnostic << " anchors=";
            for (uint32_t fragment : candidate.ownershipAnchors)
              diagnostic << fragment << ",";
            diagnostic << "]";
          }
      }
      for (const NativeDirectFragment &direct : directFragments)
        if (direct.actorSlot == mergedFragments[index].actor_slot) {
          diagnostic << " direct-continuation=" << direct.continuation
                     << " direct-fragments=" << direct.fragmentIDs.size()
                     << " direct-ranges=" << direct.promotionRanges.size()
                     << " direct-two-state=" << !direct.twoStateWrapper.empty();
          for (uint32_t fragment : direct.fragmentIDs)
            diagnostic << " fragment=" << fragment;
        }
      diagnostic << " available-direct=";
      for (const NativeDirectFragment &direct : directFragments)
        diagnostic << "[" << direct.actorSlot << ":" << direct.continuation
                   << ":" << direct.fusionGroup << ":"
                   << !direct.twoStateWrapper.empty() << "]";
    }
    return failure();
  }
  if (generatedEvalLoop) {
    // One model-wide ready bit owns each direct fragment. Physical trigger
    // groups remain distinct and merely OR into that shared model mask, so a
    // fragment reached by coincident clocks executes once before the common
    // NBA barrier.
    SmallVector<SmallVector<std::pair<uint64_t, uint64_t>>> clockMasks(
        periodicClocks.size(),
        SmallVector<std::pair<uint64_t, uint64_t>>(clockKernels.size(),
                                                   {0, 0}));
    SmallVector<SmallVector<std::pair<uint64_t, uint64_t>>> clockDirectMasks(
        periodicClocks.size(),
        SmallVector<std::pair<uint64_t, uint64_t>>(clockKernels.size(),
                                                   {0, 0}));
    auto canDispatchClockOwnerDirectly = [&](uint32_t bit) {
      auto record = llvm::find_if(mergedFragments, [&](const auto &candidate) {
        return candidate.bit == bit;
      });
      if (record == mergedFragments.end())
        return false;
      size_t index = static_cast<size_t>(record - mergedFragments.begin());
      if (mergedExecutors[index].empty())
        return false;
      LLVM::LLVMFuncOp executor = module.lookupSymbol<LLVM::LLVMFuncOp>(
          mergedExecutors[index]);
      // Keep checkpoint-producing owners in the coordinator so its existing
      // termination check remains the sole exit from the closed hot loop.
      return executor && executor->hasAttr("obelisk.eval.infallible") &&
             !executor->hasAttr("obelisk.eval.may_terminate");
    };
    auto clockTouchesFanout = [&](const NativePeriodicClock &clock,
                                  const obelisk_rt_static_fanout_entry &fanout) {
      if (fanout.static_state != clock.staticState || fanout.bit_width == 0)
        return false;
      auto bound = llvm::find_if(stateLayout.bounds, [&](const auto &candidate) {
        return candidate.handleID == clock.staticState;
      });
      if (bound == stateLayout.bounds.end() ||
          clock.bitOffset < bound->offset ||
          clock.bitOffset - bound->offset >= bound->width)
        return false;
      uint64_t localBit = clock.bitOffset - bound->offset;
      return fanout.low_bit <= localBit &&
             localBit - fanout.low_bit < fanout.bit_width;
    };
    for (auto [clockIndex, clock] : llvm::enumerate(periodicClocks))
      for (const obelisk_rt_static_fanout_entry &fanout : fanoutEntries) {
        if (fanout.reserved == 0 ||
            !clockTouchesFanout(clock, fanout) ||
            fanout.kernel >= clockKernels.size() ||
            fanout.merged_bit >= 64)
          continue;
        if (llvm::any_of(periodicAliases, [&](const NativePeriodicAlias &alias) {
              return alias.sourceStaticState == clock.staticState &&
                     alias.sourceBitOffset == clock.bitOffset &&
                     alias.forwardingActorSlot == fanout.actor_slot &&
                     alias.forwardingContinuation == fanout.continuation;
            }))
          continue;
        uint64_t bit = uint64_t{1} << fanout.merged_bit;
        auto &masks = canDispatchClockOwnerDirectly(fanout.merged_bit)
                          ? clockDirectMasks[clockIndex][fanout.kernel]
                          : clockMasks[clockIndex][fanout.kernel];
        auto &[rising, falling] = masks;
        if (fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_POSEDGE)
          rising |= bit;
        if (fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_NEGEDGE)
          falling |= bit;
      }
    for (const NativePeriodicAlias &alias : periodicAliases) {
      auto clock = llvm::find_if(periodicClocks, [&](const auto &candidate) {
        return candidate.staticState == alias.sourceStaticState &&
               candidate.bitOffset == alias.sourceBitOffset;
      });
      if (clock == periodicClocks.end())
        continue;
      size_t clockIndex = static_cast<size_t>(clock - periodicClocks.begin());
      for (const obelisk_rt_static_fanout_entry &fanout : fanoutEntries) {
        if (fanout.reserved == 0 ||
            (fanout.actor_slot == alias.forwardingActorSlot &&
             fanout.continuation == alias.forwardingContinuation) ||
            fanout.static_state != alias.targetStaticState ||
            fanout.low_bit != 0 || fanout.bit_width == 0 ||
            fanout.kernel >= clockKernels.size() || fanout.merged_bit >= 64)
          continue;
        uint64_t bit = uint64_t{1} << fanout.merged_bit;
        auto &masks = canDispatchClockOwnerDirectly(fanout.merged_bit)
                          ? clockDirectMasks[clockIndex][fanout.kernel]
                          : clockMasks[clockIndex][fanout.kernel];
        auto &[rising, falling] = masks;
        if (fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_POSEDGE)
          rising |= bit;
        if (fanout.edge == OBELISK_RT_WAIT_EDGE_CHANGE ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_BOTH ||
            fanout.edge == OBELISK_RT_WAIT_EDGE_NEGEDGE)
          falling |= bit;
      }
    }

    Value nextEdges = entryAlloca(builder, location, i64,
                                  periodicClocks.size(), 8);
    Type controlType = LLVM::LLVMStructType::getLiteral(
        context, {pointer, pointer, i64});
    Value control = entryAlloca(builder, location, controlType, 1, 8);
    Value nodes = LLVM::AddressOfOp::create(builder, location, pointer,
                                            nodesName);
    Value clocks = LLVM::AddressOfOp::create(
        builder, location, pointer, "__obelisk_periodic_clock_plan_v1");
    Value aliases =
        periodicAliases.empty()
            ? LLVM::ZeroOp::create(builder, location, pointer).getResult()
            : LLVM::AddressOfOp::create(builder, location, pointer,
                                        periodicAliasesName)
                  .getResult();
    Value prepareStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(
                context, "obelisk_rt_v1_scheduler_prepare_periodic_aot"),
            ValueRange{runEntry->getArgument(1), nodes,
                       llvmConstant(builder, location, i32,
                                    executableNodes.size()),
                       clocks,
                       llvmConstant(builder, location, i32,
                                    periodicClocks.size()),
                       aliases,
                       llvmConstant(builder, location, i32,
                                    periodicAliases.size()),
                       nextEdges, control})
            .getResult();
    Value prepared = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, prepareStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));

    Block *preparedEntry = new Block;
    Block *promote = new Block;
    Block *loop = new Block;
    Block *step = new Block;
    Block *dispatchStep = new Block;
    Block *executeStep = new Block;
    Block *silentFall = nullptr;
    Block *advanceSilentFall = nullptr;
    Block *afterStep = new Block;
    Block *handoff = new Block;
    Block *failed = new Block;
    run.getBody().push_back(preparedEntry);
    run.getBody().push_back(promote);
    run.getBody().push_back(loop);
    run.getBody().push_back(step);
    run.getBody().push_back(dispatchStep);
    run.getBody().push_back(executeStep);
    uint64_t directOwnerMask = 0;
    for (const auto &clock : clockDirectMasks)
      for (const auto &[rising, falling] : clock)
        directOwnerMask |= rising | falling;
    SmallVector<unsigned> directOwnerRecords;
    for (auto [index, record] : llvm::enumerate(mergedFragments))
      if (record.bit < 64 &&
          (directOwnerMask & (uint64_t{1} << record.bit)) != 0)
        directOwnerRecords.push_back(static_cast<unsigned>(index));
    bool canCompressSilentFall =
        periodicClocks.size() == 1 &&
        llvm::all_of(clockMasks.front(), [](const auto &masks) {
          return masks.second == 0;
        }) &&
        llvm::all_of(clockDirectMasks.front(), [](const auto &masks) {
          return masks.second == 0;
        });
    if (canCompressSilentFall) {
      silentFall = new Block;
      advanceSilentFall = new Block;
      run.getBody().push_back(silentFall);
      run.getBody().push_back(advanceSilentFall);
    }
    run.getBody().push_back(afterStep);
    run.getBody().push_back(handoff);
    run.getBody().push_back(failed);
    afterStep->addArgument(i32, location);
    handoff->addArgument(i32, location);
    cf::CondBranchOp::create(builder, location, prepared, preparedEntry,
                             ValueRange{}, failed, ValueRange{prepareStatus});

    builder.setInsertionPointToStart(preparedEntry);
    bool forcedTwoState = module->hasAttr("obelisk.eval.force_two_state");
    if (forcedTwoState)
      LLVM::MemsetOp::create(
          builder, location,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    "__obelisk_state_unknown"),
          llvmConstant(builder, location, builder.getI8Type(), 0),
          llvmConstant(builder, location, i64,
                       (stateLayout.bitCount + 7) / 8),
          /*isVolatile=*/false);
    Value preparedTerminationAddress =
        loadAt(builder, location, control, 8, pointer, 8);
    // The runtime is not re-entered while this generated transaction runs, so
    // its control addresses and deadline are immutable until handoff. Capture
    // them once outside the hot loop; direct bodies have no runtime escape
    // through which a new timed callback could be installed.
    Value preparedTimeAddress =
        loadAt(builder, location, control, 0, pointer, 8);
    Value preparedDeadline =
        loadAt(builder, location, control, 16, i64, 8);
    Value terminationSlot = LLVM::AddressOfOp::create(
        builder, location, pointer, periodicTerminationName);
    LLVM::StoreOp::create(builder, location, preparedTerminationAddress,
                          terminationSlot, 8);
    Value preparedTermination = LLVM::LoadOp::create(
        builder, location, i32, preparedTerminationAddress, 4);
    Value alreadyStopping = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, preparedTermination,
        llvmConstant(builder, location, i32, 0));
    Value enterHotLoop = arith::XOrIOp::create(
        builder, location, alreadyStopping,
        llvmConstant(builder, location, builder.getI1Type(), 1));
    cf::CondBranchOp::create(
        builder, location, enterHotLoop, promote, ValueRange{}, handoff,
        ValueRange{llvmConstant(builder, location, i32,
                                OBELISK_RT_AOT_CHECKPOINT)});

    builder.setInsertionPointToStart(promote);
    // Synthetic region kernels do not have a source coroutine whose initial
    // activation can run during bootstrap. Seed exactly those combinational
    // owners once; clocked and ordinary always-process bodies were already
    // evaluated by the generic time-zero drain and must not be replayed.
    uint64_t initialMask = 0;
    for (auto [index, executor] : llvm::enumerate(mergedExecutors)) {
      auto direct = llvm::find_if(directFragments, [&](const auto &candidate) {
        return candidate.wrapper == executor;
      });
      if (mergedFragments[index].bit < 64 &&
          direct != directFragments.end() && direct->initialActivation)
        initialMask |= uint64_t{1} << mergedFragments[index].bit;
    }
    if (initialMask != 0) {
      Value ingress = LLVM::AddressOfOp::create(
          builder, location, pointer, clockKernels.front().ingressName);
      Value pending = LLVM::LoadOp::create(builder, location, i64, ingress, 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::OrIOp::create(
              builder, location, pending,
              llvmConstant(builder, location, i64, initialMask)),
          ingress, 8);
      // Re-establish combinational quiescence before the first generated
      // clock edge. The runtime cold prefix may end immediately after a
      // clocked reset continuation; deferring these level-sensitive owners
      // until the first edge lets clocked logic sample stale canonical nets.
      Value initialStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, evalCoordinatorName),
              ValueRange{runEntry->getArgument(0), runEntry->getArgument(1)})
              .getResult();
      Value initialized = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, initialStatus,
          llvmConstant(builder, location, i32, OBELISK_RT_OK));
      cf::CondBranchOp::create(builder, location, initialized, loop,
                               ValueRange{}, failed,
                               ValueRange{initialStatus});
    } else {
      cf::BranchOp::create(builder, location, loop);
    }

    builder.setInsertionPointToStart(loop);
    Value nextTime = LLVM::LoadOp::create(builder, location, i64, nextEdges, 8);
    for (uint32_t index = 1; index != periodicClocks.size(); ++index) {
      Value candidate = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(builder, location, nextEdges,
                  uint64_t{index} * sizeof(uint64_t)),
          8);
      Value earlier = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, candidate, nextTime);
      nextTime = arith::SelectOp::create(builder, location, earlier, candidate,
                                         nextTime);
    }
    Value runtimeDue = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ule, preparedDeadline,
        nextTime);
    step->addArgument(i64, location);
    cf::CondBranchOp::create(
        builder, location, runtimeDue, handoff,
        ValueRange{llvmConstant(builder, location, i32,
                                OBELISK_RT_AOT_TIMED_CHECKPOINT)},
        step, ValueRange{nextTime});

    builder.setInsertionPointToStart(step);
    LLVM::StoreOp::create(builder, location, step->getArgument(0),
                          preparedTimeAddress, 8);
    Value hasIngress = llvmConstant(builder, location, builder.getI1Type(), 0);
    Value directReady = llvmConstant(builder, location, i64, 0);
    Value stateValue = LLVM::AddressOfOp::create(builder, location, pointer,
                                                 "__obelisk_state_value");
    for (auto [clockIndex, clock] : llvm::enumerate(periodicClocks)) {
      Value edgeAddress = byteGEP(builder, location, nextEdges,
                                  uint64_t{clockIndex} * sizeof(uint64_t));
      Value edge = LLVM::LoadOp::create(builder, location, i64, edgeAddress, 8);
      // With one periodic source, nextTime is this source's edge by
      // construction. Materialize that proof as a constant so canonicalize
      // and LLVM can remove the otherwise redundant due/select chain. The
      // general equality-based path remains for coincident multi-clock edges.
      Value due;
      if (periodicClocks.size() == 1)
        due = llvmConstant(builder, location, builder.getI1Type(), 1);
      else
        due = arith::CmpIOp::create(builder, location,
                                    arith::CmpIPredicate::eq, edge,
                                    step->getArgument(0));
      Value noOverflow = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ule, edge,
          llvmConstant(builder, location, i64,
                       UINT64_MAX - clock.halfPeriod));
      Value advancedCandidate = arith::AddIOp::create(
          builder, location, edge,
          llvmConstant(builder, location, i64, clock.halfPeriod));
      Value advanced = arith::SelectOp::create(
          builder, location, noOverflow, advancedCandidate,
          llvmConstant(builder, location, i64, UINT64_MAX));
      LLVM::StoreOp::create(
          builder, location,
          arith::SelectOp::create(builder, location, due, advanced, edge),
          edgeAddress, 8);

      Value byteAddress =
          byteGEP(builder, location, stateValue, clock.bitOffset / 8);
      Value oldByte = LLVM::LoadOp::create(builder, location,
                                           builder.getI8Type(), byteAddress, 1);
      uint8_t bitMask = uint8_t{1} << (clock.bitOffset % 8);
      Value oldSet = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(
              builder, location, oldByte,
              llvmConstant(builder, location, builder.getI8Type(), bitMask)),
          llvmConstant(builder, location, builder.getI8Type(), 0));
      Value toggled = arith::XOrIOp::create(
          builder, location, oldByte,
          llvmConstant(builder, location, builder.getI8Type(), bitMask));
      LLVM::StoreOp::create(
          builder, location,
          arith::SelectOp::create(builder, location, due, toggled, oldByte),
          byteAddress, 1);

      for (const NativePeriodicAlias &alias : periodicAliases) {
        if (alias.sourceStaticState != clock.staticState ||
            alias.sourceBitOffset != clock.bitOffset)
          continue;
        // The forwarding driver's canonical plane is not consumed inside the
        // closed generated loop. Keep the resolved net current for model
        // reads, and reconstruct the driver at a runtime/checkpoint handoff.
        // This avoids maintaining a checkpoint-only projection per edge.
        for (uint64_t bitOffset : {alias.targetBitOffset}) {
          Value aliasAddress =
              byteGEP(builder, location, stateValue, bitOffset / 8);
          Value aliasOld = LLVM::LoadOp::create(
              builder, location, builder.getI8Type(), aliasAddress, 1);
          uint8_t aliasMask = uint8_t{1} << (bitOffset % 8);
          Value aliasValue = arith::SelectOp::create(
              builder, location, oldSet,
              arith::AndIOp::create(
                  builder, location, aliasOld,
                  llvmConstant(builder, location, builder.getI8Type(),
                               static_cast<uint8_t>(~aliasMask))),
              arith::OrIOp::create(
                  builder, location, aliasOld,
                  llvmConstant(builder, location, builder.getI8Type(),
                               aliasMask)));
          LLVM::StoreOp::create(
              builder, location,
              arith::SelectOp::create(builder, location, due, aliasValue,
                                      aliasOld),
              aliasAddress, 1);
        }
      }

      for (auto [kernelIndex, kernel] : llvm::enumerate(clockKernels)) {
        auto [riseMask, fallMask] = clockMasks[clockIndex][kernelIndex];
        if (riseMask == 0 && fallMask == 0)
          continue;
        Value edgeMask = arith::SelectOp::create(
            builder, location, oldSet,
            llvmConstant(builder, location, i64, fallMask),
            llvmConstant(builder, location, i64, riseMask));
        Value selected = arith::SelectOp::create(
            builder, location, due, edgeMask,
            llvmConstant(builder, location, i64, 0));
        hasIngress = arith::OrIOp::create(
            builder, location, hasIngress,
            arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::ne, selected,
                llvmConstant(builder, location, i64, 0)));
        Value ingress = LLVM::AddressOfOp::create(
            builder, location, pointer, kernel.ingressName);
        Value pending = LLVM::LoadOp::create(builder, location, i64, ingress, 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, pending, selected), ingress,
            8);
      }
      for (auto [kernelIndex, kernel] : llvm::enumerate(clockKernels)) {
        auto [riseMask, fallMask] =
            clockDirectMasks[clockIndex][kernelIndex];
        if (riseMask == 0 && fallMask == 0)
          continue;
        Value edgeMask = arith::SelectOp::create(
            builder, location, oldSet,
            llvmConstant(builder, location, i64, fallMask),
            llvmConstant(builder, location, i64, riseMask));
        Value selected = arith::SelectOp::create(
            builder, location, due, edgeMask,
            llvmConstant(builder, location, i64, 0));
        directReady =
            arith::OrIOp::create(builder, location, directReady, selected);
        hasIngress = arith::OrIOp::create(
            builder, location, hasIngress,
            arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::ne, selected,
                llvmConstant(builder, location, i64, 0)));
      }
    }
    cf::BranchOp::create(builder, location, dispatchStep);

    builder.setInsertionPointToStart(dispatchStep);
    // Promotion is selected in two monotonic stages. Once the physical clock
    // entry owners are known, a compact steady coordinator guards the whole
    // ready mask against still-pending downstream owners. Once the exact
    // closure is known, the final trusted coordinator drops that guard too.
    Value periodicPromoted = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne,
        LLVM::LoadOp::create(
            builder, location, builder.getI8Type(),
            LLVM::AddressOfOp::create(builder, location, pointer,
                                      periodicPromotionLatchedName),
            1),
        llvmConstant(builder, location, builder.getI8Type(), 0));
    Value periodicEntryPromoted = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne,
        LLVM::LoadOp::create(
            builder, location, builder.getI8Type(),
            LLVM::AddressOfOp::create(
                builder, location, pointer,
                periodicEntryPromotionLatchedName),
            1),
        llvmConstant(builder, location, builder.getI8Type(), 0));
    Value dispatchTrustedTwoState =
        forcedTwoState
            ? llvmConstant(builder, location, builder.getI1Type(), 1)
            : periodicPromoted;
    Value dispatchTwoState =
        forcedTwoState
            ? llvmConstant(builder, location, builder.getI1Type(), 1)
            : arith::OrIOp::create(builder, location, periodicPromoted,
                                   periodicEntryPromoted);
    Value stepFourStateFallback = LLVM::AddressOfOp::create(
        builder, location, pointer, evalStepFourStateFallbackName);
    auto resetStepFourStateTracking = [&] {
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI8Type(), 0),
          stepFourStateFallback, 1);
      if (nbaTaintWordCount == 0)
        return;
      Value taint = LLVM::AddressOfOp::create(
          builder, location, pointer, evalStepFourStateNBARootsName);
      for (uint32_t word = 0; word != nbaTaintWordCount; ++word)
        LLVM::StoreOp::create(
            builder, location, llvmConstant(builder, location, i64, 0),
            byteGEP(builder, location, taint,
                    uint64_t{word} * sizeof(uint64_t)),
            8);
    };
    // The compressed single-clock path below has a structurally separate
    // trusted two-state block. Keep transient four-state accounting out of
    // that block entirely; it is initialized only when routing through the
    // hybrid prefix. General coincident-clock dispatch still shares one
    // coordinator entry, so retain its common initialization here.
    if (!canCompressSilentFall || directOwnerRecords.empty())
      resetStepFourStateTracking();
    // Clock-edge owners are already quiescent with respect to the preceding
    // slot, so invoke them directly and reserve the cttz coordinator for the
    // publications they produce. This is the compact dispatch shape of the
    // original ceiling evaluator, generalized to a bitset so coincident
    // clocks execute a shared owner exactly once.
    if (canCompressSilentFall && !directOwnerRecords.empty()) {
      // The only generated step is now the rising phase of one proven
      // periodic source. Its direct owner set is a compile-time constant, so
      // outline the fully promoted path as a straight-line instance sequence.
      // The transient prefix selects each instance independently.
      Block *prepareDirectHybrid = new Block;
      Block *executeDirectHybrid = new Block;
      Block *executeDirectTwoState = new Block;
      Block *afterDirectSequence = new Block;
      run.getBody().push_back(prepareDirectHybrid);
      run.getBody().push_back(executeDirectHybrid);
      run.getBody().push_back(executeDirectTwoState);
      run.getBody().push_back(afterDirectSequence);
      uint64_t directPromotionMask = 0;
      for (unsigned recordIndex : directOwnerRecords)
        if (!mergedTwoStateExecutors[recordIndex].empty() &&
            mergedFragments[recordIndex].bit < 64)
          directPromotionMask |=
              uint64_t{1} << mergedFragments[recordIndex].bit;
      // Select the steady variant before touching any transient promotion or
      // four-state bookkeeping. This makes the post-promotion hot CFG match
      // the original straight-line ceiling evaluator while preserving the
      // independent per-instance promotion checks in the cold prefix.
      cf::CondBranchOp::create(builder, location, dispatchTrustedTwoState,
                               executeDirectTwoState, ValueRange{},
                               prepareDirectHybrid, ValueRange{});
      builder.setInsertionPointToStart(prepareDirectHybrid);
      resetStepFourStateTracking();
      Value directPending = LLVM::LoadOp::create(
          builder, location, i64,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    promotionPendingMaskName),
          8);
      Value directOwnersPromoted = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq,
          arith::AndIOp::create(
              builder, location, directPending,
              llvmConstant(builder, location, i64, directPromotionMask)),
          llvmConstant(builder, location, i64, 0));
      cf::CondBranchOp::create(builder, location, directOwnersPromoted,
                               executeDirectTwoState, ValueRange{},
                               executeDirectHybrid, ValueRange{});
      auto clearDirectOwner = [&](unsigned recordIndex) {
          // Preserve coordinator fixpoint semantics between owners.  A
          // preceding owner may republish an earlier clock owner; clearing
          // the whole initial mask after the sequence would erase that
          // required retrigger.  Clear only the owner just executed, exactly
          // where the bitset coordinator would consume it.
          uint64_t inverse =
              ~(uint64_t{1} << mergedFragments[recordIndex].bit);
          for (const GeneratedClockKernel &kernel : clockKernels) {
            Value ingress = LLVM::AddressOfOp::create(
                builder, location, pointer, kernel.ingressName);
            Value queued =
                LLVM::LoadOp::create(builder, location, i64, ingress, 8);
            LLVM::StoreOp::create(
                builder, location,
                arith::AndIOp::create(
                    builder, location, queued,
                    llvmConstant(builder, location, i64, inverse)),
                ingress, 8);
          }
      };
      builder.setInsertionPointToStart(executeDirectTwoState);
      for (unsigned recordIndex : directOwnerRecords) {
        if (mergedTwoStateExecutors[recordIndex].empty()) {
          LLVM::StoreOp::create(
              builder, location,
              llvmConstant(builder, location, builder.getI8Type(), 1),
              stepFourStateFallback, 1);
          if (ownerMayTaintNBA(recordIndex))
            markOwnerNBATaint(recordIndex);
        }
        StringRef executor = mergedTwoStateExecutors[recordIndex].empty()
                                 ? StringRef(mergedExecutors[recordIndex])
                                 : StringRef(
                                       mergedTwoStateExecutors[recordIndex]);
        (void)LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, executor),
            ValueRange{runEntry->getArgument(1)});
        clearDirectOwner(recordIndex);
      }
      cf::BranchOp::create(builder, location, afterDirectSequence);

      Block *hybridCursor = executeDirectHybrid;
      for (unsigned recordIndex : directOwnerRecords) {
        builder.setInsertionPointToStart(hybridCursor);
        if (promotionKernelReadyNames[recordIndex].empty()) {
          LLVM::StoreOp::create(
              builder, location,
              llvmConstant(builder, location, builder.getI8Type(), 1),
              stepFourStateFallback, 1);
          if (ownerMayTaintNBA(recordIndex))
            markOwnerNBATaint(recordIndex);
          (void)LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, mergedExecutors[recordIndex]),
              ValueRange{runEntry->getArgument(1)});
          clearDirectOwner(recordIndex);
          continue;
        }
        Block *executeFourState = new Block;
        Block *executeTwoState = new Block;
        Block *nextOwner = new Block;
        run.getBody().push_back(executeFourState);
        run.getBody().push_back(executeTwoState);
        run.getBody().push_back(nextOwner);
        Value kernelReady = LLVM::CallOp::create(
                                builder, location,
                                TypeRange{builder.getI1Type()},
                                SymbolRefAttr::get(
                                    context,
                                    promotionKernelReadyNames[recordIndex]),
                                ValueRange{})
                                .getResult();
        cf::CondBranchOp::create(builder, location, kernelReady,
                                 executeTwoState, ValueRange{},
                                 executeFourState, ValueRange{});
        builder.setInsertionPointToStart(executeFourState);
        LLVM::StoreOp::create(
            builder, location,
            llvmConstant(builder, location, builder.getI8Type(), 1),
            stepFourStateFallback, 1);
        if (ownerMayTaintNBA(recordIndex))
          markOwnerNBATaint(recordIndex);
        (void)LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, mergedExecutors[recordIndex]),
            ValueRange{runEntry->getArgument(1)});
        clearDirectOwner(recordIndex);
        cf::BranchOp::create(builder, location, nextOwner);
        builder.setInsertionPointToStart(executeTwoState);
        (void)LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               mergedTwoStateExecutors[recordIndex]),
            ValueRange{runEntry->getArgument(1)});
        clearDirectOwner(recordIndex);
        cf::BranchOp::create(builder, location, nextOwner);
        hybridCursor = nextOwner;
      }
      builder.setInsertionPointToStart(hybridCursor);
      cf::BranchOp::create(builder, location, afterDirectSequence);
      builder.setInsertionPointToStart(afterDirectSequence);
    }
    if (!canCompressSilentFall)
      for (unsigned recordIndex : directOwnerRecords) {
      const auto &record = mergedFragments[recordIndex];
      Value selected = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(
              builder, location, directReady,
              llvmConstant(builder, location, i64,
                           uint64_t{1} << record.bit)),
          llvmConstant(builder, location, i64, 0));
      Block *executeDirect = new Block;
      Block *nextDirect = new Block;
      run.getBody().push_back(executeDirect);
      run.getBody().push_back(nextDirect);
      cf::CondBranchOp::create(builder, location, selected, executeDirect,
                               ValueRange{}, nextDirect, ValueRange{});
      builder.setInsertionPointToStart(executeDirect);
      StringRef fourState = mergedExecutors[recordIndex];
      StringRef twoState = mergedTwoStateExecutors[recordIndex];
      if (!twoState.empty() && twoState != fourState) {
        Block *executeFourState = new Block;
        Block *executeTwoState = new Block;
        Block *afterExecute = new Block;
        run.getBody().push_back(executeFourState);
        run.getBody().push_back(executeTwoState);
        run.getBody().push_back(afterExecute);
        Value kernelReady =
            LLVM::CallOp::create(
                builder, location, TypeRange{builder.getI1Type()},
                SymbolRefAttr::get(context,
                                   promotionKernelReadyNames[recordIndex]),
                ValueRange{})
                .getResult();
        cf::CondBranchOp::create(builder, location, kernelReady,
                                 executeTwoState, ValueRange{},
                                 executeFourState, ValueRange{});
        builder.setInsertionPointToStart(executeFourState);
        LLVM::StoreOp::create(
            builder, location,
            llvmConstant(builder, location, builder.getI8Type(), 1),
            stepFourStateFallback, 1);
        if (ownerMayTaintNBA(recordIndex))
          markOwnerNBATaint(recordIndex);
        Value fourStateStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(context, fourState),
                ValueRange{runEntry->getArgument(1)})
                .getResult();
        Value fourStateOK = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, fourStateStatus,
            llvmConstant(builder, location, i32, OBELISK_RT_OK));
        cf::CondBranchOp::create(builder, location, fourStateOK, afterExecute,
                                 ValueRange{}, afterStep,
                                 ValueRange{fourStateStatus});
        builder.setInsertionPointToStart(executeTwoState);
        Value twoStateStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(context, twoState),
                ValueRange{runEntry->getArgument(1)})
                .getResult();
        Value twoStateOK = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, twoStateStatus,
            llvmConstant(builder, location, i32, OBELISK_RT_OK));
        cf::CondBranchOp::create(builder, location, twoStateOK, afterExecute,
                                 ValueRange{}, afterStep,
                                 ValueRange{twoStateStatus});
        builder.setInsertionPointToStart(afterExecute);
      } else {
        LLVM::StoreOp::create(
            builder, location,
            llvmConstant(builder, location, builder.getI8Type(), 1),
            stepFourStateFallback, 1);
        if (ownerMayTaintNBA(recordIndex))
          markOwnerNBATaint(recordIndex);
        Value directStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(context, fourState),
                ValueRange{runEntry->getArgument(1)})
                .getResult();
        Value directOK = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, directStatus,
            llvmConstant(builder, location, i32, OBELISK_RT_OK));
        Block *afterExecute = new Block;
        run.getBody().push_back(afterExecute);
        cf::CondBranchOp::create(builder, location, directOK, afterExecute,
                                 ValueRange{}, afterStep,
                                 ValueRange{directStatus});
        builder.setInsertionPointToStart(afterExecute);
      }
      // Match coordinator execution semantics: an implicit combinational
      // sensitivity cannot retrigger the owner that is currently executing.
      // Fused clock/combinational bodies commonly publish one of their own
      // roots, so leaving this bit queued would execute the whole clock body a
      // second time in the fixpoint scan.
      for (const GeneratedClockKernel &kernel : clockKernels) {
        Value ingress = LLVM::AddressOfOp::create(
            builder, location, pointer, kernel.ingressName);
        Value queued = LLVM::LoadOp::create(builder, location, i64, ingress, 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::AndIOp::create(
                builder, location, queued,
                llvmConstant(builder, location, i64,
                             ~(uint64_t{1} << record.bit))),
            ingress, 8);
      }
      cf::BranchOp::create(builder, location, nextDirect);
      builder.setInsertionPointToStart(nextDirect);
      }
    cf::CondBranchOp::create(builder, location, hasIngress, executeStep,
                             ValueRange{}, loop, ValueRange{});

    builder.setInsertionPointToStart(executeStep);
    // The hybrid coordinator drains the transient four-state prefix and
    // independently latches owners as their closures become known. The steady
    // coordinator then runs two-state owners directly, but returns locally to
    // the hybrid path if any ready owner is still pending. Only a full exact-
    // closure proof selects the final guard-free coordinator and compact NBA
    // barrier.
    Block *executeHybridCoordinator = new Block;
    Block *selectSteadyCoordinator = new Block;
    Block *executeSteadyCoordinator = new Block;
    Block *executeTrustedCoordinator = new Block;
    Block *executeCoordinatorJoin = new Block;
    executeCoordinatorJoin->addArgument(i32, location);
    run.getBody().push_back(executeHybridCoordinator);
    run.getBody().push_back(selectSteadyCoordinator);
    run.getBody().push_back(executeSteadyCoordinator);
    run.getBody().push_back(executeTrustedCoordinator);
    run.getBody().push_back(executeCoordinatorJoin);
    cf::CondBranchOp::create(builder, location, dispatchTrustedTwoState,
                             executeTrustedCoordinator, ValueRange{},
                             selectSteadyCoordinator, ValueRange{});
    builder.setInsertionPointToStart(selectSteadyCoordinator);
    cf::CondBranchOp::create(builder, location, dispatchTwoState,
                             executeSteadyCoordinator, ValueRange{},
                             executeHybridCoordinator, ValueRange{});
    builder.setInsertionPointToStart(executeHybridCoordinator);
    Value hybridStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, evalHybridCoordinatorName),
            ValueRange{runEntry->getArgument(0), runEntry->getArgument(1)})
            .getResult();
    // The coordinator returns only at its local fixpoint (or on an exit that
    // immediately hands back to the runtime). Check the exact periodic
    // closure mask here; successful promotion changes routing on the next
    // edge and removes all per-owner promotion guards from the hot loop.
    Block *scanHybridPromotion = new Block;
    run.getBody().push_back(scanHybridPromotion);
    Value hybridOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, hybridStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(builder, location, hybridOK, scanHybridPromotion,
                             ValueRange{}, executeCoordinatorJoin,
                             ValueRange{hybridStatus});
    builder.setInsertionPointToStart(scanHybridPromotion);
    (void)LLVM::CallOp::create(
        builder, location, TypeRange{builder.getI1Type()},
        SymbolRefAttr::get(context, periodicPromotionReadyName), ValueRange{});
    cf::BranchOp::create(builder, location, executeCoordinatorJoin,
                         ValueRange{hybridStatus});

    builder.setInsertionPointToStart(executeSteadyCoordinator);
    Value steadyGuardFallback = entryAlloca(
        builder, location, builder.getI1Type(), 1, 1);
    LLVM::StoreOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI1Type(), 0),
        steadyGuardFallback, 1);
    Value steadyStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, evalSteadyTwoStateCoordinatorName),
            ValueRange{runEntry->getArgument(0), runEntry->getArgument(1),
                       preparedTerminationAddress, steadyGuardFallback})
            .getResult();
    Block *fallbackHybridCoordinator = new Block;
    Block *steadyJoin = new Block;
    steadyJoin->addArgument(i32, location);
    run.getBody().push_back(fallbackHybridCoordinator);
    run.getBody().push_back(steadyJoin);
    Value guardRejected = LLVM::LoadOp::create(
        builder, location, builder.getI1Type(), steadyGuardFallback, 1);
    cf::CondBranchOp::create(builder, location, guardRejected,
                             fallbackHybridCoordinator, ValueRange{},
                             steadyJoin, ValueRange{steadyStatus});
    builder.setInsertionPointToStart(fallbackHybridCoordinator);
    Value fallbackHybridStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, evalHybridCoordinatorName),
            ValueRange{runEntry->getArgument(0), runEntry->getArgument(1)})
            .getResult();
    Block *scanFallbackPromotion = new Block;
    run.getBody().push_back(scanFallbackPromotion);
    Value fallbackHybridOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, fallbackHybridStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(builder, location, fallbackHybridOK,
                             scanFallbackPromotion, ValueRange{}, steadyJoin,
                             ValueRange{fallbackHybridStatus});
    builder.setInsertionPointToStart(scanFallbackPromotion);
    (void)LLVM::CallOp::create(
        builder, location, TypeRange{builder.getI1Type()},
        SymbolRefAttr::get(context, periodicPromotionReadyName), ValueRange{});
    cf::BranchOp::create(builder, location, steadyJoin,
                         ValueRange{fallbackHybridStatus});
    builder.setInsertionPointToStart(steadyJoin);
    cf::BranchOp::create(builder, location, executeCoordinatorJoin,
                         ValueRange{steadyJoin->getArgument(0)});

    builder.setInsertionPointToStart(executeTrustedCoordinator);
    SmallVector<Value> twoStateCoordinatorArguments{
        runEntry->getArgument(0), runEntry->getArgument(1)};
    if (periodicPromotionComplete)
      twoStateCoordinatorArguments.push_back(preparedTerminationAddress);
    Value trustedStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(
                context, periodicPromotionComplete
                             ? evalPeriodicTwoStateCoordinatorName
                             : evalTwoStateCoordinatorName),
            twoStateCoordinatorArguments)
            .getResult();
    cf::BranchOp::create(builder, location, executeCoordinatorJoin,
                         ValueRange{trustedStatus});
    builder.setInsertionPointToStart(executeCoordinatorJoin);
    Value stepStatus = executeCoordinatorJoin->getArgument(0);
    Value stepOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, stepStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(
        builder, location, stepOK,
        canCompressSilentFall ? silentFall : loop, ValueRange{}, afterStep,
        ValueRange{stepStatus});

    if (canCompressSilentFall) {
      // A falling phase with no physical fanout is not an event region. It may
      // be compressed into the generated run_until loop provided no runtime
      // deadline occurs at or before that edge. This is the general form of
      // the cheap negedge in the original parity-ceiling loop.
      builder.setInsertionPointToStart(silentFall);
      Value fallTime = LLVM::LoadOp::create(builder, location, i64, nextEdges,
                                            8);
      Value runtimeAtFall = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ule, preparedDeadline,
          fallTime);
      cf::CondBranchOp::create(
          builder, location, runtimeAtFall, handoff,
          ValueRange{llvmConstant(builder, location, i32,
                                  OBELISK_RT_AOT_TIMED_CHECKPOINT)},
          advanceSilentFall, ValueRange{});

      builder.setInsertionPointToStart(advanceSilentFall);
      const NativePeriodicClock &clock = periodicClocks.front();
      Value sourceAddress = byteGEP(builder, location, stateValue,
                                    clock.bitOffset / 8);
      Value source = LLVM::LoadOp::create(builder, location,
                                          builder.getI8Type(), sourceAddress,
                                          1);
      uint8_t sourceMask = uint8_t{1} << (clock.bitOffset % 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::AndIOp::create(
              builder, location, source,
              llvmConstant(builder, location, builder.getI8Type(),
                           static_cast<uint8_t>(~sourceMask))),
          sourceAddress, 1);
      for (const NativePeriodicAlias &alias : periodicAliases) {
        if (alias.sourceStaticState != clock.staticState ||
            alias.sourceBitOffset != clock.bitOffset)
          continue;
        for (uint64_t bitOffset : {alias.targetBitOffset}) {
          Value address =
              byteGEP(builder, location, stateValue, bitOffset / 8);
          Value old = LLVM::LoadOp::create(builder, location,
                                           builder.getI8Type(), address, 1);
          uint8_t mask = uint8_t{1} << (bitOffset % 8);
          LLVM::StoreOp::create(
              builder, location,
              arith::AndIOp::create(
                  builder, location, old,
                  llvmConstant(builder, location, builder.getI8Type(),
                               static_cast<uint8_t>(~mask))),
              address, 1);
        }
      }
      Value noOverflow = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ule, fallTime,
          llvmConstant(builder, location, i64,
                       UINT64_MAX - clock.halfPeriod));
      Value nextRiseCandidate = arith::AddIOp::create(
          builder, location, fallTime,
          llvmConstant(builder, location, i64, clock.halfPeriod));
      Value nextRise = arith::SelectOp::create(
          builder, location, noOverflow, nextRiseCandidate,
          llvmConstant(builder, location, i64, UINT64_MAX));
      LLVM::StoreOp::create(builder, location, nextRise, nextEdges, 8);
      cf::BranchOp::create(builder, location, loop);
    }

    builder.setInsertionPointToStart(afterStep);
    // prepare_periodic_aot detaches the runtime clock deadlines.  Once that
    // succeeds every generated exit, including TierUnavailable and fatal body
    // statuses, must restore them before returning to the runtime.
    cf::BranchOp::create(builder, location, handoff,
                         ValueRange{afterStep->getArgument(0)});

    builder.setInsertionPointToStart(handoff);
    // Direct periodic forwarding elides checkpoint-only driver writes in the
    // hot loop. Reconstruct both canonical planes before runtime callbacks or
    // asynchronous Tier-2 intervention can observe them.
    Value stateUnknown = LLVM::AddressOfOp::create(
        builder, location, pointer, "__obelisk_state_unknown");
    Value handoffState = LLVM::AddressOfOp::create(
        builder, location, pointer, "__obelisk_state_value");
    for (const NativePeriodicAlias &alias : periodicAliases) {
      auto source = llvm::find_if(periodicClocks, [&](const auto &clock) {
        return clock.staticState == alias.sourceStaticState &&
               clock.bitOffset == alias.sourceBitOffset;
      });
      if (source == periodicClocks.end())
        continue;
      Value sourceAddress = byteGEP(builder, location, handoffState,
                                    source->bitOffset / 8);
      Value sourceByte = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(), sourceAddress, 1);
      uint8_t sourceMask = uint8_t{1} << (source->bitOffset % 8);
      Value sourceSet = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(
              builder, location, sourceByte,
              llvmConstant(builder, location, builder.getI8Type(),
                           sourceMask)),
          llvmConstant(builder, location, builder.getI8Type(), 0));
      Value driverAddress = byteGEP(builder, location, handoffState,
                                    alias.driverBitOffset / 8);
      Value driverByte = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(), driverAddress, 1);
      uint8_t driverMask = uint8_t{1} << (alias.driverBitOffset % 8);
      Value driverValue = arith::SelectOp::create(
          builder, location, sourceSet,
          arith::OrIOp::create(
              builder, location, driverByte,
              llvmConstant(builder, location, builder.getI8Type(),
                           driverMask)),
          arith::AndIOp::create(
              builder, location, driverByte,
              llvmConstant(builder, location, builder.getI8Type(),
                           static_cast<uint8_t>(~driverMask))));
      LLVM::StoreOp::create(builder, location, driverValue, driverAddress, 1);
      Value driverUnknownAddress = byteGEP(
          builder, location, stateUnknown, alias.driverBitOffset / 8);
      Value driverUnknown = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(), driverUnknownAddress, 1);
      LLVM::StoreOp::create(
          builder, location,
          arith::AndIOp::create(
              builder, location, driverUnknown,
              llvmConstant(builder, location, builder.getI8Type(),
                           static_cast<uint8_t>(~driverMask))),
          driverUnknownAddress, 1);
    }
    Value handoffStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(
                context, "obelisk_rt_v1_scheduler_handoff_periodic_aot"),
            ValueRange{runEntry->getArgument(1), clocks,
                       llvmConstant(builder, location, i32,
                                    periodicClocks.size()),
                       nextEdges})
            .getResult();
    Value handoffOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, handoffStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    LLVM::ReturnOp::create(
        builder, location,
        arith::SelectOp::create(
            builder, location, handoffOK,
            handoff->getArgument(0),
            handoffStatus));

    failed->addArgument(i32, location);
    builder.setInsertionPointToStart(failed);
    LLVM::ReturnOp::create(builder, location, failed->getArgument(0));
  } else {
    // Explicit eval is a proof/debug mode. Never silently measure the generic
    // scheduler when its direct run_until prerequisites are incomplete.
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, i32, OBELISK_RT_INVALID_DESIGN));
  }

  // The eval experiment has its own closed coordinator below.  Do not emit the
  // compatibility coordinator or actor wrappers: both contain runtime escape
  // hatches that are irrelevant to (and would contaminate) the ceiling test.
  if (!clockKernels.empty() && !evalScheduler) {
    builder.setInsertionPointToEnd(module.getBody());
    auto coordinator = LLVM::LLVMFuncOp::create(
        builder, location, coordinatorName,
        LLVM::LLVMFunctionType::get(i32, {pointer, pointer}, false));
    Block *entry = coordinator.addEntryBlock(builder);
    builder.setInsertionPointToStart(entry);
    uint32_t readyWordCount =
        static_cast<uint32_t>((executableNodes.size() + 63) / 64);
    Value ready = entryAlloca(builder, location, i64, readyWordCount, 8);
    for (uint32_t word = 0; word != readyWordCount; ++word)
      LLVM::StoreOp::create(
          builder, location, llvmConstant(builder, location, i64, 0),
          byteGEP(builder, location, ready, uint64_t{word} * sizeof(uint64_t)),
          8);

    // A forward pass is sufficient only when every publication targets a
    // later graph node.  Real RTL contains feedback and source-order inversions
    // (PicoRV's memory-data cone is one); those publications set ingress bits
    // for nodes already visited by this pass.  Re-enter the generated scan
    // until its model-local ingress mask is empty.  This is the native eval
    // fixpoint, not a return to the runtime worklist.
    Block *scan = new Block;
    coordinator.getBody().push_back(scan);
    cf::BranchOp::create(builder, location, scan);
    builder.setInsertionPointToStart(scan);

    struct NodeIngress {
      uint32_t node;
      SmallVector<unsigned> records;
      std::string execute;
    };
    SmallVector<NodeIngress> nodeIngress;
    for (auto [recordIndex, record] : llvm::enumerate(mergedFragments)) {
      auto found = llvm::find_if(nodeIngress, [&](const NodeIngress &entry) {
        return entry.node == record.compute_node;
      });
      if (found == nodeIngress.end()) {
        nodeIngress.push_back({record.compute_node,
                               {static_cast<unsigned>(recordIndex)},
                               mergedExecutors[recordIndex]});
      } else {
        found->records.push_back(static_cast<unsigned>(recordIndex));
        if (found->execute.empty())
          found->execute = mergedExecutors[recordIndex];
      }
    }
    llvm::sort(nodeIngress, [](const NodeIngress &lhs, const NodeIngress &rhs) {
      return lhs.node < rhs.node;
    });

    // Drain in graph order. The original actor remains suspended at the same
    // continuation while a clean private body runs; unsupported records are
    // translated to the exact fine-node fallback bit.
    for (const NodeIngress &node : nodeIngress) {
      Value selected = llvmConstant(builder, location, builder.getI1Type(), 0);
      for (unsigned recordIndex : node.records) {
        const auto &record = mergedFragments[recordIndex];
        const auto &kernel = clockKernels[record.kernel];
        Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  kernel.ingressName);
        Value address = byteGEP(builder, location, ingress,
                                uint64_t{record.bit / 64} * sizeof(uint64_t));
        Value word = LLVM::LoadOp::create(builder, location, i64, address, 8);
        uint64_t mask = uint64_t{1} << (record.bit % 64);
        Value set = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne,
            arith::AndIOp::create(builder, location, word,
                                  llvmConstant(builder, location, i64, mask)),
            llvmConstant(builder, location, i64, 0));
        selected = arith::OrIOp::create(builder, location, selected, set);
        LLVM::StoreOp::create(
            builder, location,
            arith::AndIOp::create(builder, location, word,
                                  llvmConstant(builder, location, i64, ~mask)),
            address, 8);
      }
      Block *execute = new Block;
      Block *next = new Block;
      coordinator.getBody().push_back(execute);
      coordinator.getBody().push_back(next);
      cf::CondBranchOp::create(builder, location, selected, execute,
                               ValueRange{}, next, ValueRange{});
      builder.setInsertionPointToStart(execute);
      if (!node.execute.empty()) {
        Value status =
            LLVM::CallOp::create(builder, location, TypeRange{i32},
                                 SymbolRefAttr::get(context, node.execute),
                                 ValueRange{entry->getArgument(1)})
                .getResult();
        // An implicit combinational sensitivity never retriggers its own
        // activation from a value it just published.  Encode that exclusion
        // in the generated ready mask instead of consulting coroutine actor
        // state in the runtime.
        for (unsigned recordIndex : node.records) {
          const auto &record = mergedFragments[recordIndex];
          const auto &kernel = clockKernels[record.kernel];
          Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                    kernel.ingressName);
          Value address = byteGEP(builder, location, ingress,
                                  uint64_t{record.bit / 64} * sizeof(uint64_t));
          Value word = LLVM::LoadOp::create(builder, location, i64, address, 8);
          LLVM::StoreOp::create(
              builder, location,
              arith::AndIOp::create(
                  builder, location, word,
                  llvmConstant(builder, location, i64,
                               ~(uint64_t{1} << (record.bit % 64)))),
              address, 8);
        }
        Value ok = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, status,
            llvmConstant(builder, location, i32, OBELISK_RT_OK));
        Block *failed = new Block;
        coordinator.getBody().push_back(failed);
        cf::CondBranchOp::create(builder, location, ok, next, ValueRange{},
                                 failed, ValueRange{});
        builder.setInsertionPointToStart(failed);
        LLVM::ReturnOp::create(builder, location, status);
      } else if (!evalScheduler) {
        Value readyAddress =
            byteGEP(builder, location, ready,
                    uint64_t{node.node / 64} * sizeof(uint64_t));
        Value oldReady =
            LLVM::LoadOp::create(builder, location, i64, readyAddress, 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, oldReady,
                                 llvmConstant(builder, location, i64,
                                              uint64_t{1} << (node.node % 64))),
            readyAddress, 8);
        cf::BranchOp::create(builder, location, next);
      } else {
        cf::BranchOp::create(builder, location, next);
      }
      builder.setInsertionPointToStart(next);
    }
    if (!evalScheduler)
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context,
                             "obelisk_rt_v1_scheduler_activate_static_nodes"),
          ValueRange{entry->getArgument(1), ready,
                     llvmConstant(builder, location, i32, readyWordCount)});
    if (evalScheduler) {
      Value pending = llvmConstant(builder, location, i64, 0);
      for (const GeneratedClockKernel &kernel : clockKernels) {
        Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  kernel.ingressName);
        uint32_t words = ingressWordCount(kernel);
        for (uint32_t word = 0; word != words; ++word) {
          Value address = byteGEP(builder, location, ingress,
                                  uint64_t{word} * sizeof(uint64_t));
          Value queued =
              LLVM::LoadOp::create(builder, location, i64, address, 8);
          pending = arith::OrIOp::create(builder, location, pending, queued);
        }
      }
      Value dirty = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, pending,
          llvmConstant(builder, location, i64, 0));
      Block *done = new Block;
      coordinator.getBody().push_back(done);
      cf::CondBranchOp::create(builder, location, dirty, scan, ValueRange{},
                               done, ValueRange{});
      builder.setInsertionPointToStart(done);
      Value changed = entryAlloca(builder, location, i32, 1, 4);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i32, 0), changed,
                            4);
      Value commitStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, nbaCommitName),
              ValueRange{entry->getArgument(0), entry->getArgument(1),
                         llvmConstant(builder, location, i32, 2), changed})
              .getResult();
      Value commitOK = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, commitStatus,
          llvmConstant(builder, location, i32, OBELISK_RT_OK));
      Block *afterCommit = new Block;
      Block *commitFailed = new Block;
      coordinator.getBody().push_back(afterCommit);
      coordinator.getBody().push_back(commitFailed);
      cf::CondBranchOp::create(builder, location, commitOK, afterCommit,
                               ValueRange{}, commitFailed, ValueRange{});
      builder.setInsertionPointToStart(commitFailed);
      LLVM::ReturnOp::create(builder, location, commitStatus);
      builder.setInsertionPointToStart(afterCommit);
      Value postCommitPending = llvmConstant(builder, location, i64, 0);
      for (const GeneratedClockKernel &kernel : clockKernels) {
        Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  kernel.ingressName);
        uint32_t words = ingressWordCount(kernel);
        for (uint32_t word = 0; word != words; ++word)
          postCommitPending = arith::OrIOp::create(
              builder, location, postCommitPending,
              LLVM::LoadOp::create(builder, location, i64,
                                   byteGEP(builder, location, ingress,
                                           uint64_t{word} * sizeof(uint64_t)),
                                   8));
      }
      Value needsPostNBAEval = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, postCommitPending,
          llvmConstant(builder, location, i64, 0));
      Block *complete = new Block;
      coordinator.getBody().push_back(complete);
      cf::CondBranchOp::create(builder, location, needsPostNBAEval, scan,
                               ValueRange{}, complete, ValueRange{});
      builder.setInsertionPointToStart(complete);
      LLVM::ReturnOp::create(builder, location, commitStatus);
    } else {
      LLVM::ReturnOp::create(
          builder, location,
          llvmConstant(builder, location, i32, OBELISK_RT_OK));
    }
  }

  bool generatedEvalHasPathGuards = llvm::any_of(
      mergedTwoStateExecutors, [&](const std::string &name) {
        auto function = name.empty()
                            ? LLVM::LLVMFuncOp{}
                            : module.lookupSymbol<LLVM::LLVMFuncOp>(name);
        return function &&
               function->hasAttr("obelisk.eval.path_guarded_two_state");
      });

  // Keep the exceptional unknown-NBA drain out of coordinators that are
  // intentionally inlined into run_until. Otherwise each promoted variant
  // duplicates the complete four-state coordinator on a cold edge and
  // needlessly expands the instruction working set of the periodic loop.
  constexpr StringLiteral evalFourStateNBAHandoffName =
      "__obelisk_eval_four_state_nba_handoff_v1";

  auto makeFastCoordinator =
      [&](StringRef functionName, ArrayRef<std::string> executors,
          bool promotedCoordinator,
          bool hybridCoordinator = false,
          uint64_t allowedOwnerMask = UINT64_MAX,
          bool trustedTwoState = false,
          bool guardPendingOwners = false,
          bool observePathFallback = false) -> LogicalResult {
    if (!evalScheduler || clockKernels.empty() || mergedFragments.empty() ||
        mergedFragments.size() > 64 ||
        executors.size() != mergedFragments.size())
      return success();
    bool coordinatorHasConvergence = false;
    for (auto [recordIndex, record] : llvm::enumerate(mergedFragments)) {
      if (record.bit >= 64 ||
          (allowedOwnerMask & (uint64_t{1} << record.bit)) == 0)
        continue;
      for (StringRef name : {StringRef(executors[recordIndex]),
                             StringRef(mergedTwoStateExecutors[recordIndex])})
        if (!name.empty())
          if (auto function = module.lookupSymbol<LLVM::LLVMFuncOp>(name))
            coordinatorHasConvergence |=
                function->hasAttr("obelisk.eval.tier2_convergence");
    }
    builder.setInsertionPointToEnd(module.getBody());
    SmallVector<Type> coordinatorArguments{pointer, pointer};
    if (trustedTwoState)
      coordinatorArguments.push_back(pointer);
    if (guardPendingOwners)
      coordinatorArguments.push_back(pointer);
    auto fastCoordinator = LLVM::LLVMFuncOp::create(
        builder, location, functionName,
        LLVM::LLVMFunctionType::get(i32, coordinatorArguments, false));
    if (promotedCoordinator && !hybridCoordinator)
      fastCoordinator->setAttr("obelisk.eval.two_state_variant",
                               builder.getUnitAttr());
    if (trustedTwoState && !guardPendingOwners)
      fastCoordinator->setAttr("obelisk.eval.trusted_two_state_coordinator",
                               builder.getUnitAttr());
    fastCoordinator->setAttr("obelisk.eval.call_closure_root",
                             builder.getUnitAttr());
    // Hybrid coordinators are module-instance scheduling bodies.  Leave them
    // to normal LLVM profitability so the periodic run loop can inline a
    // profitable model while large generated bodies remain out of line.  An
    // explicit noinline boundary prevented loop/state simplification and was
    // the largest remaining difference from the ceiling prototype.
    bool observesFourStateFallback =
        hybridCoordinator || guardPendingOwners || observePathFallback;
    if (!hybridCoordinator &&
        !(trustedTwoState && !observesFourStateFallback))
      fastCoordinator->setAttr(
          "passthrough",
          builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
    Block *fastEntry = fastCoordinator.addEntryBlock(builder);
    Block *dispatch = new Block;
    Block *commit = new Block;
    Block *afterCommit = new Block;
    Block *complete = new Block;
    Block *stopped = new Block;
    Block *guardRejected = guardPendingOwners ? new Block : nullptr;
    Block *failed = new Block;
    failed->addArgument(i32, location);
    fastCoordinator.getBody().push_back(dispatch);
    fastCoordinator.getBody().push_back(commit);
    fastCoordinator.getBody().push_back(afterCommit);
    fastCoordinator.getBody().push_back(complete);
    fastCoordinator.getBody().push_back(stopped);
    if (guardRejected)
      fastCoordinator.getBody().push_back(guardRejected);
    fastCoordinator.getBody().push_back(failed);
    builder.setInsertionPointToStart(fastEntry);
    if (guardPendingOwners) {
      unsigned guardArgument = trustedTwoState ? 3 : 2;
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI1Type(), 0),
          fastEntry->getArgument(guardArgument), 1);
    }
    Value changed = entryAlloca(builder, location, i32, 1, 4);
    Value fourStateFallback = entryAlloca(builder, location,
                                          builder.getI1Type(), 1, 1);
    LLVM::StoreOp::create(
        builder, location,
        (trustedTwoState && !observesFourStateFallback)
            ? llvmConstant(builder, location, builder.getI1Type(), 0)
            : (promotedCoordinator || hybridCoordinator || guardPendingOwners)
            ? arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::ne,
                  LLVM::LoadOp::create(
                      builder, location, builder.getI8Type(),
                      LLVM::AddressOfOp::create(
                          builder, location, pointer,
                          evalStepFourStateFallbackName),
                      1),
                  llvmConstant(builder, location, builder.getI8Type(), 0))
                  .getResult()
            : llvmConstant(builder, location, builder.getI1Type(), 0),
        fourStateFallback, 1);
    auto combinedIngress = [&] {
      if (evalScheduler) {
        Value ingress = LLVM::AddressOfOp::create(
            builder, location, pointer, clockKernels.front().ingressName);
        return LLVM::LoadOp::create(builder, location, i64, ingress, 8)
            .getResult();
      }
      Value combined = llvmConstant(builder, location, i64, 0);
      for (const GeneratedClockKernel &kernel : clockKernels) {
        Value ingress = LLVM::AddressOfOp::create(
            builder, location, pointer, kernel.ingressName);
        combined = arith::OrIOp::create(
            builder, location, combined,
            LLVM::LoadOp::create(builder, location, i64, ingress, 8));
      }
      return combined;
    };
    auto clearIngressMask = [&](Value mask) {
      Value inverse = arith::XOrIOp::create(
          builder, location, mask,
          llvmConstant(builder, location, i64, UINT64_MAX));
      if (evalScheduler) {
        Value ingress = LLVM::AddressOfOp::create(
            builder, location, pointer, clockKernels.front().ingressName);
        Value queued =
            LLVM::LoadOp::create(builder, location, i64, ingress, 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::AndIOp::create(builder, location, queued, inverse), ingress,
            8);
        return;
      }
      for (const GeneratedClockKernel &kernel : clockKernels) {
        Value ingress = LLVM::AddressOfOp::create(
            builder, location, pointer, kernel.ingressName);
        Value queued =
            LLVM::LoadOp::create(builder, location, i64, ingress, 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::AndIOp::create(builder, location, queued, inverse), ingress,
            8);
      }
    };
    cf::BranchOp::create(builder, location, dispatch);
    builder.setInsertionPointToStart(dispatch);
    Value ready = combinedIngress();
    Value empty =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              ready, llvmConstant(builder, location, i64, 0));
    Block *select = new Block;
    fastCoordinator.getBody().push_back(select);
    cf::CondBranchOp::create(builder, location, empty, commit, ValueRange{},
                             select, ValueRange{});

    builder.setInsertionPointToStart(select);
    Value bit =
        LLVM::CountTrailingZerosOp::create(builder, location, i64, ready, true);
    Value selectedMask = arith::ShLIOp::create(
        builder, location, llvmConstant(builder, location, i64, 1), bit);
    Block *switchBlock = select;
    if (guardPendingOwners) {
      // Preserve the global ready-bit order while draining the maximal native
      // prefix.  An uncovered owner later in the set must not force already
      // ordered, covered owners back through the hybrid coordinator.
      Value unsafeSelected = arith::AndIOp::create(
          builder, location, selectedMask,
          llvmConstant(builder, location, i64, ~allowedOwnerMask));
      Value selectedIsUnsafe = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, unsafeSelected,
          llvmConstant(builder, location, i64, 0));
      switchBlock = new Block;
      fastCoordinator.getBody().push_back(switchBlock);
      cf::CondBranchOp::create(builder, location, selectedIsUnsafe,
                               guardRejected, ValueRange{}, switchBlock,
                               ValueRange{});
      builder.setInsertionPointToStart(switchBlock);
    }
    Value selectedPromotionPending;
    // The trusted steady coordinator has already crossed its quiescent
    // promotion boundary.  Its path-guarded executors perform their own exact
    // CFG check, while every other executor is directly two-state.  Only the
    // transitional hybrid coordinator needs per-owner promotion scans here.
    if (hybridCoordinator) {
      Value pending = LLVM::LoadOp::create(
          builder, location, i64,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    promotionPendingMaskName),
          8);
      selectedPromotionPending = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(builder, location, pending, selectedMask),
          llvmConstant(builder, location, i64, 0));
    }
    SmallVector<APInt> cases;
    SmallVector<Block *> destinations;
    SmallVector<ValueRange> destinationOperands;
    for (auto [recordIndex, record] : llvm::enumerate(mergedFragments)) {
      if (record.bit >= 64 || executors[recordIndex].empty())
        continue;
      if ((allowedOwnerMask & (uint64_t{1} << record.bit)) == 0)
        continue;
      Block *execute = new Block;
      fastCoordinator.getBody().push_back(execute);
      cases.push_back(APInt(64, record.bit));
      destinations.push_back(execute);
      destinationOperands.push_back(ValueRange{});
      builder.setInsertionPointToStart(execute);
      if ((promotedCoordinator || hybridCoordinator) &&
          mergedTwoStateExecutors[recordIndex].empty()) {
        LLVM::StoreOp::create(
            builder, location,
            llvmConstant(builder, location, builder.getI1Type(), 1),
            fourStateFallback, 1);
        if (ownerMayTaintNBA(recordIndex))
          markOwnerNBATaint(recordIndex);
      }
      Value executeStatus;
      if (hybridCoordinator &&
          !mergedTwoStateExecutors[recordIndex].empty() &&
          !promotionKernelReadyNames[recordIndex].empty()) {
        Block *checkPromotion = new Block;
        Block *executeFourState = new Block;
        Block *executeTwoState = new Block;
        Block *executeJoin = new Block;
        executeJoin->addArgument(i32, location);
        fastCoordinator.getBody().push_back(checkPromotion);
        fastCoordinator.getBody().push_back(executeFourState);
        fastCoordinator.getBody().push_back(executeTwoState);
        fastCoordinator.getBody().push_back(executeJoin);
        cf::CondBranchOp::create(builder, location,
                                 selectedPromotionPending, checkPromotion,
                                 ValueRange{}, executeTwoState, ValueRange{});
        builder.setInsertionPointToStart(checkPromotion);
        Value kernelReady =
            LLVM::CallOp::create(
                builder, location, TypeRange{builder.getI1Type()},
                SymbolRefAttr::get(
                    context, promotionKernelReadyNames[recordIndex]),
                ValueRange{})
                .getResult();
        cf::CondBranchOp::create(builder, location, kernelReady,
                                 executeTwoState, ValueRange{},
                                 executeFourState, ValueRange{});
        builder.setInsertionPointToStart(executeFourState);
        LLVM::StoreOp::create(
            builder, location,
            llvmConstant(builder, location, builder.getI1Type(), 1),
            fourStateFallback, 1);
        if (ownerMayTaintNBA(recordIndex))
          markOwnerNBATaint(recordIndex);
        Value fourStateStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(
                    context, hybridCoordinator
                                 ? executors[recordIndex]
                                 : mergedExecutors[recordIndex]),
                ValueRange{fastEntry->getArgument(1)})
                .getResult();
        cf::BranchOp::create(builder, location, executeJoin,
                             ValueRange{fourStateStatus});
        builder.setInsertionPointToStart(executeTwoState);
        Value twoStateStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(
                    context, mergedTwoStateExecutors[recordIndex]),
                ValueRange{fastEntry->getArgument(1)})
                .getResult();
        cf::BranchOp::create(builder, location, executeJoin,
                             ValueRange{twoStateStatus});
        builder.setInsertionPointToStart(executeJoin);
        executeStatus = executeJoin->getArgument(0);
      } else {
        executeStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(context, executors[recordIndex]),
                ValueRange{fastEntry->getArgument(1)})
                .getResult();
      }
      clearIngressMask(llvmConstant(builder, location, i64,
                                    uint64_t{1} << record.bit));
      Value executeOK = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, executeStatus,
          llvmConstant(builder, location, i32, OBELISK_RT_OK));
      auto executor = module.lookupSymbol<LLVM::LLVMFuncOp>(
          guardPendingOwners ? mergedExecutors[recordIndex]
                             : executors[recordIndex]);
      auto twoStateExecutor = module.lookupSymbol<LLVM::LLVMFuncOp>(
          mergedTwoStateExecutors[recordIndex]);
      bool mayTerminate =
          (executor && executor->hasAttr("obelisk.eval.may_terminate")) ||
          (twoStateExecutor &&
           twoStateExecutor->hasAttr("obelisk.eval.may_terminate"));
      bool infallible =
          executor && executor->hasAttr("obelisk.eval.infallible") &&
          (!twoStateExecutor ||
           twoStateExecutor->hasAttr("obelisk.eval.infallible"));
      if (infallible && !mayTerminate) {
        cf::BranchOp::create(builder, location, dispatch);
        continue;
      }
      if (!mayTerminate) {
        cf::CondBranchOp::create(builder, location, executeOK, dispatch,
                                 ValueRange{}, failed,
                                 ValueRange{executeStatus});
        continue;
      }
      Block *checkTermination = new Block;
      Block *loadTermination = trustedTwoState ? nullptr : new Block;
      fastCoordinator.getBody().push_back(checkTermination);
      if (loadTermination)
        fastCoordinator.getBody().push_back(loadTermination);
      cf::CondBranchOp::create(builder, location, executeOK, checkTermination,
                               ValueRange{}, failed,
                               ValueRange{executeStatus});
      builder.setInsertionPointToStart(checkTermination);
      Value terminationAddress = trustedTwoState
                                     ? fastEntry->getArgument(2)
                                     : LLVM::LoadOp::create(
                                           builder, location, pointer,
                                           LLVM::AddressOfOp::create(
                                               builder, location, pointer,
                                               periodicTerminationName),
                                           8)
                                           .getResult();
      if (trustedTwoState) {
        Value termination = LLVM::LoadOp::create(
            builder, location, i32, terminationAddress, 4);
        Value stopping = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, termination,
            llvmConstant(builder, location, i32, 0));
        cf::CondBranchOp::create(builder, location, stopping, stopped,
                                 ValueRange{}, dispatch, ValueRange{});
        continue;
      }
      Value noTerminationAddress = LLVM::ICmpOp::create(
          builder, location, LLVM::ICmpPredicate::eq, terminationAddress,
          LLVM::ZeroOp::create(builder, location, pointer));
      cf::CondBranchOp::create(builder, location, noTerminationAddress,
                               dispatch, ValueRange{}, loadTermination,
                               ValueRange{});
      builder.setInsertionPointToStart(loadTermination);
      Value termination = LLVM::LoadOp::create(
          builder, location, i32, terminationAddress, 4);
      Value stopping = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, termination,
          llvmConstant(builder, location, i32, 0));
      cf::CondBranchOp::create(builder, location, stopping, stopped,
                               ValueRange{}, dispatch, ValueRange{});
    }
    builder.setInsertionPointToEnd(switchBlock);
    LLVM::SwitchOp::create(builder, location, bit, stopped, ValueRange{}, cases,
                           destinations, destinationOperands,
                           ArrayRef<int32_t>{});

    builder.setInsertionPointToStart(commit);
    if (observesFourStateFallback) {
      // A path dispatcher can reject after coordinator entry.  Observe that
      // rejection at the barrier so its four-state staging is never committed
      // by the compact value-plane-only path.
      Value dispatcherFallback = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          LLVM::LoadOp::create(
              builder, location, builder.getI8Type(),
              LLVM::AddressOfOp::create(builder, location, pointer,
                                        evalStepFourStateFallbackName),
              1),
          llvmConstant(builder, location, builder.getI8Type(), 0));
      Value priorFallback = LLVM::LoadOp::create(
          builder, location, builder.getI1Type(), fourStateFallback, 1);
      LLVM::StoreOp::create(
          builder, location,
          arith::OrIOp::create(builder, location, priorFallback,
                               dispatcherFallback),
          fourStateFallback, 1);
    }
    if (coordinatorHasConvergence) {
      // Poll once per settled Tier-2 transaction. A future direct SCC
      // subkernel can move this observation onto its true local backedge; a
      // coordinator-side poll after the subkernel returns cannot interrupt an
      // oscillation and only adds work for every convergence owner.
      Block *commitBody = new Block;
      Block *loadTermination = trustedTwoState ? nullptr : new Block;
      fastCoordinator.getBody().push_back(commitBody);
      if (loadTermination)
        fastCoordinator.getBody().push_back(loadTermination);
      Value terminationAddress =
          trustedTwoState
              ? fastEntry->getArgument(2)
              : LLVM::LoadOp::create(
                    builder, location, pointer,
                    LLVM::AddressOfOp::create(
                        builder, location, pointer, periodicTerminationName),
                    8)
                    .getResult();
      if (trustedTwoState) {
        Value termination = LLVM::LoadOp::create(
            builder, location, i32, terminationAddress, 4);
        Value stopping = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, termination,
            llvmConstant(builder, location, i32, 0));
        cf::CondBranchOp::create(builder, location, stopping, stopped,
                                 ValueRange{}, commitBody, ValueRange{});
        builder.setInsertionPointToStart(commitBody);
      } else {
        Value noTerminationAddress = LLVM::ICmpOp::create(
            builder, location, LLVM::ICmpPredicate::eq, terminationAddress,
            LLVM::ZeroOp::create(builder, location, pointer));
        cf::CondBranchOp::create(builder, location, noTerminationAddress,
                                 commitBody, ValueRange{}, loadTermination,
                                 ValueRange{});
        builder.setInsertionPointToStart(loadTermination);
        Value termination = LLVM::LoadOp::create(
            builder, location, i32, terminationAddress, 4);
        Value stopping = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, termination,
            llvmConstant(builder, location, i32, 0));
        cf::CondBranchOp::create(builder, location, stopping, stopped,
                                 ValueRange{}, commitBody, ValueRange{});
        builder.setInsertionPointToStart(commitBody);
      }
    }
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32, 0), changed, 4);
    Value commitStatus;
    if (trustedTwoState && !observesFourStateFallback) {
      auto fastTwoStateCall = LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, nbaCommitName),
          ValueRange{fastEntry->getArgument(0), fastEntry->getArgument(1),
                     llvmConstant(builder, location, i32, 2), changed});
      fastTwoStateCall->setAttr("obelisk.eval.use_fast_two_state_nba",
                                builder.getUnitAttr());
      commitStatus = fastTwoStateCall.getResult();
    } else if (!promotedCoordinator && !hybridCoordinator) {
      commitStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, nbaCommitName),
              ValueRange{fastEntry->getArgument(0), fastEntry->getArgument(1),
                         llvmConstant(builder, location, i32, 2), changed})
              .getResult();
    } else {
      Block *fastTwoStateCommit = new Block;
      Block *canonicalTwoStateCommit = new Block;
      Block *fourStateCommit = new Block;
      Block *checkFallbackNBA = new Block;
      Block *checkCanonicalNBA = new Block;
      Block *selectFastNBA = new Block;
      Block *latchFastNBA = new Block;
      Block *commitJoin = new Block;
      commitJoin->addArgument(i32, location);
      fastCoordinator.getBody().push_back(checkFallbackNBA);
      fastCoordinator.getBody().push_back(checkCanonicalNBA);
      fastCoordinator.getBody().push_back(selectFastNBA);
      fastCoordinator.getBody().push_back(latchFastNBA);
      fastCoordinator.getBody().push_back(fastTwoStateCommit);
      fastCoordinator.getBody().push_back(canonicalTwoStateCommit);
      fastCoordinator.getBody().push_back(fourStateCommit);
      fastCoordinator.getBody().push_back(commitJoin);
      Value usedFallback = LLVM::LoadOp::create(
          builder, location, builder.getI1Type(), fourStateFallback, 1);
      cf::CondBranchOp::create(builder, location, usedFallback,
                               checkFallbackNBA, ValueRange{}, selectFastNBA,
                               ValueRange{});
      auto markCurrentDirtyRootsFast = [&] {
        if (nbaTaintWordCount == 0)
          return;
        Value dirtyBase = LLVM::AddressOfOp::create(
            builder, location, pointer, nbaDirtyRootsName);
        Value fastRootsBase = LLVM::AddressOfOp::create(
            builder, location, pointer, evalFastNBARootsName);
        for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
          Value dirty = LLVM::LoadOp::create(
              builder, location, i64,
              byteGEP(builder, location, dirtyBase,
                      uint64_t{word} * sizeof(uint64_t)),
              8);
          Value fastAddress = byteGEP(builder, location, fastRootsBase,
                                      uint64_t{word} * sizeof(uint64_t));
          Value fastRoots = LLVM::LoadOp::create(builder, location, i64,
                                                 fastAddress, 8);
          LLVM::StoreOp::create(
              builder, location,
              arith::OrIOp::create(builder, location, fastRoots, dirty),
              fastAddress, 8);
        }
      };
      builder.setInsertionPointToStart(selectFastNBA);
      Value fastNBALatched = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          LLVM::LoadOp::create(
              builder, location, builder.getI8Type(),
              LLVM::AddressOfOp::create(builder, location, pointer,
                                        evalFastNBALatchedName),
              1),
          llvmConstant(builder, location, builder.getI8Type(), 0));
      if (nbaTaintWordCount != 0) {
        Value dirtyBase = LLVM::AddressOfOp::create(
            builder, location, pointer, nbaDirtyRootsName);
        Value fastRootsBase = LLVM::AddressOfOp::create(
            builder, location, pointer, evalFastNBARootsName);
        Value missingFastRoot = llvmConstant(
            builder, location, builder.getI1Type(), 0);
        for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
          Value dirty = LLVM::LoadOp::create(
              builder, location, i64,
              byteGEP(builder, location, dirtyBase,
                      uint64_t{word} * sizeof(uint64_t)),
              8);
          Value fastRoots = LLVM::LoadOp::create(
              builder, location, i64,
              byteGEP(builder, location, fastRootsBase,
                      uint64_t{word} * sizeof(uint64_t)),
              8);
          Value missing = arith::AndIOp::create(
              builder, location, dirty,
              arith::XOrIOp::create(
                  builder, location, fastRoots,
                  llvmConstant(builder, location, i64, UINT64_MAX)));
          missingFastRoot = arith::OrIOp::create(
              builder, location, missingFastRoot,
              arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::ne, missing,
                  llvmConstant(builder, location, i64, 0)));
        }
        fastNBALatched = arith::OrIOp::create(
            builder, location, fastNBALatched,
            arith::XOrIOp::create(
                builder, location, missingFastRoot,
                llvmConstant(builder, location, builder.getI1Type(), 1)));
      }
      cf::CondBranchOp::create(builder, location, fastNBALatched,
                               fastTwoStateCommit, ValueRange{},
                               checkCanonicalNBA, ValueRange{});
      builder.setInsertionPointToStart(checkFallbackNBA);
      // A four-state owner may stage only known values. If its dirty roots have
      // zero canonical and staged unknown bits, rejoin the ordinary handoff:
      // canonicalize all dirty roots once if needed, latch the compact barrier
      // at that quiescent boundary, and keep it selected on later clocks. This
      // prevents an X-valued monitor closure from downgrading unrelated DUT
      // registers forever while retaining the four-state barrier for a
      // genuinely unknown write.
      Value fallbackNBAKnown = LLVM::CallOp::create(
                                   builder, location,
                                   TypeRange{builder.getI1Type()},
                                   SymbolRefAttr::get(context, nbaKnownName),
                                   ValueRange{
                                       llvmConstant(builder, location, i32, 2),
                                       llvmConstant(builder, location,
                                                    builder.getI1Type(), 1),
                                       llvmConstant(builder, location,
                                                    builder.getI1Type(), 1)})
                                   .getResult();
      cf::CondBranchOp::create(builder, location, fallbackNBAKnown,
                               selectFastNBA, ValueRange{},
                               fourStateCommit, ValueRange{});
      builder.setInsertionPointToStart(checkCanonicalNBA);
      Value canonicalNBAKnown = LLVM::CallOp::create(
                                    builder, location,
                                    TypeRange{builder.getI1Type()},
                                    SymbolRefAttr::get(context, nbaKnownName),
                                    ValueRange{
                                        llvmConstant(builder, location, i32, 2),
                                        llvmConstant(builder, location,
                                                     builder.getI1Type(), 1),
                                        llvmConstant(builder, location,
                                                     builder.getI1Type(), 0)})
                                    .getResult();
      cf::CondBranchOp::create(builder, location, canonicalNBAKnown,
                               latchFastNBA, ValueRange{},
                               canonicalTwoStateCommit, ValueRange{});
      builder.setInsertionPointToStart(latchFastNBA);
      markCurrentDirtyRootsFast();
      cf::BranchOp::create(builder, location, fastTwoStateCommit);
      builder.setInsertionPointToStart(fastTwoStateCommit);
      auto fastTwoStateCall = LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, nbaCommitName),
          ValueRange{fastEntry->getArgument(0), fastEntry->getArgument(1),
                     llvmConstant(builder, location, i32, 2), changed});
      fastTwoStateCall->setAttr("obelisk.eval.use_fast_two_state_nba",
                                builder.getUnitAttr());
      cf::BranchOp::create(builder, location, commitJoin,
                           ValueRange{fastTwoStateCall.getResult()});
      builder.setInsertionPointToStart(canonicalTwoStateCommit);
      markCurrentDirtyRootsFast();
      auto canonicalTwoStateCall = LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, nbaCommitName),
          ValueRange{fastEntry->getArgument(0), fastEntry->getArgument(1),
                     llvmConstant(builder, location, i32, 2), changed});
      canonicalTwoStateCall->setAttr(
          "obelisk.eval.use_canonical_two_state_nba", builder.getUnitAttr());
      cf::BranchOp::create(builder, location, commitJoin,
                           ValueRange{canonicalTwoStateCall.getResult()});
      builder.setInsertionPointToStart(fourStateCommit);
      Value fourStateStatus = LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, evalFourStateNBAHandoffName),
          ValueRange{fastEntry->getArgument(0), fastEntry->getArgument(1),
                     changed})
          .getResult();
      LLVM::ReturnOp::create(builder, location, fourStateStatus);
      builder.setInsertionPointToStart(commitJoin);
      commitStatus = commitJoin->getArgument(0);
    }
    Value commitOK = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, commitStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(builder, location, commitOK, afterCommit,
                             ValueRange{}, failed, ValueRange{commitStatus});

    builder.setInsertionPointToStart(afterCommit);
    Value postNBAReady = combinedIngress();
    Value postNBAEmpty = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, postNBAReady,
        llvmConstant(builder, location, i64, 0));
    cf::CondBranchOp::create(builder, location, postNBAEmpty, complete,
                             ValueRange{}, dispatch, ValueRange{});
    builder.setInsertionPointToStart(complete);
    LLVM::ReturnOp::create(builder, location, commitStatus);
    builder.setInsertionPointToStart(stopped);
    LLVM::ReturnOp::create(builder, location,
                           llvmConstant(builder, location, i32,
                                        OBELISK_RT_TIER_UNAVAILABLE));
    if (guardRejected) {
      builder.setInsertionPointToStart(guardRejected);
      unsigned guardArgument = trustedTwoState ? 3 : 2;
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI1Type(), 1),
          fastEntry->getArgument(guardArgument), 1);
      LLVM::ReturnOp::create(builder, location,
                             llvmConstant(builder, location, i32,
                                          OBELISK_RT_TIER_UNAVAILABLE));
    }
    builder.setInsertionPointToStart(failed);
    LLVM::ReturnOp::create(builder, location, failed->getArgument(0));
    return success();
  };
  SmallVector<std::string> twoStateExecutors(mergedTwoStateExecutors);
  for (auto [index, name] : llvm::enumerate(twoStateExecutors))
    if (name.empty())
      name = mergedExecutors[index];
  if (failed(makeFastCoordinator(evalCoordinatorName, mergedExecutors,
                                 /*promotedCoordinator=*/false)) ||
      failed(makeFastCoordinator(evalTwoStateCoordinatorName,
                                 twoStateExecutors,
                                 /*promotedCoordinator=*/true,
                                 /*hybridCoordinator=*/false,
                                 /*allowedOwnerMask=*/UINT64_MAX,
                                 /*trustedTwoState=*/false,
                                 /*guardPendingOwners=*/false,
                                 /*observePathFallback=*/
                                     generatedEvalHasPathGuards)) ||
      failed(makeFastCoordinator(evalSteadyTwoStateCoordinatorName,
                                 twoStateExecutors,
                                 /*promotedCoordinator=*/true,
                                 /*hybridCoordinator=*/false,
                                 /*allowedOwnerMask=*/periodicPromotionMask,
                                 /*trustedTwoState=*/true,
                                 /*guardPendingOwners=*/true,
                                 /*observePathFallback=*/
                                     generatedEvalHasPathGuards)) ||
      (periodicPromotionComplete &&
       failed(makeFastCoordinator(evalPeriodicTwoStateCoordinatorName,
                                  twoStateExecutors,
                                  /*promotedCoordinator=*/true,
                                  /*hybridCoordinator=*/false,
                                  /*allowedOwnerMask=*/periodicPromotionMask,
                                  /*trustedTwoState=*/true,
                                  /*guardPendingOwners=*/false,
                                  /*observePathFallback=*/
                                      periodicPromotionHasPathGuards))) ||
      failed(makeFastCoordinator(evalHybridCoordinatorName, mergedExecutors,
                                 /*promotedCoordinator=*/false,
                                 /*hybridCoordinator=*/true)))
    return failure();

  // Emit the complete exceptional transaction after every coordinator.  A
  // genuinely unknown NBA must invalidate the persistent two-state routes
  // and settle its post-NBA fanout in four-state mode, but keeping those
  // blocks in each coordinator perturbs register allocation in the periodic
  // hot loop even when the edge is never taken.
  builder.setInsertionPointToEnd(module.getBody());
  auto evalFourStateNBAHandoff = LLVM::LLVMFuncOp::create(
      builder, location, evalFourStateNBAHandoffName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, pointer}, false));
  evalFourStateNBAHandoff->setAttr(
      "passthrough",
      builder.getArrayAttr(
          {builder.getStringAttr("noinline"), builder.getStringAttr("cold")}));
  Block *handoffEntry = evalFourStateNBAHandoff.addEntryBlock(builder);
  Block *afterCommit = new Block;
  Block *complete = new Block;
  Block *settle = new Block;
  evalFourStateNBAHandoff.getBody().push_back(afterCommit);
  evalFourStateNBAHandoff.getBody().push_back(complete);
  evalFourStateNBAHandoff.getBody().push_back(settle);
  builder.setInsertionPointToStart(handoffEntry);
  LLVM::StoreOp::create(
      builder, location,
      llvmConstant(builder, location, builder.getI8Type(), 0),
      LLVM::AddressOfOp::create(builder, location, pointer,
                                evalFastNBALatchedName),
      1);
  if (nbaTaintWordCount != 0) {
    Value taintBase = LLVM::AddressOfOp::create(
        builder, location, pointer, evalStepFourStateNBARootsName);
    Value fastRootsBase = LLVM::AddressOfOp::create(
        builder, location, pointer, evalFastNBARootsName);
    for (uint32_t word = 0; word != nbaTaintWordCount; ++word) {
      Value taint = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(builder, location, taintBase,
                  uint64_t{word} * sizeof(uint64_t)),
          8);
      Value fastAddress = byteGEP(builder, location, fastRootsBase,
                                  uint64_t{word} * sizeof(uint64_t));
      Value fastRoots =
          LLVM::LoadOp::create(builder, location, i64, fastAddress, 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::AndIOp::create(
              builder, location, fastRoots,
              arith::XOrIOp::create(
                  builder, location, taint,
                  llvmConstant(builder, location, i64, UINT64_MAX))),
          fastAddress, 8);
    }
  }
  auto fourStateCall = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, nbaCommitName),
      ValueRange{handoffEntry->getArgument(0), handoffEntry->getArgument(1),
                 llvmConstant(builder, location, i32, 2),
                 handoffEntry->getArgument(2)});
  fourStateCall->setAttr("obelisk.eval.keep_four_state_nba",
                         builder.getUnitAttr());
  Value commitOK = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, fourStateCall.getResult(),
      llvmConstant(builder, location, i32, OBELISK_RT_OK));
  cf::CondBranchOp::create(builder, location, commitOK, afterCommit,
                           ValueRange{}, complete, ValueRange{});
  builder.setInsertionPointToStart(afterCommit);
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(context, promotionInvalidateName), ValueRange{});
  Value postNBAReady = llvmConstant(builder, location, i64, 0);
  for (const GeneratedClockKernel &kernel : clockKernels) {
    Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                              kernel.ingressName);
    postNBAReady = arith::OrIOp::create(
        builder, location, postNBAReady,
        LLVM::LoadOp::create(builder, location, i64, ingress, 8));
    if (evalScheduler)
      break;
  }
  Value postNBAEmpty = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, postNBAReady,
      llvmConstant(builder, location, i64, 0));
  cf::CondBranchOp::create(builder, location, postNBAEmpty, complete,
                           ValueRange{}, settle, ValueRange{});
  builder.setInsertionPointToStart(settle);
  Value settleStatus = LLVM::CallOp::create(
                           builder, location, TypeRange{i32},
                           SymbolRefAttr::get(context, evalCoordinatorName),
                           ValueRange{handoffEntry->getArgument(0),
                                      handoffEntry->getArgument(1)})
                           .getResult();
  LLVM::ReturnOp::create(builder, location, settleStatus);
  builder.setInsertionPointToStart(complete);
  LLVM::ReturnOp::create(builder, location, fourStateCall.getResult());

  builder.setInsertionPointToEnd(module.getBody());
  auto snapshot = LLVM::LLVMFuncOp::create(
      builder, location, snapshotName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, pointer}, false));
  Block *snapshotEntry = snapshot.addEntryBlock(builder);
  builder.setInsertionPointToStart(snapshotEntry);
  Value snapshotStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_scheduler_snapshot_aot"),
          ValueRange{snapshotEntry->getArgument(1),
                     snapshotEntry->getArgument(2)})
          .getResult();
  LLVM::ReturnOp::create(builder, location, snapshotStatus);

  builder.setInsertionPointToEnd(module.getBody());
  auto nbaCommit = LLVM::LLVMFuncOp::create(
      builder, location, nbaCommitName,
      LLVM::LLVMFunctionType::get(i32, {pointer, pointer, i32, pointer},
                                  false));
  if (evalScheduler)
    nbaCommit->setAttr(
        "passthrough",
        builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
  Block *nbaCommitEntry = nbaCommit.addEntryBlock(builder);
  Block *genericNBACommit = new Block;
  nbaCommit.getBody().push_back(genericNBACommit);
  builder.setInsertionPointToStart(nbaCommitEntry);
  Value committedCount = entryAlloca(builder, location, i32, 1, 4);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i32, 0), committedCount,
                        4);

  bool generateScalarCommits =
      cleanSuperstepEnabled && enableDirectState &&
      !guardedSpecializationEnabled &&
      staticNBAPlan.generatedOffsets.size() == nbaRoots.size();
  SmallVector<SmallVector<uint32_t>> scalarRootsByWord(nbaDirtyWordCount);
  uint64_t planeBytes = (stateLayout.bitCount + 7) / 8 + sizeof(uint64_t);
  if (generateScalarCommits)
    for (auto [rootIndex, root, accumulator, offset] :
         llvm::enumerate(nbaRoots, staticNBAPlan.generatedAccumulators,
                         staticNBAPlan.generatedOffsets)) {
      uint64_t firstByte = offset / 8;
      uint64_t shift = offset % 8;
      bool crossesWord = root.bit_width > 64 - shift;
      bool addressable = root.bit_width != 0 && root.bit_width <= 64 &&
                         !accumulator.empty() && firstByte + 8 <= planeBytes &&
                         (!crossesWord || firstByte + 9 <= planeBytes);
      if (addressable)
        scalarRootsByWord[rootIndex / 64].push_back(
            static_cast<uint32_t>(rootIndex));
    }
  // Record roots actually consumed by the direct path. The generic
  // continuation still owns the canonical dirty hierarchy, but need not
  // rediscover that every generated accumulator in a leaf was cleared.
  SmallVector<Value> directlyCommittedByWord(nbaDirtyWordCount);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word)
    if (!evalScheduler && !scalarRootsByWord[word].empty()) {
      directlyCommittedByWord[word] = entryAlloca(builder, location, i64, 1, 8);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            directlyCommittedByWord[word], 8);
    }

  bool generateGroupedFanout =
      llvm::any_of(scalarRootsByWord, [&](ArrayRef<uint32_t> roots) {
        return llvm::any_of(roots, [&](uint32_t rootIndex) {
          return llvm::any_of(
              fanoutEntries, [&](const obelisk_rt_static_fanout_entry &entry) {
                return entry.static_state == nbaRoots[rootIndex].static_state;
              });
        });
      });
  uint32_t activationWordCount =
      generateGroupedFanout
          ? static_cast<uint32_t>((executableNodes.size() + 63) / 64)
          : 0;
  // Eval mode owns NBA fanout as well as active-region fanout.  The generated
  // commit epilogue translates changed roots straight into model-method bits
  // and immediately drains the generated fixpoint; it never reconstructs a
  // runtime ready-node worklist.
  uint32_t directActivationWordCount =
      evalScheduler ? static_cast<uint32_t>((mergedFragments.size() + 63) / 64)
                    : 0;
  Value activatedNodes;
  Value activatedDirect;
  if (generateGroupedFanout) {
    activatedNodes =
        entryAlloca(builder, location, i64, activationWordCount, 8);
    for (uint32_t word = 0; word != activationWordCount; ++word)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, activatedNodes,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
  }
  // Direct eval publication is independent of whether any generic grouped
  // fanout exists.  In particular, an eval-only design can have no
  // `activatedNodes` bitmap while still publishing fixed NBA roots to direct
  // fragments.  Keep this alloca governed by its own word count so the later
  // direct epilogue never observes an unset MLIR Value.
  if (directActivationWordCount != 0) {
    activatedDirect =
        entryAlloca(builder, location, i64, directActivationWordCount, 8);
    for (uint32_t word = 0; word != directActivationWordCount; ++word)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, activatedDirect,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
  }

  Value directEnabled;
  if (evalScheduler) {
    directEnabled = llvmConstant(builder, location, builder.getI1Type(), 1);
  } else {
    Value directGuard =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_static_nba_direct_commit_guard"),
            ValueRange{nbaCommitEntry->getArgument(1)})
            .getResult();
    directEnabled = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, directGuard,
        llvmConstant(builder, location, i32, 0));
  }
  Value stateValue = LLVM::AddressOfOp::create(builder, location, pointer,
                                               "__obelisk_state_value");
  Value stateUnknown = LLVM::AddressOfOp::create(builder, location, pointer,
                                                 "__obelisk_state_unknown");
  bool forcedTwoStateEval =
      evalScheduler && module->hasAttr("obelisk.eval.force_two_state");
  for (const DynamicEvalNBA &entry : dynamicEvalNBAs) {
    Value validAddress =
        LLVM::AddressOfOp::create(builder, location, pointer, entry.validName);
    Value valid = LLVM::LoadOp::create(builder, location, i32, validAddress, 4);
    Value active =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                              valid, llvmConstant(builder, location, i32, 0));
    Value dynamicBit = LLVM::LoadOp::create(
        builder, location, i64,
        LLVM::AddressOfOp::create(builder, location, pointer, entry.offsetName),
        8);
    Value maximumOffset = llvmConstant(
        builder, location, i64,
        staticNBAPlan.roots[entry.rootIndex].bit_width - entry.width);
    Value inBounds = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ule, dynamicBit,
        maximumOffset);
    Value safeDynamicBit = arith::SelectOp::create(
        builder, location, inBounds, dynamicBit,
        llvmConstant(builder, location, i64, 0));
    Value dynamicByte = arith::ShRUIOp::create(
        builder, location, safeDynamicBit,
        llvmConstant(builder, location, i64, 3));
    Value byteOffset = arith::AddIOp::create(
        builder, location, dynamicByte,
        llvmConstant(builder, location, i64,
                     staticNBAPlan.generatedOffsets[entry.rootIndex] / 8));
    auto planeAddress = [&](Value plane) {
      return LLVM::GEPOp::create(builder, location, pointer,
                                 builder.getI8Type(), plane,
                                 ValueRange{byteOffset});
    };
    active = arith::AndIOp::create(builder, location, active, inBounds);
    Value bitInByte = arith::AndIOp::create(
        builder, location, safeDynamicBit,
        llvmConstant(builder, location, i64, uint64_t{7}));
    Block *dynamicJoin = nullptr;
    if ((entry.width & 7) == 0) {
      Block *aligned = new Block;
      Block *unaligned = new Block;
      dynamicJoin = new Block;
      auto beforeGeneric = Region::iterator(genericNBACommit);
      nbaCommit.getBody().getBlocks().insert(beforeGeneric, aligned);
      beforeGeneric = Region::iterator(genericNBACommit);
      nbaCommit.getBody().getBlocks().insert(beforeGeneric, unaligned);
      beforeGeneric = Region::iterator(genericNBACommit);
      nbaCommit.getBody().getBlocks().insert(beforeGeneric, dynamicJoin);
      Value byteAligned = arith::CmpIOp::create(
                              builder, location, arith::CmpIPredicate::eq,
                              bitInByte,
                              llvmConstant(builder, location, i64, 0))
                              .getResult();
      cf::CondBranchOp::create(builder, location, byteAligned, aligned,
                               ValueRange{}, unaligned, ValueRange{});

      builder.setInsertionPointToStart(aligned);
      IntegerType alignedType = IntegerType::get(context, entry.width);
      auto commitAlignedPlane = [&](Value plane, StringRef stagedName) {
        Value address = planeAddress(plane);
        Value old = LLVM::LoadOp::create(builder, location, alignedType,
                                         address, 1);
        Value staged64 = LLVM::LoadOp::create(
            builder, location, i64,
            LLVM::AddressOfOp::create(builder, location, pointer, stagedName),
            8);
        Value staged = resizeNativeInteger(builder, location, staged64,
                                           alignedType);
        LLVM::StoreOp::create(
            builder, location,
            arith::SelectOp::create(builder, location, active, staged, old),
            address, 1);
      };
      commitAlignedPlane(stateValue, entry.valueName);
      if (!forcedTwoStateEval)
        commitAlignedPlane(stateUnknown, entry.unknownName);
      cf::BranchOp::create(builder, location, dynamicJoin);
      builder.setInsertionPointToStart(unaligned);
    }
    // Load the exact byte window touched by an unaligned field. The state
    // planes carry an eight-byte guard, so a <=64-bit field can safely use its
    // width+7-bit window at the final root while the dynamic mask preserves
    // adjacent roots sharing those bytes.
    // LLVM stores an odd-width integer using its byte-rounded storage size.
    // The padding bits are not represented by the SSA value and therefore
    // cannot participate in the merge below.  Use an explicitly byte-sized
    // window so an unaligned dynamic field cannot clear the first bit of the
    // adjacent packed element (for example an i39 store for a 32-bit field
    // at bit offset 32 would otherwise overwrite bit 39).
    uint64_t windowWidth = llvm::alignTo(entry.width + 7, uint64_t{8});
    IntegerType windowType = IntegerType::get(context, windowWidth);
    Value windowShift = resizeNativeInteger(builder, location, bitInByte,
                                            windowType);
    uint64_t fieldMask = entry.width == 64
                             ? UINT64_MAX
                             : (uint64_t{1} << entry.width) - 1;
    Value unshiftedMask =
        llvmConstant(builder, location, windowType, fieldMask);
    Value shiftedMask = arith::ShLIOp::create(builder, location,
                                              unshiftedMask, windowShift);
    Value valueAddress = planeAddress(stateValue);
    Value oldValue = LLVM::LoadOp::create(builder, location, windowType,
                                          valueAddress, 1);
    Value staged64 = LLVM::LoadOp::create(
        builder, location, i64,
        LLVM::AddressOfOp::create(builder, location, pointer, entry.valueName),
        8);
    Value stagedValue = resizeNativeInteger(builder, location, staged64,
                                            windowType);
    stagedValue = arith::ShLIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, stagedValue, unshiftedMask),
        windowShift);
    Value mergedValue = arith::XOrIOp::create(
        builder, location, oldValue,
        arith::AndIOp::create(
            builder, location,
            arith::XOrIOp::create(builder, location, oldValue, stagedValue),
            shiftedMask));
    LLVM::StoreOp::create(builder, location,
                          arith::SelectOp::create(builder, location, active,
                                                  mergedValue, oldValue),
                          valueAddress, 1);
    if (!forcedTwoStateEval) {
      Value unknownAddress = planeAddress(stateUnknown);
      Value oldUnknown = LLVM::LoadOp::create(builder, location, windowType,
                                              unknownAddress, 1);
      Value stagedUnknown64 = LLVM::LoadOp::create(
          builder, location, i64,
          LLVM::AddressOfOp::create(builder, location, pointer,
                                    entry.unknownName),
          8);
      Value stagedUnknown = resizeNativeInteger(builder, location,
                                                stagedUnknown64, windowType);
      stagedUnknown = arith::ShLIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, stagedUnknown,
                                unshiftedMask),
          windowShift);
      Value mergedUnknown = arith::XOrIOp::create(
          builder, location, oldUnknown,
          arith::AndIOp::create(
              builder, location,
              arith::XOrIOp::create(builder, location, oldUnknown,
                                    stagedUnknown),
              shiftedMask));
      LLVM::StoreOp::create(
          builder, location,
          arith::SelectOp::create(builder, location, active, mergedUnknown,
                                  oldUnknown),
          unknownAddress, 1);
    }
    if (dynamicJoin) {
      cf::BranchOp::create(builder, location, dynamicJoin);
      builder.setInsertionPointToStart(dynamicJoin);
    }
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32, 0), validAddress,
                          4);
  }

  SmallVector<Block *> wordBlocks(nbaDirtyWordCount);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word)
    if (!scalarRootsByWord[word].empty()) {
      wordBlocks[word] = new Block;
      nbaCommit.getBody().getBlocks().insert(Region::iterator(genericNBACommit),
                                             wordBlocks[word]);
    }
  Block *firstWord = genericNBACommit;
  for (Block *block : wordBlocks)
    if (block) {
      firstWord = block;
      break;
    }
  cf::CondBranchOp::create(builder, location, directEnabled, firstWord,
                           ValueRange{}, genericNBACommit, ValueRange{});

  auto nextWordAfter = [&](uint32_t current) -> Block * {
    for (uint32_t word = current + 1; word != nbaDirtyWordCount; ++word)
      if (wordBlocks[word])
        return wordBlocks[word];
    return genericNBACommit;
  };
  auto scalarMask = [](uint64_t width) {
    return width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
  };
  auto loadRoot = [&](Value plane, uint64_t offset, uint64_t width) {
    uint64_t firstByte = offset / 8;
    uint64_t shift = offset % 8;
    Value low =
        LLVM::LoadOp::create(builder, location, i64,
                             byteGEP(builder, location, plane, firstByte), 1);
    Value value = low;
    if (shift != 0)
      value =
          arith::ShRUIOp::create(builder, location, value,
                                 llvmConstant(builder, location, i64, shift));
    if (width > 64 - shift) {
      Value high = LLVM::LoadOp::create(
          builder, location, builder.getI8Type(),
          byteGEP(builder, location, plane, firstByte + 8), 1);
      high = LLVM::ZExtOp::create(builder, location, i64, high);
      high = arith::ShLIOp::create(
          builder, location, high,
          llvmConstant(builder, location, i64, 64 - shift));
      value = arith::OrIOp::create(builder, location, value, high);
    }
    return arith::AndIOp::create(
               builder, location, value,
               llvmConstant(builder, location, i64, scalarMask(width)))
        .getResult();
  };
  auto storeRoot = [&](Value plane, uint64_t offset, uint64_t width,
                       Value value) {
    uint64_t firstByte = offset / 8;
    uint64_t shift = offset % 8;
    Value address = byteGEP(builder, location, plane, firstByte);
    Value oldLow = LLVM::LoadOp::create(builder, location, i64, address, 1);
    uint64_t lowMask = scalarMask(width) << shift;
    Value cleared =
        arith::AndIOp::create(builder, location, oldLow,
                              llvmConstant(builder, location, i64, ~lowMask));
    Value positioned = value;
    if (shift != 0)
      positioned =
          arith::ShLIOp::create(builder, location, positioned,
                                llvmConstant(builder, location, i64, shift));
    positioned =
        arith::AndIOp::create(builder, location, positioned,
                              llvmConstant(builder, location, i64, lowMask));
    LLVM::StoreOp::create(
        builder, location,
        arith::OrIOp::create(builder, location, cleared, positioned), address,
        1);
    if (width <= 64 - shift)
      return;
    uint64_t highWidth = width - (64 - shift);
    uint8_t highMask = static_cast<uint8_t>((uint16_t{1} << highWidth) - 1);
    Value highAddress = byteGEP(builder, location, plane, firstByte + 8);
    Value oldHigh = LLVM::LoadOp::create(builder, location,
                                         builder.getI8Type(), highAddress, 1);
    Value highValue = arith::ShRUIOp::create(
        builder, location, value,
        llvmConstant(builder, location, i64, 64 - shift));
    highValue = LLVM::TruncOp::create(builder, location, builder.getI8Type(),
                                      highValue);
    Value newHigh = arith::OrIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, oldHigh,
                              llvmConstant(builder, location,
                                           builder.getI8Type(),
                                           static_cast<uint8_t>(~highMask))),
        arith::AndIOp::create(
            builder, location, highValue,
            llvmConstant(builder, location, builder.getI8Type(), highMask)));
    LLVM::StoreOp::create(builder, location, newHigh, highAddress, 1);
  };

  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
    if (!wordBlocks[word])
      continue;
    builder.setInsertionPointToStart(wordBlocks[word]);
    Value dirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                nbaDirtyRootsName);
    Value dirty =
        LLVM::LoadOp::create(builder, location, i64,
                             byteGEP(builder, location, dirtyBase,
                                     uint64_t{word} * sizeof(uint64_t)),
                             8);
    if (evalScheduler)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, dirtyBase,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
    Value wordEmpty =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              dirty, llvmConstant(builder, location, i64, 0));
    Block *next = nextWordAfter(word);
    Block *firstRoot = new Block;
    nbaCommit.getBody().getBlocks().insert(Region::iterator(next), firstRoot);
    cf::CondBranchOp::create(builder, location, wordEmpty, next, ValueRange{},
                             firstRoot, ValueRange{});

    Block *rootBlock = firstRoot;

    for (auto [position, rootIndex] :
         llvm::enumerate(scalarRootsByWord[word])) {
      const obelisk_rt_static_nba_root &root = nbaRoots[rootIndex];
      uint64_t offset = staticNBAPlan.generatedOffsets[rootIndex];
      uint32_t fixedCommitRegion =
          staticNBAPlan.generatedCommitRegions[rootIndex];
      bool fixedRegionStage = fixedCommitRegion != UINT32_MAX;
      bool fullRootStage =
          rootIndex < staticNBAPlan.generatedFullRootStages.size() &&
          staticNBAPlan.generatedFullRootStages[rootIndex];
      uint64_t fixedWriteMask =
          rootIndex < staticNBAPlan.generatedFixedWriteMasks.size()
              ? staticNBAPlan.generatedFixedWriteMasks[rootIndex]
              : 0;
      StringRef accumulator = staticNBAPlan.generatedAccumulators[rootIndex];
      Block *afterRoot =
          position + 1 == scalarRootsByWord[word].size() ? next : new Block;
      if (afterRoot != next)
        nbaCommit.getBody().getBlocks().insert(Region::iterator(next),
                                               afterRoot);
      Block *commitRoot = new Block;
      nbaCommit.getBody().getBlocks().insert(Region::iterator(afterRoot),
                                             commitRoot);
      builder.setInsertionPointToStart(rootBlock);
      // Extract the one-bit predicate directly. On x86 this gives instruction
      // selection a `bt`/shift-immediate shape for upper-half roots instead of
      // materializing a 64-bit mask in a register at every barrier.
      Value selected = arith::TruncIOp::create(
          builder, location, builder.getI1Type(),
          arith::ShRUIOp::create(
              builder, location, dirty,
              llvmConstant(builder, location, i64, rootIndex % 64)));
      Value accumulatorBase =
          LLVM::AddressOfOp::create(builder, location, pointer, accumulator);
      Value regionMatches;
      if (fixedRegionStage) {
        regionMatches = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq,
            nbaCommitEntry->getArgument(2),
            llvmConstant(builder, location, i32, fixedCommitRegion));
      } else {
        Value valid = LLVM::LoadOp::create(
            builder, location, i32,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
        Value region = LLVM::LoadOp::create(
            builder, location, i32,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256,
                             exec_region)),
            4);
        regionMatches = arith::AndIOp::create(
            builder, location,
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  valid,
                                  llvmConstant(builder, location, i32, 0)),
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  region, nbaCommitEntry->getArgument(2)));
      }
      Value validRoot =
          arith::AndIOp::create(builder, location, selected, regionMatches);
      cf::CondBranchOp::create(builder, location, validRoot, commitRoot,
                               ValueRange{}, afterRoot, ValueRange{});

      builder.setInsertionPointToStart(commitRoot);
      Value stagedValue = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(builder, location, accumulatorBase,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, value)),
          8);
      Value stagedUnknown;
      if (forcedTwoStateEval) {
        stagedUnknown = llvmConstant(builder, location, i64, 0);
      } else {
        auto load = LLVM::LoadOp::create(
            builder, location, i64,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256,
                             unknown)),
            8);
        // The later two-state commit clone must zero both canonical unknown
        // loads and staged accumulator unknown data left by an earlier
        // four-state slot. Mark this semantic role explicitly instead of
        // reverse-engineering a byte GEP after LLVM lowering.
        load->setAttr("obelisk.eval.two_state_zero_unknown",
                      UnitAttr::get(context));
        stagedUnknown = load;
      }
      Value writeMask =
          fixedWriteMask != 0
              ? llvmConstant(builder, location, i64, fixedWriteMask)
              : arith::AndIOp::create(
                    builder, location,
                    LLVM::LoadOp::create(
                        builder, location, i64,
                        byteGEP(
                            builder, location, accumulatorBase,
                            offsetof(obelisk_rt_generated_nba_accumulator_256,
                                     write_mask)),
                        8),
                    llvmConstant(builder, location, i64,
                                 scalarMask(root.bit_width)))
                    .getResult();
      Value oldValue = loadRoot(stateValue, offset, root.bit_width);
      Value oldUnknown = forcedTwoStateEval
                             ? llvmConstant(builder, location, i64, 0)
                             : loadRoot(stateUnknown, offset, root.bit_width);
      Value inverseMask = arith::XOrIOp::create(
          builder, location, writeMask,
          llvmConstant(builder, location, i64, UINT64_MAX));
      Value newValue = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, oldValue, inverseMask),
          arith::AndIOp::create(builder, location, stagedValue, writeMask));
      Value newUnknown =
          forcedTwoStateEval
              ? llvmConstant(builder, location, i64, 0)
              : arith::OrIOp::create(
                    builder, location,
                    arith::AndIOp::create(builder, location, oldUnknown,
                                          inverseMask),
                    arith::AndIOp::create(builder, location, stagedUnknown,
                                          writeMask))
                    .getResult();
      storeRoot(stateValue, offset, root.bit_width, newValue);
      if (!forcedTwoStateEval)
        storeRoot(stateUnknown, offset, root.bit_width, newUnknown);
      if (!evalScheduler || (fixedWriteMask == 0 && !fullRootStage)) {
        LLVM::StoreOp::create(
            builder, location, llvmConstant(builder, location, i64, 0),
            byteGEP(
                builder, location, accumulatorBase,
                offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask)),
            8);
        LLVM::StoreOp::create(
            builder, location, llvmConstant(builder, location, i32, 0),
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
      }
      if (!evalScheduler) {
        Value directlyCommitted = LLVM::LoadOp::create(
            builder, location, i64, directlyCommittedByWord[word], 8);
        LLVM::StoreOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, directlyCommitted,
                                 llvmConstant(builder, location, i64,
                                              uint64_t{1} << (rootIndex % 64))),
            directlyCommittedByWord[word], 8);
      }
      Value changed = arith::OrIOp::create(
          builder, location,
          arith::XOrIOp::create(builder, location, oldValue, newValue),
          arith::XOrIOp::create(builder, location, oldUnknown, newUnknown));
      Value rootChanged = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, changed,
          llvmConstant(builder, location, i64, 0));
      if (!evalScheduler) {
        Value priorChanged = LLVM::LoadOp::create(
            builder, location, i32, nbaCommitEntry->getArgument(3), 4);
        Value changedI32 =
            LLVM::ZExtOp::create(builder, location, i32, rootChanged);
        LLVM::StoreOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, priorChanged, changedI32),
            nbaCommitEntry->getArgument(3), 4);
        Value count =
            LLVM::LoadOp::create(builder, location, i32, committedCount, 4);
        LLVM::StoreOp::create(
            builder, location,
            arith::AddIOp::create(
                builder, location, count,
                llvmConstant(builder, location, i32, uint32_t{1})),
            committedCount, 4);
      }
      struct TriggerGroup {
        uint32_t edge;
        uint64_t mask;
        SmallVector<uint64_t> nodes;
        SmallVector<uint64_t> direct;
      };
      SmallVector<TriggerGroup> groups;
      for (const obelisk_rt_static_fanout_entry &entry : fanoutEntries) {
        if (entry.static_state != root.static_state ||
            entry.low_bit >= root.bit_width)
          continue;
        uint64_t high =
            std::min<uint64_t>(root.bit_width, entry.low_bit + entry.bit_width);
        if (entry.low_bit >= high)
          continue;
        uint64_t mask = scalarMask(high - entry.low_bit) << entry.low_bit;
        auto group = llvm::find_if(groups, [&](const TriggerGroup &candidate) {
          return candidate.edge == entry.edge && candidate.mask == mask;
        });
        if (group == groups.end()) {
          groups.push_back(
              {entry.edge, mask, SmallVector<uint64_t>(activationWordCount, 0),
               SmallVector<uint64_t>(directActivationWordCount, 0)});
          group = std::prev(groups.end());
        }
        if (evalScheduler && entry.reserved == 1 &&
            entry.merged_bit < directActivationWordCount * 64 &&
            directActivationWordCount != 0)
          group->direct[entry.merged_bit / 64] |= uint64_t{1}
                                                  << (entry.merged_bit % 64);
        else
          group->nodes[entry.compute_node / 64] |= uint64_t{1}
                                                   << (entry.compute_node % 64);
      }
      if (!groups.empty()) {
        Value widthMask =
            llvmConstant(builder, location, i64, scalarMask(root.bit_width));
        auto invert = [&](Value value) {
          return arith::XOrIOp::create(
                     builder, location, value,
                     llvmConstant(builder, location, i64, UINT64_MAX))
              .getResult();
        };
        Value oldKnown = arith::AndIOp::create(builder, location,
                                               invert(oldUnknown), widthMask);
        Value newKnown = arith::AndIOp::create(builder, location,
                                               invert(newUnknown), widthMask);
        Value oldZero = arith::AndIOp::create(builder, location, oldKnown,
                                              invert(oldValue));
        Value oldOne =
            arith::AndIOp::create(builder, location, oldKnown, oldValue);
        Value newZero = arith::AndIOp::create(builder, location, newKnown,
                                              invert(newValue));
        Value newOne =
            arith::AndIOp::create(builder, location, newKnown, newValue);
        Value posedge = arith::AndIOp::create(
            builder, location,
            arith::OrIOp::create(
                builder, location,
                arith::AndIOp::create(builder, location, oldZero,
                                      invert(newZero)),
                arith::AndIOp::create(builder, location, oldUnknown, newOne)),
            widthMask);
        Value negedge = arith::AndIOp::create(
            builder, location,
            arith::OrIOp::create(
                builder, location,
                arith::AndIOp::create(builder, location, oldOne,
                                      invert(newOne)),
                arith::AndIOp::create(builder, location, oldUnknown, newZero)),
            widthMask);
        for (const TriggerGroup &group : groups) {
          Value observed = changed;
          switch (group.edge) {
          case OBELISK_RT_WAIT_EDGE_POSEDGE:
            observed = posedge;
            break;
          case OBELISK_RT_WAIT_EDGE_NEGEDGE:
            observed = negedge;
            break;
          case OBELISK_RT_WAIT_EDGE_BOTH:
            observed =
                arith::OrIOp::create(builder, location, posedge, negedge);
            break;
          default:
            break;
          }
          Value triggered = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne,
              arith::AndIOp::create(
                  builder, location, observed,
                  llvmConstant(builder, location, i64, group.mask)),
              llvmConstant(builder, location, i64, 0));
          for (auto [activationWord, nodeMask] : llvm::enumerate(group.nodes)) {
            if (nodeMask == 0)
              continue;
            Value address =
                byteGEP(builder, location, activatedNodes,
                        uint64_t{activationWord} * sizeof(uint64_t));
            Value active =
                LLVM::LoadOp::create(builder, location, i64, address, 8);
            Value selected = arith::SelectOp::create(
                builder, location, triggered,
                llvmConstant(builder, location, i64, nodeMask),
                llvmConstant(builder, location, i64, 0));
            LLVM::StoreOp::create(
                builder, location,
                arith::OrIOp::create(builder, location, active, selected),
                address, 8);
          }
          for (auto [activationWord, directMask] :
               llvm::enumerate(group.direct)) {
            if (directMask == 0)
              continue;
            Value address =
                byteGEP(builder, location, activatedDirect,
                        uint64_t{activationWord} * sizeof(uint64_t));
            Value active =
                LLVM::LoadOp::create(builder, location, i64, address, 8);
            Value selected = arith::SelectOp::create(
                builder, location, triggered,
                llvmConstant(builder, location, i64, directMask),
                llvmConstant(builder, location, i64, 0));
            LLVM::StoreOp::create(
                builder, location,
                arith::OrIOp::create(builder, location, active, selected),
                address, 8);
          }
        }
      }
      cf::BranchOp::create(builder, location, afterRoot);
      rootBlock = afterRoot;
    }
  }

  builder.setInsertionPointToStart(genericNBACommit);
  if (generateGroupedFanout && !evalScheduler)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context,
                           "obelisk_rt_v1_scheduler_activate_static_nodes"),
        ValueRange{nbaCommitEntry->getArgument(1), activatedNodes,
                   llvmConstant(builder, location, i32, activationWordCount)});
  if (directActivationWordCount != 0 && !clockKernels.empty()) {
    Value ingress = LLVM::AddressOfOp::create(builder, location, pointer,
                                              clockKernels.front().ingressName);
    Value any = llvmConstant(builder, location, builder.getI1Type(), 0);
    for (uint32_t word = 0; word != directActivationWordCount; ++word) {
      Value activated =
          LLVM::LoadOp::create(builder, location, i64,
                               byteGEP(builder, location, activatedDirect,
                                       uint64_t{word} * sizeof(uint64_t)),
                               8);
      Value selected = activated;
      Value address = byteGEP(builder, location, ingress,
                              uint64_t{word} * sizeof(uint64_t));
      Value previous = LLVM::LoadOp::create(builder, location, i64, address, 8);
      LLVM::StoreOp::create(
          builder, location,
          arith::OrIOp::create(builder, location, previous, selected), address,
          8);
      any = arith::OrIOp::create(
          builder, location, any,
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                selected,
                                llvmConstant(builder, location, i64, 0)));
    }
    (void)any;
  }
  Value dirtyBase;
  if (nbaDirtyWordCount != 0)
    dirtyBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                          nbaDirtyRootsName);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
    if (evalScheduler || !directlyCommittedByWord[word])
      continue;
    Value dirtyAddress = byteGEP(builder, location, dirtyBase,
                                 uint64_t{word} * sizeof(uint64_t));
    Value dirty = LLVM::LoadOp::create(builder, location, i64, dirtyAddress, 8);
    Value committed = LLVM::LoadOp::create(builder, location, i64,
                                           directlyCommittedByWord[word], 8);
    LLVM::StoreOp::create(
        builder, location,
        arith::AndIOp::create(
            builder, location, dirty,
            arith::XOrIOp::create(
                builder, location, committed,
                llvmConstant(builder, location, i64, UINT64_MAX))),
        dirtyAddress, 8);
  }
  Value directCount =
      LLVM::LoadOp::create(builder, location, i32, committedCount, 4);
  if (!evalScheduler)
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(
            context, "obelisk_rt_v1_static_nba_account_generated_commits"),
        ValueRange{nbaCommitEntry->getArgument(1), directCount});

  // The generated scalar path normally consumes the complete dirty leaf for
  // this barrier.  Do not enter the generic ordered-root walker merely so it
  // can observe an empty bitmap and clear the summary level.  Roots belonging
  // to a later region, claimed slow roots, and unsupported wide roots remain
  // set above and still take the canonical path.
  Value remainingDirty = llvmConstant(builder, location, i64, 0);
  for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
    Value dirtyAddress = byteGEP(builder, location, dirtyBase,
                                 uint64_t{word} * sizeof(uint64_t));
    Value dirty = LLVM::LoadOp::create(builder, location, i64, dirtyAddress, 8);
    remainingDirty =
        arith::OrIOp::create(builder, location, remainingDirty, dirty);
  }
  Value noRemainingDirty = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, remainingDirty,
      llvmConstant(builder, location, i64, 0));
  Value allDirect =
      arith::AndIOp::create(builder, location, directEnabled, noRemainingDirty);
  Block *directDone = new Block;
  nbaCommit.getBody().push_back(directDone);
  Block *callGeneric = nullptr;
  if (evalScheduler) {
    cf::BranchOp::create(builder, location, directDone);
  } else {
    callGeneric = new Block;
    nbaCommit.getBody().push_back(callGeneric);
    cf::CondBranchOp::create(builder, location, allDirect, directDone,
                             ValueRange{}, callGeneric, ValueRange{});
  }

  builder.setInsertionPointToStart(directDone);
  if (!evalScheduler && nbaDirtySummaryWordCount != 0) {
    Value summaryBase = LLVM::AddressOfOp::create(builder, location, pointer,
                                                  nbaDirtySummaryName);
    for (uint32_t word = 0; word != nbaDirtySummaryWordCount; ++word)
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0),
                            byteGEP(builder, location, summaryBase,
                                    uint64_t{word} * sizeof(uint64_t)),
                            8);
  }
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, OBELISK_RT_OK));

  if (callGeneric) {
    builder.setInsertionPointToStart(callGeneric);
    Value nbaCommitStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_static_nba_commit_roots"),
            ValueRange{nbaCommitEntry->getArgument(1),
                       llvmConstant(builder, location, i32, nbaRoots.size()),
                       nbaCommitEntry->getArgument(2),
                       nbaCommitEntry->getArgument(3)})
            .getResult();
    LLVM::ReturnOp::create(builder, location, nbaCommitStatus);
  }

  if (evalScheduler) {
    // A four-state owner does not necessarily stage an unknown NBA value.  A
    // common example is a checkpoint-capable monitor incrementing a known
    // cycle counter.  Before selecting the expensive four-state barrier, scan
    // only dirty fixed roots and prove that the overwritten canonical and
    // staged unknown bits are zero.  This is a quiescent handoff test, not a
    // persistent shadow state: the canonical planes remain authoritative.
    builder.setInsertionPointToEnd(module.getBody());
    auto nbaKnown = LLVM::LLVMFuncOp::create(
        builder, location, nbaKnownName,
        LLVM::LLVMFunctionType::get(
            builder.getI1Type(),
            {i32, builder.getI1Type(), builder.getI1Type()}, false));
    nbaKnown.setLinkage(LLVM::Linkage::Internal);
    Block *knownEntry = nbaKnown.addEntryBlock(builder);
    Block *knownTrue = new Block;
    Block *knownFalse = new Block;
    nbaKnown.getBody().push_back(knownTrue);
    nbaKnown.getBody().push_back(knownFalse);
    builder.setInsertionPointToStart(knownTrue);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI1Type(), 1));
    builder.setInsertionPointToStart(knownFalse);
    LLVM::ReturnOp::create(
        builder, location,
        llvmConstant(builder, location, builder.getI1Type(), 0));

    builder.setInsertionPointToStart(knownEntry);
    Block *wordCursor = knownEntry;
    if (!dynamicEvalNBAs.empty()) {
      Value dynamicActive =
          llvmConstant(builder, location, builder.getI1Type(), 0);
      for (const DynamicEvalNBA &entry : dynamicEvalNBAs) {
        Value valid = LLVM::LoadOp::create(
            builder, location, i32,
            LLVM::AddressOfOp::create(builder, location, pointer,
                                      entry.validName),
            4);
        dynamicActive = arith::OrIOp::create(
            builder, location, dynamicActive,
            arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::ne, valid,
                llvmConstant(builder, location, i32, 0)));
      }
      wordCursor = new Block;
      nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                            wordCursor);
      cf::CondBranchOp::create(builder, location, dynamicActive, knownFalse,
                               ValueRange{}, wordCursor, ValueRange{});
    }

    for (uint32_t word = 0; word != nbaDirtyWordCount; ++word) {
      if (scalarRootsByWord[word].empty())
        continue;
      builder.setInsertionPointToStart(wordCursor);
      Value knownDirtyBase = LLVM::AddressOfOp::create(
          builder, location, pointer, nbaDirtyRootsName);
      Value dirty = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(builder, location, knownDirtyBase,
                  uint64_t{word} * sizeof(uint64_t)),
          8);
      Value taintBase = LLVM::AddressOfOp::create(
          builder, location, pointer, evalStepFourStateNBARootsName);
      Value taintedRoots = LLVM::LoadOp::create(
          builder, location, i64,
          byteGEP(builder, location, taintBase,
                  uint64_t{word} * sizeof(uint64_t)),
          8);
      taintedRoots = arith::SelectOp::create(
          builder, location, knownEntry->getArgument(1),
          llvmConstant(builder, location, i64, UINT64_MAX), taintedRoots);
      dirty = arith::AndIOp::create(builder, location, dirty, taintedRoots);
      uint64_t supportedMask = 0;
      for (uint32_t rootIndex : scalarRootsByWord[word])
        supportedMask |= uint64_t{1} << (rootIndex % 64);
      Value unsupported = arith::AndIOp::create(
          builder, location, dirty,
          llvmConstant(builder, location, i64, ~supportedMask));
      Value hasUnsupported = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, unsupported,
          llvmConstant(builder, location, i64, 0));
      Block *rootCursor = new Block;
      nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                            rootCursor);
      cf::CondBranchOp::create(builder, location, hasUnsupported, knownFalse,
                               ValueRange{}, rootCursor, ValueRange{});

      for (uint32_t rootIndex : scalarRootsByWord[word]) {
        const obelisk_rt_static_nba_root &root = nbaRoots[rootIndex];
        StringRef accumulator = staticNBAPlan.generatedAccumulators[rootIndex];
        uint32_t fixedCommitRegion =
            staticNBAPlan.generatedCommitRegions[rootIndex];
        uint64_t fixedWriteMask =
            rootIndex < staticNBAPlan.generatedFixedWriteMasks.size()
                ? staticNBAPlan.generatedFixedWriteMasks[rootIndex]
                : 0;
        builder.setInsertionPointToStart(rootCursor);
        Value selected = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne,
            arith::AndIOp::create(
                builder, location, dirty,
                llvmConstant(builder, location, i64,
                             uint64_t{1} << (rootIndex % 64))),
            llvmConstant(builder, location, i64, 0));
        Value accumulatorBase = LLVM::AddressOfOp::create(
            builder, location, pointer, accumulator);
        Value regionMatches;
        if (fixedCommitRegion != UINT32_MAX) {
          regionMatches = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::eq,
              knownEntry->getArgument(0),
              llvmConstant(builder, location, i32, fixedCommitRegion));
        } else {
          Value valid = LLVM::LoadOp::create(
              builder, location, i32,
              byteGEP(builder, location, accumulatorBase,
                      offsetof(obelisk_rt_generated_nba_accumulator_256,
                               valid)),
              4);
          Value region = LLVM::LoadOp::create(
              builder, location, i32,
              byteGEP(builder, location, accumulatorBase,
                      offsetof(obelisk_rt_generated_nba_accumulator_256,
                               exec_region)),
              4);
          regionMatches = arith::AndIOp::create(
              builder, location,
              arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::ne, valid,
                  llvmConstant(builder, location, i32, 0)),
              arith::CmpIOp::create(builder, location,
                                    arith::CmpIPredicate::eq, region,
                                    knownEntry->getArgument(0)));
        }
        Value inspectRoot = arith::AndIOp::create(builder, location, selected,
                                                   regionMatches);
        Block *inspect = new Block;
        Block *nextRoot = new Block;
        nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                              inspect);
        nbaKnown.getBody().getBlocks().insert(Region::iterator(knownTrue),
                                              nextRoot);
        cf::CondBranchOp::create(builder, location, inspectRoot, inspect,
                                 ValueRange{}, nextRoot, ValueRange{});

        builder.setInsertionPointToStart(inspect);
        Value writeMask =
            fixedWriteMask != 0
                ? llvmConstant(builder, location, i64, fixedWriteMask)
                : arith::AndIOp::create(
                      builder, location,
                      LLVM::LoadOp::create(
                          builder, location, i64,
                          byteGEP(
                              builder, location, accumulatorBase,
                              offsetof(obelisk_rt_generated_nba_accumulator_256,
                                       write_mask)),
                          8),
                      llvmConstant(builder, location, i64,
                                   scalarMask(root.bit_width)))
                      .getResult();
        Value stagedUnknown = LLVM::LoadOp::create(
            builder, location, i64,
            byteGEP(builder, location, accumulatorBase,
                    offsetof(obelisk_rt_generated_nba_accumulator_256,
                             unknown)),
            8);
        stagedUnknown = arith::SelectOp::create(
            builder, location, knownEntry->getArgument(2), stagedUnknown,
            llvmConstant(builder, location, i64, 0));
        Value oldUnknown = loadRoot(
            LLVM::AddressOfOp::create(builder, location, pointer,
                                      "__obelisk_state_unknown"),
            staticNBAPlan.generatedOffsets[rootIndex], root.bit_width);
        Value relevantUnknown = arith::AndIOp::create(
            builder, location,
            arith::OrIOp::create(builder, location, stagedUnknown, oldUnknown),
            writeMask);
        Value hasUnknown = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, relevantUnknown,
            llvmConstant(builder, location, i64, 0));
        cf::CondBranchOp::create(builder, location, hasUnknown, knownFalse,
                                 ValueRange{}, nextRoot, ValueRange{});
        rootCursor = nextRoot;
      }
      wordCursor = rootCursor;
    }
    builder.setInsertionPointToStart(wordCursor);
    cf::BranchOp::create(builder, location, knownTrue);
  }

  auto planType = LLVM::LLVMStructType::getLiteral(
      context, {i32,     i64,     pointer, i64,     i32,     i32,     pointer,
                pointer, i64,     pointer, pointer, pointer, pointer, i32,
                i32,     pointer, i64,     pointer, i64,     pointer, i64,
                pointer, pointer, pointer, i32,     i32,     pointer, i32,
                i32,     pointer, i32,     i32,     pointer, i64,     pointer,
                pointer, pointer});
  makeConstantGlobal(
      module, location, planType, planName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &initializerBuilder) {
        Value value =
            LLVM::ZeroOp::create(initializerBuilder, location, planType);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32,
                                     sizeof(obelisk_rt_native_schedule_plan)),
                        0);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         graphLayoutChecksum),
                            1);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, stateName),
                        2);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         uint64_t{actorCount} * sizeof(void *)),
                            3);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, actorCount), 4);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(
                initializerBuilder, location, i32,
                (fullyStatic ? OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC : 0) |
                    (rootSlotZero ? OBELISK_RT_NATIVE_SCHEDULE_ROOT_SLOT_ZERO
                                  : 0) |
                    (staticControlEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_CONTROL
                         : 0) |
                    (staticFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_FANOUT
                         : 0) |
                    (enableDirectState ? OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE
                                       : 0) |
                    (enableStaticNBA ? OBELISK_RT_NATIVE_SCHEDULE_STATIC_NBA
                                     : 0) |
                    (fullyStatic ? OBELISK_RT_NATIVE_SCHEDULE_GENERATED_ACTIONS
                                 : 0) |
                    (guardedFanoutEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_FANOUT
                         : 0) |
                    (guardedSpecializationEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_GUARDED_SPECIALIZATION
                         : 0) |
                    (cleanSuperstepEnabled
                         ? OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP
                         : 0) |
                    (evalScheduler ? OBELISK_RT_NATIVE_SCHEDULE_EVAL : 0)),
            5);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(initializerBuilder,
                                                      location, pointer,
                                                      "__obelisk_state_value"),
                            6);
        value = insertValue(
            initializerBuilder, location, value,
            LLVM::AddressOfOp::create(initializerBuilder, location, pointer,
                                      "__obelisk_state_unknown"),
            7);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         stateLayout.bitCount),
                            8);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, bindName),
                        9);
        value = insertValue(initializerBuilder, location, value,
                            LLVM::AddressOfOp::create(
                                initializerBuilder, location, pointer, runName),
                            10);
        value =
            insertValue(initializerBuilder, location, value,
                        LLVM::AddressOfOp::create(initializerBuilder, location,
                                                  pointer, snapshotName),
                        11);
        Value rootsAddress =
            nbaRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaRootsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, rootsAddress, 12);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, nbaRoots.size()),
            13);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 14);
        Value sitesAddress =
            nbaSites.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaSitesName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, sitesAddress, 15);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, nbaSites.size()),
            16);
        Value fanoutAddress =
            fanoutEntries.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, fanoutName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, fanoutAddress, 17);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         fanoutEntries.size()),
                            18);
        Value actorRootsAddress =
            actorRoots.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, actorRootsName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value,
                            actorRootsAddress, 19);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i64, actorRoots.size()),
            20);
        Value commitAddress =
            enableStaticNBA
                ? LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaCommitName)
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, commitAddress, 21);
        Value specializationFast =
            guardedSpecializationEnabled
                ? LLVM::AddressOfOp::create(
                      initializerBuilder, location, pointer,
                      "__obelisk_static_specialization_fast_v1")
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        value = insertValue(initializerBuilder, location, value,
                            specializationFast, 22);
        Value dirtyRoots =
            nbaDirtyWordCount == 0
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaDirtyRootsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, dirtyRoots, 23);
        value = insertValue(
            initializerBuilder, location, value,
            llvmConstant(initializerBuilder, location, i32, nbaDirtyWordCount),
            24);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 25);
        Value dirtySummary =
            nbaDirtySummaryWordCount == 0
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, nbaDirtySummaryName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, dirtySummary, 26);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32,
                                         nbaDirtySummaryWordCount),
                            27);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 28);
        Value clocksAddress =
            clockKernels.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, clockKernelsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, clocksAddress, 29);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i32,
                                         clockKernels.size()),
                            30);
        value =
            insertValue(initializerBuilder, location, value,
                        llvmConstant(initializerBuilder, location, i32, 0), 31);
        Value mergedAddress =
            mergedFragments.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, mergedFragmentsName)
                      .getResult();
        value =
            insertValue(initializerBuilder, location, value, mergedAddress, 32);
        value = insertValue(initializerBuilder, location, value,
                            llvmConstant(initializerBuilder, location, i64,
                                         mergedFragments.size()),
                            33);
        Value coordinatorAddress =
            clockKernels.empty()
                ? LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult()
                : LLVM::AddressOfOp::create(
                      initializerBuilder, location, pointer,
                      evalScheduler ? evalCoordinatorName : coordinatorName)
                      .getResult();
        value = insertValue(initializerBuilder, location, value,
                            coordinatorAddress, 34);
        Value promotionInvalidateAddress =
            evalScheduler
                ? LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, promotionInvalidateName)
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        value = insertValue(initializerBuilder, location, value,
                            promotionInvalidateAddress, 35);
        Value promotionQueryAddress =
            evalScheduler
                ? LLVM::AddressOfOp::create(initializerBuilder, location,
                                            pointer, promotionQueryName)
                      .getResult()
                : LLVM::ZeroOp::create(initializerBuilder, location, pointer)
                      .getResult();
        return insertValue(initializerBuilder, location, value,
                           promotionQueryAddress, 36);
      });
  if (!evalScheduler) {
    if (fullyStatic)
      getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run_aot_nodes",
                               i32, {pointer, pointer, i32});
    else
      getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_run", i32,
                               {pointer});
  }
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_snapshot_aot", i32,
                           {pointer, pointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_prepare_periodic_aot", i32,
      {pointer, pointer, i32, pointer, i32, pointer, i32, pointer, pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_handoff_periodic_aot", i32,
                           {pointer, pointer, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_root", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_static_nba_commit_roots", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_direct_commit_guard", i32, {pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_static_nba_account_generated_commits",
                           LLVM::LLVMVoidType::get(context), {pointer, i32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_activate_static_nodes",
      LLVM::LLVMVoidType::get(context), {pointer, pointer, i32});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_direct_fragment_enter", i32,
                           {pointer, i32, i32, pointer});
  getOrDeclareLLVMFunction(module,
                           "obelisk_rt_v1_scheduler_direct_fragment_leave", i32,
                           {pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_scheduler_execute_aot_actor",
                           i32, {pointer, i32});
  return success();
}

} // namespace obelisk::detail
