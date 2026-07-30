//===- DesignBytecodeNets.h - Bytecode net resolution ----------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_DESIGNBYTECODENETS_H
#define OBELISK_RUNTIME_LIB_DESIGNBYTECODENETS_H

#include "DesignBytecodeImage.h"

#include <cstdint>
#include <vector>

struct NetAliasCache;
struct obelisk_rt_context;

namespace obelisk::designbytecode {

struct NetPublication {
  uint64_t destination;
  bool oldValue;
  bool oldUnknown;
  bool value;
  bool unknown;
};

bool rangesOverlap(uint64_t left, uint64_t leftWidth, uint64_t right,
                   uint64_t rightWidth);
bool signalEdgeMatches(uint32_t requested, uint32_t observed);
uint32_t transitionEdges(bool oldValue, bool oldUnknown, bool newValue,
                         bool newUnknown);
NetAliasCache *getNetAliasCache(const Image &image,
                                obelisk_rt_context *context);
bool publishNetBits(obelisk_rt_context *context, const NetAliasCache &cache,
                    std::vector<NetPublication> &publications, bool &changed);
bool resolveNetRoots(const NetAliasCache &cache, obelisk_rt_context *context,
                     std::vector<uint64_t> roots, bool &changed);
bool resolveDrivenNets(const Image &image, obelisk_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd, bool &changed);

} // namespace obelisk::designbytecode

#endif // OBELISK_RUNTIME_LIB_DESIGNBYTECODENETS_H
