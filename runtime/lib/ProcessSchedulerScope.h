//===- ProcessSchedulerScope.h - Scoped executor selection ------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_PROCESSSCHEDULERSCOPE_H
#define OBELISK_RUNTIME_LIB_PROCESSSCHEDULERSCOPE_H

#include "ProcessContext.h"
#include "RuntimeInternal.h"

// A boundary can temporarily restrict the shared scheduler to one executor.
// Reentrant callbacks must restore the caller's restriction and completion
// result. Resetting to defaults would let the caller drain unrelated work or
// advance time after the callback returns (IEEE 1800-2023 4.5, 4.10).
class NativeScheduleStepScope {
public:
  NativeScheduleStepScope(obelisk_rt_context *context, uint32_t actorSlot,
                          bool controlOnly, bool processFilterActive = false,
                          uint64_t processToken = 0)
      : context(context) {
    ContextMutexLock lock(context);
    previous = {context->nativeScheduleForcedSlot,
                context->nativeScheduleSingleStep,
                context->nativeScheduleForcedExecuted,
                context->nativeScheduleControlOnly,
                context->nativeScheduleProcessFilterActive,
                context->nativeScheduleForcedProcessToken};
    context->nativeScheduleForcedSlot = actorSlot;
    context->nativeScheduleSingleStep = true;
    context->nativeScheduleForcedExecuted = false;
    context->nativeScheduleControlOnly = controlOnly;
    context->nativeScheduleProcessFilterActive = processFilterActive;
    context->nativeScheduleForcedProcessToken = processToken;
  }

  NativeScheduleStepScope(const NativeScheduleStepScope &) = delete;
  NativeScheduleStepScope &operator=(const NativeScheduleStepScope &) = delete;

  ~NativeScheduleStepScope() {
    ContextMutexLock lock(context);
    context->nativeScheduleForcedSlot = previous.actorSlot;
    context->nativeScheduleSingleStep = previous.singleStep;
    context->nativeScheduleForcedExecuted = previous.executed;
    context->nativeScheduleControlOnly = previous.controlOnly;
    context->nativeScheduleProcessFilterActive = previous.processFilterActive;
    context->nativeScheduleForcedProcessToken = previous.processToken;
  }

  bool executed() const {
    ContextMutexLock lock(context);
    return context->nativeScheduleForcedExecuted;
  }

private:
  struct Selection {
    uint32_t actorSlot;
    bool singleStep;
    bool executed;
    bool controlOnly;
    bool processFilterActive;
    uint64_t processToken;
  } previous;
  obelisk_rt_context *context;
};

#endif // OBELISK_RUNTIME_LIB_PROCESSSCHEDULERSCOPE_H
