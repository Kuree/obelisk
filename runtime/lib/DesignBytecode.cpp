//===- DesignBytecode.cpp - Design-wide validated bytecode interpreter ----===//

#include "DesignBytecodeExecution.h"
#include "DesignBytecodeImage.h"
#include "DesignBytecodeLogic.h"
#include "DesignBytecodeNets.h"
#include "DesignBytecodeRoots.h"
#include "ProcessPacking.h"
#include "ProcessShared.h"
#include "ProcessSignals.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

decltype(&obelisk_rt_v1_dpi_aggregate_state_alloc)
    designBytecodeDpiAggregateStateAlloc = nullptr;

namespace {

using namespace obelisk::designbytecode;

size_t checkedSizeSum(size_t lhs, size_t rhs) {
  if (rhs > std::numeric_limits<size_t>::max() - lhs)
    obelisk_rt_out_of_memory();
  return lhs + rhs;
}

bool hasSameDirectSignalWait(const ScheduledDesignTask &task,
                             const obelisk_rt_wait_record_v1 *wait) {
  if (!wait || !task.signalLatch ||
      task.signalSubscriptions.size() != wait->count)
    return false;
  const auto *entries =
      reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(wait + 1);
  bool suppressActiveSelf =
      (wait->flags & OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF) != 0;
  for (uint32_t index = 0; index != wait->count; ++index) {
    const SignalSubscription *subscription =
        task.signalSubscriptions[index].get();
    if (!subscription || subscription->stableID != entries[index].stable_id ||
        subscription->bitWidth != entries[index].reserved ||
        subscription->edge != entries[index].edge ||
        subscription->suppressActiveSelf != suppressActiveSelf ||
        subscription->target != SignalSubscription::DesignDirectWait)
      return false;
  }
  return true;
}

class ScopedReusableByteBuffer {
public:
  ScopedReusableByteBuffer(obelisk_rt_context *context, size_t size)
      : pool(context ? &context->designTaskFrames : nullptr),
        buffer(pool ? pool->acquire(size) : std::vector<uint8_t>(size)) {}
  ScopedReusableByteBuffer(const ScopedReusableByteBuffer &) = delete;
  ScopedReusableByteBuffer &
  operator=(const ScopedReusableByteBuffer &) = delete;
  ~ScopedReusableByteBuffer() {
    if (pool)
      pool->release(std::move(buffer));
  }

  uint8_t *data() { return buffer.data(); }

private:
  ReusableByteBufferPool *pool;
  std::vector<uint8_t> buffer;
};

class ScopedCopyMapBuffer {
public:
  ScopedCopyMapBuffer(obelisk_rt_context *context, size_t size) {
    if (size > inlineBuffer.size())
      overflow.emplace(context, size);
  }
  ScopedCopyMapBuffer(const ScopedCopyMapBuffer &) = delete;
  ScopedCopyMapBuffer &operator=(const ScopedCopyMapBuffer &) = delete;

  uint8_t *data() { return overflow ? overflow->data() : inlineBuffer.data(); }

private:
  // Most block maps only move a handful of scalar values. Keeping that
  // snapshot inline avoids both allocator traffic and pressure on the shared
  // frame pool; unusually wide maps still reuse a context-owned buffer.
  std::array<uint8_t, 256> inlineBuffer;
  std::optional<ScopedReusableByteBuffer> overflow;
};

struct PendingDesignActivation {
  DesignActivation activation;
  obelisk_rt_context *context = nullptr;
  std::vector<std::pair<uint32_t, uint64_t>> retainedAutomaticStates;
  bool ownsRetainedAutomaticStates = false;

  ~PendingDesignActivation() noexcept;
  void disarm() noexcept {
    ownsRetainedAutomaticStates = false;
    context = nullptr;
    retainedAutomaticStates.clear();
  }
};

obelisk_rt_status releaseCapturedAutomaticStates(const Image &image,
                                                 uint32_t functionIndex,
                                                 obelisk_rt_context *context,
                                                 const uint8_t *canonicalFrame,
                                                 uint64_t canonicalFrameSize) {
  if (!context || !canonicalFrame)
    return OBELISK_RT_OK;
  Function function = functionAt(image, functionIndex);
  if ((function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0)
    return OBELISK_RT_OK;
  auto stableAt = [&](uint64_t index, uint64_t &stable,
                      bool &hasHandle) -> obelisk_rt_status {
    hasHandle = false;
    stable = UINT64_MAX;
    CaptureRecord capture = captureAt(image, index);
    if (capture.function != functionIndex)
      return OBELISK_RT_OK;
    Layout layout = layoutAt(image, function, capture.argument);
    if (layout.kind != OBELISK_RT_DBREG_HANDLE ||
        capture.valueOffset == UINT64_MAX)
      return OBELISK_RT_OK;
    if (capture.valueOffset > canonicalFrameSize ||
        8 > canonicalFrameSize - capture.valueOffset)
      return OBELISK_RT_INVALID_FRAME;
    std::memcpy(&stable, canonicalFrame + capture.valueOffset, 8);
    hasHandle = stable != UINT64_MAX;
    return OBELISK_RT_OK;
  };

  // Validate every captured handle before changing any reference count.
  for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
    uint64_t stable = UINT64_MAX;
    bool hasHandle = false;
    obelisk_rt_status status = stableAt(index, stable, hasHandle);
    if (status != OBELISK_RT_OK)
      return status;
    if (!hasHandle)
      continue;
    uint32_t id = 0;
    int64_t offset = 0;
    if (decodeAutomaticHandle(stable, id, offset))
      continue;
    if (decodeStaticHandle(stable, id, offset))
      continue;
    if (isDynamicEventStableHandle(stable))
      continue;
    if (!decodeGlobalHandle(stable, offset))
      return OBELISK_RT_INVALID_HANDLE;
  }

  // Decrement in place without allocating. If a duplicate capture exceeds
  // the available count, restore all earlier decrements before failing.
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto rollback = [&](uint64_t end) {
    for (uint64_t index = 0; index != end; ++index) {
      uint64_t stable = UINT64_MAX;
      bool hasHandle = false;
      if (stableAt(index, stable, hasHandle) != OBELISK_RT_OK || !hasHandle)
        continue;
      uint32_t id = 0;
      int64_t offset = 0;
      if (!decodeAutomaticHandle(stable, id, offset))
        continue;
      auto found = context->nativeAutomaticStates.find(id);
      if (found != context->nativeAutomaticStates.end())
        ++found->second.referenceCount;
    }
  };
  for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
    uint64_t stable = UINT64_MAX;
    bool hasHandle = false;
    (void)stableAt(index, stable, hasHandle);
    if (!hasHandle)
      continue;
    uint32_t id = 0;
    int64_t offset = 0;
    if (!decodeAutomaticHandle(stable, id, offset))
      continue;
    auto found = context->nativeAutomaticStates.find(id);
    if (found == context->nativeAutomaticStates.end() ||
        found->second.referenceCount == 0) {
      rollback(index);
      return OBELISK_RT_INVALID_HANDLE;
    }
    --found->second.referenceCount;
  }
  for (auto state = context->nativeAutomaticStates.begin();
       state != context->nativeAutomaticStates.end();)
    if (state->second.referenceCount == 0) {
      obelisk_rt_erase_automatic_bookkeeping_unlocked(context, state->first);
      state = context->nativeAutomaticStates.erase(state);
    } else
      ++state;
  return OBELISK_RT_OK;
}

PendingDesignActivation::~PendingDesignActivation() noexcept {
  if (!context)
    return;
  if (ownsRetainedAutomaticStates) {
    OBELISK_RT_TRY {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      for (const auto &[id, count] : retainedAutomaticStates) {
        auto found = context->nativeAutomaticStates.find(id);
        if (found == context->nativeAutomaticStates.end())
          continue;
        if (count <= found->second.referenceCount)
          found->second.referenceCount -= count;
      }
      for (auto state = context->nativeAutomaticStates.begin();
           state != context->nativeAutomaticStates.end();)
        if (state->second.referenceCount == 0) {
          obelisk_rt_erase_automatic_bookkeeping_unlocked(context,
                                                          state->first);
          state = context->nativeAutomaticStates.erase(state);
        } else {
          ++state;
        }
    }
    OBELISK_RT_CATCH_ALL {
      // Destructors on error paths must not obscure the scheduler failure.
    }
  }
  context->designTaskFrames.release(std::move(activation.frame));
}

void releaseDesignTaskOwnedStatesUnlocked(obelisk_rt_context *context,
                                          uint64_t taskID) {
  for (auto state = context->nativeAutomaticStates.begin();
       state != context->nativeAutomaticStates.end();) {
    if (state->second.designOwner != taskID) {
      ++state;
      continue;
    }
    state->second.designOwner = 0;
    if (state->second.referenceCount <= 1) {
      obelisk_rt_erase_automatic_bookkeeping_unlocked(context, state->first);
      state = context->nativeAutomaticStates.erase(state);
    } else {
      --state->second.referenceCount;
      ++state;
    }
  }
}

struct ExecutionState {
  static constexpr size_t kMaxFrameCount = 1026;
  uint32_t callDepth = 0;
  std::array<Frame *, kMaxFrameCount> frames;
};

bool copyRegister(const Image &image, const Frame &source, uint32_t sourceIndex,
                  Frame &destination, uint32_t destinationIndex) {
  if (!validRegister(source.function, sourceIndex) ||
      !validRegister(destination.function, destinationIndex))
    return false;
  Layout sourceLayout = layoutAt(image, source.function, sourceIndex);
  Layout destinationLayout =
      layoutAt(image, destination.function, destinationIndex);
  if (!compatible(sourceLayout, destinationLayout))
    return false;
  std::memmove(destination.data + destinationLayout.offset,
               source.data + sourceLayout.offset, sourceLayout.size);
  return true;
}

bool readKnownScalar(const Image &image, const Frame &frame, uint32_t reg,
                     uint64_t &value) {
  Layout layout = layoutAt(image, frame.function, reg);
  if ((layout.kind != OBELISK_RT_DBREG_BITS &&
       layout.kind != OBELISK_RT_DBREG_LOGIC) ||
      layout.width == 0 || layout.width > 64)
    return false;
  value = 0;
  std::memcpy(&value, frame.data + layout.offset,
              static_cast<size_t>(std::min<uint64_t>(layout.size, 8)));
  if (layout.kind == OBELISK_RT_DBREG_LOGIC) {
    uint64_t unknown = 0;
    std::memcpy(&unknown, frame.data + layout.offset + 8, sizeof(unknown));
    if (unknown != 0)
      return false;
  }
  value &= finalMask(layout.width);
  return true;
}

bool writeKnownScalar(const Image &image, Frame &frame, uint32_t reg,
                      uint64_t value) {
  Layout layout = layoutAt(image, frame.function, reg);
  if ((layout.kind != OBELISK_RT_DBREG_BITS &&
       layout.kind != OBELISK_RT_DBREG_LOGIC) ||
      layout.width == 0 || layout.width > 64)
    return false;
  value &= finalMask(layout.width);
  std::memcpy(frame.data + layout.offset, &value,
              static_cast<size_t>(std::min<uint64_t>(layout.size, 8)));
  if (layout.kind == OBELISK_RT_DBREG_LOGIC)
    std::memset(frame.data + layout.offset + 8, 0, 8);
  return true;
}

bool copyMap(const Image &image, const Frame &source, Frame &destination,
             uint64_t first, uint64_t count, obelisk_rt_context *context) {
  if (first > image.operandCount || count > image.operandCount - first)
    return false;
  if (count == 0)
    return true;

  // Snapshot sources to make parallel block-argument assignment well defined.
  size_t byteCount = 0;
  for (uint64_t index = 0; index != count; ++index) {
    auto [destinationRegister, sourceRegister] =
        operandAt(image, first + index);
    (void)destinationRegister;
    if (!validRegister(source.function, sourceRegister))
      return false;
    Layout layout = layoutAt(image, source.function, sourceRegister);
    if (layout.size > std::numeric_limits<size_t>::max())
      obelisk_rt_out_of_memory();
    byteCount = checkedSizeSum(byteCount, static_cast<size_t>(layout.size));
  }
  ScopedCopyMapBuffer values(context, byteCount);
  size_t valueOffset = 0;
  for (uint64_t index = 0; index != count; ++index) {
    auto [destinationRegister, sourceRegister] =
        operandAt(image, first + index);
    (void)destinationRegister;
    Layout layout = layoutAt(image, source.function, sourceRegister);
    std::memcpy(values.data() + valueOffset, source.data + layout.offset,
                static_cast<size_t>(layout.size));
    valueOffset += static_cast<size_t>(layout.size);
  }
  valueOffset = 0;
  for (uint64_t index = 0; index != count; ++index) {
    auto [destinationRegister, sourceRegister] =
        operandAt(image, first + index);
    if (!validRegister(destination.function, destinationRegister))
      return false;
    Layout layout = layoutAt(image, destination.function, destinationRegister);
    Layout sourceLayout = layoutAt(image, source.function, sourceRegister);
    if (layout.size != sourceLayout.size)
      return false;
    std::memcpy(destination.data + layout.offset, values.data() + valueOffset,
                static_cast<size_t>(layout.size));
    valueOffset += static_cast<size_t>(layout.size);
  }
  return true;
}

struct StepBudget {
  uint64_t limit = 0;
  uint64_t used = 0;
  bool consume() { return limit == 0 || ++used <= limit; }
};

obelisk_rt_status prepareTaskActivation(
    const Image &image, Frame &caller, obelisk_rt_context *context,
    uint32_t calleeIndex, uint32_t firstOperand, uint32_t operandCount,
    std::unique_ptr<PendingDesignActivation> *pendingActivation) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!pendingActivation || *pendingActivation)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (calleeIndex >= image.functionCount)
    return OBELISK_RT_INVALID_BYTECODE;
  Function callee = functionAt(image, calleeIndex);
  if ((callee.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
      callee.resultCount != 0 || callee.argumentCount != operandCount ||
      !validMap(image, caller.function, callee, firstOperand, operandCount))
    return OBELISK_RT_INVALID_BYTECODE;
  for (uint32_t index = 0; index != operandCount; ++index)
    if (operandAt(image, firstOperand + index).first != index)
      return OBELISK_RT_INVALID_BYTECODE;

  uint64_t canonicalSize =
      (callee.flags & OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK) >> 1;
  if (callee.scratchAlignment == 0 ||
      canonicalSize > UINT64_MAX - (callee.scratchAlignment - 1))
    return OBELISK_RT_INVALID_BYTECODE;
  uint64_t scratchOffset = (canonicalSize + callee.scratchAlignment - 1) &
                           ~(callee.scratchAlignment - 1);
  if (scratchOffset > UINT64_MAX - callee.scratchSize ||
      scratchOffset + callee.scratchSize > std::numeric_limits<size_t>::max())
    return OBELISK_RT_OUT_OF_MEMORY;
  auto pending = std::make_unique<PendingDesignActivation>();
  pending->context = context;
  DesignActivation &activation = pending->activation;
  activation.function = calleeIndex;
  activation.scheduleRank = static_cast<uint32_t>(callee.initialScheduleRank);
  activation.scratchOffset = scratchOffset;
  activation.scratchSize = callee.scratchSize;
  activation.frame = context->designTaskFrames.acquire(
      static_cast<size_t>(scratchOffset + callee.scratchSize));
  uint32_t copied = 0;
  std::unordered_map<uint32_t, uint64_t> retainedAutomaticStates;
  for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
    CaptureRecord capture = captureAt(image, index);
    if (capture.function != calleeIndex)
      continue;
    ++copied;
    if (capture.argument >= operandCount)
      return OBELISK_RT_INVALID_BYTECODE;
    if (capture.valueOffset == UINT64_MAX)
      continue;
    uint32_t sourceRegister =
        operandAt(image, firstOperand + capture.argument).second;
    Layout source = layoutAt(image, caller.function, sourceRegister);
    if (source.kind == OBELISK_RT_DBREG_HANDLE) {
      uint64_t stable = UINT64_MAX;
      if (!encodeCanonicalHandle(caller.data + source.offset, stable))
        return OBELISK_RT_INVALID_HANDLE;
      std::memcpy(activation.frame.data() + capture.valueOffset, &stable,
                  sizeof(stable));
      uint32_t automaticID = 0;
      int64_t automaticOffset = 0;
      if (decodeAutomaticHandle(stable, automaticID, automaticOffset) &&
          ++retainedAutomaticStates[automaticID] == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
      continue;
    }
    std::memcpy(activation.frame.data() + capture.valueOffset,
                caller.data + source.offset, capture.planeSize);
    if (capture.unknownOffset != UINT64_MAX) {
      uint64_t sourcePlane = source.kind == OBELISK_RT_DBREG_LOGIC
                                 ? limbCount(source.width) * sizeof(uint64_t)
                                 : capture.planeSize;
      std::memcpy(activation.frame.data() + capture.unknownOffset,
                  caller.data + source.offset + sourcePlane, capture.planeSize);
    }
  }
  if (copied != callee.argumentCount)
    return OBELISK_RT_INVALID_BYTECODE;
  pending->retainedAutomaticStates.reserve(retainedAutomaticStates.size());
  for (const auto &[automaticID, count] : retainedAutomaticStates)
    pending->retainedAutomaticStates.emplace_back(automaticID, count);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    for (const auto &[automaticID, count] : pending->retainedAutomaticStates) {
      auto found = context->nativeAutomaticStates.find(automaticID);
      if (found == context->nativeAutomaticStates.end())
        return OBELISK_RT_INVALID_HANDLE;
      if (count > UINT64_MAX - found->second.referenceCount)
        return OBELISK_RT_OUT_OF_RESOURCES;
    }
    for (const auto &[automaticID, count] : pending->retainedAutomaticStates)
      context->nativeAutomaticStates.find(automaticID)->second.referenceCount +=
          count;
  }
  pending->ownsRetainedAutomaticStates = true;
  *pendingActivation = std::move(pending);
  return OBELISK_RT_OK;
}

std::optional<uint64_t> continuationPC(const Image &image,
                                       const Function &function,
                                       uint32_t continuation);

