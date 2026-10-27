//===- DesignBytecodeNets.cpp - Bytecode net resolution -----------------===//

#include "DesignBytecodeNets.h"
#include "DesignBytecodeLogic.h"
#include "ProcessPacking.h"
#include "ProcessShared.h"
#include "RuntimeInternal.h"

#include "talos/Runtime/StrengthResolution.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <map>
#include <new>
#include <unordered_map>
#include <unordered_set>

namespace talos::designbytecode {

using talos::runtime::combineStrengthRanges;
using talos::runtime::implicitNetStrength;
using talos::runtime::strengthBit;
using talos::runtime::strengthCount;

// IEEE 1800-2017 28.13 and 28.14: a nonresistive bidirectional pass switch
// limits supply to strong, while a resistive switch applies Table 28-8. Apply
// the transfer pointwise because an ambiguous value is represented as a range
// on the signed strength scale.
static uint16_t propagateThroughPass(uint16_t strengths,
                                     uint8_t resistiveEdges) {
  // Table 28-8. Four resistive crossings are enough to reach the fixed small
  // strength for every non-high-impedance input.
  static constexpr std::array<int, 8> resistiveReduction = {
      0, // high impedance
      1, // small capacitor -> small capacitor
      1, // medium capacitor -> small capacitor
      2, // weak drive -> medium capacitor
      2, // large capacitor -> medium capacitor
      3, // pull drive -> weak drive
      5, // strong drive -> pull drive
      5, // supply drive -> pull drive
  };
  uint16_t propagated = 0;
  for (int strength = -7; strength <= 7; ++strength) {
    if ((strengths & strengthBit(strength)) == 0)
      continue;
    int sign = strength < 0 ? -1 : 1;
    int magnitude = std::abs(strength);
    if (resistiveEdges == 0) {
      magnitude = std::min(magnitude, 6);
    } else {
      for (uint8_t edge = 0; edge != resistiveEdges; ++edge)
        magnitude = resistiveReduction[magnitude];
    }
    propagated |= strengthBit(sign * magnitude);
  }
  return propagated;
}

// A source reached through a pass component contributes the strengths that
// survive its definite path, joined with those of a possible path whose
// switch control is unknown. Reduction 5 marks a path that does not conduct.
static uint16_t propagateFromRemote(uint16_t strengths, uint8_t resistiveEdges,
                                    uint8_t possibleResistiveEdges) {
  uint16_t possible = propagateThroughPass(strengths, possibleResistiveEdges);
  if (resistiveEdges == 5)
    return possible | strengthBit(0);
  strengths = propagateThroughPass(strengths, resistiveEdges);
  if (possibleResistiveEdges < resistiveEdges)
    strengths |= possible;
  return strengths;
}

static uint8_t decodeDriverStrength(uint32_t flags, unsigned shift) {
  uint32_t encoded = (flags >> shift) & 0xf;
  // Strength metadata predates executable strength resolution. Preserve the
  // old encoding as the LRM default strong drive.
  return encoded == 0 ? 6 : static_cast<uint8_t>(encoded - 1);
}

static uint8_t decodeChargeStrength(uint32_t flags) {
  switch ((flags >> 7) & 3) {
  case 0: // Legacy/default trireg descriptors use medium charge.
  case 2:
    return 2;
  case 1:
    return 1;
  case 3:
    return 4;
  }
  return 2;
}

bool appendSignalEvent(talos_rt_context *context, uint64_t bitOffset,
                       bool oldValue, bool oldUnknown, bool newValue,
                       bool newUnknown, bool evaluateComputedObservers = true) {
  return talos_rt_append_signal_event_unlocked(
      context, bitOffset, oldValue, oldUnknown, newValue, newUnknown,
      evaluateComputedObservers);
}

static bool rebuildPassComponent(NetPassComponent &component) {
  size_t count = component.roots.size();
  if (count != 0 && count > SIZE_MAX / count)
    return false;
  size_t matrixSize = count * count;
  if (component.definiteNonresistive.size() != matrixSize ||
      component.definiteResistive.size() != matrixSize ||
      component.possibleNonresistive.size() != matrixSize ||
      component.possibleResistive.size() != matrixSize)
    return false;
  auto close = [&](const std::vector<uint32_t> &nonresistive,
                   const std::vector<uint32_t> &resistive,
                   std::vector<uint8_t> &result) {
    result.assign(matrixSize, uint8_t{5});
    for (size_t lhs = 0; lhs != count; ++lhs) {
      result[lhs * count + lhs] = 0;
      for (size_t rhs = 0; rhs != count; ++rhs) {
        size_t slot = lhs * count + rhs;
        if (nonresistive[slot] != 0)
          result[slot] = 0;
        else if (resistive[slot] != 0)
          result[slot] = 1;
      }
    }
    // Five is the unreachable sentinel; four resistive crossings already
    // reach Table 28-8's fixed point. This dense closure is intentionally
    // component-local and contains no device-list walk.
    for (size_t through = 0; through != count; ++through)
      for (size_t lhs = 0; lhs != count; ++lhs) {
        uint8_t left = result[lhs * count + through];
        if (left == 5)
          continue;
        for (size_t rhs = 0; rhs != count; ++rhs) {
          uint8_t right = result[through * count + rhs];
          if (right == 5)
            continue;
          uint8_t candidate =
              static_cast<uint8_t>(std::min<unsigned>(4, left + right));
          result[lhs * count + rhs] =
              std::min(result[lhs * count + rhs], candidate);
        }
      }
  };
  close(component.definiteNonresistive, component.definiteResistive,
        component.reductions);
  close(component.possibleNonresistive, component.possibleResistive,
        component.possibleReductions);
  component.reachableSources.assign(count, {});
  for (size_t target = 0; target != count; ++target)
    for (size_t source = 0; source != count; ++source) {
      size_t slot = target * count + source;
      if (component.possibleReductions[slot] == 5)
        continue;
      component.reachableSources[target].push_back(
          {static_cast<uint32_t>(source), component.reductions[slot],
           component.possibleReductions[slot]});
    }
  return true;
}

NetAliasCache *getNetAliasCache(const Image &image,
                                talos_rt_context *context) {
  if (context->netAliases.execution == context->execution)
    return &context->netAliases;
  NetAliasCache cache;
  cache.execution = context->execution;
  std::vector<CaptureRecord> nets;
  std::vector<CaptureRecord> drivers;
  std::unordered_map<uint64_t, uint8_t> resolutionByBit;
  for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
    CaptureRecord record = captureAt(image, index);
    if (record.function == kNetStateDescriptor) {
      nets.push_back(record);
      std::vector<std::optional<std::array<uint64_t, 3>>> propagationDelays(
          record.planeSize);
      bool bitwiseDelay = (record.argument & (uint32_t{1} << 4)) != 0;
      if ((record.argument & (uint32_t{1} << 3)) != 0) {
        const uint8_t *encoded =
            image.data + image.constants + record.unknownOffset;
        for (uint64_t bit = 0; bit != record.planeSize; ++bit) {
          const uint8_t *triple = encoded + (bitwiseDelay ? bit * 24 : 0);
          uint64_t rise = read64(triple);
          if (rise == UINT64_MAX)
            continue;
          propagationDelays[bit] = std::array<uint64_t, 3>{
              rise, read64(triple + 8), read64(triple + 16)};
        }
      }
      cache.nets.push_back({record.valueOffset, record.valueOffset,
                            record.planeSize, (record.argument & 1) != 0,
                            bitwiseDelay, propagationDelays});
      uint8_t resolution = decodeNetResolution(record.argument);
      if (resolution == 1)
        resolution = 0; // tri and wire have identical resolution.
      for (uint64_t bit = 0; bit != record.planeSize; ++bit) {
        resolutionByBit.emplace(record.valueOffset + bit, resolution);
        if (resolution == 9)
          cache.chargeStrengthByBit.emplace(
              record.valueOffset + bit, decodeChargeStrength(record.argument));
      }
    } else if (record.function == kDriverStateDescriptor) {
      if ((record.argument & (uint32_t{1} << 11)) != 0 && !drivers.empty()) {
        const CaptureRecord &low = drivers.back();
        cache.strengthDriverPairs.push_back(
            {low.valueOffset, record.valueOffset, record.planeSize,
             decodeDriverStrength(low.argument, 3),
             decodeDriverStrength(low.argument, 7),
             decodeDriverStrength(record.argument, 3),
             decodeDriverStrength(record.argument, 7)});
      }
      drivers.push_back(record);
      cache.drivers.push_back(
          {record.valueOffset, record.unknownOffset, record.planeSize, true});
    }
  }

  std::unordered_map<uint64_t, uint64_t> parents;
  auto findRoot = [&](uint64_t value) {
    parents.try_emplace(value, value);
    uint64_t root = value;
    while (parents[root] != root)
      root = parents[root];
    while (parents[value] != value) {
      uint64_t next = parents[value];
      parents[value] = root;
      value = next;
    }
    return root;
  };
  for (const CaptureRecord &net : nets)
    for (uint64_t bit = 0; bit != net.planeSize; ++bit)
      parents.try_emplace(net.valueOffset + bit, net.valueOffset + bit);
  std::vector<std::pair<uint64_t, uint64_t>> dominanceEdges;
  for (uint64_t index = 0; index != image.connectivityCount; ++index) {
    ConnectivityRecord connection = connectivityAt(image, index);
    if ((connection.flags & 8) != 0)
      continue;
    for (uint64_t bitIndex = 0; bitIndex != connection.width; ++bitIndex) {
      uint64_t lhs = connection.lhsOffset + bitIndex;
      uint64_t rhs = (connection.flags & 1) ? connection.rhsOffset - bitIndex
                                            : connection.rhsOffset + bitIndex;
      uint64_t lhsRoot = findRoot(lhs);
      uint64_t rhsRoot = findRoot(rhs);
      if (lhsRoot != rhsRoot)
        parents[std::max(lhsRoot, rhsRoot)] = std::min(lhsRoot, rhsRoot);
      if ((connection.flags & 2) != 0)
        dominanceEdges.push_back((connection.flags & 4) != 0
                                     ? std::pair{lhs, rhs}
                                     : std::pair{rhs, lhs});
    }
  }

