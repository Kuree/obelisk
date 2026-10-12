//===- RandReal.cpp - Sampling numeric real intervals -------------------===//

#include "ExceptionSupport.h"
#include "RealRandom.h"

#include <new>

using namespace obelisk::runtime;

namespace {
template <typename Float>
obelisk_rt_status encode(Float value, uint64_t *out, uint64_t count) {
  if (!out || count != RealCoordinates<Float>::rankWords)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    return RealCoordinates<Float>::encode(value, out)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_ARGUMENT;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

template <typename Float>
obelisk_rt_status decode(const uint64_t *words, uint64_t count, Float *out) {
  if (!words || !out || count != RealCoordinates<Float>::rankWords)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    Float result;
    if (!RealCoordinates<Float>::decode(words, result))
      return OBELISK_RT_INVALID_ARGUMENT;
    *out = result;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

template <typename Float>
obelisk_rt_status sample(obelisk_rt_random_state_v1 *state, Float lower,
                         Float upper, uint32_t flags, Float *out,
                         uint32_t *success) {
  if (!state || !(state->increment & 1) || !out || !success ||
      (flags & ~(OBELISK_RT_REAL_LOWER_OPEN | OBELISK_RT_REAL_UPPER_OPEN)) ||
      !RealCoordinates<Float>::finite(lower) ||
      !RealCoordinates<Float>::finite(upper))
    return OBELISK_RT_INVALID_ARGUMENT;
  if (lower > upper) {
    *success = 0;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_TRY {
    obelisk_rt_random_state_v1 next = *state;
    Float result;
    if (!RealCoordinates<Float>::sample(next, lower, upper, flags, result)) {
      *success = 0;
      return OBELISK_RT_OK;
    }
    *out = result;
    *state = next;
    *success = 1;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}
} // namespace

extern "C" obelisk_rt_status
obelisk_rt_v1_random_real64_to_rank(double value, uint64_t *out_words,
                                    uint64_t word_count) {
  return encode(value, out_words, word_count);
}
extern "C" obelisk_rt_status
obelisk_rt_v1_random_real32_to_rank(float value, uint64_t *out_words,
                                    uint64_t word_count) {
  return encode(value, out_words, word_count);
}
extern "C" obelisk_rt_status
obelisk_rt_v1_random_real64_from_rank(const uint64_t *words,
                                      uint64_t word_count, double *out_value) {
  return decode(words, word_count, out_value);
}
extern "C" obelisk_rt_status
obelisk_rt_v1_random_real32_from_rank(const uint64_t *words,
                                      uint64_t word_count, float *out_value) {
  return decode(words, word_count, out_value);
}
extern "C" obelisk_rt_status
obelisk_rt_v1_random_real64_interval(obelisk_rt_random_state_v1 *state,
                                     double lower, double upper, uint32_t flags,
                                     double *out_value, uint32_t *out_success) {
  return sample(state, lower, upper, flags, out_value, out_success);
}
extern "C" obelisk_rt_status
obelisk_rt_v1_random_real32_interval(obelisk_rt_random_state_v1 *state,
                                     float lower, float upper, uint32_t flags,
                                     float *out_value, uint32_t *out_success) {
  return sample(state, lower, upper, flags, out_value, out_success);
}
