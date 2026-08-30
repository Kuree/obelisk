//===- ProcessObservers.h - Native computed observers ----------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_PROCESSOBSERVERS_H
#define OBELISK_RUNTIME_LIB_PROCESSOBSERVERS_H

#include "obelisk/Runtime/Runtime.h"

#include <cstdint>

struct ScheduledProcess;
struct obelisk_rt_context;

ScheduledProcess *findScheduledProcess(obelisk_rt_context *context,
                                       uint64_t token);
obelisk_rt_computed_wait_record_v1 *computedWait(ScheduledProcess &process);
bool obelisk_rt_evaluate_native_clock_condition_unlocked(
    obelisk_rt_context *context, uint64_t processToken, uint64_t codeUnitID,
    const obelisk_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t &value, uint64_t &unknown);

#endif // OBELISK_RUNTIME_LIB_PROCESSOBSERVERS_H