  for (const CaptureRecord &net : nets)
    for (uint64_t bit = 0; bit != net.planeSize; ++bit) {
      uint64_t logicalBit = net.valueOffset + bit;
      uint64_t root = findRoot(logicalBit);
      cache.rootByBit.emplace(logicalBit, root);
      cache.members[root].push_back(logicalBit);
    }
  for (uint64_t index = 0; index != image.connectivityCount; ++index) {
    ConnectivityRecord connection = connectivityAt(image, index);
    if ((connection.flags & 8) == 0)
      continue;
    for (uint64_t bitIndex = 0; bitIndex != connection.width; ++bitIndex) {
      uint64_t lhs = connection.lhsOffset + bitIndex;
      uint64_t rhs = (connection.flags & 1) ? connection.rhsOffset - bitIndex
                                            : connection.rhsOffset + bitIndex;
      uint64_t lhsRoot = cache.rootByBit.at(lhs);
      uint64_t rhsRoot = cache.rootByBit.at(rhs);
      bool resistive = (connection.flags & 16) != 0;
      bool controlled = (connection.flags & 32) != 0;
      bool directed = (connection.flags & 2) != 0;
      bool rhsToLhs = (connection.flags & 4) != 0;
      bool delayed = (connection.flags & 64) != 0;
      if (delayed) {
        uint64_t key = cache.delayedMosEdges.size();
        uint64_t sourceRoot = rhsToLhs ? rhsRoot : lhsRoot;
        uint64_t destinationRoot = rhsToLhs ? lhsRoot : rhsRoot;
        NetDelayedMosEdge edge{key, connection.tailReserved, sourceRoot,
                               destinationRoot, resistive};
        if (!cache.delayedMosEdges.emplace(key, edge).second)
          return nullptr;
        cache.delayedMosByControl[connection.tailReserved].push_back(key);
        cache.delayedMosBySource[sourceRoot].push_back(key);
        cache.delayedMosByDestination[destinationRoot].push_back(key);
        context->delayedMosContributions.try_emplace(key, strengthBit(0));
        continue;
      }
      cache.passNeighbors[lhsRoot].push_back({rhsRoot, connection.tailReserved,
                                              resistive, controlled, directed,
                                              !directed || rhsToLhs});
      cache.passNeighbors[rhsRoot].push_back({lhsRoot, connection.tailReserved,
                                              resistive, controlled, directed,
                                              !directed || !rhsToLhs});
    }
  }
  std::unordered_map<uint64_t, uint64_t> passParents;
  auto findPassRoot = [&](uint64_t value) {
    passParents.try_emplace(value, value);
    uint64_t root = value;
    while (passParents[root] != root)
      root = passParents[root];
    while (passParents[value] != value) {
      uint64_t next = passParents[value];
      passParents[value] = root;
      value = next;
    }
    return root;
  };
  for (const auto &[root, neighbors] : cache.passNeighbors)
    for (const NetPassNeighbor &neighbor : neighbors) {
      uint64_t lhsRoot = findPassRoot(root);
      uint64_t rhsRoot = findPassRoot(neighbor.root);
      if (lhsRoot != rhsRoot)
        passParents[std::max(lhsRoot, rhsRoot)] = std::min(lhsRoot, rhsRoot);
    }
  for (const auto &[root, parent] : passParents) {
    uint64_t component = findPassRoot(root);
    cache.passComponentByRoot.emplace(root, component);
    cache.passComponents[component].roots.push_back(root);
  }
  for (auto &[component, passComponent] : cache.passComponents) {
    std::vector<uint64_t> &roots = passComponent.roots;
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    if (!roots.empty() && roots.size() > SIZE_MAX / roots.size())
      return nullptr;
    size_t matrixSize = roots.size() * roots.size();
    passComponent.definiteNonresistive.assign(matrixSize, 0);
    passComponent.definiteResistive.assign(matrixSize, 0);
    passComponent.possibleNonresistive.assign(matrixSize, 0);
    passComponent.possibleResistive.assign(matrixSize, 0);
    std::unordered_map<uint64_t, size_t> rootIndex;
    for (size_t index = 0; index != roots.size(); ++index)
      rootIndex.emplace(roots[index], index);
    for (uint64_t lhsRoot : roots)
      for (const NetPassNeighbor &edge : cache.passNeighbors.at(lhsRoot)) {
        uint32_t lhs = static_cast<uint32_t>(rootIndex.at(lhsRoot));
        uint32_t rhs = static_cast<uint32_t>(rootIndex.at(edge.root));
        if ((edge.directed && !edge.receives) || (!edge.directed && lhs >= rhs))
          continue;
        size_t forward = static_cast<size_t>(lhs) * roots.size() + rhs;
        size_t reverse = static_cast<size_t>(rhs) * roots.size() + lhs;
        if (edge.controlled) {
          // State 3 is an initialization sentinel: the precomputed matrices
          // already model the IEEE x-control topology, but the roots have not
          // necessarily been resolved through it yet.
          cache.controlledPassStates.try_emplace(edge.passSwitchId, 3);
          cache.controlledPassEdges[edge.passSwitchId].push_back(
              {component, lhs, rhs, edge.resistive, edge.directed});
          auto &possible = edge.resistive ? passComponent.possibleResistive
                                          : passComponent.possibleNonresistive;
          ++possible[forward];
          if (!edge.directed)
            ++possible[reverse];
          continue;
        }
        auto &definite = edge.resistive ? passComponent.definiteResistive
                                        : passComponent.definiteNonresistive;
        auto &possible = edge.resistive ? passComponent.possibleResistive
                                        : passComponent.possibleNonresistive;
        ++definite[forward];
        if (!edge.directed)
          ++definite[reverse];
        ++possible[forward];
        if (!edge.directed)
          ++possible[reverse];
      }
    if (!rebuildPassComponent(passComponent))
      return nullptr;
  }
  for (const CaptureRecord &driver : drivers) {
    uint8_t strength0 = decodeDriverStrength(driver.argument, 3);
    uint8_t strength1 = decodeDriverStrength(driver.argument, 7);
    for (uint64_t bit = 0; bit != driver.planeSize; ++bit)
      cache.driverBits[findRoot(driver.unknownOffset + bit)].push_back(
          {driver.valueOffset + bit, strength0, strength1});
  }
  for (const NetAliasRange &net : cache.nets) {
    if (net.bitwiseDelay || net.propagationDelays.empty() ||
        !net.propagationDelays.front())
      continue;
    std::vector<uint64_t> roots;
    roots.reserve(net.width);
    for (uint64_t bit = 0; bit != net.width; ++bit)
      roots.push_back(cache.rootByBit.at(net.valueOffset + bit));
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    for (uint64_t root : roots) {
      auto &expansion = cache.uniformDelayedRootsByRoot[root];
      expansion.insert(expansion.end(), roots.begin(), roots.end());
    }
  }
  for (auto &[root, expansion] : cache.uniformDelayedRootsByRoot) {
    std::sort(expansion.begin(), expansion.end());
    expansion.erase(std::unique(expansion.begin(), expansion.end()),
                    expansion.end());
  }
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>> dominatedByRoot;
  for (const auto &edge : dominanceEdges)
    dominatedByRoot[findRoot(edge.first)].insert(edge.first);
  for (auto &[root, component] : cache.members) {
    std::sort(component.begin(), component.end());
    component.erase(std::unique(component.begin(), component.end()),
                    component.end());
    uint8_t resolution = resolutionByBit.at(component.front());
    bool mixed =
        std::any_of(component.begin(), component.end(), [&](uint64_t member) {
          return resolutionByBit.at(member) != resolution;
        });
    if (mixed) {
      // Legacy images omitted port dominance for their only supported mixed
      // topology. Uwire is intrinsically dominant over wire/tri.
      auto uwire = std::find_if(
          component.begin(), component.end(),
          [&](uint64_t member) { return resolutionByBit.at(member) == 2; });
      if (uwire != component.end())
        resolution = 2;
      const auto &dominated = dominatedByRoot[root];
      auto winner = std::find_if(
          component.begin(), component.end(),
          [&](uint64_t member) { return !dominated.count(member); });
      if (!dominated.empty() && winner != component.end())
        resolution = resolutionByBit.at(*winner);
    }
    cache.resolutionByRoot.emplace(root, resolution);
  }
  context->netAliases = std::move(cache);
  return &context->netAliases;
}

bool isComplementaryDriverPair(const Image &image, talos_rt_context *context,
                               uint64_t lowOffset, uint64_t highOffset,
                               uint64_t width) {
  if (width == 0)
    return false;
  NetAliasCache *cache = getNetAliasCache(image, context);
  if (!cache)
    return false;
  auto pair = std::upper_bound(
      cache->strengthDriverPairs.begin(), cache->strengthDriverPairs.end(),
      lowOffset, [](uint64_t offset, const NetStrengthDriverPairRange &range) {
        return offset < range.lowOffset;
      });
  if (pair == cache->strengthDriverPairs.begin())
    return false;
  --pair;
  if (lowOffset < pair->lowOffset || lowOffset - pair->lowOffset >= pair->width)
    return false;
  uint64_t local = lowOffset - pair->lowOffset;
  return width <= pair->width - local && highOffset == pair->highOffset + local;
}

bool publishNetBits(talos_rt_context *context, const NetAliasCache &cache,
                    std::vector<NetPublication> &publications, bool &changed) {
  std::sort(publications.begin(), publications.end(),
            [](const NetPublication &lhs, const NetPublication &rhs) {
              return lhs.destination < rhs.destination;
            });
  for (const NetPublication &publication : publications) {
    changed |= publication.oldValue != publication.value ||
               publication.oldUnknown != publication.unknown;
    setBit(context->stateValue, publication.destination, publication.value);
    setBit(context->stateUnknown, publication.destination, publication.unknown);
    talos_rt_sync_native_state_range_unlocked(context,
                                                publication.destination, 1);
    // A generated schedule reads its own state planes, so a net resolved here
    // has to reach those as well as the canonical image. Otherwise the value an
    // IEEE 1800-2017 10.3.3 delayed continuous assignment finally publishes
    // stays invisible to the design waiting on it.
    if (!storeNativeScheduleStateUnlocked(context, publication.destination, 1,
                                          publication.value ? 1 : 0,
                                          publication.unknown ? 1 : 0))
      return false;
  }
  // Commit every logical alias first. Route occurrences by observer range so
  // each range is published and evaluated exactly once against the completed
  // component state.
  struct RoutedPublication {
    uint64_t observerHandle;
    uint64_t observerWidth;
    uint64_t signalHandle;
    size_t publicationIndex;
    bool changed;
  };
  std::vector<RoutedPublication> routed;
  routed.reserve(publications.size());
  for (size_t index = 0; index != publications.size(); ++index) {
    const NetPublication &publication = publications[index];
    uint64_t signalHandle = talos_rt_canonical_state_handle_unlocked(
        context, publication.destination, 1);
    if (signalHandle == UINT64_MAX)
      return false;
    uint64_t publicationHandle = publication.destination;
    uint64_t publicationWidth = 1;
    uint32_t staticID = 0;
    int64_t staticOffset = 0;
    if (decodeStaticHandle(signalHandle, staticID, staticOffset)) {
      auto state = context->nativeStaticStates.find(staticID);
      if (state == context->nativeStaticStates.end())
        return false;
      publicationHandle = encodeStaticHandle(staticID, 0);
      publicationWidth = state->second.bitWidth;
    } else {
      for (const NetAliasRange &net : cache.nets)
        if (publication.destination >= net.valueOffset &&
            publication.destination < net.valueOffset + net.width) {
          publicationHandle = net.valueOffset;
          publicationWidth = net.width;
          break;
        }
    }
    talos_rt_invalidate_signal_snapshots_unlocked(context, signalHandle, 1);
    routed.push_back({publicationHandle, publicationWidth, signalHandle, index,
                      publication.oldValue != publication.value ||
                          publication.oldUnknown != publication.unknown});
  }
  std::stable_sort(
      routed.begin(), routed.end(),
      [](const RoutedPublication &lhs, const RoutedPublication &rhs) {
        return std::tie(lhs.observerHandle, lhs.observerWidth) <
               std::tie(rhs.observerHandle, rhs.observerWidth);
      });
  for (size_t groupBegin = 0; groupBegin != routed.size();) {
    size_t groupEnd = groupBegin + 1;
    while (groupEnd != routed.size() &&
           routed[groupEnd].observerHandle ==
               routed[groupBegin].observerHandle &&
           routed[groupEnd].observerWidth == routed[groupBegin].observerWidth)
      ++groupEnd;
    bool groupChanged = false;
    for (size_t index = groupBegin; index != groupEnd; ++index)
      groupChanged |= routed[index].changed;
    if (!groupChanged) {
      groupBegin = groupEnd;
      continue;
    }
    for (size_t index = groupBegin; index != groupEnd; ++index) {
      const RoutedPublication &route = routed[index];
      const NetPublication &publication = publications[route.publicationIndex];
      if (!appendSignalEvent(context, route.signalHandle, publication.oldValue,
                             publication.oldUnknown, publication.value,
                             publication.unknown, false))
        return false;
    }
    if (!talos_rt_notify_observer_signal_unlocked(
            context, routed[groupBegin].observerHandle,
            routed[groupBegin].observerWidth))
      return false;
    groupBegin = groupEnd;
  }
  return true;
}