obelisk_rt_status
executeFunction(const Image &image, Frame &frame, obelisk_rt_context *context,
                uint8_t *canonicalFrame, uint64_t canonicalFrameSize,
                uint64_t startPC, StepBudget &budget,
                obelisk_rt_fragment_action_v1 *action, ExecutionState &state,
                std::unique_ptr<PendingDesignActivation> *pendingActivation,
                uint64_t returnFirst = 0, uint64_t returnCount = 0,
                Frame *caller = nullptr) {
  ScopedBytecodeFrameRoots managedRoots(image, frame, context);
  if (managedRoots.getStatus() != OBELISK_RT_OK)
    return managedRoots.getStatus();
  uint64_t begin = frame.function.firstInstruction;
  uint64_t end = begin + frame.function.instructionCount;
  uint64_t pc = startPC;
  while (pc >= begin && pc < end) {
    if (!budget.consume())
      return OBELISK_RT_STEP_LIMIT;
    Instruction instruction = instructionAt(image, pc++);
    auto layout = [&](uint32_t reg) {
      return layoutAt(image, frame.function, reg);
    };
    auto read = [&](uint32_t reg) {
      return readLogic(frame.data, layout(reg));
    };
    auto write = [&](uint32_t reg, const Logic &value) {
      writeLogic(frame.data, layout(reg), value);
    };
    auto readFloat = [&](uint32_t reg) {
      float value = 0.0f;
      Layout valueLayout = layout(reg);
      std::memcpy(&value, frame.data + valueLayout.offset, sizeof(value));
      return value;
    };
    auto readDouble = [&](uint32_t reg) {
      double value = 0.0;
      Layout valueLayout = layout(reg);
      std::memcpy(&value, frame.data + valueLayout.offset, sizeof(value));
      return value;
    };
    auto writeFloat = [&](uint32_t reg, float value) {
      Layout valueLayout = layout(reg);
      std::memcpy(frame.data + valueLayout.offset, &value, sizeof(value));
    };
    auto writeDouble = [&](uint32_t reg, double value) {
      Layout valueLayout = layout(reg);
      std::memcpy(frame.data + valueLayout.offset, &value, sizeof(value));
    };
    switch (instruction.opcode) {
    case OBELISK_RT_DB_NOP:
      break;
    case OBELISK_RT_DB_CONSTANT: {
      Layout destination = layout(instruction.destination);
      std::memcpy(frame.data + destination.offset,
                  image.data + image.constants + instruction.immediate,
                  destination.size);
      if ((destination.kind == OBELISK_RT_DBREG_BITS ||
           destination.kind == OBELISK_RT_DBREG_LOGIC) &&
          destination.width % 64 != 0) {
        uint64_t limbs = limbCount(destination.width);
        uint64_t last = 0;
        uint64_t lastOffset = destination.offset + (limbs - 1) * 8;
        std::memcpy(&last, frame.data + lastOffset, sizeof(last));
        last &= finalMask(destination.width);
        std::memcpy(frame.data + lastOffset, &last, sizeof(last));
        if (destination.kind == OBELISK_RT_DBREG_LOGIC) {
          uint64_t unknownOffset = destination.offset + limbs * 8;
          std::memcpy(&last, frame.data + unknownOffset + (limbs - 1) * 8,
                      sizeof(last));
          last &= finalMask(destination.width);
          std::memcpy(frame.data + unknownOffset + (limbs - 1) * 8, &last,
                      sizeof(last));
        }
      }
      break;
    }
    case OBELISK_RT_DB_MOVE:
      if (!copyRegister(image, frame, instruction.source0, frame,
                        instruction.destination))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_DB_BITCAST: {
      Layout source = layout(instruction.source0);
      Layout destination = layout(instruction.destination);
      if (!bitcastCompatible(destination, source))
        return OBELISK_RT_INVALID_BYTECODE;
      std::memset(frame.data + destination.offset, 0, destination.size);
      std::memcpy(frame.data + destination.offset, frame.data + source.offset,
                  std::min(source.size, destination.size));
      break;
    }
    case OBELISK_RT_DB_NOT: {
      Logic input = read(instruction.source0);
      for (size_t index = 0; index != input.value.size(); ++index)
        input.value[index] = ~input.value[index] & ~input.unknown[index];
      mask(input);
      write(instruction.destination, input);
      break;
    }
    case OBELISK_RT_DB_REDUCE: {
      Logic input = read(instruction.source0);
      Layout destination = layout(instruction.destination);
      Logic result{1, destination.kind == OBELISK_RT_DBREG_LOGIC, {0}, {0}};
      bool anyKnownOne = false, anyKnownZero = false, unknown = false;
      bool parity = false;
      for (uint64_t bitIndex = 0; bitIndex != input.width; ++bitIndex) {
        bool u = bit(input.unknown, bitIndex);
        bool v = bit(input.value, bitIndex);
        unknown |= u;
        anyKnownOne |= !u && v;
        anyKnownZero |= !u && !v;
        if (!u)
          parity ^= v;
      }
      bool value = false, resultUnknown = false;
      switch (instruction.flags) {
      case OBELISK_RT_DB_REDUCE_AND:
        value = !anyKnownZero && !unknown;
        resultUnknown = !anyKnownZero && unknown;
        break;
      case OBELISK_RT_DB_REDUCE_OR:
        value = anyKnownOne;
        resultUnknown = !anyKnownOne && unknown;
        break;
      case OBELISK_RT_DB_REDUCE_XOR:
        value = parity && !unknown;
        resultUnknown = unknown;
        break;
      case OBELISK_RT_DB_REDUCE_NAND:
        value = anyKnownZero;
        resultUnknown = !anyKnownZero && unknown;
        break;
      case OBELISK_RT_DB_REDUCE_NOR:
        value = !anyKnownOne && !unknown;
        resultUnknown = !anyKnownOne && unknown;
        break;
      case OBELISK_RT_DB_REDUCE_XNOR:
        value = !parity && !unknown;
        resultUnknown = unknown;
        break;
      case OBELISK_RT_DB_REDUCE_IS_TRUE:
        value = anyKnownOne;
        resultUnknown = false;
        break;
      case OBELISK_RT_DB_REDUCE_LOGICAL_NOT:
        value = !anyKnownOne && !unknown;
        resultUnknown = !anyKnownOne && unknown;
        break;
      case OBELISK_RT_DB_REDUCE_LOGICAL_VALUE:
        value = anyKnownOne;
        resultUnknown = !anyKnownOne && unknown;
        break;
      default:
        return OBELISK_RT_INVALID_BYTECODE;
      }
      result.value[0] = value;
      result.unknown[0] = resultUnknown;
      write(instruction.destination, result);
      break;
    }
    case OBELISK_RT_DB_AND:
    case OBELISK_RT_DB_OR:
    case OBELISK_RT_DB_XOR: {
      uint64_t left = 0, right = 0;
      if (readKnownScalar(image, frame, instruction.source0, left) &&
          readKnownScalar(image, frame, instruction.source1, right)) {
        uint64_t result = instruction.opcode == OBELISK_RT_DB_AND ? left & right
                          : instruction.opcode == OBELISK_RT_DB_OR
                              ? left | right
                              : left ^ right;
        if (writeKnownScalar(image, frame, instruction.destination, result))
          break;
      }
      write(instruction.destination,
            bitwise(read(instruction.source0), read(instruction.source1),
                    instruction.opcode));
      break;
    }
    case OBELISK_RT_DB_ADD:
    case OBELISK_RT_DB_SUB: {
      uint64_t left = 0, right = 0;
      if (readKnownScalar(image, frame, instruction.source0, left) &&
          readKnownScalar(image, frame, instruction.source1, right) &&
          writeKnownScalar(image, frame, instruction.destination,
                           instruction.opcode == OBELISK_RT_DB_SUB
                               ? left - right
                               : left + right))
        break;
      write(instruction.destination,
            add(read(instruction.source0), read(instruction.source1),
                instruction.opcode == OBELISK_RT_DB_SUB));
      break;
    }
    case OBELISK_RT_DB_MUL: {
      uint64_t left = 0, right = 0;
      if (readKnownScalar(image, frame, instruction.source0, left) &&
          readKnownScalar(image, frame, instruction.source1, right) &&
          writeKnownScalar(image, frame, instruction.destination, left * right))
        break;
      write(instruction.destination,
            multiply(read(instruction.source0), read(instruction.source1)));
      break;
    }
    case OBELISK_RT_DB_POWER:
      write(instruction.destination,
            power(read(instruction.source0), read(instruction.source1)));
      break;
    case OBELISK_RT_DB_REPLICATE: {
      Layout destination = layout(instruction.destination);
      write(instruction.destination,
            replicate(read(instruction.source0), destination.width,
                      instruction.immediate));
      break;
    }
    case OBELISK_RT_DB_FADD:
    case OBELISK_RT_DB_FSUB:
    case OBELISK_RT_DB_FMUL:
    case OBELISK_RT_DB_FDIV:
    case OBELISK_RT_DB_FPOW: {
      Layout destination = layout(instruction.destination);
      if (destination.kind == OBELISK_RT_DBREG_REAL32) {
        float lhs = readFloat(instruction.source0);
        float rhs = readFloat(instruction.source1);
        float result = 0.0f;
        switch (instruction.opcode) {
        case OBELISK_RT_DB_FADD:
          result = lhs + rhs;
          break;
        case OBELISK_RT_DB_FSUB:
          result = lhs - rhs;
          break;
        case OBELISK_RT_DB_FMUL:
          result = lhs * rhs;
          break;
        case OBELISK_RT_DB_FDIV:
          result = lhs / rhs;
          break;
        default:
          result = std::pow(lhs, rhs);
          break;
        }
        writeFloat(instruction.destination, result);
      } else {
        double lhs = readDouble(instruction.source0);
        double rhs = readDouble(instruction.source1);
        double result = 0.0;
        switch (instruction.opcode) {
        case OBELISK_RT_DB_FADD:
          result = lhs + rhs;
          break;
        case OBELISK_RT_DB_FSUB:
          result = lhs - rhs;
          break;
        case OBELISK_RT_DB_FMUL:
          result = lhs * rhs;
          break;
        case OBELISK_RT_DB_FDIV:
          result = lhs / rhs;
          break;
        default:
          result = std::pow(lhs, rhs);
          break;
        }
        writeDouble(instruction.destination, result);
      }
      break;
    }
    case OBELISK_RT_DB_FNEG: {
      Layout destination = layout(instruction.destination);
      if (destination.kind == OBELISK_RT_DBREG_REAL32)
        writeFloat(instruction.destination, -readFloat(instruction.source0));
      else
        writeDouble(instruction.destination, -readDouble(instruction.source0));
      break;
    }
    case OBELISK_RT_DB_FCOMPARE: {
      Layout source = layout(instruction.source0);
      bool result = false;
      auto compare = [&](auto lhs, auto rhs) {
        switch (instruction.flags) {
        case OBELISK_RT_DB_FCMP_EQ:
          return lhs == rhs;
        case OBELISK_RT_DB_FCMP_NE:
          return lhs != rhs;
        case OBELISK_RT_DB_FCMP_LT:
          return lhs < rhs;
        case OBELISK_RT_DB_FCMP_LE:
          return lhs <= rhs;
        case OBELISK_RT_DB_FCMP_GT:
          return lhs > rhs;
        case OBELISK_RT_DB_FCMP_GE:
          return lhs >= rhs;
        default:
          return false;
        }
      };
      if (source.kind == OBELISK_RT_DBREG_REAL32)
        result = compare(readFloat(instruction.source0),
                         readFloat(instruction.source1));
      else
        result = compare(readDouble(instruction.source0),
                         readDouble(instruction.source1));
      Logic encoded{1, false, {result ? uint64_t{1} : uint64_t{0}}, {0}};
      write(instruction.destination, encoded);
      break;
    }
    case OBELISK_RT_DB_FEXT:
      writeDouble(instruction.destination,
                  static_cast<double>(readFloat(instruction.source0)));
      break;
    case OBELISK_RT_DB_FTRUNC:
      writeFloat(instruction.destination,
                 static_cast<float>(readDouble(instruction.source0)));
      break;
    case OBELISK_RT_DB_UDIV:
    case OBELISK_RT_DB_SDIV:
    case OBELISK_RT_DB_UREM:
    case OBELISK_RT_DB_SREM: {
      bool signedDivision = instruction.opcode == OBELISK_RT_DB_SDIV ||
                            instruction.opcode == OBELISK_RT_DB_SREM;
      auto result = divide(read(instruction.source0), read(instruction.source1),
                           signedDivision);
      write(instruction.destination,
            instruction.opcode == OBELISK_RT_DB_UREM ||
                    instruction.opcode == OBELISK_RT_DB_SREM
                ? result.second
                : result.first);
      break;
    }
    case OBELISK_RT_DB_SHL:
    case OBELISK_RT_DB_LSHR:
    case OBELISK_RT_DB_ASHR: {
      uint64_t input = 0, amount = 0;
      Layout inputLayout = layout(instruction.source0);
      if (readKnownScalar(image, frame, instruction.source0, input) &&
          readKnownScalar(image, frame, instruction.source1, amount)) {
        uint64_t result = 0;
        if (instruction.opcode == OBELISK_RT_DB_SHL) {
          result = amount < inputLayout.width ? input << amount : 0;
        } else if (instruction.opcode == OBELISK_RT_DB_LSHR) {
          result = amount < inputLayout.width ? input >> amount : 0;
        } else {
          bool sign = ((input >> (inputLayout.width - 1)) & 1) != 0;
          if (amount >= inputLayout.width) {
            result = sign ? finalMask(inputLayout.width) : 0;
          } else {
            result = input >> amount;
            if (sign && amount != 0)
              result |= UINT64_MAX << (inputLayout.width - amount);
          }
        }
        if (writeKnownScalar(image, frame, instruction.destination, result))
          break;
      }
      write(instruction.destination,
            shift(read(instruction.source0), read(instruction.source1),
                  instruction.opcode));
      break;
    }
    case OBELISK_RT_DB_COMPARE: {
      Logic left = read(instruction.source0), right = read(instruction.source1);
      bool deterministic = instruction.flags == OBELISK_RT_DB_CMP_CASE_EQ ||
                           instruction.flags == OBELISK_RT_DB_CMP_CASE_NE ||
                           instruction.flags == OBELISK_RT_DB_CMP_CASEZ_EQ ||
                           instruction.flags == OBELISK_RT_DB_CMP_CASEXZ_EQ;
      bool wildcardEquality = instruction.flags == OBELISK_RT_DB_CMP_WILD_EQ ||
                              instruction.flags == OBELISK_RT_DB_CMP_WILD_NE;
      Logic result{1,
                   layout(instruction.destination).kind ==
                       OBELISK_RT_DBREG_LOGIC,
                   {0},
                   {0}};
      if (!deterministic && !wildcardEquality &&
          (anyUnknown(left) || anyUnknown(right))) {
        bool logicalEquality = instruction.flags == OBELISK_RT_DB_CMP_EQ ||
                               instruction.flags == OBELISK_RT_DB_CMP_NE;
        bool knownMismatch = false;
        if (logicalEquality) {
          for (size_t index = 0; index < left.value.size(); ++index) {
            uint64_t known = ~(left.unknown[index] | right.unknown[index]);
            if (((left.value[index] ^ right.value[index]) & known) != 0) {
              knownMismatch = true;
              break;
            }
          }
        }
        if (logicalEquality && knownMismatch)
          result.value[0] = instruction.flags == OBELISK_RT_DB_CMP_NE;
        else
          result = allX(1, result.fourState);
      } else {
        int compared = compareUnsigned(left.value, right.value);
        bool signedOperands = instruction.flags >= OBELISK_RT_DB_CMP_SLT &&
                              instruction.flags <= OBELISK_RT_DB_CMP_SGE;
        if (signedOperands) {
          bool ls = bit(left.value, left.width - 1);
          bool rs = bit(right.value, right.width - 1);
          if (ls != rs)
            compared = ls ? -1 : 1;
        }
        bool value = false;
        switch (instruction.flags) {
        case OBELISK_RT_DB_CMP_EQ:
          value = compared == 0;
          break;
        case OBELISK_RT_DB_CMP_NE:
          value = compared != 0;
          break;
        case OBELISK_RT_DB_CMP_ULT:
        case OBELISK_RT_DB_CMP_SLT:
          value = compared < 0;
          break;
        case OBELISK_RT_DB_CMP_ULE:
        case OBELISK_RT_DB_CMP_SLE:
          value = compared <= 0;
          break;
        case OBELISK_RT_DB_CMP_UGT:
        case OBELISK_RT_DB_CMP_SGT:
          value = compared > 0;
          break;
        case OBELISK_RT_DB_CMP_UGE:
        case OBELISK_RT_DB_CMP_SGE:
          value = compared >= 0;
          break;
        case OBELISK_RT_DB_CMP_CASE_EQ:
        case OBELISK_RT_DB_CMP_CASE_NE: {
          value = left.value == right.value && left.unknown == right.unknown;
          if (instruction.flags == OBELISK_RT_DB_CMP_CASE_NE)
            value = !value;
          break;
        }
        case OBELISK_RT_DB_CMP_CASEZ_EQ:
        case OBELISK_RT_DB_CMP_CASEXZ_EQ: {
          bool equal = true;
          for (uint32_t bitIndex = 0; bitIndex < left.width; ++bitIndex) {
            bool leftUnknown = bit(left.unknown, bitIndex);
            bool rightUnknown = bit(right.unknown, bitIndex);
            bool leftValue = bit(left.value, bitIndex);
            bool rightValue = bit(right.value, bitIndex);
            bool wildcard = false;
            if (instruction.flags == OBELISK_RT_DB_CMP_CASEZ_EQ)
              wildcard =
                  (leftUnknown && leftValue) || (rightUnknown && rightValue);
            else
              wildcard = leftUnknown || rightUnknown;
            if (!wildcard &&
                (leftUnknown != rightUnknown || leftValue != rightValue)) {
              equal = false;
              break;
            }
          }
          value = equal;
          break;
        }
        case OBELISK_RT_DB_CMP_WILD_EQ:
        case OBELISK_RT_DB_CMP_WILD_NE: {
          bool knownMismatch = false;
          bool relevantUnknown = false;
          for (uint32_t bitIndex = 0; bitIndex < left.width; ++bitIndex) {
            if (bit(right.unknown, bitIndex))
              continue;
            if (bit(left.unknown, bitIndex)) {
              relevantUnknown = true;
              continue;
            }
            if (bit(left.value, bitIndex) != bit(right.value, bitIndex)) {
              knownMismatch = true;
              break;
            }
          }
          if (!knownMismatch && relevantUnknown) {
            result = allX(1, result.fourState);
            break;
          }
          value = !knownMismatch;
          if (instruction.flags == OBELISK_RT_DB_CMP_WILD_NE)
            value = !value;
          break;
        }
        default:
          return OBELISK_RT_INVALID_BYTECODE;
        }
        result.value[0] = value;
      }
      write(instruction.destination, result);
      break;
    }
    case OBELISK_RT_DB_SELECT: {
      Logic condition = read(instruction.source2);
      if (anyUnknown(condition)) {
        if (instruction.flags == OBELISK_RT_DB_SELECT_FOUR_STATE) {
          Logic left = read(instruction.source0);
          Logic right = read(instruction.source1);
          Logic result{left.width, true, LimbVector(limbCount(left.width)),
                       LimbVector(limbCount(left.width))};
          for (uint32_t bitIndex = 0; bitIndex < left.width; ++bitIndex) {
            bool leftValue = bit(left.value, bitIndex);
            bool leftUnknown = bit(left.unknown, bitIndex);
            bool mismatch = leftUnknown || bit(right.unknown, bitIndex) ||
                            leftValue != bit(right.value, bitIndex);
            setBit(result.value, bitIndex, !mismatch && leftValue);
            setBit(result.unknown, bitIndex, mismatch);
          }
          write(instruction.destination, result);
        } else {
          write(instruction.destination,
                allX(layout(instruction.destination).width,
                     layout(instruction.destination).kind ==
                         OBELISK_RT_DBREG_LOGIC));
        }
      } else if (!copyRegister(image, frame,
                               isZero(condition) ? instruction.source1
                                                 : instruction.source0,
                               frame, instruction.destination))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    }
    case OBELISK_RT_DB_EXTRACT: {
      Logic input = read(instruction.source0);
      Layout destination = layout(instruction.destination);
      Logic result{destination.width,
                   destination.kind == OBELISK_RT_DBREG_LOGIC,
                   LimbVector(limbCount(destination.width)),
                   LimbVector(limbCount(destination.width))};
      uint64_t low = instruction.immediate;
      bool negative = false;
      uint64_t negativeMagnitude = 0;
      if (instruction.source1 != kInvalidRegister) {
        Logic dynamic = read(instruction.source1);
        if (anyUnknown(dynamic)) {
          write(instruction.destination,
                allX(destination.width, result.fourState));
          break;
        }
        negative = bit(dynamic.value, dynamic.width - 1);
        if (negative) {
          Logic magnitude = negate(dynamic);
          bool fits = !magnitude.value.empty();
          for (size_t index = 1; index < magnitude.value.size(); ++index)
            fits &= magnitude.value[index] == 0;
          if (!fits) {
            write(instruction.destination,
                  allX(destination.width, result.fourState));
            break;
          }
          negativeMagnitude = magnitude.value[0];
        } else {
          bool fits = !dynamic.value.empty();
          for (size_t index = 1; index < dynamic.value.size(); ++index)
            fits &= dynamic.value[index] == 0;
          low = fits ? dynamic.value[0] : UINT64_MAX;
        }
      }
      if (instruction.flags == OBELISK_RT_DB_AGGREGATE_MANAGED &&
          (destination.kind == OBELISK_RT_DBREG_MANAGED ||
           destination.kind == OBELISK_RT_DBREG_STRING) &&
          instruction.source1 != kInvalidRegister &&
          (negative || low == UINT64_MAX || (low & 63) != 0 ||
           low > input.width || uint64_t{64} > input.width - low)) {
        // A dynamic class-handle extraction may only select a complete,
        // naturally aligned handle word. Invalid array indices yield the
        // two-state default (null), never a forged host pointer assembled from
        // adjacent aggregate bits.
        write(instruction.destination, result);
        break;
      }
      for (uint64_t bitIndex = 0; bitIndex != destination.width; ++bitIndex) {
        bool inRange = false;
        uint64_t source = 0;
        if (negative) {
          if (bitIndex >= negativeMagnitude) {
            source = bitIndex - negativeMagnitude;
            inRange = source < input.width;
          }
        } else if (low != UINT64_MAX && bitIndex <= UINT64_MAX - low) {
          source = low + bitIndex;
          inRange = source < input.width;
        }
        if (!inRange) {
          // Static resize operations use source1 == invalid and may sign
          // extend. Dynamic selections instead pad every out-of-range bit
          // with X (or zero for a two-state result).
          bool signExtend = instruction.source1 == kInvalidRegister &&
                            (instruction.flags & 1) != 0 && input.width != 0;
          setBit(result.value, bitIndex,
                 signExtend && bit(input.value, input.width - 1));
          setBit(result.unknown, bitIndex,
                 signExtend ? bit(input.unknown, input.width - 1)
                            : (instruction.source1 != kInvalidRegister &&
                               result.fourState));
          continue;
        }
        setBit(result.value, bitIndex, bit(input.value, source));
        setBit(result.unknown, bitIndex, bit(input.unknown, source));
      }
      write(instruction.destination, result);
      break;
    }
    case OBELISK_RT_DB_MAKE_HANDLE: {
      Layout destination = layout(instruction.destination);
      if (destination.kind != OBELISK_RT_DBREG_HANDLE)
        return OBELISK_RT_INVALID_BYTECODE;
      uint8_t *address = frame.data + destination.offset;
      std::memset(address, 0, destination.size);
      uint32_t kind = instruction.source0;
      uint64_t width = instruction.source1;
      // The compiler-reserved Preponed event is deliberately not a decodable
      // state handle. Admit it only when constructing an exact zero-width
      // event descriptor; every storage/net/driver path below retains the
      // ordinary stable-state validation.
      bool preponedEvent =
          kind == OBELISK_RT_DESCRIPTOR_EVENT && width == 0 &&
          obelisk_rt_stable_handle_is_preponed_event(instruction.immediate);
      obelisk_rt_stable_handle_v1 decoded;
      if (!preponedEvent &&
          (!obelisk_rt_stable_handle_decode(instruction.immediate, &decoded) ||
           decoded.kind == OBELISK_RT_STABLE_HANDLE_AUTOMATIC ||
           decoded.offset < 0 ||
           width > uint64_t{INT64_MAX} - static_cast<uint64_t>(decoded.offset)))
        return OBELISK_RT_INVALID_HANDLE;
      int64_t begin = preponedEvent
                          ? static_cast<int64_t>(instruction.immediate)
                          : decoded.offset;
      int64_t end = begin + static_cast<int64_t>(width);
      uint64_t base = static_cast<uint64_t>(begin);
      if (!preponedEvent && decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC) {
        if (!context || kind > OBELISK_RT_DESCRIPTOR_DRIVER)
          return OBELISK_RT_INVALID_HANDLE;
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        auto state = context->nativeStaticStates.find(decoded.id);
        if (state == context->nativeStaticStates.end() ||
            static_cast<uint64_t>(begin) > state->second.bitWidth ||
            width > state->second.bitWidth - static_cast<uint64_t>(begin))
          return OBELISK_RT_INVALID_HANDLE;
        base = encodeStaticHandle(decoded.id, 0);
        if (base == UINT64_MAX)
          return OBELISK_RT_INVALID_HANDLE;
      } else if (!preponedEvent && context &&
                 kind <= OBELISK_RT_DESCRIPTOR_DRIVER) {
        // Bytecode encodes canonical plane offsets so a process can execute
        // directly without scheduler-main registration. Once native static
        // state is registered, use its stable identity so mixed-tier waits and
        // publications name the same object.
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        uint64_t canonical = obelisk_rt_canonical_state_handle_unlocked(
            context, static_cast<uint64_t>(begin), width);
        uint32_t id = 0;
        int64_t offset = 0;
        if (decodeStaticHandle(canonical, id, offset) && offset == 0) {
          auto state = context->nativeStaticStates.find(id);
          if (state == context->nativeStaticStates.end() ||
              state->second.bitWidth != width)
            return OBELISK_RT_INVALID_HANDLE;
          base = canonical;
          begin = 0;
          end = static_cast<int64_t>(width);
        }
      }
      std::memcpy(address, &kind, sizeof(kind));
      std::memcpy(address + 8, &base, sizeof(base));
      std::memcpy(address + 16, &begin, sizeof(begin));
      std::memcpy(address + 24, &end, sizeof(end));
      break;
    }
    case OBELISK_RT_DB_MAKE_LOCAL_HANDLE: {
      Layout destination = layout(instruction.destination);
      Layout storage = layout(instruction.source0);
      if (destination.kind != OBELISK_RT_DBREG_HANDLE ||
          (storage.kind != OBELISK_RT_DBREG_BITS &&
           storage.kind != OBELISK_RT_DBREG_LOGIC &&
           storage.kind != OBELISK_RT_DBREG_REAL32 &&
           storage.kind != OBELISK_RT_DBREG_REAL64) ||
          frame.id == 0 || frame.id > UINT32_C(0x7fff) ||
          instruction.source0 > UINT16_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      uint8_t *address = frame.data + destination.offset;
      std::memset(address, 0, destination.size);
      uint32_t kind = kLocalHandleKind | (frame.id << 16) | instruction.source0;
      int64_t begin = 0;
      int64_t end = storage.width;
      std::memcpy(address, &kind, sizeof(kind));
      std::memcpy(address + 8, &begin, 8);
      std::memcpy(address + 16, &begin, 8);
      std::memcpy(address + 24, &end, 8);
      break;
    }
    case OBELISK_RT_DB_HANDLE_OFFSET: {
      Layout source = layout(instruction.source0);
      Layout destination = layout(instruction.destination);
      if (source.kind != OBELISK_RT_DBREG_HANDLE ||
          destination.kind != OBELISK_RT_DBREG_HANDLE)
        return OBELISK_RT_INVALID_BYTECODE;
      std::memcpy(frame.data + destination.offset, frame.data + source.offset,
                  32);
      int64_t offset = 0;
      bool invalid = false;
      if (instruction.source1 != kInvalidRegister) {
        Logic dynamic = read(instruction.source1);
        invalid = anyUnknown(dynamic);
        if (!invalid) {
          bool negative = bit(dynamic.value, dynamic.width - 1);
          Logic magnitude = negative ? negate(dynamic) : dynamic;
          for (size_t index = 1; index < magnitude.value.size(); ++index)
            invalid |= magnitude.value[index] != 0;
          if (!invalid && magnitude.value[0] > uint64_t{INT64_MAX})
            invalid = true;
          if (!invalid)
            offset = negative ? -static_cast<int64_t>(magnitude.value[0])
                              : static_cast<int64_t>(magnitude.value[0]);
        }
      } else if (instruction.immediate > uint64_t{INT64_MAX}) {
        invalid = true;
      } else {
        offset = static_cast<int64_t>(instruction.immediate);
      }
      uint8_t *address = frame.data + destination.offset;
      uint32_t handleKind = 0;
      int64_t begin, start, end;
      std::memcpy(&handleKind, address, 4);
      std::memcpy(&start, address + 16, 8);
      std::memcpy(&end, address + 24, 8);
      bool automatic = (handleKind & kAutomaticHandleKind) != 0;
      uint64_t base = 0;
      std::memcpy(&base, address + 8, 8);
      uint32_t objectID = 0;
      int64_t sourceBegin = 0;
      bool boundedStatic =
          !automatic && decodeStaticHandle(base, objectID, sourceBegin);
      if (automatic || boundedStatic) {
        int64_t nextStart = 0, nextEnd = 0;
        bool decoded =
            boundedStatic || decodeAutomaticHandle(base, objectID, sourceBegin);
        bool failed = invalid || !decoded || start == kInvalidHandleStart ||
                      end < sourceBegin ||
                      (offset > 0 && start > INT64_MAX - offset) ||
                      (offset < 0 && start < INT64_MIN - offset);
        if (!failed)
          nextStart = start + offset;
        failed |= !failed && nextStart > INT64_MAX - static_cast<int64_t>(
                                                         instruction.auxiliary);
        if (!failed)
          nextEnd = nextStart + static_cast<int64_t>(instruction.auxiliary);
        if (failed) {
          start = kInvalidHandleStart;
          end = 0;
          base = decoded ? (automatic ? encodeAutomaticHandle(objectID, 0)
                                      : encodeStaticHandle(objectID, 0))
                         : UINT64_MAX;
        } else {
          int64_t clippedBegin = std::max(sourceBegin, nextStart);
          start = nextStart;
          end = std::min(end, nextEnd);
          if (clippedBegin > end)
            clippedBegin = end;
          base = automatic ? encodeAutomaticHandle(objectID, clippedBegin)
                           : encodeStaticHandle(objectID, clippedBegin);
        }
        std::memcpy(address + 8, &base, 8);
        std::memcpy(address + 16, &start, 8);
        std::memcpy(address + 24, &end, 8);
        break;
      }
      std::memcpy(&begin, address + 8, 8);
      int64_t nextStart = 0, nextEnd = 0;
      if (invalid || start == kInvalidHandleStart ||
          (offset > 0 && start > INT64_MAX - offset) ||
          (offset < 0 && start < INT64_MIN - offset)) {
        begin = end = 0;
        start = kInvalidHandleStart;
      } else {
        nextStart = start + offset;
        if (nextStart >
            INT64_MAX - static_cast<int64_t>(instruction.auxiliary)) {
          begin = end = 0;
          start = kInvalidHandleStart;
        } else {
          nextEnd = nextStart + static_cast<int64_t>(instruction.auxiliary);
          begin = std::max(begin, nextStart);
          end = std::min(end, nextEnd);
          start = nextStart;
          if (begin > end)
            begin = end;
        }
      }
      std::memcpy(address + 8, &begin, 8);
      std::memcpy(address + 16, &start, 8);
      std::memcpy(address + 24, &end, 8);
      break;
    }
    case OBELISK_RT_DB_HANDLE_ID: {
      Layout handle = layout(instruction.source0);
      uint32_t kind = 0;
      int64_t start = kInvalidHandleStart;
      std::memcpy(&kind, frame.data + handle.offset, 4);
      std::memcpy(&start, frame.data + handle.offset + 16, 8);
      uint32_t descriptorKind =
          kind & ~(kLocalHandleKind | kAutomaticHandleKind);
      bool nullEvent = kind == OBELISK_RT_DESCRIPTOR_EVENT && start == -1;
      if (start == kInvalidHandleStart)
        return OBELISK_RT_INVALID_HANDLE;
      uint64_t raw = nullEvent ? UINT64_MAX : static_cast<uint64_t>(start);
      bool dynamicEvent = isDynamicEventHandle(descriptorKind, raw);
      uint64_t stable = nullEvent
                            ? UINT64_MAX
                            : (dynamicEvent ? raw : encodeGlobalHandle(start));
      if (!nullEvent && !dynamicEvent && (kind & kAutomaticHandleKind) != 0) {
        uint64_t base = 0;
        std::memcpy(&base, frame.data + handle.offset + 8, 8);
        uint32_t id = 0;
        int64_t begin = 0;
        if (!decodeAutomaticHandle(base, id, begin))
          return OBELISK_RT_INVALID_HANDLE;
        stable = encodeAutomaticHandle(id, start);
      } else if (!nullEvent && !dynamicEvent) {
        uint64_t base = 0;
        std::memcpy(&base, frame.data + handle.offset + 8, 8);
        uint32_t id = 0;
        int64_t begin = 0;
        if (decodeStaticHandle(base, id, begin))
          stable = encodeStaticHandle(id, start);
      }
      if (stable == UINT64_MAX && !nullEvent)
        return OBELISK_RT_INVALID_HANDLE;
      Logic value{64, false, {stable}, {0}};
      write(instruction.destination, value);
      break;
    }
    case OBELISK_RT_DB_CONCAT: {
      Logic left = read(instruction.source0), right = read(instruction.source1);
      Layout destination = layout(instruction.destination);
      Logic result{destination.width,
                   destination.kind == OBELISK_RT_DBREG_LOGIC,
                   LimbVector(limbCount(destination.width)),
                   LimbVector(limbCount(destination.width))};
      for (uint64_t bitIndex = 0; bitIndex != right.width; ++bitIndex) {
        setBit(result.value, bitIndex, bit(right.value, bitIndex));
        setBit(result.unknown, bitIndex, bit(right.unknown, bitIndex));
      }
      for (uint64_t bitIndex = 0; bitIndex != left.width; ++bitIndex) {
        setBit(result.value, right.width + bitIndex, bit(left.value, bitIndex));
        setBit(result.unknown, right.width + bitIndex,
               bit(left.unknown, bitIndex));
      }
      write(instruction.destination, result);
      break;
    }
    case OBELISK_RT_DB_INSERT: {
      Logic base = read(instruction.source0),
            inserted = read(instruction.source1);
      bool negative = false;
      uint64_t low = instruction.immediate;
      uint64_t negativeMagnitude = 0;
      if ((instruction.flags & OBELISK_RT_DB_INSERT_DYNAMIC) != 0) {
        Logic dynamic = read(instruction.source2);
        if (anyUnknown(dynamic)) {
          write(instruction.destination, base);
          break;
        }
        negative = bit(dynamic.value, dynamic.width - 1);
        if (negative) {
          Logic magnitude = negate(dynamic);
          bool fits = !magnitude.value.empty();
          for (size_t index = 1; index < magnitude.value.size(); ++index)
            fits &= magnitude.value[index] == 0;
          if (!fits) {
            write(instruction.destination, base);
            break;
          }
          negativeMagnitude = magnitude.value[0];
        } else {
          bool fits = !dynamic.value.empty();
          for (size_t index = 1; index < dynamic.value.size(); ++index)
            fits &= dynamic.value[index] == 0;
          if (!fits) {
            write(instruction.destination, base);
            break;
          }
          low = dynamic.value[0];
        }
      }
      for (uint64_t bitIndex = 0; bitIndex != inserted.width; ++bitIndex) {
        uint64_t destination = 0;
        if (negative) {
          if (bitIndex < negativeMagnitude)
            continue;
          destination = bitIndex - negativeMagnitude;
        } else {
          if (bitIndex > UINT64_MAX - low)
            break;
          destination = low + bitIndex;
        }
        if (destination >= base.width)
          break;
        setBit(base.value, destination, bit(inserted.value, bitIndex));
        setBit(base.unknown, destination, bit(inserted.unknown, bitIndex));
      }
      write(instruction.destination, base);
      break;
    }
    case OBELISK_RT_DB_LOAD_FRAME:
    case OBELISK_RT_DB_STORE_FRAME: {
      uint32_t reg = instruction.opcode == OBELISK_RT_DB_LOAD_FRAME
                         ? instruction.destination
                         : instruction.source0;
      Layout value = layout(reg);
      uint64_t transferSize = value.kind == OBELISK_RT_DBREG_HANDLE ? 8
                              : instruction.auxiliary != 0
                                  ? instruction.auxiliary
                                  : value.size;
      if (!canonicalFrame || instruction.immediate > canonicalFrameSize ||
          transferSize > canonicalFrameSize - instruction.immediate)
        return OBELISK_RT_INVALID_FRAME;
      if (value.kind != OBELISK_RT_DBREG_HANDLE) {
        // A four-state register keeps its value and unknown planes one limb
        // stride apart, while the canonical frame packs the two planes at the
        // target ABI size of the value type. Transfer each plane on its own
        // instead of copying one contiguous run: for every logic value
        // narrower than a limb the planes do not line up, and a single copy
        // silently drops the unknown plane, turning x and z into 0 across a
        // suspension.
        uint64_t registerPlane = value.kind == OBELISK_RT_DBREG_LOGIC
                                     ? limbCount(value.width) * sizeof(uint64_t)
                                     : 0;
        uint64_t framePlane =
            registerPlane != 0 ? transferSize / 2 : transferSize;
        uint64_t unknownDisplacement =
            instruction.source1 != 0 ? instruction.source1 : framePlane;
        if (registerPlane != 0 &&
            (transferSize % 2 != 0 || framePlane > registerPlane))
          return OBELISK_RT_INVALID_FRAME;
        if (instruction.opcode == OBELISK_RT_DB_LOAD_FRAME) {
          if (transferSize != value.size)
            std::memset(frame.data + value.offset, 0, value.size);
          std::memcpy(frame.data + value.offset,
                      canonicalFrame + instruction.immediate, framePlane);
          if (registerPlane != 0)
            std::memcpy(frame.data + value.offset + registerPlane,
                        canonicalFrame + instruction.immediate +
                            unknownDisplacement,
                        framePlane);
        } else {
          std::memcpy(canonicalFrame + instruction.immediate,
                      frame.data + value.offset, framePlane);
          if (registerPlane != 0)
            std::memcpy(canonicalFrame + instruction.immediate +
                            unknownDisplacement,
                        frame.data + value.offset + registerPlane, framePlane);
        }
        break;
      }
      if (instruction.opcode == OBELISK_RT_DB_LOAD_FRAME) {
        uint64_t stable = 0;
        std::memcpy(&stable, canonicalFrame + instruction.immediate, 8);
        uint8_t *address = frame.data + value.offset;
        std::memset(address, 0, value.size);
        uint32_t kind = instruction.flags;
        int64_t begin = 0, start = kInvalidHandleStart, end = 0;
        uint32_t automaticID = 0;
        int64_t automaticOffset = 0;
        bool boundedStatic = false;
        bool nullEvent =
            kind == OBELISK_RT_DESCRIPTOR_EVENT && stable == UINT64_MAX;
        bool dynamicEvent = isDynamicEventHandle(kind, stable);
        if (nullEvent) {
          start = end = -1;
        } else if (dynamicEvent) {
          start = static_cast<int64_t>(stable);
          begin = start;
          end = start == INT64_MAX ? start : start + 1;
        } else if (decodeAutomaticHandle(stable, automaticID,
                                         automaticOffset)) {
          if (!context || kind != OBELISK_RT_DESCRIPTOR_STORAGE)
            return OBELISK_RT_INVALID_HANDLE;
          std::lock_guard<std::recursive_mutex> lock(context->mutex);
          auto found = context->nativeAutomaticStates.find(automaticID);
          int64_t width = static_cast<int64_t>(instruction.auxiliary);
          if (found == context->nativeAutomaticStates.end() || width <= 0 ||
              found->second.bitWidth > uint64_t{INT64_MAX} ||
              automaticOffset > INT64_MAX - width)
            return OBELISK_RT_INVALID_HANDLE;
          kind |= kAutomaticHandleKind;
          start = automaticOffset;
          int64_t available = static_cast<int64_t>(found->second.bitWidth);
          int64_t requestedEnd = start + width;
          if (requestedEnd <= 0) {
            begin = end = 0;
          } else if (start >= available) {
            begin = end = available;
          } else {
            begin = std::max<int64_t>(0, start);
            end = std::min<int64_t>(available, requestedEnd);
          }
          uint64_t base = encodeAutomaticHandle(automaticID, begin);
          if (base == UINT64_MAX)
            return OBELISK_RT_INVALID_HANDLE;
          std::memcpy(address + 8, &base, 8);
        } else if (decodeStaticHandle(stable, automaticID, automaticOffset)) {
          if (!context || kind > OBELISK_RT_DESCRIPTOR_DRIVER)
            return OBELISK_RT_INVALID_HANDLE;
          std::lock_guard<std::recursive_mutex> lock(context->mutex);
          auto found = context->nativeStaticStates.find(automaticID);
          int64_t width = static_cast<int64_t>(instruction.auxiliary);
          if (found == context->nativeStaticStates.end() || width <= 0 ||
              found->second.bitWidth > uint64_t{INT64_MAX} ||
              automaticOffset > INT64_MAX - width)
            return OBELISK_RT_INVALID_HANDLE;
          boundedStatic = true;
          start = automaticOffset;
          int64_t available = static_cast<int64_t>(found->second.bitWidth);
          int64_t requestedEnd = start + width;
          if (requestedEnd <= 0) {
            begin = end = 0;
          } else if (start >= available) {
            begin = end = available;
          } else {
            begin = std::max<int64_t>(0, start);
            end = std::min<int64_t>(available, requestedEnd);
          }
          uint64_t base = encodeStaticHandle(automaticID, begin);
          if (base == UINT64_MAX)
            return OBELISK_RT_INVALID_HANDLE;
          std::memcpy(address + 8, &base, 8);
        } else if (decodeGlobalHandle(stable, start)) {
          if (kind <= OBELISK_RT_DESCRIPTOR_DRIVER) {
            int64_t available =
                context && context->execution &&
                        context->execution->state_bit_count <=
                            uint64_t{INT64_MAX}
                    ? static_cast<int64_t>(context->execution->state_bit_count)
                    : 0;
            int64_t width = static_cast<int64_t>(instruction.auxiliary);
            if (width <= 0 || start > INT64_MAX - width)
              start = kInvalidHandleStart;
            else {
              begin = std::max<int64_t>(0, start);
              end = std::min<int64_t>(available, start + width);
              if (begin > end)
                begin = end;
            }
          } else {
            begin = start;
            end = start == INT64_MAX ? start : start + 1;
          }
        }
        std::memcpy(address, &kind, 4);
        if ((kind & kAutomaticHandleKind) == 0 && !boundedStatic)
          std::memcpy(address + 8, &begin, 8);
        std::memcpy(address + 16, &start, 8);
        std::memcpy(address + 24, &end, 8);
      } else {
        uint64_t stable = UINT64_MAX;
        if (!encodeCanonicalHandle(frame.data + value.offset, stable))
          return OBELISK_RT_INVALID_HANDLE;
        std::memcpy(canonicalFrame + instruction.immediate, &stable, 8);
      }
      break;
    }
    case OBELISK_RT_DB_CLEAR_FRAME_ROOT:
      if (!canonicalFrame || instruction.immediate > canonicalFrameSize ||
          sizeof(obelisk_rt_managed_word_v1) >
              canonicalFrameSize - instruction.immediate)
        return OBELISK_RT_INVALID_FRAME;
      std::memset(canonicalFrame + instruction.immediate, 0,
                  sizeof(obelisk_rt_managed_word_v1));
      break;
    case OBELISK_RT_DB_FRAME_ROOT:
      break;
    case OBELISK_RT_DB_LOAD_STATE:
    case OBELISK_RT_DB_STORE_STATE:
    case OBELISK_RT_DB_OVERRIDE_STATE: {
      bool isLoad = instruction.opcode == OBELISK_RT_DB_LOAD_STATE;
      bool isOverride = instruction.opcode == OBELISK_RT_DB_OVERRIDE_STATE;
      if (isOverride)
        context->managedValueOverridePossible.store(true,
                                                    std::memory_order_relaxed);
      bool isAssignOverride =
          isOverride &&
          (instruction.flags & OBELISK_RT_DB_OVERRIDE_KIND_MASK) ==
              OBELISK_RT_DB_OVERRIDE_ASSIGN;
      bool dynamicOverride =
          isOverride &&
          (instruction.flags & OBELISK_RT_DB_OVERRIDE_DYNAMIC) != 0;
      bool overrideClaim =
          dynamicOverride &&
          (instruction.flags & OBELISK_RT_DB_OVERRIDE_CLAIM) != 0;
      uint64_t overrideOwner = 0;
      if (dynamicOverride) {
        Logic owner = read(instruction.source2);
        if (owner.width != 64 || anyUnknown(owner) || owner.value.empty())
          return OBELISK_RT_INVALID_HANDLE;
        overrideOwner = owner.value[0];
        if (overrideOwner == 0)
          return OBELISK_RT_INVALID_HANDLE;
      }
      bool isContinuous =
          !isLoad && !isOverride &&
          (instruction.flags & OBELISK_RT_DB_STORE_STATE_CONTINUOUS) != 0;
      if (!isLoad && context &&
          context->activeExecRegion == OBELISK_RT_REGION_POSTPONED)
        return OBELISK_RT_INVALID_LIFECYCLE;
      uint32_t valueRegister =
          isLoad ? instruction.destination : instruction.source1;
      Layout valueLayout = layout(valueRegister);
      bool eventValue = valueLayout.kind == OBELISK_RT_DBREG_HANDLE;
      Logic value;
      if (eventValue) {
        if (valueLayout.size != 32 || isOverride)
          return OBELISK_RT_INVALID_HANDLE;
        value = Logic{64, false, LimbVector(1), LimbVector(1)};
        if (!isLoad) {
          uint32_t valueKind = 0;
          std::memcpy(&valueKind, frame.data + valueLayout.offset,
                      sizeof(valueKind));
          valueKind &= ~(kLocalHandleKind | kAutomaticHandleKind);
          uint64_t stable = UINT64_MAX;
          if (valueKind != OBELISK_RT_DESCRIPTOR_EVENT ||
              !encodeCanonicalHandle(frame.data + valueLayout.offset, stable))
            return OBELISK_RT_INVALID_HANDLE;
          value.value[0] = stable;
        }
      } else {
        value = !isLoad ? read(valueRegister)
                        : Logic{valueLayout.width,
                                valueLayout.kind == OBELISK_RT_DBREG_LOGIC,
                                LimbVector(limbCount(valueLayout.width)),
                                LimbVector(limbCount(valueLayout.width))};
      }
      Layout handleLayout = layout(instruction.source0);
      if (handleLayout.kind != OBELISK_RT_DBREG_HANDLE)
        return OBELISK_RT_INVALID_HANDLE;
      uint32_t handleKind = 0;
      int64_t begin = 0, start = kInvalidHandleStart, end = 0;
      uint64_t automaticBase = 0;
      std::memcpy(&handleKind, frame.data + handleLayout.offset, 4);
      std::memcpy(&start, frame.data + handleLayout.offset + 16, 8);
      std::memcpy(&end, frame.data + handleLayout.offset + 24, 8);
      bool local = (handleKind & kLocalHandleKind) != 0;
      bool automatic = (handleKind & kAutomaticHandleKind) != 0;
      uint32_t descriptorKind =
          handleKind & ~(kLocalHandleKind | kAutomaticHandleKind);
      uint32_t staticID = 0;
      int64_t staticBegin = 0;
      std::memcpy(&automaticBase, frame.data + handleLayout.offset + 8, 8);
      bool boundedStatic =
          !local && !automatic &&
          decodeStaticHandle(automaticBase, staticID, staticBegin);
      if (automatic) {
        uint32_t id = 0;
        if (!decodeAutomaticHandle(automaticBase, id, begin))
          return OBELISK_RT_INVALID_HANDLE;
      } else if (boundedStatic) {
        begin = staticBegin;
      } else {
        begin = static_cast<int64_t>(automaticBase);
      }
      if ((local && (automatic || boundedStatic)) ||
          (!local && (descriptorKind < OBELISK_RT_DESCRIPTOR_STORAGE ||
                      descriptorKind > OBELISK_RT_DESCRIPTOR_DRIVER)) ||
          (automatic && descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE) ||
          begin > end)
        return OBELISK_RT_INVALID_HANDLE;
      if (eventValue && (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE ||
                         isContinuous || instruction.flags != 0))
        return OBELISK_RT_INVALID_HANDLE;
      if (isOverride && (local || automatic ||
                         (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE &&
                          descriptorKind != OBELISK_RT_DESCRIPTOR_NET) ||
                         (isAssignOverride &&
                          descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE)))
        return OBELISK_RT_INVALID_HANDLE;
      std::vector<uint64_t> retiredOverrideOwners;
      if (isOverride && descriptorKind == OBELISK_RT_DESCRIPTOR_NET) {
        if (start < 0 || value.width == 0 ||
            value.width > static_cast<uint64_t>(end - start))
          return OBELISK_RT_INVALID_HANDLE;
        uint64_t absolute = static_cast<uint64_t>(start);
        if (boundedStatic) {
          auto found = context->nativeStaticStates.find(staticID);
          if (found == context->nativeStaticStates.end() ||
              static_cast<uint64_t>(start) > found->second.bitWidth ||
              value.width >
                  found->second.bitWidth - static_cast<uint64_t>(start))
            return OBELISK_RT_INVALID_HANDLE;
          absolute = found->second.bitOffset + static_cast<uint64_t>(start);
        }
        obelisk_rt_status status = OBELISK_RT_OK;
        uint64_t offset = 0;
        while (offset != value.width) {
          if (dynamicOverride && !overrideClaim) {
            std::lock_guard<std::recursive_mutex> lock(context->mutex);
            while (offset != value.width &&
                   !obelisk_rt_override_owner_matches_unlocked(
                       context,
                       obelisk_rt_canonical_net_bit_unlocked(context,
                                                             absolute + offset),
                       false, overrideOwner))
              ++offset;
          }
          if (offset == value.width)
            break;
          uint64_t runBegin = offset;
          if (dynamicOverride && !overrideClaim) {
            std::lock_guard<std::recursive_mutex> lock(context->mutex);
            while (offset != value.width &&
                   obelisk_rt_override_owner_matches_unlocked(
                       context,
                       obelisk_rt_canonical_net_bit_unlocked(context,
                                                             absolute + offset),
                       false, overrideOwner))
              ++offset;
          } else {
            offset = value.width;
          }
          uint64_t runWidth = offset - runBegin;
          std::vector<uint8_t> runValue(static_cast<size_t>((runWidth + 7) / 8),
                                        0);
          std::vector<uint8_t> runUnknown(
              static_cast<size_t>((runWidth + 7) / 8), 0);
          for (uint64_t bitIndex = 0; bitIndex != runWidth; ++bitIndex) {
            if (bit(value.value, runBegin + bitIndex))
              runValue[bitIndex / 8] |=
                  static_cast<uint8_t>(1u << (bitIndex % 8));
            if (value.fourState && bit(value.unknown, runBegin + bitIndex))
              runUnknown[bitIndex / 8] |=
                  static_cast<uint8_t>(1u << (bitIndex % 8));
          }
          status = obelisk_rt_force_design_nets(
              context, absolute + runBegin, runWidth, runValue.data(),
              value.fourState ? runUnknown.data() : nullptr);
          if (status != OBELISK_RT_OK)
            break;
        }
        if (status == OBELISK_RT_OK && (!dynamicOverride || overrideClaim)) {
          std::lock_guard<std::recursive_mutex> lock(context->mutex);
          for (uint64_t offset = 0; offset != value.width; ++offset)
            obelisk_rt_claim_override_range_unlocked(
                context,
                obelisk_rt_canonical_net_bit_unlocked(context,
                                                      absolute + offset),
                1, false, dynamicOverride ? overrideOwner : 0,
                retiredOverrideOwners);
        }
        if (status != OBELISK_RT_OK)
          return status;
        status = obelisk_rt_retire_override_owners(
            context, std::move(retiredOverrideOwners));
        if (status != OBELISK_RT_OK)
          return status;
        break;
      }
      if (valueLayout.kind == OBELISK_RT_DBREG_MANAGED && !isOverride) {
        if (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE ||
            valueLayout.width != 64 || local)
          return OBELISK_RT_INVALID_HANDLE;
        // A view whose window falls outside the storage denotes no storage --
        // an out-of-range dynamic array element, say. The load then yields the
        // element default and the store is dropped, which is what the packed
        // path below does per bit. Misalignment stays an error: no view can
        // produce it, so it would mean a malformed image.
        if (start == kInvalidHandleStart || start < 0 || end - start < 64) {
          if (isLoad) {
            obelisk_rt_managed_word_v1 none = 0;
            std::memcpy(frame.data + valueLayout.offset, &none, sizeof(none));
          }
          break;
        }
        if (start % 64 != 0)
          return OBELISK_RT_INVALID_HANDLE;
        obelisk_rt_managed_word_v1 managed = 0;
        obelisk_rt_managed_word_v1 previous = 0;
        if (instruction.opcode != OBELISK_RT_DB_LOAD_STATE) {
          std::memcpy(&managed, frame.data + valueLayout.offset,
                      sizeof(managed));
          if (managed != obelisk_rt_managed_word_from_object(
                             obelisk_rt_object_from_managed_word(managed)))
            return OBELISK_RT_INVALID_HANDLE;
        }
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        if (automatic) {
          uint32_t id = 0;
          int64_t baseOffset = 0;
          if (!decodeAutomaticHandle(automaticBase, id, baseOffset)) {
            return OBELISK_RT_INVALID_HANDLE;
          }
          auto found = context->nativeAutomaticStates.find(id);
          if (found == context->nativeAutomaticStates.end())
            return OBELISK_RT_INVALID_HANDLE;
          NativeAutomaticState &state = found->second;
          if (state.managedRootRegistered) {
            if (baseOffset != 0 || start != 0)
              return OBELISK_RT_INVALID_HANDLE;
            if (instruction.opcode == OBELISK_RT_DB_LOAD_STATE)
              managed = obelisk_rt_managed_word_from_object(state.managedValue);
            else {
              previous =
                  obelisk_rt_managed_word_from_object(state.managedValue);
              state.managedValue = obelisk_rt_object_from_managed_word(managed);
            }
          } else {
            if (start < 0 || baseOffset != start ||
                (static_cast<uint64_t>(start) & 63) != 0)
              return OBELISK_RT_INVALID_HANDLE;
            uint64_t byteOffset = static_cast<uint64_t>(start) / 8;
            if (byteOffset > state.value.size() ||
                sizeof(managed) > state.value.size() - byteOffset ||
                std::find(state.managedRootByteOffsets.begin(),
                          state.managedRootByteOffsets.end(),
                          byteOffset) == state.managedRootByteOffsets.end())
              return OBELISK_RT_INVALID_HANDLE;
            if (instruction.opcode == OBELISK_RT_DB_LOAD_STATE)
              std::memcpy(&managed, state.value.data() + byteOffset,
                          sizeof(managed));
            else {
              std::memcpy(&previous, state.value.data() + byteOffset,
                          sizeof(previous));
              std::memcpy(state.value.data() + byteOffset, &managed,
                          sizeof(managed));
            }
          }
        } else {
          uint64_t absolute = static_cast<uint64_t>(start);
          if (boundedStatic) {
            auto found = context->nativeStaticStates.find(staticID);
            if (found == context->nativeStaticStates.end())
              return OBELISK_RT_INVALID_HANDLE;
            absolute = found->second.bitOffset + static_cast<uint64_t>(start);
          }
          if (absolute % 64 != 0 || absolute / 64 >= context->stateValue.size())
            return OBELISK_RT_INVALID_HANDLE;
          uint64_t &slot = context->stateValue[absolute / 64];
          if (instruction.opcode == OBELISK_RT_DB_LOAD_STATE)
            managed = slot;
          else {
            previous = slot;
            uint64_t mask = uint64_t{1} << (absolute % 64);
            bool forced = absolute / 64 < context->forceMask.size() &&
                          (context->forceMask[absolute / 64] & mask) != 0;
            bool assigned = absolute / 64 < context->assignMask.size() &&
                            (context->assignMask[absolute / 64] & mask) != 0;
            if (forced || assigned) {
              managed = previous;
            } else {
              slot = managed;
            }
          }
        }
        if (instruction.opcode == OBELISK_RT_DB_LOAD_STATE)
          std::memcpy(frame.data + valueLayout.offset, &managed,
                      sizeof(managed));
        else if (previous != managed) {
          uint64_t changedHandle =
              automatic ? (automaticBase & ~uint64_t{UINT32_MAX}) |
                              static_cast<uint32_t>(start)
              : boundedStatic ? encodeStaticHandle(staticID, start)
                              : static_cast<uint64_t>(start);
          if (changedHandle == UINT64_MAX)
            return OBELISK_RT_OUT_OF_RESOURCES;
          if (!obelisk_rt_publish_signal_occurrence_unlocked(
                  context, changedHandle, 64, OBELISK_RT_SIGNAL_CHANGE))
            return context->schedulerStatus;
          obelisk_rt_invalidate_signal_snapshots_unlocked(context,
                                                          changedHandle, 64);
          if (!obelisk_rt_latch_conditional_signal_range_unlocked(
                  context, changedHandle, 64, OBELISK_RT_SIGNAL_CHANGE))
            return context->schedulerStatus;
          if (!obelisk_rt_notify_observer_signal_unlocked(context,
                                                          changedHandle, 64))
            return context->schedulerStatus;
          if (++context->schedulerEpoch == 0)
            context->schedulerEpoch = 1;
        }
        break;
      }
      Frame *localFrame = nullptr;
      Layout localLayout;
      Logic localValue;
      NativeAutomaticState *automaticState = nullptr;
      const NativeStaticState *staticState = nullptr;
      if (local) {
        uint32_t frameID = (handleKind >> 16) & UINT32_C(0x7fff);
        uint32_t registerIndex = handleKind & UINT32_C(0xffff);
        if (frameID == 0 || frameID > state.callDepth + 1 ||
            !validRegister(state.frames[frameID]->function, registerIndex))
          return OBELISK_RT_INVALID_HANDLE;
        localFrame = state.frames[frameID];
        localLayout = layoutAt(image, localFrame->function, registerIndex);
        if (localLayout.kind != OBELISK_RT_DBREG_BITS &&
            localLayout.kind != OBELISK_RT_DBREG_LOGIC &&
            localLayout.kind != OBELISK_RT_DBREG_REAL32 &&
            localLayout.kind != OBELISK_RT_DBREG_REAL64)
          return OBELISK_RT_INVALID_HANDLE;
        localValue = readLogic(localFrame->data, localLayout);
      } else if (!context) {
        return OBELISK_RT_INVALID_ARGUMENT;
      }
      bool changed = false;
      {
        std::unique_lock<std::recursive_mutex> lock;
        if (!local)
          lock = std::unique_lock<std::recursive_mutex>(context->mutex);
        if (automatic) {
          uint32_t id = 0;
          int64_t baseOffset = 0;
          if (!decodeAutomaticHandle(automaticBase, id, baseOffset) ||
              baseOffset != begin)
            return OBELISK_RT_INVALID_HANDLE;
          auto found = context->nativeAutomaticStates.find(id);
          if (found == context->nativeAutomaticStates.end())
            return OBELISK_RT_INVALID_HANDLE;
          automaticState = &found->second;
        } else if (boundedStatic) {
          auto found = context->nativeStaticStates.find(staticID);
          if (found == context->nativeStaticStates.end() ||
              staticBegin != begin || !context->execution ||
              found->second.bitOffset > context->execution->state_bit_count ||
              found->second.bitWidth >
                  context->execution->state_bit_count - found->second.bitOffset)
            return OBELISK_RT_INVALID_HANDLE;
          staticState = &found->second;
        }
        if (isOverride) {
          if (start < 0 || value.width == 0 ||
              value.width > static_cast<uint64_t>(end - start))
            return OBELISK_RT_INVALID_HANDLE;
          uint64_t overrideBegin =
              boundedStatic
                  ? staticState->bitOffset + static_cast<uint64_t>(start)
                  : static_cast<uint64_t>(start);
          if (!dynamicOverride || overrideClaim)
            obelisk_rt_claim_override_range_unlocked(
                context, overrideBegin, value.width, isAssignOverride,
                dynamicOverride ? overrideOwner : 0, retiredOverrideOwners);
        }
        auto automaticBit = [](const std::vector<uint8_t> &plane,
                               uint64_t index) {
          return index / 8 < plane.size() &&
                 ((plane[index / 8] >> (index % 8)) & 1) != 0;
        };
        auto setAutomaticBit = [](std::vector<uint8_t> &plane, uint64_t index,
                                  bool enabled) {
          if (index / 8 >= plane.size())
            return;
          uint8_t mask = static_cast<uint8_t>(1u << (index % 8));
          plane[index / 8] =
              enabled ? plane[index / 8] | mask
                      : plane[index / 8] & static_cast<uint8_t>(~mask);
        };
        struct PendingTransition {
          uint64_t bitIndex;
          uint64_t handle;
          bool oldValue;
          bool oldUnknown;
          bool newValue;
          bool newUnknown;
        };
        std::vector<PendingTransition> transitions;
        if (!local && !isLoad)
          transitions.reserve(static_cast<size_t>(std::min<uint64_t>(
              value.width, std::numeric_limits<size_t>::max())));
        if (isOverride) {
          // A Tier-3 force/assign can be followed immediately by a native
          // fragment or foreign reentry. Revoke direct addressing before
          // publishing the new ownership, even without an AOT schedule.
          invalidateNativeStaticSpecializationFastUnlocked(context);
          size_t limbs = context->stateValue.size();
          if (isAssignOverride) {
            if (context->assignMask.empty()) {
              context->assignMask.assign(limbs, 0);
              context->assignValue.assign(limbs, 0);
              context->assignUnknown.assign(limbs, 0);
            }
          } else if (context->forceMask.empty()) {
            context->forceMask.assign(limbs, 0);
          }
        }
        if (isContinuous && !local && !automatic &&
            context->continuousMask.empty()) {
          size_t limbs = context->stateValue.size();
          context->continuousMask.assign(limbs, 0);
          context->continuousValue.assign(limbs, 0);
          context->continuousUnknown.assign(limbs, 0);
        }
        bool equalStringContents = false;
        if (valueLayout.kind == OBELISK_RT_DBREG_STRING && !isLoad) {
          if (local || descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE ||
              valueLayout.width != 64 || start < 0 || start % 64 != 0 ||
              end - start < 64)
            return OBELISK_RT_INVALID_HANDLE;
          obelisk_rt_string_v1 previous = 0;
          obelisk_rt_string_v1 next = 0;
          std::memcpy(&next, frame.data + valueLayout.offset, sizeof(next));
          if (automatic) {
            uint32_t id = 0;
            int64_t baseOffset = 0;
            if (!decodeAutomaticHandle(automaticBase, id, baseOffset) ||
                baseOffset != start)
              return OBELISK_RT_INVALID_HANDLE;
            auto found = context->nativeAutomaticStates.find(id);
            if (found == context->nativeAutomaticStates.end())
              return OBELISK_RT_INVALID_HANDLE;
            NativeAutomaticState &state = found->second;
            uint64_t byteOffset = static_cast<uint64_t>(start) / 8;
            if (state.managedRootRegistered) {
              if (start != 0)
                return OBELISK_RT_INVALID_HANDLE;
              previous =
                  obelisk_rt_managed_word_from_object(state.managedValue);
            } else {
              if (byteOffset > state.value.size() ||
                  sizeof(previous) > state.value.size() - byteOffset)
                return OBELISK_RT_INVALID_HANDLE;
              std::memcpy(&previous, state.value.data() + byteOffset,
                          sizeof(previous));
            }
          } else {
            uint64_t absolute = static_cast<uint64_t>(start);
            if (boundedStatic) {
              auto found = context->nativeStaticStates.find(staticID);
              if (found == context->nativeStaticStates.end())
                return OBELISK_RT_INVALID_HANDLE;
              absolute = found->second.bitOffset + static_cast<uint64_t>(start);
            }
            if (absolute % 64 != 0 ||
                absolute / 64 >= context->stateValue.size())
              return OBELISK_RT_INVALID_HANDLE;
            std::memcpy(&previous, &context->stateValue[absolute / 64],
                        sizeof(previous));
          }
          if (obelisk_rt_validate_string(context, previous) != OBELISK_RT_OK ||
              obelisk_rt_validate_string(context, next) != OBELISK_RT_OK)
            return OBELISK_RT_INVALID_HANDLE;
          equalStringContents =
              obelisk_rt_v1_string_compare(previous, next) == 0;
        }
        bool realValue = valueLayout.kind == OBELISK_RT_DBREG_REAL32 ||
                         valueLayout.kind == OBELISK_RT_DBREG_REAL64;
        Logic oldReal{value.width, false, LimbVector(limbCount(value.width)),
                      LimbVector(limbCount(value.width))};
        uint64_t mirroredBegin = UINT64_MAX;
        uint64_t mirroredEnd = 0;
        // Fully bounded canonical reads share the native packed-plane helper.
        // Keep partial/invalid views on the bitwise path for IEEE 1800-2023
        // 11.5.1's per-bit X/zero fill. Publication overlays still run below.
        bool packedLoad = isLoad && !local && !automatic && start >= 0 &&
                          start >= begin && start <= end &&
                          value.width <= static_cast<uint64_t>(end - start);
        if (packedLoad) {
          uint64_t absolute = static_cast<uint64_t>(start);
          uint64_t available = boundedStatic ? staticState->bitWidth
                                             : context->stateValue.size() * 64;
          if (absolute > available || value.width > available - absolute)
            return OBELISK_RT_INVALID_HANDLE;
          uint64_t source =
              boundedStatic ? staticState->bitOffset + absolute : absolute;
          // Canonical value and unknown planes normally have identical size.
          // Retain the old zero-fill behavior if an unknown plane is shorter.
          packedLoad =
              source <= context->stateUnknown.size() * 64 &&
              value.width <= context->stateUnknown.size() * 64 - source;
          if (packedLoad) {
            for (uint64_t bitIndex = 0; bitIndex < value.width;
                 bitIndex += 64) {
              uint64_t width = std::min<uint64_t>(64, value.width - bitIndex);
              value.value[bitIndex / 64] =
                  loadPackedBits(context->stateValue, source + bitIndex, width);
              value.unknown[bitIndex / 64] = loadPackedBits(
                  context->stateUnknown, source + bitIndex, width);
            }
          }
        }
        for (uint64_t bitIndex = packedLoad ? value.width : 0;
             bitIndex != value.width; ++bitIndex) {
          bool valid = bitIndex <= uint64_t{INT64_MAX} &&
                       start <= INT64_MAX - static_cast<int64_t>(bitIndex);
          int64_t coordinate =
              valid ? start + static_cast<int64_t>(bitIndex) : -1;
          valid &= coordinate >= begin && coordinate < end && coordinate >= 0;
          uint64_t absolute = valid ? static_cast<uint64_t>(coordinate) : 0;
          uint64_t available = local       ? localValue.width
                               : automatic ? automaticState->bitWidth
                               : boundedStatic
                                   ? staticState->bitWidth
                                   : context->stateValue.size() * 64;
          if (valid && absolute >= available)
            return OBELISK_RT_INVALID_HANDLE;
          uint64_t storageBit =
              boundedStatic ? staticState->bitOffset + absolute : absolute;
          if (!valid) {
            if (instruction.opcode == OBELISK_RT_DB_LOAD_STATE) {
              setBit(value.value, bitIndex, false);
              setBit(value.unknown, bitIndex, value.fourState);
            }
            continue;
          }
          if (isLoad) {
            bool loadedValue =
                automatic ? automaticBit(automaticState->value, absolute)
                : local   ? bit(localValue.value, storageBit)
                          : bit(context->stateValue, storageBit);
            bool loadedUnknown =
                automatic ? automaticBit(automaticState->unknown, absolute)
                : local   ? bit(localValue.unknown, storageBit)
                          : bit(context->stateUnknown, storageBit);
            setBit(value.value, bitIndex, loadedValue);
            setBit(value.unknown, bitIndex, loadedUnknown);
          } else {
            uint64_t forceMask = uint64_t{1} << (storageBit % 64);
            bool forced =
                storageBit / 64 < context->forceMask.size() &&
                (context->forceMask[storageBit / 64] & forceMask) != 0;
            bool assigned =
                storageBit / 64 < context->assignMask.size() &&
                (context->assignMask[storageBit / 64] & forceMask) != 0;
            bool newValue = bit(value.value, bitIndex);
            bool newUnknown = bit(value.unknown, bitIndex);
            if (isContinuous && !local && !automatic) {
              setBit(context->continuousValue, storageBit, newValue);
              setBit(context->continuousUnknown, storageBit, newUnknown);
              context->continuousMask[storageBit / 64] |= forceMask;
            }
            if (!isOverride && !local && !automatic && (forced || assigned))
              continue;
            bool oldValue = automatic
                                ? automaticBit(automaticState->value, absolute)
                            : local ? bit(localValue.value, storageBit)
                                    : bit(context->stateValue, storageBit);
            bool oldUnknown =
                automatic ? automaticBit(automaticState->unknown, absolute)
                : local   ? bit(localValue.unknown, storageBit)
                          : bit(context->stateUnknown, storageBit);
            if (dynamicOverride && !overrideClaim &&
                !obelisk_rt_override_owner_matches_unlocked(
                    context, storageBit, isAssignOverride, overrideOwner))
              continue;
            if (realValue)
              setBit(oldReal.value, bitIndex, oldValue);
            if (isOverride) {
              uint64_t limb = storageBit / 64;
              if (isAssignOverride) {
                context->assignMask[limb] |= forceMask;
                setBit(context->assignValue, storageBit, newValue);
                setBit(context->assignUnknown, storageBit, newUnknown);
                if (forced) {
                  newValue = oldValue;
                  newUnknown = oldUnknown;
                }
              } else {
                context->forceMask[limb] |= forceMask;
              }
            }
            if (!realValue && !equalStringContents) {
              changed |= oldValue != newValue;
              changed |= oldUnknown != newUnknown;
            }
            if (automatic) {
              setAutomaticBit(automaticState->value, absolute, newValue);
              setAutomaticBit(automaticState->unknown, absolute, newUnknown);
            } else if (local) {
              setBit(localValue.value, storageBit, newValue);
              setBit(localValue.unknown, storageBit, newUnknown);
            } else {
              setBit(context->stateValue, storageBit, newValue);
              setBit(context->stateUnknown, storageBit, newUnknown);
              mirroredBegin = std::min(mirroredBegin, storageBit);
              mirroredEnd = std::max(mirroredEnd, storageBit + 1);
            }
            if (!local && !realValue && !equalStringContents)
              transitions.push_back(
                  {bitIndex,
                   automatic ? (automaticBase & ~uint64_t{UINT32_MAX}) |
                                   static_cast<uint32_t>(absolute)
                   : boundedStatic ? encodeStaticHandle(staticID, coordinate)
                                   : absolute,
                   oldValue, oldUnknown, newValue, newUnknown});
          }
        }
        // Bytecode is the tier-three authority for the canonical vectors, but
        // a native schedule plan can remain bound while individual actors
        // fall back to bytecode.  Keep its flat planes coherent immediately:
        // Preponed sampling and a later native handover may read them before
        // another generated fragment happens to touch the same byte.
        if (mirroredBegin != UINT64_MAX)
          obelisk_rt_sync_native_state_range_unlocked(
              context, mirroredBegin, mirroredEnd - mirroredBegin);
        if (isLoad && !eventValue && !local && !automatic &&
            context->observerForcesCanonicalPlane &&
            context->conditionPublication) {
          uint64_t stable = UINT64_MAX;
          if (!encodeCanonicalHandle(frame.data + handleLayout.offset, stable))
            return OBELISK_RT_INVALID_HANDLE;
          for (uint64_t bit = 0; bit != value.width; ++bit) {
            bool publishedValue = false;
            bool publishedUnknown = false;
            if (!obelisk_rt_read_clock_condition_publication_bit_unlocked(
                    context, stable, bit, publishedValue, publishedUnknown))
              continue;
            setBit(value.value, bit, publishedValue);
            setBit(value.unknown, bit, publishedUnknown);
          }
        }
        bool realNotified = false;
        if (realValue && !isLoad && !local) {
          if (valueLayout.kind == OBELISK_RT_DBREG_REAL32) {
            float oldValue = 0.0f;
            float newValue = 0.0f;
            std::memcpy(&oldValue, oldReal.value.data(), sizeof(oldValue));
            std::memcpy(&newValue, value.value.data(), sizeof(newValue));
            changed = oldValue != newValue || std::isnan(newValue);
          } else {
            double oldValue = 0.0;
            double newValue = 0.0;
            std::memcpy(&oldValue, oldReal.value.data(), sizeof(oldValue));
            std::memcpy(&newValue, value.value.data(), sizeof(newValue));
            changed = oldValue != newValue || std::isnan(newValue);
          }
          if (changed) {
            uint64_t realHandle =
                automatic ? (automaticBase & ~uint64_t{UINT32_MAX}) |
                                static_cast<uint32_t>(start)
                : boundedStatic ? encodeStaticHandle(staticID, start)
                                : static_cast<uint64_t>(start);
            if (!obelisk_rt_publish_signal_occurrence_unlocked(
                    context, realHandle, value.width, OBELISK_RT_SIGNAL_CHANGE))
              return context->schedulerStatus;
            obelisk_rt_invalidate_signal_snapshots_unlocked(context, realHandle,
                                                            value.width);
            if (!obelisk_rt_latch_conditional_signal_range_unlocked(
                    context, realHandle, value.width, OBELISK_RT_SIGNAL_CHANGE))
              return context->schedulerStatus;
            if (!obelisk_rt_notify_observer_signal_unlocked(context, realHandle,
                                                            value.width))
              return context->schedulerStatus;
            if (++context->schedulerEpoch == 0)
              context->schedulerEpoch = 1;
            realNotified = true;
          }
        }
        // A blocking assignment publishes its complete packed value before
        // any observer samples it. Preserve per-bit edges in one range batch
        // instead of repeating the subscription lookup for every packed bit.
        if (!transitions.empty()) {
          uint64_t firstBit = transitions.front().bitIndex;
          uint64_t publicationWidth =
              transitions.back().bitIndex - firstBit + 1;
          PackedSignalTransitionBuffer packed(publicationWidth);
          bool anyTransition = false;
          for (const PendingTransition &transition : transitions) {
            uint32_t edges =
                transitionEdges(transition.oldValue, transition.oldUnknown,
                                transition.newValue, transition.newUnknown);
            if (edges == 0)
              continue;
            packed.record(transition.bitIndex - firstBit, edges);
            anyTransition = true;
          }
          if (anyTransition) {
            uint64_t sequence = 0;
            if (!obelisk_rt_publish_signal_transition_batch_unlocked(
                    context, transitions.front().handle, publicationWidth,
                    packed.changed(), packed.posedge(), packed.negedge(), 0,
                    &sequence))
              return context->schedulerStatus;
            obelisk_rt_invalidate_signal_snapshots_unlocked(
                context, transitions.front().handle, publicationWidth);
            if (obelisk_rt_has_conditional_signal_waiters(context)) {
              for (const PendingTransition &transition : transitions) {
                if (transition.oldValue == transition.newValue &&
                    transition.oldUnknown == transition.newUnknown)
                  continue;
                context->signalValueSnapshots[transition.handle] = {
                    sequence, transition.newValue, transition.newUnknown};
              }
              for (const PendingTransition &transition : transitions) {
                uint32_t edges =
                    transitionEdges(transition.oldValue, transition.oldUnknown,
                                    transition.newValue, transition.newUnknown);
                if (edges != 0 &&
                    !obelisk_rt_latch_conditional_signal_waiters_unlocked(
                        context, transition.handle, edges))
                  return context->schedulerStatus;
              }
            }
            if (!obelisk_rt_notify_observer_signal_unlocked(
                    context, transitions.front().handle, publicationWidth))
              return context->schedulerStatus;
          }
        }
        if (!local && !automatic && !isOverride &&
            instruction.opcode == OBELISK_RT_DB_STORE_STATE &&
            descriptorKind == OBELISK_RT_DESCRIPTOR_DRIVER && start >= 0 &&
            begin < end &&
            (instruction.flags &
             OBELISK_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION) == 0 &&
            !resolveDrivenNets(
                image, context,
                boundedStatic
                    ? static_cast<int64_t>(staticState->bitOffset) + begin
                    : begin,
                boundedStatic
                    ? static_cast<int64_t>(staticState->bitOffset) + end
                    : end,
                changed))
          return OBELISK_RT_INVALID_HANDLE;
        if (!local && changed && !realNotified &&
            ++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
      }
      if (instruction.opcode == OBELISK_RT_DB_STORE_STATE &&
          (instruction.flags & OBELISK_RT_DB_STORE_STATE_CHANGED) != 0) {
        if ((instruction.flags &
             OBELISK_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION) != 0)
          changed = false;
        Logic changedValue{1, false, LimbVector(1), LimbVector(1)};
        setBit(changedValue.value, 0, changed);
        write(instruction.destination, changedValue);
      }
      if (local && instruction.opcode == OBELISK_RT_DB_STORE_STATE)
        writeLogic(localFrame->data, localLayout, localValue);
      if (isOverride) {
        obelisk_rt_status status = obelisk_rt_retire_override_owners(
            context, std::move(retiredOverrideOwners));
        if (status != OBELISK_RT_OK)
          return status;
      }
      if (isLoad) {
        if (!eventValue) {
          write(valueRegister, value);
          break;
        }
        uint64_t stable = value.value[0];
        uint8_t *address = frame.data + valueLayout.offset;
        std::memset(address, 0, valueLayout.size);
        uint32_t kind = OBELISK_RT_DESCRIPTOR_EVENT;
        int64_t start = -1;
        int64_t end = -1;
        uint64_t base = 0;
        if (stable != UINT64_MAX) {
          if (isDynamicEventStableHandle(stable)) {
            start = static_cast<int64_t>(stable);
            end = start == INT64_MAX ? start : start + 1;
            base = stable;
          } else if (decodeGlobalHandle(stable, start)) {
            end = start == INT64_MAX ? start : start + 1;
            base = static_cast<uint64_t>(start);
          } else {
            return OBELISK_RT_INVALID_HANDLE;
          }
        }
        std::memcpy(address, &kind, sizeof(kind));
        std::memcpy(address + 8, &base, sizeof(base));
        std::memcpy(address + 16, &start, sizeof(start));
        std::memcpy(address + 24, &end, sizeof(end));
      }
      break;
    }
    case OBELISK_RT_DB_RELEASE_STATE: {
      if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED)
        return OBELISK_RT_INVALID_LIFECYCLE;
      Layout handleLayout = layout(instruction.source0);
      if (handleLayout.kind != OBELISK_RT_DBREG_HANDLE)
        return OBELISK_RT_INVALID_HANDLE;
      uint32_t handleKind = 0;
      int64_t start = kInvalidHandleStart, end = 0;
      uint64_t base = 0;
      std::memcpy(&handleKind, frame.data + handleLayout.offset, 4);
      std::memcpy(&base, frame.data + handleLayout.offset + 8, 8);
      std::memcpy(&start, frame.data + handleLayout.offset + 16, 8);
      std::memcpy(&end, frame.data + handleLayout.offset + 24, 8);
      bool local = (handleKind & kLocalHandleKind) != 0;
      bool automatic = (handleKind & kAutomaticHandleKind) != 0;
      uint32_t descriptorKind =
          handleKind & ~(kLocalHandleKind | kAutomaticHandleKind);
      uint32_t staticID = 0;
      int64_t staticBegin = 0;
      bool boundedStatic = !local && !automatic &&
                           decodeStaticHandle(base, staticID, staticBegin);
      if (local || automatic || start < 0 || start > end ||
          (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE &&
           descriptorKind != OBELISK_RT_DESCRIPTOR_NET) ||
          (instruction.flags == OBELISK_RT_DB_OVERRIDE_ASSIGN &&
           descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE))
        return OBELISK_RT_INVALID_HANDLE;

      const NativeStaticState *staticState = nullptr;
      if (boundedStatic) {
        auto found = context->nativeStaticStates.find(staticID);
        if (found == context->nativeStaticStates.end() || staticBegin < 0 ||
            start < staticBegin ||
            static_cast<uint64_t>(end) > found->second.bitWidth)
          return OBELISK_RT_INVALID_HANDLE;
        staticState = &found->second;
      } else if (static_cast<uint64_t>(end) > context->stateValue.size() * 64) {
        return OBELISK_RT_INVALID_HANDLE;
      }
      uint64_t storageBegin =
          boundedStatic ? staticState->bitOffset + static_cast<uint64_t>(start)
                        : static_cast<uint64_t>(start);
      uint64_t width = static_cast<uint64_t>(end - start);
      bool releaseAssign = instruction.flags == OBELISK_RT_DB_OVERRIDE_ASSIGN;
      std::vector<uint64_t> retiredOverrideOwners;

      struct PendingTransition {
        uint64_t bitIndex;
        uint64_t handle;
        bool oldValue;
        bool oldUnknown;
        bool newValue;
        bool newUnknown;
      };
      std::vector<PendingTransition> transitions;
      transitions.reserve(static_cast<size_t>(
          std::min<uint64_t>(width, std::numeric_limits<size_t>::max())));
      {
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        if (descriptorKind == OBELISK_RT_DESCRIPTOR_NET) {
          for (uint64_t offset = 0; offset != width; ++offset)
            obelisk_rt_release_override_range_unlocked(
                context,
                obelisk_rt_canonical_net_bit_unlocked(context,
                                                      storageBegin + offset),
                1, releaseAssign, retiredOverrideOwners);
        } else {
          obelisk_rt_release_override_range_unlocked(context, storageBegin,
                                                     width, releaseAssign,
                                                     retiredOverrideOwners);
        }
        for (uint64_t bitIndex = 0; bitIndex != width; ++bitIndex) {
          uint64_t storageBit = storageBegin + bitIndex;
          uint64_t mask = uint64_t{1} << (storageBit % 64);
          uint64_t limb = storageBit / 64;
          bool oldValue = bit(context->stateValue, storageBit);
          bool oldUnknown = bit(context->stateUnknown, storageBit);
          bool newValue = oldValue;
          bool newUnknown = oldUnknown;
          if (releaseAssign) {
            if (limb < context->assignMask.size())
              context->assignMask[limb] &= ~mask;
          } else {
            if (limb < context->forceMask.size())
              context->forceMask[limb] &= ~mask;
          }
          bool forced = limb < context->forceMask.size() &&
                        (context->forceMask[limb] & mask) != 0;
          bool assigned = limb < context->assignMask.size() &&
                          (context->assignMask[limb] & mask) != 0;
          bool retained = limb < context->continuousMask.size() &&
                          (context->continuousMask[limb] & mask) != 0;
          if (descriptorKind == OBELISK_RT_DESCRIPTOR_STORAGE && !forced &&
              (assigned || retained)) {
            if (assigned) {
              newValue = bit(context->assignValue, storageBit);
              newUnknown = bit(context->assignUnknown, storageBit);
            } else {
              newValue = bit(context->continuousValue, storageBit);
              newUnknown = bit(context->continuousUnknown, storageBit);
            }
            setBit(context->stateValue, storageBit, newValue);
            setBit(context->stateUnknown, storageBit, newUnknown);
          }
          if (oldValue != newValue || oldUnknown != newUnknown) {
            int64_t coordinate = start + static_cast<int64_t>(bitIndex);
            transitions.push_back(
                {bitIndex,
                 boundedStatic ? encodeStaticHandle(staticID, coordinate)
                               : storageBit,
                 oldValue, oldUnknown, newValue, newUnknown});
          }
        }
        if (!transitions.empty()) {
          PackedSignalTransitionBuffer packed(width);
          for (const PendingTransition &transition : transitions)
            packed.record(
                transition.bitIndex,
                transitionEdges(transition.oldValue, transition.oldUnknown,
                                transition.newValue, transition.newUnknown));
          uint64_t publishedHandle = boundedStatic
                                         ? encodeStaticHandle(staticID, start)
                                         : storageBegin;
          uint64_t sequence = 0;
          if (!obelisk_rt_publish_signal_transition_batch_unlocked(
                  context, publishedHandle, width, packed.changed(),
                  packed.posedge(), packed.negedge(), 0, &sequence))
            return context->schedulerStatus;
          obelisk_rt_invalidate_signal_snapshots_unlocked(
              context, publishedHandle, width);
          if (obelisk_rt_has_conditional_signal_waiters(context)) {
            for (const PendingTransition &transition : transitions)
              context->signalValueSnapshots[transition.handle] = {
                  sequence, transition.newValue, transition.newUnknown};
            for (const PendingTransition &transition : transitions) {
              uint32_t edges =
                  transitionEdges(transition.oldValue, transition.oldUnknown,
                                  transition.newValue, transition.newUnknown);
              if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
                      context, transition.handle, edges))
                return context->schedulerStatus;
            }
          }
          if (!obelisk_rt_notify_observer_signal_unlocked(
                  context, publishedHandle, width))
            return context->schedulerStatus;
        }
      }
      if (!releaseAssign && descriptorKind == OBELISK_RT_DESCRIPTOR_NET) {
        obelisk_rt_status status =
            obelisk_rt_release_design_nets(context, storageBegin, width);
        if (status != OBELISK_RT_OK)
          return status;
        status = obelisk_rt_retire_override_owners(
            context, std::move(retiredOverrideOwners));
        if (status != OBELISK_RT_OK)
          return status;
        break;
      }
      if (!transitions.empty() && ++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
      {
        obelisk_rt_status status = obelisk_rt_retire_override_owners(
            context, std::move(retiredOverrideOwners));
        if (status != OBELISK_RT_OK)
          return status;
      }
      break;
    }
    case OBELISK_RT_DB_JUMP:
      if (!copyMap(image, frame, frame, instruction.source0,
                   instruction.source1, context))
        return OBELISK_RT_INVALID_BYTECODE;
      pc = instruction.immediate;
      break;
    case OBELISK_RT_DB_BRANCH: {
      Logic condition = read(instruction.destination);
      if (anyUnknown(condition) || !isZero(condition)) {
        if (!copyMap(image, frame, frame, instruction.source0,
                     instruction.source1, context))
          return OBELISK_RT_INVALID_BYTECODE;
        pc = instruction.immediate;
      }
      break;
    }
    case OBELISK_RT_DB_CALL: {
      Function calleeFunction = functionAt(image, instruction.source0);
      if (state.callDepth >= 1024)
        return OBELISK_RT_OUT_OF_RESOURCES;
      ScopedReusableByteBuffer storage(
          context, static_cast<size_t>(calleeFunction.scratchSize));
      Frame callee{calleeFunction, instruction.source0, storage.data(),
                   state.callDepth + 2};
      state.frames[callee.id] = &callee;
      ++state.callDepth;
      if (!copyMap(image, frame, callee, instruction.source1,
                   instruction.source2, context)) {
        --state.callDepth;
        return OBELISK_RT_INVALID_BYTECODE;
      }
      obelisk_rt_fragment_action_v1 callerAction = *action;
      *action = {
          OBELISK_RT_FRAGMENT_CONTINUE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
      obelisk_rt_status status =
          executeFunction(image, callee, context, canonicalFrame,
                          canonicalFrameSize, calleeFunction.firstInstruction,
                          budget, action, state, pendingActivation,
                          instruction.auxiliary, instruction.immediate, &frame);
      --state.callDepth;
      if (status != OBELISK_RT_OK)
        return status;
      if (action->kind != OBELISK_RT_FRAGMENT_CONTINUE)
        return OBELISK_RT_OK;
      *action = callerAction;
      break;
    }
    case OBELISK_RT_DB_VIRTUAL_CALL:
    case OBELISK_RT_DB_INTERFACE_CALL: {
      Layout receiverLayout = layout(instruction.source0);
      obelisk_rt_managed_word_v1 receiverWord = 0;
      std::memcpy(&receiverWord, frame.data + receiverLayout.offset,
                  sizeof(receiverWord));
      obelisk_rt_object_v1 *receiver =
          obelisk_rt_object_from_managed_word(receiverWord);
      if (obelisk_rt_managed_word_from_object(receiver) != receiverWord)
        return OBELISK_RT_INVALID_BYTECODE;
      const obelisk_rt_method_descriptor_v1 *method = nullptr;
      obelisk_rt_status status;
      if (instruction.opcode == OBELISK_RT_DB_INTERFACE_CALL) {
        auto [interfaceID, ordinal] = operandAt(image, instruction.destination);
        status = obelisk_rt_v1_interface_method_resolve(
            receiver, interfaceID, ordinal, instruction.immediate, &method);
      } else {
        status = obelisk_rt_v1_method_resolve(receiver, instruction.destination,
                                              instruction.immediate, &method);
      }
      if (status != OBELISK_RT_OK)
        return status;
      if (!method ||
          method->bytecode_function == OBELISK_RT_METHOD_NO_BYTECODE ||
          method->bytecode_function >= image.functionCount)
        return OBELISK_RT_TIER_UNAVAILABLE;
      Function calleeFunction = functionAt(image, method->bytecode_function);
      if ((calleeFunction.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0 ||
          calleeFunction.argumentCount != instruction.source2 ||
          calleeFunction.resultCount != instruction.flags ||
          !validMap(image, frame.function, calleeFunction, instruction.source1,
                    instruction.source2) ||
          !validMap(image, calleeFunction, frame.function,
                    instruction.auxiliary, instruction.flags))
        return OBELISK_RT_INVALID_BYTECODE;
      for (uint32_t index = 0; index != calleeFunction.argumentCount; ++index)
        if (operandAt(image, instruction.source1 + index).first != index)
          return OBELISK_RT_INVALID_BYTECODE;
      for (uint32_t index = 0; index != calleeFunction.resultCount; ++index)
        if (operandAt(image, instruction.auxiliary + index).second !=
            calleeFunction.argumentCount + index)
          return OBELISK_RT_INVALID_BYTECODE;
      if (state.callDepth >= 1024)
        return OBELISK_RT_OUT_OF_RESOURCES;
      ScopedReusableByteBuffer storage(
          context, static_cast<size_t>(calleeFunction.scratchSize));
      Frame callee{calleeFunction, method->bytecode_function, storage.data(),
                   state.callDepth + 2};
      state.frames[callee.id] = &callee;
      ++state.callDepth;
      if (!copyMap(image, frame, callee, instruction.source1,
                   instruction.source2, context)) {
        --state.callDepth;
        return OBELISK_RT_INVALID_BYTECODE;
      }
      obelisk_rt_fragment_action_v1 callerAction = *action;
      *action = {
          OBELISK_RT_FRAGMENT_CONTINUE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
      status = executeFunction(
          image, callee, context, canonicalFrame, canonicalFrameSize,
          calleeFunction.firstInstruction, budget, action, state,
          pendingActivation, instruction.auxiliary, instruction.flags, &frame);
      --state.callDepth;
      if (status != OBELISK_RT_OK)
        return status;
      if (action->kind != OBELISK_RT_FRAGMENT_CONTINUE)
        return OBELISK_RT_OK;
      *action = callerAction;
      break;
    }
    case OBELISK_RT_DB_RETURN:
      if (!copyMap(image, frame, frame, instruction.source0,
                   instruction.source1, context))
        return OBELISK_RT_INVALID_BYTECODE;
      if (!caller)
        return OBELISK_RT_OK;
      if (!copyMap(image, frame, *caller, returnFirst, returnCount, context))
        return OBELISK_RT_INVALID_BYTECODE;
      return OBELISK_RT_OK;
    case OBELISK_RT_DB_CONTINUE:
      *action = {OBELISK_RT_FRAGMENT_CONTINUE,
                 OBELISK_RT_SUSPEND_NONE,
                 static_cast<uint32_t>(instruction.immediate),
                 0,
                 0,
                 0};
      return OBELISK_RT_OK;
    case OBELISK_RT_DB_PROCESS_CONTROL: {
      Logic value = read(instruction.source0);
      if (anyUnknown(value) || value.value.size() != 1)
        return OBELISK_RT_INVALID_BYTECODE;
      obelisk_rt_process_control_disposition disposition =
          OBELISK_RT_PROCESS_CONTROL_CONTINUE;
      obelisk_rt_status status = obelisk_rt_v1_process_control(
          context, value.value[0], instruction.flags, &disposition);
      if (status != OBELISK_RT_OK)
        return status;
      switch (disposition) {
      case OBELISK_RT_PROCESS_CONTROL_CONTINUE: {
        std::optional<uint64_t> continuation =
            continuationPC(image, frame.function,
                           static_cast<uint32_t>(instruction.immediate));
        if (!continuation)
          return OBELISK_RT_INVALID_CONTINUATION;
        pc = *continuation;
        continue;
      }
      case OBELISK_RT_PROCESS_CONTROL_SUSPEND_CURRENT:
        *action = {OBELISK_RT_FRAGMENT_PROCESS_SUSPEND,
                   OBELISK_RT_SUSPEND_NONE,
                   static_cast<uint32_t>(instruction.immediate),
                   0,
                   0,
                   0};
        break;
      case OBELISK_RT_PROCESS_CONTROL_KILL_CURRENT:
        *action = {
            OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
        break;
      default:
        return OBELISK_RT_INVALID_ARGUMENT;
      }
      return OBELISK_RT_OK;
    }
    case OBELISK_RT_DB_SUSPEND: {
      uint64_t payload = 0;
      if (instruction.source0 != kInvalidRegister) {
        Logic value = read(instruction.source0);
        if (anyUnknown(value) || value.value.size() > 1)
          return OBELISK_RT_INVALID_BYTECODE;
        payload = value.value[0];
      }
      *action = {OBELISK_RT_FRAGMENT_SUSPEND,
                 static_cast<uint32_t>(instruction.flags),
                 static_cast<uint32_t>(instruction.immediate),
                 instruction.auxiliary,
                 payload,
                 0};
      return OBELISK_RT_OK;
    }
    case OBELISK_RT_DB_TASK_CALL: {
      obelisk_rt_status status = prepareTaskActivation(
          image, frame, context, instruction.source0, instruction.source1,
          instruction.source2, pendingActivation);
      if (status != OBELISK_RT_OK)
        return status;
      *action = {OBELISK_RT_FRAGMENT_TASK_CALL,
                 OBELISK_RT_SUSPEND_NONE,
                 static_cast<uint32_t>(instruction.immediate),
                 0,
                 0,
                 0};
      return OBELISK_RT_OK;
    }
    case OBELISK_RT_DB_VIRTUAL_TASK_CALL:
    case OBELISK_RT_DB_INTERFACE_TASK_CALL: {
      Layout receiverLayout = layout(instruction.source0);
      obelisk_rt_managed_word_v1 receiverWord = 0;
      std::memcpy(&receiverWord, frame.data + receiverLayout.offset,
                  sizeof(receiverWord));
      obelisk_rt_object_v1 *receiver =
          obelisk_rt_object_from_managed_word(receiverWord);
      if (obelisk_rt_managed_word_from_object(receiver) != receiverWord)
        return OBELISK_RT_INVALID_BYTECODE;
      const obelisk_rt_method_descriptor_v1 *method = nullptr;
      obelisk_rt_status status;
      if (instruction.opcode == OBELISK_RT_DB_INTERFACE_TASK_CALL) {
        auto [interfaceID, ordinal] = operandAt(image, instruction.destination);
        status = obelisk_rt_v1_interface_method_resolve(
            receiver, interfaceID, ordinal, instruction.immediate, &method);
      } else {
        status = obelisk_rt_v1_method_resolve(receiver, instruction.destination,
                                              instruction.immediate, &method);
      }
      if (status != OBELISK_RT_OK)
        return status;
      if (!method || (method->flags & OBELISK_RT_METHOD_TASK) == 0)
        return OBELISK_RT_INVALID_ARGUMENT;
      if ((method->flags & OBELISK_RT_METHOD_PURE) != 0 ||
          method->bytecode_function == OBELISK_RT_METHOD_NO_BYTECODE)
        return OBELISK_RT_TIER_UNAVAILABLE;
      status = prepareTaskActivation(
          image, frame, context, method->bytecode_function, instruction.source1,
          instruction.source2, pendingActivation);
      if (status != OBELISK_RT_OK)
        return status;
      *action = {OBELISK_RT_FRAGMENT_TASK_CALL,
                 OBELISK_RT_SUSPEND_NONE,
                 instruction.auxiliary,
                 0,
                 0,
                 0};
      return OBELISK_RT_OK;
    }
    case OBELISK_RT_DB_TERMINATE:
      *action = {OBELISK_RT_FRAGMENT_TERMINATE,
                 OBELISK_RT_SUSPEND_NONE,
                 0,
                 0,
                 instruction.immediate,
                 0};
      return OBELISK_RT_OK;
    case OBELISK_RT_DB_FAIL: {
      Layout status = layout(instruction.source0);
      if (status.kind != OBELISK_RT_DBREG_STATUS)
        return OBELISK_RT_INVALID_BYTECODE;
      uint64_t value;
      std::memcpy(&value, frame.data + status.offset, sizeof(value));
      if (value == 0)
        break;
      if (value > OBELISK_RT_FATAL)
        return OBELISK_RT_INVALID_BYTECODE;
      return static_cast<obelisk_rt_status>(value);
    }
    case OBELISK_RT_DB_INTRINSIC: {
      obelisk_rt_status status = invokeIntrinsic(
          image, frame, context, static_cast<uint32_t>(instruction.immediate));
      if (status != OBELISK_RT_OK)
        return status;
      break;
    }
    default:
      return OBELISK_RT_INVALID_BYTECODE;
    }
  }
  return OBELISK_RT_INVALID_BYTECODE;
}

std::optional<uint64_t> continuationPC(const Image &image,
                                       const Function &function,
                                       uint32_t continuation) {
  uint64_t low = 0, high = function.continuationCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    if (continuationAt(image, function.firstContinuation + middle).id <
        continuation)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == function.continuationCount)
    return std::nullopt;
  Continuation entry = continuationAt(image, function.firstContinuation + low);
  if (entry.id != continuation)
    return std::nullopt;
  return entry.instruction;
}

std::optional<uint32_t> continuationScheduleRank(const Image &image,
                                                 const Function &function,
                                                 uint32_t continuation) {
  uint64_t low = 0, high = function.continuationCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    if (continuationAt(image, function.firstContinuation + middle).id <
        continuation)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == function.continuationCount)
    return std::nullopt;
  Continuation entry = continuationAt(image, function.firstContinuation + low);
  if (entry.id != continuation)
    return std::nullopt;
  return entry.scheduleRank;
}

bool indexedSignalBlocked(const ScheduledDesignTask &task) {
  return obelisk_rt_design_signal_wait_blocked(task);
}

OBELISK_RT_FEATURE_HELPER bool
designReadyCohortLater(const DesignReadyCohortEntry &lhs,
                       const DesignReadyCohortEntry &rhs) {
  return std::tuple{lhs.region, lhs.rank, lhs.insertionSequence} >
         std::tuple{rhs.region, rhs.rank, rhs.insertionSequence};
}

OBELISK_RT_FEATURE_HELPER bool classifyDirectDesignReadyCohortMember(
    const ScheduledDesignTask &task, uint32_t activePhase,
    uint32_t unstartedActorRegion, DesignReadyCohortEntry &entry) {
  if (task.id == 0 || task.terminated || task.explicitlySuspended ||
      !task.started || task.phase != activePhase || task.urgent ||
      task.prioritySignal ||
      (task.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
       task.suspendKind != OBELISK_RT_SUSPEND_EDGE))
    return false;
  bool signalTriggered =
      task.signalTriggered || (task.signalLatch && task.signalLatch->triggered);
  if (!signalTriggered ||
      (task.queuedRegion >= unstartedActorRegion && !task.prioritySignal))
    return false;
  // Clause 31 slot-final waits are a tiny cold feature cohort whose effective
  // scheduler region depends on its wait flag. Keep them out of the ordinary
  // direct-signal cache so the common entry layout and classifier stay flat.
  const obelisk_rt_wait_record_v1 *wait = designTaskCurrentWait(task);
  if (obelisk_rt_is_slot_final_clock_occurrence_wait(wait))
    return false;
  entry.id = task.id;
  entry.region = designTaskOrderingRegion(task, signalTriggered);
  entry.rank = task.scheduleRank;
  entry.insertionSequence = task.insertionSequence;
  return true;
}

OBELISK_RT_FEATURE_TEXT bool trySelectCachedDesignReadyCohort(
    obelisk_rt_context *context, uint32_t activePhase,
    uint32_t unstartedActorRegion, size_t &candidateIndex,
    DesignReadyCohortEntry &selected) {
  DesignReadyCohortState *cohort = context->designReadyCohort.get();
  if (!cohort || !cohort->valid ||
      context->nativeScheduleDesignTaskFilterActive ||
      cohort->selectionGeneration != context->schedulerSelectionGeneration ||
      cohort->schedulerTime != context->schedulerTime ||
      cohort->runningFinals != context->schedulerRunningFinals ||
      cohort->nextDesignTaskID != context->nextDesignTaskID ||
      cohort->ready.size() + cohort->slowCandidates.size() !=
          context->designPollCandidates.size() ||
      cohort->ready.empty())
    return false;

  const DesignReadyCohortEntry &cached = cohort->ready.back();
  auto indexed = context->scheduledDesignTaskIndices.find(cached.id);
  if (indexed == context->scheduledDesignTaskIndices.end() ||
      indexed->second >= context->scheduledDesignTasks.size() ||
      !context->designPollCandidates.count(cached.id))
    return false;
  candidateIndex = indexed->second;
  const ScheduledDesignTask &task =
      context->scheduledDesignTasks[candidateIndex];
  DesignReadyCohortEntry current;
  if (context->signalDiagnosticsEnabled) {
    ++context->signalDiagnostics.candidateScans;
    ++context->signalDiagnostics.readinessCalls;
  }
  if (!classifyDirectDesignReadyCohortMember(task, activePhase,
                                             unstartedActorRegion, current) ||
      current.id != cached.id || current.region != cached.region ||
      current.rank != cached.rank ||
      current.insertionSequence != cached.insertionSequence)
    return false;
  selected = cached;
  return true;
}

OBELISK_RT_FEATURE_TEXT void
installDesignReadyCohort(obelisk_rt_context *context,
                         std::vector<DesignReadyCohortEntry> ready,
                         std::vector<uint64_t> slowCandidates) {
  std::sort(ready.begin(), ready.end(), designReadyCohortLater);
  if (!context->designReadyCohort)
    context->designReadyCohort = std::make_unique<DesignReadyCohortState>();
  DesignReadyCohortState &cohort = *context->designReadyCohort;
  cohort.ready = std::move(ready);
  cohort.slowCandidates = std::move(slowCandidates);
  cohort.selectionGeneration = context->schedulerSelectionGeneration;
  cohort.schedulerTime = context->schedulerTime;
  cohort.nextDesignTaskID = context->nextDesignTaskID;
  cohort.suppressedCandidateHighWater = 0;
  cohort.runningFinals = context->schedulerRunningFinals;
  cohort.valid = true;
  cohort.suppressed = false;
  cohort.persistentSuppression = false;
  context->designReadyCohortExactScan = false;
}

OBELISK_RT_FEATURE_TEXT void
suppressDesignReadyCohort(obelisk_rt_context *context, bool persistent) {
  if (!context->designReadyCohort)
    context->designReadyCohort = std::make_unique<DesignReadyCohortState>();
  DesignReadyCohortState &cohort = *context->designReadyCohort;
  cohort.ready.clear();
  cohort.slowCandidates.clear();
  cohort.selectionGeneration = context->schedulerSelectionGeneration;
  cohort.schedulerTime = context->schedulerTime;
  cohort.nextDesignTaskID = context->nextDesignTaskID;
  cohort.suppressedCandidateHighWater = context->designPollCandidates.size();
  cohort.runningFinals = context->schedulerRunningFinals;
  cohort.valid = false;
  cohort.suppressed = true;
  cohort.persistentSuppression = persistent;
  context->designReadyCohortExactScan = persistent;
}

bool designReadyCohortSuppressed(const obelisk_rt_context *context) {
  const DesignReadyCohortState *cohort = context->designReadyCohort.get();
  return cohort && cohort->suppressed &&
         !context->nativeScheduleDesignTaskFilterActive &&
         cohort->nextDesignTaskID == context->nextDesignTaskID &&
         context->designPollCandidates.size() <=
             cohort->suppressedCandidateHighWater &&
         (cohort->persistentSuppression ||
          (cohort->selectionGeneration ==
               context->schedulerSelectionGeneration &&
           cohort->schedulerTime == context->schedulerTime &&
           cohort->runningFinals == context->schedulerRunningFinals));
}

void invalidateDesignReadyCohort(obelisk_rt_context *context) {
  obelisk_rt_invalidate_design_ready_cohort(context);
}

void rebuildDesignSchedulerIndexUnlocked(obelisk_rt_context *context) {
  context->scheduledDesignTaskIndices.clear();
  context->designPollCandidates.clear();
  context->scheduledDesignTaskIndices.reserve(
      context->scheduledDesignTasks.size());
  context->designPollCandidates.reserve(context->scheduledDesignTasks.size());
  for (size_t index = 0; index != context->scheduledDesignTasks.size();
       ++index) {
    const ScheduledDesignTask &task = context->scheduledDesignTasks[index];
    context->scheduledDesignTaskIndices[task.id] = index;
    if (!task.terminated && !indexedSignalBlocked(task))
      context->designPollCandidates.insert(task.id);
  }
}

} // namespace

obelisk_rt_status obelisk_rt_execute_design_observer(
    const obelisk_rt_execution_descriptor_v1 &execution,
    obelisk_rt_context *context, uint32_t functionIndex,
    const obelisk_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t *value, uint64_t *unknown, uint32_t outputLimbs) noexcept {
  if (!context || !value || !unknown || (captureCount != 0 && !captures))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ManagedExecutionScope managedExecution(context);
    if (managedExecution.getStatus() != OBELISK_RT_OK)
      return managedExecution.getStatus();
    obelisk_rt_design_bytecode_entry_v1 entry{&execution, functionIndex, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image) ||
        functionIndex >= image.functionCount)
      return OBELISK_RT_INVALID_BYTECODE;
    const obelisk_rt_observer_descriptor_v1 *descriptor = nullptr;
    for (uint64_t index = 0; index != execution.observer_count; ++index)
      if (execution.observers[index].bytecode_function == functionIndex) {
        descriptor = &execution.observers[index];
        break;
      }
    if (!descriptor || descriptor->capture_count != captureCount)
      return OBELISK_RT_INVALID_DESIGN;
    Function function = functionAt(image, functionIndex);
    if (function.id != descriptor->code_unit_id ||
        function.argumentCount != captureCount + 1 ||
        function.resultCount != 1 ||
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout contextLayout = layoutAt(image, function, 0);
    Layout resultLayout = layoutAt(image, function, function.argumentCount);
    bool fourState = (descriptor->flags & OBELISK_RT_OBSERVER_FOUR_STATE) != 0;
    uint32_t expectedKind =
        (descriptor->flags & OBELISK_RT_OBSERVER_REAL32) != 0
            ? OBELISK_RT_DBREG_REAL32
        : (descriptor->flags & OBELISK_RT_OBSERVER_REAL64) != 0
            ? OBELISK_RT_DBREG_REAL64
        : fourState ? OBELISK_RT_DBREG_LOGIC
                    : OBELISK_RT_DBREG_BITS;
    if (contextLayout.kind != OBELISK_RT_DBREG_HANDLE ||
        contextLayout.size != 32 || resultLayout.kind != expectedKind ||
        resultLayout.width != descriptor->result_width)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t resultLimbs =
        static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
    if (outputLimbs < resultLimbs)
      return OBELISK_RT_ARGUMENT_MISMATCH;
    ScopedReusableByteBuffer storage(context,
                                     static_cast<size_t>(function.scratchSize));
    Frame frame{function, functionIndex, storage.data(), 1};
    {
      std::lock_guard<std::recursive_mutex> captureLock(context->mutex);
      for (uint32_t index = 0; index != captureCount; ++index) {
        Layout layout = layoutAt(image, function, index + 1);
        uint8_t *address = frame.data + layout.offset;
        uint64_t stable = captures[index].stable_id;
        const obelisk_rt_observer_capture_abi_v1 &abi =
            descriptor->capture_abi[index];
        if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_ARGUMENT_REF) {
          if (layout.kind != OBELISK_RT_DBREG_ARGUMENT_REF ||
              layout.size != 24 || captures[index].payload1 > 2 ||
              captures[index].payload2 != 0)
            return OBELISK_RT_INVALID_BYTECODE;
          std::memcpy(address, &captures[index], 24);
          continue;
        }
        if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_MANAGED) {
          if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8)
            return OBELISK_RT_INVALID_BYTECODE;
          std::memcpy(address, &stable, sizeof(stable));
          continue;
        }
        if (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP) {
          if (layout.kind != OBELISK_RT_DBREG_BITS || layout.width != 64 ||
              layout.size != 8)
            return OBELISK_RT_INVALID_BYTECODE;
          std::memcpy(address, &stable, sizeof(stable));
          continue;
        }
        if (layout.kind != OBELISK_RT_DBREG_HANDLE || layout.size != 32)
          return OBELISK_RT_INVALID_BYTECODE;
        uint32_t kind = abi.kind == OBELISK_RT_OBSERVER_CAPTURE_STORAGE
                            ? OBELISK_RT_DESCRIPTOR_STORAGE
                        : abi.kind == OBELISK_RT_OBSERVER_CAPTURE_NET
                            ? OBELISK_RT_DESCRIPTOR_NET
                        : abi.kind == OBELISK_RT_OBSERVER_CAPTURE_EVENT
                            ? OBELISK_RT_DESCRIPTOR_EVENT
                            : OBELISK_RT_DESCRIPTOR_DRIVER;
        int64_t start = kInvalidHandleStart;
        int64_t begin = 0;
        int64_t end = 0;
        uint64_t base = 0;
        uint32_t dynamicID = 0;
        int64_t dynamicOffset = 0;
        if (isDynamicEventHandle(kind, stable)) {
          start = static_cast<int64_t>(stable);
          begin = start;
          end = start == INT64_MAX ? start : start + 1;
          base = stable;
        } else if (decodeAutomaticHandle(stable, dynamicID, dynamicOffset)) {
          if (kind != OBELISK_RT_DESCRIPTOR_STORAGE)
            return OBELISK_RT_INVALID_HANDLE;
          auto found = context->nativeAutomaticStates.find(dynamicID);
          if (found == context->nativeAutomaticStates.end() ||
              found->second.bitWidth > uint64_t{INT64_MAX})
            return OBELISK_RT_INVALID_HANDLE;
          kind |= kAutomaticHandleKind;
          start = dynamicOffset;
          int64_t available = static_cast<int64_t>(found->second.bitWidth);
          if (start > INT64_MAX - static_cast<int64_t>(abi.width))
            return OBELISK_RT_INVALID_HANDLE;
          begin = std::max<int64_t>(0, start);
          end = std::min<int64_t>(available,
                                  start + static_cast<int64_t>(abi.width));
          if (begin > end)
            begin = end;
          base = encodeAutomaticHandle(dynamicID, begin);
        } else if (decodeStaticHandle(stable, dynamicID, dynamicOffset)) {
          auto found = context->nativeStaticStates.find(dynamicID);
          if (found == context->nativeStaticStates.end() ||
              found->second.bitWidth > uint64_t{INT64_MAX})
            return OBELISK_RT_INVALID_HANDLE;
          start = dynamicOffset;
          int64_t available = static_cast<int64_t>(found->second.bitWidth);
          if (start > INT64_MAX - static_cast<int64_t>(abi.width))
            return OBELISK_RT_INVALID_HANDLE;
          begin = std::max<int64_t>(0, start);
          end = std::min<int64_t>(available,
                                  start + static_cast<int64_t>(abi.width));
          if (begin > end)
            begin = end;
          base = encodeStaticHandle(dynamicID, begin);
        } else if (decodeGlobalHandle(stable, start)) {
          begin = start;
          if (kind <= OBELISK_RT_DESCRIPTOR_DRIVER) {
            if (start > INT64_MAX - static_cast<int64_t>(abi.width))
              return OBELISK_RT_INVALID_HANDLE;
            int64_t available =
                execution.state_bit_count <= uint64_t{INT64_MAX}
                    ? static_cast<int64_t>(execution.state_bit_count)
                    : 0;
            begin = std::max<int64_t>(0, start);
            end = std::min<int64_t>(available,
                                    start + static_cast<int64_t>(abi.width));
            if (begin > end)
              begin = end;
          } else {
            end = start == INT64_MAX ? start : start + 1;
          }
          base = static_cast<uint64_t>(begin);
        } else {
          return OBELISK_RT_INVALID_HANDLE;
        }
        std::memcpy(address, &kind, 4);
        std::memcpy(address + 8, &base, 8);
        std::memcpy(address + 16, &start, 8);
        std::memcpy(address + 24, &end, 8);
      }
    }
    ExecutionState state;
    state.frames[frame.id] = &frame;
    StepBudget budget{UINT64_MAX, 0};
    obelisk_rt_fragment_action_v1 action{};
    obelisk_rt_status status = executeFunction(image, frame, context, nullptr,
                                               0, function.firstInstruction,
                                               budget, &action, state, nullptr);
    if (status != OBELISK_RT_OK)
      return status;
    Layout result = layoutAt(image, function, function.argumentCount);
    Logic evaluated = readLogic(frame.data, result);
    std::fill(value, value + outputLimbs, 0);
    std::fill(unknown, unknown + outputLimbs, 0);
    std::copy(evaluated.value.begin(), evaluated.value.end(), value);
    if (evaluated.fourState)
      std::copy(evaluated.unknown.begin(), evaluated.unknown.end(), unknown);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_BYTECODE; }
}

