//===- Random.cpp - Deterministic hierarchical PCG streams ---------------===//

#include "RuntimeInternal.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {

constexpr uint64_t kMultiplier = TALOS_RT_RANDOM_MULTIPLIER;
// The prior context-global generator used this odd increment directly.
constexpr uint64_t kDefaultIncrement = TALOS_RT_RANDOM_DEFAULT_INCREMENT;
constexpr uint64_t kDefaultSequence = kDefaultIncrement >> 1;

uint64_t next64(talos_rt_random_state_v1 &state) {
  return (static_cast<uint64_t>(talos_rt_v1_random_state_next32(&state))
          << 32) |
         talos_rt_v1_random_state_next32(&state);
}

uint64_t lowMask(unsigned width) {
  return width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
}

uint64_t randcRound(uint64_t key, uint64_t value, unsigned round) {
  uint64_t mixed = key ^ (value + UINT64_C(0x9e3779b97f4a7c15) * (round + 1));
  mixed = (mixed ^ (mixed >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  mixed = (mixed ^ (mixed >> 27)) * UINT64_C(0x94d049bb133111eb);
  return mixed ^ (mixed >> 31);
}

uint64_t randcPermute(uint64_t key, uint64_t position, unsigned width) {
  if (width == 1)
    return (position ^ key) & 1;

  unsigned leftWidth = width / 2;
  unsigned rightWidth = width - leftWidth;
  uint64_t left = position >> rightWidth;
  uint64_t right = position & lowMask(rightWidth);
  for (unsigned round = 0; round != 4; ++round) {
    uint64_t nextLeft = right;
    uint64_t nextRight =
        (left ^ randcRound(key, right, round)) & lowMask(leftWidth);
    left = nextLeft;
    right = nextRight;
    std::swap(leftWidth, rightWidth);
  }
  return ((left << rightWidth) | right) & lowMask(width);
}

} // namespace

extern "C" void
talos_rt_v1_random_state_seed(talos_rt_random_state_v1 *state,
                                uint64_t seed, uint64_t sequence) {
  if (!state)
    return;
  state->state = 0;
  state->increment = (sequence << 1) | 1;
  (void)talos_rt_v1_random_state_next32(state);
  state->state += seed;
  (void)talos_rt_v1_random_state_next32(state);
}

extern "C" uint32_t
talos_rt_v1_random_state_next32(talos_rt_random_state_v1 *state) {
  if (!state)
    return 0;
  uint64_t old = state->state;
  state->state = old * kMultiplier + state->increment;
  uint32_t shifted = static_cast<uint32_t>(((old >> 18) ^ old) >> 27);
  uint32_t rotation = static_cast<uint32_t>(old >> 59);
  return (shifted >> rotation) | (shifted << ((0u - rotation) & 31));
}

extern "C" uint64_t
talos_rt_v1_random_state_next64(talos_rt_random_state_v1 *state) {
  return state ? next64(*state) : 0;
}

extern "C" talos_rt_status
talos_rt_v1_random_cycle_next(uint64_t key, uint64_t position, uint32_t width,
                                uint64_t *outNextPosition, uint64_t *outValue) {
  if (!outNextPosition || !outValue || width == 0 || width > 32 ||
      (position & ~lowMask(width)) != 0)
    return TALOS_RT_INVALID_ARGUMENT;
  uint64_t mask = lowMask(width);
  *outValue = randcPermute(key, position, width);
  *outNextPosition = (position + 1) & mask;
  return TALOS_RT_OK;
}

extern "C" talos_rt_status
talos_rt_v1_random_state_bounded(talos_rt_random_state_v1 *state,
                                   uint64_t bound, uint64_t *outValue) {
  if (!state || !outValue || bound == 0 || (state->increment & 1) == 0)
    return TALOS_RT_INVALID_ARGUMENT;
  uint64_t threshold = (uint64_t{0} - bound) % bound;
  uint64_t value;
  do {
    value = next64(*state);
  } while (value < threshold);
  *outValue = value % bound;
  return TALOS_RT_OK;
}

talos_rt_random_state_v1 *
talos_rt_random_active_state_unlocked(talos_rt_context *context) {
  if (context && context->activeRandom)
    return context->activeRandom;
  if (!context || context->activeLogicalProcessToken == 0)
    return context ? &context->random : nullptr;
  uint64_t token = context->activeLogicalProcessToken;
  if ((token & TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0) {
    token &= ~TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
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

static talos_rt_random_state_v1 *
processRandomStateUnlocked(talos_rt_context *context,
                           uint64_t logicalProcess) {
  if (!context || logicalProcess == 0)
    return nullptr;
  if (context->activeLogicalProcessToken == logicalProcess &&
      context->activeRandom)
    return context->activeRandom;
  if ((logicalProcess & TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0) {
    uint64_t token = logicalProcess & ~TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG;
    for (ScheduledProcess &process : context->scheduledProcesses)
      if (process.token == token && process.instance)
        return &process.random;
    return context->terminatedNativeProcesses.randomState(token);
  }
  for (ScheduledDesignTask &task : context->scheduledDesignTasks)
    if (task.id == logicalProcess && !task.terminated)
      return &task.random;
  return context->terminatedDesignTasks.randomState(logicalProcess);
}

void talos_rt_random_split_unlocked(talos_rt_context *context,
                                      talos_rt_random_state_v1 &child) {
  talos_rt_random_state_v1 *parent =
      talos_rt_random_active_state_unlocked(context);
  uint64_t seed = next64(*parent);
  uint64_t sequence = next64(*parent);
  talos_rt_v1_random_state_seed(&child, seed, sequence);
}

uint64_t
talos_rt_random_initialization_scope_unlocked(talos_rt_context *context) {
  return context->activeLogicalProcessToken == 0 &&
                 context->initializationRandomScopes
             ? context->initializationRandomScopes->activeScope
             : 0;
}

void talos_rt_random_split_initialization_unlocked(
    talos_rt_context *context, uint64_t scope,
    talos_rt_random_state_v1 &child) {
  auto &parent = scope ? context->initializationRandomScopes->streams.at(scope)
                       : context->random;
  uint64_t seed = next64(parent);
  uint64_t sequence = next64(parent);
  talos_rt_v1_random_state_seed(&child, seed, sequence);
}

extern "C" talos_rt_status talos_rt_v1_random_initialization_enter(
    talos_rt_context *context, uint64_t scope, uint64_t *outActivation) {
  if (!context || scope == 0 || !outActivation)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (!context->initializationRandomScopes)
      context->initializationRandomScopes =
          std::make_unique<InitializationRandomScopes>();
    auto &scopes = *context->initializationRandomScopes;
    if (scopes.nextToken == 0)
      return TALOS_RT_OUT_OF_RESOURCES;
    // Allocate before publishing any binding or consuming the stream.
    scopes.activations.reserve(scopes.activations.size() + 1);
    auto [stream, inserted] = scopes.streams.try_emplace(scope);
    if (inserted)
      talos_rt_v1_random_state_seed(&stream->second, context->configuredSeed,
                                      kDefaultSequence);
    uint64_t token = scopes.nextToken++;
    scopes.activations.push_back(
        {token, scopes.activeScope, context->activeLogicalProcessToken,
         context->activeLogicalProcessParent, context->activeRandom});
    scopes.activeScope = scope;
    context->activeLogicalProcessToken = 0;
    context->activeLogicalProcessParent = 0;
    context->activeRandom = &stream->second;
    *outActivation = token;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_OUT_OF_MEMORY; }
}

extern "C" talos_rt_status
talos_rt_v1_random_initialization_leave(talos_rt_context *context,
                                          uint64_t activation) {
  if (!context || activation == 0)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (!context->initializationRandomScopes)
      return TALOS_RT_INVALID_LIFECYCLE;
    auto &scopes = *context->initializationRandomScopes;
    if (scopes.activations.empty() ||
        scopes.activations.back().token != activation ||
        context->activeLogicalProcessToken != 0)
      return TALOS_RT_INVALID_LIFECYCLE;
    auto previous = scopes.activations.back();
    context->activeRandom = previous.previousRandom;
    context->activeLogicalProcessToken = previous.previousProcess;
    context->activeLogicalProcessParent = previous.previousParent;
    scopes.activeScope = previous.previousScope;
    scopes.activations.pop_back();
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

void talos_rt_random_seed_context_unlocked(talos_rt_context *context,
                                             uint64_t seed) {
  talos_rt_v1_random_state_seed(&context->random, seed, kDefaultSequence);
}

extern "C" talos_rt_status
talos_rt_v1_random_next(talos_rt_context *context, uint64_t *outValue) {
  if (!context || !outValue)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    *outValue = next64(*talos_rt_random_active_state_unlocked(context));
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

extern "C" talos_rt_status
talos_rt_v1_random_seed(talos_rt_context *context, uint64_t seed) {
  if (!context)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_random_state_v1 *state =
        talos_rt_random_active_state_unlocked(context);
    // IEEE 1800-2023 18.13.1 makes an explicit seed determine the complete
    // sequence, independent of this thread's inherited stream selector. Use
    // the same canonical mapping as object and process srandom().
    talos_rt_v1_random_state_seed(state, seed, kDefaultSequence);
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

extern "C" talos_rt_status
talos_rt_v1_random_bounded(talos_rt_context *context, uint64_t bound,
                             uint64_t *outValue) {
  if (!context || !outValue || bound == 0)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    return talos_rt_v1_random_state_bounded(
        talos_rt_random_active_state_unlocked(context), bound, outValue);
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

namespace {

// IEEE 1800-2017 Annex N defines a separate 32-bit generator for the
// probabilistic distribution functions. Keep its float-bit construction and
// arithmetic order intact so both the returned variate and inout seed match
// the normative algorithm.
double annexUniform(int32_t &seed, int32_t start, int32_t end) {
  double lower = start >= end ? 0.0 : static_cast<double>(start);
  double upper = start >= end ? 2147483647.0 : static_cast<double>(end);
  uint32_t bits = seed == 0 ? UINT32_C(259341593) : static_cast<uint32_t>(seed);
  bits = UINT32_C(69069) * bits + 1;
  static_assert(sizeof(seed) == sizeof(bits));
  std::memcpy(&seed, &bits, sizeof(seed));
  uint32_t sampleBits = (bits >> 9) | UINT32_C(0x3f800000);
  float sampleFloat = 0.0f;
  static_assert(sizeof(sampleFloat) == sizeof(sampleBits));
  std::memcpy(&sampleFloat, &sampleBits, sizeof(sampleFloat));
  double sample = static_cast<double>(sampleFloat);
  sample += sample * 0.00000011920928955078125;
  return ((upper - lower) * (sample - 1.0)) + lower;
}

double annexNormal(int32_t &seed, int32_t mean, int32_t deviation) {
  double first;
  double second;
  double sum = 1.0;
  while (sum >= 1.0 || sum == 0.0) {
    first = annexUniform(seed, -1, 1);
    second = annexUniform(seed, -1, 1);
    sum = first * first + second * second;
  }
  double normal = first * std::sqrt(-2.0 * std::log(sum) / sum);
  return normal * static_cast<double>(deviation) + static_cast<double>(mean);
}

double annexExponential(int32_t &seed, int32_t mean) {
  double draw = annexUniform(seed, 0, 1);
  return draw != 0.0 ? -std::log(draw) * static_cast<double>(mean) : draw;
}

int32_t annexPoisson(int32_t &seed, int32_t mean) {
  int32_t count = 0;
  double limit = std::exp(-static_cast<double>(mean));
  double product = annexUniform(seed, 0, 1);
  while (limit < product) {
    ++count;
    product = annexUniform(seed, 0, 1) * product;
  }
  return count;
}

double annexChiSquare(int32_t &seed, int32_t freedom) {
  double value = 0.0;
  if (freedom % 2) {
    value = annexNormal(seed, 0, 1);
    value *= value;
  }
  for (int32_t index = 2; index <= freedom; index += 2)
    value += 2.0 * annexExponential(seed, 1);
  return value;
}

double annexT(int32_t &seed, int32_t freedom) {
  double chi = annexChiSquare(seed, freedom);
  return annexNormal(seed, 0, 1) /
         std::sqrt(chi / static_cast<double>(freedom));
}

double annexErlang(int32_t &seed, int32_t stages, int32_t mean) {
  double product = 1.0;
  for (int32_t index = 1; index <= stages; ++index)
    product *= annexUniform(seed, 0, 1);
  return -static_cast<double>(mean) * std::log(product) /
         static_cast<double>(stages);
}

int32_t annexRound(double value) {
  if (value >= 0.0)
    return static_cast<int32_t>(value + 0.5);
  return -static_cast<int32_t>(-value + 0.5);
}

int32_t annexDistUniform(int32_t &seed, int32_t start, int32_t end) {
  if (start >= end)
    return start;
  double draw;
  int32_t result;
  if (end != std::numeric_limits<int32_t>::max()) {
    int32_t exclusiveEnd = end + 1;
    draw = annexUniform(seed, start, exclusiveEnd);
    result = draw >= 0.0 ? static_cast<int32_t>(draw)
                         : static_cast<int32_t>(draw - 1.0);
    if (result < start)
      result = start;
    if (result >= exclusiveEnd)
      result = exclusiveEnd - 1;
  } else if (start != std::numeric_limits<int32_t>::min()) {
    int32_t exclusiveStart = start - 1;
    draw = annexUniform(seed, exclusiveStart, end) + 1.0;
    result = draw >= 0.0 ? static_cast<int32_t>(draw)
                         : static_cast<int32_t>(draw - 1.0);
    if (result <= exclusiveStart)
      result = exclusiveStart + 1;
    if (result > end)
      result = end;
  } else {
    draw = (annexUniform(seed, start, end) + 2147483648.0) / 4294967295.0;
    draw = draw * 4294967296.0 - 2147483648.0;
    result = draw >= 0.0 ? static_cast<int32_t>(draw)
                         : static_cast<int32_t>(draw - 1.0);
  }
  return result;
}

} // namespace

extern "C" talos_rt_status
talos_rt_v1_random_distribution(talos_rt_context *context,
                                  talos_rt_distribution distribution,
                                  int32_t seed, int32_t first, int32_t second,
                                  int32_t *outValue, int32_t *outNextSeed) {
  if (!context || !outValue || !outNextSeed)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    double value = 0.0;
    switch (distribution) {
    case TALOS_RT_DISTRIBUTION_UNIFORM:
      *outValue = annexDistUniform(seed, first, second);
      *outNextSeed = seed;
      return TALOS_RT_OK;
    case TALOS_RT_DISTRIBUTION_NORMAL:
      value = annexNormal(seed, first, second);
      break;
    case TALOS_RT_DISTRIBUTION_EXPONENTIAL:
      value = first > 0 ? annexExponential(seed, first) : 0.0;
      break;
    case TALOS_RT_DISTRIBUTION_POISSON:
      *outValue = first > 0 ? annexPoisson(seed, first) : 0;
      *outNextSeed = seed;
      return TALOS_RT_OK;
    case TALOS_RT_DISTRIBUTION_CHI_SQUARE:
      value = first > 0 ? annexChiSquare(seed, first) : 0.0;
      break;
    case TALOS_RT_DISTRIBUTION_T:
      value = first > 0 ? annexT(seed, first) : 0.0;
      break;
    case TALOS_RT_DISTRIBUTION_ERLANG:
      value = first > 0 ? annexErlang(seed, first, second) : 0.0;
      break;
    default:
      return TALOS_RT_INVALID_ARGUMENT;
    }
    *outValue = annexRound(value);
    *outNextSeed = seed;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

extern "C" talos_rt_status
talos_rt_v1_random_legacy(talos_rt_context *context, int32_t *outValue) {
  if (!context || !outValue)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    *outValue = annexDistUniform(context->legacyRandomSeed,
                                 std::numeric_limits<int32_t>::min(),
                                 std::numeric_limits<int32_t>::max());
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

extern "C" talos_rt_status
talos_rt_v1_random_get_state(talos_rt_context *context,
                               talos_rt_random_state_v1 *outState) {
  if (!context || !outState)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    *outState = *talos_rt_random_active_state_unlocked(context);
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

extern "C" talos_rt_status
talos_rt_v1_random_set_state(talos_rt_context *context,
                               const talos_rt_random_state_v1 *state) {
  if (!context || !state || (state->increment & 1) == 0)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    *talos_rt_random_active_state_unlocked(context) = *state;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

extern "C" talos_rt_status
talos_rt_v1_process_random_get(talos_rt_context *context,
                                 uint64_t logicalProcess,
                                 talos_rt_random_state_v1 *outState) {
  if (!context || logicalProcess == 0 || !outState)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_random_state_v1 *state =
        processRandomStateUnlocked(context, logicalProcess);
    if (!state)
      return TALOS_RT_INVALID_HANDLE;
    *outState = *state;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

extern "C" talos_rt_status
talos_rt_v1_process_random_set(talos_rt_context *context,
                                 uint64_t logicalProcess,
                                 const talos_rt_random_state_v1 *state) {
  if (!context || logicalProcess == 0 || !state || (state->increment & 1) == 0)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_random_state_v1 *target =
        processRandomStateUnlocked(context, logicalProcess);
    if (!target)
      return TALOS_RT_INVALID_HANDLE;
    *target = *state;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}
