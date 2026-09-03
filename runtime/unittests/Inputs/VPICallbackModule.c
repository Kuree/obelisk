#include "vpi_user.h"

static int mode;
static int startup_calls;
static int end_compile_calls;
static int forbidden_handle_rejected;
static int forbidden_printf_rejected;

void obelisk_vpi_callback_test_throw(void);

static PLI_INT32 end_compile(p_cb_data callback) {
  (void)callback;
  ++end_compile_calls;
  return 0;
}

static PLI_INT32 unused_callback(p_cb_data callback) {
  (void)callback;
  return 0;
}

static void startup(void) {
  PLI_BYTE8 root_name[] = "$root";
  PLI_BYTE8 forbidden_message[] = "forbidden startup print\n";
  ++startup_calls;
  if (mode == 0)
    return;
  if (mode == 3)
    obelisk_vpi_callback_test_throw();
  forbidden_handle_rejected = vpi_handle_by_name(root_name, 0) == 0;
  forbidden_printf_rejected = vpi_printf(forbidden_message) < 0;
  s_cb_data callback = {0};
  callback.reason = cbEndOfCompile;
  callback.cb_rtn = end_compile;
  vpi_register_cb(&callback);
  if (mode == 2) {
    callback.reason = cbValueChange;
    callback.cb_rtn = unused_callback;
    vpi_register_cb(&callback);
  }
}

void (*vlog_startup_routines[])(void) = {startup, 0};

void obelisk_vpi_callback_test_reset(int new_mode) {
  mode = new_mode;
  startup_calls = 0;
  end_compile_calls = 0;
  forbidden_handle_rejected = 0;
  forbidden_printf_rejected = 0;
}

int obelisk_vpi_callback_test_query(int property) {
  switch (property) {
  case 0:
    return startup_calls;
  case 1:
    return end_compile_calls;
  case 2:
    return forbidden_handle_rejected;
  case 3:
    return forbidden_printf_rejected;
  default:
    return -1;
  }
}
