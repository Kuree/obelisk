#include "vpi_user.h"

// Reads a net and a variable through the VPI backdoor while the design is
// running. Nets lose their directly addressable handles once VPI observability
// is requested, so these probes exercise the runtime plane accessor path.

static PLI_INT32 end_compile(p_cb_data callback) {
  vpi_printf("probe startup\n");
  return 0;
}

static void startup(void) {
  static s_cb_data callback = {cbEndOfCompile, end_compile};
  vpi_register_cb(&callback);
}

void (*vlog_startup_routines[])(void) = {startup, 0};

static int readInt(const char *name) {
  vpiHandle object = vpi_handle_by_name((PLI_BYTE8 *)name, 0);
  if (!object)
    return -1;
  s_vpi_value value = {vpiIntVal};
  vpi_get_value(object, &value);
  vpi_release_handle(object);
  return value.value.integer;
}

int vpi_probe_net(void) { return readInt("vpi_nets.u.masked"); }

int vpi_probe_reg(void) { return readInt("vpi_nets.u.captured"); }

int vpi_probe_size(void) {
  vpiHandle object = vpi_handle_by_name((PLI_BYTE8 *)"vpi_nets.u.masked", 0);
  if (!object)
    return -1;
  int size = (int)vpi_get(vpiSize, object);
  vpi_release_handle(object);
  return size;
}
