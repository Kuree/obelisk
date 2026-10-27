#include "talos/Runtime/Runtime.h"
#include "vpi_user.h"
#include <stdio.h>

extern talos_rt_status __real_talos_rt_v1_scheduler_run(talos_rt_context *);
extern talos_rt_status __real_talos_rt_v1_scheduler_run_aot(talos_rt_context *);
static int driving;

static int get(vpiHandle handle) {
  s_vpi_value value = {0};
  value.format = vpiIntVal;
  vpi_get_value(handle, &value);
  return value.value.integer;
}
static void put(vpiHandle handle, int number, int mode) {
  s_vpi_value value = {0};
  value.format = vpiIntVal;
  value.value.integer = number;
  vpi_put_value(handle, &value, 0, mode);
}

static talos_rt_status drive(talos_rt_context *context,
                              talos_rt_status (*run)(talos_rt_context *)) {
  if (driving) return run(context);
  driving = 1;
  talos_rt_status status = talos_rt_v1_vpi_startup(context, 0, 0);
  if (!status) status = talos_rt_v1_vpi_end_compile(context);
  if (!status) status = talos_rt_v1_vpi_start_simulation(context);
  if (!status) status = run(context);
  vpiHandle clock = vpi_handle_by_name("external_clock.clk", 0);
  vpiHandle forced = vpi_handle_by_name("external_clock.accumulator", 0);
  vpiHandle count = vpi_handle_by_name("external_clock.count", 0);
  vpiHandle untouched = vpi_handle_by_name("external_clock.untouched", 0);
  if (!clock || !forced || !count || !untouched)
    status = TALOS_RT_INVALID_HANDLE;
  for (int cycle = 1; cycle <= 1000 && !status; ++cycle) {
    // 38.34/10.6.2: procedural updates cannot defeat a force; release of a
    // variable retains its forced value until the next procedural update.
    if (cycle == 5) put(forced, 7, vpiForceFlag);
    if (cycle == 12) put(forced, 0, vpiReleaseFlag);
    for (int edge = 0; edge != 2 && !status; ++edge) {
      put(clock, edge, vpiNoDelay);
      if (vpi_chk_error(0)) status = TALOS_RT_INVALID_ARGUMENT;
      if (!status) status = __real_talos_rt_v1_scheduler_run(context);
    }
    int expected = cycle < 5 ? cycle : cycle < 12 ? 7 : cycle - 4;
    if (!status && (get(count) != expected || get(untouched) != cycle)) {
      fprintf(stderr, "cycle=%d forced=%d untouched=%d expected=%d\n",
              cycle, get(count), get(untouched), expected);
      status = TALOS_RT_INVALID_DESIGN;
    }
  }
  if (!status) puts("local force count=996 untouched=1000");
  if (clock) vpi_release_handle(clock);
  if (forced) vpi_release_handle(forced);
  if (count) vpi_release_handle(count);
  if (untouched) vpi_release_handle(untouched);
  talos_rt_v1_vpi_end_simulation(context);
  talos_rt_v1_vpi_shutdown(context);
  driving = 0;
  return status;
}
talos_rt_status __wrap_talos_rt_v1_scheduler_run(talos_rt_context *context) {
  return drive(context, __real_talos_rt_v1_scheduler_run);
}
talos_rt_status __wrap_talos_rt_v1_scheduler_run_aot(talos_rt_context *context) {
  return drive(context, __real_talos_rt_v1_scheduler_run_aot);
}
