#include "vpi_user.h"

static PLI_INT32 end_compile(p_cb_data callback) {
  vpi_printf("startup second\n");
  return 0;
}

static PLI_INT32 start_simulation(p_cb_data callback) {
  vpi_printf("start simulation\n");
  return 0;
}

static PLI_INT32 end_simulation(p_cb_data callback) {
  vpi_printf("end simulation\n");
  return 0;
}

static void startup(void) {
  static s_cb_data callbacks[] = {
      {cbEndOfCompile, end_compile},
      {cbStartOfSimulation, start_simulation},
      {cbEndOfSimulation, end_simulation},
  };
  for (unsigned index = 0; index != 3; ++index)
    vpi_register_cb(&callbacks[index]);
}

void (*vlog_startup_routines[])(void) = {startup, 0};