obelisk_rt_status obelisk_rt_execute_design_export(
    const obelisk_rt_execution_descriptor_v1 &execution,
    const obelisk_rt_export_descriptor_v1 &descriptor,
    obelisk_rt_context *context, const obelisk_rt_import_input_v1 *inputs,
    uint32_t inputCount, obelisk_rt_import_output_v1 *outputs,
    uint32_t outputCount) noexcept {
  if (!context || inputCount != descriptor.input_count ||
      outputCount != descriptor.output_count || (inputCount != 0 && !inputs) ||
      (outputCount != 0 && !outputs) ||
      descriptor.bytecode_function == OBELISK_RT_EXPORT_NO_BYTECODE)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ManagedExecutionScope managedExecution(context);
    if (managedExecution.getStatus() != OBELISK_RT_OK)
      return managedExecution.getStatus();
    uint32_t functionIndex = descriptor.bytecode_function;
    obelisk_rt_design_bytecode_entry_v1 entry{&execution, functionIndex, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image) ||
        functionIndex >= image.functionCount)
      return OBELISK_RT_INVALID_BYTECODE;
    Function function = functionAt(image, functionIndex);
    if (function.id != descriptor.code_unit_id ||
        function.argumentCount != inputCount + 1 ||
        function.resultCount != outputCount ||
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout contextLayout = layoutAt(image, function, 0);
    if (contextLayout.kind != OBELISK_RT_DBREG_HANDLE ||
        contextLayout.size != 32)
      return OBELISK_RT_INVALID_BYTECODE;

    auto matches = [](const Layout &layout, uint8_t kind, uint8_t flags,
                      uint32_t width) {
      if (layout.width != width)
        return false;
      if ((flags & ~uint8_t{OBELISK_RT_DBREG_SIGNED}) != 0)
        return false;
      switch (kind) {
      case OBELISK_RT_DBREG_BITS:
        return layout.kind == OBELISK_RT_DBREG_BITS &&
               layout.size == limbCount(width) * sizeof(uint64_t);
      case OBELISK_RT_DBREG_LOGIC:
        return layout.kind == OBELISK_RT_DBREG_LOGIC &&
               layout.size == limbCount(width) * sizeof(uint64_t) * 2;
      case OBELISK_RT_DBREG_AGGREGATE:
        return (layout.kind == OBELISK_RT_DBREG_BITS &&
                layout.size == limbCount(width) * sizeof(uint64_t)) ||
               (layout.kind == OBELISK_RT_DBREG_LOGIC &&
                layout.size == limbCount(width) * sizeof(uint64_t) * 2);
      case OBELISK_RT_DBREG_STRING:
        return layout.kind == OBELISK_RT_DBREG_STRING && width == 64 &&
               layout.size == 8 && flags == 0;
      case OBELISK_RT_DBREG_REAL32:
        return layout.kind == OBELISK_RT_DBREG_REAL32 && width == 32 &&
               layout.size == 4 && flags == 0;
      case OBELISK_RT_DBREG_REAL64:
        return layout.kind == OBELISK_RT_DBREG_REAL64 && width == 64 &&
               layout.size == 8 && flags == 0;
      default:
        return false;
      }
    };
    auto copyIntoFrame = [&](uint8_t *frame, const Layout &layout,
                             const obelisk_rt_import_input_v1 &input) {
      if (!matches(layout, input.kind, input.flags, input.bit_width) ||
          input.limb_count != limbCount(input.bit_width) || !input.value ||
          (layout.kind == OBELISK_RT_DBREG_LOGIC) != (input.unknown != nullptr))
        return false;
      uint64_t bytes = input.limb_count * sizeof(uint64_t);
      if (input.kind == OBELISK_RT_DBREG_REAL32 ||
          input.kind == OBELISK_RT_DBREG_REAL64)
        bytes = input.bit_width / 8;
      std::memcpy(frame + layout.offset, input.value,
                  static_cast<size_t>(bytes));
      if (input.unknown)
        std::memcpy(frame + layout.offset + bytes, input.unknown,
                    static_cast<size_t>(bytes));
      return true;
    };
    auto copyFromFrame = [&](const uint8_t *frame, const Layout &layout,
                             obelisk_rt_import_output_v1 &output) {
      if (!matches(layout, output.kind, output.flags, output.bit_width) ||
          output.limb_count != limbCount(output.bit_width) || !output.value ||
          (layout.kind == OBELISK_RT_DBREG_LOGIC) !=
              (output.unknown != nullptr))
        return false;
      uint64_t bytes = output.limb_count * sizeof(uint64_t);
      if (output.kind == OBELISK_RT_DBREG_REAL32 ||
          output.kind == OBELISK_RT_DBREG_REAL64)
        bytes = output.bit_width / 8;
      std::memcpy(output.value, frame + layout.offset,
                  static_cast<size_t>(bytes));
      if (output.unknown)
        std::memcpy(output.unknown, frame + layout.offset + bytes,
                    static_cast<size_t>(bytes));
      return true;
    };

    for (uint32_t index = 0; index != outputCount; ++index) {
      const obelisk_rt_import_output_v1 &output = outputs[index];
      Layout layout = layoutAt(image, function, function.argumentCount + index);
      if (!matches(layout, output.kind, output.flags, output.bit_width) ||
          output.limb_count != limbCount(output.bit_width) || !output.value ||
          (layout.kind == OBELISK_RT_DBREG_LOGIC) !=
              (output.unknown != nullptr))
        return OBELISK_RT_INVALID_BYTECODE;
    }
    if (function.scratchSize > std::numeric_limits<size_t>::max())
      return OBELISK_RT_OUT_OF_RESOURCES;

    ScopedReusableByteBuffer storage(context,
                                     static_cast<size_t>(function.scratchSize));
    std::memset(storage.data(), 0, static_cast<size_t>(function.scratchSize));
    Frame frame{function, functionIndex, storage.data(), 1};
    for (uint32_t index = 0; index != inputCount; ++index)
      if (!copyIntoFrame(frame.data, layoutAt(image, function, index + 1),
                         inputs[index]))
        return OBELISK_RT_INVALID_BYTECODE;
    ExecutionState state;
    state.frames[frame.id] = &frame;
    StepBudget budget{UINT64_MAX, 0};
    obelisk_rt_fragment_action_v1 action{};
    obelisk_rt_status status = executeFunction(image, frame, context, nullptr,
                                               0, function.firstInstruction,
                                               budget, &action, state, nullptr);
    if (status != OBELISK_RT_OK)
      return status;
    for (uint32_t index = 0; index != outputCount; ++index)
      if (!copyFromFrame(
              frame.data,
              layoutAt(image, function, function.argumentCount + index),
              outputs[index]))
        return OBELISK_RT_INVALID_BYTECODE;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_BYTECODE; }
}

