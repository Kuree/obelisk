//===- DesignBytecodeNets.h - Bytecode net resolution ----------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_DESIGNBYTECODENETS_H
#define OBELISK_RUNTIME_LIB_DESIGNBYTECODENETS_H

#include "DesignBytecodeImage.h"
#include "SignalSemantics.h"

#include <cstdint>
#include <vector>

struct NetAliasCache;
struct obelisk_rt_context;

obelisk_rt_status obelisk_rt_count_design_drivers(
    obelisk_rt_context *context, uint64_t netHandle, uint32_t *outForced,
    uint32_t *outTotal, uint32_t *outZero, uint32_t *outOne,
    uint32_t *outUnknown, bool useNativeState) noexcept;

obelisk_rt_status obelisk_rt_design_net_strength(
    obelisk_rt_context *context, uint64_t netHandle,
    uint16_t *outStrengths, bool useNativeState) noexcept;

namespace obelisk::designbytecode {

using obelisk::runtime::rangesOverlap;
using obelisk::runtime::signalEdgeMatches;
using obelisk::runtime::transitionEdges;

struct NetPublication {
  uint64_t destination;
  bool oldValue;
  bool oldUnknown;
  bool value;
  bool unknown;
};

NetAliasCache *getNetAliasCache(const Image &image,
                                obelisk_rt_context *context);
bool isComplementaryDriverPair(const Image &image, obelisk_rt_context *context,
                               uint64_t lowOffset, uint64_t highOffset,
                               uint64_t width);
bool publishNetBits(obelisk_rt_context *context, const NetAliasCache &cache,
                    std::vector<NetPublication> &publications, bool &changed);
bool resolveNetRoots(const NetAliasCache &cache, obelisk_rt_context *context,
                     std::vector<uint64_t> roots, bool &changed);
bool resolveNetRoots(const NetAliasCache &cache, obelisk_rt_context *context,
                     std::vector<uint64_t> roots, bool &changed,
                     bool useNativeState);
bool resolveDrivenNets(const Image &image, obelisk_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd, bool &changed);
bool resolveDrivenNets(const Image &image, obelisk_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd, bool &changed,
                       bool useNativeState);

} // namespace obelisk::designbytecode

#endif // OBELISK_RUNTIME_LIB_DESIGNBYTECODENETS_H
