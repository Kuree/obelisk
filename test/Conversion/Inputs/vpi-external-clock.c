#include "obelisk/Runtime/Runtime.h"
#include "vpi_user.h"
#include <stdio.h>

extern obelisk_rt_status
__real_obelisk_rt_v1_scheduler_run(obelisk_rt_context *context);
extern obelisk_rt_status
__real_obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context);
static int driving;

static obelisk_rt_status driveClock(
    obelisk_rt_context *context,
    obelisk_rt_status (*run)(obelisk_rt_context *)) {
  if (driving)
    return run(context);
  driving = 1;
  obelisk_rt_status status = obelisk_rt_v1_vpi_startup(context, 0, 0);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_end_compile(context);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_start_simulation(context);
  if (status == OBELISK_RT_OK)
    status = run(context);
  vpiHandle clock = vpi_handle_by_name("external_clock.clk", 0);
  vpiHandle count = vpi_handle_by_name("external_clock.count", 0);
  if (!clock || !count)
    status = OBELISK_RT_INVALID_HANDLE;
  for (int cycle = 1; cycle <= 1000 && status == OBELISK_RT_OK; ++cycle) {
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
  if (status == OBELISK_RT_OK)
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