obelisk_rt_status obelisk_rt_execute_design_export_task(
    const obelisk_rt_execution_descriptor_v1 &execution,
    const obelisk_rt_export_descriptor_v1 &descriptor,
    obelisk_rt_context *context, const obelisk_rt_import_input_v1 *inputs,
    uint32_t inputCount, obelisk_rt_import_output_v1 *outputs,
    uint32_t outputCount, const uint8_t *directions,
    const int64_t *const *aggregatePlans,
    const uint64_t *aggregatePlanWords) noexcept {
  if (!context || inputCount != descriptor.input_count ||
      outputCount != descriptor.output_count ||
      (descriptor.flags & OBELISK_RT_EXPORT_TASK) == 0 ||
      descriptor.bytecode_function == OBELISK_RT_EXPORT_NO_BYTECODE ||
      (inputCount != 0 &&
       (!inputs || !directions || !aggregatePlans || !aggregatePlanWords)) ||
      (outputCount != 0 && !outputs))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ManagedExecutionScope managedExecution(context);
    if (managedExecution.getStatus() != OBELISK_RT_OK)
      return managedExecution.getStatus();
    uint32_t functionIndex = descriptor.bytecode_function;
    obelisk_rt_design_bytecode_entry_v1 entry{&execution, functionIndex, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image) ||
        functionIndex >= image.functionCount)
      return OBELISK_RT_INVALID_BYTECODE;
    Function function = functionAt(image, functionIndex);
    uint32_t copiedOutputs = 0;
    uint64_t physicalArguments = 1;
    for (uint32_t index = 0; index != inputCount; ++index) {
      uint8_t direction = directions[index] & 0x7fu;
      bool elided = (directions[index] & 0x80u) != 0;
      if (direction > 2 || (elided && direction != 1))
        return OBELISK_RT_INVALID_ARGUMENT;
      physicalArguments += !elided + (direction != 0);
      copiedOutputs += direction != 0;
    }
    if (function.id != descriptor.code_unit_id ||
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
        function.argumentCount != physicalArguments ||
        function.resultCount != 0 || copiedOutputs != outputCount)
      return OBELISK_RT_INVALID_BYTECODE;

    auto matches = [](const Layout &layout, uint8_t kind, uint8_t flags,
                      uint32_t width) {
      if (layout.width != width ||
          (flags & ~uint8_t{OBELISK_RT_DBREG_SIGNED}) != 0)
        return false;
      switch (kind) {
      case OBELISK_RT_DBREG_BITS:
        return layout.kind == OBELISK_RT_DBREG_BITS;
      case OBELISK_RT_DBREG_LOGIC:
        return layout.kind == OBELISK_RT_DBREG_LOGIC;
      case OBELISK_RT_DBREG_AGGREGATE:
        return layout.kind == OBELISK_RT_DBREG_BITS ||
               layout.kind == OBELISK_RT_DBREG_LOGIC;
      case OBELISK_RT_DBREG_STRING:
        return layout.kind == OBELISK_RT_DBREG_STRING && width == 64 &&
               flags == 0;
      case OBELISK_RT_DBREG_REAL32:
        return layout.kind == OBELISK_RT_DBREG_REAL32 && width == 32 &&
               flags == 0;
      case OBELISK_RT_DBREG_REAL64:
        return layout.kind == OBELISK_RT_DBREG_REAL64 && width == 64 &&
               flags == 0;
      default:
        return false;
      }
    };
    std::vector<CaptureRecord> captures(function.argumentCount);
    std::vector<uint8_t> hasCapture(function.argumentCount, 0);
    for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
      CaptureRecord capture = captureAt(image, index);
      if (capture.function != functionIndex)
        continue;
      if (capture.argument >= function.argumentCount ||
          hasCapture[capture.argument])
        return OBELISK_RT_INVALID_BYTECODE;
      captures[capture.argument] = capture;
      hasCapture[capture.argument] = 1;
    }
    if (std::find(hasCapture.begin(), hasCapture.end(), uint8_t{0}) !=
        hasCapture.end())
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t canonicalSize =
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK) >> 1;
    if (function.scratchAlignment == 0 ||
        canonicalSize > UINT64_MAX - (function.scratchAlignment - 1))
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t scratchOffset = (canonicalSize + function.scratchAlignment - 1) &
                             ~(function.scratchAlignment - 1);
    if (scratchOffset > UINT64_MAX - function.scratchSize ||
        scratchOffset + function.scratchSize >
            std::numeric_limits<size_t>::max())
      return OBELISK_RT_OUT_OF_MEMORY;

    ScheduledDesignTask task;
    task.parent = context->activeLogicalProcessToken;
    if (task.parent != 0)
      context->logicalProcessParentsWithChildren.insert(task.parent);
    task.programOwner = context->activeProgramOwner;
    obelisk_rt_random_split_unlocked(context, task.random);
    task.function = functionIndex;
    task.scheduleRank = static_cast<uint32_t>(function.initialScheduleRank);
    task.scratchOffset = scratchOffset;
    task.scratchSize = function.scratchSize;
    task.frame = context->designTaskFrames.acquire(
        static_cast<size_t>(scratchOffset + function.scratchSize));

    auto copyInput = [&](uint32_t argument,
                         const obelisk_rt_import_input_v1 &input) {
      Layout layout = layoutAt(image, function, argument);
      CaptureRecord capture = captures[argument];
      if (!matches(layout, input.kind, input.flags, input.bit_width) ||
          input.limb_count != limbCount(input.bit_width) || !input.value ||
          (layout.kind == OBELISK_RT_DBREG_LOGIC) !=
              (input.unknown != nullptr) ||
          capture.valueOffset == UINT64_MAX ||
          capture.valueOffset > scratchOffset ||
          capture.planeSize > scratchOffset - capture.valueOffset)
        return false;
      std::memcpy(task.frame.data() + capture.valueOffset, input.value,
                  static_cast<size_t>(capture.planeSize));
      if (capture.unknownOffset != UINT64_MAX) {
        if (!input.unknown || capture.unknownOffset > scratchOffset ||
            capture.planeSize > scratchOffset - capture.unknownOffset)
          return false;
        std::memcpy(task.frame.data() + capture.unknownOffset, input.unknown,
                    static_cast<size_t>(capture.planeSize));
      }
      return true;
    };
    auto copyHandle = [&](uint32_t argument, uint64_t handle) {
      Layout layout = layoutAt(image, function, argument);
      CaptureRecord capture = captures[argument];
      if (layout.kind != OBELISK_RT_DBREG_HANDLE ||
          capture.valueOffset == UINT64_MAX ||
          capture.valueOffset > scratchOffset ||
          sizeof(handle) > scratchOffset - capture.valueOffset)
        return false;
      std::memcpy(task.frame.data() + capture.valueOffset, &handle,
                  sizeof(handle));
      return true;
    };

    std::vector<uint64_t> owners;
    std::vector<uint64_t> retained;
    auto releaseUnscheduled = [&] {
      for (uint64_t handle : retained)
        (void)obelisk_rt_v1_native_state_release(context, handle, 0);
      for (uint64_t handle : owners)
        (void)obelisk_rt_v1_native_state_release(context, handle, 1);
    };
    uint32_t argument = 1, output = 0;
    for (uint32_t index = 0; index != inputCount; ++index) {
      uint8_t direction = directions[index] & 0x7fu;
      bool elided = (directions[index] & 0x80u) != 0;
      if (!elided && !copyInput(argument++, inputs[index])) {
        releaseUnscheduled();
        return OBELISK_RT_INVALID_BYTECODE;
      }
      if (direction == 0)
        continue;
      obelisk_rt_import_output_v1 &destination = outputs[output++];
      if (destination.kind != inputs[index].kind ||
          destination.flags != inputs[index].flags ||
          destination.bit_width != inputs[index].bit_width ||
          destination.limb_count != inputs[index].limb_count ||
          !destination.value ||
          (inputs[index].unknown != nullptr) !=
              (destination.unknown != nullptr)) {
        releaseUnscheduled();
        return OBELISK_RT_ARGUMENT_MISMATCH;
      }
      uint64_t handle = UINT64_MAX;
      obelisk_rt_status status;
      if (inputs[index].kind == OBELISK_RT_DBREG_AGGREGATE &&
          aggregatePlans[index] && aggregatePlanWords[index] != 0) {
        if (!designBytecodeDpiAggregateStateAlloc) {
          releaseUnscheduled();
          return OBELISK_RT_TIER_UNAVAILABLE;
        }
        status = designBytecodeDpiAggregateStateAlloc(
            context, inputs[index].bit_width,
            reinterpret_cast<const uint8_t *>(inputs[index].value),
            reinterpret_cast<const uint8_t *>(inputs[index].unknown),
            aggregatePlans[index], aggregatePlanWords[index], &handle);
      } else if (inputs[index].kind == OBELISK_RT_DBREG_STRING) {
        obelisk_rt_managed_root_slot_v1 root{
            0, OBELISK_RT_MANAGED_ROOT_KIND_STRING, 0};
        status = obelisk_rt_v1_native_state_alloc_with_typed_roots(
            context, inputs[index].bit_width,
            reinterpret_cast<const uint8_t *>(inputs[index].value), nullptr,
            &root, 1, &handle);
      } else {
        status = obelisk_rt_v1_native_state_alloc(
            context, inputs[index].bit_width,
            reinterpret_cast<const uint8_t *>(inputs[index].value),
            reinterpret_cast<const uint8_t *>(inputs[index].unknown), &handle);
      }
      if (status != OBELISK_RT_OK) {
        releaseUnscheduled();
        return status;
      }
      owners.push_back(handle);
      status = obelisk_rt_v1_native_state_retain(context, handle);
      if (status == OBELISK_RT_OK)
        retained.push_back(handle);
      if (status != OBELISK_RT_OK || !copyHandle(argument++, handle)) {
        releaseUnscheduled();
        return status != OBELISK_RT_OK ? status : OBELISK_RT_INVALID_BYTECODE;
      }
    }

    uint64_t id = 0;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->nextDesignTaskID == 0 ||
          context->nextDesignTaskID > uint64_t{INT64_MAX} ||
          context->nextProcessInsertionSequence == 0 ||
          context->nextProcessInsertionSequence == UINT64_MAX) {
        releaseUnscheduled();
        return OBELISK_RT_OUT_OF_RESOURCES;
      }
      id = context->nextDesignTaskID++;
      obelisk_rt_invalidate_design_ready_cohort(context);
      task.id = id;
      task.phase =
          context->activeDesignTaskID != 0 ? context->activeDesignTaskPhase : 0;
      task.homeRegion = functionHomeRegion(function);
      task.queuedRegion = task.homeRegion;
      task.controls = context->activeControls;
      task.insertionSequence = context->nextProcessInsertionSequence++;
      task.observedEpoch = context->schedulerEpoch;
      context->scheduledDesignTasks.push_back(std::move(task));
      context->scheduledDesignTaskIndices[id] =
          context->scheduledDesignTasks.size() - 1;
      context->designPollCandidates.insert(id);
      obelisk_rt_register_unstarted_actor(
          context, context->scheduledDesignTasks.back().phase, id);
      obelisk_rt_retain_controls_unlocked(
          context, context->scheduledDesignTasks.back().controls);
    }
    // The scheduled task now owns every retained capture reference.
    retained.clear();
    obelisk_rt_status status =
        obelisk_rt_run_dpi_export_task_logical(context, id);
    if (status == OBELISK_RT_OK) {
      for (uint32_t index = 0; index != outputCount; ++index) {
        obelisk_rt_import_output_v1 &destination = outputs[index];
        status = obelisk_rt_v1_native_state_load_plane(
            context, reinterpret_cast<const uint8_t *>(destination.value), 0,
            owners[index], destination.bit_width, 0, 0,
            reinterpret_cast<uint8_t *>(destination.value));
        if (status == OBELISK_RT_OK && destination.unknown)
          status = obelisk_rt_v1_native_state_load_plane(
              context, reinterpret_cast<const uint8_t *>(destination.unknown),
              0, owners[index], destination.bit_width, 1, 0,
              reinterpret_cast<uint8_t *>(destination.unknown));
        if (status != OBELISK_RT_OK)
          break;
      }
    }
    for (uint64_t handle : owners) {
      obelisk_rt_status released =
          obelisk_rt_v1_native_state_release(context, handle, 1);
      if (status == OBELISK_RT_OK && released != OBELISK_RT_OK)
        status = released;
    }
    return status;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_BYTECODE; }
}

