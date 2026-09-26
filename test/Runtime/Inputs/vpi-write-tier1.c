#include "obelisk/Runtime/Runtime.h"
#include "vpi_user.h"
#include <stdlib.h>

static obelisk_rt_context *active;
static unsigned checkpoints;
extern obelisk_rt_status
__real_obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context);

// Supply the driver's VPI lifecycle around the pass-generated MLIR main.
// No callback is registered ahead of time. Inject writes at the delayed
// actor's generic-output checkpoints, never through the runtime-free
// eval_display service. This tests late mutation without a frontend fixture
// or a persistent callback that would keep Tier 1 disabled from startup.
obelisk_rt_status
__wrap_obelisk_rt_v1_scheduler_run_aot(obelisk_rt_context *context) {
  active = context;
  checkpoints = 0;
  obelisk_rt_status status = obelisk_rt_v1_vpi_startup(context, 0, 0);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_end_compile(context);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_start_simulation(context);
  if (status == OBELISK_RT_OK)
    status = __real_obelisk_rt_v1_scheduler_run_aot(context);
  obelisk_rt_v1_vpi_end_simulation(context);
  obelisk_rt_v1_vpi_shutdown(context);
  active = 0;
  return status;
}

static int checkImmediateNetRelease(void) {
  vpiHandle net = vpi_handle_by_name("vpi_write.net", 0);
  if (!net)
    return -1;
  s_vpi_value read = {0};
  read.format = vpiIntVal;
  vpi_get_value(net, &read);
  int before = read.value.integer;
  int status = before == 500 ? 0 : -1;
  // Include an unchanged force, a changed force, and an X force. All releases
  // happen before returning to the scheduler, with no intervening driver store.
  for (int mode = 0; mode != 3; ++mode) {
    s_vpi_vecval bits = {mode == 0   ? before
                         : mode == 1 ? 123
                                     : 0,
                         mode == 2 ? -1 : 0};
    s_vpi_value value = {0};
    value.format = vpiVectorVal;
    value.value.vector = &bits;
    vpi_put_value(net, &value, 0, vpiForceFlag);
    vpi_put_value(net, 0, 0, vpiReleaseFlag);
    vpi_get_value(net, &read);
    s_vpi_error_info error = {0};
    if (vpi_chk_error(&error) || read.value.integer != before)
      status = -1;
  }
  vpi_release_handle(net);
  return status;
}

static int writeValue(int mode) {
  if (!active)
    return -1;
  if (mode == 0 && getenv("OBELISK_TEST_IMMEDIATE_NET_RELEASE") &&
      checkImmediateNetRelease() != 0)
    return -4;
  vpiHandle handle =
      vpi_handle_by_name(mode >= 5 ? "vpi_write.net" : "vpi_write.q", 0);
  if (!handle)
    return -2;
  s_vpi_vecval bits = {mode == 0 ? 41 : mode == 1 ? 7 : mode == 3 ? -1
                                      : mode == 5 ? 55 : 0,
                      mode == 3 ? -1 : 0};
  s_vpi_value value = {0};
  value.format = vpiVectorVal;
  value.value.vector = &bits;
  int release = mode == 2 || mode == 6;
  vpi_put_value(handle, release ? 0 : &value, 0,
                mode == 1 || mode == 5 ? vpiForceFlag
                : release ? vpiReleaseFlag : vpiNoDelay);
  s_vpi_error_info error = {0};
  int status = vpi_chk_error(&error) ? -3 : 0;
  vpi_release_handle(handle);
  return status;
}

extern obelisk_rt_status __real_obelisk_rt_v1_display(
    obelisk_rt_context *, uint32_t, uint32_t, obelisk_rt_radix,
    const obelisk_rt_arg_v1 *, uint64_t, const obelisk_rt_format_env_v1 *);

obelisk_rt_status __wrap_obelisk_rt_v1_display(
    obelisk_rt_context *context, uint32_t descriptor, uint32_t newline,
    obelisk_rt_radix radix, const obelisk_rt_arg_v1 *items, uint64_t count,
    const obelisk_rt_format_env_v1 *environment) {
  obelisk_rt_status status = __real_obelisk_rt_v1_display(
      context, descriptor, newline, radix, items, count, environment);
  unsigned checkpoint = checkpoints++;
  if (status == OBELISK_RT_OK && checkpoint < 14 && checkpoint % 2 == 0 &&
      writeValue(checkpoint / 2) != 0)
    return OBELISK_RT_INVALID_DESIGN;
  return status;
}
