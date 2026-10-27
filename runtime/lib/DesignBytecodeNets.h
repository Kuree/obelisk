//===- DesignBytecodeNets.h - Bytecode net resolution ----------*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_DESIGNBYTECODENETS_H
#define TALOS_RUNTIME_LIB_DESIGNBYTECODENETS_H

#include "DesignBytecodeImage.h"
#include "SignalSemantics.h"

#include <cstdint>
#include <vector>

struct NetAliasCache;
struct talos_rt_context;

talos_rt_status talos_rt_count_design_drivers(
    talos_rt_context *context, uint64_t netHandle, uint32_t *outForced,
    uint32_t *outTotal, uint32_t *outZero, uint32_t *outOne,
    uint32_t *outUnknown, bool useNativeState) noexcept;

talos_rt_status talos_rt_design_net_strength(talos_rt_context *context,
                                                 uint64_t netHandle,
                                                 uint16_t *outStrengths,
                                                 bool useNativeState) noexcept;

// Read one logical driver's exact published strength range. highOffset is
// UINT64_MAX for a single driver, or the matching bit in the explicitly
// validated complementary bank. Net resolution, force, and other drivers do
// not contribute. This query does not mutate scheduler or state ownership.
talos_rt_status talos_rt_design_driver_strength(
    talos_rt_context *context, uint64_t lowOffset, uint64_t highOffset,
    uint16_t *outStrengths, bool useNativeState) noexcept;

namespace talos::designbytecode {

using talos::runtime::rangesOverlap;
using talos::runtime::signalEdgeMatches;
using talos::runtime::transitionEdges;

struct NetPublication {
  uint64_t destination;
  bool oldValue;
  bool oldUnknown;
  bool value;
  bool unknown;
};

NetAliasCache *getNetAliasCache(const Image &image,
                                talos_rt_context *context);
bool isComplementaryDriverPair(const Image &image, talos_rt_context *context,
                               uint64_t lowOffset, uint64_t highOffset,
                               uint64_t width);
bool publishNetBits(talos_rt_context *context, const NetAliasCache &cache,
                    std::vector<NetPublication> &publications, bool &changed);
bool resolveNetRoots(const NetAliasCache &cache, talos_rt_context *context,
                     std::vector<uint64_t> roots, bool &changed);
bool resolveNetRoots(const NetAliasCache &cache, talos_rt_context *context,
                     std::vector<uint64_t> roots, bool &changed,
                     bool useNativeState);
bool resolveDrivenNets(const Image &image, talos_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd, bool &changed);
bool resolveDrivenNets(const Image &image, talos_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd, bool &changed,
                       bool useNativeState);
talos_rt_status applyPassSwitchControl(talos_rt_context *context,
                                         uint32_t passSwitchID,
                                         uint8_t nextState, bool &changed);
talos_rt_status applyDelayedMosEvent(talos_rt_context *context,
                                       uint64_t edgeKey, uint16_t strengths,
                                       bool &changed);

} // namespace talos::designbytecode

#endif // TALOS_RUNTIME_LIB_DESIGNBYTECODENETS_H