void cancelNetBit(talos_rt_context *context, uint64_t destination,
                  bool preserveInitialProjection = false) {
  // Resolution can run while the scheduler is applying a propagation-
  // delayed driver from this same vector. Marking the superseded net event
  // avoids invalidating the scheduler's current reference; barrier scans
  // ignore the tombstone and a later compaction removes it.
  if (context->schedulerApplyingNativeUpdate ||
      context->schedulerDrainingNBAQueues) {
    for (ScheduledNBA &update : context->scheduledNBAs)
      if (update.inertialNetBit == destination &&
          (!preserveInitialProjection || !update.inertialNetInitialProjection))
        update.cancelled = true;
  } else {
    context->scheduledNBAs.erase(
        std::remove_if(context->scheduledNBAs.begin(),
                       context->scheduledNBAs.end(),
                       [&](const ScheduledNBA &update) {
                         return update.inertialNetBit == destination &&
                                (!preserveInitialProjection ||
                                 !update.inertialNetInitialProjection);
                       }),
        context->scheduledNBAs.end());
  }
  context->inertialNetPending.erase(destination);
}

void cancelNetGroup(talos_rt_context *context, uint64_t group) {
  std::vector<uint64_t> destinations;
  for (ScheduledNBA &update : context->scheduledNBAs) {
    if (update.inertialNetCancelGroup != group)
      continue;
    destinations.push_back(update.inertialNetBit);
    if (context->schedulerApplyingNativeUpdate ||
        context->schedulerDrainingNBAQueues)
      update.cancelled = true;
  }
  if (!context->schedulerApplyingNativeUpdate &&
      !context->schedulerDrainingNBAQueues)
    context->scheduledNBAs.erase(
        std::remove_if(context->scheduledNBAs.begin(),
                       context->scheduledNBAs.end(),
                       [&](const ScheduledNBA &update) {
                         return update.inertialNetCancelGroup == group;
                       }),
        context->scheduledNBAs.end());
  for (uint64_t destination : destinations)
    context->inertialNetPending.erase(destination);
}

