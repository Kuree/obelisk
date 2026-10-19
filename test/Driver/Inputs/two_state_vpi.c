#include "sv_vpi_user.h"
#include "vpi_user.h"
#include <stdint.h>

static int check(const char *name, int kind, int width, uint32_t low,
                 uint32_t middle, uint32_t high) {
  vpiHandle handle = vpi_handle_by_name((PLI_BYTE8 *)name, 0);
  if (!handle || vpi_get(vpiType, handle) != kind ||
      vpi_get(vpiSize, handle) != width) {
    vpi_printf("%s: type=%d size=%d, expected type=%d size=%d\n", name,
               handle ? vpi_get(vpiType, handle) : 0,
               handle ? vpi_get(vpiSize, handle) : 0, kind, width);
    return 1;
  }
  s_vpi_value value = {vpiVectorVal};
  vpi_get_value(handle, &value);
  const uint32_t expected[] = {low, middle, high};
  for (int word = 0; word < (width + 31) / 32; ++word)
    if ((uint32_t)value.value.vector[word].aval != expected[word] ||
        value.value.vector[word].bval) {
      vpi_printf("%s[%d]: aval=%x bval=%x, expected %x\n", name, word,
                 value.value.vector[word].aval, value.value.vector[word].bval,
                 expected[word]);
      return 1;
    }
  vpi_release_handle(handle);
  return vpi_chk_error(0) != 0;
}

static int put_scalar(const char *name, int bit, int mode) {
  vpiHandle handle = vpi_handle_by_name((PLI_BYTE8 *)name, 0);
  if (!handle)
    return 1;
  s_vpi_value value = {vpiScalarVal};
  value.value.scalar = bit;
  vpi_put_value(handle, &value, 0, mode);
  vpi_release_handle(handle);
  return vpi_chk_error(0) != 0;
}

int check_two_state_vpi(void) {
  if (check("$root.top.scalar", vpiLogicVar, 1, 1, 0, 0) ||
      check("$root.top.flag", vpiReg, 1, 1, 0, 0) ||
      check("$root.top.declared_bit", vpiBitVar, 1, 1, 0, 0) ||
      check("$root.top.legacy", vpiIntegerVar, 32, 7, 0, 0) ||
      check("$root.top.timestamp", vpiTimeVar, 64, 9, 0, 0) ||
      check("$root.top.wide", vpiLogicVar, 65, 0xabcdef01, 0x12345678, 1) ||
      check("values::PARAM", vpiParameter, 8, 0, 0, 0) ||
      check("values::TIME_PARAM", vpiParameter, 64, 12, 0, 0) ||
      check("$root.top.state", vpiEnumVar, 2, 1, 0, 0) ||
      check("$root.top.packet", vpiStructVar, 9, 0xb5, 0, 0))
    return 1;
  if (put_scalar("$root.top.scalar", vpiZ, vpiNoDelay) ||
      put_scalar("$root.top.flag", vpiX, vpiForceFlag) ||
      put_scalar("$root.top.flag", vpi0, vpiReleaseFlag))
    return 1;
  vpiHandle wide = vpi_handle_by_name((PLI_BYTE8 *)"$root.top.wide", 0);
  if (!wide)
    return 1;
  s_vpi_vecval bits[] = {{0x1234, (int)0xffff0000}, {-1, -1}, {1, 0}};
  s_vpi_value value = {vpiVectorVal};
  value.value.vector = bits;
  vpi_put_value(wide, &value, 0, vpiNoDelay);
  vpi_release_handle(wide);
  return vpi_chk_error(0) != 0;
}

void (*vlog_startup_routines[])(void) = {0};
