#include "vpi_user.h"

static const char *names[] = {"collapsed_ports.bus", "collapsed_ports.u.pin",
                              "collapsed_ports.u.leaf.pin"};
static vpiHandle nets[3], callbacks[3];
static int counts[3], failures;
static int equal(const char *a, const char *b) {
  if (!a || !b)
    return 0;
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}
static int check(int ok, const char *message) {
  if (!ok) {
    vpi_printf("FAIL: %s\n", message);
    ++failures;
  }
  return ok;
}
static int vector(vpiHandle object, int a, int b) {
  s_vpi_value value = {vpiVectorVal};
  vpi_get_value(object, &value);
  return value.value.vector && (value.value.vector[0].aval & 255) == a &&
         (value.value.vector[0].bval & 255) == b;
}
static int count(int relation, vpiHandle object) {
  int n = 0;
  vpiHandle iterator = vpi_iterate(relation, object);
  if (iterator)
    while (vpi_scan(iterator))
      ++n;
  return n;
}
static PLI_INT32 changed(p_cb_data callback);
static vpiHandle timer(int index, int delay) {
  static char indices[][2] = {"0", "1", "2"};
  s_vpi_time time = {vpiSimTime, 0, delay};
  s_cb_data callback = {cbAfterDelay, changed, 0, &time, 0, 0, indices[index]};
  return vpi_register_cb(&callback);
}
static PLI_INT32 changed(p_cb_data callback) {
  int index = callback->user_data[0] - '0';
  ++counts[index];
  s_vpi_value value = {vpiVectorVal};
  vpi_get_value(nets[index], &value);
  int a = value.value.vector[0].aval & 255;
  int b = value.value.vector[0].bval & 255;
  for (int i = 0; i < 3; ++i)
    check(vector(nets[i], a, b), "callback sees coherent aliases");
  callbacks[index] = timer(index, 100);
  return 0;
}
int collapse_subscribe(int enable) {
  for (int i = 0; i < 3; ++i) {
    if (enable) {
      // Existing unsupported subscriber kinds must fail without activation.
      s_cb_data unsupported = {cbValueChange, changed, nets[i]};
      check(!vpi_register_cb(&unsupported), "unsupported callback rejected");
      s_vpi_error_info error;
      check(vpi_chk_error(&error) != 0, "unsupported callback diagnostic");
      callbacks[i] = timer(i, 1);
      check(callbacks[i] != 0, "register timer");
    } else {
      check(counts[i] == 1, "timer fired once");
      check(vpi_remove_cb(callbacks[i]), "remove timer");
      callbacks[i] = 0;
    }
  }
  return failures;
}
int collapse_check(int a, int b) {
  for (int i = 0; i < 3; ++i) {
    check(vector(nets[i], a, b), "read through declared net");
  }
  return failures;
}
int collapse_setup(void) {
  for (int i = 0; i < 3; ++i) {
    nets[i] = vpi_handle_by_name((PLI_BYTE8 *)names[i], 0);
    if (!check(nets[i] != 0, "lookup declared net"))
      return failures;
    check(vpi_get(vpiType, nets[i]) == vpiNet, "declared net type");
    check(equal(vpi_get_str(vpiFullName, nets[i]), names[i]), "full name");
    check(vpi_get(vpiSize, nets[i]) == 8, "width");
    check(count(vpiDriver, nets[i]) == 1, "physical driver inventory");
    check(count(vpiLocalDriver, nets[i]) == (i == 0), "local driver inventory");
  }
  for (int i = 1; i < 3; ++i) {
    check(!vpi_compare_objects(nets[0], nets[i]),
          "distinct declaration identity");
    vpiHandle a = vpi_handle(vpiSimNet, nets[0]);
    vpiHandle b = vpi_handle(vpiSimNet, nets[i]);
    check(a && b && vpi_compare_objects(a, b), "one simulated net");
    if (a)
      vpi_release_handle(a);
    if (b)
      vpi_release_handle(b);
  }
  const char *instances[] = {"collapsed_ports.u", "collapsed_ports.u.leaf",
                             "collapsed_ports.left", "collapsed_ports.right"};
  const char *lows[] = {names[1], names[2], "collapsed_ports.left.pin",
                        "collapsed_ports.right.pin"};
  const char *highs[] = {names[0], names[1], "collapsed_ports.right.pin",
                         "collapsed_ports.left.pin"};
  for (int i = 0; i < 4; ++i) {
    vpiHandle expectedLow = vpi_handle_by_name((PLI_BYTE8 *)lows[i], 0);
    vpiHandle expectedHigh = vpi_handle_by_name((PLI_BYTE8 *)highs[i], 0);
    vpiHandle instance = vpi_handle_by_name((PLI_BYTE8 *)instances[i], 0);
    vpiHandle iterator = vpi_iterate(vpiPort, instance);
    vpiHandle port;
    int inputs = 0;
    while (iterator && (port = vpi_scan(iterator))) {
      if (vpi_get(vpiPortIndex, port) != 0)
        continue;
      ++inputs;
      vpiHandle low = vpi_handle(vpiLowConn, port);
      vpiHandle high = vpi_handle(vpiHighConn, port);
      check(low && vpi_compare_objects(low, expectedLow),
            "declared low connection");
      check(high && vpi_compare_objects(high, expectedHigh),
            "declared high connection");
      if (low)
        vpi_release_handle(low);
      if (high)
        vpi_release_handle(high);
    }
    check(inputs == 1, "input port inventory");
    vpi_release_handle(expectedLow);
    vpi_release_handle(expectedHigh);
    vpi_release_handle(instance);
  }
  return collapse_check(0x12, 0);
}
int collapse_put(int index, int a, int b, int mode, int writable) {
  s_vpi_vecval bits = {mode == 2 ? 0xa5 : a, mode == 2 ? 0x5a : b};
  s_vpi_value value = {vpiVectorVal};
  value.value.vector = &bits;
  int flags = mode == 0   ? vpiNoDelay
              : mode == 1 ? vpiForceFlag
                          : vpiReleaseFlag;
  vpi_put_value(nets[index], &value, 0, flags);
  s_vpi_error_info error;
  int failed = vpi_chk_error(&error) != 0;
  // Deposits on driven nets remain explicitly unsupported.
  check(failed == (!writable || mode == 0), "write capability");
  if (writable && mode == 2)
    check(value.value.vector && (value.value.vector[0].aval & 255) == a &&
              (value.value.vector[0].bval & 255) == b,
          "release returns resolved value");
  return failures;
}
void (*vlog_startup_routines[])(void) = {0};
