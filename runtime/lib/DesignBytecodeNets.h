//===- DesignBytecodeNets.h - Bytecode net resolution ----------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_DESIGNBYTECODENETS_H
#define OBELISK_RUNTIME_LIB_DESIGNBYTECODENETS_H

#include "DesignBytecodeImage.h"
#include "SignalSemantics.h"

#include <cstdint>
#include <vector>

struct NetAliasCache;
struct obelisk_rt_context;

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
bool publishNetBits(obelisk_rt_context *context, const NetAliasCache &cache,
                    std::vector<NetPublication> &publications, bool &changed);
bool resolveNetRoots(const NetAliasCache &cache, obelisk_rt_context *context,
                     std::vector<uint64_t> roots, bool &changed);
bool resolveDrivenNets(const Image &image, obelisk_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd, bool &changed);

} // namespace obelisk::designbytecode

#endif // OBELISK_RUNTIME_LIB_DESIGNBYTECODENETS_H
