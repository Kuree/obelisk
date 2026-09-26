#include "obelisk/Runtime/Runtime.h"
#include "vpi_user.h"
#include <stdio.h>
#include <stdlib.h>

extern obelisk_rt_status
__real_obelisk_rt_v1_scheduler_run(obelisk_rt_context *context);
extern obelisk_rt_status
__real_obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context);
static int driving;

static vpiHandle timerClock, timerCount;
static int timerEdges, timerFailed;
static obelisk_rt_context *timerSimulation;
static PLI_INT32 clockTimer(p_cb_data callback);

static void scheduleClockTimer(void) {
  s_vpi_time delay = {0};
  delay.type = vpiSimTime;
  delay.low = 5;
  s_cb_data callback = {0};
  callback.reason = cbAfterDelay;
  callback.cb_rtn = clockTimer;
  callback.time = &delay;
  vpiHandle handle = vpi_register_cb(&callback);
  if (!handle)
    timerFailed = 1;
  else
    vpi_release_handle(handle);
}

static PLI_INT32 clockTimer(p_cb_data callback) {
  s_vpi_value value = {0};
  value.format = vpiIntVal;
  vpi_get_value(timerCount, &value);
  int expected = (timerEdges + 1) / 2;
  if (value.value.integer != expected || !callback->time ||
      callback->time->high != 0 ||
      callback->time->low != (unsigned)(timerEdges + 1) * 5) {
    fprintf(stderr, "timer edge=%d count=%d expected=%d\n", timerEdges,
            value.value.integer, expected);
    timerFailed = 1;
    return 0;
  }
  if (timerEdges == 2000) {
    puts("timed external clock count=1000 time=10005");
    return 0;
  }
  value.value.integer = !(timerEdges & 1);
  vpi_put_value(timerClock, &value, 0, vpiNoDelay);
  if (vpi_chk_error(0)) {
    timerFailed = 1;
    return 0;
  }
  // A clock deposit publishes ingress but cannot run HDL before the foreign
  // callback returns. Both tiers must expose the old counter here.
  vpi_get_value(timerCount, &value);
  if (value.value.integer != expected) {
    fprintf(stderr, "clock deposit executed HDL inside the timer callback\n");
    timerFailed = 1;
    return 0;
  }
  ++timerEdges;
  if (getenv("OBELISK_TEST_FINISH_ON_EDGE")) {
    obelisk_rt_v1_scheduler_finish(timerSimulation, 0);
    return 0;
  }
  scheduleClockTimer();
  return 0;
}

static obelisk_rt_status driveClock(
    obelisk_rt_context *context,
    obelisk_rt_status (*run)(obelisk_rt_context *)) {
  if (driving)
    return run(context);
  driving = 1;
  int timed = getenv("OBELISK_TEST_TIMED_CLOCK") != 0;
  obelisk_rt_status status = obelisk_rt_v1_vpi_startup(context, 0, 0);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_end_compile(context);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_start_simulation(context);
  if (!timed && status == OBELISK_RT_OK)
    status = run(context);
  vpiHandle clock = vpi_handle_by_name("external_clock.clk", 0);
  vpiHandle count = vpi_handle_by_name("external_clock.count", 0);
  if (!clock || !count)
    status = OBELISK_RT_INVALID_HANDLE;
  if (timed && status == OBELISK_RT_OK) {
    timerClock = clock;
    timerCount = count;
    timerSimulation = context;
    scheduleClockTimer();
    status = run(context);
    int finish = getenv("OBELISK_TEST_FINISH_ON_EDGE") != 0;
    if (timerFailed || timerEdges != (finish ? 1 : 2000))
      status = OBELISK_RT_INVALID_DESIGN;
    if (finish) {
      s_vpi_value value = {0};
      value.format = vpiIntVal;
      vpi_get_value(count, &value);
      if (value.value.integer != 0)
        status = OBELISK_RT_INVALID_DESIGN;
      else
        puts("timed external finish count=0 time=5");
    }
  }
  for (int cycle = 1; !timed && cycle <= 1000 && status == OBELISK_RT_OK;
       ++cycle) {
    // No HDL oscillator exists. The foreign driver supplies every edge and
    // returns to the simulator at the same boundary as a cocotb VPI write.
    for (int edge = 0; edge != 2; ++edge) {
      s_vpi_value value = {0};
      value.format = vpiIntVal;
      value.value.integer = edge;
      vpi_put_value(clock, &value, 0, vpiNoDelay);
      s_vpi_error_info error = {0};
      if (vpi_chk_error(&error)) {
        status = OBELISK_RT_INVALID_ARGUMENT;
        break;
      }
      status = __real_obelisk_rt_v1_scheduler_run(context);
      if (status != OBELISK_RT_OK)
        break;
    }
    s_vpi_value value = {0};
    value.format = vpiIntVal;
    vpi_get_value(count, &value);
    if (value.value.integer != cycle) {
      fprintf(stderr, "external clock cycle=%d count=%d\n", cycle,
              value.value.integer);
      status = OBELISK_RT_INVALID_DESIGN;
    }
  }
  if (!timed && status == OBELISK_RT_OK)
    puts("external clock count=1000");
  if (clock)
    vpi_release_handle(clock);
  if (count)
    vpi_release_handle(count);
  obelisk_rt_v1_vpi_end_simulation(context);
  obelisk_rt_v1_vpi_shutdown(context);
  driving = 0;
  return status;
}

obelisk_rt_status
__wrap_obelisk_rt_v1_scheduler_run(obelisk_rt_context *context) {
  return driveClock(context, __real_obelisk_rt_v1_scheduler_run);
}
obelisk_rt_status
__wrap_obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context) {
  return driveClock(context, __real_obelisk_rt_v1_scheduler_run_aot);
}
