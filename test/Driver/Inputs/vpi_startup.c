#include "vpi_user.h"

static void startup(void) {
  vpiHandle module = vpi_handle_by_name("$root.vpi_top", 0);
  vpiHandle object = vpi_handle_by_name("value", module);
  if (!object) {
    vpi_printf("lookup-failed\n");
    return;
  }
  vpiHandle scope = vpi_handle(vpiScope, object);
  vpiHandle iterator = vpi_iterate(vpiReg, module);
  int register_count = 0;
  vpiHandle scanned;
  while ((scanned = vpi_scan(iterator)) != 0) {
    register_count++;
    vpi_release_handle(scanned);
  }
  vpi_printf(
      "traverse module=%s scope=%s regs=%d same=%d\n",
      vpi_get_str(vpiFullName, module), vpi_get_str(vpiFullName, scope),
      register_count,
      vpi_compare_objects(object, vpi_handle_by_name("vpi_top.value", 0)));
  vpiHandle absolute = vpi_handle_by_name("$root.vpi_top.value", module);
  if (!vpi_compare_objects(object, absolute))
    vpi_printf("absolute-scoped-lookup-failed\n");
  vpi_release_handle(absolute);
  vpi_release_handle(scope);
  vpi_release_handle(module);
  s_vpi_value value = {vpiVectorVal};
  vpi_get_value(object, &value);
  vpi_printf("startup %s size=%d aval=%x bval=%x\n",
             vpi_get_str(vpiFullName, object), vpi_get(vpiSize, object),
             value.value.vector[0].aval, value.value.vector[0].bval);
  value.format = vpiBinStrVal;
  value.value.str = "10_01";
  vpi_put_value(object, &value, 0, vpiForceFlag);
  value.format = vpiIntVal;
  vpi_get_value(object, &value);
  vpi_printf("binary underscore=%d\n", value.value.integer);
  value.format = vpiIntVal;
  value.value.integer = 7;
  vpi_put_value(object, &value, 0, vpiForceFlag);
  vpi_release_handle(object);

  object = vpi_handle_by_name("vpi_top.net_value", 0);
  value.value.integer = 1;
  vpi_put_value(object, &value, 0, vpiForceFlag);
  vpi_release_handle(object);
}

void (*vlog_startup_routines[])(void) = {startup, 0};

int vpi_release_value(void) {
  vpiHandle object = vpi_handle_by_name("$root.vpi_top.value", 0);
  if (!object)
    return -1;
  vpi_put_value(object, 0, 0, vpiReleaseFlag);
  s_vpi_value value = {vpiIntVal};
  vpi_get_value(object, &value);
  vpi_release_handle(object);
  return value.value.integer;
}

int vpi_release_net(void) {
  vpiHandle object = vpi_handle_by_name("vpi_top.net_value", 0);
  if (!object)
    return -1;
  vpi_put_value(object, 0, 0, vpiReleaseFlag);
  s_vpi_value value = {vpiIntVal};
  vpi_get_value(object, &value);
  vpi_release_handle(object);
  return value.value.integer;
}

int vpi_force_release_value(void) {
  vpiHandle object = vpi_handle_by_name("vpi_top.value", 0);
  if (!object)
    return -1;
  s_vpi_value value = {vpiIntVal};
  value.value.integer = 13;
  vpi_put_value(object, &value, 0, vpiForceFlag);
  vpi_get_value(object, &value);
  int forced = value.value.integer;
  vpi_put_value(object, 0, 0, vpiReleaseFlag);
  vpi_get_value(object, &value);
  vpi_release_handle(object);
  return forced * 100 + value.value.integer;
}