static obelisk_rt_status executeDesignBytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    obelisk_rt_context *context, void *frame, uint64_t frameSize,
    uint64_t scratchOffset, uint64_t scratchSize, uint32_t continuation,
    uint64_t instructionLimit, obelisk_rt_fragment_action_v1 *outAction,
    std::unique_ptr<PendingDesignActivation> *pendingActivation) noexcept {
  if (!outAction || (frameSize != 0 && !frame))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    ManagedExecutionScope managedExecution(context);
    if (managedExecution.getStatus() != OBELISK_RT_OK)
      return managedExecution.getStatus();
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return OBELISK_RT_INVALID_BYTECODE;
    Function function = functionAt(image, entry.function);
    if (function.scratchSize > scratchSize || scratchOffset > frameSize ||
        scratchSize > frameSize - scratchOffset)
      return OBELISK_RT_INVALID_FRAME;
    std::optional<uint64_t> pc = continuationPC(image, function, continuation);
    if (!pc)
      return OBELISK_RT_INVALID_CONTINUATION;
    uint8_t *scratch = static_cast<uint8_t *>(frame) + scratchOffset;
    std::memset(scratch, 0, static_cast<size_t>(function.scratchSize));
    Frame top{function, entry.function, scratch, 1};
    ExecutionState state;
    state.frames[top.id] = &top;
    StepBudget budget{instructionLimit, 0};
    *outAction = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
    obelisk_rt_status status = executeFunction(
        image, top, context, static_cast<uint8_t *>(frame), scratchOffset, *pc,
        budget, outAction, state, pendingActivation);
    if (status != OBELISK_RT_OK ||
        outAction->kind != OBELISK_RT_FRAGMENT_TERMINATE)
      return status;
    status = releaseCapturedAutomaticStates(image, entry.function, context,
                                            static_cast<const uint8_t *>(frame),
                                            scratchOffset);
    return status;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_BYTECODE; }
}

obelisk_rt_status obelisk_rt_execute_design_bytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    obelisk_rt_context *context, void *frame, uint64_t frameSize,
    uint64_t scratchOffset, uint64_t scratchSize, uint32_t continuation,
    uint64_t instructionLimit,
    obelisk_rt_fragment_action_v1 *outAction) noexcept {
  return executeDesignBytecode(entry, context, frame, frameSize, scratchOffset,
                               scratchSize, continuation, instructionLimit,
                               outAction, nullptr);
}

