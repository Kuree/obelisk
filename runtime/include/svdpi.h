/*
 * Pinned SystemVerilog DPI-C surface provided by Obelisk.
 *
 * This initial header intentionally exposes the standard scalar and packed
 * integral ABI plus the context functions implemented by libobelisk_rt.
 * Open arrays, export/disable re-entry, strings, real, and chandle are not
 * part of the initial compiler-supported import slice.
 */
#ifndef INCLUDED_SVDPI
#define INCLUDED_SVDPI

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DPI_EXTERN
#if defined(__GNUC__) || defined(__clang__)
#define DPI_EXTERN __attribute__((visibility("default")))
#else
#define DPI_EXTERN
#endif
#endif
#define XXTERN DPI_EXTERN

#define sv_0 0
#define sv_1 1
#define sv_z 2
#define sv_x 3

typedef uint8_t svScalar;
typedef svScalar svBit;
typedef svScalar svLogic;

#ifndef VPI_VECVAL
#define VPI_VECVAL
typedef struct t_vpi_vecval {
  uint32_t aval;
  uint32_t bval;
} s_vpi_vecval, *p_vpi_vecval;
#endif
typedef s_vpi_vecval svLogicVecVal;
typedef uint32_t svBitVecVal;

#define SV_PACKED_DATA_NELEMS(WIDTH) (((WIDTH) + 31) >> 5)

#ifndef VPI_TIME
#define VPI_TIME
typedef struct t_vpi_time {
  int32_t type;
  uint32_t high;
  uint32_t low;
  double real;
} s_vpi_time, *p_vpi_time;

#define vpiScaledRealTime 1
#define vpiSimTime 2
#define vpiSuppressTime 3
#endif
#define sv_scaled_real_time vpiScaledRealTime
#define sv_sim_time vpiSimTime

typedef s_vpi_time svTimeVal;
typedef void *svScope;
typedef void *svOpenArrayHandle;

XXTERN const char *svDpiVersion(void);
XXTERN svScope svGetScope(void);
XXTERN svScope svSetScope(const svScope scope);
XXTERN const char *svGetNameFromScope(const svScope scope);
XXTERN svScope svGetScopeFromName(const char *scopeName);
XXTERN int svPutUserData(const svScope scope, void *userKey, void *userData);
XXTERN void *svGetUserData(const svScope scope, void *userKey);
XXTERN int svGetCallerInfo(const char **fileName, int *lineNumber);
XXTERN int svIsDisabledState(void);
XXTERN void svAckDisabledState(void);
XXTERN int svGetTime(const svScope scope, svTimeVal *time);
XXTERN int svGetTimeUnit(const svScope scope, int32_t *time_unit);
XXTERN int svGetTimePrecision(const svScope scope, int32_t *time_precision);

#undef XXTERN

#ifdef __cplusplus
}
#endif

#endif