bool scheduleNetBit(talos_rt_context *context, uint64_t root,
                    uint64_t cancelGroup, uint64_t destination, bool value,
                    bool unknown, const std::array<uint64_t, 3> &delays,
                    uint8_t resolution, bool chargeDecay) {
  if (auto pending = context->inertialNetPending.find(destination);
      pending != context->inertialNetPending.end() &&
      pending->second.value == value && pending->second.unknown == unknown &&
      pending->second.chargeDecay == chargeDecay)
    return true;

  bool currentValue = bit(context->stateValue, destination);
  bool currentUnknown = bit(context->stateUnknown, destination);
  if (currentValue == value && currentUnknown == unknown) {
    cancelNetBit(context, destination);
    return true;
  }
  // Preserve the first projected value that establishes a wire from its
  // implicit high-impedance initialization. Once a value has matured, normal
  // inertial pulse rejection replaces the older pending projection.
  bool initialProjection = false;
  if (!chargeDecay && currentValue && currentUnknown) {
    initialProjection = std::none_of(
        context->scheduledNBAs.begin(), context->scheduledNBAs.end(),
        [&](const ScheduledNBA &update) {
          return update.inertialNetBit == destination &&
                 update.inertialNetInitialProjection && !update.cancelled;
        });
    cancelNetBit(context, destination, true);
  } else {
    cancelNetBit(context, destination);
  }
  if (context->nextSchedulerSequence == 0 ||
      context->nextSchedulerSequence == UINT64_MAX) {
    context->schedulerStatus = TALOS_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint64_t handle =
      talos_rt_canonical_state_handle_unlocked(context, destination, 1);
  if (handle == UINT64_MAX) {
    context->schedulerStatus = TALOS_RT_INVALID_HANDLE;
    return false;
  }
  uint64_t delay;
  if (chargeDecay)
    delay = delays[2];
  else if (!unknown)
    delay = delays[value ? 0 : 1];
  else if (value)
    delay = delays[2];
  else if (resolution == 9)
    delay = std::min(delays[0], delays[1]);
  else
    delay = std::min({delays[0], delays[1], delays[2]});
  ScheduledNBA update;
  update.sequence = context->nextSchedulerSequence++;
  update.dueTime = delay > UINT64_MAX - context->schedulerTime
                       ? UINT64_MAX
                       : context->schedulerTime + delay;
  update.execRegion = TALOS_RT_REGION_ACTIVE;
  // Native/generic execution binds compiler-emitted planes during startup.
  // Write those planes as the scheduled destination while applyNative also
  // maintains the canonical context image. Pure bytecode contexts have no
  // binding and use the canonical planes directly.
  update.valuePlane =
      context->nativeStateValue
          ? context->nativeStateValue
          : reinterpret_cast<uint8_t *>(context->stateValue.data());
  update.unknownPlane =
      context->nativeStateUnknown
          ? context->nativeStateUnknown
          : reinterpret_cast<uint8_t *>(context->stateUnknown.data());
  update.planeBitCount = context->execution->state_bit_count;
  update.bitOffset = handle;
  update.bitWidth = 1;
  update.inlinePacked = true;
  update.inlineValue = value;
  update.inlineUnknown = unknown;
  update.inertialNetBit = destination;
  update.inertialNetGroup = root;
  update.inertialNetCancelGroup = cancelGroup;
  update.inertialNetInitialProjection = initialProjection;
  update.inertialNetChargeDecay = chargeDecay;
  context->scheduledNBAs.push_back(std::move(update));
  context->inertialNetPending.emplace(
      destination, InertialNetPending{value, unknown, chargeDecay});
  return true;
}

// Queries and resolution must observe the same current state planes. A direct
// native schedule can own the published planes; dirty roots still use the
// canonical context state until their pending update is synchronized.
class DriverStateReader {
public:
  DriverStateReader(talos_rt_context *context, bool useNativeState,
                    bool useSchedulePlan)
      : context(context) {
    if (useSchedulePlan && context->nativeSchedulePlan && context->execution &&
        (context->nativeSchedulePlan->flags &
         TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE) != 0 &&
        !context->nativeScheduleDeoptimized &&
        context->nativeSchedulePlan->state_bit_count ==
            context->execution->state_bit_count &&
        context->nativeSchedulePlan->state_value &&
        context->nativeSchedulePlan->state_unknown) {
      nativeValue = context->nativeSchedulePlan->state_value;
      nativeUnknown = context->nativeSchedulePlan->state_unknown;
      nativeBits = context->nativeSchedulePlan->state_bit_count;
      schedulePlanState = true;
    } else if (useNativeState && context->execution &&
               context->nativeStateBitCount ==
                   context->execution->state_bit_count) {
      nativeValue = context->nativeStateValue;
      nativeUnknown = context->nativeStateUnknown;
      nativeBits = context->nativeStateBitCount;
    }
  }

  bool operator()(bool unknownPlane, uint64_t offset) const {
    const uint8_t *native = unknownPlane ? nativeUnknown : nativeValue;
    bool dirty = false;
    if (schedulePlanState && context->nativeScheduleDirtyRootsPresent) {
      for (const auto &[id, state] : context->nativeStaticStates) {
        if (offset < state.bitOffset ||
            offset - state.bitOffset >= state.bitWidth)
          continue;
        dirty = context->nativeScheduleTransientDirtyRoots.find(id) !=
                    context->nativeScheduleTransientDirtyRoots.end() ||
                context->nativeSchedulePersistentDirtyRoots.find(id) !=
                    context->nativeSchedulePersistentDirtyRoots.end();
        if (dirty)
          break;
      }
    }
    if (native && offset < nativeBits && !dirty)
      return (native[offset / 8] & static_cast<uint8_t>(1u << (offset % 8))) !=
             0;
    return bit(unknownPlane ? context->stateUnknown : context->stateValue,
               offset);
  }

private:
  talos_rt_context *context;
  const uint8_t *nativeValue = nullptr;
  const uint8_t *nativeUnknown = nullptr;
  uint64_t nativeBits = 0;
  bool schedulePlanState = false;
};

static uint16_t driverStrengths(bool value, bool unknown, uint8_t strength0,
                                uint8_t strength1) {
  return runtime::driverBitStrengths(value, unknown, strength0, strength1);
}

static bool computeResolvedStrengths(const NetAliasCache &cache,
                                     talos_rt_context *context, uint64_t root,
                                     bool useNativeState, bool useSchedulePlan,
                                     uint16_t &resolvedStrengths) {
  auto members = cache.members.find(root);
  if (members == cache.members.end())
    return false;
  DriverStateReader stateBit(context, useNativeState, useSchedulePlan);
  uint8_t resolution = cache.resolutionByRoot.at(root);
  auto component = cache.passComponentByRoot.find(root);
  resolvedStrengths = strengthBit(0);
  auto accumulateRoot = [&](uint64_t sourceRoot, uint8_t resistiveEdges,
                            uint8_t possibleResistiveEdges) {
    if (possibleResistiveEdges == 5)
      return true;
    bool remote = sourceRoot != root;
    auto sourceMembers = cache.members.find(sourceRoot);
    if (sourceMembers == cache.members.end())
      return false;
    auto forcedMember =
        std::find_if(sourceMembers->second.begin(), sourceMembers->second.end(),
                     [&](uint64_t member) {
                       return member / 64 < context->forceMask.size() &&
                              (context->forceMask[member / 64] &
                               (uint64_t{1} << (member % 64))) != 0;
                     });
    if (forcedMember != sourceMembers->second.end()) {
      uint16_t strengths = driverStrengths(stateBit(false, *forcedMember),
                                           stateBit(true, *forcedMember), 7, 7);
      if (remote)
        strengths = propagateFromRemote(strengths, resistiveEdges,
                                        possibleResistiveEdges);
      resolvedStrengths =
          combineStrengthRanges(resolvedStrengths, strengths, resolution);
      return true;
    }
    uint16_t implicit =
        implicitNetStrength(cache.resolutionByRoot.at(sourceRoot));
    if (remote)
      implicit =
          propagateFromRemote(implicit, resistiveEdges, possibleResistiveEdges);
    resolvedStrengths =
        combineStrengthRanges(resolvedStrengths, implicit, resolution);
    auto componentDrivers = cache.driverBits.find(sourceRoot);
    if (componentDrivers != cache.driverBits.end())
      for (const NetDriverBit &driver : componentDrivers->second) {
        bool driverValue = stateBit(false, driver.valueOffset);
        bool driverUnknown = stateBit(true, driver.valueOffset);
        uint16_t strengths = driverStrengths(
            driverValue, driverUnknown, driver.strength0, driver.strength1);
        if (remote)
          strengths = propagateFromRemote(strengths, resistiveEdges,
                                          possibleResistiveEdges);
        resolvedStrengths =
            combineStrengthRanges(resolvedStrengths, strengths, resolution);
      }
    auto delayed = cache.delayedMosByDestination.find(sourceRoot);
    if (delayed != cache.delayedMosByDestination.end())
      for (uint64_t edge : delayed->second) {
        auto contribution = context->delayedMosContributions.find(edge);
        if (contribution == context->delayedMosContributions.end())
          return false;
        uint16_t strengths = contribution->second;
        if (remote)
          strengths = propagateFromRemote(strengths, resistiveEdges,
                                          possibleResistiveEdges);
        resolvedStrengths =
            combineStrengthRanges(resolvedStrengths, strengths, resolution);
      }
    return true;
  };
  if (component != cache.passComponentByRoot.end()) {
    const NetPassComponent &passComponent =
        cache.passComponents.at(component->second);
    const std::vector<uint64_t> &roots = passComponent.roots;
    auto target = std::lower_bound(roots.begin(), roots.end(), root);
    if (target == roots.end() || *target != root ||
        passComponent.reductions.size() != roots.size() * roots.size() ||
        passComponent.possibleReductions.size() !=
            roots.size() * roots.size() ||
        passComponent.reachableSources.size() != roots.size())
      return false;
    size_t targetIndex = static_cast<size_t>(target - roots.begin());
    for (const NetPassComponent::ReachableSource &source :
         passComponent.reachableSources[targetIndex])
      if (source.index >= roots.size() ||
          !accumulateRoot(roots[source.index], source.definiteReduction,
                          source.possibleReduction))
        return false;
  } else if (!accumulateRoot(root, 0, 0)) {
    return false;
  }
  return true;
}

static uint16_t normalizeStrengthRange(uint16_t strengths) {
  unsigned first = 0;
  while (first != strengthCount && (strengths & (uint16_t{1} << first)) == 0)
    ++first;
  if (first == strengthCount)
    return strengthBit(0);
  unsigned last = strengthCount;
  while (last != first && (strengths & (uint16_t{1} << (last - 1))) == 0)
    --last;
  uint16_t range = 0;
  for (unsigned index = first; index != last; ++index)
    range |= uint16_t{1} << index;
  return range;
}

static bool scheduleDelayedMosEdge(talos_rt_context *context,
                                   const NetDelayedMosEdge &edge,
                                   uint16_t sourceStrengths,
                                   const DelayedMosControl &control) {
  if (control.state > 2)
    return true;
  uint16_t target = strengthBit(0);
  if (control.state != 0) {
    target = propagateThroughPass(sourceStrengths, edge.resistive ? 1 : 0);
    if (control.state == 2)
      target = normalizeStrengthRange(target | strengthBit(0));
  }
  auto current = context->delayedMosContributions.find(edge.key);
  if (current == context->delayedMosContributions.end())
    return false;
  auto pending = context->delayedMosPending.find(edge.key);
  if (pending != context->delayedMosPending.end() &&
      pending->second.strengths == target)
    return true;
  if (pending != context->delayedMosPending.end()) {
    auto scheduled = pending->second.event;
    if (scheduled == context->scheduledPassSwitchEvents.end() ||
        scheduled->second.delayedMosEdge != edge.key)
      return false;
    context->scheduledPassSwitchEvents.erase(scheduled);
    context->delayedMosPending.erase(pending);
  }
  if (current->second == target)
    return true;

  constexpr uint16_t negativeMask = (uint16_t{1} << 7) - 1;
  constexpr uint16_t positiveMask = static_cast<uint16_t>(
      ((uint16_t{1} << strengthCount) - 1) & ~((uint16_t{1} << 8) - 1));
  bool hasZ = (target & strengthBit(0)) != 0;
  bool hasNegative = (target & negativeMask) != 0;
  bool hasPositive = (target & positiveMask) != 0;
  uint64_t delay = 0;
  if (hasPositive && !hasNegative && !hasZ)
    delay = control.delays[0];
  else if (hasNegative && !hasPositive && !hasZ)
    delay = control.delays[1];
  else if (hasZ && !hasNegative && !hasPositive)
    delay = control.delays[2];
  else
    delay = std::min({control.delays[0], control.delays[1], control.delays[2]});
  if (context->nextSchedulerSequence == 0 ||
      context->nextSchedulerSequence == UINT64_MAX) {
    context->schedulerStatus = TALOS_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint64_t sequence = context->nextSchedulerSequence++;
  uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                         ? UINT64_MAX
                         : context->schedulerTime + delay;
  std::pair<uint64_t, uint64_t> key{dueTime, sequence};
  ScheduledPassSwitchEvent event;
  event.delayedMosEdge = edge.key;
  event.strengths = target;
  auto scheduled = context->scheduledPassSwitchEvents.emplace(key, event);
  if (!scheduled.second)
    return false;
  TALOS_RT_TRY {
    if (!context->delayedMosPending
             .emplace(edge.key, DelayedMosPending{scheduled.first, target})
             .second) {
      context->scheduledPassSwitchEvents.erase(scheduled.first);
      return false;
    }
  }
  TALOS_RT_CATCH_ALL {
    context->scheduledPassSwitchEvents.erase(scheduled.first);
    TALOS_RT_RETHROW;
  }
  return true;
}

bool resolveNetRoots(const NetAliasCache &cache, talos_rt_context *context,
                     std::vector<uint64_t> affectedRoots, bool &changed,
                     bool useNativeState) {
  if (affectedRoots.empty())
    return false;
  // A uniform vector net delay rejects transitions as one vector even when
  // only one contributing driver bit changed. Expand the worklist to every
  // bit of each affected vector before computing its replacement target.
  std::unordered_set<uint64_t> affectedSet(affectedRoots.begin(),
                                           affectedRoots.end());
  for (size_t index = 0; index != affectedRoots.size(); ++index) {
    auto component = cache.passComponentByRoot.find(affectedRoots[index]);
    if (component == cache.passComponentByRoot.end())
      continue;
    for (uint64_t root : cache.passComponents.at(component->second).roots)
      if (affectedSet.insert(root).second)
        affectedRoots.push_back(root);
  }
  for (size_t index = 0; index != affectedRoots.size(); ++index) {
    auto expansion = cache.uniformDelayedRootsByRoot.find(affectedRoots[index]);
    if (expansion == cache.uniformDelayedRootsByRoot.end())
      continue;
    for (uint64_t root : expansion->second)
      if (affectedSet.insert(root).second)
        affectedRoots.push_back(root);
  }
  std::sort(affectedRoots.begin(), affectedRoots.end());
  affectedRoots.erase(std::unique(affectedRoots.begin(), affectedRoots.end()),
                      affectedRoots.end());
  std::vector<NetPublication> publications;
  struct DelayedNetPublication {
    uint64_t root = 0;
    uint64_t cancelGroup = 0;
    NetPublication publication{};
    std::array<uint64_t, 3> delays{};
    uint8_t resolution = 0;
    bool initiallyHighZ = false;
  };
  std::vector<DelayedNetPublication> delayedPublications;
  std::unordered_map<uint64_t, bool> initiallyHighZGroups;
  for (uint64_t root : affectedRoots) {
    auto members = cache.members.find(root);
    if (members == cache.members.end())
      return false;
    // IEEE 1800-2017 28.12 defines an ambiguous signal as a range on the
    // signed Figure 28-2 strength scale. Resolve the small, fixed 15-point
    // domain exactly, then collapse the final range to a four-state value.
    // Keeping L/H ranges until every driver participates is essential: a
    // strong L combined with a strong 0 resolves to 0, not x.
    uint8_t resolution = cache.resolutionByRoot.at(root);
    uint16_t resolvedStrengths = 0;
    if (!computeResolvedStrengths(cache, context, root, useNativeState,
                                  /*useSchedulePlan=*/false, resolvedStrengths))
      return false;
    if (auto outgoing = cache.delayedMosBySource.find(root);
        outgoing != cache.delayedMosBySource.end())
      for (uint64_t edgeKey : outgoing->second) {
        auto edge = cache.delayedMosEdges.find(edgeKey);
        if (edge == cache.delayedMosEdges.end())
          return false;
        auto control = context->delayedMosControls.find(edge->second.control);
        if (control != context->delayedMosControls.end() &&
            !scheduleDelayedMosEdge(context, edge->second, resolvedStrengths,
                                    control->second))
          return false;
      }
    auto [resolvedValue, resolvedUnknown] =
        runtime::resolvedBit(resolvedStrengths);
    bool resolvedZ = resolvedValue && resolvedUnknown;
    bool chargeValue = false;
    bool chargeUnknown = true;
    if (resolution == 9 && resolvedZ) {
      // IEEE 1800-2017 6.6.4 and 28.16: once every active driver is Z,
      // resolve the stored charges on the component at their declared
      // small/medium/large strengths. This is charge sharing; unlike an
      // active driver, a weaker stored charge can lose to a stronger one.
      // 6.6.4.1: the charge spreads over the capacitive network, the trireg
      // nets that conducting pass switches connect to this one.
      uint16_t chargeStrengths = strengthBit(0);
      auto accumulateCharges = [&](uint64_t sourceRoot, uint8_t resistiveEdges,
                                   uint8_t possibleResistiveEdges) {
        if (possibleResistiveEdges == 5)
          return true;
        auto sourceMembers = cache.members.find(sourceRoot);
        if (sourceMembers == cache.members.end())
          return false;
        for (uint64_t member : sourceMembers->second) {
          auto strength = cache.chargeStrengthByBit.find(member);
          if (strength == cache.chargeStrengthByBit.end())
            continue;
          bool value = bit(context->stateValue, member);
          bool unknown = bit(context->stateUnknown, member);
          uint16_t stored = 0;
          if (!unknown) {
            stored = strengthBit(value ? strength->second
                                       : -static_cast<int>(strength->second));
          } else if (value) {
            stored = strengthBit(0);
          } else {
            for (int point = -static_cast<int>(strength->second);
                 point <= static_cast<int>(strength->second); ++point)
              stored |= strengthBit(point);
          }
          if (sourceRoot != root)
            stored = propagateFromRemote(stored, resistiveEdges,
                                         possibleResistiveEdges);
          chargeStrengths = combineStrengthRanges(chargeStrengths, stored);
        }
        return true;
      };
      if (auto component = cache.passComponentByRoot.find(root);
          component != cache.passComponentByRoot.end()) {
        const NetPassComponent &passComponent =
            cache.passComponents.at(component->second);
        const std::vector<uint64_t> &roots = passComponent.roots;
        auto target = std::lower_bound(roots.begin(), roots.end(), root);
        if (target == roots.end() || *target != root ||
            passComponent.reachableSources.size() != roots.size())
          return false;
        for (const NetPassComponent::ReachableSource &source :
             passComponent.reachableSources[target - roots.begin()])
          if (source.index >= roots.size() ||
              !accumulateCharges(roots[source.index], source.definiteReduction,
                                 source.possibleReduction))
            return false;
      } else if (!accumulateCharges(root, 0, 0)) {
        return false;
      }
      // Only a known 0 or 1 charge survives; anything else, including no
      // charge at all, is x.
      runtime::ResolvedBit charge = runtime::resolvedBit(chargeStrengths);
      chargeUnknown = charge.unknown;
      chargeValue = !charge.unknown && charge.value;
    }
    for (uint64_t destination : members->second) {
      const NetAliasRange *net = nullptr;
      for (const NetAliasRange &candidate : cache.nets)
        if (destination >= candidate.valueOffset &&
            destination < candidate.valueOffset + candidate.width) {
          net = &candidate;
          break;
        }
      if (!net)
        return false;
      bool publishUnknown = net->fourState && resolvedUnknown;
      bool publishValue = net->fourState
                              ? resolvedValue
                              : (resolvedUnknown ? false : resolvedValue);
      if (resolution == 9 && resolvedZ) {
        publishValue = chargeValue;
        publishUnknown = chargeUnknown;
      }
      uint64_t mask = uint64_t{1} << (destination % 64);
      bool forced = destination / 64 < context->forceMask.size() &&
                    (context->forceMask[destination / 64] & mask) != 0;
      bool assigned = destination / 64 < context->assignMask.size() &&
                      (context->assignMask[destination / 64] & mask) != 0;
      if (forced || assigned) {
        publishValue = bit(context->stateValue, destination);
        publishUnknown = bit(context->stateUnknown, destination);
      }
      NetPublication publication{destination,
                                 bit(context->stateValue, destination),
                                 bit(context->stateUnknown, destination),
                                 publishValue, publishUnknown};
      const auto &delays =
          net->propagationDelays[destination - net->valueOffset];
      if (delays) {
        bool decay = resolution == 9 && resolvedZ && !forced && !assigned &&
                     !publishUnknown && (*delays)[2] != UINT64_MAX;
        if (resolution == 9 && resolvedZ) {
          // Charge sharing takes effect on entry to the capacitive state;
          // decay is a later transition from that shared value to x.
          publications.push_back(publication);
          if (decay) {
            if (!scheduleNetBit(context, root, root, destination, false, true,
                                *delays, resolution, true))
              return false;
          } else {
            cancelNetBit(context, destination);
          }
        } else {
          uint64_t group = net->bitwiseDelay ? destination : net->valueOffset;
          auto [initial, inserted] =
              initiallyHighZGroups.try_emplace(group, true);
          if (inserted)
            for (uint64_t bit = 0; bit != net->width; ++bit)
              initial->second &=
                  talos::designbytecode::bit(context->stateValue,
                                               net->valueOffset + bit) &&
                  talos::designbytecode::bit(context->stateUnknown,
                                               net->valueOffset + bit);
          delayedPublications.push_back(
              {root, group, publication, *delays, resolution, initial->second});
        }
      } else {
        publications.push_back(publication);
      }
    }
  }
  if (!delayedPublications.empty()) {
    std::unordered_set<uint64_t> groupsToCancel;
    std::unordered_set<uint64_t> groupsWithPending;
    for (const ScheduledNBA &update : context->scheduledNBAs)
      if (update.inertialNetBit != UINT64_MAX &&
          !update.inertialNetInitialProjection && !update.cancelled)
        groupsWithPending.insert(update.inertialNetCancelGroup);
    for (const DelayedNetPublication &delayed : delayedPublications) {
      if (delayed.initiallyHighZ)
        continue;
      auto pending =
          context->inertialNetPending.find(delayed.publication.destination);
      bool matchingPending =
          pending != context->inertialNetPending.end() &&
          pending->second.value == delayed.publication.value &&
          pending->second.unknown == delayed.publication.unknown &&
          !pending->second.chargeDecay;
      bool targetChanged =
          delayed.publication.oldValue != delayed.publication.value ||
          delayed.publication.oldUnknown != delayed.publication.unknown;
      if ((pending != context->inertialNetPending.end() && !matchingPending) ||
          (targetChanged && !matchingPending &&
           groupsWithPending.count(delayed.cancelGroup)))
        groupsToCancel.insert(delayed.cancelGroup);
    }
    for (uint64_t group : groupsToCancel)
      cancelNetGroup(context, group);
    for (const DelayedNetPublication &delayed : delayedPublications)
      if (!scheduleNetBit(context, delayed.root, delayed.cancelGroup,
                          delayed.publication.destination,
                          delayed.publication.value,
                          delayed.publication.unknown, delayed.delays,
                          delayed.resolution, false))
        return false;
  }
  if (publications.empty())
    return true;
  return publishNetBits(context, cache, publications, changed);
}

bool resolveNetRoots(const NetAliasCache &cache, talos_rt_context *context,
                     std::vector<uint64_t> affectedRoots, bool &changed) {
  return resolveNetRoots(cache, context, std::move(affectedRoots), changed,
                         false);
}

bool resolveDrivenNets(const Image &image, talos_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd, bool &changed,
                       bool useNativeState) {
  if (!context || changedBegin < 0 || changedEnd < changedBegin)
    return false;
  NetAliasCache *cache = getNetAliasCache(image, context);
  std::vector<uint64_t> affectedRoots;
  // A net force release names resolved-net state rather than a driver slot.
  // Seed the same component roots from an overlapping net range so release
  // can republish from all current drivers even when no driver just changed.
  for (const NetAliasRange &net : cache->nets) {
    uint64_t netEnd = net.valueOffset + net.width;
    uint64_t overlapBegin = std::max<uint64_t>(
        static_cast<uint64_t>(changedBegin), net.valueOffset);
    uint64_t overlapEnd =
        std::min<uint64_t>(static_cast<uint64_t>(changedEnd), netEnd);
    for (uint64_t netBit = overlapBegin; netBit < overlapEnd; ++netBit) {
      uint64_t root = cache->rootByBit.at(netBit);
      if (std::find(affectedRoots.begin(), affectedRoots.end(), root) ==
          affectedRoots.end())
        affectedRoots.push_back(root);
    }
  }
  for (const NetAliasRange &driver : cache->drivers) {
    uint64_t driverEnd = driver.valueOffset + driver.width;
    uint64_t overlapBegin = std::max<uint64_t>(
        static_cast<uint64_t>(changedBegin), driver.valueOffset);
    uint64_t overlapEnd =
        std::min<uint64_t>(static_cast<uint64_t>(changedEnd), driverEnd);
    for (uint64_t driverBit = overlapBegin; driverBit < overlapEnd;
         ++driverBit) {
      uint64_t root = cache->rootByBit.at(driver.targetOffset + driverBit -
                                          driver.valueOffset);
      if (std::find(affectedRoots.begin(), affectedRoots.end(), root) ==
          affectedRoots.end())
        affectedRoots.push_back(root);
    }
  }
  return resolveNetRoots(*cache, context, std::move(affectedRoots), changed,
                         useNativeState);
}

bool resolveDrivenNets(const Image &image, talos_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd,
                       bool &changed) {
  return resolveDrivenNets(image, context, changedBegin, changedEnd, changed,
                           false);
}

static bool resolveInitialDriverNets(const Image &image,
                                     talos_rt_context *context,
                                     bool useNativeState, bool &changed) {
  NetAliasCache *cache = getNetAliasCache(image, context);
  if (!cache)
    return false;
  std::vector<uint64_t> roots;
  std::unordered_set<uint64_t> seen;
  for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
    CaptureRecord driver = captureAt(image, index);
    if (driver.function != kDriverStateDescriptor ||
        (driver.argument & TALOS_RT_DB_DRIVER_INITIAL_X) == 0)
      continue;
    for (uint64_t bit = 0; bit != driver.planeSize; ++bit) {
      auto found = cache->rootByBit.find(driver.unknownOffset + bit);
      if (found == cache->rootByBit.end())
        return false;
      if (seen.insert(found->second).second)
        roots.push_back(found->second);
    }
  }
  if (roots.empty())
    return true;
  return resolveNetRoots(*cache, context, std::move(roots), changed,
                         useNativeState);
}

} // namespace talos::designbytecode

