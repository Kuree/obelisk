//===- Random.cpp - Deterministic hierarchical PCG streams ---------------===//

#include "RuntimeInternal.h"

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