namespace {

struct CancelledLogicalDesignActivation {
  uint64_t id;
  uint32_t function;
  uint64_t scratchOffset;
  std::vector<uint8_t> frame;
};

obelisk_rt_status cancelLogicalProcessTree(obelisk_rt_context *context,
                                           uint64_t root,
                                           uint64_t preservedActive,
                                           bool &preserved) {
  OBELISK_RT_TRY {
    preserved = false;
    std::vector<uint64_t> targets{root};
    std::vector<obelisk_rt_process_instance_v1 *> nativeInstances;
    std::vector<CancelledLogicalDesignActivation> designActivations;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      invalidateDesignReadyCohort(context);
      auto contains = [&](uint64_t token) {
        return std::find(targets.begin(), targets.end(), token) !=
               targets.end();
      };
      bool foundRoot = preservedActive == root &&
                       context->activeLogicalProcessToken == preservedActive;
      bool changed = true;
      while (changed) {
        changed = false;
        for (const ScheduledProcess &process : context->scheduledProcesses) {
          uint64_t token =
              OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG | process.token;
          foundRoot |= token == root && process.instance;
          if (process.instance && contains(process.parent) &&
              !contains(token)) {
            targets.push_back(token);
            changed = true;
          }
        }
        for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
          foundRoot |= task.id == root && !task.terminated;
          if (!task.terminated && contains(task.parent) && !contains(task.id)) {
            targets.push_back(task.id);
            changed = true;
          }
        }
        if (preservedActive != 0 && !contains(preservedActive) &&
            context->activeLogicalProcessToken == preservedActive &&
            contains(context->activeLogicalProcessParent)) {
          targets.push_back(preservedActive);
          changed = true;
        }
      }
      if (!foundRoot)
        return OBELISK_RT_INVALID_HANDLE;
      preserved = preservedActive != 0 && contains(preservedActive);

      size_t nativeCount = 0;
      size_t designCount = 0;
      size_t nativeTasks = 0;
      size_t designTasks = 0;
      for (const ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG | process.token;
        if (!process.instance || !contains(token) || token == preservedActive)
          continue;
        nativeCount = checkedSizeSum(nativeCount, process.callers.size() + 1);
        ++nativeTasks;
      }
      for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || !contains(task.id) || task.id == preservedActive)
          continue;
        designCount = checkedSizeSum(designCount, task.callers.size() + 1);
        ++designTasks;
      }
      nativeInstances.reserve(nativeCount);
      designActivations.reserve(designCount);
      context->terminatedNativeProcesses.reserveRanges(checkedSizeSum(
          context->terminatedNativeProcesses.rangeCount(), nativeTasks));
      context->terminatedDesignTasks.reserveRanges(checkedSizeSum(
          context->terminatedDesignTasks.rangeCount(), designTasks));
      context->terminatedNativeProcesses.reserveRandomStates(checkedSizeSum(
          context->terminatedNativeProcesses.randomStateCount(), nativeTasks));
      context->terminatedDesignTasks.reserveRandomStates(checkedSizeSum(
          context->terminatedDesignTasks.randomStateCount(), designTasks));
      context->killedNativeProcesses.reserveRanges(checkedSizeSum(
          context->killedNativeProcesses.rangeCount(),
          checkedSizeSum(nativeTasks,
                         preserved && (preservedActive &
                                       OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG)
                             ? 1
                             : 0)));
      context->killedDesignTasks.reserveRanges(checkedSizeSum(
          context->killedDesignTasks.rangeCount(),
          checkedSizeSum(designTasks,
                         preserved && !(preservedActive &
                                        OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG)
                             ? 1
                             : 0)));
      if (preserved) {
        if (preservedActive & OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG)
          context->killedNativeProcesses.insert(
              preservedActive & ~OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG);
        else
          context->killedDesignTasks.insert(preservedActive);
      }

      for (ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG | process.token;
        if (!process.instance || !contains(token) || token == preservedActive)
          continue;
        context->terminatedNativeProcesses.insert(process.token,
                                                  process.random);
        context->killedNativeProcesses.insert(process.token);
        obelisk_rt_program_complete_unlocked(context, token,
                                             process.programOwner);
        context->logicalProcessParentsWithChildren.erase(token);
        if (!process.started)
          obelisk_rt_unregister_unstarted_actor(context, process.phase, token);
        obelisk_rt_flush_deferred_immediate_reports_unlocked(context, token);
        nativeInstances.push_back(process.instance);
        nativeInstances.insert(nativeInstances.end(), process.callers.begin(),
                               process.callers.end());
        process.callers.clear();
        process.callerControlDepths.clear();
        obelisk_rt_unregister_signal_wait_unlocked(
            context, process.signalSubscriptions, process.token, false);
        process.instance = nullptr;
        ++context->schedulerDeadProcessCount;
        context->schedulerCompactionPending = true;
        obelisk_rt_release_controls_unlocked(context, process.controls);
        process.controls.clear();
        process.signalTriggered = false;
        process.explicitlySuspended = false;
      }
      for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || !contains(task.id) || task.id == preservedActive)
          continue;
        context->terminatedDesignTasks.insert(task.id, task.random);
        context->killedDesignTasks.insert(task.id);
        obelisk_rt_program_complete_unlocked(context, task.id,
                                             task.programOwner);
        context->logicalProcessParentsWithChildren.erase(task.id);
        if (!task.started)
          obelisk_rt_unregister_unstarted_actor(context, task.phase, task.id);
        obelisk_rt_flush_deferred_immediate_reports_unlocked(context, task.id);
        designActivations.push_back({task.id, task.function, task.scratchOffset,
                                     std::move(task.frame)});
        for (DesignActivation &caller : task.callers)
          designActivations.push_back({task.id, caller.function,
                                       caller.scratchOffset,
                                       std::move(caller.frame)});
        task.callers.clear();
        obelisk_rt_unregister_signal_wait_unlocked(
            context, task.signalSubscriptions, task.id, true);
        obelisk_rt_release_controls_unlocked(context, task.controls);
        task.controls.clear();
        task.terminated = true;
        ++context->schedulerDeadDesignTaskCount;
        context->schedulerCompactionPending = true;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
        task.explicitlySuspended = false;
        context->designPollCandidates.erase(task.id);
      }
      if (++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
    }

    obelisk_rt_status result = OBELISK_RT_OK;
    if (!designActivations.empty()) {
      if (!context->execution)
        result = OBELISK_RT_INVALID_LIFECYCLE;
      else {
        obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
        Image image;
        if (!loadValidatedImage(entry, context, image))
          result = OBELISK_RT_INVALID_BYTECODE;
        else
          for (const auto &activation : designActivations) {
            obelisk_rt_status status = releaseCapturedAutomaticStates(
                image, activation.function, context, activation.frame.data(),
                activation.scratchOffset);
            if (result == OBELISK_RT_OK && status != OBELISK_RT_OK)
              result = status;
          }
      }
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      uint64_t previous = 0;
      for (const auto &activation : designActivations)
        if (activation.id != previous) {
          releaseDesignTaskOwnedStatesUnlocked(context, activation.id);
          previous = activation.id;
        }
      for (auto &activation : designActivations)
        context->designTaskFrames.release(std::move(activation.frame));
    }
    for (obelisk_rt_process_instance_v1 *instance : nativeInstances) {
      obelisk_rt_status status =
          obelisk_rt_v1_process_instance_destroy(instance);
      if (result == OBELISK_RT_OK && status != OBELISK_RT_OK)
        result = status;
    }
    return result;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_process_control(
    obelisk_rt_context *context, uint64_t logicalProcess,
    obelisk_rt_process_control_kind kind,
    obelisk_rt_process_control_disposition *outDisposition) {
  if (!context || logicalProcess == 0 || !outDisposition ||
      kind > OBELISK_RT_PROCESS_CONTROL_RESUME)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outDisposition = OBELISK_RT_PROCESS_CONTROL_CONTINUE;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    uint64_t activeProcess = 0;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      invalidateDesignReadyCohort(context);
      // Compiled designs containing process.control are excluded from AOT.
      // Keep the public ABI safe for external callers too: mutating or
      // destroying actors still owned by a live generated plan would leave
      // its ready/deadline inventory stale.
      if (context->nativeSchedulePlan)
        return OBELISK_RT_TIER_UNAVAILABLE;
      bool native =
          (logicalProcess & OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG) != 0;
      uint64_t nativeToken =
          logicalProcess & ~OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG;
      bool terminated =
          native ? context->terminatedNativeProcesses.count(nativeToken) != 0
                 : context->terminatedDesignTasks.count(logicalProcess) != 0;
      if (terminated)
        return OBELISK_RT_OK;

      activeProcess = context->activeLogicalProcessToken;
      if (activeProcess == logicalProcess) {
        if (kind == OBELISK_RT_PROCESS_CONTROL_SUSPEND)
          *outDisposition = OBELISK_RT_PROCESS_CONTROL_SUSPEND_CURRENT;
        if (kind != OBELISK_RT_PROCESS_CONTROL_KILL)
          return OBELISK_RT_OK;
      }

      if (kind != OBELISK_RT_PROCESS_CONTROL_KILL) {
        if (native) {
          auto found = std::find_if(context->scheduledProcesses.begin(),
                                    context->scheduledProcesses.end(),
                                    [&](const ScheduledProcess &process) {
                                      return process.token == nativeToken &&
                                             process.instance;
                                    });
          if (found == context->scheduledProcesses.end())
            return OBELISK_RT_INVALID_HANDLE;
          found->explicitlySuspended =
              kind == OBELISK_RT_PROCESS_CONTROL_SUSPEND;
          context->nativePollCandidates.insert(nativeToken);
        } else {
          if (context->scheduledDesignTaskIndices.size() !=
              context->scheduledDesignTasks.size())
            rebuildDesignSchedulerIndexUnlocked(context);
          auto indexed =
              context->scheduledDesignTaskIndices.find(logicalProcess);
          if (indexed == context->scheduledDesignTaskIndices.end() ||
              indexed->second >= context->scheduledDesignTasks.size())
            return OBELISK_RT_INVALID_HANDLE;
          ScheduledDesignTask &task =
              context->scheduledDesignTasks[indexed->second];
          if (task.terminated || task.id != logicalProcess)
            return OBELISK_RT_INVALID_HANDLE;
          task.explicitlySuspended = kind == OBELISK_RT_PROCESS_CONTROL_SUSPEND;
          context->designPollCandidates.insert(logicalProcess);
        }
        if (++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
        return OBELISK_RT_OK;
      }
    }
    bool killedActive = false;
    obelisk_rt_status status = cancelLogicalProcessTree(
        context, logicalProcess, activeProcess, killedActive);
    if (status == OBELISK_RT_OK && killedActive)
      *outDisposition = OBELISK_RT_PROCESS_CONTROL_KILL_CURRENT;
    return status;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_disable_children(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  struct CancelledDesignTask {
    uint64_t id;
    uint32_t function;
    uint64_t scratchOffset;
    std::vector<uint8_t> frame;
  };
  OBELISK_RT_TRY {
    std::vector<uint64_t> descendants;
    std::vector<obelisk_rt_process_instance_v1 *> nativeInstances;
    std::vector<CancelledDesignTask> designTasks;
    std::vector<uint64_t> insertedNativeTerminations;
    std::vector<uint64_t> insertedDesignTerminations;
    std::vector<uint64_t> insertedNativeKills;
    std::vector<uint64_t> insertedDesignKills;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      invalidateDesignReadyCohort(context);
      uint64_t root = context->activeLogicalProcessToken;
      if (root == 0)
        return OBELISK_RT_INVALID_LIFECYCLE;
      descendants.reserve(
          checkedSizeSum(checkedSizeSum(context->scheduledProcesses.size(),
                                        context->scheduledDesignTasks.size()),
                         1));
      descendants.push_back(root);
      auto contains = [&](uint64_t token) {
        return std::find(descendants.begin(), descendants.end(), token) !=
               descendants.end();
      };
      bool changed = true;
      while (changed) {
        changed = false;
        for (const ScheduledProcess &process : context->scheduledProcesses) {
          uint64_t token = (UINT64_C(1) << 63) | process.token;
          if (process.instance && contains(process.parent) &&
              !contains(token)) {
            descendants.push_back(token);
            changed = true;
          }
        }
        for (const ScheduledDesignTask &task : context->scheduledDesignTasks)
          if (!task.terminated && contains(task.parent) && !contains(task.id)) {
            descendants.push_back(task.id);
            changed = true;
          }
      }

      size_t nativeActivationCount = 0;
      size_t designActivationCount = 0;
      size_t nativeTaskCount = 0;
      size_t designTaskCount = 0;
      for (const ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = (UINT64_C(1) << 63) | process.token;
        if (!process.instance || !contains(token) || token == root)
          continue;
        if (process.callers.size() == std::numeric_limits<size_t>::max())
          obelisk_rt_out_of_memory();
        size_t count = process.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - nativeActivationCount)
          obelisk_rt_out_of_memory();
        nativeActivationCount += count;
        ++nativeTaskCount;
      }
      for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || !contains(task.id) || task.id == root)
          continue;
        if (task.callers.size() == std::numeric_limits<size_t>::max())
          obelisk_rt_out_of_memory();
        size_t count = task.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - designActivationCount)
          obelisk_rt_out_of_memory();
        designActivationCount += count;
        ++designTaskCount;
      }
      nativeInstances.reserve(nativeActivationCount);
      designTasks.reserve(designActivationCount);
      insertedNativeTerminations.reserve(nativeTaskCount);
      insertedDesignTerminations.reserve(designTaskCount);
      insertedNativeKills.reserve(nativeTaskCount);
      insertedDesignKills.reserve(designTaskCount);
      context->terminatedNativeProcesses.reserveRanges(checkedSizeSum(
          context->terminatedNativeProcesses.rangeCount(), nativeTaskCount));
      context->terminatedDesignTasks.reserveRanges(checkedSizeSum(
          context->terminatedDesignTasks.rangeCount(), designTaskCount));
      context->terminatedNativeProcesses.reserveRandomStates(
          checkedSizeSum(context->terminatedNativeProcesses.randomStateCount(),
                         nativeTaskCount));
      context->terminatedDesignTasks.reserveRandomStates(checkedSizeSum(
          context->terminatedDesignTasks.randomStateCount(), designTaskCount));
      context->killedNativeProcesses.reserveRanges(checkedSizeSum(
          context->killedNativeProcesses.rangeCount(), nativeTaskCount));
      context->killedDesignTasks.reserveRanges(checkedSizeSum(
          context->killedDesignTasks.rangeCount(), designTaskCount));
      OBELISK_RT_TRY {
        for (const ScheduledProcess &process : context->scheduledProcesses) {
          uint64_t token = (UINT64_C(1) << 63) | process.token;
          if (!process.instance || !contains(token) || token == root)
            continue;
          if (context->terminatedNativeProcesses
                  .insert(process.token, process.random)
                  .second)
            insertedNativeTerminations.push_back(process.token);
          if (context->killedNativeProcesses.insert(process.token).second)
            insertedNativeKills.push_back(process.token);
        }
        for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
          if (task.terminated || !contains(task.id) || task.id == root)
            continue;
          if (context->terminatedDesignTasks.insert(task.id, task.random)
                  .second)
            insertedDesignTerminations.push_back(task.id);
          if (context->killedDesignTasks.insert(task.id).second)
            insertedDesignKills.push_back(task.id);
        }
      }
      OBELISK_RT_CATCH_ALL {
        for (uint64_t token : insertedNativeTerminations)
          context->terminatedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignTerminations)
          context->terminatedDesignTasks.erase(token);
        for (uint64_t token : insertedNativeKills)
          context->killedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignKills)
          context->killedDesignTasks.erase(token);
        OBELISK_RT_RETHROW;
      }
      for (ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = (UINT64_C(1) << 63) | process.token;
        if (!process.instance || !contains(token) || token == root)
          continue;
        nativeInstances.push_back(process.instance);
        nativeInstances.insert(nativeInstances.end(), process.callers.begin(),
                               process.callers.end());
        process.callers.clear();
        process.callerControlDepths.clear();
        obelisk_rt_unregister_signal_wait_unlocked(
            context, process.signalSubscriptions, process.token, false);
        context->logicalProcessParentsWithChildren.erase(token);
        process.instance = nullptr;
        ++context->schedulerDeadProcessCount;
        context->schedulerCompactionPending = true;
        obelisk_rt_release_controls_unlocked(context, process.controls);
        process.controls.clear();
        process.signalTriggered = false;
      }
      for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || !contains(task.id) || task.id == root)
          continue;
        designTasks.push_back({task.id, task.function, task.scratchOffset,
                               std::move(task.frame)});
        for (DesignActivation &activation : task.callers)
          designTasks.push_back({task.id, activation.function,
                                 activation.scratchOffset,
                                 std::move(activation.frame)});
        task.callers.clear();
        obelisk_rt_unregister_signal_wait_unlocked(
            context, task.signalSubscriptions, task.id, true);
        context->logicalProcessParentsWithChildren.erase(task.id);
        obelisk_rt_release_controls_unlocked(context, task.controls);
        task.controls.clear();
        task.terminated = true;
        ++context->schedulerDeadDesignTaskCount;
        context->schedulerCompactionPending = true;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
      }
      if (!nativeInstances.empty() || !designTasks.empty())
        if (++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
    }

    obelisk_rt_status result = OBELISK_RT_OK;
    if (!designTasks.empty()) {
      if (!context->execution)
        result = OBELISK_RT_INVALID_LIFECYCLE;
      else {
        obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
        Image image;
        if (!loadValidatedImage(entry, context, image))
          result = OBELISK_RT_INVALID_BYTECODE;
        else
          for (const CancelledDesignTask &task : designTasks) {
            obelisk_rt_status status = releaseCapturedAutomaticStates(
                image, task.function, context, task.frame.data(),
                task.scratchOffset);
            if (result == OBELISK_RT_OK && status != OBELISK_RT_OK)
              result = status;
          }
      }
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      uint64_t last = 0;
      for (const CancelledDesignTask &task : designTasks)
        if (task.id != last) {
          releaseDesignTaskOwnedStatesUnlocked(context, task.id);
          last = task.id;
        }
      for (CancelledDesignTask &task : designTasks)
        context->designTaskFrames.release(std::move(task.frame));
    }
    for (obelisk_rt_process_instance_v1 *instance : nativeInstances) {
      obelisk_rt_status status =
          obelisk_rt_v1_process_instance_destroy(instance);
      if (result == OBELISK_RT_OK && status != OBELISK_RT_OK)
        result = status;
    }
    return result;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_control_disable(obelisk_rt_context *context, uint64_t targetID,
                              uint64_t activation, uint32_t allActivations) {
  if (!context || targetID == 0 || allActivations > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  struct CancelledDesignTask {
    uint64_t id;
    uint32_t function;
    uint64_t scratchOffset;
    bool releaseOwnedStates;
    std::vector<uint8_t> frame;
  };
  OBELISK_RT_TRY {
    std::vector<uint64_t> targets;
    std::vector<obelisk_rt_process_instance_v1 *> nativeInstances;
    std::vector<CancelledDesignTask> designTasks;
    std::vector<uint64_t> insertedNativeTerminations;
    std::vector<uint64_t> insertedDesignTerminations;
    std::vector<uint64_t> insertedNativeKills;
    std::vector<uint64_t> insertedDesignKills;
    bool resumedControl = false;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      invalidateDesignReadyCohort(context);
      uint64_t current = context->activeLogicalProcessToken;
      if (current == 0)
        return OBELISK_RT_INVALID_LIFECYCLE;
      context->controlEscapePending = false;
      if (activation != 0) {
        auto found = context->controlActivations.find(activation);
        if (found == context->controlActivations.end() ||
            found->second.target != targetID ||
            std::find(context->activeControls.begin(),
                      context->activeControls.end(),
                      activation) == context->activeControls.end())
          return OBELISK_RT_INVALID_LIFECYCLE;
        targets.push_back(activation);
      } else if (allActivations != 0) {
        // An activation-less disable is a resolved hierarchical target. It
        // applies to every live activation of that exact elaborated identity.
        targets.reserve(context->controlActivations.size());
        for (const auto &[token, control] : context->controlActivations)
          if (control.target == targetID && control.memberships != 0)
            targets.push_back(token);
      } else {
        for (auto iterator = context->activeControls.rbegin();
             iterator != context->activeControls.rend(); ++iterator) {
          auto found = context->controlActivations.find(*iterator);
          if (found != context->controlActivations.end() &&
              found->second.target == targetID) {
            targets.push_back(*iterator);
            break;
          }
        }
      }
      // A labeled assertion is a stable assertion identity rather than a
      // live procedural control activation. Its report may therefore be
      // pending even when no dynamic activation with this target remains.
      obelisk_rt_cancel_deferred_immediate_assertion_unlocked(
          context, targetID, allActivations ? 0 : current);
      if (targets.empty())
        return OBELISK_RT_OK;
      std::unordered_map<uint64_t, ControlActivation> targetControls;
      targetControls.reserve(targets.size());
      for (uint64_t target : targets)
        if (auto found = context->controlActivations.find(target);
            found != context->controlActivations.end())
          targetControls.emplace(target, found->second);
      auto isTargetMember = [&](const std::vector<uint64_t> &controls) {
        return std::any_of(
            controls.begin(), controls.end(), [&](uint64_t control) {
              return targetControls.find(control) != targetControls.end();
            });
      };
      auto disablesDPIImport = [&](const std::vector<uint64_t> &controls,
                                   uint64_t dpiLogical) {
        return std::any_of(controls.begin(), controls.end(),
                           [&](uint64_t control) {
                             auto found = targetControls.find(control);
                             return found != targetControls.end() &&
                                    found->second.owner != dpiLogical;
                           });
      };
      if (context->activeDpiExportTaskLogical != 0) {
        uint64_t dpiLogical = context->activeDpiExportTaskLogical;
        if ((dpiLogical & OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0) {
          uint64_t token =
              dpiLogical & ~uint64_t{OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG};
          for (const ScheduledProcess &process : context->scheduledProcesses)
            if (process.instance && process.token == token &&
                disablesDPIImport(process.controls, dpiLogical)) {
              context->activeDpiExportTaskDisabled = true;
              break;
            }
        } else {
          auto indexed = context->scheduledDesignTaskIndices.find(dpiLogical);
          if (indexed != context->scheduledDesignTaskIndices.end() &&
              indexed->second < context->scheduledDesignTasks.size() &&
              disablesDPIImport(
                  context->scheduledDesignTasks[indexed->second].controls,
                  dpiLogical))
            context->activeDpiExportTaskDisabled = true;
        }
      }
      struct ResumeBoundary {
        size_t control;
        uint32_t continuation;
      };
      auto resumeBoundary = [&](uint64_t logicalProcess,
                                const std::vector<uint64_t> &controls)
          -> std::optional<ResumeBoundary> {
        for (size_t index = 0; index != controls.size(); ++index) {
          auto found = targetControls.find(controls[index]);
          if (found != targetControls.end() &&
              found->second.owner == logicalProcess &&
              found->second.continuation != 0)
            return ResumeBoundary{index, found->second.continuation};
        }
        return std::nullopt;
      };
      struct UnwindBoundary {
        size_t caller;
        size_t control;
      };
      auto targetedControl =
          [&](const std::vector<uint64_t> &controls) -> std::optional<size_t> {
        for (size_t index = 0; index != controls.size(); ++index)
          if (std::find(targets.begin(), targets.end(), controls[index]) !=
              targets.end())
            return index;
        return std::nullopt;
      };
      auto nativeUnwind = [&](const ScheduledProcess &process)
          -> std::optional<UnwindBoundary> {
        std::optional<size_t> control = targetedControl(process.controls);
        if (!control ||
            process.callerControlDepths.size() != process.callers.size())
          return std::nullopt;
        for (size_t index = 0; index != process.callerControlDepths.size();
             ++index)
          if (process.callerControlDepths[index] == *control)
            return UnwindBoundary{index, *control};
        return std::nullopt;
      };
      auto designUnwind = [&](const ScheduledDesignTask &task)
          -> std::optional<UnwindBoundary> {
        std::optional<size_t> control = targetedControl(task.controls);
        if (!control)
          return std::nullopt;
        for (size_t index = 0; index != task.callers.size(); ++index)
          if (task.callers[index].controlDepth == *control)
            return UnwindBoundary{index, *control};
        return std::nullopt;
      };
      auto nativeResumeOwner = [](const ScheduledProcess &process,
                                  size_t control) {
        size_t owner = process.callers.size();
        for (size_t index = 0; index != process.callerControlDepths.size();
             ++index)
          if (control < process.callerControlDepths[index]) {
            owner = index;
            break;
          }
        return owner;
      };
      auto designResumeOwner = [](const ScheduledDesignTask &task,
                                  size_t control) {
        size_t owner = task.callers.size();
        for (size_t index = 0; index != task.callers.size(); ++index)
          if (control < task.callers[index].controlDepth) {
            owner = index;
            break;
          }
        return owner;
      };

      // The disabling process follows its statically lowered exit edge. Drop
      // the targeted activation and every dynamically nested activation from
      // its inherited stack instead of terminating its logical identity.
      size_t trim = context->activeControls.size();
      for (size_t index = 0; index != context->activeControls.size(); ++index)
        if (std::find(targets.begin(), targets.end(),
                      context->activeControls[index]) != targets.end()) {
          trim = index;
          break;
        }
      // Ordinary code follows a statically lowered exit edge after disabling
      // its own control. An observer callback has no such edge in the
      // suspended activation, so disabling one of the waiter's active
      // controls cancels that logical process instead.
      bool cancelCurrent =
          context->observerDepth != 0 && trim != context->activeControls.size();
      struct ActiveControlEscape {
        enum class Kind { Native, Design } kind;
        size_t owner;
        uint32_t continuation;
      };
      std::optional<ActiveControlEscape> activeEscape;
      std::optional<ResumeBoundary> activeResume =
          cancelCurrent ? std::nullopt
                        : resumeBoundary(current, context->activeControls);
      bool currentNonlocalExit =
          activeResume && activation == 0 && allActivations != 0;
      if (activeResume) {
        const ResumeBoundary &resume = *activeResume;
        if (context->activeDesignTask &&
            context->activeDesignTask->id == current) {
          size_t owner =
              designResumeOwner(*context->activeDesignTask, resume.control);
          if (owner < context->activeDesignTask->callers.size())
            activeEscape = ActiveControlEscape{
                ActiveControlEscape::Kind::Design, owner, resume.continuation};
        } else if (context->activeNativeProcess) {
          auto scheduled = std::find_if(
              context->scheduledProcesses.begin(),
              context->scheduledProcesses.end(),
              [&](const ScheduledProcess &process) {
                return process.instance == context->activeNativeProcess &&
                       ((UINT64_C(1) << 63) | process.token) == current;
              });
          if (scheduled != context->scheduledProcesses.end()) {
            if (scheduled->callerControlDepths.size() !=
                scheduled->callers.size())
              return OBELISK_RT_INVALID_LIFECYCLE;
            size_t owner = nativeResumeOwner(*scheduled, resume.control);
            if (owner < scheduled->callers.size())
              activeEscape =
                  ActiveControlEscape{ActiveControlEscape::Kind::Native, owner,
                                      resume.continuation};
          }
        }
      }
      size_t nativeActivationCount = 0;
      size_t designActivationCount = 0;
      size_t nativeTaskCount = 0;
      size_t designTaskCount = 0;
      if (activeEscape) {
        if (activeEscape->kind == ActiveControlEscape::Kind::Native) {
          auto scheduled = std::find_if(context->scheduledProcesses.begin(),
                                        context->scheduledProcesses.end(),
                                        [&](const ScheduledProcess &process) {
                                          return process.instance ==
                                                 context->activeNativeProcess;
                                        });
          if (scheduled == context->scheduledProcesses.end())
            return OBELISK_RT_INVALID_LIFECYCLE;
          nativeActivationCount =
              scheduled->callers.size() - activeEscape->owner - 1;
        } else {
          designActivationCount = context->activeDesignTask->callers.size() -
                                  activeEscape->owner - 1;
        }
      }
      for (const ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = (UINT64_C(1) << 63) | process.token;
        if (!process.instance || (token == current && !cancelCurrent) ||
            !isTargetMember(process.controls))
          continue;
        if (process.callerControlDepths.size() != process.callers.size())
          return OBELISK_RT_INVALID_LIFECYCLE;
        if (std::optional<ResumeBoundary> resume =
                resumeBoundary(token, process.controls)) {
          resumedControl = true;
          size_t owner = nativeResumeOwner(process, resume->control);
          size_t count = process.callers.size() - owner;
          if (count >
              std::numeric_limits<size_t>::max() - nativeActivationCount)
            obelisk_rt_out_of_memory();
          nativeActivationCount += count;
          continue;
        }
        std::optional<UnwindBoundary> unwind = nativeUnwind(process);
        if (!unwind &&
            process.callers.size() == std::numeric_limits<size_t>::max())
          obelisk_rt_out_of_memory();
        size_t count = unwind ? process.callers.size() - unwind->caller
                              : process.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - nativeActivationCount)
          obelisk_rt_out_of_memory();
        nativeActivationCount += count;
        if (!unwind)
          ++nativeTaskCount;
      }
      for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || (task.id == current && !cancelCurrent) ||
            !isTargetMember(task.controls))
          continue;
        if (std::optional<ResumeBoundary> resume =
                resumeBoundary(task.id, task.controls)) {
          resumedControl = true;
          size_t owner = designResumeOwner(task, resume->control);
          size_t count = task.callers.size() - owner;
          if (count >
              std::numeric_limits<size_t>::max() - designActivationCount)
            obelisk_rt_out_of_memory();
          designActivationCount += count;
          continue;
        }
        std::optional<UnwindBoundary> unwind = designUnwind(task);
        if (!unwind &&
            task.callers.size() == std::numeric_limits<size_t>::max())
          obelisk_rt_out_of_memory();
        size_t count = unwind ? task.callers.size() - unwind->caller
                              : task.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - designActivationCount)
          obelisk_rt_out_of_memory();
        designActivationCount += count;
        if (!unwind)
          ++designTaskCount;
      }
      nativeInstances.reserve(nativeActivationCount);
      designTasks.reserve(designActivationCount);
      insertedNativeTerminations.reserve(nativeTaskCount);
      insertedDesignTerminations.reserve(designTaskCount);
      insertedNativeKills.reserve(nativeTaskCount);
      insertedDesignKills.reserve(designTaskCount);
      context->terminatedNativeProcesses.reserveRanges(checkedSizeSum(
          context->terminatedNativeProcesses.rangeCount(), nativeTaskCount));
      context->terminatedDesignTasks.reserveRanges(checkedSizeSum(
          context->terminatedDesignTasks.rangeCount(), designTaskCount));
      context->terminatedNativeProcesses.reserveRandomStates(
          checkedSizeSum(context->terminatedNativeProcesses.randomStateCount(),
                         nativeTaskCount));
      context->terminatedDesignTasks.reserveRandomStates(checkedSizeSum(
          context->terminatedDesignTasks.randomStateCount(), designTaskCount));
      context->killedNativeProcesses.reserveRanges(checkedSizeSum(
          context->killedNativeProcesses.rangeCount(), nativeTaskCount));
      context->killedDesignTasks.reserveRanges(checkedSizeSum(
          context->killedDesignTasks.rangeCount(), designTaskCount));
      OBELISK_RT_TRY {
        for (const ScheduledProcess &process : context->scheduledProcesses) {
          uint64_t token = (UINT64_C(1) << 63) | process.token;
          if (!process.instance || (token == current && !cancelCurrent) ||
              !isTargetMember(process.controls) ||
              resumeBoundary(token, process.controls) || nativeUnwind(process))
            continue;
          if (context->terminatedNativeProcesses
                  .insert(process.token, process.random)
                  .second)
            insertedNativeTerminations.push_back(process.token);
          if (context->killedNativeProcesses.insert(process.token).second)
            insertedNativeKills.push_back(process.token);
        }
        for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
          if (task.terminated || (task.id == current && !cancelCurrent) ||
              !isTargetMember(task.controls) ||
              resumeBoundary(task.id, task.controls) || designUnwind(task))
            continue;
          if (context->terminatedDesignTasks.insert(task.id, task.random)
                  .second)
            insertedDesignTerminations.push_back(task.id);
          if (context->killedDesignTasks.insert(task.id).second)
            insertedDesignKills.push_back(task.id);
        }
      }
      OBELISK_RT_CATCH_ALL {
        for (uint64_t token : insertedNativeTerminations)
          context->terminatedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignTerminations)
          context->terminatedDesignTasks.erase(token);
        for (uint64_t token : insertedNativeKills)
          context->killedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignKills)
          context->killedDesignTasks.erase(token);
        OBELISK_RT_RETHROW;
      }

      // Clause 16.4 flushes reports when the outermost enclosing process
      // scope is disabled. A canceled observer has no static exit edge and
      // likewise cannot retain a pending report.
      if (cancelCurrent || trim == 0)
        obelisk_rt_flush_deferred_immediate_reports_unlocked(context, current);

      if (activeEscape) {
        context->controlEscapePending = true;
        if (activeEscape->kind == ActiveControlEscape::Kind::Native) {
          auto scheduled = std::find_if(context->scheduledProcesses.begin(),
                                        context->scheduledProcesses.end(),
                                        [&](const ScheduledProcess &process) {
                                          return process.instance ==
                                                 context->activeNativeProcess;
                                        });
          if (scheduled == context->scheduledProcesses.end())
            return OBELISK_RT_INVALID_LIFECYCLE;
          obelisk_rt_process_instance_v1 *owner =
              scheduled->callers[activeEscape->owner];
          if (owner->native_handle) {
            owner->descriptor->native_destroy(owner);
            if (owner->native_handle)
              return OBELISK_RT_INVALID_LIFECYCLE;
          }
          owner->continuation = activeEscape->continuation;
          for (size_t index = scheduled->callers.size();
               index != activeEscape->owner + 1; --index)
            nativeInstances.push_back(scheduled->callers[index - 1]);
          scheduled->callers.resize(activeEscape->owner + 1);
          scheduled->callerControlDepths.resize(activeEscape->owner + 1);
        } else {
          ScheduledDesignTask &task = *context->activeDesignTask;
          task.callers[activeEscape->owner].continuation =
              activeEscape->continuation;
          for (size_t index = task.callers.size();
               index != activeEscape->owner + 1; --index) {
            DesignActivation &activation = task.callers[index - 1];
            designTasks.push_back({task.id, activation.function,
                                   activation.scratchOffset, false,
                                   std::move(activation.frame)});
          }
          task.callers.resize(activeEscape->owner + 1);
        }
      }
      if (currentNonlocalExit)
        context->controlEscapePending = true;

      if (!cancelCurrent && trim != context->activeControls.size()) {
        for (size_t index = trim; index != context->activeControls.size();
             ++index)
          obelisk_rt_release_control_unlocked(context,
                                              context->activeControls[index]);
        context->activeControls.resize(trim);
      }
      for (ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = (UINT64_C(1) << 63) | process.token;
        if (!process.instance || (token == current && !cancelCurrent) ||
            !isTargetMember(process.controls))
          continue;
        if (std::optional<ResumeBoundary> resume =
                resumeBoundary(token, process.controls)) {
          if (resume->control == 0)
            obelisk_rt_flush_deferred_immediate_reports_unlocked(context,
                                                                 token);
          size_t owner = nativeResumeOwner(process, resume->control);
          obelisk_rt_process_instance_v1 *selected = process.instance;
          if (owner != process.callers.size()) {
            selected = process.callers[owner];
            nativeInstances.push_back(process.instance);
            for (size_t index = process.callers.size(); index != owner + 1;
                 --index)
              nativeInstances.push_back(process.callers[index - 1]);
            process.instance = selected;
            process.callers.resize(owner);
            process.callerControlDepths.resize(owner);
          }
          if (selected->native_handle) {
            selected->descriptor->native_destroy(selected);
            if (selected->native_handle)
              return OBELISK_RT_INVALID_LIFECYCLE;
          }
          selected->continuation = resume->continuation;
          if (process.aotActorSlot != UINT32_MAX) {
            uint32_t slot = process.aotActorSlot;
            if (!context->nativeSchedulePlan ||
                slot >= context->nativeScheduleActors.size())
              return OBELISK_RT_INVALID_LIFECYCLE;
            obelisk_rt_status status = context->nativeSchedulePlan->bind(
                context->nativeSchedulePlan->mutable_state, context, slot,
                selected);
            if (status != OBELISK_RT_OK)
              return status;
            context->nativeScheduleActors[slot] = selected;
            context->nativeScheduleActorTokens[slot] = process.token;
          }
          obelisk_rt_unregister_signal_wait_unlocked(
              context, process.signalSubscriptions, process.token, false);
          for (size_t index = resume->control; index != process.controls.size();
               ++index)
            obelisk_rt_release_control_unlocked(context,
                                                process.controls[index]);
          process.controls.resize(resume->control);
          process.suspendKind = OBELISK_RT_SUSPEND_NONE;
          process.waitOffset = 0;
          process.waitSize = 0;
          process.waitGenerations.clear();
          process.signalLatch.reset();
          process.signalTriggered = false;
          process.urgent = true;
          process.queuedRegion = process.homeRegion;
          context->nativePollCandidates.insert(process.token);
          continue;
        }
        obelisk_rt_flush_deferred_immediate_reports_unlocked(context, token);
        if (std::optional<UnwindBoundary> unwind = nativeUnwind(process)) {
          obelisk_rt_process_instance_v1 *caller =
              process.callers[unwind->caller];
          if (process.aotActorSlot != UINT32_MAX) {
            uint32_t slot = process.aotActorSlot;
            if (!context->nativeSchedulePlan ||
                slot >= context->nativeScheduleActors.size())
              return OBELISK_RT_INVALID_LIFECYCLE;
            obelisk_rt_status status = context->nativeSchedulePlan->bind(
                context->nativeSchedulePlan->mutable_state, context, slot,
                caller);
            if (status != OBELISK_RT_OK)
              return status;
            context->nativeScheduleActors[slot] = caller;
            context->nativeScheduleActorTokens[slot] = process.token;
          }
          nativeInstances.push_back(process.instance);
          for (size_t index = process.callers.size();
               index != unwind->caller + 1; --index)
            nativeInstances.push_back(process.callers[index - 1]);
          process.instance = caller;
          process.callers.resize(unwind->caller);
          process.callerControlDepths.resize(unwind->caller);
          obelisk_rt_unregister_signal_wait_unlocked(
              context, process.signalSubscriptions, process.token, false);
          for (size_t index = unwind->control; index != process.controls.size();
               ++index)
            obelisk_rt_release_control_unlocked(context,
                                                process.controls[index]);
          process.controls.resize(unwind->control);
          process.suspendKind = OBELISK_RT_SUSPEND_NONE;
          process.waitOffset = 0;
          process.waitSize = 0;
          process.waitGenerations.clear();
          process.signalLatch.reset();
          process.signalTriggered = false;
          process.urgent = true;
          process.queuedRegion = process.homeRegion;
          context->nativePollCandidates.insert(process.token);
          continue;
        }
        nativeInstances.push_back(process.instance);
        nativeInstances.insert(nativeInstances.end(), process.callers.begin(),
                               process.callers.end());
        process.callers.clear();
        process.callerControlDepths.clear();
        obelisk_rt_unregister_signal_wait_unlocked(
            context, process.signalSubscriptions, process.token, false);
        context->logicalProcessParentsWithChildren.erase(token);
        process.instance = nullptr;
        ++context->schedulerDeadProcessCount;
        context->schedulerCompactionPending = true;
        if (token == current) {
          obelisk_rt_release_controls_unlocked(context,
                                               context->activeControls);
          context->activeControls.clear();
        } else {
          obelisk_rt_release_controls_unlocked(context, process.controls);
        }
        process.controls.clear();
        process.signalTriggered = false;
      }
      for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || (task.id == current && !cancelCurrent) ||
            !isTargetMember(task.controls))
          continue;
        if (std::optional<ResumeBoundary> resume =
                resumeBoundary(task.id, task.controls)) {
          if (resume->control == 0)
            obelisk_rt_flush_deferred_immediate_reports_unlocked(context,
                                                                 task.id);
          size_t owner = designResumeOwner(task, resume->control);
          if (owner != task.callers.size()) {
            designTasks.push_back({task.id, task.function, task.scratchOffset,
                                   false, std::move(task.frame)});
            for (size_t index = task.callers.size(); index != owner + 1;
                 --index) {
              DesignActivation &activation = task.callers[index - 1];
              designTasks.push_back({task.id, activation.function,
                                     activation.scratchOffset, false,
                                     std::move(activation.frame)});
            }
            DesignActivation selected = std::move(task.callers[owner]);
            task.callers.resize(owner);
            task.function = selected.function;
            task.frame = std::move(selected.frame);
            task.scratchOffset = selected.scratchOffset;
            task.scratchSize = selected.scratchSize;
            task.scheduleRank = selected.scheduleRank;
          }
          task.continuation = resume->continuation;
          obelisk_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
          for (size_t index = resume->control; index != task.controls.size();
               ++index)
            obelisk_rt_release_control_unlocked(context, task.controls[index]);
          task.controls.resize(resume->control);
          task.suspendKind = OBELISK_RT_SUSPEND_NONE;
          task.waitOffset = 0;
          task.waitSize = 0;
          task.waitGenerations.clear();
          task.signalLatch.reset();
          task.signalTriggered = false;
          task.urgent = true;
          task.queuedRegion = task.homeRegion;
          context->designPollCandidates.insert(task.id);
          continue;
        }
        obelisk_rt_flush_deferred_immediate_reports_unlocked(context, task.id);
        if (std::optional<UnwindBoundary> unwind = designUnwind(task)) {
          designTasks.push_back({task.id, task.function, task.scratchOffset,
                                 false, std::move(task.frame)});
          for (size_t index = task.callers.size(); index != unwind->caller + 1;
               --index) {
            DesignActivation &activation = task.callers[index - 1];
            designTasks.push_back({task.id, activation.function,
                                   activation.scratchOffset, false,
                                   std::move(activation.frame)});
          }
          DesignActivation caller = std::move(task.callers[unwind->caller]);
          task.callers.resize(unwind->caller);
          task.function = caller.function;
          task.continuation = caller.continuation;
          task.frame = std::move(caller.frame);
          task.scratchOffset = caller.scratchOffset;
          task.scratchSize = caller.scratchSize;
          task.scheduleRank = caller.scheduleRank;
          obelisk_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
          for (size_t index = unwind->control; index != task.controls.size();
               ++index)
            obelisk_rt_release_control_unlocked(context, task.controls[index]);
          task.controls.resize(unwind->control);
          task.suspendKind = OBELISK_RT_SUSPEND_NONE;
          task.waitOffset = 0;
          task.waitSize = 0;
          task.waitGenerations.clear();
          task.signalLatch.reset();
          task.signalTriggered = false;
          task.urgent = true;
          task.queuedRegion = task.homeRegion;
          context->designPollCandidates.insert(task.id);
          continue;
        }
        designTasks.push_back({task.id, task.function, task.scratchOffset, true,
                               std::move(task.frame)});
        for (DesignActivation &caller : task.callers)
          designTasks.push_back({task.id, caller.function, caller.scratchOffset,
                                 true, std::move(caller.frame)});
        task.callers.clear();
        obelisk_rt_unregister_signal_wait_unlocked(
            context, task.signalSubscriptions, task.id, true);
        context->logicalProcessParentsWithChildren.erase(task.id);
        if (task.id == current) {
          obelisk_rt_release_controls_unlocked(context,
                                               context->activeControls);
          context->activeControls.clear();
        } else {
          obelisk_rt_release_controls_unlocked(context, task.controls);
        }
        task.controls.clear();
        task.terminated = true;
        ++context->schedulerDeadDesignTaskCount;
        context->schedulerCompactionPending = true;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
      }
      if (resumedControl || !nativeInstances.empty() || !designTasks.empty())
        if (++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
    }

    obelisk_rt_status result = OBELISK_RT_OK;
    if (!designTasks.empty()) {
      if (!context->execution)
        result = OBELISK_RT_INVALID_LIFECYCLE;
      else {
        obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
        Image image;
        if (!loadValidatedImage(entry, context, image))
          result = OBELISK_RT_INVALID_BYTECODE;
        else
          for (const CancelledDesignTask &task : designTasks) {
            obelisk_rt_status status = releaseCapturedAutomaticStates(
                image, task.function, context, task.frame.data(),
                task.scratchOffset);
            if (result == OBELISK_RT_OK && status != OBELISK_RT_OK)
              result = status;
          }
      }
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      uint64_t last = 0;
      for (const CancelledDesignTask &task : designTasks)
        if (task.releaseOwnedStates && task.id != last) {
          releaseDesignTaskOwnedStatesUnlocked(context, task.id);
          last = task.id;
        }
      for (CancelledDesignTask &task : designTasks)
        context->designTaskFrames.release(std::move(task.frame));
    }
    for (obelisk_rt_process_instance_v1 *instance : nativeInstances) {
      obelisk_rt_status status =
          obelisk_rt_v1_process_instance_destroy(instance);
      if (result == OBELISK_RT_OK && status != OBELISK_RT_OK)
        result = status;
    }
    return result;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

