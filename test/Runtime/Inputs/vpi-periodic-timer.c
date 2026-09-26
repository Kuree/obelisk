#include "obelisk/Runtime/Runtime.h"
#include "vpi_user.h"
#include <stdio.h>

extern obelisk_rt_status
__real_obelisk_rt_v1_scheduler_run(obelisk_rt_context *context);
extern obelisk_rt_status
__real_obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context);
static obelisk_rt_context *simulation;
static vpiHandle count;
static int calls, failed;
static PLI_INT32 observeClock(p_cb_data data);

static PLI_INT32 beforeInitialization(p_cb_data data) {
  s_vpi_value value = {0};
  value.format = vpiVectorVal;
  vpi_get_value(count, &value);
  if (!data->time || data->time->high || data->time->low ||
      !value.value.vector || value.value.vector[0].bval != 0xffffffffu) {
    fprintf(stderr, "zero-delay timer must precede Active initialization\n");
    failed = 1;
    obelisk_rt_v1_scheduler_finish(simulation, 0);
    return 0;
  }
  s_vpi_time delay = {0};
  delay.type = vpiSimTime;
  delay.low = 7;
  s_cb_data next = {0};
  next.reason = cbAfterDelay;
  next.cb_rtn = observeClock;
  next.time = &delay;
  vpiHandle timer = vpi_register_cb(&next);
  if (!timer) {
    failed = 1;
    obelisk_rt_v1_scheduler_finish(simulation, 0);
  } else {
    vpi_release_handle(timer);
  }
  return 0;
}

static PLI_INT32 observeClock(p_cb_data data) {
  s_vpi_value value = {0};
  value.format = vpiIntVal;
  vpi_get_value(count, &value);
  unsigned expectedTime = 7 + 10000 * calls;
  int expectedCount = 1 + 1000 * calls;
  if (!data->time || data->time->high || data->time->low != expectedTime ||
      value.value.integer != expectedCount) {
    fprintf(stderr, "periodic callback %d time=%u count=%d\n", calls,
            data->time ? data->time->low : 0, value.value.integer);
    failed = 1;
  } else {
    printf("periodic timer time=%u count=%d\n", expectedTime, expectedCount);
  }
  if (failed || ++calls == 3) {
    obelisk_rt_v1_scheduler_finish(simulation, 0);
  } else {
    s_vpi_time delay = {0};
    delay.type = vpiSimTime;
    delay.low = 10000;
    s_cb_data next = {0};
    next.reason = cbAfterDelay;
    next.cb_rtn = observeClock;
    next.time = &delay;
    vpiHandle timer = vpi_register_cb(&next);
    if (!timer) {
      failed = 1;
      obelisk_rt_v1_scheduler_finish(simulation, 0);
    } else {
      vpi_release_handle(timer);
    }
  }
  return 0;
}

static obelisk_rt_status runWithTimer(
    obelisk_rt_context *context,
    obelisk_rt_status (*run)(obelisk_rt_context *)) {
  if (simulation)
    return run(context);
  simulation = context;
  obelisk_rt_status status = obelisk_rt_v1_vpi_startup(context, 0, 0);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_end_compile(context);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_start_simulation(context);
  count = vpi_handle_by_name("external_clock.count", 0);
  s_vpi_time delay = {0};
  delay.type = vpiSimTime;
  s_cb_data callback = {0};
  callback.reason = cbAfterDelay;
  callback.cb_rtn = beforeInitialization;
  callback.time = &delay;
  vpiHandle timer = vpi_register_cb(&callback);
  if (!count || !timer)
    status = OBELISK_RT_INVALID_HANDLE;
  else
    vpi_release_handle(timer);
  if (status == OBELISK_RT_OK)
    status = run(context);
  if (failed || calls != 3)
    status = OBELISK_RT_INVALID_DESIGN;
  obelisk_rt_v1_vpi_end_simulation(context);
  obelisk_rt_v1_vpi_shutdown(context);
  simulation = 0;
  return status;
}

obelisk_rt_status
__wrap_obelisk_rt_v1_scheduler_run(obelisk_rt_context *context) {
  return runWithTimer(context, __real_obelisk_rt_v1_scheduler_run);
}
obelisk_rt_status
__wrap_obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context) {
  return runWithTimer(context, __real_obelisk_rt_v1_scheduler_run_aot);
}
