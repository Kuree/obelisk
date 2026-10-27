//===- RandReal.cpp - Sampling numeric real intervals -------------------===//

#include "ExceptionSupport.h"
#include "RealRandom.h"

#include <new>

using namespace talos::runtime;

namespace {
template <typename Float>
talos_rt_status encode(Float value, uint64_t *out, uint64_t count) {
  if (!out || count != RealCoordinates<Float>::rankWords)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    return RealCoordinates<Float>::encode(value, out)
               ? TALOS_RT_OK
               : TALOS_RT_INVALID_ARGUMENT;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

template <typename Float>
talos_rt_status decode(const uint64_t *words, uint64_t count, Float *out) {
  if (!words || !out || count != RealCoordinates<Float>::rankWords)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    Float result;
    if (!RealCoordinates<Float>::decode(words, result))
      return TALOS_RT_INVALID_ARGUMENT;
    *out = result;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

template <typename Float>
talos_rt_status sample(talos_rt_random_state_v1 *state, Float lower,
                         Float upper, uint32_t flags, Float *out,
                         uint32_t *success) {
  if (!state || !(state->increment & 1) || !out || !success ||
      (flags & ~(TALOS_RT_REAL_LOWER_OPEN | TALOS_RT_REAL_UPPER_OPEN)) ||
      !RealCoordinates<Float>::finite(lower) ||
      !RealCoordinates<Float>::finite(upper))
    return TALOS_RT_INVALID_ARGUMENT;
  if (lower > upper) {
    *success = 0;
    return TALOS_RT_OK;
  }
  TALOS_RT_TRY {
    talos_rt_random_state_v1 next = *state;
    Float result;
    if (!RealCoordinates<Float>::sample(next, lower, upper, flags, result)) {
      *success = 0;
      return TALOS_RT_OK;
    }
    *out = result;
    *state = next;
    *success = 1;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}
} // namespace

extern "C" talos_rt_status
talos_rt_v1_random_real64_to_rank(double value, uint64_t *out_words,
                                    uint64_t word_count) {
  return encode(value, out_words, word_count);
}
extern "C" talos_rt_status
talos_rt_v1_random_real32_to_rank(float value, uint64_t *out_words,
                                    uint64_t word_count) {
  return encode(value, out_words, word_count);
}
extern "C" talos_rt_status
talos_rt_v1_random_real64_from_rank(const uint64_t *words,
                                      uint64_t word_count, double *out_value) {
  return decode(words, word_count, out_value);
}
extern "C" talos_rt_status
talos_rt_v1_random_real32_from_rank(const uint64_t *words,
                                      uint64_t word_count, float *out_value) {
  return decode(words, word_count, out_value);
}
extern "C" talos_rt_status
talos_rt_v1_random_real64_interval(talos_rt_random_state_v1 *state,
                                     double lower, double upper, uint32_t flags,
                                     double *out_value, uint32_t *out_success) {
  return sample(state, lower, upper, flags, out_value, out_success);
}
extern "C" talos_rt_status
talos_rt_v1_random_real32_interval(talos_rt_random_state_v1 *state,
                                     float lower, float upper, uint32_t flags,
                                     float *out_value, uint32_t *out_success) {
  return sample(state, lower, upper, flags, out_value, out_success);
}