using namespace talos::designbytecode;

extern "C" uint16_t talos_rt_v1_strength_resolve(uint16_t lhs, uint16_t rhs) {
  return combineStrengthRanges(lhs, rhs);
}

talos_rt_status talos_rt_design_net_strength(talos_rt_context *context,
                                                 uint64_t netHandle,
                                                 uint16_t *outStrengths,
                                                 bool useNativeState) noexcept {
  if (!context || !context->execution || !outStrengths)
    return TALOS_RT_INVALID_ARGUMENT;
  *outStrengths = 0;
  TALOS_RT_TRY {
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    ContextTransaction transaction(context);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);

    int64_t coordinate = 0;
    uint64_t absolute = 0;
    uint32_t staticID = 0;
    if (decodeNativeStatic(netHandle, staticID, coordinate)) {
      const NativeStaticState *state = findNativeStaticState(context, staticID);
      if (!state || coordinate < 0 ||
          static_cast<uint64_t>(coordinate) >= state->bitWidth)
        return TALOS_RT_INVALID_HANDLE;
      absolute = state->bitOffset + static_cast<uint64_t>(coordinate);
    } else if (decodeNativeGlobal(netHandle, coordinate)) {
      if (coordinate < 0)
        return TALOS_RT_INVALID_HANDLE;
      absolute = static_cast<uint64_t>(coordinate);
    } else {
      return TALOS_RT_INVALID_HANDLE;
    }

    NetAliasCache *cache = getNetAliasCache(image, context);
    auto root = cache->rootByBit.find(absolute);
    if (root == cache->rootByBit.end())
      return TALOS_RT_INVALID_HANDLE;
    if (!computeResolvedStrengths(*cache, context, root->second, useNativeState,
                                  /*useSchedulePlan=*/useNativeState,
                                  *outStrengths))
      return TALOS_RT_INVALID_HANDLE;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

talos_rt_status talos_rt_design_driver_strength(
    talos_rt_context *context, uint64_t lowOffset, uint64_t highOffset,
    uint16_t *outStrengths, bool useNativeState) noexcept {
  if (!context || !context->execution || !outStrengths)
    return TALOS_RT_INVALID_ARGUMENT;
  *outStrengths = 0;
  if ((context->execution->flags & TALOS_RT_EXECUTION_HAS_BYTECODE) == 0)
    return TALOS_RT_EOF;
  TALOS_RT_TRY {
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    ContextTransaction transaction(context);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);

    // Validated state descriptors contain net records followed by drivers in
    // nonoverlapping state order. Find the driver containing the selected bit.
    uint64_t first = 0, last = image.stateDescriptorCount;
    while (first < last) {
      uint64_t middle = first + (last - first) / 2;
      CaptureRecord candidate = captureAt(image, middle);
      if (candidate.function < kDriverStateDescriptor ||
          candidate.valueOffset <= lowOffset)
        first = middle + 1;
      else
        last = middle;
    }
    if (first == 0)
      return TALOS_RT_INVALID_HANDLE;
    uint64_t lowIndex = first - 1;
    CaptureRecord low = captureAt(image, lowIndex);
    constexpr uint32_t highBank = uint32_t{1} << 11;
    if (low.function != kDriverStateDescriptor ||
        (low.argument & highBank) != 0 || lowOffset < low.valueOffset ||
        lowOffset - low.valueOffset >= low.planeSize)
      return TALOS_RT_INVALID_HANDLE;

    CaptureRecord high;
    bool paired =
        lowIndex + 1 < image.stateDescriptorCount &&
        ((high = captureAt(image, lowIndex + 1)).argument & highBank) != 0;
    if (paired != (highOffset != UINT64_MAX))
      return TALOS_RT_INVALID_HANDLE;
    if (paired &&
        (high.function != kDriverStateDescriptor ||
         highOffset != high.valueOffset + (lowOffset - low.valueOffset)))
      return TALOS_RT_INVALID_HANDLE;

    DriverStateReader stateBit(context, useNativeState,
                               /*useSchedulePlan=*/useNativeState);
    auto readStrengths = [&](const CaptureRecord &driver, uint64_t offset) {
      return driverStrengths(stateBit(false, offset), stateBit(true, offset),
                             decodeDriverStrength(driver.argument, 3),
                             decodeDriverStrength(driver.argument, 7));
    };
    uint16_t strengths = readStrengths(low, lowOffset);
    if (paired)
      strengths =
          combineStrengthRanges(strengths, readStrengths(high, highOffset));
    *outStrengths = strengths;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

talos_rt_status talos_rt_count_design_drivers(
    talos_rt_context *context, uint64_t netHandle, uint32_t *outForced,
    uint32_t *outTotal, uint32_t *outZero, uint32_t *outOne,
    uint32_t *outUnknown, bool useNativeState) noexcept {
  if (!context || !context->execution || !outForced || !outTotal || !outZero ||
      !outOne || !outUnknown)
    return TALOS_RT_INVALID_ARGUMENT;
  *outForced = *outTotal = *outZero = *outOne = *outUnknown = 0;
  TALOS_RT_TRY {
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    ContextTransaction transaction(context);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);

    int64_t coordinate = 0;
    uint64_t absolute = 0;
    uint32_t staticID = 0;
    if (decodeNativeStatic(netHandle, staticID, coordinate)) {
      const NativeStaticState *state = findNativeStaticState(context, staticID);
      if (!state || coordinate < 0 ||
          static_cast<uint64_t>(coordinate) >= state->bitWidth)
        return TALOS_RT_INVALID_HANDLE;
      absolute = state->bitOffset + static_cast<uint64_t>(coordinate);
    } else if (decodeNativeGlobal(netHandle, coordinate)) {
      if (coordinate < 0)
        return TALOS_RT_INVALID_HANDLE;
      absolute = static_cast<uint64_t>(coordinate);
    } else {
      return TALOS_RT_INVALID_HANDLE;
    }

    NetAliasCache *cache = getNetAliasCache(image, context);
    auto rootFound = cache->rootByBit.find(absolute);
    if (rootFound == cache->rootByBit.end())
      return TALOS_RT_INVALID_HANDLE;
    uint64_t root = rootFound->second;
    auto members = cache->members.find(root);
    if (members == cache->members.end())
      return TALOS_RT_INVALID_HANDLE;
    for (uint64_t member : members->second)
      if (member / 64 < context->forceMask.size() &&
          (context->forceMask[member / 64] & (uint64_t{1} << (member % 64))) !=
              0) {
        *outForced = 1;
        break;
      }

    auto contribution = [&](uint16_t strengths) {
      bool hasZ = (strengths & strengthBit(0)) != 0;
      constexpr uint16_t negative = (uint16_t{1} << 7) - 1;
      constexpr uint16_t positive = static_cast<uint16_t>(
          ((uint16_t{1} << strengthCount) - 1) & ~((uint16_t{1} << 8) - 1));
      bool hasZero = (strengths & negative) != 0;
      bool hasOne = (strengths & positive) != 0;
      if (hasZ && !hasZero && !hasOne)
        return;
      ++*outTotal;
      if (hasZero && !hasZ && !hasOne)
        ++*outZero;
      else if (hasOne && !hasZ && !hasZero)
        ++*outOne;
      else
        ++*outUnknown;
    };
    auto stateBit = [&](bool unknownPlane, uint64_t offset) {
      const uint8_t *native = unknownPlane ? context->nativeStateUnknown
                                           : context->nativeStateValue;
      if (useNativeState && native && offset < context->nativeStateBitCount)
        return (native[offset / 8] &
                static_cast<uint8_t>(1u << (offset % 8))) != 0;
      return bit(unknownPlane ? context->stateUnknown : context->stateValue,
                 offset);
    };
    auto strengthsFor = [&](uint64_t offset, uint8_t strength0,
                            uint8_t strength1) {
      bool value = stateBit(false, offset);
      bool unknown = stateBit(true, offset);
      if (!unknown)
        return strengthBit(value ? strength1 : -static_cast<int>(strength0));
      if (value)
        return strengthBit(0);
      uint16_t strengths = 0;
      for (int point = -static_cast<int>(strength0);
           point <= static_cast<int>(strength1); ++point)
        strengths |= strengthBit(point);
      return strengths;
    };
    auto containingPair =
        [&](uint64_t offset,
            bool highBank) -> const NetStrengthDriverPairRange * {
      auto pair = std::upper_bound(
          cache->strengthDriverPairs.begin(), cache->strengthDriverPairs.end(),
          offset,
          [&](uint64_t needle, const NetStrengthDriverPairRange &range) {
            return needle < (highBank ? range.highOffset : range.lowOffset);
          });
      if (pair == cache->strengthDriverPairs.begin())
        return static_cast<const NetStrengthDriverPairRange *>(nullptr);
      --pair;
      uint64_t start = highBank ? pair->highOffset : pair->lowOffset;
      if (offset < start || offset - start >= pair->width)
        return static_cast<const NetStrengthDriverPairRange *>(nullptr);
      return &*pair;
    };

    auto drivers = cache->driverBits.find(root);
    if (drivers != cache->driverBits.end()) {
      for (const NetDriverBit &driver : drivers->second) {
        if (containingPair(driver.valueOffset, true))
          continue;
        const NetStrengthDriverPairRange *pair =
            containingPair(driver.valueOffset, false);
        uint16_t strengths = strengthsFor(driver.valueOffset, driver.strength0,
                                          driver.strength1);
        if (pair) {
          uint64_t highOffset =
              pair->highOffset + (driver.valueOffset - pair->lowOffset);
          strengths = combineStrengthRanges(
              strengths, strengthsFor(highOffset, pair->highStrength0,
                                      pair->highStrength1));
        }
        contribution(strengths);
      }
    }
    auto neighbors = cache->passNeighbors.find(root);
    if (neighbors != cache->passNeighbors.end())
      for (const NetPassNeighbor &neighbor : neighbors->second) {
        if (neighbor.directed && !neighbor.receives)
          continue;
        if (neighbor.controlled) {
          auto state = cache->controlledPassStates.find(neighbor.passSwitchId);
          if (state == cache->controlledPassStates.end())
            return TALOS_RT_INVALID_DESIGN;
          if (state->second == 0)
            continue;
        }
        auto neighborMembers = cache->members.find(neighbor.root);
        if (neighborMembers == cache->members.end() ||
            neighborMembers->second.empty())
          return TALOS_RT_INVALID_DESIGN;
        contribution(strengthsFor(neighborMembers->second.front(), 6, 6));
      }
    if (auto delayed = cache->delayedMosByDestination.find(root);
        delayed != cache->delayedMosByDestination.end())
      for (uint64_t edge : delayed->second) {
        auto strengths = context->delayedMosContributions.find(edge);
        if (strengths == context->delayedMosContributions.end())
          return TALOS_RT_INVALID_DESIGN;
        contribution(strengths->second);
      }
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

extern "C" talos_rt_status
talos_rt_v1_net_count_drivers(talos_rt_context *context, uint64_t netHandle,
                                uint32_t *outForced, uint32_t *outTotal,
                                uint32_t *outZero, uint32_t *outOne,
                                uint32_t *outUnknown) {
  return talos_rt_count_design_drivers(context, netHandle, outForced,
                                         outTotal, outZero, outOne, outUnknown,
                                         true);
}

namespace talos::designbytecode {

talos_rt_status applyPassSwitchControl(talos_rt_context *context,
                                         uint32_t passSwitchId,
                                         uint8_t nextState, bool &changed) {
  if (!context || !context->designBytecodeImageValidated ||
      passSwitchId == UINT32_MAX || nextState > 2)
    return TALOS_RT_INVALID_ARGUMENT;
  NetAliasCache *cache =
      getNetAliasCache(context->designBytecodeImage, context);
  if (!cache)
    return TALOS_RT_INVALID_DESIGN;
  uint32_t encodedId = passSwitchId + 1;
  auto edges = cache->controlledPassEdges.find(encodedId);
  auto state = cache->controlledPassStates.find(encodedId);
  if (edges == cache->controlledPassEdges.end() ||
      state == cache->controlledPassStates.end())
    return TALOS_RT_INVALID_DESIGN;
  uint8_t previousState = state->second;
  if (previousState > 3)
    return TALOS_RT_INVALID_DESIGN;
  if (previousState == nextState)
    return TALOS_RT_OK;
  std::unordered_set<uint64_t> affectedComponents;
  for (const NetControlledPassEdge &edge : edges->second) {
    auto component = cache->passComponents.find(edge.component);
    if (component == cache->passComponents.end())
      return TALOS_RT_INVALID_DESIGN;
    NetPassComponent &pass = component->second;
    size_t count = pass.roots.size();
    if (edge.lhs >= count || edge.rhs >= count)
      return TALOS_RT_INVALID_DESIGN;
    size_t forward = static_cast<size_t>(edge.lhs) * count + edge.rhs;
    size_t reverse = static_cast<size_t>(edge.rhs) * count + edge.lhs;
    auto adjust = [&](std::vector<uint32_t> &counts, bool add) {
      if (add) {
        ++counts[forward];
        if (!edge.directed)
          ++counts[reverse];
      } else {
        --counts[forward];
        if (!edge.directed)
          --counts[reverse];
      }
    };
    auto &definite =
        edge.resistive ? pass.definiteResistive : pass.definiteNonresistive;
    auto &possible =
        edge.resistive ? pass.possibleResistive : pass.possibleNonresistive;
    if (previousState != 3 || nextState != 2) {
      uint8_t effectivePrevious = previousState == 3 ? 2 : previousState;
      if (effectivePrevious == 1)
        adjust(definite, false);
      if (effectivePrevious != 0)
        adjust(possible, false);
      if (nextState == 1)
        adjust(definite, true);
      if (nextState != 0)
        adjust(possible, true);
    }
    affectedComponents.insert(edge.component);
  }
  state->second = nextState;
  std::vector<uint64_t> roots;
  for (uint64_t componentId : affectedComponents) {
    NetPassComponent &component = cache->passComponents.at(componentId);
    if (!rebuildPassComponent(component))
      return TALOS_RT_INVALID_DESIGN;
    roots.insert(roots.end(), component.roots.begin(), component.roots.end());
  }
  if (!resolveNetRoots(*cache, context, std::move(roots), changed,
                       context->nativeStateValue != nullptr))
    return context->schedulerStatus == TALOS_RT_OK ? TALOS_RT_INVALID_DESIGN
                                                     : context->schedulerStatus;
  return TALOS_RT_OK;
}

talos_rt_status applyDelayedMosEvent(talos_rt_context *context,
                                       uint64_t edgeKey, uint16_t strengths,
                                       bool &changed) {
  if (!context || !context->designBytecodeImageValidated || strengths == 0 ||
      (strengths & ~uint16_t{0x7fff}) != 0)
    return TALOS_RT_INVALID_ARGUMENT;
  NetAliasCache *cache =
      getNetAliasCache(context->designBytecodeImage, context);
  if (!cache)
    return TALOS_RT_INVALID_DESIGN;
  auto edge = cache->delayedMosEdges.find(edgeKey);
  auto contribution = context->delayedMosContributions.find(edgeKey);
  if (edge == cache->delayedMosEdges.end() ||
      contribution == context->delayedMosContributions.end())
    return TALOS_RT_INVALID_DESIGN;
  if (contribution->second == strengths)
    return TALOS_RT_OK;
  contribution->second = strengths;
  if (!resolveNetRoots(*cache, context, {edge->second.destinationRoot}, changed,
                       context->nativeStateValue != nullptr))
    return context->schedulerStatus == TALOS_RT_OK ? TALOS_RT_INVALID_DESIGN
                                                     : context->schedulerStatus;
  return TALOS_RT_OK;
}

} // namespace talos::designbytecode

extern "C" talos_rt_status
talos_rt_v1_pass_switch_control(talos_rt_context *context,
                                  uint32_t passSwitchId, uint32_t value,
                                  uint32_t unknown) {
  if (!context || !context->execution || value > 1 || unknown > 1 ||
      passSwitchId == UINT32_MAX)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ContextTransaction transaction(context);
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    talos::designbytecode::Image image;
    if (!talos::designbytecode::loadValidatedImage(entry, context, image,
                                                     /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    bool changed = false;
    talos_rt_status status = talos::designbytecode::applyPassSwitchControl(
        context, passSwitchId, unknown ? 2 : static_cast<uint8_t>(value),
        changed);
    if (status != TALOS_RT_OK)
      return status;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

extern "C" talos_rt_status talos_rt_v1_pass_switch_control_delayed(
    talos_rt_context *context, uint32_t passSwitchId, uint32_t value,
    uint32_t unknown, uint64_t turnOnDelay, uint64_t turnOffDelay,
    uint64_t unknownDelay) {
  if (!context || !context->execution || value > 1 || unknown > 1 ||
      passSwitchId == UINT32_MAX)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ContextTransaction transaction(context);
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    talos::designbytecode::Image image;
    if (!talos::designbytecode::loadValidatedImage(entry, context, image,
                                                     /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NetAliasCache *cache =
        talos::designbytecode::getNetAliasCache(image, context);
    if (!cache)
      return TALOS_RT_INVALID_DESIGN;
    uint32_t encodedId = passSwitchId + 1;
    auto current = cache->controlledPassStates.find(encodedId);
    if (current == cache->controlledPassStates.end() ||
        cache->controlledPassEdges.find(encodedId) ==
            cache->controlledPassEdges.end())
      return TALOS_RT_INVALID_DESIGN;
    if (current->second == 3) {
      bool initialized = false;
      talos_rt_status status =
          talos::designbytecode::applyPassSwitchControl(context, passSwitchId,
                                                          2, initialized);
      if (status != TALOS_RT_OK)
        return status;
      if (initialized && ++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
    }
    uint8_t nextState = unknown ? 2 : static_cast<uint8_t>(value);
    auto pending = context->delayedPassSwitchPending.find(passSwitchId);
    if (pending != context->delayedPassSwitchPending.end() &&
        pending->second.state == nextState)
      return TALOS_RT_OK;
    if (pending != context->delayedPassSwitchPending.end()) {
      auto scheduled = pending->second.event;
      if (scheduled == context->scheduledPassSwitchEvents.end() ||
          scheduled->second.passSwitchID != passSwitchId)
        return TALOS_RT_INVALID_DESIGN;
      context->scheduledPassSwitchEvents.erase(scheduled);
      context->delayedPassSwitchPending.erase(pending);
    }
    if (current->second == nextState)
      return TALOS_RT_OK;
    uint64_t delay = nextState == 1   ? turnOnDelay
                     : nextState == 0 ? turnOffDelay
                                      : unknownDelay;
    if (delay == 0) {
      bool changed = false;
      talos_rt_status status =
          talos::designbytecode::applyPassSwitchControl(context, passSwitchId,
                                                          nextState, changed);
      if (status != TALOS_RT_OK)
        return status;
      if (changed && ++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
      return TALOS_RT_OK;
    }
    if (context->nextSchedulerSequence == 0 ||
        context->nextSchedulerSequence == UINT64_MAX)
      return TALOS_RT_OUT_OF_RESOURCES;
    uint64_t sequence = context->nextSchedulerSequence++;
    uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                           ? UINT64_MAX
                           : context->schedulerTime + delay;
    std::pair<uint64_t, uint64_t> key{dueTime, sequence};
    auto scheduled = context->scheduledPassSwitchEvents.emplace(
        key, ScheduledPassSwitchEvent{passSwitchId, nextState});
    if (!scheduled.second)
      return TALOS_RT_INVALID_DESIGN;
    TALOS_RT_TRY {
      if (!context->delayedPassSwitchPending
               .emplace(passSwitchId,
                        DelayedPassSwitchPending{scheduled.first, nextState})
               .second) {
        context->scheduledPassSwitchEvents.erase(scheduled.first);
        return TALOS_RT_INVALID_DESIGN;
      }
    }
    TALOS_RT_CATCH_ALL {
      context->scheduledPassSwitchEvents.erase(scheduled.first);
      TALOS_RT_RETHROW;
    }
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

extern "C" talos_rt_status
talos_rt_v1_mos_drive_delayed(talos_rt_context *context,
                                uint32_t passSwitchId, uint32_t value,
                                uint32_t unknown, uint64_t riseDelay,
                                uint64_t fallDelay, uint64_t turnoffDelay) {
  if (!context || !context->execution || value > 1 || unknown > 1 ||
      passSwitchId == UINT32_MAX)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ContextTransaction transaction(context);
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NetAliasCache *cache = getNetAliasCache(image, context);
    if (!cache)
      return TALOS_RT_INVALID_DESIGN;
    uint32_t encodedId = passSwitchId + 1;
    auto edges = cache->delayedMosByControl.find(encodedId);
    if (edges == cache->delayedMosByControl.end() || edges->second.empty())
      return TALOS_RT_INVALID_DESIGN;
    DelayedMosControl control;
    control.state = unknown ? 2 : static_cast<uint8_t>(value);
    control.delays = {riseDelay, fallDelay, turnoffDelay};
    context->delayedMosControls[encodedId] = control;
    for (uint64_t edgeKey : edges->second) {
      auto edge = cache->delayedMosEdges.find(edgeKey);
      if (edge == cache->delayedMosEdges.end())
        return TALOS_RT_INVALID_DESIGN;
      uint16_t sourceStrengths = 0;
      if (!computeResolvedStrengths(*cache, context, edge->second.sourceRoot,
                                    context->nativeStateValue != nullptr,
                                    /*useSchedulePlan=*/false,
                                    sourceStrengths) ||
          !scheduleDelayedMosEdge(context, edge->second, sourceStrengths,
                                  control))
        return context->schedulerStatus == TALOS_RT_OK
                   ? TALOS_RT_INVALID_DESIGN
                   : context->schedulerStatus;
    }
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

extern "C" uint16_t talos_rt_v1_strength_resolve_kind(uint16_t lhs,
                                                        uint16_t rhs,
                                                        uint32_t resolution) {
  return combineStrengthRanges(lhs, rhs, static_cast<uint8_t>(resolution));
}

talos_rt_status
talos_rt_initialize_design_state(talos_rt_context *context) noexcept {
  if (!context || !context->execution)
    return TALOS_RT_INVALID_ARGUMENT;
  if ((context->execution->flags & TALOS_RT_EXECUTION_HAS_BYTECODE) == 0)
    return TALOS_RT_OK;
  TALOS_RT_TRY {
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    if (context->stateValue.size() !=
            static_cast<size_t>((image.stateBitCount + 63) / 64) ||
        context->stateUnknown.size() != context->stateValue.size())
      return TALOS_RT_INVALID_DESIGN;
    for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
      CaptureRecord driver = captureAt(image, index);
      if (driver.function == kNetStateDescriptor) {
        bool fourState = (driver.argument & 1) != 0;
        for (uint64_t bitIndex = 0; bitIndex != driver.planeSize; ++bitIndex) {
          setBit(context->stateValue, driver.valueOffset + bitIndex, fourState);
          setBit(context->stateUnknown, driver.valueOffset + bitIndex,
                 fourState);
        }
      } else if (driver.function == kDriverStateDescriptor) {
        bool initialX = (driver.argument & TALOS_RT_DB_DRIVER_INITIAL_X) != 0;
        for (uint64_t bitIndex = 0; bitIndex != driver.planeSize; ++bitIndex) {
          setBit(context->stateValue, driver.valueOffset + bitIndex,
                 !context->twoState() && !initialX);
          setBit(context->stateUnknown, driver.valueOffset + bitIndex, true);
        }
      }
    }
    NetAliasCache *cache = getNetAliasCache(image, context);
    if (!cache)
      return TALOS_RT_INVALID_DESIGN;
    for (const auto &[root, resolution] : cache->resolutionByRoot) {
      if (resolution < 5)
        continue;
      for (uint64_t destination : cache->members.at(root)) {
        if (resolution == 9) {
          setBit(context->stateValue, destination, false);
          continue;
        }
        setBit(context->stateValue, destination,
               resolution == 6 || resolution == 8);
        setBit(context->stateUnknown, destination, false);
      }
    }
    bool changed = false;
    if (!resolveInitialDriverNets(image, context, false, changed))
      return context->schedulerStatus == TALOS_RT_OK
                 ? TALOS_RT_INVALID_DESIGN
                 : context->schedulerStatus;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

talos_rt_status talos_rt_resolve_design_drivers(talos_rt_context *context,
                                                    uint64_t begin,
                                                    uint64_t end) noexcept {
  if (!context || !context->execution || begin > end ||
      end > uint64_t{INT64_MAX})
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ContextTransaction transaction(context);
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    bool changed = false;
    if (!resolveDrivenNets(image, context, static_cast<int64_t>(begin),
                           static_cast<int64_t>(end), changed, true))
      return context->schedulerStatus == TALOS_RT_OK
                 ? TALOS_RT_INVALID_HANDLE
                 : context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

extern "C" talos_rt_status
talos_rt_v1_scheduler_resolve_drivers(talos_rt_context *context,
                                        uint64_t begin, uint64_t end) {
  return talos_rt_resolve_design_drivers(context, begin, end);
}

extern "C" talos_rt_status
talos_rt_v1_scheduler_resolve_initial_drivers(talos_rt_context *context) {
  if (!context || !context->execution)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ContextTransaction transaction(context);
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    bool changed = false;
    bool useNativeState = context->nativeStateValue != nullptr &&
                          context->nativeStateUnknown != nullptr;
    if (!resolveInitialDriverNets(image, context, useNativeState, changed))
      return context->schedulerStatus == TALOS_RT_OK
                 ? TALOS_RT_INVALID_DESIGN
                 : context->schedulerStatus;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

// A VPI mutation can arrive inside a native fragment, before a scheduler
// handoff imports its clean driver stores. Net release must resolve those
// current contributions (IEEE 1800-2023 10.6.2), not the previous runtime
// snapshot. Preserve already dirty roots before importing the clean planes.
static bool
synchronizeNativeNetOverrideStateUnlocked(talos_rt_context *context) {
  const auto *plan = context->nativeSchedulePlan;
  if (!plan || !(plan->flags & TALOS_RT_NATIVE_SCHEDULE_DIRECT_STATE) ||
      context->nativeScheduleDeoptimized || !plan->state_bit_count)
    return true;
  return reconcileNativeDirtyRootsToPlanesUnlocked(context, plan) &&
         importNativeStatePlanesUnlocked(context, plan->state_value,
                                         plan->state_unknown,
                                         plan->state_bit_count);
}

talos_rt_status
talos_rt_force_design_nets(talos_rt_context *context, uint64_t begin,
                             uint64_t width, const uint8_t *value,
                             const uint8_t *unknown) noexcept {
  if (!context || !context->execution || !value || width == 0 ||
      begin > UINT64_MAX - width)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ContextTransaction transaction(context);
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (!synchronizeNativeNetOverrideStateUnlocked(context))
      return TALOS_RT_INVALID_DESIGN;
    NetAliasCache *cache = getNetAliasCache(image, context);
    std::map<uint64_t, std::pair<bool, bool>> forcedRoots;
    for (uint64_t index = 0; index != width; ++index) {
      auto found = cache->rootByBit.find(begin + index);
      if (found == cache->rootByBit.end())
        return TALOS_RT_INVALID_HANDLE;
      bool nextValue =
          (value[index / 8] & static_cast<uint8_t>(1u << (index % 8))) != 0;
      bool nextUnknown =
          unknown &&
          (unknown[index / 8] & static_cast<uint8_t>(1u << (index % 8))) != 0;
      forcedRoots[found->second] = {nextValue, nextUnknown};
    }
    if (context->forceMask.empty())
      context->forceMask.assign(context->stateValue.size(), 0);
    std::vector<NetPublication> publications;
    for (const auto &[root, forced] : forcedRoots) {
      auto members = cache->members.find(root);
      if (members == cache->members.end())
        return TALOS_RT_INVALID_HANDLE;
      for (uint64_t destination : members->second) {
        bool fourState = false;
        for (const NetAliasRange &net : cache->nets)
          if (destination >= net.valueOffset &&
              destination < net.valueOffset + net.width) {
            fourState = net.fourState;
            break;
          }
        bool nextValue = forced.first;
        bool nextUnknown = fourState && forced.second;
        if (!fourState && forced.second)
          nextValue = false;
        publications.push_back(
            {destination, bit(context->stateValue, destination),
             bit(context->stateUnknown, destination), nextValue, nextUnknown});
        context->forceMask[destination / 64] |= uint64_t{1}
                                                << (destination % 64);
        // Every alias of a forced net must leave the clean native path, even
        // when forcing the value already present produces no transition.
        talos_rt_aot_external_write_range_unlocked(context, destination, 1,
                                                     true);
      }
    }
    bool changed = false;
    if (!publishNetBits(context, *cache, publications, changed))
      return context->schedulerStatus;
    std::vector<uint64_t> roots;
    roots.reserve(forcedRoots.size());
    for (const auto &[root, ignored] : forcedRoots)
      roots.push_back(root);
    // Generic native fragments also update driver planes directly. Driver
    // writes have no signal publication to mirror them into bytecode storage;
    // resolve from the bound authoritative planes at this foreign boundary.
    if (!resolveNetRoots(*cache, context, std::move(roots), changed,
                         /*useNativeState=*/context->nativeStateValue !=
                                 nullptr ||
                             !cache->passComponents.empty()))
      return context->schedulerStatus == TALOS_RT_OK
                 ? TALOS_RT_INVALID_HANDLE
                 : context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

uint64_t
talos_rt_canonical_net_bit_unlocked(const talos_rt_context *context,
                                      uint64_t bit) noexcept {
  auto found = context->netAliases.rootByBit.find(bit);
  return found == context->netAliases.rootByBit.end() ? bit : found->second;
}

talos_rt_status talos_rt_release_design_nets(talos_rt_context *context,
                                                 uint64_t begin,
                                                 uint64_t width) noexcept {
  if (!context || !context->execution || width == 0 ||
      begin > UINT64_MAX - width)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ContextTransaction transaction(context);
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (!synchronizeNativeNetOverrideStateUnlocked(context))
      return TALOS_RT_INVALID_DESIGN;
    NetAliasCache *cache = getNetAliasCache(image, context);
    std::vector<uint64_t> roots;
    for (uint64_t index = 0; index != width; ++index) {
      auto found = cache->rootByBit.find(begin + index);
      if (found == cache->rootByBit.end())
        return TALOS_RT_INVALID_HANDLE;
      roots.push_back(found->second);
    }
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    std::vector<uint64_t> releasedBits;
    for (uint64_t root : roots) {
      auto members = cache->members.find(root);
      if (members == cache->members.end())
        return TALOS_RT_INVALID_HANDLE;
      for (uint64_t destination : members->second)
        if (destination / 64 < context->forceMask.size()) {
          context->forceMask[destination / 64] &=
              ~(uint64_t{1} << (destination % 64));
          releasedBits.push_back(destination);
        }
    }
    bool changed = false;
    if (!resolveNetRoots(*cache, context, std::move(roots), changed,
                         /*useNativeState=*/context->nativeStateValue !=
                                 nullptr ||
                             !cache->passComponents.empty()))
      return context->schedulerStatus == TALOS_RT_OK
                 ? TALOS_RT_INVALID_HANDLE
                 : context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    // Resolve first: re-promotion must see the released driver's value, not
    // the previous forced value. Clear all aliases before checking each root.
    for (uint64_t destination : releasedBits)
      talos_rt_aot_release_range_unlocked(context, destination, 1);
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

talos_rt_status
talos_rt_design_net_is_connected(talos_rt_context *context, uint64_t begin,
                                   uint64_t end, bool *outConnected) noexcept {
  if (!context || !context->execution || !outConnected || begin > end)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image, /*inventoryOnly=*/true))
      return TALOS_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NetAliasCache *cache = getNetAliasCache(image, context);
    *outConnected = false;
    for (uint64_t bitIndex = begin; bitIndex != end; ++bitIndex) {
      auto root = cache->rootByBit.find(bitIndex);
      if (root != cache->rootByBit.end()) {
        auto members = cache->members.find(root->second);
        if (members != cache->members.end() && members->second.size() > 1) {
          *outConnected = true;
          break;
        }
      }
      auto drivers = root == cache->rootByBit.end()
                         ? cache->driverBits.end()
                         : cache->driverBits.find(root->second);
      if (drivers != cache->driverBits.end() && !drivers->second.empty()) {
        *outConnected = true;
        break;
      }
    }
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}
