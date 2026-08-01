//===- Random.cpp - Deterministic hierarchical PCG streams ---------------===//

#include "RuntimeInternal.h"

#include <cmath>
#include <limits>

namespace {

constexpr uint64_t kMultiplier = UINT64_C(6364136223846793005);
// The prior context-global generator used this odd increment directly.
constexpr uint64_t kDefaultIncrement = UINT64_C(1442695040888963407);
constexpr uint64_t kDefaultSequence = kDefaultIncrement >> 1;

uint64_t next64(obelisk_rt_random_state_v1 &state) {
  return (static_cast<uint64_t>(obelisk_rt_v1_random_state_next32(&state))
          << 32) |
         obelisk_rt_v1_random_state_next32(&state);
}

} // namespace

extern "C" void
obelisk_rt_v1_random_state_seed(obelisk_rt_random_state_v1 *state,
                                uint64_t seed, uint64_t sequence) {
  if (!state)
    return;
  state->state = 0;
  state->increment = (sequence << 1) | 1;
  (void)obelisk_rt_v1_random_state_next32(state);
  state->state += seed;
  (void)obelisk_rt_v1_random_state_next32(state);
}

extern "C" uint32_t
obelisk_rt_v1_random_state_next32(obelisk_rt_random_state_v1 *state) {
  if (!state)
    return 0;
  uint64_t old = state->state;
  state->state = old * kMultiplier + state->increment;
  uint32_t shifted = static_cast<uint32_t>(((old >> 18) ^ old) >> 27);
  uint32_t rotation = static_cast<uint32_t>(old >> 59);
  return (shifted >> rotation) | (shifted << ((0u - rotation) & 31));
}

