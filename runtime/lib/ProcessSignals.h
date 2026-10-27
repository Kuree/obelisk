//===- ProcessSignals.h - Signal subscription indexing ---------*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_PROCESSSIGNALS_H
#define TALOS_RUNTIME_LIB_PROCESSSIGNALS_H

#include "talos/Runtime/Runtime.h"

#include <cstdint>

struct talos_rt_context;
struct talos_rt_wait_record_v1;

constexpr int64_t kSignalSubscriptionPageBits = 256;
constexpr int64_t kWideSignalSubscriptionPage = INT64_MIN;
constexpr __int128 kMaximumIndexedSignalPages = 64;

bool signalSubscriptionBucketRange(uint64_t stableID, uint64_t bitWidth,
                                   uint32_t &kind, uint32_t &id,
                                   int64_t &firstPage, int64_t &lastPage);

bool talos_rt_notify_managed_waiters_unlocked(talos_rt_context *context,
                                                uint64_t token);
bool talos_rt_same_clock_occurrence_wait_unlocked(
    const talos_rt_context *context, const talos_rt_wait_record_v1 *wait,
    uint64_t waiterToken, bool designWaiter);
void talos_rt_unregister_covergroup_clock_events_unlocked(
    talos_rt_context *context, uint64_t logicalToken);
bool talos_rt_covergroup_strobes_pending_unlocked(
    const talos_rt_context *context);
talos_rt_status
talos_rt_drain_covergroup_strobes_unlocked(talos_rt_context *context);

#endif // TALOS_RUNTIME_LIB_PROCESSSIGNALS_H
