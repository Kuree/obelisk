//===- StochasticQueue.cpp - IEEE queueing-analysis utilities -----------===//

#include "RuntimeInternal.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>

namespace {

constexpr uint32_t kFIFO = 1;
constexpr uint32_t kLIFO = 2;

uint64_t scaleTime(uint64_t ticks, uint64_t scale) {
  uint64_t quotient = ticks / scale;
  uint64_t remainder = ticks % scale;
  uint64_t halfway = scale / 2 + scale % 2;
  if (remainder >= halfway && quotient != UINT64_MAX)
    ++quotient;
  return quotient;
}

uint64_t entryIndex(const StochasticQueueState &queue, uint64_t ordinal) {
  uint64_t index = queue.head + ordinal;
  if (index >= queue.maximumLength)
    index -= queue.maximumLength;
  return index;
}

void setUnknown(uint64_t *value, uint64_t *unknown) {
  *value = 0;
  *unknown = UINT64_MAX;
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_stochastic_queue(
    obelisk_rt_context *context, obelisk_rt_stochastic_queue_action_v1 action,
    uint32_t idValue, uint32_t idUnknown, uint32_t firstValue,
    uint32_t firstUnknown, uint32_t secondValue, uint32_t secondUnknown,
    uint64_t unitScale, uint64_t *outPrimaryValue, uint64_t *outPrimaryUnknown,
    uint64_t *outSecondaryValue, uint64_t *outSecondaryUnknown,
    obelisk_rt_stochastic_queue_status_v1 *outQueueStatus) {
  if (!context || !unitScale || !outPrimaryValue || !outPrimaryUnknown ||
      !outSecondaryValue || !outSecondaryUnknown || !outQueueStatus ||
      action > OBELISK_RT_STOCHASTIC_QUEUE_EXAM)
    return OBELISK_RT_INVALID_ARGUMENT;

  setUnknown(outPrimaryValue, outPrimaryUnknown);
  setUnknown(outSecondaryValue, outSecondaryUnknown);
  *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_OK;

  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (idUnknown) {
      *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_UNDEFINED_ID;
      return OBELISK_RT_OK;
    }
    int32_t id = static_cast<int32_t>(idValue);

    if (action == OBELISK_RT_STOCHASTIC_QUEUE_INITIALIZE) {
      if (firstUnknown || (firstValue != kFIFO && firstValue != kLIFO)) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_UNSUPPORTED_TYPE;
        return OBELISK_RT_OK;
      }
      if (secondUnknown || static_cast<int32_t>(secondValue) <= 0) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_INVALID_LENGTH;
        return OBELISK_RT_OK;
      }
      if (context->stochasticQueues.count(id)) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_DUPLICATE_ID;
        return OBELISK_RT_OK;
      }
      try {
        StochasticQueueState queue;
        queue.type = firstValue;
        queue.maximumLength = secondValue;
        queue.entries.reserve(secondValue);
        context->stochasticQueues.emplace(id, std::move(queue));
      } catch (const std::bad_alloc &) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_OUT_OF_MEMORY;
      } catch (const std::length_error &) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_OUT_OF_MEMORY;
      }
      return OBELISK_RT_OK;
    }

    auto found = context->stochasticQueues.find(id);
    if (found == context->stochasticQueues.end()) {
      *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_UNDEFINED_ID;
      return OBELISK_RT_OK;
    }
    StochasticQueueState &queue = found->second;
    uint64_t now = context->schedulerTime;

    if (action == OBELISK_RT_STOCHASTIC_QUEUE_ADD) {
      if (queue.count == queue.maximumLength) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_FULL_STATUS;
        return OBELISK_RT_OK;
      }
      uint64_t index = entryIndex(queue, queue.count);
      StochasticQueueEntry entry;
      entry.jobValue = firstValue;
      entry.jobUnknown = firstUnknown;
      entry.informValue = secondValue;
      entry.informUnknown = secondUnknown;
      entry.addTime = now;
      if (index == queue.entries.size())
        queue.entries.push_back(entry);
      else
        queue.entries[index] = entry;
      ++queue.count;
      queue.maximumCount = std::max(queue.maximumCount, queue.count);
      if (queue.numberOfAdds == 0)
        queue.firstAddTime = now;
      queue.latestAddTime = now;
      if (queue.numberOfAdds != UINT64_MAX)
        ++queue.numberOfAdds;
      return OBELISK_RT_OK;
    }

    if (action == OBELISK_RT_STOCHASTIC_QUEUE_REMOVE) {
      if (queue.count == 0) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_EMPTY;
        return OBELISK_RT_OK;
      }
      uint64_t ordinal = queue.type == kFIFO ? 0 : queue.count - 1;
      uint64_t index = entryIndex(queue, ordinal);
      const StochasticQueueEntry &entry = queue.entries[index];
      *outPrimaryValue = entry.jobValue;
      *outPrimaryUnknown = entry.jobUnknown;
      *outSecondaryValue = entry.informValue;
      *outSecondaryUnknown = entry.informUnknown;
      uint64_t waited = now - entry.addTime;
      queue.completedWait += waited;
      queue.shortestWait = std::min(queue.shortestWait, waited);
      queue.haveShortestWait = true;
      if (queue.type == kFIFO && ++queue.head == queue.maximumLength)
        queue.head = 0;
      --queue.count;
      return OBELISK_RT_OK;
    }

    if (action == OBELISK_RT_STOCHASTIC_QUEUE_FULL) {
      *outPrimaryValue = queue.count == queue.maximumLength ? 1 : 0;
      *outPrimaryUnknown = 0;
      return OBELISK_RT_OK;
    }

    if (firstUnknown || firstValue < 1 || firstValue > 6) {
      *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_UNDEFINED_STATISTIC;
      return OBELISK_RT_OK;
    }

    uint64_t statistic = 0;
    switch (firstValue) {
    case 1:
      statistic = queue.count;
      break;
    case 2:
      if (queue.numberOfAdds < 2) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_NO_STATISTICS;
        return OBELISK_RT_OK;
      }
      statistic = scaleTime((queue.latestAddTime - queue.firstAddTime) /
                                (queue.numberOfAdds - 1),
                            unitScale);
      break;
    case 3:
      statistic = queue.maximumCount;
      break;
    case 4:
      if (!queue.haveShortestWait) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_NO_STATISTICS;
        return OBELISK_RT_OK;
      }
      statistic = scaleTime(queue.shortestWait, unitScale);
      break;
    case 5: {
      if (queue.count == 0) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_NO_STATISTICS;
        return OBELISK_RT_OK;
      }
      uint64_t oldest = now;
      for (uint64_t ordinal = 0; ordinal < queue.count; ++ordinal)
        oldest =
            std::min(oldest, queue.entries[entryIndex(queue, ordinal)].addTime);
      statistic = scaleTime(now - oldest, unitScale);
      break;
    }
    case 6: {
      if (queue.numberOfAdds == 0) {
        *outQueueStatus = OBELISK_RT_STOCHASTIC_QUEUE_NO_STATISTICS;
        return OBELISK_RT_OK;
      }
      unsigned __int128 total = queue.completedWait;
      for (uint64_t ordinal = 0; ordinal < queue.count; ++ordinal)
        total += now - queue.entries[entryIndex(queue, ordinal)].addTime;
      uint64_t average = static_cast<uint64_t>(total / queue.numberOfAdds);
      statistic = scaleTime(average, unitScale);
      break;
    }
    default:
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    *outPrimaryValue = statistic;
    *outPrimaryUnknown = 0;
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}