extern "C" uint64_t
obelisk_rt_v1_random_state_next64(obelisk_rt_random_state_v1 *state) {
  return state ? next64(*state) : 0;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_random_state_bounded(obelisk_rt_random_state_v1 *state,
                                   uint64_t bound, uint64_t *outValue) {
  if (!state || !outValue || bound == 0 || (state->increment & 1) == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t threshold = (uint64_t{0} - bound) % bound;
  uint64_t value;
  do {
    value = next64(*state);
  } while (value < threshold);
  *outValue = value % bound;
  return OBELISK_RT_OK;
}

obelisk_rt_random_state_v1 *
obelisk_rt_random_active_state_unlocked(obelisk_rt_context *context) {
  if (context && context->activeRandom)
    return context->activeRandom;
  if (!context || context->activeLogicalProcessToken == 0)
    return context ? &context->random : nullptr;
  uint64_t token = context->activeLogicalProcessToken;
  if ((token & OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0) {
    token &= ~OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG;
    for (ScheduledProcess &process : context->scheduledProcesses)
      if (process.token == token)
        return &process.random;
  } else {
    for (ScheduledDesignTask &task : context->scheduledDesignTasks)
      if (task.id == token)
        return &task.random;
  }
  return &context->random;
}

void obelisk_rt_random_split_unlocked(obelisk_rt_context *context,
                                      obelisk_rt_random_state_v1 &child) {
  obelisk_rt_random_state_v1 *parent =
      obelisk_rt_random_active_state_unlocked(context);
  uint64_t seed = next64(*parent);
  uint64_t sequence = next64(*parent);
  obelisk_rt_v1_random_state_seed(&child, seed, sequence);
}

void obelisk_rt_random_seed_context_unlocked(obelisk_rt_context *context,
                                             uint64_t seed) {
  obelisk_rt_v1_random_state_seed(&context->random, seed, kDefaultSequence);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_random_next(obelisk_rt_context *context, uint64_t *outValue) {
  if (!context || !outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    *outValue = next64(*obelisk_rt_random_active_state_unlocked(context));
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_random_seed(obelisk_rt_context *context, uint64_t seed) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_random_state_v1 *state =
        obelisk_rt_random_active_state_unlocked(context);
    uint64_t sequence = state->increment >> 1;
    obelisk_rt_v1_random_state_seed(state, seed, sequence);
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_random_bounded(obelisk_rt_context *context, uint64_t bound,
                             uint64_t *outValue) {
  if (!context || !outValue || bound == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    return obelisk_rt_v1_random_state_bounded(
        obelisk_rt_random_active_state_unlocked(context), bound, outValue);
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

namespace {

// A uniform draw on the open interval (0, 1). The transforms below take
// logarithms and reciprocals, so neither endpoint may ever be produced.
double openUnit(obelisk_rt_random_state_v1 &state) {
  for (;;) {
    uint64_t draw = next64(state) >> 11;
    if (draw != 0)
      return static_cast<double>(draw) * 0x1.0p-53;
  }
}

// Box-Muller. Only one of the generated pair is kept, which costs a second
// draw per call but keeps the transform free of carried state.
double standardNormal(obelisk_rt_random_state_v1 &state) {
  double radius = std::sqrt(-2.0 * std::log(openUnit(state)));
  return radius * std::cos(2.0 * 3.14159265358979323846 * openUnit(state));
}

double chiSquareDraw(obelisk_rt_random_state_v1 &state, int32_t freedom) {
  double total = 0.0;
  for (int32_t index = 0; index < freedom; ++index) {
    double normal = standardNormal(state);
    total += normal * normal;
  }
  return total;
}

// Knuth's product method below the range where exp(-mean) underflows; a
// normal approximation past it, where that method would loop indefinitely.
double poissonDraw(obelisk_rt_random_state_v1 &state, double mean) {
  if (mean <= 0.0)
    return 0.0;
  if (mean < 30.0) {
    double limit = std::exp(-mean);
    double product = 1.0;
    double count = 0.0;
    for (product *= openUnit(state); product > limit;
         product *= openUnit(state))
      count += 1.0;
    return count;
  }
  double approximation = mean + std::sqrt(mean) * standardNormal(state);
  return approximation < 0.0 ? 0.0 : approximation;
}

int32_t roundToInt32(double value) {
  if (!std::isfinite(value))
    return 0;
  double rounded = std::round(value);
  if (rounded <= static_cast<double>(std::numeric_limits<int32_t>::min()))
    return std::numeric_limits<int32_t>::min();
  if (rounded >= static_cast<double>(std::numeric_limits<int32_t>::max()))
    return std::numeric_limits<int32_t>::max();
  return static_cast<int32_t>(rounded);
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_random_distribution(
    obelisk_rt_context *context, obelisk_rt_distribution distribution,
    int32_t first, int32_t second, int32_t *outValue) {
  if (!context || !outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_random_state_v1 *state =
        obelisk_rt_random_active_state_unlocked(context);
    double value = 0.0;
    switch (distribution) {
    case OBELISK_RT_DISTRIBUTION_UNIFORM: {
      int32_t low = first < second ? first : second;
      int32_t high = first < second ? second : first;
      uint64_t extent = static_cast<uint64_t>(
          static_cast<int64_t>(high) - static_cast<int64_t>(low) + 1);
      uint64_t draw = 0;
      obelisk_rt_status status =
          obelisk_rt_v1_random_state_bounded(state, extent, &draw);
      if (status != OBELISK_RT_OK)
        return status;
      *outValue = static_cast<int32_t>(static_cast<int64_t>(low) +
                                       static_cast<int64_t>(draw));
      return OBELISK_RT_OK;
    }
    case OBELISK_RT_DISTRIBUTION_NORMAL:
      value = static_cast<double>(first) +
              static_cast<double>(second) * standardNormal(*state);
      break;
    case OBELISK_RT_DISTRIBUTION_EXPONENTIAL:
      // The mean is specified as positive; a nonpositive one has no draw to
      // make, so the distribution degenerates to its mean.
      value = first > 0 ? -static_cast<double>(first) *
                              std::log(openUnit(*state))
                        : static_cast<double>(first);
      break;
    case OBELISK_RT_DISTRIBUTION_POISSON:
      value = poissonDraw(*state, static_cast<double>(first));
      break;
    case OBELISK_RT_DISTRIBUTION_CHI_SQUARE:
      value = first > 0 ? chiSquareDraw(*state, first) : 0.0;
      break;
    case OBELISK_RT_DISTRIBUTION_T: {
      if (first <= 0) {
        value = 0.0;
        break;
      }
      double normal = standardNormal(*state);
      double chi = chiSquareDraw(*state, first);
      value = chi > 0.0
                  ? normal / std::sqrt(chi / static_cast<double>(first))
                  : 0.0;
      break;
    }
    case OBELISK_RT_DISTRIBUTION_ERLANG: {
      // k exponential stages whose means sum to the requested mean.
      if (first <= 0) {
        value = static_cast<double>(second);
        break;
      }
      double stage = static_cast<double>(second) / static_cast<double>(first);
      for (int32_t index = 0; index < first; ++index)
        value += -stage * std::log(openUnit(*state));
      break;
    }
    default:
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    *outValue = roundToInt32(value);
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_random_get_state(obelisk_rt_context *context,
                               obelisk_rt_random_state_v1 *outState) {
  if (!context || !outState)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    *outState = *obelisk_rt_random_active_state_unlocked(context);
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_random_set_state(obelisk_rt_context *context,
                               const obelisk_rt_random_state_v1 *state) {
  if (!context || !state || (state->increment & 1) == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    *obelisk_rt_random_active_state_unlocked(context) = *state;
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}
