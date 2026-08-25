/*
 * Pinned SystemVerilog DPI-C surface provided by Obelisk.
 *
 * This header exposes the standard scalar and packed integral ABI plus the
 * context functions implemented by libobelisk_rt. Open-array entry points are
 * added together with their runtime representation so that merely including
 * this header never advertises an ABI that the runtime cannot execute.
 */
#ifndef INCLUDED_SVDPI
#define INCLUDED_SVDPI

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DPI_DLLISPEC
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
#define DPI_DLLISPEC __declspec(dllimport)
#else
#define DPI_DLLISPEC
#endif
#endif

#ifndef DPI_DLLESPEC
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
#define DPI_DLLESPEC __declspec(dllexport)
#else
#define DPI_DLLESPEC
#endif
#endif

#ifndef DPI_EXTERN
#if defined(__GNUC__) || defined(__clang__)
#define DPI_EXTERN __attribute__((visibility("default")))
#else
#define DPI_EXTERN
#endif
#endif

#ifndef DPI_PROTOTYPES
#define DPI_PROTOTYPES
#define XXTERN DPI_EXTERN DPI_DLLISPEC
#define EETERN DPI_EXTERN DPI_DLLESPEC
#endif

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
#define SV_MASK(N) (~(0xffffffffU << (N)))
#define SV_GET_UNSIGNED_BITS(VALUE, N)                                        \
  ((N) == 32 ? (VALUE) : ((VALUE) & SV_MASK(N)))
#define SV_GET_SIGNED_BITS(VALUE, N)                                          \
  ((N) == 32                                                                  \
       ? (VALUE)                                                              \
       : (((VALUE) & (1U << (N))) ? ((VALUE) | ~SV_MASK(N))                  \
                                      : ((VALUE) & SV_MASK(N))))

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
XXTERN svBit svGetBitselBit(const svBitVecVal *s, int i);
XXTERN svLogic svGetBitselLogic(const svLogicVecVal *s, int i);
XXTERN void svPutBitselBit(svBitVecVal *d, int i, svBit s);
XXTERN void svPutBitselLogic(svLogicVecVal *d, int i, svLogic s);
XXTERN void svGetPartselBit(svBitVecVal *d, const svBitVecVal *s, int i,
                            int w);
XXTERN void svGetPartselLogic(svLogicVecVal *d, const svLogicVecVal *s, int i,
                              int w);
XXTERN void svPutPartselBit(svBitVecVal *d, const svBitVecVal s, int i, int w);
XXTERN void svPutPartselLogic(svLogicVecVal *d, const svLogicVecVal s, int i,
                              int w);
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

#ifdef __cplusplus
}
#endif

#endif
