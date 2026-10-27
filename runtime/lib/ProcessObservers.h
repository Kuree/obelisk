//===- ProcessObservers.h - Native computed observers ----------*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_PROCESSOBSERVERS_H
#define TALOS_RUNTIME_LIB_PROCESSOBSERVERS_H

#include "talos/Runtime/Runtime.h"

#include <cstdint>

struct ScheduledProcess;
struct talos_rt_context;

ScheduledProcess *findScheduledProcess(talos_rt_context *context,
                                       uint64_t token);
ScheduledProcess *
findScheduledProcessByInstance(talos_rt_context *context,
                               talos_rt_process_instance_v1 *instance);
void updateScheduledProcessInstance(talos_rt_context *context,
                                    ScheduledProcess &process,
                                    talos_rt_process_instance_v1 *instance);
talos_rt_computed_wait_record_v1 *computedWait(ScheduledProcess &process);
bool talos_rt_evaluate_native_clock_condition_unlocked(
    talos_rt_context *context, uint64_t processToken, uint64_t codeUnitID,
    const talos_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t &value, uint64_t &unknown);
bool talos_rt_evaluate_native_bound_observer_unlocked(
    talos_rt_context *context, uint64_t processToken, uint64_t codeUnitID,
    const talos_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t *value, uint64_t *unknown, uint32_t limbCapacity);

#endif // TALOS_RUNTIME_LIB_PROCESSOBSERVERS_H
