//===- DesignBytecodeNets.cpp - Bytecode net resolution -----------------===//

#include "DesignBytecodeNets.h"
#include "DesignBytecodeLogic.h"
#include "ProcessPacking.h"
#include "ProcessShared.h"
#include "RuntimeInternal.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <map>
#include <new>
#include <unordered_map>
#include <unordered_set>

namespace obelisk::designbytecode {

static constexpr unsigned strengthCount = 15;

static uint16_t strengthBit(int strength) {
  return static_cast<uint16_t>(uint16_t{1} << (strength + 7));
}

static uint16_t implicitNetStrength(uint8_t resolution) {
  switch (resolution) {
  case 5:
    return strengthBit(-5); // tri0
  case 6:
    return strengthBit(5); // tri1
  case 7:
    return strengthBit(-7); // supply0
  case 8:
    return strengthBit(7); // supply1
  default:
    return strengthBit(0); // high impedance
  }
}

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

static uint16_t combineStrengthRanges(uint16_t lhs, uint16_t rhs,
                                      uint8_t resolution = 0) {
  uint16_t result = 0;
  for (unsigned lhsIndex = 0; lhsIndex != strengthCount; ++lhsIndex) {
    if ((lhs & (uint16_t{1} << lhsIndex)) == 0)
      continue;
    int left = static_cast<int>(lhsIndex) - 7;
    for (unsigned rhsIndex = 0; rhsIndex != strengthCount; ++rhsIndex) {
      if ((rhs & (uint16_t{1} << rhsIndex)) == 0)
        continue;
      int right = static_cast<int>(rhsIndex) - 7;
      if (left == 0 || right == 0) {
        result |= strengthBit(left == 0 ? right : left);
        continue;
      }
      if ((left < 0) == (right < 0)) {
        result |= strengthBit(std::abs(left) >= std::abs(right) ? left : right);
        continue;
      }
      if (std::abs(left) != std::abs(right)) {
        result |= strengthBit(std::abs(left) > std::abs(right) ? left : right);
        continue;
      }
      // IEEE 1800-2017 28.12.4 applies wired logic to equal-strength
      // conflicts. wand/triand select 0; wor/trior select 1.
      if (resolution == 3 || resolution == 4) {
        int magnitude = std::abs(left);
        result |= strengthBit(resolution == 3 ? -magnitude : magnitude);
        continue;
      }
      for (int strength = -std::abs(left); strength <= std::abs(left);
           ++strength)
        result |= strengthBit(strength);
    }
  }
  // Ambiguous strengths are ranges, not sparse sets (28.12.2). Preserve that
  // invariant so a later driver observes every intermediate strength level.
  unsigned first = 0;
  while (first != strengthCount && (result & (uint16_t{1} << first)) == 0)
    ++first;
  unsigned last = strengthCount;
  while (last != first && (result & (uint16_t{1} << (last - 1))) == 0)
    --last;
  uint16_t range = 0;
  for (unsigned index = first; index != last; ++index)
    range |= uint16_t{1} << index;
  return range;
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

bool appendSignalEvent(obelisk_rt_context *context, uint64_t bitOffset,
                       bool oldValue, bool oldUnknown, bool newValue,
                       bool newUnknown, bool evaluateComputedObservers = true) {
  return obelisk_rt_append_signal_event_unlocked(
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
  return true;
}

NetAliasCache *getNetAliasCache(const Image &image,
                                obelisk_rt_context *context) {
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
      cache.passNeighbors[lhsRoot].push_back(
          {rhsRoot, connection.tailReserved, resistive, controlled});
      cache.passNeighbors[rhsRoot].push_back(
          {lhsRoot, connection.tailReserved, resistive, controlled});
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
        if (lhs >= rhs)
          continue;
        size_t forward = static_cast<size_t>(lhs) * roots.size() + rhs;
        size_t reverse = static_cast<size_t>(rhs) * roots.size() + lhs;
        if (edge.controlled) {
          cache.controlledPassStates.try_emplace(edge.passSwitchId, 0);
          cache.controlledPassEdges[edge.passSwitchId].push_back(
              {component, lhs, rhs, edge.resistive});
          continue;
        }
        auto &definite = edge.resistive
                             ? passComponent.definiteResistive
                             : passComponent.definiteNonresistive;
        auto &possible = edge.resistive
                             ? passComponent.possibleResistive
                             : passComponent.possibleNonresistive;
        ++definite[forward];
        ++definite[reverse];
        ++possible[forward];
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

bool isComplementaryDriverPair(const Image &image, obelisk_rt_context *context,
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

bool publishNetBits(obelisk_rt_context *context, const NetAliasCache &cache,
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
    obelisk_rt_sync_native_state_range_unlocked(context,
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
    uint64_t signalHandle = obelisk_rt_canonical_state_handle_unlocked(
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
    obelisk_rt_invalidate_signal_snapshots_unlocked(context, signalHandle, 1);
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
    if (!obelisk_rt_notify_observer_signal_unlocked(
            context, routed[groupBegin].observerHandle,
            routed[groupBegin].observerWidth))
      return false;
    groupBegin = groupEnd;
  }
  return true;
}

void cancelNetBit(obelisk_rt_context *context, uint64_t destination,
                  bool preserveInitialProjection = false) {
  // Resolution can run while the scheduler is applying a propagation-
  // delayed driver from this same vector. Marking the superseded net event
  // avoids invalidating the scheduler's current reference; barrier scans
  // ignore the tombstone and a later compaction removes it.
  if (context->schedulerApplyingNativeUpdate) {
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

void cancelNetGroup(obelisk_rt_context *context, uint64_t group) {
  std::vector<uint64_t> destinations;
  for (ScheduledNBA &update : context->scheduledNBAs) {
    if (update.inertialNetCancelGroup != group)
      continue;
    destinations.push_back(update.inertialNetBit);
    if (context->schedulerApplyingNativeUpdate)
      update.cancelled = true;
  }
  if (!context->schedulerApplyingNativeUpdate)
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

bool scheduleNetBit(obelisk_rt_context *context, uint64_t root,
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
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint64_t handle =
      obelisk_rt_canonical_state_handle_unlocked(context, destination, 1);
  if (handle == UINT64_MAX) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
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
  update.execRegion = OBELISK_RT_REGION_ACTIVE;
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

static bool computeResolvedStrengths(const NetAliasCache &cache,
                                     obelisk_rt_context *context, uint64_t root,
                                     bool useNativeState,
                                     bool useSchedulePlan,
                                     uint16_t &resolvedStrengths) {
  auto members = cache.members.find(root);
  if (members == cache.members.end())
    return false;
  auto driverStrengths = [&](bool value, bool unknown, uint8_t strength0,
                             uint8_t strength1) {
    if (!unknown)
      return strengthBit(value ? strength1 : -static_cast<int>(strength0));
    if (value)
      return strengthBit(0);
    uint16_t result = 0;
    for (int strength = -static_cast<int>(strength0);
         strength <= static_cast<int>(strength1); ++strength)
      result |= strengthBit(strength);
    return result;
  };
  const uint8_t *nativeValue = nullptr;
  const uint8_t *nativeUnknown = nullptr;
  uint64_t nativeBits = 0;
  if (useSchedulePlan && context->nativeSchedulePlan && context->execution &&
        context->nativeSchedulePlan->state_bit_count ==
            context->execution->state_bit_count) {
    nativeValue = context->nativeSchedulePlan->state_value;
    nativeUnknown = context->nativeSchedulePlan->state_unknown;
    nativeBits = context->nativeSchedulePlan->state_bit_count;
  } else if (useNativeState && context->execution &&
             context->nativeStateBitCount ==
                 context->execution->state_bit_count) {
    nativeValue = context->nativeStateValue;
    nativeUnknown = context->nativeStateUnknown;
    nativeBits = context->nativeStateBitCount;
  }
  auto stateBit = [&](bool unknownPlane, uint64_t offset) {
    const uint8_t *native = unknownPlane ? nativeUnknown : nativeValue;
    if (native && offset < nativeBits)
      return (native[offset / 8] & static_cast<uint8_t>(1u << (offset % 8))) !=
             0;
    return bit(unknownPlane ? context->stateUnknown : context->stateValue,
               offset);
  };
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
      if (remote) {
        uint16_t possible =
            propagateThroughPass(strengths, possibleResistiveEdges);
        if (resistiveEdges == 5)
          strengths = possible | strengthBit(0);
        else {
          strengths = propagateThroughPass(strengths, resistiveEdges);
          if (possibleResistiveEdges < resistiveEdges)
            strengths |= possible;
        }
      }
      resolvedStrengths =
          combineStrengthRanges(resolvedStrengths, strengths, resolution);
      return true;
    }
    uint16_t implicit =
        implicitNetStrength(cache.resolutionByRoot.at(sourceRoot));
    if (remote) {
      uint16_t possible =
          propagateThroughPass(implicit, possibleResistiveEdges);
      if (resistiveEdges == 5)
        implicit = possible | strengthBit(0);
      else {
        implicit = propagateThroughPass(implicit, resistiveEdges);
        if (possibleResistiveEdges < resistiveEdges)
          implicit |= possible;
      }
    }
    resolvedStrengths =
        combineStrengthRanges(resolvedStrengths, implicit, resolution);
    auto componentDrivers = cache.driverBits.find(sourceRoot);
    if (componentDrivers == cache.driverBits.end())
      return true;
    for (const NetDriverBit &driver : componentDrivers->second) {
      bool driverValue = stateBit(false, driver.valueOffset);
      bool driverUnknown = stateBit(true, driver.valueOffset);
      uint16_t strengths = driverStrengths(driverValue, driverUnknown,
                                           driver.strength0, driver.strength1);
      if (remote) {
        uint16_t possible =
            propagateThroughPass(strengths, possibleResistiveEdges);
        if (resistiveEdges == 5)
          strengths = possible | strengthBit(0);
        else {
          strengths = propagateThroughPass(strengths, resistiveEdges);
          if (possibleResistiveEdges < resistiveEdges)
            strengths |= possible;
        }
      }
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
            roots.size() * roots.size())
      return false;
    size_t reductionOffset =
        static_cast<size_t>(target - roots.begin()) * roots.size();
    for (size_t index = 0; index != roots.size(); ++index)
      if (!accumulateRoot(
              roots[index], passComponent.reductions[reductionOffset + index],
              passComponent
                  .possibleReductions[reductionOffset + index]))
        return false;
  } else if (!accumulateRoot(root, 0, 0)) {
    return false;
  }
  return true;
}

bool resolveNetRoots(const NetAliasCache &cache, obelisk_rt_context *context,
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
                                  /*useSchedulePlan=*/false,
                                  resolvedStrengths))
      return false;
    bool hasZero = (resolvedStrengths & strengthBit(0)) != 0;
    constexpr uint16_t negativeMask = (uint16_t{1} << 7) - 1;
    constexpr uint16_t positiveMask = static_cast<uint16_t>(
        ((uint16_t{1} << strengthCount) - 1) & ~((uint16_t{1} << 8) - 1));
    bool hasNegative = (resolvedStrengths & negativeMask) != 0;
    bool hasPositive = (resolvedStrengths & positiveMask) != 0;
    bool resolvedZ = hasZero && !hasNegative && !hasPositive;
    bool resolvedUnknown =
        resolvedZ || (hasNegative + hasZero + hasPositive) != 1;
    bool resolvedValue = resolvedZ || (!resolvedUnknown && hasPositive);
    bool chargeValue = false;
    bool chargeUnknown = true;
    if (resolution == 9 && resolvedZ) {
      // IEEE 1800-2017 6.6.4 and 28.16: once every active driver is Z,
      // resolve the stored charges on the component at their declared
      // small/medium/large strengths. This is charge sharing; unlike an
      // active driver, a weaker stored charge can lose to a stronger one.
      uint16_t chargeStrengths = strengthBit(0);
      for (uint64_t member : members->second) {
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
        chargeStrengths = combineStrengthRanges(chargeStrengths, stored);
      }
      bool chargeHasZero = (chargeStrengths & strengthBit(0)) != 0;
      bool chargeHasNegative = (chargeStrengths & negativeMask) != 0;
      bool chargeHasPositive = (chargeStrengths & positiveMask) != 0;
      bool chargeKnownZero =
          chargeHasNegative && !chargeHasZero && !chargeHasPositive;
      bool chargeKnownOne =
          chargeHasPositive && !chargeHasNegative && !chargeHasZero;
      chargeUnknown = !(chargeKnownZero || chargeKnownOne);
      chargeValue = chargeKnownOne;
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
                  obelisk::designbytecode::bit(context->stateValue,
                                               net->valueOffset + bit) &&
                  obelisk::designbytecode::bit(context->stateUnknown,
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

bool resolveNetRoots(const NetAliasCache &cache, obelisk_rt_context *context,
                     std::vector<uint64_t> affectedRoots, bool &changed) {
  return resolveNetRoots(cache, context, std::move(affectedRoots), changed,
                         false);
}

bool resolveDrivenNets(const Image &image, obelisk_rt_context *context,
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

bool resolveDrivenNets(const Image &image, obelisk_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd,
                       bool &changed) {
  return resolveDrivenNets(image, context, changedBegin, changedEnd, changed,
                           false);
}

} // namespace obelisk::designbytecode

using namespace obelisk::designbytecode;

extern "C" uint16_t obelisk_rt_v1_strength_resolve(uint16_t lhs, uint16_t rhs) {
  return combineStrengthRanges(lhs, rhs);
}

obelisk_rt_status
obelisk_rt_design_net_strength(obelisk_rt_context *context, uint64_t netHandle,
                               uint16_t *outStrengths,
                               bool useNativeState) noexcept {
  if (!context || !context->execution || !outStrengths)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outStrengths = 0;
  try {
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return OBELISK_RT_INVALID_BYTECODE;
    ContextTransaction transaction(context);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);

    int64_t coordinate = 0;
    uint64_t absolute = 0;
    uint32_t staticID = 0;
    if (decodeNativeStatic(netHandle, staticID, coordinate)) {
      const NativeStaticState *state = findNativeStaticState(context, staticID);
      if (!state || coordinate < 0 ||
          static_cast<uint64_t>(coordinate) >= state->bitWidth)
        return OBELISK_RT_INVALID_HANDLE;
      absolute = state->bitOffset + static_cast<uint64_t>(coordinate);
    } else if (decodeNativeGlobal(netHandle, coordinate)) {
      if (coordinate < 0)
        return OBELISK_RT_INVALID_HANDLE;
      absolute = static_cast<uint64_t>(coordinate);
    } else {
      return OBELISK_RT_INVALID_HANDLE;
    }

    NetAliasCache *cache = getNetAliasCache(image, context);
    auto root = cache->rootByBit.find(absolute);
    if (root == cache->rootByBit.end())
      return OBELISK_RT_INVALID_HANDLE;
    if (!computeResolvedStrengths(*cache, context, root->second, useNativeState,
                                  /*useSchedulePlan=*/useNativeState,
                                  *outStrengths))
      return OBELISK_RT_INVALID_HANDLE;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status obelisk_rt_count_design_drivers(
    obelisk_rt_context *context, uint64_t netHandle, uint32_t *outForced,
    uint32_t *outTotal, uint32_t *outZero, uint32_t *outOne,
    uint32_t *outUnknown, bool useNativeState) noexcept {
  if (!context || !context->execution || !outForced || !outTotal || !outZero ||
      !outOne || !outUnknown)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outForced = *outTotal = *outZero = *outOne = *outUnknown = 0;
  try {
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return OBELISK_RT_INVALID_BYTECODE;
    ContextTransaction transaction(context);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);

    int64_t coordinate = 0;
    uint64_t absolute = 0;
    uint32_t staticID = 0;
    if (decodeNativeStatic(netHandle, staticID, coordinate)) {
      const NativeStaticState *state = findNativeStaticState(context, staticID);
      if (!state || coordinate < 0 ||
          static_cast<uint64_t>(coordinate) >= state->bitWidth)
        return OBELISK_RT_INVALID_HANDLE;
      absolute = state->bitOffset + static_cast<uint64_t>(coordinate);
    } else if (decodeNativeGlobal(netHandle, coordinate)) {
      if (coordinate < 0)
        return OBELISK_RT_INVALID_HANDLE;
      absolute = static_cast<uint64_t>(coordinate);
    } else {
      return OBELISK_RT_INVALID_HANDLE;
    }

    NetAliasCache *cache = getNetAliasCache(image, context);
    auto rootFound = cache->rootByBit.find(absolute);
    if (rootFound == cache->rootByBit.end())
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t root = rootFound->second;
    auto members = cache->members.find(root);
    if (members == cache->members.end())
      return OBELISK_RT_INVALID_HANDLE;
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
        if (neighbor.controlled) {
          auto state = cache->controlledPassStates.find(neighbor.passSwitchId);
          if (state == cache->controlledPassStates.end())
            return OBELISK_RT_INVALID_DESIGN;
          if (state->second == 0)
            continue;
        }
        auto neighborMembers = cache->members.find(neighbor.root);
        if (neighborMembers == cache->members.end() ||
            neighborMembers->second.empty())
          return OBELISK_RT_INVALID_DESIGN;
        contribution(strengthsFor(neighborMembers->second.front(), 6, 6));
      }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_net_count_drivers(obelisk_rt_context *context, uint64_t netHandle,
                                uint32_t *outForced, uint32_t *outTotal,
                                uint32_t *outZero, uint32_t *outOne,
                                uint32_t *outUnknown) {
  return obelisk_rt_count_design_drivers(context, netHandle, outForced,
                                         outTotal, outZero, outOne, outUnknown,
                                         true);
}

extern "C" obelisk_rt_status obelisk_rt_v1_pass_switch_control(
    obelisk_rt_context *context, uint32_t passSwitchId, uint32_t value,
    uint32_t unknown) {
  if (!context || !context->execution || value > 1 || unknown > 1 ||
      passSwitchId == UINT32_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ContextTransaction transaction(context);
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return OBELISK_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NetAliasCache *cache = getNetAliasCache(image, context);
    if (!cache)
      return OBELISK_RT_INVALID_DESIGN;
    uint32_t encodedId = passSwitchId + 1;
    auto edges = cache->controlledPassEdges.find(encodedId);
    auto state = cache->controlledPassStates.find(encodedId);
    if (edges == cache->controlledPassEdges.end() ||
        state == cache->controlledPassStates.end())
      return OBELISK_RT_INVALID_DESIGN;
    uint8_t nextState = unknown ? 2 : static_cast<uint8_t>(value);
    uint8_t previousState = state->second;
    if (previousState == nextState)
      return OBELISK_RT_OK;
    std::unordered_set<uint64_t> affectedComponents;
    for (const NetControlledPassEdge &edge : edges->second) {
      auto component = cache->passComponents.find(edge.component);
      if (component == cache->passComponents.end())
        return OBELISK_RT_INVALID_DESIGN;
      NetPassComponent &pass = component->second;
      size_t count = pass.roots.size();
      if (edge.lhs >= count || edge.rhs >= count)
        return OBELISK_RT_INVALID_DESIGN;
      size_t forward = static_cast<size_t>(edge.lhs) * count + edge.rhs;
      size_t reverse = static_cast<size_t>(edge.rhs) * count + edge.lhs;
      auto adjust = [&](std::vector<uint32_t> &counts, bool add) {
        if (add) {
          ++counts[forward];
          ++counts[reverse];
        } else {
          --counts[forward];
          --counts[reverse];
        }
      };
      auto &definite = edge.resistive ? pass.definiteResistive
                                      : pass.definiteNonresistive;
      auto &possible = edge.resistive ? pass.possibleResistive
                                      : pass.possibleNonresistive;
      if (previousState == 1)
        adjust(definite, false);
      if (previousState != 0)
        adjust(possible, false);
      if (nextState == 1)
        adjust(definite, true);
      if (nextState != 0)
        adjust(possible, true);
      affectedComponents.insert(edge.component);
    }
    state->second = nextState;
    std::vector<uint64_t> roots;
    for (uint64_t componentId : affectedComponents) {
      NetPassComponent &component = cache->passComponents.at(componentId);
      if (!rebuildPassComponent(component))
        return OBELISK_RT_INVALID_DESIGN;
      roots.insert(roots.end(), component.roots.begin(), component.roots.end());
    }
    bool changed = false;
    if (!resolveNetRoots(*cache, context, std::move(roots), changed,
                         context->nativeStateValue != nullptr))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_DESIGN
                 : context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

extern "C" uint16_t obelisk_rt_v1_strength_resolve_kind(uint16_t lhs,
                                                        uint16_t rhs,
                                                        uint32_t resolution) {
  return combineStrengthRanges(lhs, rhs, static_cast<uint8_t>(resolution));
}

obelisk_rt_status
obelisk_rt_initialize_design_state(obelisk_rt_context *context) noexcept {
  if (!context || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  if ((context->execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0)
    return OBELISK_RT_OK;
  try {
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return OBELISK_RT_INVALID_BYTECODE;
    if (context->stateValue.size() !=
            static_cast<size_t>((image.stateBitCount + 63) / 64) ||
        context->stateUnknown.size() != context->stateValue.size())
      return OBELISK_RT_INVALID_DESIGN;
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
        for (uint64_t bitIndex = 0; bitIndex != driver.planeSize; ++bitIndex) {
          setBit(context->stateValue, driver.valueOffset + bitIndex, true);
          setBit(context->stateUnknown, driver.valueOffset + bitIndex, true);
        }
      }
    }
    NetAliasCache *cache = getNetAliasCache(image, context);
    if (!cache)
      return OBELISK_RT_INVALID_DESIGN;
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
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status obelisk_rt_resolve_design_drivers(obelisk_rt_context *context,
                                                    uint64_t begin,
                                                    uint64_t end) noexcept {
  if (!context || !context->execution || begin > end ||
      end > uint64_t{INT64_MAX})
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ContextTransaction transaction(context);
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return OBELISK_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    bool changed = false;
    if (!resolveDrivenNets(image, context, static_cast<int64_t>(begin),
                           static_cast<int64_t>(end), changed, true))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_HANDLE
                 : context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_resolve_drivers(obelisk_rt_context *context,
                                        uint64_t begin, uint64_t end) {
  return obelisk_rt_resolve_design_drivers(context, begin, end);
}

obelisk_rt_status
obelisk_rt_force_design_nets(obelisk_rt_context *context, uint64_t begin,
                             uint64_t width, const uint8_t *value,
                             const uint8_t *unknown) noexcept {
  if (!context || !context->execution || !value || width == 0 ||
      begin > UINT64_MAX - width)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ContextTransaction transaction(context);
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return OBELISK_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NetAliasCache *cache = getNetAliasCache(image, context);
    std::map<uint64_t, std::pair<bool, bool>> forcedRoots;
    for (uint64_t index = 0; index != width; ++index) {
      auto found = cache->rootByBit.find(begin + index);
      if (found == cache->rootByBit.end())
        return OBELISK_RT_INVALID_HANDLE;
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
        return OBELISK_RT_INVALID_HANDLE;
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
      }
    }
    bool changed = false;
    if (!publishNetBits(context, *cache, publications, changed))
      return context->schedulerStatus;
    std::vector<uint64_t> roots;
    roots.reserve(forcedRoots.size());
    for (const auto &[root, ignored] : forcedRoots)
      roots.push_back(root);
    if (!resolveNetRoots(*cache, context, std::move(roots), changed,
                         /*useNativeState=*/!cache->passComponents.empty()))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_HANDLE
                 : context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

uint64_t
obelisk_rt_canonical_net_bit_unlocked(const obelisk_rt_context *context,
                                      uint64_t bit) noexcept {
  auto found = context->netAliases.rootByBit.find(bit);
  return found == context->netAliases.rootByBit.end() ? bit : found->second;
}

obelisk_rt_status obelisk_rt_release_design_nets(obelisk_rt_context *context,
                                                 uint64_t begin,
                                                 uint64_t width) noexcept {
  if (!context || !context->execution || width == 0 ||
      begin > UINT64_MAX - width)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ContextTransaction transaction(context);
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return OBELISK_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NetAliasCache *cache = getNetAliasCache(image, context);
    std::vector<uint64_t> roots;
    for (uint64_t index = 0; index != width; ++index) {
      auto found = cache->rootByBit.find(begin + index);
      if (found == cache->rootByBit.end())
        return OBELISK_RT_INVALID_HANDLE;
      roots.push_back(found->second);
    }
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    for (uint64_t root : roots) {
      auto members = cache->members.find(root);
      if (members == cache->members.end())
        return OBELISK_RT_INVALID_HANDLE;
      for (uint64_t destination : members->second)
        if (destination / 64 < context->forceMask.size())
          context->forceMask[destination / 64] &=
              ~(uint64_t{1} << (destination % 64));
    }
    bool changed = false;
    if (!resolveNetRoots(*cache, context, std::move(roots), changed,
                         /*useNativeState=*/!cache->passComponents.empty()))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_HANDLE
                 : context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status
obelisk_rt_design_net_is_connected(obelisk_rt_context *context, uint64_t begin,
                                   uint64_t end, bool *outConnected) noexcept {
  if (!context || !context->execution || !outConnected || begin > end)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return OBELISK_RT_INVALID_BYTECODE;
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
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}
