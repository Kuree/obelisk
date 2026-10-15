#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>

int control_toggle(int operation) {
  return vpi_control(operation, vpiToggleCoverage,
                     vpi_handle_by_name("vpi_control", 0));
}

int control_unsupported(void) {
  int result = vpi_control(vpiReset, 0, 0, 0);
  s_vpi_error_info error = {0};
  printf("unsupported result=%d error=%d\n", result, vpi_chk_error(&error));
  return result;
}

int control_finish(void) { return vpi_control(vpiFinish, 0); }
