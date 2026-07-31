//===- DesignBytecodeNets.cpp - Bytecode net resolution -----------------===//

#include "DesignBytecodeNets.h"
#include "DesignBytecodeLogic.h"
#include "RuntimeInternal.h"

#include <algorithm>
#include <map>
#include <new>
#include <unordered_map>

namespace obelisk::designbytecode {

bool appendSignalEvent(obelisk_rt_context *context, uint64_t bitOffset,
                       bool oldValue, bool oldUnknown, bool newValue,
                       bool newUnknown, bool evaluateComputedObservers = true) {
  return obelisk_rt_append_signal_event_unlocked(
      context, bitOffset, oldValue, oldUnknown, newValue, newUnknown,
      evaluateComputedObservers);
}

NetAliasCache *getNetAliasCache(const Image &image,
                                obelisk_rt_context *context) {
  if (context->netAliases.execution == context->execution)
    return &context->netAliases;
  NetAliasCache cache;
  cache.execution = context->execution;
  std::vector<CaptureRecord> nets;
  std::vector<CaptureRecord> drivers;
  for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
    CaptureRecord record = captureAt(image, index);
    if (record.function == kNetStateDescriptor) {
      nets.push_back(record);
      cache.nets.push_back({record.valueOffset, record.valueOffset,
                            record.planeSize, (record.argument & 1) != 0});
    } else if (record.function == kDriverStateDescriptor) {
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
  for (uint64_t index = 0; index != image.connectivityCount; ++index) {
    ConnectivityRecord connection = connectivityAt(image, index);
    for (uint64_t bitIndex = 0; bitIndex != connection.width; ++bitIndex) {
      uint64_t lhs = connection.lhsOffset + bitIndex;
      uint64_t rhs = (connection.flags & 1) ? connection.rhsOffset - bitIndex
                                            : connection.rhsOffset + bitIndex;
      uint64_t lhsRoot = findRoot(lhs);
      uint64_t rhsRoot = findRoot(rhs);
      if (lhsRoot != rhsRoot)
        parents[std::max(lhsRoot, rhsRoot)] = std::min(lhsRoot, rhsRoot);
    }
  }

  for (const CaptureRecord &net : nets)
    for (uint64_t bit = 0; bit != net.planeSize; ++bit) {
      uint64_t logicalBit = net.valueOffset + bit;
      uint64_t root = findRoot(logicalBit);
      cache.rootByBit.emplace(logicalBit, root);
      cache.members[root].push_back(logicalBit);
    }
  for (const CaptureRecord &driver : drivers)
    for (uint64_t bit = 0; bit != driver.planeSize; ++bit)
      cache.driverBits[findRoot(driver.unknownOffset + bit)].push_back(
          driver.valueOffset + bit);
  for (auto &[root, component] : cache.members) {
    std::sort(component.begin(), component.end());
    component.erase(std::unique(component.begin(), component.end()),
                    component.end());
  }
  context->netAliases = std::move(cache);
  return &context->netAliases;
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
    uint64_t signalHandle = publication.destination;
    uint64_t publicationHandle = publication.destination;
    uint64_t publicationWidth = 1;
    uint32_t chosenID = UINT32_MAX;
    for (const auto &[id, state] : context->nativeStaticStates)
      if (publication.destination >= state.bitOffset &&
          publication.destination < state.bitOffset + state.bitWidth &&
          id < chosenID) {
        chosenID = id;
        signalHandle = encodeStaticHandle(
            id,
            static_cast<int64_t>(publication.destination - state.bitOffset));
        publicationHandle = encodeStaticHandle(id, 0);
        publicationWidth = state.bitWidth;
      }
    if (chosenID == UINT32_MAX)
      for (const NetAliasRange &net : cache.nets)
        if (publication.destination >= net.valueOffset &&
            publication.destination < net.valueOffset + net.width) {
          publicationHandle = net.valueOffset;
          publicationWidth = net.width;
          break;
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

bool resolveNetRoots(const NetAliasCache &cache, obelisk_rt_context *context,
                     std::vector<uint64_t> affectedRoots, bool &changed) {
  if (affectedRoots.empty())
    return false;
  std::sort(affectedRoots.begin(), affectedRoots.end());
  affectedRoots.erase(std::unique(affectedRoots.begin(), affectedRoots.end()),
                      affectedRoots.end());
  std::vector<NetPublication> publications;
  for (uint64_t root : affectedRoots) {
    auto members = cache.members.find(root);
    if (members == cache.members.end())
      return false;
    bool resolvedValue = true;
    bool resolvedUnknown = true;
    auto componentDrivers = cache.driverBits.find(root);
    if (componentDrivers != cache.driverBits.end()) {
      for (uint64_t driverBit : componentDrivers->second) {
        bool driverValue = bit(context->stateValue, driverBit);
        bool driverUnknown = bit(context->stateUnknown, driverBit);
        bool currentZ = resolvedUnknown && resolvedValue;
        bool driverZ = driverUnknown && driverValue;
        bool currentX = resolvedUnknown && !resolvedValue;
        bool driverX = driverUnknown && !driverValue;
        bool conflict = currentX || driverX || resolvedValue != driverValue;
        bool mergedValue = conflict ? false : resolvedValue;
        bool mergedUnknown = conflict;
        bool withoutCurrentZ = driverZ ? resolvedValue : mergedValue;
        bool withoutCurrentZUnknown = driverZ ? resolvedUnknown : mergedUnknown;
        resolvedValue = currentZ ? driverValue : withoutCurrentZ;
        resolvedUnknown = currentZ ? driverUnknown : withoutCurrentZUnknown;
      }
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
      uint64_t mask = uint64_t{1} << (destination % 64);
      bool forced = destination / 64 < context->forceMask.size() &&
                    (context->forceMask[destination / 64] & mask) != 0;
      bool assigned = destination / 64 < context->assignMask.size() &&
                      (context->assignMask[destination / 64] & mask) != 0;
      if (forced || assigned) {
        publishValue = bit(context->stateValue, destination);
        publishUnknown = bit(context->stateUnknown, destination);
      }
      publications.push_back({destination,
                              bit(context->stateValue, destination),
                              bit(context->stateUnknown, destination),
                              publishValue, publishUnknown});
    }
  }
  return publishNetBits(context, cache, publications, changed);
}

bool resolveDrivenNets(const Image &image, obelisk_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd,
                       bool &changed) {
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
  return resolveNetRoots(*cache, context, std::move(affectedRoots), changed);
}

} // namespace obelisk::designbytecode

using namespace obelisk::designbytecode;

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
                           static_cast<int64_t>(end), changed))
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
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
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
    if (!resolveNetRoots(*cache, context, std::move(roots), changed))
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
