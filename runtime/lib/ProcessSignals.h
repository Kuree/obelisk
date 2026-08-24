//===- ProcessSignals.h - Signal subscription indexing ---------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_PROCESSSIGNALS_H
#define OBELISK_RUNTIME_LIB_PROCESSSIGNALS_H

#include <cstdint>

struct obelisk_rt_context;

constexpr int64_t kSignalSubscriptionPageBits = 256;
constexpr int64_t kWideSignalSubscriptionPage = INT64_MIN;
constexpr __int128 kMaximumIndexedSignalPages = 64;

bool signalSubscriptionBucketRange(uint64_t stableID, uint64_t bitWidth,
                                   uint32_t &kind, uint32_t &id,
                                   int64_t &firstPage, int64_t &lastPage);

bool obelisk_rt_notify_managed_waiters_unlocked(obelisk_rt_context *context,
                                                uint64_t token);

#endif // OBELISK_RUNTIME_LIB_PROCESSSIGNALS_H
