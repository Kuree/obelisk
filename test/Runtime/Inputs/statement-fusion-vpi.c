#include "obelisk/Runtime/Runtime.h"
#include "vpi_user.h"
#include <stdlib.h>

static obelisk_rt_context *active;
static unsigned checkpoints;
extern obelisk_rt_status
__real_obelisk_rt_v1_scheduler_run(obelisk_rt_context *context);

// Supply the driver lifecycle around the MLIR main. Writes arrive at timed
// generic-output safe points, after the preceding cone has settled.
obelisk_rt_status
__wrap_obelisk_rt_v1_scheduler_run(obelisk_rt_context *context) {
  active = context;
  checkpoints = 0;
  obelisk_rt_status status = obelisk_rt_v1_vpi_startup(context, 0, 0);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_end_compile(context);
  if (status == OBELISK_RT_OK)
    status = obelisk_rt_v1_vpi_start_simulation(context);
  if (status == OBELISK_RT_OK)
    status = __real_obelisk_rt_v1_scheduler_run(context);
  obelisk_rt_v1_vpi_end_simulation(context);
  obelisk_rt_v1_vpi_shutdown(context);
  active = 0;
  return status;
}

// IEEE 1800-2023 38.34: a deposit activates consumers, not an unrelated
// producer. Re-running the whole DAG here would erase the internal deposit.
static int writeValue(unsigned step) {
  static const char *names[] = {"dag.input",    "dag.internal", "dag.internal",
                                "dag.internal", "dag.input",    "dag.internal",
                                "dag.input",    "dag.internal", "dag.internal",
                                "dag.input",    "dag.output",   "dag.input"};
  static const unsigned values[] = {9,  64,  64, 7,   11, 0,
                                    12, 255, 0,  255, 99, 0};
  if (step >= sizeof(values) / sizeof(values[0]))
    return 0;
  if (!active)
    return -1;
  vpiHandle handle = vpi_handle_by_name((PLI_BYTE8 *)names[step], 0);
  if (!handle)
    return -2;
  // VPI vector encoding: X is aval=1,bval=1; Z is aval=0,bval=1.
  s_vpi_vecval bits = {values[step], step == 7 || step == 8 ? 255 : 0};
  s_vpi_value value = {0};
  value.format = vpiVectorVal;
  value.value.vector = &bits;
  vpi_put_value(handle, step == 5 ? 0 : &value, 0,
                step == 3   ? vpiForceFlag
                : step == 5 ? vpiReleaseFlag
                            : vpiNoDelay);
  s_vpi_error_info error = {0};
  int status = vpi_chk_error(&error) ? -3 : 0;
  vpi_release_handle(handle);
  return status;
}

extern obelisk_rt_status
__real_obelisk_rt_v1_display(obelisk_rt_context *, uint32_t, uint32_t,
                             obelisk_rt_radix, const obelisk_rt_arg_v1 *,
                             uint64_t, const obelisk_rt_format_env_v1 *);

obelisk_rt_status
__wrap_obelisk_rt_v1_display(obelisk_rt_context *context, uint32_t descriptor,
                             uint32_t newline, obelisk_rt_radix radix,
                             const obelisk_rt_arg_v1 *items, uint64_t count,
                             const obelisk_rt_format_env_v1 *environment) {
  obelisk_rt_status status = __real_obelisk_rt_v1_display(
      context, descriptor, newline, radix, items, count, environment);
  if (status == OBELISK_RT_OK && writeValue(checkpoints++) != 0)
    return OBELISK_RT_INVALID_DESIGN;
  return status;
}
