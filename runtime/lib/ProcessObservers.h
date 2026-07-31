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

#endif // OBELISK_RUNTIME_LIB_PROCESSOBSERVERS_H
