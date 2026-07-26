// Runtime-private declarations matching the canonical staged vpi_user.h.
// Keep this header small: external modules compile against the complete IEEE
// headers in the Obelisk resource directory.
#ifndef OBELISK_RUNTIME_VPI_INTERNAL_H
#define OBELISK_RUNTIME_VPI_INTERNAL_H

#include <cstdarg>
#include <cstdint>

using PLI_INT32 = int;
using PLI_UINT32 = unsigned;
using PLI_INT64 = int64_t;
using PLI_BYTE8 = char;
using vpiHandle = PLI_UINT32 *;

struct t_vpi_time {
  PLI_INT32 type;
  PLI_UINT32 high, low;
  double real;
};
using s_vpi_time = t_vpi_time;
using p_vpi_time = t_vpi_time *;

struct t_vpi_vecval {
  PLI_UINT32 aval, bval;
};
using s_vpi_vecval = t_vpi_vecval;

struct t_vpi_value {
  PLI_INT32 format;
  union {
    PLI_BYTE8 *str;
    PLI_INT32 scalar;
    PLI_INT32 integer;
    double real;
    t_vpi_time *time;
    t_vpi_vecval *vector;
    void *strength;
    PLI_BYTE8 *misc;
  } value;
};
using s_vpi_value = t_vpi_value;
using p_vpi_value = t_vpi_value *;

struct t_vpi_error_info {
  PLI_INT32 state;
  PLI_INT32 level;
  PLI_BYTE8 *message;
  PLI_BYTE8 *product;
  PLI_BYTE8 *code;
  PLI_BYTE8 *file;
  PLI_INT32 line;
};
using p_vpi_error_info = t_vpi_error_info *;

struct t_vpi_vlog_info {
  PLI_INT32 argc;
  PLI_BYTE8 **argv;
  PLI_BYTE8 *product;
  PLI_BYTE8 *version;
};
using p_vpi_vlog_info = t_vpi_vlog_info *;

struct t_cb_data;
using p_cb_data = t_cb_data *;
struct t_vpi_systf_data;
using p_vpi_systf_data = t_vpi_systf_data *;

enum {
  vpiModule = 32,
  vpiNet = 36,
  vpiReg = 48,
  vpiScope = 84,
  vpiDriver = 91,
  vpiInternalScope = 92,
  vpiType = 1,
  vpiName = 2,
  vpiFullName = 3,
  vpiSize = 4,
  vpiUndefined = -1,
  vpiBinStrVal = 1,
  vpiScalarVal = 5,
  vpiIntVal = 6,
  vpiVectorVal = 9,
  vpiNoDelay = 1,
  vpiForceFlag = 5,
  vpiReleaseFlag = 6,
  vpiCompile = 1,
  vpiPLI = 2,
  vpiRun = 3,
  vpiNotice = 1,
  vpiWarning = 2,
  vpiError = 3,
  vpiSystem = 4,
  vpiInternal = 5,
  vpi0 = 0,
  vpi1 = 1,
  vpiZ = 2,
  vpiX = 3
};

#endif