struct DesignTaskReadiness {
  bool runnable = false;
  bool signalTriggered = false;
};

// This is the canonical readiness predicate shared by scheduler selection and
// cold VPI inspection. Inspection suppresses diagnostics and error-state
// mutation, but evaluates the same wait sources as execution.
__attribute__((always_inline)) static inline obelisk_rt_status
inspectDesignTaskReadiness(obelisk_rt_context *context,
                           const ScheduledDesignTask &task,
                           uint32_t activePhase, uint32_t unstartedActorRegion,
                           bool recordSchedulerEffects,
                           DesignTaskReadiness &result) {
  result = {};
  if (task.phase != activePhase ||
      !schedulerRegionEligible(context, task.queuedRegion))
    return OBELISK_RT_OK;

  bool awaited = false;
  bool childrenDone = false;
  bool eventTriggered = false;
  bool eventOrderReady = task.waitOrderReady;
  bool mailboxReady = false;
  bool semaphoreReady = false;
  result.signalTriggered =
      task.signalTriggered || ((task.suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
                                task.suspendKind == OBELISK_RT_SUSPEND_EDGE) &&
                               task.signalLatch && task.signalLatch->triggered);
  if (recordSchedulerEffects && context->signalDiagnosticsEnabled &&
      task.started &&
      (task.suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
       task.suspendKind == OBELISK_RT_SUSPEND_EDGE ||
       task.suspendKind == OBELISK_RT_SUSPEND_OBSERVER))
    ++context->signalDiagnostics.readinessCalls;

  if (task.started &&
      (task.suspendKind == OBELISK_RT_SUSPEND_EVENT ||
       task.suspendKind == OBELISK_RT_SUSPEND_EVENT_ORDER ||
       task.suspendKind == OBELISK_RT_SUSPEND_MAILBOX ||
       task.suspendKind == OBELISK_RT_SUSPEND_SEMAPHORE ||
       task.suspendKind == OBELISK_RT_SUSPEND_AWAIT ||
       task.suspendKind == OBELISK_RT_SUSPEND_JOIN) &&
      task.waitSize >= sizeof(obelisk_rt_wait_record_v1) &&
      task.waitOffset <= task.scratchOffset &&
      task.waitSize <= task.scratchOffset - task.waitOffset) {
    const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
        task.frame.data() + task.waitOffset);
    const auto *entries = reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(
        reinterpret_cast<const uint8_t *>(wait) + sizeof(*wait));
    if (task.suspendKind == OBELISK_RT_SUSPEND_EVENT) {
      if (task.waitGenerations.size() == wait->count)
        for (uint32_t index = 0; index != wait->count; ++index) {
          auto event = context->events.find(entries[index].stable_id);
          uint64_t generation =
              event == context->events.end() ? 0 : event->second.generation;
          eventTriggered |= generation != task.waitGenerations[index];
        }
    } else if (task.suspendKind == OBELISK_RT_SUSPEND_EVENT_ORDER) {
      eventOrderReady = task.waitOrderReady;
    } else if (task.suspendKind == OBELISK_RT_SUSPEND_MAILBOX) {
      if (wait->count == 1) {
        obelisk_rt_status status = obelisk_rt_mailbox_wait_ready(
            obelisk_rt_object_from_managed_word(entries[0].stable_id),
            wait->flags, mailboxReady);
        if (status != OBELISK_RT_OK) {
          if (recordSchedulerEffects)
            context->schedulerStatus = status;
          return status;
        }
      }
    } else if (task.suspendKind == OBELISK_RT_SUSPEND_SEMAPHORE) {
      if (wait->count == 1 && wait->payload <= UINT32_MAX) {
        obelisk_rt_status status = obelisk_rt_semaphore_wait_ready(
            context, obelisk_rt_object_from_managed_word(entries[0].stable_id),
            static_cast<int32_t>(wait->payload), task.waitSequence,
            semaphoreReady);
        if (status != OBELISK_RT_OK) {
          if (recordSchedulerEffects)
            context->schedulerStatus = status;
          return status;
        }
      }
    } else if (task.suspendKind == OBELISK_RT_SUSPEND_AWAIT) {
      awaited = wait->count == 1 && obelisk_rt_logical_process_terminated(
                                        context, entries[0].stable_id);
    } else if (wait->count != 0) {
      awaited = wait->flags == 0;
      if (wait->flags == 0)
        for (uint32_t index = 0; index != wait->count; ++index)
          awaited &= obelisk_rt_logical_process_terminated(
              context, entries[index].stable_id);
      else
        for (uint32_t index = 0; index != wait->count; ++index)
          awaited |= obelisk_rt_logical_process_terminated(
              context, entries[index].stable_id);
    }
  }

  if (task.started && task.suspendKind == OBELISK_RT_SUSPEND_CHILDREN) {
    childrenDone = true;
    for (const ScheduledDesignTask &child : context->scheduledDesignTasks)
      childrenDone &= child.terminated || child.parent != task.id;
    for (const ScheduledProcess &child : context->scheduledProcesses)
      childrenDone &= !child.instance || child.parent != task.id;
  }

  result.runnable =
      !task.terminated && !task.explicitlySuspended &&
      (!task.started || awaited || eventTriggered || eventOrderReady ||
       mailboxReady || semaphoreReady || result.signalTriggered ||
       childrenDone || task.suspendKind == OBELISK_RT_SUSPEND_NONE ||
       (task.suspendKind == OBELISK_RT_SUSPEND_DELAY
            ? task.wakeTime <= context->schedulerTime
            : (task.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
               task.suspendKind != OBELISK_RT_SUSPEND_EDGE &&
               task.suspendKind != OBELISK_RT_SUSPEND_EVENT &&
               task.suspendKind != OBELISK_RT_SUSPEND_EVENT_ORDER &&
               task.suspendKind != OBELISK_RT_SUSPEND_MAILBOX &&
               task.suspendKind != OBELISK_RT_SUSPEND_SEMAPHORE &&
               task.suspendKind != OBELISK_RT_SUSPEND_AWAIT &&
               task.suspendKind != OBELISK_RT_SUSPEND_JOIN &&
               task.suspendKind != OBELISK_RT_SUSPEND_FOREVER &&
               task.suspendKind != OBELISK_RT_SUSPEND_CHILDREN &&
               task.suspendKind != OBELISK_RT_SUSPEND_OBSERVER &&
               task.observedEpoch != context->schedulerEpoch)));
  if (result.runnable && task.queuedRegion >= unstartedActorRegion &&
      result.signalTriggered && !task.urgent && !task.prioritySignal)
    result.runnable = false;
  return OBELISK_RT_OK;
}

bool obelisk_rt_design_task_pending_before_read_only_unlocked(
    obelisk_rt_context *context) {
  if (!context || context->designTaskExecuting)
    return false;
  uint32_t activePhase = context->schedulerRunningFinals ? 1u : 0u;
  uint32_t unstartedActorRegion =
      obelisk_rt_peek_unstarted_actor_region(context, activePhase);
  for (uint64_t candidateID : context->designPollCandidates) {
    if (context->nativeScheduleDesignTaskFilterActive &&
        candidateID != context->nativeScheduleForcedDesignTask)
      continue;
    auto indexed = context->scheduledDesignTaskIndices.find(candidateID);
    if (indexed == context->scheduledDesignTaskIndices.end() ||
        indexed->second >= context->scheduledDesignTasks.size())
      continue;
    const ScheduledDesignTask &task =
        context->scheduledDesignTasks[indexed->second];
    DesignTaskReadiness readiness;
    if (inspectDesignTaskReadiness(context, task, activePhase,
                                   unstartedActorRegion, false,
                                   readiness) != OBELISK_RT_OK ||
        !readiness.runnable)
      continue;
    if (task.urgent ||
        designTaskOrderingRegion(task, readiness.signalTriggered) <=
            OBELISK_RT_REGION_POSTPONED)
      return true;
  }
  return false;
}

