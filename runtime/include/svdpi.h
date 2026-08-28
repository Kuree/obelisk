/*
 * IEEE 1800-2017 Annex I DPI-C include surface provided by Obelisk.
 * The optional deprecated SV3.1a portion is intentionally omitted.
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
#define DPI_EXTERN
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
#define SV_GET_UNSIGNED_BITS(VALUE, N)                                         \
  ((N) == 32 ? (VALUE) : ((VALUE) & SV_MASK(N)))
#define SV_GET_SIGNED_BITS(VALUE, N)                                           \
  ((N) == 32 ? (VALUE)                                                         \
             : (((VALUE) & (1U << (N))) ? ((VALUE) | ~SV_MASK(N))              \
                                        : ((VALUE) & SV_MASK(N))))

typedef void *svScope;
typedef void *svOpenArrayHandle;

XXTERN const char *svDpiVersion(void);
XXTERN svBit svGetBitselBit(const svBitVecVal *s, int i);
XXTERN svLogic svGetBitselLogic(const svLogicVecVal *s, int i);
XXTERN void svPutBitselBit(svBitVecVal *d, int i, svBit s);
XXTERN void svPutBitselLogic(svLogicVecVal *d, int i, svLogic s);
XXTERN void svGetPartselBit(svBitVecVal *d, const svBitVecVal *s, int i, int w);
XXTERN void svGetPartselLogic(svLogicVecVal *d, const svLogicVecVal *s, int i,
                              int w);
XXTERN void svPutPartselBit(svBitVecVal *d, const svBitVecVal s, int i, int w);
XXTERN void svPutPartselLogic(svLogicVecVal *d, const svLogicVecVal s, int i,
                              int w);
XXTERN int svLeft(const svOpenArrayHandle h, int d);
XXTERN int svRight(const svOpenArrayHandle h, int d);
XXTERN int svLow(const svOpenArrayHandle h, int d);
XXTERN int svHigh(const svOpenArrayHandle h, int d);
XXTERN int svIncrement(const svOpenArrayHandle h, int d);
XXTERN int svSize(const svOpenArrayHandle h, int d);
XXTERN int svDimensions(const svOpenArrayHandle h);
XXTERN void *svGetArrayPtr(const svOpenArrayHandle h);
XXTERN int svSizeOfArray(const svOpenArrayHandle h);
XXTERN void *svGetArrElemPtr(const svOpenArrayHandle h, int indx1, ...);
XXTERN void *svGetArrElemPtr1(const svOpenArrayHandle h, int indx1);
XXTERN void *svGetArrElemPtr2(const svOpenArrayHandle h, int indx1, int indx2);
XXTERN void *svGetArrElemPtr3(const svOpenArrayHandle h, int indx1, int indx2,
                              int indx3);

XXTERN void svPutBitArrElemVecVal(const svOpenArrayHandle d,
                                  const svBitVecVal *s, int indx1, ...);
XXTERN void svPutBitArrElem1VecVal(const svOpenArrayHandle d,
                                   const svBitVecVal *s, int indx1);
XXTERN void svPutBitArrElem2VecVal(const svOpenArrayHandle d,
                                   const svBitVecVal *s, int indx1, int indx2);
XXTERN void svPutBitArrElem3VecVal(const svOpenArrayHandle d,
                                   const svBitVecVal *s, int indx1, int indx2,
                                   int indx3);
XXTERN void svPutLogicArrElemVecVal(const svOpenArrayHandle d,
                                    const svLogicVecVal *s, int indx1, ...);
XXTERN void svPutLogicArrElem1VecVal(const svOpenArrayHandle d,
                                     const svLogicVecVal *s, int indx1);
XXTERN void svPutLogicArrElem2VecVal(const svOpenArrayHandle d,
                                     const svLogicVecVal *s, int indx1,
                                     int indx2);
XXTERN void svPutLogicArrElem3VecVal(const svOpenArrayHandle d,
                                     const svLogicVecVal *s, int indx1,
                                     int indx2, int indx3);
XXTERN void svGetBitArrElemVecVal(svBitVecVal *d, const svOpenArrayHandle s,
                                  int indx1, ...);
XXTERN void svGetBitArrElem1VecVal(svBitVecVal *d, const svOpenArrayHandle s,
                                   int indx1);
XXTERN void svGetBitArrElem2VecVal(svBitVecVal *d, const svOpenArrayHandle s,
                                   int indx1, int indx2);
XXTERN void svGetBitArrElem3VecVal(svBitVecVal *d, const svOpenArrayHandle s,
                                   int indx1, int indx2, int indx3);
XXTERN void svGetLogicArrElemVecVal(svLogicVecVal *d, const svOpenArrayHandle s,
                                    int indx1, ...);
XXTERN void svGetLogicArrElem1VecVal(svLogicVecVal *d,
                                     const svOpenArrayHandle s, int indx1);
XXTERN void svGetLogicArrElem2VecVal(svLogicVecVal *d,
                                     const svOpenArrayHandle s, int indx1,
                                     int indx2);
XXTERN void svGetLogicArrElem3VecVal(svLogicVecVal *d,
                                     const svOpenArrayHandle s, int indx1,
                                     int indx2, int indx3);
XXTERN svBit svGetBitArrElem(const svOpenArrayHandle s, int indx1, ...);
XXTERN svBit svGetBitArrElem1(const svOpenArrayHandle s, int indx1);
XXTERN svBit svGetBitArrElem2(const svOpenArrayHandle s, int indx1, int indx2);
XXTERN svBit svGetBitArrElem3(const svOpenArrayHandle s, int indx1, int indx2,
                              int indx3);
XXTERN svLogic svGetLogicArrElem(const svOpenArrayHandle s, int indx1, ...);
XXTERN svLogic svGetLogicArrElem1(const svOpenArrayHandle s, int indx1);
XXTERN svLogic svGetLogicArrElem2(const svOpenArrayHandle s, int indx1,
                                  int indx2);
XXTERN svLogic svGetLogicArrElem3(const svOpenArrayHandle s, int indx1,
                                  int indx2, int indx3);
XXTERN void svPutBitArrElem(const svOpenArrayHandle d, svBit value, int indx1,
                            ...);
XXTERN void svPutBitArrElem1(const svOpenArrayHandle d, svBit value, int indx1);
XXTERN void svPutBitArrElem2(const svOpenArrayHandle d, svBit value, int indx1,
                             int indx2);
XXTERN void svPutBitArrElem3(const svOpenArrayHandle d, svBit value, int indx1,
                             int indx2, int indx3);
XXTERN void svPutLogicArrElem(const svOpenArrayHandle d, svLogic value,
                              int indx1, ...);
XXTERN void svPutLogicArrElem1(const svOpenArrayHandle d, svLogic value,
                               int indx1);
XXTERN void svPutLogicArrElem2(const svOpenArrayHandle d, svLogic value,
                               int indx1, int indx2);
XXTERN void svPutLogicArrElem3(const svOpenArrayHandle d, svLogic value,
                               int indx1, int indx2, int indx3);
XXTERN svScope svGetScope(void);
XXTERN svScope svSetScope(const svScope scope);
XXTERN const char *svGetNameFromScope(const svScope scope);
XXTERN svScope svGetScopeFromName(const char *scopeName);
XXTERN int svPutUserData(const svScope scope, void *userKey, void *userData);
XXTERN void *svGetUserData(const svScope scope, void *userKey);
XXTERN int svGetCallerInfo(const char **fileName, int *lineNumber);
XXTERN int svIsDisabledState(void);
XXTERN void svAckDisabledState(void);

#undef DPI_EXTERN
#undef DPI_PROTOTYPES
#undef XXTERN
#undef EETERN

#ifdef __cplusplus
}
#endif

#endif