template <bool EnableReadyCohort>
__attribute__((always_inline)) inline obelisk_rt_status
runOneDesignTaskImpl(obelisk_rt_context *context, uint32_t maximumRegion,
                     uint32_t maximumRank, uint64_t maximumInsertionSequence,
                     bool *outProgress) noexcept {
  if (!context || !outProgress)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outProgress = false;
  ScheduledDesignTask task;
  bool taskDequeued = false;
  bool currentFrameReleased = false;
  auto abandonTask = [&](obelisk_rt_status failure) noexcept {
    OBELISK_RT_TRY {
      if (taskDequeued && context->execution) {
        obelisk_rt_design_bytecode_entry_v1 entry{context->execution,
                                                  task.function, 0};
        Image image;
        if (loadValidatedImage(entry, context, image)) {
          if (!currentFrameReleased && !task.frame.empty() &&
              task.function < image.functionCount &&
              task.scratchOffset <= task.frame.size())
            (void)releaseCapturedAutomaticStates(image, task.function, context,
                                                 task.frame.data(),
                                                 task.scratchOffset);
          for (DesignActivation &activation : task.callers)
            if (!activation.frame.empty() &&
                activation.function < image.functionCount &&
                activation.scratchOffset <= activation.frame.size())
              (void)releaseCapturedAutomaticStates(
                  image, activation.function, context, activation.frame.data(),
                  activation.scratchOffset);
        }
      }
    }
    OBELISK_RT_CATCH_ALL {}
    OBELISK_RT_TRY {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      obelisk_rt_unregister_signal_wait_unlocked(
          context, task.signalSubscriptions, task.id, true);
      if (taskDequeued) {
        task.controls = std::move(context->activeControls);
        obelisk_rt_release_controls_unlocked(context, task.controls);
        releaseDesignTaskOwnedStatesUnlocked(context, task.id);
      }
      context->activeDesignTaskID = 0;
      context->activeDesignTask = nullptr;
      context->activeRandom = nullptr;
      context->activeDesignTaskPhase = 0;
      context->activeHomeRegion = UINT32_MAX;
      context->activeExecRegion = UINT32_MAX;
      context->activeLogicalProcessToken = 0;
      context->activeProgramOwner = 0;
      context->controlEscapePending = false;
      context->activeLogicalProcessParent = 0;
      context->activeWaitOrderFailed = false;
      context->designTaskExecuting = false;
    }
    OBELISK_RT_CATCH_ALL {}
    if (taskDequeued) {
      context->designTaskFrames.release(std::move(task.frame));
      for (DesignActivation &activation : task.callers)
        context->designTaskFrames.release(std::move(activation.frame));
    }
    return failure;
  };
  OBELISK_RT_TRY {
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->designTaskExecuting)
        return OBELISK_RT_OK;
      if (context->scheduledDesignTaskIndices.size() !=
          context->scheduledDesignTasks.size())
        rebuildDesignSchedulerIndexUnlocked(context);
      auto found = context->scheduledDesignTasks.end();
      bool foundUrgent = false;
      uint64_t foundUrgentSequence = UINT64_MAX;
      uint32_t selectedRegion = UINT32_MAX;
      uint32_t selectedRank = UINT32_MAX;
      uint64_t selectedInsertionSequence = UINT64_MAX;
      uint32_t activePhase = context->schedulerRunningFinals ? 1u : 0u;
      uint32_t unstartedActorRegion =
          obelisk_rt_unstarted_actor_region(context, activePhase);
      constexpr size_t minCachedDesignSignalCohort = 16;
      bool selectedFromReadyCohort = false;
      DesignReadyCohortState *readyCohort =
          EnableReadyCohort ? context->designReadyCohort.get() : nullptr;
      bool readyCohortFeatureActive =
          EnableReadyCohort &&
          (readyCohort ||
           context->designPollCandidates.size() > minCachedDesignSignalCohort);
      bool readyCohortSuppressed =
          readyCohortFeatureActive && designReadyCohortSuppressed(context);
      if (readyCohortFeatureActive && readyCohort && !readyCohortSuppressed) {
        size_t candidateIndex = SIZE_MAX;
        DesignReadyCohortEntry cached;
        if (trySelectCachedDesignReadyCohort(context, activePhase,
                                             unstartedActorRegion,
                                             candidateIndex, cached)) {
          found = context->scheduledDesignTasks.begin() + candidateIndex;
          selectedRegion = cached.region;
          selectedRank = cached.rank;
          selectedInsertionSequence = cached.insertionSequence;
          selectedFromReadyCohort = true;
        }
      }
      if (readyCohort && readyCohort->valid && !selectedFromReadyCohort)
        readyCohort->valid = false;

      bool collectReadyCohort =
          readyCohortFeatureActive && !selectedFromReadyCohort &&
          !readyCohortSuppressed &&
          !context->nativeScheduleDesignTaskFilterActive &&
          context->designPollCandidates.size() > minCachedDesignSignalCohort;
      bool readyCohortEligible = collectReadyCohort;
      struct DesignReadyCohortBuild {
        std::vector<DesignReadyCohortEntry> ready;
        std::vector<uint64_t> slow;
      };
      std::optional<DesignReadyCohortBuild> readyCohortBuild;
      if (collectReadyCohort) {
        readyCohortBuild.emplace();
        readyCohortBuild->ready.reserve(context->designPollCandidates.size());
      }
      bool directExactScan = !selectedFromReadyCohort && !collectReadyCohort;
      auto scanExactDesignCandidates = [&]() __attribute__((always_inline))
                                           ->obelisk_rt_status {
        // Keep the ordinary and negative-admission path in the original scan
        // body. This is the dominant generic-scheduler path and must not pay
        // an outlined feature-scanner call or its altered register layout.
        for (uint64_t candidateID : context->designPollCandidates) {
          if (context->nativeScheduleDesignTaskFilterActive &&
              candidateID != context->nativeScheduleForcedDesignTask)
            continue;
          auto indexed = context->scheduledDesignTaskIndices.find(candidateID);
          if (indexed == context->scheduledDesignTaskIndices.end() ||
              indexed->second >= context->scheduledDesignTasks.size())
            continue;
          size_t candidateIndex = indexed->second;
          auto iterator =
              context->scheduledDesignTasks.begin() + candidateIndex;
          if (context->signalDiagnosticsEnabled)
            ++context->signalDiagnostics.candidateScans;
          DesignTaskReadiness readiness;
          obelisk_rt_status readinessStatus = inspectDesignTaskReadiness(
              context, *iterator, context->schedulerRunningFinals ? 1u : 0u,
              unstartedActorRegion, true, readiness);
          if (readinessStatus != OBELISK_RT_OK)
            return readinessStatus;
          bool runnable = readiness.runnable;
          bool signalTriggered = readiness.signalTriggered;
          uint32_t orderingRegion =
              designTaskOrderingRegion(*iterator, signalTriggered);
          auto key = iterator->prioritySignal && signalTriggered
                         ? std::tuple{orderingRegion, uint32_t{0}, uint64_t{0}}
                         : std::tuple{orderingRegion, iterator->scheduleRank,
                                      iterator->insertionSequence};
          auto selectedKey = std::tuple{selectedRegion, selectedRank,
                                        selectedInsertionSequence};
          if (runnable && iterator->urgent) {
            if (!foundUrgent ||
                iterator->insertionSequence < foundUrgentSequence) {
              found = iterator;
              foundUrgent = true;
              foundUrgentSequence = iterator->insertionSequence;
              selectedRegion = 0;
              selectedRank = 0;
              selectedInsertionSequence = 0;
            }
            continue;
          }
          if (foundUrgent)
            continue;
          if (runnable && key < selectedKey) {
            found = iterator;
            selectedRegion = std::get<0>(key);
            selectedRank = std::get<1>(key);
            selectedInsertionSequence = std::get<2>(key);
          }
        }
        return OBELISK_RT_OK;
      };
      if (directExactScan) {
        obelisk_rt_status status = scanExactDesignCandidates();
        if (status != OBELISK_RT_OK)
          return status;
      }
      bool cachedSlowMembershipValid = true;
      auto scanDesignCandidates = [&](const auto &candidates,
                                      auto scanMode) -> obelisk_rt_status {
        constexpr unsigned mode = decltype(scanMode)::value;
        constexpr bool collect = mode == 1;
        constexpr bool validateCachedSlow = mode == 2;
        for (uint64_t candidateID : candidates) {
          if constexpr (validateCachedSlow) {
            if (context->signalDiagnosticsEnabled)
              ++context->signalDiagnostics.candidateScans;
            if (!context->designPollCandidates.count(candidateID)) {
              cachedSlowMembershipValid = false;
              continue;
            }
          }
          if (context->nativeScheduleDesignTaskFilterActive &&
              candidateID != context->nativeScheduleForcedDesignTask)
            continue;
          auto indexed = context->scheduledDesignTaskIndices.find(candidateID);
          if (indexed == context->scheduledDesignTaskIndices.end() ||
              indexed->second >= context->scheduledDesignTasks.size()) {
            if constexpr (collect)
              readyCohortEligible = false;
            if constexpr (validateCachedSlow)
              cachedSlowMembershipValid = false;
            continue;
          }
          size_t candidateIndex = indexed->second;
          auto iterator =
              context->scheduledDesignTasks.begin() + candidateIndex;
          if (context->signalDiagnosticsEnabled && !validateCachedSlow)
            ++context->signalDiagnostics.candidateScans;
          if (iterator->phase != (context->schedulerRunningFinals ? 1u : 0u)) {
            if constexpr (collect)
              readyCohortBuild->slow.push_back(candidateID);
            continue;
          }
          DesignTaskReadiness readiness;
          obelisk_rt_status readinessStatus =
              inspectDesignTaskReadiness(context, *iterator, activePhase,
                                         unstartedActorRegion, true, readiness);
          if (readinessStatus != OBELISK_RT_OK)
            return readinessStatus;
          bool runnable = readiness.runnable;
          bool signalTriggered = readiness.signalTriggered;
          if constexpr (collect) {
            DesignReadyCohortEntry cached;
            if (runnable &&
                classifyDirectDesignReadyCohortMember(
                    *iterator, activePhase, unstartedActorRegion, cached))
              readyCohortBuild->ready.push_back(cached);
            else
              readyCohortBuild->slow.push_back(candidateID);
          }
          uint32_t orderingRegion =
              designTaskOrderingRegion(*iterator, signalTriggered);
          auto key = iterator->prioritySignal && signalTriggered
                         ? std::tuple{orderingRegion, uint32_t{0}, uint64_t{0}}
                         : std::tuple{orderingRegion, iterator->scheduleRank,
                                      iterator->insertionSequence};
          auto selectedKey = std::tuple{selectedRegion, selectedRank,
                                        selectedInsertionSequence};
          if (runnable && iterator->urgent) {
            if (!foundUrgent ||
                iterator->insertionSequence < foundUrgentSequence) {
              found = iterator;
              foundUrgent = true;
              foundUrgentSequence = iterator->insertionSequence;
              selectedRegion = 0;
              selectedRank = 0;
              selectedInsertionSequence = 0;
            }
            continue;
          }
          if (foundUrgent)
            continue;
          if (runnable && key < selectedKey) {
            found = iterator;
            selectedRegion = std::get<0>(key);
            selectedRank = std::get<1>(key);
            selectedInsertionSequence = std::get<2>(key);
          }
        }
        return OBELISK_RT_OK;
      };
      obelisk_rt_status scanStatus = OBELISK_RT_OK;
      if (selectedFromReadyCohort)
        scanStatus = scanDesignCandidates(
            readyCohort->slowCandidates, std::integral_constant<unsigned, 2>{});
      else if (collectReadyCohort)
        scanStatus =
            scanDesignCandidates(context->designPollCandidates,
                                 std::integral_constant<unsigned, 1>{});
      if (scanStatus != OBELISK_RT_OK)
        return scanStatus;
      if (selectedFromReadyCohort && !cachedSlowMembershipValid) {
        readyCohort->valid = false;
        selectedFromReadyCohort = false;
        found = context->scheduledDesignTasks.end();
        foundUrgent = false;
        foundUrgentSequence = UINT64_MAX;
        selectedRegion = UINT32_MAX;
        selectedRank = UINT32_MAX;
        selectedInsertionSequence = UINT64_MAX;
        scanStatus = scanExactDesignCandidates();
        if (scanStatus != OBELISK_RT_OK)
          return scanStatus;
      }
      if (readyCohortEligible) {
        size_t readyCount = readyCohortBuild->ready.size();
        size_t slowCount = readyCohortBuild->slow.size();
        bool completeShape =
            readyCount + slowCount == context->designPollCandidates.size();
        // Do not let cached slow work exceed the leading R^2/2 cost of exact
        // ready rescans: R >= 2*S implies R*S <= R^2/2. Smaller or
        // slow-dominant shapes stay on the exact scan for this generation.
        bool profitable = readyCount > minCachedDesignSignalCohort &&
                          slowCount <= readyCount / 2;
        if (completeShape && profitable) {
          installDesignReadyCohort(context, std::move(readyCohortBuild->ready),
                                   std::move(readyCohortBuild->slow));
          readyCohort = context->designReadyCohort.get();
        } else if (completeShape) {
          // Any observed ready work proves this is not the zero-ready startup
          // shape. Persist its negative profitability result; a recurring
          // N=17 wave reaches this probe after one task has run and therefore
          // has only 16 remaining ready tasks. Startup/unstarted or zero-ready
          // shapes must still be reconsidered on the next signal generation.
          suppressDesignReadyCohort(context, readyCount != 0);
          readyCohort = context->designReadyCohort.get();
        }
      }
      auto maximumKey =
          std::tuple{maximumRegion, maximumRank, maximumInsertionSequence};
      if (found == context->scheduledDesignTasks.end() ||
          (!foundUrgent &&
           !(std::tuple{selectedRegion, selectedRank,
                        selectedInsertionSequence} < maximumKey)))
        return OBELISK_RT_OK;
      if (found->suspendKind == OBELISK_RT_SUSPEND_SEMAPHORE) {
        const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
            found->frame.data() + found->waitOffset);
        bool acquired = false;
        obelisk_rt_status status =
            obelisk_rt_semaphore_wait_acquire(wait, acquired);
        if (status != OBELISK_RT_OK) {
          context->schedulerStatus = status;
          return status;
        }
        if (!acquired)
          return OBELISK_RT_OK;
      }
      size_t selectedIndex =
          static_cast<size_t>(found - context->scheduledDesignTasks.begin());
      if (readyCohort && readyCohort->valid) {
        if (!readyCohort->ready.empty() &&
            readyCohort->ready.back().id == found->id)
          readyCohort->ready.pop_back();
        else
          readyCohort->valid = false;
      }
      task = std::move(context->scheduledDesignTasks[selectedIndex]);
      if (!context->schedulerRunningFinals &&
          isReactiveSchedulerRegion(task.queuedRegion))
        setSchedulerDrainingReactive(context, true);
      bool resuming =
          task.started && task.suspendKind != OBELISK_RT_SUSPEND_NONE;
      if (!task.started)
        obelisk_rt_unregister_unstarted_actor(context, task.phase, task.id);
      // Keep a direct wait indexed while its task executes. The task may
      // change its own watched signal and then suspend on the same wait; the
      // transition is the next occurrence, not part of the one being
      // consumed now.
      if (task.signalLatch) {
        task.signalLatch->triggered = false;
        task.signalLatch->affected = false;
      }
      task.signalTriggered = false;
      context->designPollCandidates.erase(task.id);
      context->scheduledDesignTaskIndices.erase(task.id);
      size_t lastIndex = context->scheduledDesignTasks.size() - 1;
      if (selectedIndex != lastIndex) {
        context->scheduledDesignTasks[selectedIndex] =
            std::move(context->scheduledDesignTasks[lastIndex]);
        context->scheduledDesignTaskIndices
            [context->scheduledDesignTasks[selectedIndex].id] = selectedIndex;
      }
      context->scheduledDesignTasks.pop_back();
      taskDequeued = true;
      context->designTaskExecuting = true;
      context->activeDesignTaskID = task.id;
      context->activeDesignTask = &task;
      context->activeRandom = &task.random;
      context->activeDesignTaskPhase = task.phase;
      context->activeHomeRegion = task.homeRegion;
      context->activeExecRegion = task.queuedRegion;
      context->activeLogicalProcessToken = task.id;
      context->activeProgramOwner = task.programOwner;
      context->controlEscapePending = false;
      context->activeLogicalProcessParent = task.parent;
      context->activeWaitOrderFailed =
          resuming && task.suspendKind == OBELISK_RT_SUSPEND_EVENT_ORDER &&
          task.waitOrderReady && task.waitOrderFailed;
      if (resuming)
        obelisk_rt_flush_deferred_immediate_reports_unlocked(context, task.id);
      context->activeControls = std::move(task.controls);
    }
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, task.function,
                                              0};
    obelisk_rt_fragment_action_v1 action{};
    std::unique_ptr<PendingDesignActivation> pendingActivation;
    obelisk_rt_status status = executeDesignBytecode(
        entry, context, task.frame.data(), task.frame.size(),
        task.scratchOffset, task.scratchSize, task.continuation, 0, &action,
        &pendingActivation);
    currentFrameReleased =
        status == OBELISK_RT_OK && action.kind == OBELISK_RT_FRAGMENT_TERMINATE;
    bool terminationRequested =
        obelisk_rt_v1_scheduler_termination_requested(context) != 0;
    bool killRequested = false;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      killRequested |= context->killedDesignTasks.count(task.id) != 0;
    }
    if (status != OBELISK_RT_OK) [[unlikely]] {
      if (status == OBELISK_RT_DPI_DISABLE_UNSUPPORTED) {
        if (context->activeDpiExportTaskLogical == task.id)
          context->activeDpiExportTaskDisabled = true;
        killRequested = true;
      } else if (!terminationRequested) {
        return abandonTask(status);
      }
    }
    if (terminationRequested || killRequested) {
      Image image;
      if (!loadValidatedImage(entry, context, image))
        return abandonTask(OBELISK_RT_INVALID_BYTECODE);
      if (!currentFrameReleased) {
        obelisk_rt_status releaseStatus = releaseCapturedAutomaticStates(
            image, task.function, context, task.frame.data(),
            task.scratchOffset);
        currentFrameReleased = true;
        if (releaseStatus != OBELISK_RT_OK)
          return abandonTask(releaseStatus);
      }
      while (!task.callers.empty()) {
        DesignActivation &activation = task.callers.back();
        obelisk_rt_status releaseStatus = releaseCapturedAutomaticStates(
            image, activation.function, context, activation.frame.data(),
            activation.scratchOffset);
        task.callers.pop_back();
        if (releaseStatus != OBELISK_RT_OK)
          return abandonTask(releaseStatus);
      }
      action = {
          OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
      status = OBELISK_RT_OK;
    }
    std::optional<uint32_t> nextScheduleRank;
    if (action.kind != OBELISK_RT_FRAGMENT_TERMINATE) {
      Image image;
      if (!loadValidatedImage(entry, context, image))
        return abandonTask(OBELISK_RT_INVALID_BYTECODE);
      nextScheduleRank = continuationScheduleRank(
          image, functionAt(image, task.function), action.continuation);
      if (!nextScheduleRank)
        return abandonTask(OBELISK_RT_INVALID_CONTINUATION);
    }
    obelisk_rt_status finalizeStatus = OBELISK_RT_OK;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      task.controls = std::move(context->activeControls);
      task.started = true;
      task.continuation = action.continuation;
      // A direct task activation is the same logical process. Preserve its
      // stable scheduler rank across the call stack; only a suspension in the
      // root activation advances to a graph continuation rank.
      if (nextScheduleRank && action.kind != OBELISK_RT_FRAGMENT_TASK_CALL &&
          task.callers.empty())
        task.scheduleRank = *nextScheduleRank;
      task.observedEpoch = context->schedulerEpoch;
      switch (action.kind) {
      case OBELISK_RT_FRAGMENT_CONTINUE:
        if (!task.signalSubscriptions.empty() ||
            task.computedObserverWaitRegistered)
          obelisk_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        task.suspendKind = OBELISK_RT_SUSPEND_NONE;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
        task.urgent = task.startupProcess;
        task.queuedRegion = task.homeRegion;
        break;
      case OBELISK_RT_FRAGMENT_SUSPEND: {
        if (action.suspend_kind == OBELISK_RT_SUSPEND_OBSERVER) {
          constexpr uint32_t resumeFlags =
              OBELISK_RT_ACTION_RESUME_REGION_VALID |
              OBELISK_RT_ACTION_RESUME_REGION_MASK;
          if ((action.flags & ~resumeFlags) != 0 ||
              action.payload % alignof(obelisk_rt_computed_wait_record_v1) !=
                  0 ||
              action.payload > task.scratchOffset ||
              sizeof(obelisk_rt_computed_wait_record_v1) >
                  task.scratchOffset - action.payload) {
            finalizeStatus = OBELISK_RT_INVALID_FRAME;
            break;
          }
          auto *computed =
              reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
                  task.frame.data() + action.payload);
          if (!obelisk_rt_validate_computed_wait_record(
                  context->execution, computed,
                  task.scratchOffset - action.payload)) {
            finalizeStatus = OBELISK_RT_INVALID_FRAME;
            break;
          }
          task.suspendKind = action.suspend_kind;
          task.waitOffset = action.payload;
          task.waitSize = computed->total_size;
          task.waitGenerations.clear();
          task.signalTriggered = false;
          task.startupProcess = false;
          task.urgent = false;
          if (!obelisk_rt_next_queued_region(task.homeRegion,
                                             action.suspend_kind, 1,
                                             action.flags, task.queuedRegion)) {
            finalizeStatus = OBELISK_RT_INVALID_BYTECODE;
            break;
          }
          if (!task.signalSubscriptions.empty() ||
              task.computedObserverWaitRegistered)
            obelisk_rt_unregister_signal_wait_unlocked(
                context, task.signalSubscriptions, task.id, true);
          if (!obelisk_rt_register_computed_signal_wait_unlocked(
                  context, computed, task.id, true, task.signalSubscriptions,
                  task.signalLatch))
            finalizeStatus = context->schedulerStatus;
          break;
        }
        if (action.payload > task.scratchOffset ||
            sizeof(obelisk_rt_wait_record_v1) >
                task.scratchOffset - action.payload) {
          finalizeStatus = OBELISK_RT_INVALID_FRAME;
          break;
        }
        const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
            task.frame.data() + action.payload);
        uint64_t entries =
            uint64_t{wait->count} * sizeof(obelisk_rt_wait_entry_v1);
        bool signalWait = action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE ||
                          action.suspend_kind == OBELISK_RT_SUSPEND_EDGE;
        if (wait->version != OBELISK_RT_VERSION ||
            wait->kind != action.suspend_kind ||
            entries > task.scratchOffset - action.payload -
                          sizeof(obelisk_rt_wait_record_v1)) {
          finalizeStatus = OBELISK_RT_INVALID_FRAME;
          break;
        }
        const auto *waitEntries =
            reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(wait + 1);
        uint32_t behaviorFlags =
            wait->flags & ~(OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF |
                            OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL |
                            OBELISK_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS);
        bool suppressActiveSelf =
            (wait->flags & OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF) != 0;
        bool slotFinal =
            (wait->flags & OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL) != 0;
        bool mailboxWait = action.suspend_kind == OBELISK_RT_SUSPEND_MAILBOX;
        bool semaphoreWait =
            action.suspend_kind == OBELISK_RT_SUSPEND_SEMAPHORE;
        bool eventOrderWait =
            action.suspend_kind == OBELISK_RT_SUSPEND_EVENT_ORDER;
        bool validFlags =
            mailboxWait
                ? wait->flags <= OBELISK_RT_WAIT_MAILBOX_NOT_FULL
                : (wait->flags &
                   ~(OBELISK_RT_WAIT_LEVEL_TRUE | OBELISK_RT_WAIT_EDGE_IFF |
                     OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF |
                     OBELISK_RT_WAIT_CLOCK_OCCURRENCE |
                     OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL |
                     OBELISK_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS)) == 0 &&
                      (!suppressActiveSelf ||
                       (signalWait &&
                        (behaviorFlags == 0 ||
                         action.suspend_kind == OBELISK_RT_SUSPEND_EDGE))) &&
                      (behaviorFlags == OBELISK_RT_WAIT_FLAGS_NONE ||
                       (action.suspend_kind == OBELISK_RT_SUSPEND_JOIN &&
                        behaviorFlags <= 1) ||
                       (action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE &&
                        behaviorFlags == OBELISK_RT_WAIT_LEVEL_TRUE) ||
                       (action.suspend_kind == OBELISK_RT_SUSPEND_EDGE &&
                        (behaviorFlags == OBELISK_RT_WAIT_EDGE_IFF ||
                         behaviorFlags == OBELISK_RT_WAIT_CLOCK_OCCURRENCE)));
        uint32_t occurrenceConditions =
            behaviorFlags == OBELISK_RT_WAIT_CLOCK_OCCURRENCE
                ? static_cast<uint32_t>(__builtin_popcountll(wait->auxiliary))
                : 0;
        uint32_t occurrencePrimaries = occurrenceConditions < wait->count
                                           ? wait->count - occurrenceConditions
                                           : 0;
        bool validOccurrence =
            behaviorFlags != OBELISK_RT_WAIT_CLOCK_OCCURRENCE ||
            (obelisk_rt_is_clock_occurrence_wait_flags(wait->flags) &&
             wait->payload != 0 && occurrencePrimaries >= 1 &&
             occurrencePrimaries <= 64 &&
             (occurrencePrimaries == 64 ||
              (wait->auxiliary >> occurrencePrimaries) == 0));
        if (!validFlags || !validOccurrence ||
            (slotFinal && behaviorFlags != OBELISK_RT_WAIT_CLOCK_OCCURRENCE) ||
            (action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE &&
             behaviorFlags == OBELISK_RT_WAIT_LEVEL_TRUE && wait->count != 1) ||
            (action.suspend_kind == OBELISK_RT_SUSPEND_EDGE &&
             behaviorFlags == OBELISK_RT_WAIT_EDGE_IFF && wait->count != 2) ||
            (action.suspend_kind == OBELISK_RT_SUSPEND_FOREVER &&
             wait->count != 0) ||
            (eventOrderWait && (wait->count == 0 || wait->payload != 0 ||
                                wait->auxiliary != 0)) ||
            (mailboxWait && wait->count != 1) ||
            (semaphoreWait && (wait->flags != 0 || wait->count != 1 ||
                               wait->payload > UINT32_MAX ||
                               static_cast<int32_t>(wait->payload) < 0))) {
          finalizeStatus = OBELISK_RT_INVALID_FRAME;
          break;
        }
        uint64_t waitSize = sizeof(obelisk_rt_wait_record_v1) + entries;
        bool hasObserverCondition = false;
        for (uint32_t index = 0; index != wait->count; ++index) {
          bool validEdge =
              waitEntries[index].edge >= OBELISK_RT_WAIT_EDGE_CHANGE &&
              waitEntries[index].edge <= OBELISK_RT_WAIT_EDGE_BOTH;
          bool iffCondition =
              behaviorFlags == OBELISK_RT_WAIT_EDGE_IFF && index == 1;
          if (behaviorFlags == OBELISK_RT_WAIT_CLOCK_OCCURRENCE)
            iffCondition = index >= occurrencePrimaries;
          bool validClockPrimary =
              validEdge || ((waitEntries[index].edge &
                             ~OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) ==
                                OBELISK_RT_WAIT_EDGE_TRANSITION_MASK &&
                            (waitEntries[index].edge &
                             OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) != 0);
          bool validClockCondition =
              waitEntries[index].edge == OBELISK_RT_WAIT_EDGE_NONE ||
              (waitEntries[index].edge >= OBELISK_RT_WAIT_CONDITION_KNOWN_ONE &&
               waitEntries[index].edge <=
                   OBELISK_RT_WAIT_CONDITION_CASE_NE_ONE) ||
              (waitEntries[index].edge >= OBELISK_RT_WAIT_CONDITION_OBSERVER &&
               waitEntries[index].edge <=
                   OBELISK_RT_WAIT_CONDITION_OBSERVER_LAST);
          bool observerCondition =
              iffCondition &&
              waitEntries[index].edge >= OBELISK_RT_WAIT_CONDITION_OBSERVER &&
              waitEntries[index].edge <=
                  OBELISK_RT_WAIT_CONDITION_OBSERVER_LAST;
          if (observerCondition) {
            const obelisk_rt_observer_descriptor_v1 *descriptor =
                obelisk::process::findObserverDescriptor(
                    context->execution, waitEntries[index].stable_id);
            uint64_t captureBytes = uint64_t{waitEntries[index].reserved} *
                                    sizeof(obelisk_rt_computed_capture_v1);
            if (!descriptor || descriptor->result_width != 1 ||
                descriptor->capture_count != waitEntries[index].reserved ||
                captureBytes > task.scratchOffset - action.payload - waitSize) {
              finalizeStatus = OBELISK_RT_INVALID_FRAME;
              break;
            }
            waitSize += captureBytes;
            hasObserverCondition = true;
          }
          bool managed =
              signalWait && !iffCondition &&
              waitEntries[index].reserved == OBELISK_RT_WAIT_WIDTH_MANAGED;
          obelisk_rt_stable_handle_v1 decodedSignal;
          bool validSignalHandle =
              !signalWait ||
              (observerCondition || managed
                   ? true
                   : waitEntries[index].stable_id == UINT64_MAX ||
                         obelisk_rt_stable_handle_decode(
                             waitEntries[index].stable_id, &decodedSignal));
          if (signalWait
                  ? (!validSignalHandle ||
                     (managed &&
                      waitEntries[index].edge != OBELISK_RT_WAIT_EDGE_CHANGE) ||
                     (!(behaviorFlags == OBELISK_RT_WAIT_CLOCK_OCCURRENCE
                            ? iffCondition ? validClockCondition
                                           : validClockPrimary
                            : validEdge || iffCondition)) ||
                     (behaviorFlags != OBELISK_RT_WAIT_CLOCK_OCCURRENCE &&
                      iffCondition &&
                      waitEntries[index].edge != OBELISK_RT_WAIT_EDGE_NONE) ||
                     (!managed && !observerCondition &&
                      waitEntries[index].reserved == 0))
                  : (waitEntries[index].edge != OBELISK_RT_WAIT_EDGE_NONE ||
                     waitEntries[index].reserved != 0)) {
            finalizeStatus = OBELISK_RT_INVALID_FRAME;
            break;
          }
        }
        if (finalizeStatus == OBELISK_RT_OK &&
            (((wait->flags & OBELISK_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS) !=
              0) != hasObserverCondition))
          finalizeStatus = OBELISK_RT_INVALID_FRAME;
        if (finalizeStatus != OBELISK_RT_OK)
          break;
        bool sameSignalWait =
            signalWait && (behaviorFlags == OBELISK_RT_WAIT_CLOCK_OCCURRENCE
                               ? obelisk_rt_same_clock_occurrence_wait_unlocked(
                                     context, wait, task.id, true)
                               : hasSameDirectSignalWait(task, wait));
        if (!signalWait && (!task.signalSubscriptions.empty() ||
                            task.computedObserverWaitRegistered))
          obelisk_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        task.suspendKind = action.suspend_kind;
        task.waitOffset = action.payload;
        task.waitSize = waitSize;
        if (action.suspend_kind == OBELISK_RT_SUSPEND_SEMAPHORE) {
          if (context->nextWaitSequence == 0) {
            finalizeStatus = OBELISK_RT_OUT_OF_RESOURCES;
            break;
          }
          task.waitSequence = context->nextWaitSequence++;
        } else {
          task.waitSequence = 0;
        }
        task.waitGenerations.clear();
        task.waitOrderIndex = 0;
        task.waitOrderReady = false;
        task.waitOrderFailed = false;
        task.signalTriggered = false;
        task.startupProcess = false;
        task.urgent = false;
        if (!obelisk_rt_next_queued_region(task.homeRegion, action.suspend_kind,
                                           wait->payload, action.flags,
                                           task.queuedRegion)) {
          finalizeStatus = OBELISK_RT_INVALID_BYTECODE;
          break;
        }
        if (action.suspend_kind == OBELISK_RT_SUSPEND_EVENT) {
          task.waitGenerations.reserve(wait->count);
          for (uint32_t index = 0; index != wait->count; ++index) {
            auto event = context->events.find(waitEntries[index].stable_id);
            task.waitGenerations.push_back(
                event == context->events.end() ? 0 : event->second.generation);
          }
        }
        if (action.suspend_kind == OBELISK_RT_SUSPEND_EVENT_ORDER &&
            !obelisk_rt_initialize_event_order_wait_unlocked(
                context, wait, task.waitOrderIndex, task.waitOrderReady,
                task.waitOrderFailed)) {
          finalizeStatus = OBELISK_RT_INVALID_FRAME;
          break;
        }
        if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY)
          task.wakeTime = wait->payload > UINT64_MAX - context->schedulerTime
                              ? UINT64_MAX
                              : context->schedulerTime + wait->payload;
        if (signalWait && !sameSignalWait &&
            !obelisk_rt_register_signal_wait_unlocked(
                context, wait, task.signalSubscriptions, task.signalLatch,
                task.id, true))
          finalizeStatus = context->schedulerStatus;
        break;
      }
      case OBELISK_RT_FRAGMENT_PROCESS_SUSPEND:
        if (!task.signalSubscriptions.empty() ||
            task.computedObserverWaitRegistered)
          obelisk_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        task.suspendKind = OBELISK_RT_SUSPEND_NONE;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
        task.explicitlySuspended = true;
        task.urgent = false;
        task.queuedRegion = task.homeRegion;
        break;
      case OBELISK_RT_FRAGMENT_TASK_CALL: {
        if (!pendingActivation) {
          finalizeStatus = OBELISK_RT_INVALID_BYTECODE;
          break;
        }
        task.callers.reserve(checkedSizeSum(task.callers.size(), 1));
        if (!task.signalSubscriptions.empty() ||
            task.computedObserverWaitRegistered)
          obelisk_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        task.callers.push_back({task.function, task.continuation,
                                std::move(task.frame), task.scratchOffset,
                                task.scratchSize, task.scheduleRank,
                                task.controls.size()});
        DesignActivation activation = std::move(pendingActivation->activation);
        task.function = activation.function;
        task.continuation = activation.continuation;
        task.frame = std::move(activation.frame);
        task.scratchOffset = activation.scratchOffset;
        task.scratchSize = activation.scratchSize;
        task.suspendKind = OBELISK_RT_SUSPEND_NONE;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
        task.urgent = true;
        task.queuedRegion = task.homeRegion;
        pendingActivation->disarm();
        currentFrameReleased = false;
        break;
      }
      case OBELISK_RT_FRAGMENT_TERMINATE:
        if (!task.signalSubscriptions.empty() ||
            task.computedObserverWaitRegistered)
          obelisk_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        if (!task.callers.empty()) {
          DesignActivation caller = std::move(task.callers.back());
          task.callers.pop_back();
          context->designTaskFrames.release(std::move(task.frame));
          task.function = caller.function;
          task.continuation = caller.continuation;
          task.frame = std::move(caller.frame);
          task.scratchOffset = caller.scratchOffset;
          task.scratchSize = caller.scratchSize;
          task.scheduleRank = caller.scheduleRank;
          task.suspendKind = OBELISK_RT_SUSPEND_NONE;
          task.waitOffset = 0;
          task.waitSize = 0;
          task.waitGenerations.clear();
          task.signalTriggered = false;
          task.urgent = true;
          task.queuedRegion = task.homeRegion;
          currentFrameReleased = false;
        } else {
          obelisk_rt_program_complete_unlocked(context, task.id,
                                               task.programOwner);
          obelisk_rt_reparent_process_children_unlocked(context, task.id,
                                                        task.parent);
          context->terminatedDesignTasks.insert(task.id, task.random);
          releaseDesignTaskOwnedStatesUnlocked(context, task.id);
          obelisk_rt_release_controls_unlocked(context, task.controls);
          task.controls.clear();
          task.terminated = true;
          task.waitOffset = 0;
          task.waitSize = 0;
          task.waitGenerations.clear();
          task.signalTriggered = false;
          task.urgent = false;
          if (++context->schedulerEpoch == 0)
            context->schedulerEpoch = 1;
        }
        break;
      default:
        finalizeStatus = OBELISK_RT_INVALID_BYTECODE;
        break;
      }
      if (finalizeStatus == OBELISK_RT_OK) {
        if (task.terminated)
          context->designTaskFrames.release(std::move(task.frame));
        else {
          context->scheduledDesignTasks.push_back(std::move(task));
          uint64_t scheduledID = context->scheduledDesignTasks.back().id;
          OBELISK_RT_TRY {
            context->scheduledDesignTaskIndices[scheduledID] =
                context->scheduledDesignTasks.size() - 1;
            if (!indexedSignalBlocked(context->scheduledDesignTasks.back()))
              context->designPollCandidates.insert(scheduledID);
          }
          OBELISK_RT_CATCH_ALL {
            context->scheduledDesignTaskIndices.erase(scheduledID);
            context->designPollCandidates.erase(scheduledID);
            task = std::move(context->scheduledDesignTasks.back());
            context->scheduledDesignTasks.pop_back();
            OBELISK_RT_RETHROW;
          }
        }
        // Keep the dequeued task addressable through finalization and requeue
        // so a computed wait with only event/managed dependencies can attach
        // its owner-local registration bit. On allocation failure,
        // abandonTask still sees the owner and removes that registration.
        context->activeDesignTaskID = 0;
        context->activeDesignTask = nullptr;
        context->activeRandom = nullptr;
        context->activeDesignTaskPhase = 0;
        context->activeHomeRegion = UINT32_MAX;
        context->activeExecRegion = UINT32_MAX;
        context->activeLogicalProcessToken = 0;
        context->activeProgramOwner = 0;
        context->controlEscapePending = false;
        context->activeLogicalProcessParent = 0;
        context->activeWaitOrderFailed = false;
        context->designTaskExecuting = false;
        taskDequeued = false;
      }
    }
    if (finalizeStatus != OBELISK_RT_OK)
      return abandonTask(finalizeStatus);
    *outProgress = true;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    return abandonTask(OBELISK_RT_OUT_OF_MEMORY);
  }
  OBELISK_RT_CATCH_ALL { return abandonTask(OBELISK_RT_INVALID_BYTECODE); }
}

OBELISK_RT_FEATURE_TEXT obelisk_rt_status runOneDesignTaskCohort(
    obelisk_rt_context *context, uint32_t maximumRegion, uint32_t maximumRank,
    uint64_t maximumInsertionSequence, bool *outProgress) noexcept {
  return runOneDesignTaskImpl<true>(context, maximumRegion, maximumRank,
                                    maximumInsertionSequence, outProgress);
}

obelisk_rt_status obelisk_rt_run_one_design_task(
    obelisk_rt_context *context, uint32_t maximumRegion, uint32_t maximumRank,
    uint64_t maximumInsertionSequence, bool *outProgress) noexcept {
  bool enableReadyCohort = false;
  if (context && !context->designReadyCohortExactScan &&
      !context->nativeScheduleDesignTaskFilterActive) {
    DesignReadyCohortState *cohort = context->designReadyCohort.get();
    if (!cohort)
      enableReadyCohort = context->designPollCandidates.size() > 16;
    else if (!cohort->suppressed)
      enableReadyCohort = true;
    else {
      bool structuralChange =
          cohort->nextDesignTaskID != context->nextDesignTaskID ||
          context->designPollCandidates.size() >
              cohort->suppressedCandidateHighWater;
      bool transientReprobe =
          !cohort->persistentSuppression &&
          (cohort->selectionGeneration !=
               context->schedulerSelectionGeneration ||
           cohort->schedulerTime != context->schedulerTime ||
           cohort->runningFinals != context->schedulerRunningFinals);
      enableReadyCohort = structuralChange || transientReprobe;
    }
  }
  if (enableReadyCohort)
    return runOneDesignTaskCohort(context, maximumRegion, maximumRank,
                                  maximumInsertionSequence, outProgress);
  return runOneDesignTaskImpl<false>(context, maximumRegion, maximumRank,
                                     maximumInsertionSequence, outProgress);
}

obelisk_rt_status obelisk_rt_prime_design_task(obelisk_rt_context *context,
                                               uint64_t taskID) noexcept {
  if (!context || taskID == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  struct ActiveStateGuard {
    obelisk_rt_context *context;
    obelisk_rt_process_instance_v1 *native = context->activeNativeProcess;
    uint64_t logical = context->activeLogicalProcessToken;
    uint64_t programOwner = context->activeProgramOwner;
    uint64_t logicalParent = context->activeLogicalProcessParent;
    uint64_t design = context->activeDesignTaskID;
    ScheduledDesignTask *designTask = context->activeDesignTask;
    uint32_t phase = context->activeDesignTaskPhase;
    uint32_t home = context->activeHomeRegion;
    uint32_t region = context->activeExecRegion;
    bool waitOrderFailed = context->activeWaitOrderFailed;
    bool designExecuting = context->designTaskExecuting;
    bool escapePending = context->controlEscapePending;
    obelisk_rt_random_state_v1 *random = context->activeRandom;
    bool designFilter = context->nativeScheduleDesignTaskFilterActive;
    uint64_t forcedDesignTask = context->nativeScheduleForcedDesignTask;
    std::vector<uint64_t> controls = std::move(context->activeControls);

    ~ActiveStateGuard() noexcept {
      context->activeControls = std::move(controls);
      context->activeNativeProcess = native;
      context->activeLogicalProcessToken = logical;
      context->activeProgramOwner = programOwner;
      context->activeLogicalProcessParent = logicalParent;
      context->activeDesignTaskID = design;
      context->activeDesignTask = designTask;
      context->activeDesignTaskPhase = phase;
      context->activeHomeRegion = home;
      context->activeExecRegion = region;
      context->activeWaitOrderFailed = waitOrderFailed;
      context->designTaskExecuting = designExecuting;
      context->controlEscapePending = escapePending;
      context->activeRandom = random;
      obelisk_rt_set_design_task_filter_unlocked(context, designFilter,
                                                 forcedDesignTask);
    }
  } activeState{context};
  OBELISK_RT_TRY {
    // A primed child executes inside its parent's SPAWN intrinsic. Temporarily
    // lend the active-context fields to the child, force scheduler selection
    // to that one task, and restore the parent before returning from the
    // intrinsic. This establishes the wait atomically with the source
    // statement without yielding to unrelated work.
    context->activeNativeProcess = nullptr;
    context->activeLogicalProcessToken = 0;
    context->activeProgramOwner = 0;
    context->activeLogicalProcessParent = 0;
    context->activeDesignTaskID = 0;
    context->activeDesignTask = nullptr;
    context->activeDesignTaskPhase = 0;
    context->activeHomeRegion = UINT32_MAX;
    context->activeExecRegion = UINT32_MAX;
    context->activeWaitOrderFailed = false;
    context->designTaskExecuting = false;
    context->controlEscapePending = false;
    context->activeRandom = nullptr;
    obelisk_rt_set_design_task_filter_unlocked(context, true, taskID);

    for (uint32_t step = 0; step != 1024; ++step) {
      bool progress = false;
      obelisk_rt_status status = obelisk_rt_run_one_design_task(
          context, UINT32_MAX, UINT32_MAX, UINT64_MAX, &progress);
      if (status != OBELISK_RT_OK || !progress)
        return status != OBELISK_RT_OK ? status : OBELISK_RT_INVALID_LIFECYCLE;
      auto indexed = context->scheduledDesignTaskIndices.find(taskID);
      if (indexed == context->scheduledDesignTaskIndices.end() ||
          indexed->second >= context->scheduledDesignTasks.size())
        return OBELISK_RT_INVALID_LIFECYCLE;
      const ScheduledDesignTask &task =
          context->scheduledDesignTasks[indexed->second];
      if (task.id != taskID || task.terminated || task.explicitlySuspended)
        return OBELISK_RT_INVALID_LIFECYCLE;
      if (task.started && task.suspendKind != OBELISK_RT_SUSPEND_NONE)
        return OBELISK_RT_OK;
    }
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}
