//===- DesignBytecode.cpp - Design-wide validated bytecode interpreter ----===//

#include "DesignBytecodeExecution.h"
#include "DesignBytecodeImage.h"
#include "DesignBytecodeLogic.h"
#include "DesignBytecodeNets.h"
#include "DesignBytecodeRoots.h"
#include "ProcessObservers.h"
#include "ProcessPacking.h"
#include "ProcessShared.h"
#include "ProcessSignals.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "talos/Runtime/StableHandle.h"
#include "talos/Runtime/StableHash.h"

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

decltype(&talos_rt_v1_dpi_aggregate_state_alloc)
    designBytecodeDpiAggregateStateAlloc = nullptr;

namespace {

using namespace talos::designbytecode;

size_t checkedSizeSum(size_t lhs, size_t rhs) {
  if (rhs > std::numeric_limits<size_t>::max() - lhs)
    talos_rt_out_of_memory();
  return lhs + rhs;
}

bool hasSameDirectSignalWait(const ScheduledDesignTask &task,
                             const talos_rt_wait_record_v1 *wait) {
  if (!wait || !task.signalLatch ||
      task.signalSubscriptions.size() != wait->count)
    return false;
  const auto *entries =
      reinterpret_cast<const talos_rt_wait_entry_v1 *>(wait + 1);
  bool suppressActiveSelf =
      (wait->flags & TALOS_RT_WAIT_SUPPRESS_ACTIVE_SELF) != 0;
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
  ScopedReusableByteBuffer(talos_rt_context *context, size_t size)
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
  ScopedCopyMapBuffer(talos_rt_context *context, size_t size) {
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
  talos_rt_context *context = nullptr;
  std::vector<std::pair<uint32_t, uint64_t>> retainedAutomaticStates;
  bool ownsRetainedAutomaticStates = false;

  ~PendingDesignActivation() noexcept;
  void disarm() noexcept {
    ownsRetainedAutomaticStates = false;
    context = nullptr;
    retainedAutomaticStates.clear();
  }
};

talos_rt_status releaseCapturedAutomaticStates(const Image &image,
                                                 uint32_t functionIndex,
                                                 talos_rt_context *context,
                                                 const uint8_t *canonicalFrame,
                                                 uint64_t canonicalFrameSize) {
  if (!context || !canonicalFrame)
    return TALOS_RT_OK;
  Function function = functionAt(image, functionIndex);
  if ((function.flags & TALOS_RT_DESIGN_FUNCTION_PROCESS) == 0)
    return TALOS_RT_OK;
  auto stableAt = [&](uint64_t index, uint64_t &stable,
                      bool &hasHandle) -> talos_rt_status {
    hasHandle = false;
    stable = UINT64_MAX;
    CaptureRecord capture = captureAt(image, index);
    if (capture.function != functionIndex)
      return TALOS_RT_OK;
    Layout layout = layoutAt(image, function, capture.argument);
    if (layout.kind != TALOS_RT_DBREG_HANDLE ||
        capture.valueOffset == UINT64_MAX)
      return TALOS_RT_OK;
    if (capture.valueOffset > canonicalFrameSize ||
        8 > canonicalFrameSize - capture.valueOffset)
      return TALOS_RT_INVALID_FRAME;
    std::memcpy(&stable, canonicalFrame + capture.valueOffset, 8);
    hasHandle = stable != UINT64_MAX;
    return TALOS_RT_OK;
  };

  // Validate every captured handle before changing any reference count.
  for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
    uint64_t stable = UINT64_MAX;
    bool hasHandle = false;
    talos_rt_status status = stableAt(index, stable, hasHandle);
    if (status != TALOS_RT_OK)
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
      return TALOS_RT_INVALID_HANDLE;
  }

  // Decrement in place without allocating. If a duplicate capture exceeds
  // the available count, restore all earlier decrements before failing.
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto rollback = [&](uint64_t end) {
    for (uint64_t index = 0; index != end; ++index) {
      uint64_t stable = UINT64_MAX;
      bool hasHandle = false;
      if (stableAt(index, stable, hasHandle) != TALOS_RT_OK || !hasHandle)
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
      return TALOS_RT_INVALID_HANDLE;
    }
    --found->second.referenceCount;
  }
  for (auto state = context->nativeAutomaticStates.begin();
       state != context->nativeAutomaticStates.end();)
    if (state->second.referenceCount == 0) {
      talos_rt_erase_automatic_bookkeeping_unlocked(context, state->first);
      state = context->nativeAutomaticStates.erase(state);
    } else
      ++state;
  return TALOS_RT_OK;
}

PendingDesignActivation::~PendingDesignActivation() noexcept {
  if (!context)
    return;
  if (ownsRetainedAutomaticStates) {
    TALOS_RT_TRY {
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
          talos_rt_erase_automatic_bookkeeping_unlocked(context,
                                                          state->first);
          state = context->nativeAutomaticStates.erase(state);
        } else {
          ++state;
        }
    }
    TALOS_RT_CATCH_ALL {
      // Destructors on error paths must not obscure the scheduler failure.
    }
  }
  context->designTaskFrames.release(std::move(activation.frame));
}

void releaseDesignTaskOwnedStatesUnlocked(talos_rt_context *context,
                                          uint64_t taskID) {
  for (auto state = context->nativeAutomaticStates.begin();
       state != context->nativeAutomaticStates.end();) {
    if (state->second.designOwner != taskID) {
      ++state;
      continue;
    }
    state->second.designOwner = 0;
    if (state->second.referenceCount <= 1) {
      talos_rt_erase_automatic_bookkeeping_unlocked(context, state->first);
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

/// Copies a register's bytes. Scalar logic registers are one or two limbs;
/// copy those with fixed-size moves instead of a library call. The ranges
/// may be the same register but never partially overlap.
void copyRegisterBytes(uint8_t *to, const uint8_t *from, uint64_t size) {
  if (to == from)
    return;
  uint64_t limbs[2];
  switch (size) {
  case 8:
    std::memcpy(limbs, from, 8);
    std::memcpy(to, limbs, 8);
    return;
  case 16:
    std::memcpy(limbs, from, 16);
    std::memcpy(to, limbs, 16);
    return;
  default:
    std::memmove(to, from, static_cast<size_t>(size));
  }
}

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
  copyRegisterBytes(destination.data + destinationLayout.offset,
                    source.data + sourceLayout.offset, sourceLayout.size);
  return true;
}

bool readKnownScalar(const Image &image, const Frame &frame, uint32_t reg,
                     uint64_t &value) {
  Layout layout = layoutAt(image, frame.function, reg);
  if ((layout.kind != TALOS_RT_DBREG_BITS &&
       layout.kind != TALOS_RT_DBREG_LOGIC) ||
      layout.width == 0 || layout.width > 64)
    return false;
  value = 0;
  if (layout.size >= 8)
    std::memcpy(&value, frame.data + layout.offset, 8);
  else
    std::memcpy(&value, frame.data + layout.offset,
                static_cast<size_t>(layout.size));
  if (layout.kind == TALOS_RT_DBREG_LOGIC) {
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
  if ((layout.kind != TALOS_RT_DBREG_BITS &&
       layout.kind != TALOS_RT_DBREG_LOGIC) ||
      layout.width == 0 || layout.width > 64)
    return false;
  value &= finalMask(layout.width);
  if (layout.size >= 8)
    std::memcpy(frame.data + layout.offset, &value, 8);
  else
    std::memcpy(frame.data + layout.offset, &value,
                static_cast<size_t>(layout.size));
  if (layout.kind == TALOS_RT_DBREG_LOGIC) {
    uint64_t known = 0;
    std::memcpy(frame.data + layout.offset + 8, &known, 8);
  }
  return true;
}

bool copyMap(const Image &image, const Frame &source, Frame &destination,
             uint64_t first, uint64_t count, talos_rt_context *context) {
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
      talos_rt_out_of_memory();
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

talos_rt_status prepareTaskActivation(
    const Image &image, Frame &caller, talos_rt_context *context,
    uint32_t calleeIndex, uint32_t firstOperand, uint32_t operandCount,
    std::unique_ptr<PendingDesignActivation> *pendingActivation) {
  if (!context)
    return TALOS_RT_INVALID_ARGUMENT;
  if (!pendingActivation || *pendingActivation)
    return TALOS_RT_INVALID_LIFECYCLE;
  if (calleeIndex >= image.functionCount)
    return TALOS_RT_INVALID_BYTECODE;
  Function callee = functionAt(image, calleeIndex);
  if ((callee.flags & TALOS_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
      callee.resultCount != 0 || callee.argumentCount != operandCount ||
      !validMap(image, caller.function, callee, firstOperand, operandCount))
    return TALOS_RT_INVALID_BYTECODE;
  for (uint32_t index = 0; index != operandCount; ++index)
    if (operandAt(image, firstOperand + index).first != index)
      return TALOS_RT_INVALID_BYTECODE;

  uint64_t canonicalSize =
      (callee.flags & TALOS_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK) >> 1;
  if (callee.scratchAlignment == 0 ||
      canonicalSize > UINT64_MAX - (callee.scratchAlignment - 1))
    return TALOS_RT_INVALID_BYTECODE;
  uint64_t scratchOffset = (canonicalSize + callee.scratchAlignment - 1) &
                           ~(callee.scratchAlignment - 1);
  if (scratchOffset > UINT64_MAX - callee.scratchSize ||
      scratchOffset + callee.scratchSize > std::numeric_limits<size_t>::max())
    return TALOS_RT_OUT_OF_MEMORY;
  auto pending = std::make_unique<PendingDesignActivation>();
  pending->context = context;
  DesignActivation &activation = pending->activation;
  activation.function = calleeIndex;
  if (context->vpiState) {
    auto status =
        talos_rt_vpi_pending_call(context, &activation.vpiSourceCallID);
    if (status != TALOS_RT_OK)
      return status;
    if (activation.vpiSourceCallID) {
      status = talos_rt_cached_vpi_call_target(
          context, activation.vpiSourceCallID, callee.id);
      if (status != TALOS_RT_OK)
        return status == TALOS_RT_EOF ? TALOS_RT_INVALID_ARGUMENT : status;
    }
  }
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
      return TALOS_RT_INVALID_BYTECODE;
    if (capture.valueOffset == UINT64_MAX)
      continue;
    uint32_t sourceRegister =
        operandAt(image, firstOperand + capture.argument).second;
    Layout source = layoutAt(image, caller.function, sourceRegister);
    if (source.kind == TALOS_RT_DBREG_HANDLE) {
      uint64_t stable = UINT64_MAX;
      if (!encodeCanonicalHandle(caller.data + source.offset, stable))
        return TALOS_RT_INVALID_HANDLE;
      std::memcpy(activation.frame.data() + capture.valueOffset, &stable,
                  sizeof(stable));
      uint32_t automaticID = 0;
      int64_t automaticOffset = 0;
      if (decodeAutomaticHandle(stable, automaticID, automaticOffset) &&
          ++retainedAutomaticStates[automaticID] == 0)
        return TALOS_RT_OUT_OF_RESOURCES;
      continue;
    }
    std::memcpy(activation.frame.data() + capture.valueOffset,
                caller.data + source.offset, capture.planeSize);
    if (capture.unknownOffset != UINT64_MAX) {
      uint64_t sourcePlane = source.kind == TALOS_RT_DBREG_LOGIC
                                 ? limbCount(source.width) * sizeof(uint64_t)
                                 : capture.planeSize;
      std::memcpy(activation.frame.data() + capture.unknownOffset,
                  caller.data + source.offset + sourcePlane, capture.planeSize);
    }
  }
  if (copied != callee.argumentCount)
    return TALOS_RT_INVALID_BYTECODE;
  pending->retainedAutomaticStates.reserve(retainedAutomaticStates.size());
  for (const auto &[automaticID, count] : retainedAutomaticStates)
    pending->retainedAutomaticStates.emplace_back(automaticID, count);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    for (const auto &[automaticID, count] : pending->retainedAutomaticStates) {
      auto found = context->nativeAutomaticStates.find(automaticID);
      if (found == context->nativeAutomaticStates.end())
        return TALOS_RT_INVALID_HANDLE;
      if (count > UINT64_MAX - found->second.referenceCount)
        return TALOS_RT_OUT_OF_RESOURCES;
    }
    for (const auto &[automaticID, count] : pending->retainedAutomaticStates)
      context->nativeAutomaticStates.find(automaticID)->second.referenceCount +=
          count;
  }
  pending->ownsRetainedAutomaticStates = true;
  *pendingActivation = std::move(pending);
  return TALOS_RT_OK;
}

// The interpreter's fast path. Most instructions in typical process bodies
// operate on scalar two- or four-state registers. Decoding such an
// instruction once into a FastOp lets the loop execute it on frame words
// directly; every other instruction, and any fast instruction whose dynamic
// precondition fails, runs the general implementation below. Each handler
// reproduces that implementation's result bit for bit, including the
// four-state rules of IEEE 1800-2023 11.4 and readLogic/writeLogic's masking.
enum FastKind : uint8_t {
  kFastGeneric = 0,
  kFastConstant,
  kFastMove,
  kFastJump,
  kFastBranch,
  kFastNot,
  kFastAnd,
  kFastOr,
  kFastXor,
  kFastAdd,
  kFastSub,
  kFastShl,
  kFastLshr,
  kFastAshr,
  kFastExtract,
  kFastConcat,
  kFastCompare,
  kFastSelect,
  kFastLoadFrame,
  kFastStoreFrame,
  kFastLoadFrameHandle,
  kFastStoreFrameHandle,
  kFastHandleID,
  kFastLoadState,
  kFastStoreState,
};

constexpr uint8_t kFastDestinationFourState = 1;
constexpr uint8_t kFastSource0FourState = 2;
constexpr uint8_t kFastSource1FourState = 4;
constexpr uint8_t kFastSource2FourState = 8;

inline uint64_t fastMask(uint32_t width) {
  return width >= 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
}

inline uint64_t fastLoad(const uint8_t *data, uint32_t offset) {
  uint64_t word;
  std::memcpy(&word, data + offset, sizeof(word));
  return word;
}

inline void fastStoreWord(uint8_t *data, uint32_t offset, uint64_t word) {
  std::memcpy(data + offset, &word, sizeof(word));
}

/// Reads a scalar register's planes, masked to its width as readLogic does.
inline void fastRead(const uint8_t *data, uint32_t offset, bool fourState,
                     uint32_t width, uint64_t &value, uint64_t &unknown) {
  uint64_t mask = fastMask(width);
  value = fastLoad(data, offset) & mask;
  unknown = fourState ? fastLoad(data, offset + 8) & mask : 0;
}

/// Writes a result as writeLogic does: masked to the destination width, and
/// X/Z mapped to zero in a two-state destination.
inline void fastWrite(uint8_t *data, const FastOp &op, uint64_t value,
                      uint64_t unknown) {
  uint64_t mask = fastMask(op.width);
  value &= mask;
  unknown &= mask;
  if (op.fourState & kFastDestinationFourState) {
    fastStoreWord(data, op.destination, value);
    fastStoreWord(data, op.destination + 8, unknown);
  } else {
    fastStoreWord(data, op.destination, value & ~unknown);
  }
}

/// A frame transfer of 1 to 8 bytes; larger transfers stay generic.
inline void fastCopy(uint8_t *to, const uint8_t *from, uint64_t size) {
  switch (size) {
  case 1:
    *to = *from;
    return;
  case 2:
    std::memcpy(to, from, 2);
    return;
  case 4:
    std::memcpy(to, from, 4);
    return;
  case 8:
    std::memcpy(to, from, 8);
    return;
  default:
    std::memcpy(to, from, static_cast<size_t>(size));
  }
}

FastOp decodeFastOp(const Image &image, const Function &function,
                    const Instruction &instruction) {
  FastOp op;
  struct Scalar {
    bool valid = false, fourState = false;
    uint32_t offset = 0, width = 0;
  };
  auto scalar = [&](uint32_t reg) {
    Scalar result;
    if (!validRegister(function, reg))
      return result;
    Layout layout = layoutAt(image, function, reg);
    bool fourState = layout.kind == TALOS_RT_DBREG_LOGIC;
    if ((layout.kind != TALOS_RT_DBREG_BITS && !fourState) ||
        layout.width == 0 || layout.width > 64 ||
        layout.size != (fourState ? 16u : 8u) ||
        layout.offset > UINT32_MAX - 16)
      return result;
    result = {true, fourState, static_cast<uint32_t>(layout.offset),
              layout.width};
    return result;
  };
  auto use = [&](const Scalar &value, uint32_t &offset, uint8_t bit) {
    offset = value.offset;
    if (value.fourState)
      op.fourState |= bit;
  };
  auto same = [](const Scalar &left, const Scalar &right) {
    return left.valid && right.valid && left.fourState == right.fourState &&
           left.width == right.width;
  };
  Scalar destination = scalar(instruction.destination);
  Scalar source0 = scalar(instruction.source0);
  Scalar source1 = scalar(instruction.source1);
  op.flags = instruction.flags;
  op.width = static_cast<uint8_t>(destination.width);
  op.sourceWidth = static_cast<uint8_t>(source0.width);
  auto unary = [&](uint8_t kind) {
    if (!same(destination, source0))
      return;
    op.kind = kind;
    use(destination, op.destination, kFastDestinationFourState);
    use(source0, op.source0, kFastSource0FourState);
  };
  auto binary = [&](uint8_t kind) {
    if (!same(destination, source0) || !same(destination, source1))
      return;
    unary(kind);
    use(source1, op.source1, kFastSource1FourState);
  };
  switch (instruction.opcode) {
  case TALOS_RT_DB_CONSTANT: {
    if (!destination.valid)
      break;
    uint64_t size = destination.fourState ? 16 : 8;
    if (instruction.immediate > image.constantSize ||
        size > image.constantSize - instruction.immediate)
      break;
    const uint8_t *constant =
        image.data + image.constants + instruction.immediate;
    op.kind = kFastConstant;
    use(destination, op.destination, kFastDestinationFourState);
    op.immediate = fastLoad(constant, 0) & fastMask(destination.width);
    op.immediate2 = destination.fourState
                        ? fastLoad(constant, 8) & fastMask(destination.width)
                        : 0;
    break;
  }
  case TALOS_RT_DB_MOVE:
    if (validRegister(function, instruction.destination) &&
        validRegister(function, instruction.source0) &&
        compatible(layoutAt(image, function, instruction.destination),
                   layoutAt(image, function, instruction.source0)))
      unary(kFastMove);
    break;
  case TALOS_RT_DB_JUMP:
    // Block arguments (source1 != 0) keep copyMap's parallel assignment.
    if (instruction.source1 == 0 && instruction.source0 <= image.operandCount) {
      op.kind = kFastJump;
      op.immediate = instruction.immediate;
    }
    break;
  case TALOS_RT_DB_BRANCH:
    if (instruction.source1 == 0 && instruction.source0 <= image.operandCount &&
        destination.valid) {
      op.kind = kFastBranch;
      use(destination, op.destination, kFastDestinationFourState);
      op.immediate = instruction.immediate;
    }
    break;
  case TALOS_RT_DB_NOT:
    unary(kFastNot);
    break;
  case TALOS_RT_DB_AND:
    binary(kFastAnd);
    break;
  case TALOS_RT_DB_OR:
    binary(kFastOr);
    break;
  case TALOS_RT_DB_XOR:
    binary(kFastXor);
    break;
  case TALOS_RT_DB_ADD:
    binary(kFastAdd);
    break;
  case TALOS_RT_DB_SUB:
    binary(kFastSub);
    break;
  case TALOS_RT_DB_SHL:
  case TALOS_RT_DB_LSHR:
  case TALOS_RT_DB_ASHR:
    if (same(destination, source0) && source1.valid) {
      op.kind = instruction.opcode == TALOS_RT_DB_SHL    ? kFastShl
                : instruction.opcode == TALOS_RT_DB_LSHR ? kFastLshr
                                                           : kFastAshr;
      use(destination, op.destination, kFastDestinationFourState);
      use(source0, op.source0, kFastSource0FourState);
      use(source1, op.source1, kFastSource1FourState);
      op.reserved = static_cast<uint16_t>(source1.width);
    }
    break;
  case TALOS_RT_DB_EXTRACT:
    if (destination.valid && source0.valid &&
        instruction.source1 == kInvalidRegister &&
        instruction.flags != TALOS_RT_DB_AGGREGATE_MANAGED) {
      op.kind = kFastExtract;
      use(destination, op.destination, kFastDestinationFourState);
      use(source0, op.source0, kFastSource0FourState);
      op.immediate = instruction.immediate;
    }
    break;
  case TALOS_RT_DB_CONCAT:
    if (destination.valid && source0.valid && source1.valid &&
        destination.fourState == source0.fourState &&
        destination.fourState == source1.fourState &&
        source0.width + source1.width == destination.width) {
      op.kind = kFastConcat;
      use(destination, op.destination, kFastDestinationFourState);
      use(source0, op.source0, kFastSource0FourState);
      use(source1, op.source1, kFastSource1FourState);
      op.immediate = source1.width;
    }
    break;
  case TALOS_RT_DB_COMPARE:
    if (destination.valid && destination.width == 1 && same(source0, source1) &&
        instruction.flags <= TALOS_RT_DB_CMP_CASEXZ_EQ) {
      op.kind = kFastCompare;
      use(destination, op.destination, kFastDestinationFourState);
      use(source0, op.source0, kFastSource0FourState);
      use(source1, op.source1, kFastSource1FourState);
    }
    break;
  case TALOS_RT_DB_SELECT: {
    Scalar condition = scalar(instruction.source2);
    if (same(destination, source0) && same(destination, source1) &&
        condition.valid && condition.width == 1) {
      binary(kFastSelect);
      use(condition, op.source2, kFastSource2FourState);
    }
    break;
  }
  case TALOS_RT_DB_LOAD_FRAME:
  case TALOS_RT_DB_STORE_FRAME: {
    // A scalar register's planes are each one limb; the canonical frame packs
    // each plane at the transfer size (half of it per plane when four-state).
    const Scalar &value =
        instruction.opcode == TALOS_RT_DB_LOAD_FRAME ? destination : source0;
    if (!value.valid)
      break;
    uint64_t size = value.fourState ? 16 : 8;
    uint64_t transfer =
        instruction.auxiliary != 0 ? instruction.auxiliary : size;
    uint64_t plane = value.fourState ? transfer / 2 : transfer;
    uint64_t displacement =
        instruction.source1 != 0 ? instruction.source1 : plane;
    if (plane == 0 || plane > 8 || (value.fourState && transfer % 2 != 0) ||
        displacement > UINT32_MAX)
      break;
    op.kind = instruction.opcode == TALOS_RT_DB_LOAD_FRAME ? kFastLoadFrame
                                                             : kFastStoreFrame;
    use(value, op.destination, kFastDestinationFourState);
    op.width = static_cast<uint8_t>(value.width);
    op.source1 = static_cast<uint32_t>(displacement);
    op.immediate = instruction.immediate;
    op.immediate2 = (transfer << 8) | plane;
    break;
  }
  default:
    break;
  }
  if (op.kind != kFastGeneric)
    return op;
  // Handle transfers and scalar state accesses.
  auto handle = [&](uint32_t reg) {
    if (!validRegister(function, reg))
      return false;
    Layout layout = layoutAt(image, function, reg);
    return layout.kind == TALOS_RT_DBREG_HANDLE && layout.size == 32 &&
           layout.offset <= UINT32_MAX - 32;
  };
  auto handleOffset = [&](uint32_t reg) {
    return static_cast<uint32_t>(layoutAt(image, function, reg).offset);
  };
  switch (instruction.opcode) {
  case TALOS_RT_DB_LOAD_FRAME:
    if (handle(instruction.destination)) {
      op.kind = kFastLoadFrameHandle;
      op.destination = handleOffset(instruction.destination);
      op.immediate = instruction.immediate;
      op.immediate2 = instruction.auxiliary;
    }
    break;
  case TALOS_RT_DB_STORE_FRAME:
    if (handle(instruction.source0)) {
      op.kind = kFastStoreFrameHandle;
      op.source0 = handleOffset(instruction.source0);
      op.immediate = instruction.immediate;
    }
    break;
  case TALOS_RT_DB_HANDLE_ID:
    if (destination.valid && handle(instruction.source0)) {
      op.kind = kFastHandleID;
      use(destination, op.destination, kFastDestinationFourState);
      op.source0 = handleOffset(instruction.source0);
    }
    break;
  case TALOS_RT_DB_LOAD_STATE:
    if (destination.valid && handle(instruction.source0)) {
      op.kind = kFastLoadState;
      use(destination, op.destination, kFastDestinationFourState);
      op.source0 = handleOffset(instruction.source0);
    }
    break;
  case TALOS_RT_DB_STORE_STATE: {
    bool reportsChange =
        (instruction.flags & TALOS_RT_DB_STORE_STATE_CHANGED) != 0;
    if (source1.valid && handle(instruction.source0) &&
        (instruction.flags &
         ~(TALOS_RT_DB_STORE_STATE_CHANGED |
           TALOS_RT_DB_STORE_STATE_CONTINUOUS |
           TALOS_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION)) == 0 &&
        (!reportsChange || destination.valid)) {
      op.kind = kFastStoreState;
      op.width = static_cast<uint8_t>(source1.width);
      op.source0 = handleOffset(instruction.source0);
      use(source1, op.source1, kFastSource1FourState);
      if (reportsChange) {
        op.destination = destination.offset;
        if (destination.fourState)
          op.fourState |= kFastDestinationFourState;
        op.sourceWidth = static_cast<uint8_t>(destination.width);
      }
    }
    break;
  }
  default:
    break;
  }
  return op;
}

/// The handle register fields: kind, base, start, end.
struct FastHandle {
  uint32_t kind;
  uint64_t base;
  int64_t start, end;
};

inline FastHandle fastHandle(const uint8_t *data, uint32_t offset) {
  FastHandle handle;
  std::memcpy(&handle.kind, data + offset, 4);
  std::memcpy(&handle.base, data + offset + 8, 8);
  std::memcpy(&handle.start, data + offset + 16, 8);
  std::memcpy(&handle.end, data + offset + 24, 8);
  return handle;
}

/// Resolves a static state for `op`, caching the result. The caller holds
/// the context mutex. Returns false when the state is unknown or out of the
/// execution's state planes (the general path reports the error).
inline bool fastStaticState(const FastOp &op, talos_rt_context *context,
                            uint32_t id) {
  if (op.staticID == id && id != 0)
    return true;
  auto found = context->nativeStaticStates.find(id);
  if (found == context->nativeStaticStates.end() || !context->execution ||
      found->second.bitOffset > context->execution->state_bit_count ||
      found->second.bitWidth >
          context->execution->state_bit_count - found->second.bitOffset)
    return false;
  op.staticID = id;
  op.staticOffset = found->second.bitOffset;
  op.staticWidth = found->second.bitWidth;
  return true;
}

/// Resolves a canonical (not local, not automatic) state view of `width`
/// bits for a scalar access: its first bit in the canonical planes and the
/// handle the access publishes under. The caller holds the context mutex.
inline bool fastCanonicalView(const FastOp &op, talos_rt_context *context,
                              const FastHandle &handle, uint64_t width,
                              uint64_t &storageBit, uint64_t &published) {
  uint32_t id = 0;
  int64_t begin = 0;
  bool boundedStatic = decodeStaticHandle(handle.base, id, begin);
  if (!boundedStatic)
    begin = static_cast<int64_t>(handle.base);
  if (begin > handle.end || handle.start < 0 || handle.start < begin ||
      handle.start > handle.end ||
      width > static_cast<uint64_t>(handle.end - handle.start))
    return false;
  uint64_t absolute = static_cast<uint64_t>(handle.start);
  uint64_t available = context->stateValue.size() * 64;
  storageBit = absolute;
  if (boundedStatic) {
    if (!fastStaticState(op, context, id))
      return false;
    available = op.staticWidth;
    storageBit = op.staticOffset + absolute;
  }
  if (absolute > available || width > available - absolute ||
      storageBit + width > context->stateValue.size() * 64 ||
      storageBit + width > context->stateUnknown.size() * 64)
    return false;
  published = boundedStatic ? encodeStaticHandle(id, handle.start) : absolute;
  return true;
}

/// Runs a decoded instruction. Returns false, with nothing changed, when its
/// dynamic precondition fails; the caller then runs the general path. A
/// failure after an instruction took effect returns true and sets `status`.
inline bool executeFastOp(const FastOp &op, uint8_t *data,
                          uint8_t *canonicalFrame, uint64_t canonicalFrameSize,
                          talos_rt_context *context, uint64_t &pc,
                          talos_rt_status &status) {
  bool source0FourState = (op.fourState & kFastSource0FourState) != 0;
  bool source1FourState = (op.fourState & kFastSource1FourState) != 0;
  uint64_t mask = fastMask(op.width);
  switch (op.kind) {
  case kFastConstant:
    fastStoreWord(data, op.destination, op.immediate);
    if (op.fourState & kFastDestinationFourState)
      fastStoreWord(data, op.destination + 8, op.immediate2);
    break;
  case kFastMove:
    fastStoreWord(data, op.destination, fastLoad(data, op.source0));
    if (op.fourState & kFastDestinationFourState)
      fastStoreWord(data, op.destination + 8, fastLoad(data, op.source0 + 8));
    break;
  case kFastJump:
    pc = op.immediate;
    return true;
  case kFastBranch: {
    uint64_t value, unknown;
    fastRead(data, op.destination,
             (op.fourState & kFastDestinationFourState) != 0, op.width, value,
             unknown);
    if (unknown != 0 || value != 0) {
      pc = op.immediate;
      return true;
    }
    break;
  }
  case kFastNot: {
    uint64_t value, unknown;
    fastRead(data, op.source0, source0FourState, op.width, value, unknown);
    fastWrite(data, op, ~value & ~unknown, unknown);
    break;
  }
  case kFastAnd:
  case kFastOr:
  case kFastXor: {
    uint64_t lv, lu, rv, ru;
    fastRead(data, op.source0, source0FourState, op.width, lv, lu);
    fastRead(data, op.source1, source1FourState, op.width, rv, ru);
    uint64_t lk = ~lu, rk = ~ru;
    if (op.kind == kFastAnd) {
      uint64_t knownZero = (~lv & lk) | (~rv & rk);
      uint64_t knownOne = (lv & lk) & (rv & rk);
      fastWrite(data, op, knownOne, ~(knownZero | knownOne));
    } else if (op.kind == kFastOr) {
      uint64_t knownOne = (lv & lk) | (rv & rk);
      uint64_t knownZero = (~lv & lk) & (~rv & rk);
      fastWrite(data, op, knownOne, ~(knownZero | knownOne));
    } else {
      fastWrite(data, op, (lv ^ rv) & ~(lu | ru), lu | ru);
    }
    break;
  }
  case kFastAdd:
  case kFastSub: {
    uint64_t lv, lu, rv, ru;
    fastRead(data, op.source0, source0FourState, op.width, lv, lu);
    fastRead(data, op.source1, source1FourState, op.width, rv, ru);
    if (lu | ru)
      fastWrite(data, op, 0, UINT64_MAX);
    else
      fastWrite(data, op, op.kind == kFastAdd ? lv + rv : lv - rv, 0);
    break;
  }
  case kFastShl:
  case kFastLshr:
  case kFastAshr: {
    uint64_t value, unknown, amount, amountUnknown;
    fastRead(data, op.source0, source0FourState, op.width, value, unknown);
    fastRead(data, op.source1, source1FourState, op.reserved, amount,
             amountUnknown);
    if (amountUnknown != 0) {
      fastWrite(data, op, 0, UINT64_MAX);
      break;
    }
    bool oversized = amount >= op.width;
    if (op.kind == kFastShl) {
      fastWrite(data, op, oversized ? 0 : value << amount,
                oversized ? 0 : unknown << amount);
    } else {
      bool arithmetic = op.kind == kFastAshr;
      uint64_t fill = oversized ? mask : mask & ~(mask >> amount);
      uint64_t signValue =
          arithmetic && ((value >> (op.width - 1)) & 1) ? fill : 0;
      uint64_t signUnknown =
          arithmetic && ((unknown >> (op.width - 1)) & 1) ? fill : 0;
      fastWrite(data, op, (oversized ? 0 : value >> amount) | signValue,
                (oversized ? 0 : unknown >> amount) | signUnknown);
    }
    break;
  }
  case kFastExtract: {
    uint64_t value, unknown;
    fastRead(data, op.source0, source0FourState, op.sourceWidth, value,
             unknown);
    uint64_t low = op.immediate;
    uint64_t count = low < op.sourceWidth
                         ? std::min<uint64_t>(op.width, op.sourceWidth - low)
                         : 0;
    uint64_t kept = fastMask(static_cast<uint32_t>(count));
    // A static resize may sign extend; its padding is never X otherwise.
    bool signExtend = (op.flags & 1) != 0;
    uint64_t pad = mask & ~kept;
    uint64_t padValue =
        signExtend && ((value >> (op.sourceWidth - 1)) & 1) ? pad : 0;
    uint64_t padUnknown =
        signExtend && ((unknown >> (op.sourceWidth - 1)) & 1) ? pad : 0;
    uint64_t shiftedValue = count ? (value >> low) & kept : 0;
    uint64_t shiftedUnknown = count ? (unknown >> low) & kept : 0;
    fastWrite(data, op, shiftedValue | padValue, shiftedUnknown | padUnknown);
    break;
  }
  case kFastConcat: {
    uint64_t lv, lu, rv, ru;
    uint32_t rightWidth = static_cast<uint32_t>(op.immediate);
    fastRead(data, op.source0, source0FourState, op.width - rightWidth, lv, lu);
    fastRead(data, op.source1, source1FourState, rightWidth, rv, ru);
    fastWrite(data, op, rv | (rightWidth < 64 ? lv << rightWidth : 0),
              ru | (rightWidth < 64 ? lu << rightWidth : 0));
    break;
  }
  case kFastCompare: {
    uint64_t lv, lu, rv, ru;
    fastRead(data, op.source0, source0FourState, op.sourceWidth, lv, lu);
    fastRead(data, op.source1, source1FourState, op.sourceWidth, rv, ru);
    uint16_t predicate = op.flags;
    bool deterministic = predicate == TALOS_RT_DB_CMP_CASE_EQ ||
                         predicate == TALOS_RT_DB_CMP_CASE_NE ||
                         predicate == TALOS_RT_DB_CMP_CASEZ_EQ ||
                         predicate == TALOS_RT_DB_CMP_CASEXZ_EQ;
    bool wildcard = predicate == TALOS_RT_DB_CMP_WILD_EQ ||
                    predicate == TALOS_RT_DB_CMP_WILD_NE;
    bool value = false, unknownResult = false;
    if (!deterministic && !wildcard && (lu | ru)) {
      if (predicate == TALOS_RT_DB_CMP_EQ ||
          predicate == TALOS_RT_DB_CMP_NE) {
        if (((lv ^ rv) & ~(lu | ru)) != 0) {
          fastWrite(data, op, predicate == TALOS_RT_DB_CMP_NE, 0);
          break;
        }
      }
      fastWrite(data, op, 0, 1);
      break;
    }
    bool negativeLeft = (lv >> (op.sourceWidth - 1)) & 1;
    bool negativeRight = (rv >> (op.sourceWidth - 1)) & 1;
    bool signedOrder = predicate >= TALOS_RT_DB_CMP_SLT &&
                       predicate <= TALOS_RT_DB_CMP_SGE;
    int compared = lv == rv ? 0 : lv < rv ? -1 : 1;
    if (signedOrder && negativeLeft != negativeRight)
      compared = negativeLeft ? -1 : 1;
    switch (predicate) {
    case TALOS_RT_DB_CMP_EQ:
      value = compared == 0;
      break;
    case TALOS_RT_DB_CMP_NE:
      value = compared != 0;
      break;
    case TALOS_RT_DB_CMP_ULT:
    case TALOS_RT_DB_CMP_SLT:
      value = compared < 0;
      break;
    case TALOS_RT_DB_CMP_ULE:
    case TALOS_RT_DB_CMP_SLE:
      value = compared <= 0;
      break;
    case TALOS_RT_DB_CMP_UGT:
    case TALOS_RT_DB_CMP_SGT:
      value = compared > 0;
      break;
    case TALOS_RT_DB_CMP_UGE:
    case TALOS_RT_DB_CMP_SGE:
      value = compared >= 0;
      break;
    case TALOS_RT_DB_CMP_CASE_EQ:
    case TALOS_RT_DB_CMP_CASE_NE:
      value =
          (lv == rv && lu == ru) == (predicate == TALOS_RT_DB_CMP_CASE_EQ);
      break;
    case TALOS_RT_DB_CMP_CASEZ_EQ:
    case TALOS_RT_DB_CMP_CASEXZ_EQ: {
      uint64_t ignored = predicate == TALOS_RT_DB_CMP_CASEZ_EQ
                             ? (lu & lv) | (ru & rv)
                             : lu | ru;
      value = (~ignored & ((lu ^ ru) | (lv ^ rv))) == 0;
      break;
    }
    default: {
      // Wildcard equality: X/Z bits of the right operand match anything.
      uint64_t compared = ~ru;
      bool mismatch = ((lv ^ rv) & ~lu & compared) != 0;
      unknownResult = !mismatch && (lu & compared) != 0;
      value = !mismatch == (predicate == TALOS_RT_DB_CMP_WILD_EQ);
      break;
    }
    }
    if (unknownResult)
      fastWrite(data, op, 0, 1);
    else
      fastWrite(data, op, value, 0);
    break;
  }
  case kFastSelect: {
    uint64_t cv, cu;
    fastRead(data, op.source2, (op.fourState & kFastSource2FourState) != 0, 1,
             cv, cu);
    if (cu == 0) {
      uint32_t source = cv != 0 ? op.source0 : op.source1;
      fastStoreWord(data, op.destination, fastLoad(data, source));
      if (op.fourState & kFastDestinationFourState)
        fastStoreWord(data, op.destination + 8, fastLoad(data, source + 8));
      break;
    }
    if (op.flags != TALOS_RT_DB_SELECT_FOUR_STATE) {
      fastWrite(data, op, 0, UINT64_MAX);
      break;
    }
    uint64_t lv, lu, rv, ru;
    fastRead(data, op.source0, source0FourState, op.width, lv, lu);
    fastRead(data, op.source1, source1FourState, op.width, rv, ru);
    uint64_t mismatch = lu | ru | (lv ^ rv);
    fastWrite(data, op, ~mismatch & lv, mismatch);
    break;
  }
  case kFastLoadFrame:
  case kFastStoreFrame: {
    uint64_t transfer = op.immediate2 >> 8;
    uint64_t plane = op.immediate2 & 0xff;
    if (!canonicalFrame || op.immediate > canonicalFrameSize ||
        transfer > canonicalFrameSize - op.immediate)
      return false;
    bool fourState = (op.fourState & kFastDestinationFourState) != 0;
    uint8_t *frameValue = canonicalFrame + op.immediate;
    uint8_t *registerValue = data + op.destination;
    if (op.kind == kFastLoadFrame) {
      uint64_t value = 0, unknown = 0;
      fastCopy(reinterpret_cast<uint8_t *>(&value), frameValue, plane);
      if (fourState)
        fastCopy(reinterpret_cast<uint8_t *>(&unknown), frameValue + op.source1,
                 plane);
      // A transfer narrower than the register zero-fills the rest of it.
      fastStoreWord(registerValue, 0, value);
      if (fourState)
        fastStoreWord(registerValue, 8, unknown);
    } else {
      fastCopy(frameValue, registerValue, plane);
      if (fourState)
        fastCopy(frameValue + op.source1, registerValue + 8, plane);
    }
    break;
  }
  case kFastLoadFrameHandle: {
    if (!canonicalFrame || op.immediate > canonicalFrameSize ||
        8 > canonicalFrameSize - op.immediate)
      return false;
    uint64_t stable = fastLoad(canonicalFrame + op.immediate, 0);
    uint32_t kind = op.flags;
    int64_t width = static_cast<int64_t>(op.immediate2);
    uint32_t id = 0;
    int64_t offset = 0;
    // Null and dynamic events and automatic views take the general path.
    if ((kind == TALOS_RT_DESCRIPTOR_EVENT && stable == UINT64_MAX) ||
        isDynamicEventHandle(kind, stable) ||
        decodeAutomaticHandle(stable, id, offset))
      return false;
    int64_t begin = 0, start = kInvalidHandleStart, end = 0;
    uint64_t base = 0;
    if (decodeStaticHandle(stable, id, offset)) {
      if (!context || kind > TALOS_RT_DESCRIPTOR_DRIVER)
        return false;
      {
        // Registration rejects ID zero, so it never names a cached state.
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        if (op.staticID != id || id == 0) {
          auto found = context->nativeStaticStates.find(id);
          if (found == context->nativeStaticStates.end())
            return false;
          op.staticID = id;
          op.staticOffset = found->second.bitOffset;
          op.staticWidth = found->second.bitWidth;
        }
      }
      if (width <= 0 || op.staticWidth > uint64_t{INT64_MAX} ||
          offset > INT64_MAX - width)
        return false;
      start = offset;
      int64_t available = static_cast<int64_t>(op.staticWidth);
      int64_t requestedEnd = start + width;
      if (requestedEnd <= 0) {
        begin = end = 0;
      } else if (start >= available) {
        begin = end = available;
      } else {
        begin = std::max<int64_t>(0, start);
        end = std::min<int64_t>(available, requestedEnd);
      }
      base = encodeStaticHandle(id, begin);
      if (base == UINT64_MAX)
        return false;
    } else if (decodeGlobalHandle(stable, start)) {
      if (kind <= TALOS_RT_DESCRIPTOR_DRIVER) {
        int64_t available =
            context && context->execution &&
                    context->execution->state_bit_count <= uint64_t{INT64_MAX}
                ? static_cast<int64_t>(context->execution->state_bit_count)
                : 0;
        if (width <= 0 || start > INT64_MAX - width) {
          start = kInvalidHandleStart;
        } else {
          begin = std::max<int64_t>(0, start);
          end = std::min<int64_t>(available, start + width);
          if (begin > end)
            begin = end;
        }
      } else {
        begin = start;
        end = start == INT64_MAX ? start : start + 1;
      }
      std::memcpy(&base, &begin, 8);
    } else {
      return false;
    }
    uint8_t *address = data + op.destination;
    std::memset(address, 0, 32);
    std::memcpy(address, &kind, 4);
    std::memcpy(address + 8, &base, 8);
    std::memcpy(address + 16, &start, 8);
    std::memcpy(address + 24, &end, 8);
    break;
  }
  case kFastStoreFrameHandle: {
    if (!canonicalFrame || op.immediate > canonicalFrameSize ||
        8 > canonicalFrameSize - op.immediate)
      return false;
    uint64_t stable = UINT64_MAX;
    if (!encodeCanonicalHandle(data + op.source0, stable))
      return false;
    std::memcpy(canonicalFrame + op.immediate, &stable, 8);
    break;
  }
  case kFastHandleID: {
    FastHandle handle = fastHandle(data, op.source0);
    uint32_t descriptorKind =
        handle.kind & ~(kLocalHandleKind | kAutomaticHandleKind);
    bool nullEvent =
        handle.kind == TALOS_RT_DESCRIPTOR_EVENT && handle.start == -1;
    if (handle.start == kInvalidHandleStart)
      return false;
    uint64_t raw = nullEvent ? UINT64_MAX : static_cast<uint64_t>(handle.start);
    bool dynamicEvent = isDynamicEventHandle(descriptorKind, raw);
    uint64_t stable = nullEvent      ? UINT64_MAX
                      : dynamicEvent ? raw
                                     : encodeGlobalHandle(handle.start);
    uint32_t id = 0;
    int64_t begin = 0;
    if (!nullEvent && !dynamicEvent &&
        (handle.kind & kAutomaticHandleKind) != 0) {
      if (!decodeAutomaticHandle(handle.base, id, begin))
        return false;
      stable = encodeAutomaticHandle(id, handle.start);
    } else if (!nullEvent && !dynamicEvent &&
               decodeStaticHandle(handle.base, id, begin)) {
      stable = encodeStaticHandle(id, handle.start);
    }
    if (stable == UINT64_MAX && !nullEvent)
      return false;
    fastWrite(data, op, stable, 0);
    break;
  }
  case kFastLoadState: {
    FastHandle handle = fastHandle(data, op.source0);
    if (!context || handle.kind < TALOS_RT_DESCRIPTOR_STORAGE ||
        handle.kind > TALOS_RT_DESCRIPTOR_DRIVER)
      return false;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint64_t storageBit = 0, published = 0;
    // Clock-condition publications overlay canonical reads per bit.
    if ((context->observerForcesCanonicalPlane &&
         context->conditionPublication) ||
        !fastCanonicalView(op, context, handle, op.width, storageBit,
                           published))
      return false;
    fastWrite(data, op,
              loadPackedBits(context->stateValue, storageBit, op.width),
              loadPackedBits(context->stateUnknown, storageBit, op.width));
    break;
  }
  case kFastStoreState: {
    FastHandle handle = fastHandle(data, op.source0);
    if (!context || handle.kind != TALOS_RT_DESCRIPTOR_STORAGE ||
        context->activeExecRegion == TALOS_RT_REGION_POSTPONED)
      return false;
    uint64_t newValue, newUnknown;
    fastRead(data, op.source1, source1FourState, op.width, newValue,
             newUnknown);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint64_t at = 0, published = 0;
    if (talos_rt_has_conditional_signal_waiters(context) ||
        !fastCanonicalView(op, context, handle, op.width, at, published))
      return false;
    // A force or procedural assign owning any stored bit takes the general
    // path, which skips those bits.
    for (const std::vector<uint64_t> *mask :
         {&context->forceMask, &context->assignMask})
      if (!mask->empty() && (mask->size() * 64 < at + op.width ||
                             loadPackedBits(*mask, at, op.width) != 0))
        return false;
    uint64_t oldValue = loadPackedBits(context->stateValue, at, op.width);
    uint64_t oldUnknown = loadPackedBits(context->stateUnknown, at, op.width);
    if (op.flags & TALOS_RT_DB_STORE_STATE_CONTINUOUS) {
      if (context->continuousMask.empty()) {
        size_t limbs = context->stateValue.size();
        context->continuousMask.assign(limbs, 0);
        context->continuousValue.assign(limbs, 0);
        if (context->twoState())
          context->continuousUnknown.assignZero(limbs);
        else
          context->continuousUnknown.assign(limbs, 0);
      }
      storePackedBits(context->continuousValue, at, op.width, newValue);
      storePackedBits(context->continuousUnknown, at, op.width, newUnknown);
      storePackedBits(context->continuousMask, at, op.width, UINT64_MAX);
    }
    storePackedBits(context->stateValue, at, op.width, newValue);
    storePackedBits(context->stateUnknown, at, op.width, newUnknown);
    talos_rt_sync_native_state_range_unlocked(context, at, op.width);
    uint64_t differs = (oldValue ^ newValue) | (oldUnknown ^ newUnknown);
    if (differs != 0) {
      // transitionEdges, a word at a time.
      uint64_t oldZero = ~oldUnknown & ~oldValue;
      uint64_t oldOne = ~oldUnknown & oldValue;
      uint64_t newZero = ~newUnknown & ~newValue;
      uint64_t newOne = ~newUnknown & newValue;
      PackedSignalTransitionBuffer edges(op.width);
      storePackedBytes(edges.changed(), 0, op.width, differs);
      storePackedBytes(edges.posedge(), 0, op.width,
                       differs &
                           ((oldZero & ~newZero) | (oldUnknown & newOne)));
      storePackedBytes(edges.negedge(), 0, op.width,
                       differs & ((oldOne & ~newOne) | (oldUnknown & newZero)));
      uint64_t sequence = 0;
      if (!talos_rt_publish_signal_transition_batch_unlocked(
              context, published, op.width, edges.changed(), edges.posedge(),
              edges.negedge(), 0, &sequence)) {
        status = context->schedulerStatus;
        return true;
      }
      talos_rt_invalidate_signal_snapshots_unlocked(context, published,
                                                      op.width);
      if (!talos_rt_notify_observer_signal_unlocked(context, published,
                                                      op.width)) {
        status = context->schedulerStatus;
        return true;
      }
      if (++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
    }
    if (op.flags & TALOS_RT_DB_STORE_STATE_CHANGED) {
      FastOp changedOp = op;
      changedOp.width = op.sourceWidth;
      bool reported =
          differs != 0 &&
          (op.flags & TALOS_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION) == 0;
      fastWrite(data, changedOp, reported, 0);
    }
    break;
  }
  default:
    return false;
  }
  ++pc;
  return true;
}

/// The fast-path plan of `frame`'s function, decoded on its first execution,
/// or null when the image is not the context's validated image.
const FastOp *fastPlan(const Image &image, const Frame &frame,
                       talos_rt_context *context) {
  if (!context || !context->designBytecodeImageValidated ||
      context->designBytecodeImage.data != image.data ||
      frame.function.instructionCount == 0 ||
      frame.functionIndex >= image.functionCount)
    return nullptr;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto &plans = context->designBytecodePlans;
  if (plans.size() != image.functionCount)
    plans.assign(static_cast<size_t>(image.functionCount), {});
  std::vector<FastOp> &plan = plans[frame.functionIndex];
  if (plan.empty()) {
    plan.reserve(static_cast<size_t>(frame.function.instructionCount));
    for (uint64_t index = 0; index != frame.function.instructionCount; ++index)
      plan.push_back(decodeFastOp(
          image, frame.function,
          instructionAt(image, frame.function.firstInstruction + index)));
  }
  return plan.data();
}

std::optional<uint64_t> continuationPC(const Image &image,
                                       const Function &function,
                                       uint32_t continuation);

talos_rt_status
executeFunction(const Image &image, Frame &frame, talos_rt_context *context,
                uint8_t *canonicalFrame, uint64_t canonicalFrameSize,
                uint64_t startPC, StepBudget &budget,
                talos_rt_fragment_action_v1 *action, ExecutionState &state,
                std::unique_ptr<PendingDesignActivation> *pendingActivation,
                uint64_t returnFirst = 0, uint64_t returnCount = 0,
                Frame *caller = nullptr) {
  ScopedBytecodeFrameRoots managedRoots(image, frame, context);
  if (managedRoots.getStatus() != TALOS_RT_OK)
    return managedRoots.getStatus();
  uint64_t begin = frame.function.firstInstruction;
  uint64_t end = begin + frame.function.instructionCount;
  uint64_t pc = startPC;
  const FastOp *plan = fastPlan(image, frame, context);
  while (pc >= begin && pc < end) {
    if (!budget.consume())
      return TALOS_RT_STEP_LIMIT;
    talos_rt_status fastStatus = TALOS_RT_OK;
    if (plan && executeFastOp(plan[pc - begin], frame.data, canonicalFrame,
                              canonicalFrameSize, context, pc, fastStatus)) {
      if (fastStatus != TALOS_RT_OK)
        return fastStatus;
      continue;
    }
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
    case TALOS_RT_DB_NOP:
      break;
    case TALOS_RT_DB_CONSTANT: {
      Layout destination = layout(instruction.destination);
      copyRegisterBytes(frame.data + destination.offset,
                        image.data + image.constants + instruction.immediate,
                        destination.size);
      if ((destination.kind == TALOS_RT_DBREG_BITS ||
           destination.kind == TALOS_RT_DBREG_LOGIC) &&
          destination.width % 64 != 0) {
        uint64_t limbs = limbCount(destination.width);
        uint64_t last = 0;
        uint64_t lastOffset = destination.offset + (limbs - 1) * 8;
        std::memcpy(&last, frame.data + lastOffset, sizeof(last));
        last &= finalMask(destination.width);
        std::memcpy(frame.data + lastOffset, &last, sizeof(last));
        if (destination.kind == TALOS_RT_DBREG_LOGIC) {
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
    case TALOS_RT_DB_MOVE:
      if (!copyRegister(image, frame, instruction.source0, frame,
                        instruction.destination))
        return TALOS_RT_INVALID_BYTECODE;
      break;
    case TALOS_RT_DB_BITCAST: {
      Layout source = layout(instruction.source0);
      Layout destination = layout(instruction.destination);
      if (!bitcastCompatible(destination, source))
        return TALOS_RT_INVALID_BYTECODE;
      if (source.kind == TALOS_RT_DBREG_MANAGED) {
        talos_rt_managed_word_v1 word = 0;
        std::memcpy(&word, frame.data + source.offset, sizeof(word));
        auto *object = talos_rt_object_from_managed_word(word);
        BuiltinClassKind expected =
            destination.flags == TALOS_RT_DBREG_MAILBOX
                ? BuiltinClassKind::Mailbox
                : BuiltinClassKind::Semaphore;
        if (word != talos_rt_managed_word_from_object(object) ||
            (object &&
             (!talos_rt_managed_object_belongs_to(context, object) ||
              talos_rt_managed_object_kind(object) !=
                  TALOS_RT_MANAGED_CLASS ||
              talos_rt_builtin_class_kind_unlocked(object) != expected)))
          return TALOS_RT_INVALID_BYTECODE;
        std::memcpy(frame.data + destination.offset, &word, sizeof(word));
        break;
      }
      std::memset(frame.data + destination.offset, 0, destination.size);
      std::memcpy(frame.data + destination.offset, frame.data + source.offset,
                  std::min(source.size, destination.size));
      break;
    }
    case TALOS_RT_DB_NOT: {
      Logic input = read(instruction.source0);
      for (size_t index = 0; index != input.value.size(); ++index)
        input.value[index] = ~input.value[index] & ~input.unknown[index];
      mask(input);
      write(instruction.destination, input);
      break;
    }
    case TALOS_RT_DB_POPCOUNT: {
      Logic input = read(instruction.source0);
      uint64_t count = 0;
      for (uint64_t limb : input.value)
        count += static_cast<uint64_t>(__builtin_popcountll(limb));
      Logic result{input.width, false, LimbVector(input.value.size()),
                   LimbVector(input.value.size())};
      result.value[0] = count;
      mask(result);
      write(instruction.destination, result);
      break;
    }
    case TALOS_RT_DB_REDUCE: {
      Logic input = read(instruction.source0);
      Layout destination = layout(instruction.destination);
      Logic result{1, destination.kind == TALOS_RT_DBREG_LOGIC, {0}, {0}};
      bool anyKnownOne = false, anyKnownZero = false, unknown = false;
      bool parity = false;
      for (size_t index = 0; index != input.value.size(); ++index) {
        uint64_t valid = index + 1 == input.value.size()
                             ? finalMask(input.width)
                             : UINT64_MAX;
        uint64_t u = input.unknown[index] & valid;
        uint64_t v = input.value[index] & valid;
        unknown |= u != 0;
        anyKnownOne |= (v & ~u) != 0;
        anyKnownZero |= (~v & ~u & valid) != 0;
        parity ^= (__builtin_popcountll(v & ~u) & 1) != 0;
      }
      bool value = false, resultUnknown = false;
      switch (instruction.flags) {
      case TALOS_RT_DB_REDUCE_AND:
        value = !anyKnownZero && !unknown;
        resultUnknown = !anyKnownZero && unknown;
        break;
      case TALOS_RT_DB_REDUCE_OR:
        value = anyKnownOne;
        resultUnknown = !anyKnownOne && unknown;
        break;
      case TALOS_RT_DB_REDUCE_XOR:
        value = parity && !unknown;
        resultUnknown = unknown;
        break;
      case TALOS_RT_DB_REDUCE_NAND:
        value = anyKnownZero;
        resultUnknown = !anyKnownZero && unknown;
        break;
      case TALOS_RT_DB_REDUCE_NOR:
        value = !anyKnownOne && !unknown;
        resultUnknown = !anyKnownOne && unknown;
        break;
      case TALOS_RT_DB_REDUCE_XNOR:
        value = !parity && !unknown;
        resultUnknown = unknown;
        break;
      case TALOS_RT_DB_REDUCE_IS_TRUE:
        value = anyKnownOne;
        resultUnknown = false;
        break;
      case TALOS_RT_DB_REDUCE_LOGICAL_NOT:
        value = !anyKnownOne && !unknown;
        resultUnknown = !anyKnownOne && unknown;
        break;
      case TALOS_RT_DB_REDUCE_LOGICAL_VALUE:
        value = anyKnownOne;
        resultUnknown = !anyKnownOne && unknown;
        break;
      default:
        return TALOS_RT_INVALID_BYTECODE;
      }
      result.value[0] = value;
      result.unknown[0] = resultUnknown;
      write(instruction.destination, result);
      break;
    }
    case TALOS_RT_DB_AND:
    case TALOS_RT_DB_OR:
    case TALOS_RT_DB_XOR: {
      uint64_t left = 0, right = 0;
      if (readKnownScalar(image, frame, instruction.source0, left) &&
          readKnownScalar(image, frame, instruction.source1, right)) {
        uint64_t result = instruction.opcode == TALOS_RT_DB_AND ? left & right
                          : instruction.opcode == TALOS_RT_DB_OR
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
    case TALOS_RT_DB_ADD:
    case TALOS_RT_DB_SUB: {
      uint64_t left = 0, right = 0;
      if (readKnownScalar(image, frame, instruction.source0, left) &&
          readKnownScalar(image, frame, instruction.source1, right) &&
          writeKnownScalar(image, frame, instruction.destination,
                           instruction.opcode == TALOS_RT_DB_SUB
                               ? left - right
                               : left + right))
        break;
      write(instruction.destination,
            add(read(instruction.source0), read(instruction.source1),
                instruction.opcode == TALOS_RT_DB_SUB));
      break;
    }
    case TALOS_RT_DB_MUL: {
      uint64_t left = 0, right = 0;
      if (readKnownScalar(image, frame, instruction.source0, left) &&
          readKnownScalar(image, frame, instruction.source1, right) &&
          writeKnownScalar(image, frame, instruction.destination, left * right))
        break;
      write(instruction.destination,
            multiply(read(instruction.source0), read(instruction.source1)));
      break;
    }
    case TALOS_RT_DB_POWER:
      write(instruction.destination,
            power(read(instruction.source0), read(instruction.source1)));
      break;
    case TALOS_RT_DB_REPLICATE: {
      Layout destination = layout(instruction.destination);
      write(instruction.destination,
            replicate(read(instruction.source0), destination.width,
                      instruction.immediate));
      break;
    }
    case TALOS_RT_DB_FADD:
    case TALOS_RT_DB_FSUB:
    case TALOS_RT_DB_FMUL:
    case TALOS_RT_DB_FDIV:
    case TALOS_RT_DB_FPOW: {
      Layout destination = layout(instruction.destination);
      if (destination.kind == TALOS_RT_DBREG_REAL32) {
        float lhs = readFloat(instruction.source0);
        float rhs = readFloat(instruction.source1);
        float result = 0.0f;
        switch (instruction.opcode) {
        case TALOS_RT_DB_FADD:
          result = lhs + rhs;
          break;
        case TALOS_RT_DB_FSUB:
          result = lhs - rhs;
          break;
        case TALOS_RT_DB_FMUL:
          result = lhs * rhs;
          break;
        case TALOS_RT_DB_FDIV:
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
        case TALOS_RT_DB_FADD:
          result = lhs + rhs;
          break;
        case TALOS_RT_DB_FSUB:
          result = lhs - rhs;
          break;
        case TALOS_RT_DB_FMUL:
          result = lhs * rhs;
          break;
        case TALOS_RT_DB_FDIV:
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
    case TALOS_RT_DB_FNEG: {
      Layout destination = layout(instruction.destination);
      if (destination.kind == TALOS_RT_DBREG_REAL32)
        writeFloat(instruction.destination, -readFloat(instruction.source0));
      else
        writeDouble(instruction.destination, -readDouble(instruction.source0));
      break;
    }
    case TALOS_RT_DB_FCOMPARE: {
      Layout source = layout(instruction.source0);
      bool result = false;
      auto compare = [&](auto lhs, auto rhs) {
        switch (instruction.flags) {
        case TALOS_RT_DB_FCMP_EQ:
          return lhs == rhs;
        case TALOS_RT_DB_FCMP_NE:
          return lhs != rhs;
        case TALOS_RT_DB_FCMP_LT:
          return lhs < rhs;
        case TALOS_RT_DB_FCMP_LE:
          return lhs <= rhs;
        case TALOS_RT_DB_FCMP_GT:
          return lhs > rhs;
        case TALOS_RT_DB_FCMP_GE:
          return lhs >= rhs;
        default:
          return false;
        }
      };
      if (source.kind == TALOS_RT_DBREG_REAL32)
        result = compare(readFloat(instruction.source0),
                         readFloat(instruction.source1));
      else
        result = compare(readDouble(instruction.source0),
                         readDouble(instruction.source1));
      Logic encoded{1, false, {result ? uint64_t{1} : uint64_t{0}}, {0}};
      write(instruction.destination, encoded);
      break;
    }
    case TALOS_RT_DB_FEXT:
      writeDouble(instruction.destination,
                  static_cast<double>(readFloat(instruction.source0)));
      break;
    case TALOS_RT_DB_FTRUNC:
      writeFloat(instruction.destination,
                 static_cast<float>(readDouble(instruction.source0)));
      break;
    case TALOS_RT_DB_UDIV:
    case TALOS_RT_DB_SDIV:
    case TALOS_RT_DB_UREM:
    case TALOS_RT_DB_SREM: {
      bool signedDivision = instruction.opcode == TALOS_RT_DB_SDIV ||
                            instruction.opcode == TALOS_RT_DB_SREM;
      auto result = divide(read(instruction.source0), read(instruction.source1),
                           signedDivision);
      write(instruction.destination,
            instruction.opcode == TALOS_RT_DB_UREM ||
                    instruction.opcode == TALOS_RT_DB_SREM
                ? result.second
                : result.first);
      break;
    }
    case TALOS_RT_DB_SHL:
    case TALOS_RT_DB_LSHR:
    case TALOS_RT_DB_ASHR: {
      uint64_t input = 0, amount = 0;
      Layout inputLayout = layout(instruction.source0);
      if (readKnownScalar(image, frame, instruction.source0, input) &&
          readKnownScalar(image, frame, instruction.source1, amount)) {
        uint64_t result = 0;
        if (instruction.opcode == TALOS_RT_DB_SHL) {
          result = amount < inputLayout.width ? input << amount : 0;
        } else if (instruction.opcode == TALOS_RT_DB_LSHR) {
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
    case TALOS_RT_DB_COMPARE: {
      Logic left = read(instruction.source0), right = read(instruction.source1);
      bool deterministic = instruction.flags == TALOS_RT_DB_CMP_CASE_EQ ||
                           instruction.flags == TALOS_RT_DB_CMP_CASE_NE ||
                           instruction.flags == TALOS_RT_DB_CMP_CASEZ_EQ ||
                           instruction.flags == TALOS_RT_DB_CMP_CASEXZ_EQ;
      bool wildcardEquality = instruction.flags == TALOS_RT_DB_CMP_WILD_EQ ||
                              instruction.flags == TALOS_RT_DB_CMP_WILD_NE;
      Logic result{1,
                   layout(instruction.destination).kind ==
                       TALOS_RT_DBREG_LOGIC,
                   {0},
                   {0}};
      if (!deterministic && !wildcardEquality &&
          (anyUnknown(left) || anyUnknown(right))) {
        bool logicalEquality = instruction.flags == TALOS_RT_DB_CMP_EQ ||
                               instruction.flags == TALOS_RT_DB_CMP_NE;
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
          result.value[0] = instruction.flags == TALOS_RT_DB_CMP_NE;
        else
          result = allX(1, result.fourState);
      } else {
        int compared = compareUnsigned(left.value, right.value);
        bool signedOperands = instruction.flags >= TALOS_RT_DB_CMP_SLT &&
                              instruction.flags <= TALOS_RT_DB_CMP_SGE;
        if (signedOperands) {
          bool ls = bit(left.value, left.width - 1);
          bool rs = bit(right.value, right.width - 1);
          if (ls != rs)
            compared = ls ? -1 : 1;
        }
        bool value = false;
        switch (instruction.flags) {
        case TALOS_RT_DB_CMP_EQ:
          value = compared == 0;
          break;
        case TALOS_RT_DB_CMP_NE:
          value = compared != 0;
          break;
        case TALOS_RT_DB_CMP_ULT:
        case TALOS_RT_DB_CMP_SLT:
          value = compared < 0;
          break;
        case TALOS_RT_DB_CMP_ULE:
        case TALOS_RT_DB_CMP_SLE:
          value = compared <= 0;
          break;
        case TALOS_RT_DB_CMP_UGT:
        case TALOS_RT_DB_CMP_SGT:
          value = compared > 0;
          break;
        case TALOS_RT_DB_CMP_UGE:
        case TALOS_RT_DB_CMP_SGE:
          value = compared >= 0;
          break;
        case TALOS_RT_DB_CMP_CASE_EQ:
        case TALOS_RT_DB_CMP_CASE_NE: {
          value = left.value == right.value && left.unknown == right.unknown;
          if (instruction.flags == TALOS_RT_DB_CMP_CASE_NE)
            value = !value;
          break;
        }
        case TALOS_RT_DB_CMP_CASEZ_EQ:
        case TALOS_RT_DB_CMP_CASEXZ_EQ: {
          if (right.width != left.width)
            return TALOS_RT_INVALID_BYTECODE;
          bool equal = true;
          for (size_t index = 0; equal && index != left.value.size(); ++index) {
            uint64_t lu = left.unknown[index], ru = right.unknown[index];
            uint64_t lv = left.value[index], rv = right.value[index];
            uint64_t wildcard = instruction.flags == TALOS_RT_DB_CMP_CASEZ_EQ
                                    ? (lu & lv) | (ru & rv)
                                    : lu | ru;
            equal = (~wildcard & ((lu ^ ru) | (lv ^ rv))) == 0;
          }
          value = equal;
          break;
        }
        case TALOS_RT_DB_CMP_WILD_EQ:
        case TALOS_RT_DB_CMP_WILD_NE: {
          if (right.width != left.width)
            return TALOS_RT_INVALID_BYTECODE;
          bool knownMismatch = false;
          bool relevantUnknown = false;
          // X/Z bits of the right operand are wildcards.
          for (size_t index = 0; index != left.value.size(); ++index) {
            uint64_t compared = ~right.unknown[index];
            relevantUnknown |= (left.unknown[index] & compared) != 0;
            knownMismatch |= ((left.value[index] ^ right.value[index]) &
                              ~left.unknown[index] & compared) != 0;
          }
          if (!knownMismatch && relevantUnknown) {
            result = allX(1, result.fourState);
            break;
          }
          value = !knownMismatch;
          if (instruction.flags == TALOS_RT_DB_CMP_WILD_NE)
            value = !value;
          break;
        }
        default:
          return TALOS_RT_INVALID_BYTECODE;
        }
        result.value[0] = value;
      }
      write(instruction.destination, result);
      break;
    }
    case TALOS_RT_DB_SELECT: {
      Logic condition = read(instruction.source2);
      if (anyUnknown(condition)) {
        if (instruction.flags == TALOS_RT_DB_SELECT_FOUR_STATE) {
          Logic left = read(instruction.source0);
          Logic right = read(instruction.source1);
          Logic result{left.width, true, LimbVector(limbCount(left.width)),
                       LimbVector(limbCount(left.width))};
          if (right.width != left.width)
            return TALOS_RT_INVALID_BYTECODE;
          for (size_t index = 0; index != result.value.size(); ++index) {
            uint64_t mismatch = left.unknown[index] | right.unknown[index] |
                                (left.value[index] ^ right.value[index]);
            result.value[index] = ~mismatch & left.value[index];
            result.unknown[index] = mismatch;
          }
          mask(result);
          write(instruction.destination, result);
        } else {
          write(instruction.destination,
                allX(layout(instruction.destination).width,
                     layout(instruction.destination).kind ==
                         TALOS_RT_DBREG_LOGIC));
        }
      } else if (!copyRegister(image, frame,
                               isZero(condition) ? instruction.source1
                                                 : instruction.source0,
                               frame, instruction.destination))
        return TALOS_RT_INVALID_BYTECODE;
      break;
    }
    case TALOS_RT_DB_EXTRACT: {
      Logic input = read(instruction.source0);
      Layout destination = layout(instruction.destination);
      Logic result{destination.width,
                   destination.kind == TALOS_RT_DBREG_LOGIC,
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
      if (instruction.flags == TALOS_RT_DB_AGGREGATE_MANAGED &&
          (destination.kind == TALOS_RT_DBREG_MANAGED ||
           destination.kind == TALOS_RT_DBREG_STRING) &&
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
      // Result bits [first, first + count) come from input bits [source,
      // source + count); every other result bit is out of range.
      uint64_t width = destination.width;
      uint64_t first = 0, source = 0, count = 0;
      if (negative) {
        first = std::min<uint64_t>(negativeMagnitude, width);
        count = std::min<uint64_t>(width - first, input.width);
      } else if (low < input.width) {
        source = low;
        count = std::min<uint64_t>(width, input.width - low);
      }
      // Static resize operations use source1 == invalid and may sign extend.
      // Dynamic selections instead pad every out-of-range bit with X (or zero
      // for a two-state result).
      bool signExtend = instruction.source1 == kInvalidRegister &&
                        (instruction.flags & 1) != 0 && input.width != 0;
      bool padValue = signExtend && bit(input.value, input.width - 1);
      bool padUnknown = signExtend ? bit(input.unknown, input.width - 1)
                                   : (instruction.source1 != kInvalidRegister &&
                                      result.fourState);
      fillBits(result.value, 0, first, padValue);
      fillBits(result.unknown, 0, first, padUnknown);
      copyBits(result.value, first, input.value, source, count);
      copyBits(result.unknown, first, input.unknown, source, count);
      fillBits(result.value, first + count, width - first - count, padValue);
      fillBits(result.unknown, first + count, width - first - count,
               padUnknown);
      write(instruction.destination, result);
      break;
    }
    case TALOS_RT_DB_MAKE_HANDLE: {
      Layout destination = layout(instruction.destination);
      if (destination.kind != TALOS_RT_DBREG_HANDLE)
        return TALOS_RT_INVALID_BYTECODE;
      uint8_t *address = frame.data + destination.offset;
      std::memset(address, 0, destination.size);
      uint32_t kind = instruction.source0;
      uint64_t width = instruction.source1;
      // The compiler-reserved Preponed event is deliberately not a decodable
      // state handle. Admit it only when constructing an exact zero-width
      // event descriptor; every storage/net/driver path below retains the
      // ordinary stable-state validation.
      bool preponedEvent =
          kind == TALOS_RT_DESCRIPTOR_EVENT && width == 0 &&
          talos_rt_stable_handle_is_preponed_event(instruction.immediate);
      talos_rt_stable_handle_v1 decoded;
      if (!preponedEvent &&
          (!talos_rt_stable_handle_decode(instruction.immediate, &decoded) ||
           decoded.kind == TALOS_RT_STABLE_HANDLE_AUTOMATIC ||
           decoded.offset < 0 ||
           width > uint64_t{INT64_MAX} - static_cast<uint64_t>(decoded.offset)))
        return TALOS_RT_INVALID_HANDLE;
      int64_t begin = preponedEvent
                          ? static_cast<int64_t>(instruction.immediate)
                          : decoded.offset;
      int64_t end = begin + static_cast<int64_t>(width);
      uint64_t base = static_cast<uint64_t>(begin);
      if (!preponedEvent && decoded.kind == TALOS_RT_STABLE_HANDLE_STATIC) {
        if (!context || kind > TALOS_RT_DESCRIPTOR_DRIVER)
          return TALOS_RT_INVALID_HANDLE;
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        auto state = context->nativeStaticStates.find(decoded.id);
        if (state == context->nativeStaticStates.end() ||
            static_cast<uint64_t>(begin) > state->second.bitWidth ||
            width > state->second.bitWidth - static_cast<uint64_t>(begin))
          return TALOS_RT_INVALID_HANDLE;
        base = encodeStaticHandle(decoded.id, 0);
        if (base == UINT64_MAX)
          return TALOS_RT_INVALID_HANDLE;
      } else if (!preponedEvent && context &&
                 kind <= TALOS_RT_DESCRIPTOR_DRIVER) {
        // Bytecode encodes canonical plane offsets so a process can execute
        // directly without scheduler-main registration. Once native static
        // state is registered, use its stable identity so mixed-tier waits and
        // publications name the same object.
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        uint64_t canonical = talos_rt_canonical_state_handle_unlocked(
            context, static_cast<uint64_t>(begin), width);
        uint32_t id = 0;
        int64_t offset = 0;
        if (decodeStaticHandle(canonical, id, offset) && offset == 0) {
          auto state = context->nativeStaticStates.find(id);
          if (state == context->nativeStaticStates.end() ||
              state->second.bitWidth != width)
            return TALOS_RT_INVALID_HANDLE;
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
    case TALOS_RT_DB_MAKE_LOCAL_HANDLE: {
      Layout destination = layout(instruction.destination);
      Layout storage = layout(instruction.source0);
      if (destination.kind != TALOS_RT_DBREG_HANDLE ||
          (storage.kind != TALOS_RT_DBREG_BITS &&
           storage.kind != TALOS_RT_DBREG_LOGIC &&
           storage.kind != TALOS_RT_DBREG_REAL32 &&
           storage.kind != TALOS_RT_DBREG_REAL64) ||
          frame.id == 0 || frame.id > UINT32_C(0x7fff) ||
          instruction.source0 > UINT16_MAX)
        return TALOS_RT_OUT_OF_RESOURCES;
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
    case TALOS_RT_DB_HANDLE_OFFSET: {
      Layout source = layout(instruction.source0);
      Layout destination = layout(instruction.destination);
      if (source.kind != TALOS_RT_DBREG_HANDLE ||
          destination.kind != TALOS_RT_DBREG_HANDLE)
        return TALOS_RT_INVALID_BYTECODE;
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
    case TALOS_RT_DB_HANDLE_ID: {
      Layout handle = layout(instruction.source0);
      uint32_t kind = 0;
      int64_t start = kInvalidHandleStart;
      std::memcpy(&kind, frame.data + handle.offset, 4);
      std::memcpy(&start, frame.data + handle.offset + 16, 8);
      uint32_t descriptorKind =
          kind & ~(kLocalHandleKind | kAutomaticHandleKind);
      bool nullEvent = kind == TALOS_RT_DESCRIPTOR_EVENT && start == -1;
      if (start == kInvalidHandleStart)
        return TALOS_RT_INVALID_HANDLE;
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
          return TALOS_RT_INVALID_HANDLE;
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
        return TALOS_RT_INVALID_HANDLE;
      Logic value{64, false, {stable}, {0}};
      write(instruction.destination, value);
      break;
    }
    case TALOS_RT_DB_CONCAT: {
      Logic left = read(instruction.source0), right = read(instruction.source1);
      Layout destination = layout(instruction.destination);
      Logic result{destination.width,
                   destination.kind == TALOS_RT_DBREG_LOGIC,
                   LimbVector(limbCount(destination.width)),
                   LimbVector(limbCount(destination.width))};
      if (uint64_t{left.width} + right.width != destination.width)
        return TALOS_RT_INVALID_BYTECODE;
      copyBits(result.value, 0, right.value, 0, right.width);
      copyBits(result.unknown, 0, right.unknown, 0, right.width);
      copyBits(result.value, right.width, left.value, 0, left.width);
      copyBits(result.unknown, right.width, left.unknown, 0, left.width);
      write(instruction.destination, result);
      break;
    }
    case TALOS_RT_DB_INSERT: {
      Logic base = read(instruction.source0),
            inserted = read(instruction.source1);
      bool negative = false;
      uint64_t low = instruction.immediate;
      uint64_t negativeMagnitude = 0;
      if ((instruction.flags & TALOS_RT_DB_INSERT_DYNAMIC) != 0) {
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
      // Inserted bits [source, source + count) replace base bits
      // [first, first + count); bits falling outside the base are dropped.
      uint64_t first = 0, source = 0, count = 0;
      if (negative) {
        if (negativeMagnitude < inserted.width) {
          source = negativeMagnitude;
          count = std::min<uint64_t>(inserted.width - negativeMagnitude,
                                     base.width);
        }
      } else if (low < base.width) {
        first = low;
        count = std::min<uint64_t>(inserted.width, base.width - low);
      }
      copyBits(base.value, first, inserted.value, source, count);
      copyBits(base.unknown, first, inserted.unknown, source, count);
      write(instruction.destination, base);
      break;
    }
    case TALOS_RT_DB_LOAD_FRAME:
    case TALOS_RT_DB_STORE_FRAME: {
      uint32_t reg = instruction.opcode == TALOS_RT_DB_LOAD_FRAME
                         ? instruction.destination
                         : instruction.source0;
      Layout value = layout(reg);
      uint64_t transferSize = value.kind == TALOS_RT_DBREG_HANDLE ? 8
                              : instruction.auxiliary != 0
                                  ? instruction.auxiliary
                                  : value.size;
      if (!canonicalFrame || instruction.immediate > canonicalFrameSize ||
          transferSize > canonicalFrameSize - instruction.immediate)
        return TALOS_RT_INVALID_FRAME;
      if (value.kind != TALOS_RT_DBREG_HANDLE) {
        // A four-state register keeps its value and unknown planes one limb
        // stride apart, while the canonical frame packs the two planes at the
        // target ABI size of the value type. Transfer each plane on its own
        // instead of copying one contiguous run: for every logic value
        // narrower than a limb the planes do not line up, and a single copy
        // silently drops the unknown plane, turning x and z into 0 across a
        // suspension.
        uint64_t registerPlane = value.kind == TALOS_RT_DBREG_LOGIC
                                     ? limbCount(value.width) * sizeof(uint64_t)
                                     : 0;
        uint64_t framePlane =
            registerPlane != 0 ? transferSize / 2 : transferSize;
        uint64_t unknownDisplacement =
            instruction.source1 != 0 ? instruction.source1 : framePlane;
        if (registerPlane != 0 &&
            (transferSize % 2 != 0 || framePlane > registerPlane))
          return TALOS_RT_INVALID_FRAME;
        if (instruction.opcode == TALOS_RT_DB_LOAD_FRAME) {
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
      if (instruction.opcode == TALOS_RT_DB_LOAD_FRAME) {
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
            kind == TALOS_RT_DESCRIPTOR_EVENT && stable == UINT64_MAX;
        bool dynamicEvent = isDynamicEventHandle(kind, stable);
        if (nullEvent) {
          start = end = -1;
        } else if (dynamicEvent) {
          start = static_cast<int64_t>(stable);
          begin = start;
          end = start == INT64_MAX ? start : start + 1;
        } else if (decodeAutomaticHandle(stable, automaticID,
                                         automaticOffset)) {
          if (!context || kind != TALOS_RT_DESCRIPTOR_STORAGE)
            return TALOS_RT_INVALID_HANDLE;
          std::lock_guard<std::recursive_mutex> lock(context->mutex);
          auto found = context->nativeAutomaticStates.find(automaticID);
          int64_t width = static_cast<int64_t>(instruction.auxiliary);
          if (found == context->nativeAutomaticStates.end() || width <= 0 ||
              found->second.bitWidth > uint64_t{INT64_MAX} ||
              automaticOffset > INT64_MAX - width)
            return TALOS_RT_INVALID_HANDLE;
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
            return TALOS_RT_INVALID_HANDLE;
          std::memcpy(address + 8, &base, 8);
        } else if (decodeStaticHandle(stable, automaticID, automaticOffset)) {
          if (!context || kind > TALOS_RT_DESCRIPTOR_DRIVER)
            return TALOS_RT_INVALID_HANDLE;
          std::lock_guard<std::recursive_mutex> lock(context->mutex);
          auto found = context->nativeStaticStates.find(automaticID);
          int64_t width = static_cast<int64_t>(instruction.auxiliary);
          if (found == context->nativeStaticStates.end() || width <= 0 ||
              found->second.bitWidth > uint64_t{INT64_MAX} ||
              automaticOffset > INT64_MAX - width)
            return TALOS_RT_INVALID_HANDLE;
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
            return TALOS_RT_INVALID_HANDLE;
          std::memcpy(address + 8, &base, 8);
        } else if (decodeGlobalHandle(stable, start)) {
          if (kind <= TALOS_RT_DESCRIPTOR_DRIVER) {
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
          return TALOS_RT_INVALID_HANDLE;
        std::memcpy(canonicalFrame + instruction.immediate, &stable, 8);
      }
      break;
    }
    case TALOS_RT_DB_CLEAR_FRAME_ROOT:
      if (!canonicalFrame || instruction.immediate > canonicalFrameSize ||
          sizeof(talos_rt_managed_word_v1) >
              canonicalFrameSize - instruction.immediate)
        return TALOS_RT_INVALID_FRAME;
      std::memset(canonicalFrame + instruction.immediate, 0,
                  sizeof(talos_rt_managed_word_v1));
      break;
    case TALOS_RT_DB_FRAME_ROOT:
      break;
    case TALOS_RT_DB_LOAD_STATE:
    case TALOS_RT_DB_STORE_STATE:
    case TALOS_RT_DB_OVERRIDE_STATE: {
      bool isLoad = instruction.opcode == TALOS_RT_DB_LOAD_STATE;
      bool isOverride = instruction.opcode == TALOS_RT_DB_OVERRIDE_STATE;
      if (isOverride)
        context->managedValueOverridePossible.store(true,
                                                    std::memory_order_relaxed);
      bool isAssignOverride =
          isOverride &&
          (instruction.flags & TALOS_RT_DB_OVERRIDE_KIND_MASK) ==
              TALOS_RT_DB_OVERRIDE_ASSIGN;
      bool dynamicOverride =
          isOverride &&
          (instruction.flags & TALOS_RT_DB_OVERRIDE_DYNAMIC) != 0;
      bool overrideClaim =
          dynamicOverride &&
          (instruction.flags & TALOS_RT_DB_OVERRIDE_CLAIM) != 0;
      uint64_t overrideOwner = 0;
      if (dynamicOverride) {
        Logic owner = read(instruction.source2);
        if (owner.width != 64 || anyUnknown(owner) || owner.value.empty())
          return TALOS_RT_INVALID_HANDLE;
        overrideOwner = owner.value[0];
        if (overrideOwner == 0)
          return TALOS_RT_INVALID_HANDLE;
      }
      bool isContinuous =
          !isLoad && !isOverride &&
          (instruction.flags & TALOS_RT_DB_STORE_STATE_CONTINUOUS) != 0;
      if (!isLoad && context &&
          context->activeExecRegion == TALOS_RT_REGION_POSTPONED)
        return TALOS_RT_INVALID_LIFECYCLE;
      uint32_t valueRegister =
          isLoad ? instruction.destination : instruction.source1;
      Layout valueLayout = layout(valueRegister);
      bool eventValue = valueLayout.kind == TALOS_RT_DBREG_HANDLE;
      Logic value;
      if (eventValue) {
        if (valueLayout.size != 32 || isOverride)
          return TALOS_RT_INVALID_HANDLE;
        value = Logic{64, false, LimbVector(1), LimbVector(1)};
        if (!isLoad) {
          uint32_t valueKind = 0;
          std::memcpy(&valueKind, frame.data + valueLayout.offset,
                      sizeof(valueKind));
          valueKind &= ~(kLocalHandleKind | kAutomaticHandleKind);
          uint64_t stable = UINT64_MAX;
          if (valueKind != TALOS_RT_DESCRIPTOR_EVENT ||
              !encodeCanonicalHandle(frame.data + valueLayout.offset, stable))
            return TALOS_RT_INVALID_HANDLE;
          value.value[0] = stable;
        }
      } else {
        value = !isLoad ? read(valueRegister)
                        : Logic{valueLayout.width,
                                valueLayout.kind == TALOS_RT_DBREG_LOGIC,
                                LimbVector(limbCount(valueLayout.width)),
                                LimbVector(limbCount(valueLayout.width))};
      }
      Layout handleLayout = layout(instruction.source0);
      if (handleLayout.kind != TALOS_RT_DBREG_HANDLE)
        return TALOS_RT_INVALID_HANDLE;
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
          return TALOS_RT_INVALID_HANDLE;
      } else if (boundedStatic) {
        begin = staticBegin;
      } else {
        begin = static_cast<int64_t>(automaticBase);
      }
      if ((local && (automatic || boundedStatic)) ||
          (!local && (descriptorKind < TALOS_RT_DESCRIPTOR_STORAGE ||
                      descriptorKind > TALOS_RT_DESCRIPTOR_DRIVER)) ||
          (automatic && descriptorKind != TALOS_RT_DESCRIPTOR_STORAGE) ||
          begin > end)
        return TALOS_RT_INVALID_HANDLE;
      if (eventValue && (descriptorKind != TALOS_RT_DESCRIPTOR_STORAGE ||
                         isContinuous || instruction.flags != 0))
        return TALOS_RT_INVALID_HANDLE;
      if (isOverride && (local || automatic ||
                         (descriptorKind != TALOS_RT_DESCRIPTOR_STORAGE &&
                          descriptorKind != TALOS_RT_DESCRIPTOR_NET) ||
                         (isAssignOverride &&
                          descriptorKind != TALOS_RT_DESCRIPTOR_STORAGE)))
        return TALOS_RT_INVALID_HANDLE;
      std::vector<uint64_t> retiredOverrideOwners;
      if (isOverride && descriptorKind == TALOS_RT_DESCRIPTOR_NET) {
        if (start < 0 || value.width == 0 ||
            value.width > static_cast<uint64_t>(end - start))
          return TALOS_RT_INVALID_HANDLE;
        uint64_t absolute = static_cast<uint64_t>(start);
        if (boundedStatic) {
          auto found = context->nativeStaticStates.find(staticID);
          if (found == context->nativeStaticStates.end() ||
              static_cast<uint64_t>(start) > found->second.bitWidth ||
              value.width >
                  found->second.bitWidth - static_cast<uint64_t>(start))
            return TALOS_RT_INVALID_HANDLE;
          absolute = found->second.bitOffset + static_cast<uint64_t>(start);
        }
        talos_rt_status status = TALOS_RT_OK;
        uint64_t offset = 0;
        while (offset != value.width) {
          if (dynamicOverride && !overrideClaim) {
            std::lock_guard<std::recursive_mutex> lock(context->mutex);
            while (offset != value.width &&
                   !talos_rt_override_owner_matches_unlocked(
                       context,
                       talos_rt_canonical_net_bit_unlocked(context,
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
                   talos_rt_override_owner_matches_unlocked(
                       context,
                       talos_rt_canonical_net_bit_unlocked(context,
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
          status = talos_rt_force_design_nets(
              context, absolute + runBegin, runWidth, runValue.data(),
              value.fourState ? runUnknown.data() : nullptr);
          if (status != TALOS_RT_OK)
            break;
        }
        if (status == TALOS_RT_OK && (!dynamicOverride || overrideClaim)) {
          std::lock_guard<std::recursive_mutex> lock(context->mutex);
          for (uint64_t offset = 0; offset != value.width; ++offset)
            talos_rt_claim_override_range_unlocked(
                context,
                talos_rt_canonical_net_bit_unlocked(context,
                                                      absolute + offset),
                1, false, dynamicOverride ? overrideOwner : 0,
                retiredOverrideOwners);
        }
        if (status != TALOS_RT_OK)
          return status;
        status = talos_rt_retire_override_owners(
            context, std::move(retiredOverrideOwners));
        if (status != TALOS_RT_OK)
          return status;
        break;
      }
      if (valueLayout.kind == TALOS_RT_DBREG_MANAGED && !isOverride) {
        if (descriptorKind != TALOS_RT_DESCRIPTOR_STORAGE ||
            valueLayout.width != 64 || local)
          return TALOS_RT_INVALID_HANDLE;
        // A view whose window falls outside the storage denotes no storage --
        // an out-of-range dynamic array element, say. The load then yields the
        // element default and the store is dropped, which is what the packed
        // path below does per bit. Misalignment stays an error: no view can
        // produce it, so it would mean a malformed image.
        if (start == kInvalidHandleStart || start < 0 || end - start < 64) {
          if (isLoad) {
            talos_rt_managed_word_v1 none = 0;
            std::memcpy(frame.data + valueLayout.offset, &none, sizeof(none));
          }
          break;
        }
        if (start % 64 != 0)
          return TALOS_RT_INVALID_HANDLE;
        talos_rt_managed_word_v1 managed = 0;
        talos_rt_managed_word_v1 previous = 0;
        if (instruction.opcode != TALOS_RT_DB_LOAD_STATE) {
          std::memcpy(&managed, frame.data + valueLayout.offset,
                      sizeof(managed));
          if (managed != talos_rt_managed_word_from_object(
                             talos_rt_object_from_managed_word(managed)))
            return TALOS_RT_INVALID_HANDLE;
        }
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        if (automatic) {
          uint32_t id = 0;
          int64_t baseOffset = 0;
          if (!decodeAutomaticHandle(automaticBase, id, baseOffset)) {
            return TALOS_RT_INVALID_HANDLE;
          }
          auto found = context->nativeAutomaticStates.find(id);
          if (found == context->nativeAutomaticStates.end())
            return TALOS_RT_INVALID_HANDLE;
          NativeAutomaticState &state = found->second;
          if (state.managedRootRegistered) {
            if (baseOffset != 0 || start != 0)
              return TALOS_RT_INVALID_HANDLE;
            if (instruction.opcode == TALOS_RT_DB_LOAD_STATE)
              managed = talos_rt_managed_word_from_object(state.managedValue);
            else {
              previous =
                  talos_rt_managed_word_from_object(state.managedValue);
              state.managedValue = talos_rt_object_from_managed_word(managed);
            }
          } else {
            if (start < 0 || baseOffset != start ||
                (static_cast<uint64_t>(start) & 63) != 0)
              return TALOS_RT_INVALID_HANDLE;
            uint64_t byteOffset = static_cast<uint64_t>(start) / 8;
            if (byteOffset > state.value.size() ||
                sizeof(managed) > state.value.size() - byteOffset ||
                std::find(state.managedRootByteOffsets.begin(),
                          state.managedRootByteOffsets.end(),
                          byteOffset) == state.managedRootByteOffsets.end())
              return TALOS_RT_INVALID_HANDLE;
            if (instruction.opcode == TALOS_RT_DB_LOAD_STATE)
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
              return TALOS_RT_INVALID_HANDLE;
            absolute = found->second.bitOffset + static_cast<uint64_t>(start);
          }
          if (absolute % 64 != 0 || absolute / 64 >= context->stateValue.size())
            return TALOS_RT_INVALID_HANDLE;
          auto slot = context->stateValue[absolute / 64];
          if (instruction.opcode == TALOS_RT_DB_LOAD_STATE)
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
        if (instruction.opcode == TALOS_RT_DB_LOAD_STATE)
          std::memcpy(frame.data + valueLayout.offset, &managed,
                      sizeof(managed));
        else if (previous != managed) {
          uint64_t changedHandle =
              automatic ? (automaticBase & ~uint64_t{UINT32_MAX}) |
                              static_cast<uint32_t>(start)
              : boundedStatic ? encodeStaticHandle(staticID, start)
                              : static_cast<uint64_t>(start);
          if (changedHandle == UINT64_MAX)
            return TALOS_RT_OUT_OF_RESOURCES;
          if (!talos_rt_publish_signal_occurrence_unlocked(
                  context, changedHandle, 64, TALOS_RT_SIGNAL_CHANGE))
            return context->schedulerStatus;
          talos_rt_invalidate_signal_snapshots_unlocked(context,
                                                          changedHandle, 64);
          if (!talos_rt_latch_conditional_signal_range_unlocked(
                  context, changedHandle, 64, TALOS_RT_SIGNAL_CHANGE))
            return context->schedulerStatus;
          if (!talos_rt_notify_observer_signal_unlocked(context,
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
          return TALOS_RT_INVALID_HANDLE;
        localFrame = state.frames[frameID];
        localLayout = layoutAt(image, localFrame->function, registerIndex);
        if (localLayout.kind != TALOS_RT_DBREG_BITS &&
            localLayout.kind != TALOS_RT_DBREG_LOGIC &&
            localLayout.kind != TALOS_RT_DBREG_REAL32 &&
            localLayout.kind != TALOS_RT_DBREG_REAL64)
          return TALOS_RT_INVALID_HANDLE;
        localValue = readLogic(localFrame->data, localLayout);
      } else if (!context) {
        return TALOS_RT_INVALID_ARGUMENT;
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
            return TALOS_RT_INVALID_HANDLE;
          auto found = context->nativeAutomaticStates.find(id);
          if (found == context->nativeAutomaticStates.end())
            return TALOS_RT_INVALID_HANDLE;
          automaticState = &found->second;
        } else if (boundedStatic) {
          auto found = context->nativeStaticStates.find(staticID);
          if (found == context->nativeStaticStates.end() ||
              staticBegin != begin || !context->execution ||
              found->second.bitOffset > context->execution->state_bit_count ||
              found->second.bitWidth >
                  context->execution->state_bit_count - found->second.bitOffset)
            return TALOS_RT_INVALID_HANDLE;
          staticState = &found->second;
        }
        if (isOverride) {
          if (start < 0 || value.width == 0 ||
              value.width > static_cast<uint64_t>(end - start))
            return TALOS_RT_INVALID_HANDLE;
          uint64_t overrideBegin =
              boundedStatic
                  ? staticState->bitOffset + static_cast<uint64_t>(start)
                  : static_cast<uint64_t>(start);
          if (!dynamicOverride || overrideClaim)
            talos_rt_claim_override_range_unlocked(
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
              if (context->twoState())
                context->assignUnknown.assignZero(limbs);
              else
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
          if (context->twoState())
            context->continuousUnknown.assignZero(limbs);
          else
            context->continuousUnknown.assign(limbs, 0);
        }
        bool equalStringContents = false;
        if (valueLayout.kind == TALOS_RT_DBREG_STRING && !isLoad) {
          if (local || descriptorKind != TALOS_RT_DESCRIPTOR_STORAGE ||
              valueLayout.width != 64 || start < 0 || start % 64 != 0 ||
              end - start < 64)
            return TALOS_RT_INVALID_HANDLE;
          talos_rt_string_v1 previous = 0;
          talos_rt_string_v1 next = 0;
          std::memcpy(&next, frame.data + valueLayout.offset, sizeof(next));
          if (automatic) {
            uint32_t id = 0;
            int64_t baseOffset = 0;
            if (!decodeAutomaticHandle(automaticBase, id, baseOffset) ||
                baseOffset != start)
              return TALOS_RT_INVALID_HANDLE;
            auto found = context->nativeAutomaticStates.find(id);
            if (found == context->nativeAutomaticStates.end())
              return TALOS_RT_INVALID_HANDLE;
            NativeAutomaticState &state = found->second;
            uint64_t byteOffset = static_cast<uint64_t>(start) / 8;
            if (state.managedRootRegistered) {
              if (start != 0)
                return TALOS_RT_INVALID_HANDLE;
              previous =
                  talos_rt_managed_word_from_object(state.managedValue);
            } else {
              if (byteOffset > state.value.size() ||
                  sizeof(previous) > state.value.size() - byteOffset)
                return TALOS_RT_INVALID_HANDLE;
              std::memcpy(&previous, state.value.data() + byteOffset,
                          sizeof(previous));
            }
          } else {
            uint64_t absolute = static_cast<uint64_t>(start);
            if (boundedStatic) {
              auto found = context->nativeStaticStates.find(staticID);
              if (found == context->nativeStaticStates.end())
                return TALOS_RT_INVALID_HANDLE;
              absolute = found->second.bitOffset + static_cast<uint64_t>(start);
            }
            if (absolute % 64 != 0 ||
                absolute / 64 >= context->stateValue.size())
              return TALOS_RT_INVALID_HANDLE;
            previous = context->stateValue[absolute / 64];
          }
          if (talos_rt_validate_string(context, previous) != TALOS_RT_OK ||
              talos_rt_validate_string(context, next) != TALOS_RT_OK)
            return TALOS_RT_INVALID_HANDLE;
          equalStringContents =
              talos_rt_v1_string_compare(previous, next) == 0;
        }
        bool realValue = valueLayout.kind == TALOS_RT_DBREG_REAL32 ||
                         valueLayout.kind == TALOS_RT_DBREG_REAL64;
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
            return TALOS_RT_INVALID_HANDLE;
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
        // An automatic variable's planes are bytes. A fully bounded load of
        // one reads them a word at a time too.
        bool packedAutomaticLoad =
            isLoad && automatic && start >= 0 && start >= begin &&
            start <= end && value.width <= static_cast<uint64_t>(end - start);
        if (packedAutomaticLoad) {
          uint64_t absolute = static_cast<uint64_t>(start);
          if (absolute > automaticState->bitWidth ||
              value.width > automaticState->bitWidth - absolute)
            return TALOS_RT_INVALID_HANDLE;
          uint64_t bytes = (absolute + value.width + 7) / 8;
          packedAutomaticLoad = bytes <= automaticState->value.size() &&
                                bytes <= automaticState->unknown.size();
          if (packedAutomaticLoad)
            for (uint64_t bitIndex = 0; bitIndex < value.width;
                 bitIndex += 64) {
              uint64_t width = std::min<uint64_t>(64, value.width - bitIndex);
              value.value[bitIndex / 64] = loadPackedBytes(
                  automaticState->value.data(), absolute + bitIndex, width);
              value.unknown[bitIndex / 64] = loadPackedBytes(
                  automaticState->unknown.data(), absolute + bitIndex, width);
            }
        }
        // A fully bounded canonical or automatic store that no force,
        // assignment, override or conditional waiter observes per bit updates
        // both planes (and a continuous driver's record) and computes its
        // edges a word at a time. Every other store keeps the per-bit path
        // below.
        bool packedStore = !isLoad && !local && !isOverride &&
                           !dynamicOverride && !realValue &&
                           !equalStringContents && start >= 0 &&
                           start >= begin && start <= end &&
                           value.width <= static_cast<uint64_t>(end - start) &&
                           !talos_rt_has_conditional_signal_waiters(context);
        uint64_t packedStoreBit = 0;
        if (packedStore && automatic) {
          uint64_t absolute = static_cast<uint64_t>(start);
          uint64_t bytes = (absolute + value.width + 7) / 8;
          packedStoreBit = absolute;
          packedStore = absolute <= automaticState->bitWidth &&
                        value.width <= automaticState->bitWidth - absolute &&
                        bytes <= automaticState->value.size() &&
                        bytes <= automaticState->unknown.size();
        } else if (packedStore) {
          uint64_t absolute = static_cast<uint64_t>(start);
          uint64_t available = boundedStatic ? staticState->bitWidth
                                             : context->stateValue.size() * 64;
          packedStoreBit =
              boundedStatic ? staticState->bitOffset + absolute : absolute;
          // Whether a force or procedural-assign mask claims any stored bit.
          auto overlaps = [&](const std::vector<uint64_t> &mask) {
            if (mask.empty())
              return false;
            if (mask.size() * 64 < packedStoreBit + value.width)
              return true;
            for (uint64_t bitIndex = 0; bitIndex < value.width; bitIndex += 64)
              if (loadPackedBits(
                      mask, packedStoreBit + bitIndex,
                      std::min<uint64_t>(64, value.width - bitIndex)) != 0)
                return true;
            return false;
          };
          packedStore =
              absolute <= available && value.width <= available - absolute &&
              packedStoreBit + value.width <= context->stateValue.size() * 64 &&
              packedStoreBit + value.width <=
                  context->stateUnknown.size() * 64 &&
              !overlaps(context->forceMask) && !overlaps(context->assignMask);
        }
        PackedSignalTransitionBuffer packedTransitions(packedStore ? value.width
                                                                   : 0);
        bool packedTransition = false;
        if (packedStore) {
          for (uint64_t bitIndex = 0; bitIndex < value.width; bitIndex += 64) {
            uint64_t width = std::min<uint64_t>(64, value.width - bitIndex);
            uint64_t at = packedStoreBit + bitIndex;
            uint64_t newValue =
                value.value[bitIndex / 64] & packedWidthMask(width);
            uint64_t newUnknown =
                value.unknown[bitIndex / 64] & packedWidthMask(width);
            uint64_t oldValue = 0, oldUnknown = 0;
            if (automatic) {
              oldValue =
                  loadPackedBytes(automaticState->value.data(), at, width);
              oldUnknown =
                  loadPackedBytes(automaticState->unknown.data(), at, width);
              storePackedBytes(automaticState->value.data(), at, width,
                               newValue);
              storePackedBytes(automaticState->unknown.data(), at, width,
                               newUnknown);
            } else {
              oldValue = loadPackedBits(context->stateValue, at, width);
              oldUnknown = loadPackedBits(context->stateUnknown, at, width);
              if (isContinuous) {
                storePackedBits(context->continuousValue, at, width, newValue);
                storePackedBits(context->continuousUnknown, at, width,
                                newUnknown);
                storePackedBits(context->continuousMask, at, width, UINT64_MAX);
              }
              storePackedBits(context->stateValue, at, width, newValue);
              storePackedBits(context->stateUnknown, at, width, newUnknown);
            }
            uint64_t differs =
                (oldValue ^ newValue) | (oldUnknown ^ newUnknown);
            if (differs == 0)
              continue;
            changed = true;
            packedTransition = true;
            // transitionEdges, a word at a time.
            uint64_t oldZero = ~oldUnknown & ~oldValue;
            uint64_t oldOne = ~oldUnknown & oldValue;
            uint64_t newZero = ~newUnknown & ~newValue;
            uint64_t newOne = ~newUnknown & newValue;
            storePackedBytes(packedTransitions.changed(), bitIndex, width,
                             differs);
            storePackedBytes(
                packedTransitions.posedge(), bitIndex, width,
                differs & ((oldZero & ~newZero) | (oldUnknown & newOne)));
            storePackedBytes(packedTransitions.negedge(), bitIndex, width,
                             differs &
                                 ((oldOne & ~newOne) | (oldUnknown & newZero)));
          }
          if (value.width != 0 && !automatic) {
            mirroredBegin = packedStoreBit;
            mirroredEnd = packedStoreBit + value.width;
          }
        }
        if (!local && !isLoad && !packedStore)
          transitions.reserve(static_cast<size_t>(std::min<uint64_t>(
              value.width, std::numeric_limits<size_t>::max())));
        for (uint64_t bitIndex =
                 packedLoad || packedAutomaticLoad || packedStore ? value.width
                                                                  : 0;
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
            return TALOS_RT_INVALID_HANDLE;
          uint64_t storageBit =
              boundedStatic ? staticState->bitOffset + absolute : absolute;
          if (!valid) {
            if (instruction.opcode == TALOS_RT_DB_LOAD_STATE) {
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
                !talos_rt_override_owner_matches_unlocked(
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
          talos_rt_sync_native_state_range_unlocked(
              context, mirroredBegin, mirroredEnd - mirroredBegin);
        if (isLoad && !eventValue && !local && !automatic &&
            context->observerForcesCanonicalPlane &&
            context->conditionPublication) {
          uint64_t stable = UINT64_MAX;
          if (!encodeCanonicalHandle(frame.data + handleLayout.offset, stable))
            return TALOS_RT_INVALID_HANDLE;
          for (uint64_t bit = 0; bit != value.width; ++bit) {
            bool publishedValue = false;
            bool publishedUnknown = false;
            if (!talos_rt_read_clock_condition_publication_bit_unlocked(
                    context, stable, bit, publishedValue, publishedUnknown))
              continue;
            setBit(value.value, bit, publishedValue);
            setBit(value.unknown, bit, publishedUnknown);
          }
        }
        bool realNotified = false;
        if (realValue && !isLoad && !local) {
          if (valueLayout.kind == TALOS_RT_DBREG_REAL32) {
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
            if (!talos_rt_publish_signal_occurrence_unlocked(
                    context, realHandle, value.width, TALOS_RT_SIGNAL_CHANGE))
              return context->schedulerStatus;
            talos_rt_invalidate_signal_snapshots_unlocked(context, realHandle,
                                                            value.width);
            if (!talos_rt_latch_conditional_signal_range_unlocked(
                    context, realHandle, value.width, TALOS_RT_SIGNAL_CHANGE))
              return context->schedulerStatus;
            if (!talos_rt_notify_observer_signal_unlocked(context, realHandle,
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
        if (packedTransition) {
          uint64_t handle =
              automatic       ? (automaticBase & ~uint64_t{UINT32_MAX}) |
                                    static_cast<uint32_t>(start)
              : boundedStatic ? encodeStaticHandle(staticID, start)
                              : static_cast<uint64_t>(start);
          uint64_t sequence = 0;
          if (!talos_rt_publish_signal_transition_batch_unlocked(
                  context, handle, value.width, packedTransitions.changed(),
                  packedTransitions.posedge(), packedTransitions.negedge(), 0,
                  &sequence))
            return context->schedulerStatus;
          talos_rt_invalidate_signal_snapshots_unlocked(context, handle,
                                                          value.width);
          if (!talos_rt_notify_observer_signal_unlocked(context, handle,
                                                          value.width))
            return context->schedulerStatus;
        }
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
            if (!talos_rt_publish_signal_transition_batch_unlocked(
                    context, transitions.front().handle, publicationWidth,
                    packed.changed(), packed.posedge(), packed.negedge(), 0,
                    &sequence))
              return context->schedulerStatus;
            talos_rt_invalidate_signal_snapshots_unlocked(
                context, transitions.front().handle, publicationWidth);
            if (talos_rt_has_conditional_signal_waiters(context)) {
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
                    !talos_rt_latch_conditional_signal_waiters_unlocked(
                        context, transition.handle, edges))
                  return context->schedulerStatus;
              }
            }
            if (!talos_rt_notify_observer_signal_unlocked(
                    context, transitions.front().handle, publicationWidth))
              return context->schedulerStatus;
          }
        }
        if (!local && !automatic && !isOverride &&
            instruction.opcode == TALOS_RT_DB_STORE_STATE &&
            descriptorKind == TALOS_RT_DESCRIPTOR_DRIVER && start >= 0 &&
            begin < end &&
            (instruction.flags &
             TALOS_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION) == 0 &&
            !resolveDrivenNets(
                image, context,
                boundedStatic
                    ? static_cast<int64_t>(staticState->bitOffset) + begin
                    : begin,
                boundedStatic
                    ? static_cast<int64_t>(staticState->bitOffset) + end
                    : end,
                changed))
          return TALOS_RT_INVALID_HANDLE;
        if (!local && changed && !realNotified &&
            ++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
      }
      if (instruction.opcode == TALOS_RT_DB_STORE_STATE &&
          (instruction.flags & TALOS_RT_DB_STORE_STATE_CHANGED) != 0) {
        if ((instruction.flags &
             TALOS_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION) != 0)
          changed = false;
        Logic changedValue{1, false, LimbVector(1), LimbVector(1)};
        setBit(changedValue.value, 0, changed);
        write(instruction.destination, changedValue);
      }
      if (local && instruction.opcode == TALOS_RT_DB_STORE_STATE)
        writeLogic(localFrame->data, localLayout, localValue);
      if (isOverride) {
        talos_rt_status status = talos_rt_retire_override_owners(
            context, std::move(retiredOverrideOwners));
        if (status != TALOS_RT_OK)
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
        uint32_t kind = TALOS_RT_DESCRIPTOR_EVENT;
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
            return TALOS_RT_INVALID_HANDLE;
          }
        }
        std::memcpy(address, &kind, sizeof(kind));
        std::memcpy(address + 8, &base, sizeof(base));
        std::memcpy(address + 16, &start, sizeof(start));
        std::memcpy(address + 24, &end, sizeof(end));
      }
      break;
    }
    case TALOS_RT_DB_RELEASE_STATE: {
      if (context->activeExecRegion == TALOS_RT_REGION_POSTPONED)
        return TALOS_RT_INVALID_LIFECYCLE;
      Layout handleLayout = layout(instruction.source0);
      if (handleLayout.kind != TALOS_RT_DBREG_HANDLE)
        return TALOS_RT_INVALID_HANDLE;
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
          (descriptorKind != TALOS_RT_DESCRIPTOR_STORAGE &&
           descriptorKind != TALOS_RT_DESCRIPTOR_NET) ||
          (instruction.flags == TALOS_RT_DB_OVERRIDE_ASSIGN &&
           descriptorKind != TALOS_RT_DESCRIPTOR_STORAGE))
        return TALOS_RT_INVALID_HANDLE;

      const NativeStaticState *staticState = nullptr;
      if (boundedStatic) {
        auto found = context->nativeStaticStates.find(staticID);
        if (found == context->nativeStaticStates.end() || staticBegin < 0 ||
            start < staticBegin ||
            static_cast<uint64_t>(end) > found->second.bitWidth)
          return TALOS_RT_INVALID_HANDLE;
        staticState = &found->second;
      } else if (static_cast<uint64_t>(end) > context->stateValue.size() * 64) {
        return TALOS_RT_INVALID_HANDLE;
      }
      uint64_t storageBegin =
          boundedStatic ? staticState->bitOffset + static_cast<uint64_t>(start)
                        : static_cast<uint64_t>(start);
      uint64_t width = static_cast<uint64_t>(end - start);
      bool releaseAssign = instruction.flags == TALOS_RT_DB_OVERRIDE_ASSIGN;
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
        if (descriptorKind == TALOS_RT_DESCRIPTOR_NET) {
          for (uint64_t offset = 0; offset != width; ++offset)
            talos_rt_release_override_range_unlocked(
                context,
                talos_rt_canonical_net_bit_unlocked(context,
                                                      storageBegin + offset),
                1, releaseAssign, retiredOverrideOwners);
        } else {
          talos_rt_release_override_range_unlocked(context, storageBegin,
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
          if (descriptorKind == TALOS_RT_DESCRIPTOR_STORAGE && !forced &&
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
          if (!talos_rt_publish_signal_transition_batch_unlocked(
                  context, publishedHandle, width, packed.changed(),
                  packed.posedge(), packed.negedge(), 0, &sequence))
            return context->schedulerStatus;
          talos_rt_invalidate_signal_snapshots_unlocked(
              context, publishedHandle, width);
          if (talos_rt_has_conditional_signal_waiters(context)) {
            for (const PendingTransition &transition : transitions)
              context->signalValueSnapshots[transition.handle] = {
                  sequence, transition.newValue, transition.newUnknown};
            for (const PendingTransition &transition : transitions) {
              uint32_t edges =
                  transitionEdges(transition.oldValue, transition.oldUnknown,
                                  transition.newValue, transition.newUnknown);
              if (!talos_rt_latch_conditional_signal_waiters_unlocked(
                      context, transition.handle, edges))
                return context->schedulerStatus;
            }
          }
          if (!talos_rt_notify_observer_signal_unlocked(
                  context, publishedHandle, width))
            return context->schedulerStatus;
        }
      }
      if (!releaseAssign && descriptorKind == TALOS_RT_DESCRIPTOR_NET) {
        talos_rt_status status =
            talos_rt_release_design_nets(context, storageBegin, width);
        if (status != TALOS_RT_OK)
          return status;
        status = talos_rt_retire_override_owners(
            context, std::move(retiredOverrideOwners));
        if (status != TALOS_RT_OK)
          return status;
        break;
      }
      if (!transitions.empty() && ++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
      {
        talos_rt_status status = talos_rt_retire_override_owners(
            context, std::move(retiredOverrideOwners));
        if (status != TALOS_RT_OK)
          return status;
      }
      break;
    }
    case TALOS_RT_DB_JUMP:
      if (!copyMap(image, frame, frame, instruction.source0,
                   instruction.source1, context))
        return TALOS_RT_INVALID_BYTECODE;
      pc = instruction.immediate;
      break;
    case TALOS_RT_DB_BRANCH: {
      Logic condition = read(instruction.destination);
      if (anyUnknown(condition) || !isZero(condition)) {
        if (!copyMap(image, frame, frame, instruction.source0,
                     instruction.source1, context))
          return TALOS_RT_INVALID_BYTECODE;
        pc = instruction.immediate;
      }
      break;
    }
    case TALOS_RT_DB_CALL: {
      Function calleeFunction = functionAt(image, instruction.source0);
      if (state.callDepth >= 1024)
        return TALOS_RT_OUT_OF_RESOURCES;
      ScopedReusableByteBuffer storage(
          context, static_cast<size_t>(calleeFunction.scratchSize));
      Frame callee{calleeFunction, instruction.source0, storage.data(),
                   state.callDepth + 2};
      state.frames[callee.id] = &callee;
      ++state.callDepth;
      if (!copyMap(image, frame, callee, instruction.source1,
                   instruction.source2, context)) {
        --state.callDepth;
        return TALOS_RT_INVALID_BYTECODE;
      }
      talos_rt_fragment_action_v1 callerAction = *action;
      *action = {
          TALOS_RT_FRAGMENT_CONTINUE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
      talos_rt_status status =
          executeFunction(image, callee, context, canonicalFrame,
                          canonicalFrameSize, calleeFunction.firstInstruction,
                          budget, action, state, pendingActivation,
                          instruction.auxiliary, instruction.immediate, &frame);
      --state.callDepth;
      if (status != TALOS_RT_OK)
        return status;
      if (action->kind != TALOS_RT_FRAGMENT_CONTINUE)
        return TALOS_RT_OK;
      *action = callerAction;
      break;
    }
    case TALOS_RT_DB_VIRTUAL_CALL:
    case TALOS_RT_DB_INTERFACE_CALL: {
      Layout receiverLayout = layout(instruction.source0);
      talos_rt_managed_word_v1 receiverWord = 0;
      std::memcpy(&receiverWord, frame.data + receiverLayout.offset,
                  sizeof(receiverWord));
      talos_rt_object_v1 *receiver =
          talos_rt_object_from_managed_word(receiverWord);
      if (talos_rt_managed_word_from_object(receiver) != receiverWord)
        return TALOS_RT_INVALID_BYTECODE;
      const talos_rt_method_descriptor_v1 *method = nullptr;
      talos_rt_status status;
      if (instruction.opcode == TALOS_RT_DB_INTERFACE_CALL) {
        auto [interfaceID, ordinal] = operandAt(image, instruction.destination);
        status = talos_rt_v1_interface_method_resolve(
            receiver, interfaceID, ordinal, instruction.immediate, &method);
      } else {
        status = talos_rt_v1_method_resolve(receiver, instruction.destination,
                                              instruction.immediate, &method);
      }
      if (status != TALOS_RT_OK)
        return status;
      if (!method ||
          method->bytecode_function == TALOS_RT_METHOD_NO_BYTECODE ||
          method->bytecode_function >= image.functionCount)
        return TALOS_RT_TIER_UNAVAILABLE;
      Function calleeFunction = functionAt(image, method->bytecode_function);
      if ((calleeFunction.flags & TALOS_RT_DESIGN_FUNCTION_PROCESS) != 0 ||
          calleeFunction.argumentCount != instruction.source2 ||
          calleeFunction.resultCount != instruction.flags ||
          !validMap(image, frame.function, calleeFunction, instruction.source1,
                    instruction.source2) ||
          !validMap(image, calleeFunction, frame.function,
                    instruction.auxiliary, instruction.flags))
        return TALOS_RT_INVALID_BYTECODE;
      for (uint32_t index = 0; index != calleeFunction.argumentCount; ++index)
        if (operandAt(image, instruction.source1 + index).first != index)
          return TALOS_RT_INVALID_BYTECODE;
      for (uint32_t index = 0; index != calleeFunction.resultCount; ++index)
        if (operandAt(image, instruction.auxiliary + index).second !=
            calleeFunction.argumentCount + index)
          return TALOS_RT_INVALID_BYTECODE;
      if (state.callDepth >= 1024)
        return TALOS_RT_OUT_OF_RESOURCES;
      ScopedReusableByteBuffer storage(
          context, static_cast<size_t>(calleeFunction.scratchSize));
      Frame callee{calleeFunction, method->bytecode_function, storage.data(),
                   state.callDepth + 2};
      state.frames[callee.id] = &callee;
      ++state.callDepth;
      if (!copyMap(image, frame, callee, instruction.source1,
                   instruction.source2, context)) {
        --state.callDepth;
        return TALOS_RT_INVALID_BYTECODE;
      }
      talos_rt_fragment_action_v1 callerAction = *action;
      *action = {
          TALOS_RT_FRAGMENT_CONTINUE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
      status = executeFunction(
          image, callee, context, canonicalFrame, canonicalFrameSize,
          calleeFunction.firstInstruction, budget, action, state,
          pendingActivation, instruction.auxiliary, instruction.flags, &frame);
      --state.callDepth;
      if (status != TALOS_RT_OK)
        return status;
      if (action->kind != TALOS_RT_FRAGMENT_CONTINUE)
        return TALOS_RT_OK;
      *action = callerAction;
      break;
    }
    case TALOS_RT_DB_RETURN:
      if (!copyMap(image, frame, frame, instruction.source0,
                   instruction.source1, context))
        return TALOS_RT_INVALID_BYTECODE;
      if (!caller)
        return TALOS_RT_OK;
      if (!copyMap(image, frame, *caller, returnFirst, returnCount, context))
        return TALOS_RT_INVALID_BYTECODE;
      return TALOS_RT_OK;
    case TALOS_RT_DB_CONTINUE:
      *action = {TALOS_RT_FRAGMENT_CONTINUE,
                 TALOS_RT_SUSPEND_NONE,
                 static_cast<uint32_t>(instruction.immediate),
                 0,
                 0,
                 0};
      return TALOS_RT_OK;
    case TALOS_RT_DB_PROCESS_CONTROL: {
      Logic value = read(instruction.source0);
      if (anyUnknown(value) || value.value.size() != 1)
        return TALOS_RT_INVALID_BYTECODE;
      talos_rt_process_control_disposition disposition =
          TALOS_RT_PROCESS_CONTROL_CONTINUE;
      talos_rt_status status = talos_rt_v1_process_control(
          context, value.value[0], instruction.flags, &disposition);
      if (status != TALOS_RT_OK)
        return status;
      switch (disposition) {
      case TALOS_RT_PROCESS_CONTROL_CONTINUE: {
        std::optional<uint64_t> continuation =
            continuationPC(image, frame.function,
                           static_cast<uint32_t>(instruction.immediate));
        if (!continuation)
          return TALOS_RT_INVALID_CONTINUATION;
        pc = *continuation;
        continue;
      }
      case TALOS_RT_PROCESS_CONTROL_SUSPEND_CURRENT:
        *action = {TALOS_RT_FRAGMENT_PROCESS_SUSPEND,
                   TALOS_RT_SUSPEND_NONE,
                   static_cast<uint32_t>(instruction.immediate),
                   0,
                   0,
                   0};
        break;
      case TALOS_RT_PROCESS_CONTROL_KILL_CURRENT:
        *action = {
            TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
        break;
      default:
        return TALOS_RT_INVALID_ARGUMENT;
      }
      return TALOS_RT_OK;
    }
    case TALOS_RT_DB_SUSPEND: {
      uint64_t payload = 0;
      if (instruction.source0 != kInvalidRegister) {
        Logic value = read(instruction.source0);
        if (anyUnknown(value) || value.value.size() > 1)
          return TALOS_RT_INVALID_BYTECODE;
        payload = value.value[0];
      }
      *action = {TALOS_RT_FRAGMENT_SUSPEND,
                 static_cast<uint32_t>(instruction.flags),
                 static_cast<uint32_t>(instruction.immediate),
                 instruction.auxiliary,
                 payload,
                 0};
      return TALOS_RT_OK;
    }
    case TALOS_RT_DB_TASK_CALL: {
      talos_rt_status status = prepareTaskActivation(
          image, frame, context, instruction.source0, instruction.source1,
          instruction.source2, pendingActivation);
      if (status != TALOS_RT_OK)
        return status;
      *action = {TALOS_RT_FRAGMENT_TASK_CALL,
                 TALOS_RT_SUSPEND_NONE,
                 static_cast<uint32_t>(instruction.immediate),
                 0,
                 0,
                 0};
      return TALOS_RT_OK;
    }
    case TALOS_RT_DB_VIRTUAL_TASK_CALL:
    case TALOS_RT_DB_INTERFACE_TASK_CALL: {
      Layout receiverLayout = layout(instruction.source0);
      talos_rt_managed_word_v1 receiverWord = 0;
      std::memcpy(&receiverWord, frame.data + receiverLayout.offset,
                  sizeof(receiverWord));
      talos_rt_object_v1 *receiver =
          talos_rt_object_from_managed_word(receiverWord);
      if (talos_rt_managed_word_from_object(receiver) != receiverWord)
        return TALOS_RT_INVALID_BYTECODE;
      const talos_rt_method_descriptor_v1 *method = nullptr;
      talos_rt_status status;
      if (instruction.opcode == TALOS_RT_DB_INTERFACE_TASK_CALL) {
        auto [interfaceID, ordinal] = operandAt(image, instruction.destination);
        status = talos_rt_v1_interface_method_resolve(
            receiver, interfaceID, ordinal, instruction.immediate, &method);
      } else {
        status = talos_rt_v1_method_resolve(receiver, instruction.destination,
                                              instruction.immediate, &method);
      }
      if (status != TALOS_RT_OK)
        return status;
      if (!method || (method->flags & TALOS_RT_METHOD_TASK) == 0)
        return TALOS_RT_INVALID_ARGUMENT;
      if ((method->flags & TALOS_RT_METHOD_PURE) != 0 ||
          method->bytecode_function == TALOS_RT_METHOD_NO_BYTECODE)
        return TALOS_RT_TIER_UNAVAILABLE;
      status = prepareTaskActivation(
          image, frame, context, method->bytecode_function, instruction.source1,
          instruction.source2, pendingActivation);
      if (status != TALOS_RT_OK)
        return status;
      *action = {TALOS_RT_FRAGMENT_TASK_CALL,
                 TALOS_RT_SUSPEND_NONE,
                 instruction.auxiliary,
                 0,
                 0,
                 0};
      return TALOS_RT_OK;
    }
    case TALOS_RT_DB_TERMINATE:
      *action = {TALOS_RT_FRAGMENT_TERMINATE,
                 TALOS_RT_SUSPEND_NONE,
                 0,
                 0,
                 instruction.immediate,
                 0};
      return TALOS_RT_OK;
    case TALOS_RT_DB_FAIL: {
      Layout status = layout(instruction.source0);
      if (status.kind != TALOS_RT_DBREG_STATUS)
        return TALOS_RT_INVALID_BYTECODE;
      uint64_t value;
      std::memcpy(&value, frame.data + status.offset, sizeof(value));
      if (value == 0)
        break;
      if (value > TALOS_RT_FATAL)
        return TALOS_RT_INVALID_BYTECODE;
      return static_cast<talos_rt_status>(value);
    }
    case TALOS_RT_DB_INTRINSIC: {
      talos_rt_status status = invokeIntrinsic(
          image, frame, context, static_cast<uint32_t>(instruction.immediate));
      if (status != TALOS_RT_OK)
        return status;
      break;
    }
    default:
      return TALOS_RT_INVALID_BYTECODE;
    }
  }
  return TALOS_RT_INVALID_BYTECODE;
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
  return talos_rt_design_signal_wait_blocked(task);
}

TALOS_RT_FEATURE_HELPER bool
designReadyCohortLater(const DesignReadyCohortEntry &lhs,
                       const DesignReadyCohortEntry &rhs) {
  return std::tuple{lhs.region, lhs.rank, lhs.insertionSequence} >
         std::tuple{rhs.region, rhs.rank, rhs.insertionSequence};
}

TALOS_RT_FEATURE_HELPER bool classifyDirectDesignReadyCohortMember(
    const ScheduledDesignTask &task, uint32_t activePhase,
    uint32_t unstartedActorRegion, DesignReadyCohortEntry &entry) {
  if (task.id == 0 || task.terminated || task.explicitlySuspended ||
      !task.started || task.phase != activePhase || task.urgent ||
      task.prioritySignal ||
      (task.suspendKind != TALOS_RT_SUSPEND_CHANGE &&
       task.suspendKind != TALOS_RT_SUSPEND_EDGE))
    return false;
  bool signalTriggered =
      task.signalTriggered || (task.signalLatch && task.signalLatch->triggered);
  if (!signalTriggered ||
      (task.queuedRegion >= unstartedActorRegion && !task.prioritySignal))
    return false;
  // Clause 31 slot-final waits are a tiny cold feature cohort whose effective
  // scheduler region depends on its wait flag. Keep them out of the ordinary
  // direct-signal cache so the common entry layout and classifier stay flat.
  const talos_rt_wait_record_v1 *wait = designTaskCurrentWait(task);
  if (talos_rt_is_slot_final_clock_occurrence_wait(wait))
    return false;
  entry.id = task.id;
  entry.region = designTaskOrderingRegion(task, signalTriggered);
  entry.rank = talos_rt_scheduler_rank(task);
  entry.insertionSequence = task.insertionSequence;
  return true;
}

TALOS_RT_FEATURE_TEXT bool trySelectCachedDesignReadyCohort(
    talos_rt_context *context, uint32_t activePhase,
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

TALOS_RT_FEATURE_TEXT void
installDesignReadyCohort(talos_rt_context *context,
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

TALOS_RT_FEATURE_TEXT void
suppressDesignReadyCohort(talos_rt_context *context, bool persistent) {
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

bool designReadyCohortSuppressed(const talos_rt_context *context) {
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

void invalidateDesignReadyCohort(talos_rt_context *context) {
  talos_rt_invalidate_design_ready_cohort(context);
}

void rebuildDesignSchedulerIndexUnlocked(talos_rt_context *context) {
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

talos_rt_status talos_rt_execute_design_observer(
    const talos_rt_execution_descriptor_v1 &execution,
    talos_rt_context *context, uint32_t functionIndex,
    const talos_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t *value, uint64_t *unknown, uint32_t outputLimbs) noexcept {
  if (!context || !value || !unknown || (captureCount != 0 && !captures))
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ManagedExecutionScope managedExecution(context);
    if (managedExecution.getStatus() != TALOS_RT_OK)
      return managedExecution.getStatus();
    talos_rt_design_bytecode_entry_v1 entry{&execution, functionIndex, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image) ||
        functionIndex >= image.functionCount)
      return TALOS_RT_INVALID_BYTECODE;
    const talos_rt_observer_descriptor_v1 *descriptor = nullptr;
    for (uint64_t index = 0; index != execution.observer_count; ++index)
      if (execution.observers[index].bytecode_function == functionIndex) {
        descriptor = &execution.observers[index];
        break;
      }
    if (!descriptor || descriptor->capture_count != captureCount)
      return TALOS_RT_INVALID_DESIGN;
    Function function = functionAt(image, functionIndex);
    if (function.id != descriptor->code_unit_id ||
        function.argumentCount != captureCount + 1 ||
        function.resultCount != 1 ||
        (function.flags & TALOS_RT_DESIGN_FUNCTION_PROCESS) != 0)
      return TALOS_RT_INVALID_BYTECODE;
    Layout contextLayout = layoutAt(image, function, 0);
    Layout resultLayout = layoutAt(image, function, function.argumentCount);
    bool fourState = (descriptor->flags & TALOS_RT_OBSERVER_FOUR_STATE) != 0;
    uint32_t expectedKind =
        (descriptor->flags & TALOS_RT_OBSERVER_REAL32) != 0
            ? TALOS_RT_DBREG_REAL32
        : (descriptor->flags & TALOS_RT_OBSERVER_REAL64) != 0
            ? TALOS_RT_DBREG_REAL64
        : fourState ? TALOS_RT_DBREG_LOGIC
                    : TALOS_RT_DBREG_BITS;
    if (contextLayout.kind != TALOS_RT_DBREG_HANDLE ||
        contextLayout.size != 32 || resultLayout.kind != expectedKind ||
        resultLayout.width != descriptor->result_width)
      return TALOS_RT_INVALID_BYTECODE;
    uint32_t resultLimbs =
        static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
    if (outputLimbs < resultLimbs)
      return TALOS_RT_ARGUMENT_MISMATCH;
    ScopedReusableByteBuffer storage(context,
                                     static_cast<size_t>(function.scratchSize));
    Frame frame{function, functionIndex, storage.data(), 1};
    {
      std::lock_guard<std::recursive_mutex> captureLock(context->mutex);
      for (uint32_t index = 0; index != captureCount; ++index) {
        Layout layout = layoutAt(image, function, index + 1);
        uint8_t *address = frame.data + layout.offset;
        uint64_t stable = captures[index].stable_id;
        const talos_rt_observer_capture_abi_v1 &abi =
            descriptor->capture_abi[index];
        if (abi.kind == TALOS_RT_OBSERVER_CAPTURE_ARGUMENT_REF) {
          if (layout.kind != TALOS_RT_DBREG_ARGUMENT_REF ||
              layout.size != 24 || captures[index].payload1 > 2 ||
              captures[index].payload2 != 0)
            return TALOS_RT_INVALID_BYTECODE;
          std::memcpy(address, &captures[index], 24);
          continue;
        }
        if (abi.kind == TALOS_RT_OBSERVER_CAPTURE_MANAGED) {
          if (layout.kind != TALOS_RT_DBREG_MANAGED || layout.size != 8)
            return TALOS_RT_INVALID_BYTECODE;
          std::memcpy(address, &stable, sizeof(stable));
          continue;
        }
        if (abi.kind == TALOS_RT_OBSERVER_CAPTURE_COVERGROUP) {
          if (layout.kind != TALOS_RT_DBREG_BITS || layout.width != 64 ||
              layout.size != 8)
            return TALOS_RT_INVALID_BYTECODE;
          std::memcpy(address, &stable, sizeof(stable));
          continue;
        }
        if (layout.kind != TALOS_RT_DBREG_HANDLE || layout.size != 32)
          return TALOS_RT_INVALID_BYTECODE;
        uint32_t kind = abi.kind == TALOS_RT_OBSERVER_CAPTURE_STORAGE
                            ? TALOS_RT_DESCRIPTOR_STORAGE
                        : abi.kind == TALOS_RT_OBSERVER_CAPTURE_NET
                            ? TALOS_RT_DESCRIPTOR_NET
                        : abi.kind == TALOS_RT_OBSERVER_CAPTURE_EVENT
                            ? TALOS_RT_DESCRIPTOR_EVENT
                            : TALOS_RT_DESCRIPTOR_DRIVER;
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
          if (kind != TALOS_RT_DESCRIPTOR_STORAGE)
            return TALOS_RT_INVALID_HANDLE;
          auto found = context->nativeAutomaticStates.find(dynamicID);
          if (found == context->nativeAutomaticStates.end() ||
              found->second.bitWidth > uint64_t{INT64_MAX})
            return TALOS_RT_INVALID_HANDLE;
          kind |= kAutomaticHandleKind;
          start = dynamicOffset;
          int64_t available = static_cast<int64_t>(found->second.bitWidth);
          if (start > INT64_MAX - static_cast<int64_t>(abi.width))
            return TALOS_RT_INVALID_HANDLE;
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
            return TALOS_RT_INVALID_HANDLE;
          start = dynamicOffset;
          int64_t available = static_cast<int64_t>(found->second.bitWidth);
          if (start > INT64_MAX - static_cast<int64_t>(abi.width))
            return TALOS_RT_INVALID_HANDLE;
          begin = std::max<int64_t>(0, start);
          end = std::min<int64_t>(available,
                                  start + static_cast<int64_t>(abi.width));
          if (begin > end)
            begin = end;
          base = encodeStaticHandle(dynamicID, begin);
        } else if (decodeGlobalHandle(stable, start)) {
          begin = start;
          if (kind <= TALOS_RT_DESCRIPTOR_DRIVER) {
            if (start > INT64_MAX - static_cast<int64_t>(abi.width))
              return TALOS_RT_INVALID_HANDLE;
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
          return TALOS_RT_INVALID_HANDLE;
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
    talos_rt_fragment_action_v1 action{};
    talos_rt_status status = executeFunction(image, frame, context, nullptr,
                                               0, function.firstInstruction,
                                               budget, &action, state, nullptr);
    if (status != TALOS_RT_OK)
      return status;
    Layout result = layoutAt(image, function, function.argumentCount);
    Logic evaluated = readLogic(frame.data, result);
    std::fill(value, value + outputLimbs, 0);
    std::fill(unknown, unknown + outputLimbs, 0);
    std::copy(evaluated.value.begin(), evaluated.value.end(), value);
    if (evaluated.fourState)
      std::copy(evaluated.unknown.begin(), evaluated.unknown.end(), unknown);
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

talos_rt_status talos_rt_execute_design_export(
    const talos_rt_execution_descriptor_v1 &execution,
    const talos_rt_export_descriptor_v1 &descriptor,
    talos_rt_context *context, const talos_rt_import_input_v1 *inputs,
    uint32_t inputCount, talos_rt_import_output_v1 *outputs,
    uint32_t outputCount) noexcept {
  if (!context || inputCount != descriptor.input_count ||
      outputCount != descriptor.output_count || (inputCount != 0 && !inputs) ||
      (outputCount != 0 && !outputs) ||
      descriptor.bytecode_function == TALOS_RT_EXPORT_NO_BYTECODE)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ManagedExecutionScope managedExecution(context);
    if (managedExecution.getStatus() != TALOS_RT_OK)
      return managedExecution.getStatus();
    uint32_t functionIndex = descriptor.bytecode_function;
    talos_rt_design_bytecode_entry_v1 entry{&execution, functionIndex, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image) ||
        functionIndex >= image.functionCount)
      return TALOS_RT_INVALID_BYTECODE;
    Function function = functionAt(image, functionIndex);
    if (function.id != descriptor.code_unit_id ||
        function.argumentCount != inputCount + 1 ||
        function.resultCount != outputCount ||
        (function.flags & TALOS_RT_DESIGN_FUNCTION_PROCESS) != 0)
      return TALOS_RT_INVALID_BYTECODE;
    Layout contextLayout = layoutAt(image, function, 0);
    if (contextLayout.kind != TALOS_RT_DBREG_HANDLE ||
        contextLayout.size != 32)
      return TALOS_RT_INVALID_BYTECODE;

    auto matches = [](const Layout &layout, uint8_t kind, uint8_t flags,
                      uint32_t width) {
      if (layout.width != width)
        return false;
      if ((flags & ~uint8_t{TALOS_RT_DBREG_SIGNED}) != 0)
        return false;
      switch (kind) {
      case TALOS_RT_DBREG_BITS:
        return layout.kind == TALOS_RT_DBREG_BITS &&
               layout.size == limbCount(width) * sizeof(uint64_t);
      case TALOS_RT_DBREG_LOGIC:
        return layout.kind == TALOS_RT_DBREG_LOGIC &&
               layout.size == limbCount(width) * sizeof(uint64_t) * 2;
      case TALOS_RT_DBREG_AGGREGATE:
        return (layout.kind == TALOS_RT_DBREG_BITS &&
                layout.size == limbCount(width) * sizeof(uint64_t)) ||
               (layout.kind == TALOS_RT_DBREG_LOGIC &&
                layout.size == limbCount(width) * sizeof(uint64_t) * 2);
      case TALOS_RT_DBREG_STRING:
        return layout.kind == TALOS_RT_DBREG_STRING && width == 64 &&
               layout.size == 8 && flags == 0;
      case TALOS_RT_DBREG_REAL32:
        return layout.kind == TALOS_RT_DBREG_REAL32 && width == 32 &&
               layout.size == 4 && flags == 0;
      case TALOS_RT_DBREG_REAL64:
        return layout.kind == TALOS_RT_DBREG_REAL64 && width == 64 &&
               layout.size == 8 && flags == 0;
      default:
        return false;
      }
    };
    auto copyIntoFrame = [&](uint8_t *frame, const Layout &layout,
                             const talos_rt_import_input_v1 &input) {
      if (!matches(layout, input.kind, input.flags, input.bit_width) ||
          input.limb_count != limbCount(input.bit_width) || !input.value ||
          (layout.kind == TALOS_RT_DBREG_LOGIC) != (input.unknown != nullptr))
        return false;
      uint64_t bytes = input.limb_count * sizeof(uint64_t);
      if (input.kind == TALOS_RT_DBREG_REAL32 ||
          input.kind == TALOS_RT_DBREG_REAL64)
        bytes = input.bit_width / 8;
      std::memcpy(frame + layout.offset, input.value,
                  static_cast<size_t>(bytes));
      if (input.unknown)
        std::memcpy(frame + layout.offset + bytes, input.unknown,
                    static_cast<size_t>(bytes));
      return true;
    };
    auto copyFromFrame = [&](const uint8_t *frame, const Layout &layout,
                             talos_rt_import_output_v1 &output) {
      if (!matches(layout, output.kind, output.flags, output.bit_width) ||
          output.limb_count != limbCount(output.bit_width) || !output.value ||
          (layout.kind == TALOS_RT_DBREG_LOGIC) !=
              (output.unknown != nullptr))
        return false;
      uint64_t bytes = output.limb_count * sizeof(uint64_t);
      if (output.kind == TALOS_RT_DBREG_REAL32 ||
          output.kind == TALOS_RT_DBREG_REAL64)
        bytes = output.bit_width / 8;
      std::memcpy(output.value, frame + layout.offset,
                  static_cast<size_t>(bytes));
      if (output.unknown)
        std::memcpy(output.unknown, frame + layout.offset + bytes,
                    static_cast<size_t>(bytes));
      return true;
    };

    for (uint32_t index = 0; index != outputCount; ++index) {
      const talos_rt_import_output_v1 &output = outputs[index];
      Layout layout = layoutAt(image, function, function.argumentCount + index);
      if (!matches(layout, output.kind, output.flags, output.bit_width) ||
          output.limb_count != limbCount(output.bit_width) || !output.value ||
          (layout.kind == TALOS_RT_DBREG_LOGIC) !=
              (output.unknown != nullptr))
        return TALOS_RT_INVALID_BYTECODE;
    }
    if (function.scratchSize > std::numeric_limits<size_t>::max())
      return TALOS_RT_OUT_OF_RESOURCES;

    ScopedReusableByteBuffer storage(context,
                                     static_cast<size_t>(function.scratchSize));
    std::memset(storage.data(), 0, static_cast<size_t>(function.scratchSize));
    Frame frame{function, functionIndex, storage.data(), 1};
    for (uint32_t index = 0; index != inputCount; ++index)
      if (!copyIntoFrame(frame.data, layoutAt(image, function, index + 1),
                         inputs[index]))
        return TALOS_RT_INVALID_BYTECODE;
    ExecutionState state;
    state.frames[frame.id] = &frame;
    StepBudget budget{UINT64_MAX, 0};
    talos_rt_fragment_action_v1 action{};
    talos_rt_status status = executeFunction(image, frame, context, nullptr,
                                               0, function.firstInstruction,
                                               budget, &action, state, nullptr);
    if (status != TALOS_RT_OK)
      return status;
    for (uint32_t index = 0; index != outputCount; ++index)
      if (!copyFromFrame(
              frame.data,
              layoutAt(image, function, function.argumentCount + index),
              outputs[index]))
        return TALOS_RT_INVALID_BYTECODE;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

talos_rt_status talos_rt_execute_design_export_task(
    const talos_rt_execution_descriptor_v1 &execution,
    const talos_rt_export_descriptor_v1 &descriptor,
    talos_rt_context *context, const talos_rt_import_input_v1 *inputs,
    uint32_t inputCount, talos_rt_import_output_v1 *outputs,
    uint32_t outputCount, const uint8_t *directions,
    const int64_t *const *aggregatePlans,
    const uint64_t *aggregatePlanWords) noexcept {
  if (!context || inputCount != descriptor.input_count ||
      outputCount != descriptor.output_count ||
      (descriptor.flags & TALOS_RT_EXPORT_TASK) == 0 ||
      descriptor.bytecode_function == TALOS_RT_EXPORT_NO_BYTECODE ||
      (inputCount != 0 &&
       (!inputs || !directions || !aggregatePlans || !aggregatePlanWords)) ||
      (outputCount != 0 && !outputs))
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ManagedExecutionScope managedExecution(context);
    if (managedExecution.getStatus() != TALOS_RT_OK)
      return managedExecution.getStatus();
    uint32_t functionIndex = descriptor.bytecode_function;
    talos_rt_design_bytecode_entry_v1 entry{&execution, functionIndex, 0};
    Image image;
    if (!loadValidatedImage(entry, context, image) ||
        functionIndex >= image.functionCount)
      return TALOS_RT_INVALID_BYTECODE;
    Function function = functionAt(image, functionIndex);
    uint32_t copiedOutputs = 0;
    uint64_t physicalArguments = 1;
    for (uint32_t index = 0; index != inputCount; ++index) {
      uint8_t direction = directions[index] & 0x7fu;
      bool elided = (directions[index] & 0x80u) != 0;
      if (direction > 2 || (elided && direction != 1))
        return TALOS_RT_INVALID_ARGUMENT;
      physicalArguments += !elided + (direction != 0);
      copiedOutputs += direction != 0;
    }
    if (function.id != descriptor.code_unit_id ||
        (function.flags & TALOS_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
        function.argumentCount != physicalArguments ||
        function.resultCount != 0 || copiedOutputs != outputCount)
      return TALOS_RT_INVALID_BYTECODE;

    auto matches = [](const Layout &layout, uint8_t kind, uint8_t flags,
                      uint32_t width) {
      if (layout.width != width ||
          (flags & ~uint8_t{TALOS_RT_DBREG_SIGNED}) != 0)
        return false;
      switch (kind) {
      case TALOS_RT_DBREG_BITS:
        return layout.kind == TALOS_RT_DBREG_BITS;
      case TALOS_RT_DBREG_LOGIC:
        return layout.kind == TALOS_RT_DBREG_LOGIC;
      case TALOS_RT_DBREG_AGGREGATE:
        return layout.kind == TALOS_RT_DBREG_BITS ||
               layout.kind == TALOS_RT_DBREG_LOGIC;
      case TALOS_RT_DBREG_STRING:
        return layout.kind == TALOS_RT_DBREG_STRING && width == 64 &&
               flags == 0;
      case TALOS_RT_DBREG_REAL32:
        return layout.kind == TALOS_RT_DBREG_REAL32 && width == 32 &&
               flags == 0;
      case TALOS_RT_DBREG_REAL64:
        return layout.kind == TALOS_RT_DBREG_REAL64 && width == 64 &&
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
        return TALOS_RT_INVALID_BYTECODE;
      captures[capture.argument] = capture;
      hasCapture[capture.argument] = 1;
    }
    if (std::find(hasCapture.begin(), hasCapture.end(), uint8_t{0}) !=
        hasCapture.end())
      return TALOS_RT_INVALID_BYTECODE;
    uint64_t canonicalSize =
        (function.flags & TALOS_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK) >> 1;
    if (function.scratchAlignment == 0 ||
        canonicalSize > UINT64_MAX - (function.scratchAlignment - 1))
      return TALOS_RT_INVALID_BYTECODE;
    uint64_t scratchOffset = (canonicalSize + function.scratchAlignment - 1) &
                             ~(function.scratchAlignment - 1);
    if (scratchOffset > UINT64_MAX - function.scratchSize ||
        scratchOffset + function.scratchSize >
            std::numeric_limits<size_t>::max())
      return TALOS_RT_OUT_OF_MEMORY;

    ScheduledDesignTask task;
    task.parent = context->activeLogicalProcessToken;
    if (task.parent != 0)
      context->logicalProcessParentsWithChildren.insert(task.parent);
    task.programOwner = context->activeProgramOwner;
    task.initializationRandomScope =
        talos_rt_random_initialization_scope_unlocked(context);
    talos_rt_random_split_unlocked(context, task.random);
    task.function = functionIndex;
    task.scheduleRank = static_cast<uint32_t>(function.initialScheduleRank);
    task.scratchOffset = scratchOffset;
    task.scratchSize = function.scratchSize;
    task.frame = context->designTaskFrames.acquire(
        static_cast<size_t>(scratchOffset + function.scratchSize));

    auto copyInput = [&](uint32_t argument,
                         const talos_rt_import_input_v1 &input) {
      Layout layout = layoutAt(image, function, argument);
      CaptureRecord capture = captures[argument];
      if (!matches(layout, input.kind, input.flags, input.bit_width) ||
          input.limb_count != limbCount(input.bit_width) || !input.value ||
          (layout.kind == TALOS_RT_DBREG_LOGIC) !=
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
      if (layout.kind != TALOS_RT_DBREG_HANDLE ||
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
        (void)talos_rt_v1_native_state_release(context, handle, 0);
      for (uint64_t handle : owners)
        (void)talos_rt_v1_native_state_release(context, handle, 1);
    };
    uint32_t argument = 1, output = 0;
    for (uint32_t index = 0; index != inputCount; ++index) {
      uint8_t direction = directions[index] & 0x7fu;
      bool elided = (directions[index] & 0x80u) != 0;
      if (!elided && !copyInput(argument++, inputs[index])) {
        releaseUnscheduled();
        return TALOS_RT_INVALID_BYTECODE;
      }
      if (direction == 0)
        continue;
      talos_rt_import_output_v1 &destination = outputs[output++];
      if (destination.kind != inputs[index].kind ||
          destination.flags != inputs[index].flags ||
          destination.bit_width != inputs[index].bit_width ||
          destination.limb_count != inputs[index].limb_count ||
          !destination.value ||
          (inputs[index].unknown != nullptr) !=
              (destination.unknown != nullptr)) {
        releaseUnscheduled();
        return TALOS_RT_ARGUMENT_MISMATCH;
      }
      uint64_t handle = UINT64_MAX;
      talos_rt_status status;
      if (inputs[index].kind == TALOS_RT_DBREG_AGGREGATE &&
          aggregatePlans[index] && aggregatePlanWords[index] != 0) {
        if (!designBytecodeDpiAggregateStateAlloc) {
          releaseUnscheduled();
          return TALOS_RT_TIER_UNAVAILABLE;
        }
        status = designBytecodeDpiAggregateStateAlloc(
            context, inputs[index].bit_width,
            reinterpret_cast<const uint8_t *>(inputs[index].value),
            reinterpret_cast<const uint8_t *>(inputs[index].unknown),
            aggregatePlans[index], aggregatePlanWords[index], &handle);
      } else if (inputs[index].kind == TALOS_RT_DBREG_STRING) {
        talos_rt_managed_root_slot_v1 root{
            0, TALOS_RT_MANAGED_ROOT_KIND_STRING, 0};
        status = talos_rt_v1_native_state_alloc_with_typed_roots(
            context, inputs[index].bit_width,
            reinterpret_cast<const uint8_t *>(inputs[index].value), nullptr,
            &root, 1, &handle);
      } else {
        status = talos_rt_v1_native_state_alloc(
            context, inputs[index].bit_width,
            reinterpret_cast<const uint8_t *>(inputs[index].value),
            reinterpret_cast<const uint8_t *>(inputs[index].unknown), &handle);
      }
      if (status != TALOS_RT_OK) {
        releaseUnscheduled();
        return status;
      }
      owners.push_back(handle);
      status = talos_rt_v1_native_state_retain(context, handle);
      if (status == TALOS_RT_OK)
        retained.push_back(handle);
      if (status != TALOS_RT_OK || !copyHandle(argument++, handle)) {
        releaseUnscheduled();
        return status != TALOS_RT_OK ? status : TALOS_RT_INVALID_BYTECODE;
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
        return TALOS_RT_OUT_OF_RESOURCES;
      }
      id = context->nextDesignTaskID++;
      talos_rt_invalidate_design_ready_cohort(context);
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
      talos_rt_register_unstarted_actor(
          context, context->scheduledDesignTasks.back().phase, id);
      talos_rt_retain_controls_unlocked(
          context, context->scheduledDesignTasks.back().controls);
    }
    // The scheduled task now owns every retained capture reference.
    retained.clear();
    talos_rt_status status =
        talos_rt_run_dpi_export_task_logical(context, id);
    if (status == TALOS_RT_OK) {
      for (uint32_t index = 0; index != outputCount; ++index) {
        talos_rt_import_output_v1 &destination = outputs[index];
        status = talos_rt_v1_native_state_load_plane(
            context, reinterpret_cast<const uint8_t *>(destination.value), 0,
            owners[index], destination.bit_width, 0, 0,
            reinterpret_cast<uint8_t *>(destination.value));
        if (status == TALOS_RT_OK && destination.unknown)
          status = talos_rt_v1_native_state_load_plane(
              context, reinterpret_cast<const uint8_t *>(destination.unknown),
              0, owners[index], destination.bit_width, 1, 0,
              reinterpret_cast<uint8_t *>(destination.unknown));
        if (status != TALOS_RT_OK)
          break;
      }
    }
    for (uint64_t handle : owners) {
      talos_rt_status released =
          talos_rt_v1_native_state_release(context, handle, 1);
      if (status == TALOS_RT_OK && released != TALOS_RT_OK)
        status = released;
    }
    return status;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

static talos_rt_status executeDesignBytecode(
    const talos_rt_design_bytecode_entry_v1 &entry,
    talos_rt_context *context, void *frame, uint64_t frameSize,
    uint64_t scratchOffset, uint64_t scratchSize, uint32_t continuation,
    uint64_t instructionLimit, talos_rt_fragment_action_v1 *outAction,
    std::unique_ptr<PendingDesignActivation> *pendingActivation) noexcept {
  if (!outAction || (frameSize != 0 && !frame))
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    ManagedExecutionScope managedExecution(context);
    if (managedExecution.getStatus() != TALOS_RT_OK)
      return managedExecution.getStatus();
    Image image;
    if (!loadValidatedImage(entry, context, image))
      return TALOS_RT_INVALID_BYTECODE;
    Function function = functionAt(image, entry.function);
    if (function.scratchSize > scratchSize || scratchOffset > frameSize ||
        scratchSize > frameSize - scratchOffset)
      return TALOS_RT_INVALID_FRAME;
    std::optional<uint64_t> pc = continuationPC(image, function, continuation);
    if (!pc)
      return TALOS_RT_INVALID_CONTINUATION;
    uint8_t *scratch = static_cast<uint8_t *>(frame) + scratchOffset;
    std::memset(scratch, 0, static_cast<size_t>(function.scratchSize));
    Frame top{function, entry.function, scratch, 1};
    ExecutionState state;
    state.frames[top.id] = &top;
    StepBudget budget{instructionLimit, 0};
    *outAction = {
        TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
    talos_rt_status status = executeFunction(
        image, top, context, static_cast<uint8_t *>(frame), scratchOffset, *pc,
        budget, outAction, state, pendingActivation);
    if (status != TALOS_RT_OK ||
        outAction->kind != TALOS_RT_FRAGMENT_TERMINATE)
      return status;
    status = releaseCapturedAutomaticStates(image, entry.function, context,
                                            static_cast<const uint8_t *>(frame),
                                            scratchOffset);
    return status;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_BYTECODE; }
}

talos_rt_status talos_rt_execute_design_bytecode(
    const talos_rt_design_bytecode_entry_v1 &entry,
    talos_rt_context *context, void *frame, uint64_t frameSize,
    uint64_t scratchOffset, uint64_t scratchSize, uint32_t continuation,
    uint64_t instructionLimit,
    talos_rt_fragment_action_v1 *outAction) noexcept {
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

talos_rt_status cancelLogicalProcessTree(talos_rt_context *context,
                                           uint64_t root,
                                           uint64_t preservedActive,
                                           bool &preserved) {
  TALOS_RT_TRY {
    preserved = false;
    std::unordered_set<uint64_t> targets;
    std::vector<talos_rt_process_instance_v1 *> nativeInstances;
    std::vector<CancelledLogicalDesignActivation> designActivations;
    std::vector<std::pair<uint64_t, uint64_t>> killedParents;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      invalidateDesignReadyCohort(context);
      bool foundRoot = false;
      targets =
          talos_rt_collect_process_tree_unlocked(context, root, foundRoot);
      auto contains = [&](uint64_t token) { return targets.count(token) != 0; };
      // A finished root has nothing left to terminate but its descendants.
      if (!foundRoot && !context->finishedProcessParents.count(root))
        return TALOS_RT_INVALID_HANDLE;
      preserved = preservedActive != 0 && contains(preservedActive);

      size_t nativeCount = 0;
      size_t designCount = 0;
      size_t nativeTasks = 0;
      size_t designTasks = 0;
      for (const ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG | process.token;
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
      killedParents.reserve(checkedSizeSum(nativeTasks, designTasks));
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
                                       TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG)
                             ? 1
                             : 0)));
      context->killedDesignTasks.reserveRanges(checkedSizeSum(
          context->killedDesignTasks.rangeCount(),
          checkedSizeSum(designTasks,
                         preserved && !(preservedActive &
                                        TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG)
                             ? 1
                             : 0)));
      if (preserved) {
        if (preservedActive & TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG)
          context->killedNativeProcesses.insert(
              preservedActive & ~TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG);
        else
          context->killedDesignTasks.insert(preservedActive);
      }

      for (ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG | process.token;
        if (!process.instance || !contains(token) || token == preservedActive)
          continue;
        context->terminatedNativeProcesses.insert(process.token,
                                                  process.random);
        context->killedNativeProcesses.insert(process.token);
        talos_rt_program_complete_unlocked(context, token,
                                             process.programOwner);
        if (context->logicalProcessParentsWithChildren.erase(token))
          killedParents.emplace_back(token, process.parent);
        if (!process.started)
          talos_rt_unregister_unstarted_actor(context, process.phase, token);
        talos_rt_flush_deferred_immediate_reports_unlocked(context, token);
        nativeInstances.push_back(process.instance);
        nativeInstances.insert(nativeInstances.end(), process.callers.begin(),
                               process.callers.end());
        process.callers.clear();
        process.callerDepths.clear();
        talos_rt_unregister_signal_wait_unlocked(
            context, process.signalSubscriptions, process.token, false);
        if (context->vpiState)
          (void)talos_rt_vpi_frame_unwind_process(context, token, 0);
        process.instance = nullptr;
        ++context->schedulerDeadProcessCount;
        context->schedulerCompactionPending = true;
        talos_rt_release_controls_unlocked(context, process.controls);
        process.controls.clear();
        process.signalTriggered = false;
        process.explicitlySuspended = false;
      }
      for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || !contains(task.id) || task.id == preservedActive)
          continue;
        context->terminatedDesignTasks.insert(task.id, task.random);
        context->killedDesignTasks.insert(task.id);
        talos_rt_program_complete_unlocked(context, task.id,
                                             task.programOwner);
        if (context->logicalProcessParentsWithChildren.erase(task.id))
          killedParents.emplace_back(task.id, task.parent);
        if (!task.started)
          talos_rt_unregister_unstarted_actor(context, task.phase, task.id);
        talos_rt_flush_deferred_immediate_reports_unlocked(context, task.id);
        designActivations.push_back({task.id, task.function, task.scratchOffset,
                                     std::move(task.frame)});
        for (DesignActivation &caller : task.callers)
          designActivations.push_back({task.id, caller.function,
                                       caller.scratchOffset,
                                       std::move(caller.frame)});
        task.callers.clear();
        talos_rt_unregister_signal_wait_unlocked(
            context, task.signalSubscriptions, task.id, true);
        talos_rt_release_controls_unlocked(context, task.controls);
        task.controls.clear();
        if (context->vpiState)
          (void)talos_rt_vpi_frame_unwind_process(context, task.id, 0);
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
      for (auto [killed, parent] : killedParents)
        talos_rt_record_terminated_parent_unlocked(context, killed, parent);
      if (++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
    }

    talos_rt_status result = TALOS_RT_OK;
    if (!designActivations.empty()) {
      if (!context->execution)
        result = TALOS_RT_INVALID_LIFECYCLE;
      else {
        talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
        Image image;
        if (!loadValidatedImage(entry, context, image))
          result = TALOS_RT_INVALID_BYTECODE;
        else
          for (const auto &activation : designActivations) {
            talos_rt_status status = releaseCapturedAutomaticStates(
                image, activation.function, context, activation.frame.data(),
                activation.scratchOffset);
            if (result == TALOS_RT_OK && status != TALOS_RT_OK)
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
    for (talos_rt_process_instance_v1 *instance : nativeInstances) {
      talos_rt_status status =
          talos_rt_v1_process_instance_destroy(instance);
      if (result == TALOS_RT_OK && status != TALOS_RT_OK)
        result = status;
    }
    return result;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

} // namespace

extern "C" talos_rt_status talos_rt_v1_process_control(
    talos_rt_context *context, uint64_t logicalProcess,
    talos_rt_process_control_kind kind,
    talos_rt_process_control_disposition *outDisposition) {
  if (!context || logicalProcess == 0 || !outDisposition ||
      kind > TALOS_RT_PROCESS_CONTROL_RESUME)
    return TALOS_RT_INVALID_ARGUMENT;
  *outDisposition = TALOS_RT_PROCESS_CONTROL_CONTINUE;
  ContextTransaction transaction(context);
  TALOS_RT_TRY {
    uint64_t activeProcess = 0;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      invalidateDesignReadyCohort(context);
      // Compiled designs containing process.control are excluded from AOT.
      // Keep the public ABI safe for external callers too: mutating or
      // destroying actors still owned by a live generated plan would leave
      // its ready/deadline inventory stale.
      if (context->nativeSchedulePlan)
        return TALOS_RT_TIER_UNAVAILABLE;
      bool native =
          (logicalProcess & TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG) != 0;
      uint64_t nativeToken =
          logicalProcess & ~TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG;
      bool terminated =
          native ? context->terminatedNativeProcesses.count(nativeToken) != 0
                 : context->terminatedDesignTasks.count(logicalProcess) != 0;
      // IEEE 1800-2023 9.7: kill() of a FINISHED or KILLED process still
      // terminates its live descendants.
      if (terminated &&
          (kind != TALOS_RT_PROCESS_CONTROL_KILL ||
           !context->finishedProcessParents.count(logicalProcess)))
        return TALOS_RT_OK;

      activeProcess = context->activeLogicalProcessToken;
      if (activeProcess == logicalProcess) {
        if (kind == TALOS_RT_PROCESS_CONTROL_SUSPEND)
          *outDisposition = TALOS_RT_PROCESS_CONTROL_SUSPEND_CURRENT;
        if (kind != TALOS_RT_PROCESS_CONTROL_KILL)
          return TALOS_RT_OK;
      }

      if (kind != TALOS_RT_PROCESS_CONTROL_KILL) {
        if (native) {
          ScheduledProcess *found = findScheduledProcess(context, nativeToken);
          if (!found || !found->instance)
            return TALOS_RT_INVALID_HANDLE;
          if (kind == TALOS_RT_PROCESS_CONTROL_RESUME &&
              found->explicitlySuspended)
            talos_rt_resensitize_process_unlocked(context, *found);
          found->explicitlySuspended =
              kind == TALOS_RT_PROCESS_CONTROL_SUSPEND;
          context->nativePollCandidates.insert(nativeToken);
        } else {
          if (context->scheduledDesignTaskIndices.size() !=
              context->scheduledDesignTasks.size())
            rebuildDesignSchedulerIndexUnlocked(context);
          auto indexed =
              context->scheduledDesignTaskIndices.find(logicalProcess);
          if (indexed == context->scheduledDesignTaskIndices.end() ||
              indexed->second >= context->scheduledDesignTasks.size())
            return TALOS_RT_INVALID_HANDLE;
          ScheduledDesignTask &task =
              context->scheduledDesignTasks[indexed->second];
          if (task.terminated || task.id != logicalProcess)
            return TALOS_RT_INVALID_HANDLE;
          if (kind == TALOS_RT_PROCESS_CONTROL_RESUME &&
              task.explicitlySuspended)
            talos_rt_resensitize_design_task_unlocked(context, task);
          task.explicitlySuspended = kind == TALOS_RT_PROCESS_CONTROL_SUSPEND;
          context->designPollCandidates.insert(logicalProcess);
        }
        if (++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
        return TALOS_RT_OK;
      }
    }
    bool killedActive = false;
    talos_rt_status status = cancelLogicalProcessTree(
        context, logicalProcess, activeProcess, killedActive);
    if (status == TALOS_RT_OK && killedActive)
      *outDisposition = TALOS_RT_PROCESS_CONTROL_KILL_CURRENT;
    return status;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

extern "C" talos_rt_status
talos_rt_v1_scheduler_disable_children(talos_rt_context *context) {
  if (!context)
    return TALOS_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  struct CancelledDesignTask {
    uint64_t id;
    uint32_t function;
    uint64_t scratchOffset;
    std::vector<uint8_t> frame;
  };
  TALOS_RT_TRY {
    std::unordered_set<uint64_t> descendants;
    std::vector<talos_rt_process_instance_v1 *> nativeInstances;
    std::vector<CancelledDesignTask> designTasks;
    std::vector<uint64_t> insertedNativeTerminations;
    std::vector<uint64_t> insertedDesignTerminations;
    std::vector<uint64_t> insertedNativeKills;
    std::vector<uint64_t> insertedDesignKills;
    std::vector<std::pair<uint64_t, uint64_t>> killedParents;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      invalidateDesignReadyCohort(context);
      uint64_t root = context->activeLogicalProcessToken;
      if (root == 0)
        return TALOS_RT_INVALID_LIFECYCLE;
      bool rootLive = false;
      descendants =
          talos_rt_collect_process_tree_unlocked(context, root, rootLive);
      auto contains = [&](uint64_t token) {
        return descendants.count(token) != 0;
      };

      size_t nativeActivationCount = 0;
      size_t designActivationCount = 0;
      size_t nativeTaskCount = 0;
      size_t designTaskCount = 0;
      for (const ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = (UINT64_C(1) << 63) | process.token;
        if (!process.instance || !contains(token) || token == root)
          continue;
        if (process.callers.size() == std::numeric_limits<size_t>::max())
          talos_rt_out_of_memory();
        size_t count = process.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - nativeActivationCount)
          talos_rt_out_of_memory();
        nativeActivationCount += count;
        ++nativeTaskCount;
      }
      for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || !contains(task.id) || task.id == root)
          continue;
        if (task.callers.size() == std::numeric_limits<size_t>::max())
          talos_rt_out_of_memory();
        size_t count = task.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - designActivationCount)
          talos_rt_out_of_memory();
        designActivationCount += count;
        ++designTaskCount;
      }
      nativeInstances.reserve(nativeActivationCount);
      designTasks.reserve(designActivationCount);
      insertedNativeTerminations.reserve(nativeTaskCount);
      insertedDesignTerminations.reserve(designTaskCount);
      insertedNativeKills.reserve(nativeTaskCount);
      insertedDesignKills.reserve(designTaskCount);
      killedParents.reserve(checkedSizeSum(nativeTaskCount, designTaskCount));
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
      TALOS_RT_TRY {
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
      TALOS_RT_CATCH_ALL {
        for (uint64_t token : insertedNativeTerminations)
          context->terminatedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignTerminations)
          context->terminatedDesignTasks.erase(token);
        for (uint64_t token : insertedNativeKills)
          context->killedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignKills)
          context->killedDesignTasks.erase(token);
        TALOS_RT_RETHROW;
      }
      for (ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = (UINT64_C(1) << 63) | process.token;
        if (!process.instance || !contains(token) || token == root)
          continue;
        nativeInstances.push_back(process.instance);
        nativeInstances.insert(nativeInstances.end(), process.callers.begin(),
                               process.callers.end());
        process.callers.clear();
        process.callerDepths.clear();
        talos_rt_unregister_signal_wait_unlocked(
            context, process.signalSubscriptions, process.token, false);
        if (context->logicalProcessParentsWithChildren.erase(token))
          killedParents.emplace_back(token, process.parent);
        if (context->vpiState)
          (void)talos_rt_vpi_frame_unwind_process(context, token, 0);
        process.instance = nullptr;
        ++context->schedulerDeadProcessCount;
        context->schedulerCompactionPending = true;
        talos_rt_release_controls_unlocked(context, process.controls);
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
        talos_rt_unregister_signal_wait_unlocked(
            context, task.signalSubscriptions, task.id, true);
        if (context->logicalProcessParentsWithChildren.erase(task.id))
          killedParents.emplace_back(task.id, task.parent);
        talos_rt_release_controls_unlocked(context, task.controls);
        task.controls.clear();
        if (context->vpiState)
          (void)talos_rt_vpi_frame_unwind_process(context, task.id, 0);
        task.terminated = true;
        ++context->schedulerDeadDesignTaskCount;
        context->schedulerCompactionPending = true;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
      }
      for (auto [killed, parent] : killedParents)
        talos_rt_record_terminated_parent_unlocked(context, killed, parent);
      if (!nativeInstances.empty() || !designTasks.empty())
        if (++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
    }

    talos_rt_status result = TALOS_RT_OK;
    if (!designTasks.empty()) {
      if (!context->execution)
        result = TALOS_RT_INVALID_LIFECYCLE;
      else {
        talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
        Image image;
        if (!loadValidatedImage(entry, context, image))
          result = TALOS_RT_INVALID_BYTECODE;
        else
          for (const CancelledDesignTask &task : designTasks) {
            talos_rt_status status = releaseCapturedAutomaticStates(
                image, task.function, context, task.frame.data(),
                task.scratchOffset);
            if (result == TALOS_RT_OK && status != TALOS_RT_OK)
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
    for (talos_rt_process_instance_v1 *instance : nativeInstances) {
      talos_rt_status status =
          talos_rt_v1_process_instance_destroy(instance);
      if (result == TALOS_RT_OK && status != TALOS_RT_OK)
        result = status;
    }
    return result;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

static talos_rt_status controlDisable(talos_rt_context *context,
                                        uint64_t targetID, uint64_t activation,
                                        uint32_t allActivations,
                                        uint64_t *outSelected) {
  if (outSelected)
    *outSelected = 0;
  uint64_t selected = 0;
  if (!context || targetID == 0 || allActivations > 1)
    return TALOS_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  struct CancelledDesignTask {
    uint64_t id;
    uint32_t function;
    uint64_t scratchOffset;
    bool releaseOwnedStates;
    std::vector<uint8_t> frame;
  };
  TALOS_RT_TRY {
    std::vector<uint64_t> targets;
    std::vector<talos_rt_process_instance_v1 *> nativeInstances;
    std::vector<CancelledDesignTask> designTasks;
    std::vector<uint64_t> insertedNativeTerminations;
    std::vector<uint64_t> insertedDesignTerminations;
    std::vector<uint64_t> insertedNativeKills;
    std::vector<uint64_t> insertedDesignKills;
    std::vector<std::pair<uint64_t, uint64_t>> killedParents;
    bool resumedControl = false;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      invalidateDesignReadyCohort(context);
      uint64_t current = context->activeLogicalProcessToken;
      if (current == 0)
        return TALOS_RT_INVALID_LIFECYCLE;
      context->controlEscapePending = false;
      if (activation != 0) {
        auto found = context->controlActivations.find(activation);
        if (found == context->controlActivations.end() ||
            found->second.target != targetID ||
            std::find(context->activeControls.begin(),
                      context->activeControls.end(),
                      activation) == context->activeControls.end())
          return TALOS_RT_INVALID_LIFECYCLE;
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
      talos_rt_cancel_deferred_immediate_assertion_unlocked(
          context, targetID, allActivations ? 0 : current);
      if (targets.empty())
        return TALOS_RT_OK;
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
        if ((dpiLogical & TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG) != 0) {
          uint64_t token =
              dpiLogical & ~uint64_t{TALOS_RT_NATIVE_LOGICAL_PROCESS_TAG};
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
          if (targetControls.count(controls[index]))
            return index;
        return std::nullopt;
      };
      auto nativeUnwind = [&](const ScheduledProcess &process)
          -> std::optional<UnwindBoundary> {
        std::optional<size_t> control = targetedControl(process.controls);
        if (!control || process.callerDepths.size() != process.callers.size())
          return std::nullopt;
        for (size_t index = 0; index != process.callerDepths.size(); ++index)
          if (process.callerDepths[index].controlDepth == *control)
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
        for (size_t index = 0; index != process.callerDepths.size(); ++index)
          if (control < process.callerDepths[index].controlDepth) {
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
        if (targetControls.count(context->activeControls[index])) {
          trim = index;
          break;
        }
      if (trim != context->activeControls.size())
        selected = context->activeControls[trim];
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
            if (scheduled->callerDepths.size() != scheduled->callers.size())
              return TALOS_RT_INVALID_LIFECYCLE;
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
            return TALOS_RT_INVALID_LIFECYCLE;
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
        if (process.callerDepths.size() != process.callers.size())
          return TALOS_RT_INVALID_LIFECYCLE;
        if (std::optional<ResumeBoundary> resume =
                resumeBoundary(token, process.controls)) {
          resumedControl = true;
          size_t owner = nativeResumeOwner(process, resume->control);
          size_t count = process.callers.size() - owner;
          if (count >
              std::numeric_limits<size_t>::max() - nativeActivationCount)
            talos_rt_out_of_memory();
          nativeActivationCount += count;
          continue;
        }
        std::optional<UnwindBoundary> unwind = nativeUnwind(process);
        if (!unwind &&
            process.callers.size() == std::numeric_limits<size_t>::max())
          talos_rt_out_of_memory();
        size_t count = unwind ? process.callers.size() - unwind->caller
                              : process.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - nativeActivationCount)
          talos_rt_out_of_memory();
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
            talos_rt_out_of_memory();
          designActivationCount += count;
          continue;
        }
        std::optional<UnwindBoundary> unwind = designUnwind(task);
        if (!unwind &&
            task.callers.size() == std::numeric_limits<size_t>::max())
          talos_rt_out_of_memory();
        size_t count = unwind ? task.callers.size() - unwind->caller
                              : task.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - designActivationCount)
          talos_rt_out_of_memory();
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
      killedParents.reserve(checkedSizeSum(nativeTaskCount, designTaskCount));
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
      TALOS_RT_TRY {
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
      TALOS_RT_CATCH_ALL {
        for (uint64_t token : insertedNativeTerminations)
          context->terminatedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignTerminations)
          context->terminatedDesignTasks.erase(token);
        for (uint64_t token : insertedNativeKills)
          context->killedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignKills)
          context->killedDesignTasks.erase(token);
        TALOS_RT_RETHROW;
      }

      // Control identities retain the frame depth from before their own scope
      // activation. A fork can inherit a foreign control; disabling it removes
      // that descendant's entire frame stack instead of borrowing the owner's
      // unrelated depth.
      auto unwindControlFrames = [&](uint64_t logicalProcess,
                                     const std::vector<uint64_t> &controls,
                                     size_t index) -> talos_rt_status {
        if (!context->vpiState)
          return TALOS_RT_OK;
        if (index >= controls.size())
          return TALOS_RT_INVALID_LIFECYCLE;
        auto found = targetControls.find(controls[index]);
        if (found == targetControls.end())
          return TALOS_RT_INVALID_LIFECYCLE;
        uint64_t depth = found->second.owner == logicalProcess
                             ? found->second.vpiFrameDepth
                             : 0;
        return talos_rt_vpi_frame_unwind_process(context, logicalProcess,
                                                   depth);
      };
      if (!cancelCurrent && trim != context->activeControls.size()) {
        auto status =
            unwindControlFrames(current, context->activeControls, trim);
        if (status != TALOS_RT_OK)
          return status;
      }

      // Clause 16.4 flushes reports when the outermost enclosing process
      // scope is disabled. A canceled observer has no static exit edge and
      // likewise cannot retain a pending report.
      if (cancelCurrent || trim == 0)
        talos_rt_flush_deferred_immediate_reports_unlocked(context, current);

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
            return TALOS_RT_INVALID_LIFECYCLE;
          talos_rt_process_instance_v1 *owner =
              scheduled->callers[activeEscape->owner];
          if (owner->native_handle) {
            owner->descriptor->native_destroy(owner);
            if (owner->native_handle)
              return TALOS_RT_INVALID_LIFECYCLE;
          }
          owner->continuation = activeEscape->continuation;
          for (size_t index = scheduled->callers.size();
               index != activeEscape->owner + 1; --index)
            nativeInstances.push_back(scheduled->callers[index - 1]);
          scheduled->callers.resize(activeEscape->owner + 1);
          scheduled->callerDepths.resize(activeEscape->owner + 1);
          if (context->vpiState) {
            auto status = talos_rt_vpi_frame_depth_process(
                context, current,
                &scheduled->callerDepths.back().vpiFrameDepth);
            if (status != TALOS_RT_OK)
              return status;
          }
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
          if (context->vpiState) {
            auto status = talos_rt_vpi_frame_depth_process(
                context, current, &task.callers.back().vpiFrameDepth);
            if (status != TALOS_RT_OK)
              return status;
          }
        }
      }
      if (currentNonlocalExit)
        context->controlEscapePending = true;

      if (!cancelCurrent && trim != context->activeControls.size()) {
        for (size_t index = trim; index != context->activeControls.size();
             ++index)
          talos_rt_release_control_unlocked(context,
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
          auto status =
              unwindControlFrames(token, process.controls, resume->control);
          if (status != TALOS_RT_OK)
            return status;
          if (resume->control == 0)
            talos_rt_flush_deferred_immediate_reports_unlocked(context,
                                                                 token);
          size_t owner = nativeResumeOwner(process, resume->control);
          talos_rt_process_instance_v1 *selected = process.instance;
          if (owner != process.callers.size()) {
            selected = process.callers[owner];
            nativeInstances.push_back(process.instance);
            for (size_t index = process.callers.size(); index != owner + 1;
                 --index)
              nativeInstances.push_back(process.callers[index - 1]);
            process.instance = selected;
            process.callers.resize(owner);
            process.callerDepths.resize(owner);
          }
          if (selected->native_handle) {
            selected->descriptor->native_destroy(selected);
            if (selected->native_handle)
              return TALOS_RT_INVALID_LIFECYCLE;
          }
          selected->continuation = resume->continuation;
          if (process.aotActorSlot != UINT32_MAX) {
            uint32_t slot = process.aotActorSlot;
            if (!context->nativeSchedulePlan ||
                slot >= context->nativeScheduleActors.size())
              return TALOS_RT_INVALID_LIFECYCLE;
            talos_rt_status status = context->nativeSchedulePlan->bind(
                context->nativeSchedulePlan->mutable_state, context, slot,
                selected);
            if (status != TALOS_RT_OK)
              return status;
            context->nativeScheduleActors[slot] = selected;
            context->nativeScheduleActorTokens[slot] = process.token;
          }
          talos_rt_unregister_signal_wait_unlocked(
              context, process.signalSubscriptions, process.token, false);
          for (size_t index = resume->control; index != process.controls.size();
               ++index)
            talos_rt_release_control_unlocked(context,
                                                process.controls[index]);
          process.controls.resize(resume->control);
          process.suspendKind = TALOS_RT_SUSPEND_NONE;
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
        talos_rt_flush_deferred_immediate_reports_unlocked(context, token);
        if (std::optional<UnwindBoundary> unwind = nativeUnwind(process)) {
          auto status =
              unwindControlFrames(token, process.controls, unwind->control);
          if (status != TALOS_RT_OK)
            return status;
          talos_rt_process_instance_v1 *caller =
              process.callers[unwind->caller];
          if (process.aotActorSlot != UINT32_MAX) {
            uint32_t slot = process.aotActorSlot;
            if (!context->nativeSchedulePlan ||
                slot >= context->nativeScheduleActors.size())
              return TALOS_RT_INVALID_LIFECYCLE;
            talos_rt_status status = context->nativeSchedulePlan->bind(
                context->nativeSchedulePlan->mutable_state, context, slot,
                caller);
            if (status != TALOS_RT_OK)
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
          process.callerDepths.resize(unwind->caller);
          talos_rt_unregister_signal_wait_unlocked(
              context, process.signalSubscriptions, process.token, false);
          for (size_t index = unwind->control; index != process.controls.size();
               ++index)
            talos_rt_release_control_unlocked(context,
                                                process.controls[index]);
          process.controls.resize(unwind->control);
          process.suspendKind = TALOS_RT_SUSPEND_NONE;
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
        process.callerDepths.clear();
        talos_rt_unregister_signal_wait_unlocked(
            context, process.signalSubscriptions, process.token, false);
        if (context->logicalProcessParentsWithChildren.erase(token))
          killedParents.emplace_back(token, process.parent);
        if (context->vpiState)
          (void)talos_rt_vpi_frame_unwind_process(context, token, 0);
        process.instance = nullptr;
        ++context->schedulerDeadProcessCount;
        context->schedulerCompactionPending = true;
        if (token == current) {
          talos_rt_release_controls_unlocked(context,
                                               context->activeControls);
          context->activeControls.clear();
        } else {
          talos_rt_release_controls_unlocked(context, process.controls);
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
          auto status =
              unwindControlFrames(task.id, task.controls, resume->control);
          if (status != TALOS_RT_OK)
            return status;
          if (resume->control == 0)
            talos_rt_flush_deferred_immediate_reports_unlocked(context,
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
          talos_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
          for (size_t index = resume->control; index != task.controls.size();
               ++index)
            talos_rt_release_control_unlocked(context, task.controls[index]);
          task.controls.resize(resume->control);
          task.suspendKind = TALOS_RT_SUSPEND_NONE;
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
        talos_rt_flush_deferred_immediate_reports_unlocked(context, task.id);
        if (std::optional<UnwindBoundary> unwind = designUnwind(task)) {
          auto status =
              unwindControlFrames(task.id, task.controls, unwind->control);
          if (status != TALOS_RT_OK)
            return status;
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
          task.vpiSourceCallID = caller.vpiSourceCallID;
          task.scheduleRank = caller.scheduleRank;
          talos_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
          for (size_t index = unwind->control; index != task.controls.size();
               ++index)
            talos_rt_release_control_unlocked(context, task.controls[index]);
          task.controls.resize(unwind->control);
          task.suspendKind = TALOS_RT_SUSPEND_NONE;
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
        talos_rt_unregister_signal_wait_unlocked(
            context, task.signalSubscriptions, task.id, true);
        if (context->logicalProcessParentsWithChildren.erase(task.id))
          killedParents.emplace_back(task.id, task.parent);
        if (task.id == current) {
          talos_rt_release_controls_unlocked(context,
                                               context->activeControls);
          context->activeControls.clear();
        } else {
          talos_rt_release_controls_unlocked(context, task.controls);
        }
        task.controls.clear();
        if (context->vpiState)
          (void)talos_rt_vpi_frame_unwind_process(context, task.id, 0);
        task.terminated = true;
        ++context->schedulerDeadDesignTaskCount;
        context->schedulerCompactionPending = true;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
      }
      for (auto [killed, parent] : killedParents)
        talos_rt_record_terminated_parent_unlocked(context, killed, parent);
      if (resumedControl || !nativeInstances.empty() || !designTasks.empty())
        if (++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
    }

    talos_rt_status result = TALOS_RT_OK;
    if (!designTasks.empty()) {
      if (!context->execution)
        result = TALOS_RT_INVALID_LIFECYCLE;
      else {
        talos_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
        Image image;
        if (!loadValidatedImage(entry, context, image))
          result = TALOS_RT_INVALID_BYTECODE;
        else
          for (const CancelledDesignTask &task : designTasks) {
            talos_rt_status status = releaseCapturedAutomaticStates(
                image, task.function, context, task.frame.data(),
                task.scratchOffset);
            if (result == TALOS_RT_OK && status != TALOS_RT_OK)
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
    for (talos_rt_process_instance_v1 *instance : nativeInstances) {
      talos_rt_status status =
          talos_rt_v1_process_instance_destroy(instance);
      if (result == TALOS_RT_OK && status != TALOS_RT_OK)
        result = status;
    }
    if (result == TALOS_RT_OK && outSelected)
      *outSelected = selected;
    return result;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

extern "C" talos_rt_status
talos_rt_v1_control_disable(talos_rt_context *context, uint64_t targetID,
                              uint64_t activation, uint32_t allActivations) {
  return controlDisable(context, targetID, activation, allActivations, nullptr);
}

extern "C" talos_rt_status talos_rt_v1_control_disable_selected(
    talos_rt_context *context, uint64_t targetID, uint64_t activation,
    uint32_t allActivations, uint64_t *outSelected) {
  if (!outSelected)
    return TALOS_RT_INVALID_ARGUMENT;
  return controlDisable(context, targetID, activation, allActivations,
                        outSelected);
}

struct DesignTaskReadiness {
  bool runnable = false;
  bool signalTriggered = false;
};

// This is the canonical readiness predicate shared by scheduler selection and
// cold VPI inspection. Inspection suppresses diagnostics and error-state
// mutation, but evaluates the same wait sources as execution.
__attribute__((always_inline)) static inline talos_rt_status
inspectDesignTaskReadiness(talos_rt_context *context,
                           const ScheduledDesignTask &task,
                           uint32_t activePhase, uint32_t unstartedActorRegion,
                           bool recordSchedulerEffects,
                           DesignTaskReadiness &result) {
  result = {};
  if (task.phase != activePhase ||
      !schedulerRegionEligible(context, task.queuedRegion))
    return TALOS_RT_OK;

  bool awaited = false;
  bool childrenDone = false;
  bool eventTriggered = false;
  bool eventOrderReady = task.waitOrderReady;
  bool mailboxReady = false;
  bool semaphoreReady = false;
  result.signalTriggered =
      task.signalTriggered || ((task.suspendKind == TALOS_RT_SUSPEND_CHANGE ||
                                task.suspendKind == TALOS_RT_SUSPEND_EDGE) &&
                               task.signalLatch && task.signalLatch->triggered);
  if (recordSchedulerEffects && context->signalDiagnosticsEnabled &&
      task.started &&
      (task.suspendKind == TALOS_RT_SUSPEND_CHANGE ||
       task.suspendKind == TALOS_RT_SUSPEND_EDGE ||
       task.suspendKind == TALOS_RT_SUSPEND_OBSERVER))
    ++context->signalDiagnostics.readinessCalls;

  if (task.started &&
      (task.suspendKind == TALOS_RT_SUSPEND_EVENT ||
       task.suspendKind == TALOS_RT_SUSPEND_EVENT_ORDER ||
       task.suspendKind == TALOS_RT_SUSPEND_MAILBOX ||
       task.suspendKind == TALOS_RT_SUSPEND_SEMAPHORE ||
       task.suspendKind == TALOS_RT_SUSPEND_AWAIT ||
       task.suspendKind == TALOS_RT_SUSPEND_JOIN) &&
      task.waitSize >= sizeof(talos_rt_wait_record_v1) &&
      task.waitOffset <= task.scratchOffset &&
      task.waitSize <= task.scratchOffset - task.waitOffset) {
    const auto *wait = reinterpret_cast<const talos_rt_wait_record_v1 *>(
        task.frame.data() + task.waitOffset);
    const auto *entries = reinterpret_cast<const talos_rt_wait_entry_v1 *>(
        reinterpret_cast<const uint8_t *>(wait) + sizeof(*wait));
    if (task.suspendKind == TALOS_RT_SUSPEND_EVENT) {
      if (task.waitGenerations.size() == wait->count)
        for (uint32_t index = 0; index != wait->count; ++index) {
          auto event = context->events.find(entries[index].stable_id);
          uint64_t generation =
              event == context->events.end() ? 0 : event->second.generation;
          eventTriggered |= generation != task.waitGenerations[index];
        }
    } else if (task.suspendKind == TALOS_RT_SUSPEND_EVENT_ORDER) {
      eventOrderReady = task.waitOrderReady;
    } else if (task.suspendKind == TALOS_RT_SUSPEND_MAILBOX) {
      if (wait->count == 1) {
        talos_rt_status status = talos_rt_mailbox_wait_ready(
            talos_rt_object_from_managed_word(entries[0].stable_id),
            wait->flags, mailboxReady);
        if (status != TALOS_RT_OK) {
          if (recordSchedulerEffects)
            context->schedulerStatus = status;
          return status;
        }
      }
    } else if (task.suspendKind == TALOS_RT_SUSPEND_SEMAPHORE) {
      if (wait->count == 1 && wait->payload <= UINT32_MAX) {
        talos_rt_status status = talos_rt_semaphore_wait_ready(
            context, talos_rt_object_from_managed_word(entries[0].stable_id),
            static_cast<int32_t>(wait->payload), task.waitSequence,
            semaphoreReady);
        if (status != TALOS_RT_OK) {
          if (recordSchedulerEffects)
            context->schedulerStatus = status;
          return status;
        }
      }
    } else if (task.suspendKind == TALOS_RT_SUSPEND_AWAIT) {
      awaited = wait->count == 1 && talos_rt_logical_process_terminated(
                                        context, entries[0].stable_id);
    } else if (wait->count != 0) {
      awaited = wait->flags == 0;
      if (wait->flags == 0)
        for (uint32_t index = 0; index != wait->count; ++index)
          awaited &= talos_rt_logical_process_terminated(
              context, entries[index].stable_id);
      else
        for (uint32_t index = 0; index != wait->count; ++index)
          awaited |= talos_rt_logical_process_terminated(
              context, entries[index].stable_id);
    }
  }

  if (task.started && task.suspendKind == TALOS_RT_SUSPEND_CHILDREN) {
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
       childrenDone || task.suspendKind == TALOS_RT_SUSPEND_NONE ||
       (task.suspendKind == TALOS_RT_SUSPEND_DELAY
            ? task.wakeTime <= context->schedulerTime
            : (task.suspendKind != TALOS_RT_SUSPEND_CHANGE &&
               task.suspendKind != TALOS_RT_SUSPEND_EDGE &&
               task.suspendKind != TALOS_RT_SUSPEND_EVENT &&
               task.suspendKind != TALOS_RT_SUSPEND_EVENT_ORDER &&
               task.suspendKind != TALOS_RT_SUSPEND_MAILBOX &&
               task.suspendKind != TALOS_RT_SUSPEND_SEMAPHORE &&
               task.suspendKind != TALOS_RT_SUSPEND_AWAIT &&
               task.suspendKind != TALOS_RT_SUSPEND_JOIN &&
               task.suspendKind != TALOS_RT_SUSPEND_FOREVER &&
               task.suspendKind != TALOS_RT_SUSPEND_CHILDREN &&
               task.suspendKind != TALOS_RT_SUSPEND_OBSERVER &&
               task.observedEpoch != context->schedulerEpoch)));
  if (result.runnable && task.queuedRegion >= unstartedActorRegion &&
      result.signalTriggered && !task.urgent && !task.prioritySignal)
    result.runnable = false;
  return TALOS_RT_OK;
}

PostObservedPromotion talos_rt_promote_post_observed_design_task_unlocked(
    talos_rt_context *context, uint64_t taskID) {
  auto indexed = context->scheduledDesignTaskIndices.find(taskID);
  if (indexed == context->scheduledDesignTaskIndices.end() ||
      indexed->second >= context->scheduledDesignTasks.size())
    return PostObservedPromotion::Stale;
  ScheduledDesignTask &task = context->scheduledDesignTasks[indexed->second];
  if (task.id != taskID || task.terminated || !task.postObserved ||
      task.queuedRegion != TALOS_RT_REGION_OBSERVED)
    return PostObservedPromotion::Stale;
  uint32_t activePhase = context->schedulerRunningFinals ? 1u : 0u;
  DesignTaskReadiness readiness;
  if (inspectDesignTaskReadiness(
          context, task, activePhase,
          talos_rt_peek_unstarted_actor_region(context, activePhase), false,
          readiness) != TALOS_RT_OK ||
      !readiness.runnable)
    return PostObservedPromotion::Waiting;
  task.queuedRegion = task.homeRegion;
  task.postObserved = false;
  context->designPollCandidates.insert(taskID);
  return PostObservedPromotion::Promoted;
}

bool talos_rt_design_task_pending_before_read_only_unlocked(
    talos_rt_context *context) {
  if (!context || context->designTaskExecuting)
    return false;
  uint32_t activePhase = context->schedulerRunningFinals ? 1u : 0u;
  uint32_t unstartedActorRegion =
      talos_rt_peek_unstarted_actor_region(context, activePhase);
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
                                   readiness) != TALOS_RT_OK ||
        !readiness.runnable)
      continue;
    if (task.urgent ||
        designTaskOrderingRegion(task, readiness.signalTriggered) <=
            TALOS_RT_REGION_POSTPONED)
      return true;
  }
  return false;
}

template <bool EnableReadyCohort>
__attribute__((always_inline)) inline talos_rt_status
runOneDesignTaskImpl(talos_rt_context *context, uint32_t maximumRegion,
                     uint32_t maximumRank, uint64_t maximumInsertionSequence,
                     bool *outProgress, bool stopBeforeExecution) noexcept {
  if (!context || !outProgress)
    return TALOS_RT_INVALID_ARGUMENT;
  *outProgress = false;
  ScheduledDesignTask task;
  bool taskDequeued = false;
  bool currentFrameReleased = false;
  auto abandonTask = [&](talos_rt_status failure) noexcept {
    TALOS_RT_TRY {
      if (taskDequeued && context->execution) {
        talos_rt_design_bytecode_entry_v1 entry{context->execution,
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
    TALOS_RT_CATCH_ALL {}
    TALOS_RT_TRY {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      talos_rt_unregister_signal_wait_unlocked(
          context, task.signalSubscriptions, task.id, true);
      if (taskDequeued) {
        if (context->vpiState)
          (void)talos_rt_vpi_frame_unwind_process(context, task.id, 0);
        task.controls = std::move(context->activeControls);
        talos_rt_release_controls_unlocked(context, task.controls);
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
    TALOS_RT_CATCH_ALL {}
    if (taskDequeued) {
      context->designTaskFrames.release(std::move(task.frame));
      for (DesignActivation &activation : task.callers)
        context->designTaskFrames.release(std::move(activation.frame));
    }
    return failure;
  };
  TALOS_RT_TRY {
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->designTaskExecuting)
        return TALOS_RT_OK;
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
          talos_rt_unstarted_actor_region(context, activePhase);
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
                                           ->talos_rt_status {
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
          talos_rt_status readinessStatus = inspectDesignTaskReadiness(
              context, *iterator, context->schedulerRunningFinals ? 1u : 0u,
              unstartedActorRegion, true, readiness);
          if (readinessStatus != TALOS_RT_OK)
            return readinessStatus;
          bool runnable = readiness.runnable;
          bool signalTriggered = readiness.signalTriggered;
          uint32_t orderingRegion =
              designTaskOrderingRegion(*iterator, signalTriggered);
          auto key = iterator->prioritySignal && signalTriggered
                         ? std::tuple{orderingRegion, uint32_t{0}, uint64_t{0}}
                         : std::tuple{orderingRegion,
                                      talos_rt_scheduler_rank(*iterator),
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
        return TALOS_RT_OK;
      };
      if (directExactScan) {
        talos_rt_status status = scanExactDesignCandidates();
        if (status != TALOS_RT_OK)
          return status;
      }
      bool cachedSlowMembershipValid = true;
      auto scanDesignCandidates = [&](const auto &candidates,
                                      auto scanMode) -> talos_rt_status {
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
          talos_rt_status readinessStatus =
              inspectDesignTaskReadiness(context, *iterator, activePhase,
                                         unstartedActorRegion, true, readiness);
          if (readinessStatus != TALOS_RT_OK)
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
                         : std::tuple{orderingRegion,
                                      talos_rt_scheduler_rank(*iterator),
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
        return TALOS_RT_OK;
      };
      talos_rt_status scanStatus = TALOS_RT_OK;
      if (selectedFromReadyCohort)
        scanStatus = scanDesignCandidates(
            readyCohort->slowCandidates, std::integral_constant<unsigned, 2>{});
      else if (collectReadyCohort)
        scanStatus =
            scanDesignCandidates(context->designPollCandidates,
                                 std::integral_constant<unsigned, 1>{});
      if (scanStatus != TALOS_RT_OK)
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
        if (scanStatus != TALOS_RT_OK)
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
        return TALOS_RT_OK;
      if (stopBeforeExecution)
        // LRM 38.34/4.9.4: dynamic task effects are not a scoped native
        // certificate. Export the enclosing eval captures before execution,
        // leaving this exact selected continuation and its frame intact.
        return TALOS_RT_TIER_UNAVAILABLE;
      if (found->suspendKind == TALOS_RT_SUSPEND_SEMAPHORE) {
        const auto *wait = reinterpret_cast<const talos_rt_wait_record_v1 *>(
            found->frame.data() + found->waitOffset);
        bool acquired = false;
        talos_rt_status status =
            talos_rt_semaphore_wait_acquire(wait, acquired);
        if (status != TALOS_RT_OK) {
          context->schedulerStatus = status;
          return status;
        }
        if (!acquired)
          return TALOS_RT_OK;
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
      if (found->postObserved &&
          found->queuedRegion == TALOS_RT_REGION_OBSERVED)
        talos_rt_promote_post_observed_resumptions_unlocked(context);
      task = std::move(context->scheduledDesignTasks[selectedIndex]);
      if (!context->schedulerRunningFinals &&
          isReactiveSchedulerRegion(task.queuedRegion))
        setSchedulerDrainingReactive(context, true);
      bool resuming =
          task.started && task.suspendKind != TALOS_RT_SUSPEND_NONE;
      if (!task.started)
        talos_rt_unregister_unstarted_actor(context, task.phase, task.id);
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
          resuming && task.suspendKind == TALOS_RT_SUSPEND_EVENT_ORDER &&
          task.waitOrderReady && task.waitOrderFailed;
      if (resuming &&
          talos_rt_resume_flushes_deferred_reports(task.suspendKind))
        talos_rt_flush_deferred_immediate_reports_unlocked(context, task.id);
      context->activeControls = std::move(task.controls);
    }
    talos_rt_design_bytecode_entry_v1 entry{context->execution, task.function,
                                              0};
    talos_rt_fragment_action_v1 action{};
    std::unique_ptr<PendingDesignActivation> pendingActivation;
    talos_rt_status status = executeDesignBytecode(
        entry, context, task.frame.data(), task.frame.size(),
        task.scratchOffset, task.scratchSize, task.continuation, 0, &action,
        &pendingActivation);
    currentFrameReleased =
        status == TALOS_RT_OK && action.kind == TALOS_RT_FRAGMENT_TERMINATE;
    bool terminationRequested =
        context->schedulerFinishRequested.load(std::memory_order_relaxed) != 0;
    bool killRequested = false;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      killRequested |= context->killedDesignTasks.count(task.id) != 0;
    }
    if (status != TALOS_RT_OK) [[unlikely]] {
      if (status == TALOS_RT_DPI_DISABLE_UNSUPPORTED) {
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
        return abandonTask(TALOS_RT_INVALID_BYTECODE);
      if (!currentFrameReleased) {
        talos_rt_status releaseStatus = releaseCapturedAutomaticStates(
            image, task.function, context, task.frame.data(),
            task.scratchOffset);
        currentFrameReleased = true;
        if (releaseStatus != TALOS_RT_OK)
          return abandonTask(releaseStatus);
      }
      while (!task.callers.empty()) {
        DesignActivation &activation = task.callers.back();
        talos_rt_status releaseStatus = releaseCapturedAutomaticStates(
            image, activation.function, context, activation.frame.data(),
            activation.scratchOffset);
        task.callers.pop_back();
        if (releaseStatus != TALOS_RT_OK)
          return abandonTask(releaseStatus);
      }
      action = {
          TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
      status = TALOS_RT_OK;
    }
    std::optional<uint32_t> nextScheduleRank;
    if (action.kind != TALOS_RT_FRAGMENT_TERMINATE) {
      Image image;
      if (!loadValidatedImage(entry, context, image))
        return abandonTask(TALOS_RT_INVALID_BYTECODE);
      nextScheduleRank = continuationScheduleRank(
          image, functionAt(image, task.function), action.continuation);
      if (!nextScheduleRank)
        return abandonTask(TALOS_RT_INVALID_CONTINUATION);
    }
    talos_rt_status finalizeStatus = TALOS_RT_OK;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      task.controls = std::move(context->activeControls);
      task.started = true;
      task.continuation = action.continuation;
      // A direct task activation is the same logical process. Preserve its
      // stable scheduler rank across the call stack; only a suspension in the
      // root activation advances to a graph continuation rank.
      if (nextScheduleRank && action.kind != TALOS_RT_FRAGMENT_TASK_CALL &&
          task.callers.empty())
        task.scheduleRank = *nextScheduleRank;
      task.observedEpoch = context->schedulerEpoch;
      switch (action.kind) {
      case TALOS_RT_FRAGMENT_CONTINUE:
        if (!task.signalSubscriptions.empty() ||
            task.computedObserverWaitRegistered)
          talos_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        task.suspendKind = TALOS_RT_SUSPEND_NONE;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
        task.urgent = task.startupProcess;
        task.queuedRegion = task.homeRegion;
        break;
      case TALOS_RT_FRAGMENT_SUSPEND: {
        if (action.suspend_kind == TALOS_RT_SUSPEND_OBSERVER) {
          if ((action.flags & ~kResumeActionFlags) != 0 ||
              action.payload % alignof(talos_rt_computed_wait_record_v1) !=
                  0 ||
              action.payload > task.scratchOffset ||
              sizeof(talos_rt_computed_wait_record_v1) >
                  task.scratchOffset - action.payload) {
            finalizeStatus = TALOS_RT_INVALID_FRAME;
            break;
          }
          auto *computed =
              reinterpret_cast<talos_rt_computed_wait_record_v1 *>(
                  task.frame.data() + action.payload);
          if (!talos_rt_validate_computed_wait_record(
                  context->execution, computed,
                  task.scratchOffset - action.payload)) {
            finalizeStatus = TALOS_RT_INVALID_FRAME;
            break;
          }
          task.suspendKind = action.suspend_kind;
          task.waitOffset = action.payload;
          task.waitSize = computed->total_size;
          task.waitGenerations.clear();
          task.signalTriggered = false;
          task.startupProcess = false;
          task.urgent = false;
          if (!talos_rt_next_queued_region(
                  task.homeRegion, action.suspend_kind, 1, action.flags,
                  task.queuedRegion, task.postObserved)) {
            finalizeStatus = TALOS_RT_INVALID_BYTECODE;
            break;
          }
          if (task.postObserved)
            context->postObservedResumptions.push_back(task.id);
          if (!task.signalSubscriptions.empty() ||
              task.computedObserverWaitRegistered)
            talos_rt_unregister_signal_wait_unlocked(
                context, task.signalSubscriptions, task.id, true);
          if (!talos_rt_register_computed_signal_wait_unlocked(
                  context, computed, task.id, true, task.signalSubscriptions,
                  task.signalLatch))
            finalizeStatus = context->schedulerStatus;
          break;
        }
        if (action.payload > task.scratchOffset ||
            sizeof(talos_rt_wait_record_v1) >
                task.scratchOffset - action.payload) {
          finalizeStatus = TALOS_RT_INVALID_FRAME;
          break;
        }
        const auto *wait = reinterpret_cast<const talos_rt_wait_record_v1 *>(
            task.frame.data() + action.payload);
        uint64_t entries =
            uint64_t{wait->count} * sizeof(talos_rt_wait_entry_v1);
        bool signalWait = action.suspend_kind == TALOS_RT_SUSPEND_CHANGE ||
                          action.suspend_kind == TALOS_RT_SUSPEND_EDGE;
        if (wait->version != TALOS_RT_VERSION ||
            wait->kind != action.suspend_kind ||
            entries > task.scratchOffset - action.payload -
                          sizeof(talos_rt_wait_record_v1)) {
          finalizeStatus = TALOS_RT_INVALID_FRAME;
          break;
        }
        const auto *waitEntries =
            reinterpret_cast<const talos_rt_wait_entry_v1 *>(wait + 1);
        uint32_t behaviorFlags =
            wait->flags & ~(TALOS_RT_WAIT_SUPPRESS_ACTIVE_SELF |
                            TALOS_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL |
                            TALOS_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS);
        bool suppressActiveSelf =
            (wait->flags & TALOS_RT_WAIT_SUPPRESS_ACTIVE_SELF) != 0;
        bool slotFinal =
            (wait->flags & TALOS_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL) != 0;
        bool mailboxWait = action.suspend_kind == TALOS_RT_SUSPEND_MAILBOX;
        bool semaphoreWait =
            action.suspend_kind == TALOS_RT_SUSPEND_SEMAPHORE;
        bool eventOrderWait =
            action.suspend_kind == TALOS_RT_SUSPEND_EVENT_ORDER;
        bool validFlags =
            mailboxWait
                ? wait->flags <= TALOS_RT_WAIT_MAILBOX_NOT_FULL
                : (wait->flags &
                   ~(TALOS_RT_WAIT_LEVEL_TRUE | TALOS_RT_WAIT_EDGE_IFF |
                     TALOS_RT_WAIT_SUPPRESS_ACTIVE_SELF |
                     TALOS_RT_WAIT_CLOCK_OCCURRENCE |
                     TALOS_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL |
                     TALOS_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS)) == 0 &&
                      (!suppressActiveSelf ||
                       (signalWait &&
                        (behaviorFlags == 0 ||
                         action.suspend_kind == TALOS_RT_SUSPEND_EDGE))) &&
                      (behaviorFlags == TALOS_RT_WAIT_FLAGS_NONE ||
                       (action.suspend_kind == TALOS_RT_SUSPEND_JOIN &&
                        behaviorFlags <= 1) ||
                       (action.suspend_kind == TALOS_RT_SUSPEND_CHANGE &&
                        behaviorFlags == TALOS_RT_WAIT_LEVEL_TRUE) ||
                       (action.suspend_kind == TALOS_RT_SUSPEND_EDGE &&
                        (behaviorFlags == TALOS_RT_WAIT_EDGE_IFF ||
                         behaviorFlags == TALOS_RT_WAIT_CLOCK_OCCURRENCE)));
        uint32_t occurrenceConditions =
            behaviorFlags == TALOS_RT_WAIT_CLOCK_OCCURRENCE
                ? static_cast<uint32_t>(__builtin_popcountll(wait->auxiliary))
                : 0;
        uint32_t occurrencePrimaries = occurrenceConditions < wait->count
                                           ? wait->count - occurrenceConditions
                                           : 0;
        bool validOccurrence =
            behaviorFlags != TALOS_RT_WAIT_CLOCK_OCCURRENCE ||
            (talos_rt_is_clock_occurrence_wait_flags(wait->flags) &&
             wait->payload != 0 && occurrencePrimaries >= 1 &&
             occurrencePrimaries <= 64 &&
             (occurrencePrimaries == 64 ||
              (wait->auxiliary >> occurrencePrimaries) == 0));
        if (!validFlags || !validOccurrence ||
            (slotFinal && behaviorFlags != TALOS_RT_WAIT_CLOCK_OCCURRENCE) ||
            (action.suspend_kind == TALOS_RT_SUSPEND_CHANGE &&
             behaviorFlags == TALOS_RT_WAIT_LEVEL_TRUE && wait->count != 1) ||
            (action.suspend_kind == TALOS_RT_SUSPEND_EDGE &&
             behaviorFlags == TALOS_RT_WAIT_EDGE_IFF && wait->count != 2) ||
            (action.suspend_kind == TALOS_RT_SUSPEND_FOREVER &&
             wait->count != 0) ||
            (eventOrderWait && (wait->count == 0 || wait->payload != 0 ||
                                wait->auxiliary != 0)) ||
            (mailboxWait && wait->count != 1) ||
            (semaphoreWait && (wait->flags != 0 || wait->count != 1 ||
                               wait->payload > UINT32_MAX ||
                               static_cast<int32_t>(wait->payload) < 0))) {
          finalizeStatus = TALOS_RT_INVALID_FRAME;
          break;
        }
        uint64_t waitSize = sizeof(talos_rt_wait_record_v1) + entries;
        bool hasObserverCondition = false;
        for (uint32_t index = 0; index != wait->count; ++index) {
          bool validEdge =
              waitEntries[index].edge >= TALOS_RT_WAIT_EDGE_CHANGE &&
              waitEntries[index].edge <= TALOS_RT_WAIT_EDGE_BOTH;
          bool iffCondition =
              behaviorFlags == TALOS_RT_WAIT_EDGE_IFF && index == 1;
          if (behaviorFlags == TALOS_RT_WAIT_CLOCK_OCCURRENCE)
            iffCondition = index >= occurrencePrimaries;
          bool validClockPrimary =
              validEdge || ((waitEntries[index].edge &
                             ~TALOS_RT_WAIT_EDGE_TRANSITION_CLASSES) ==
                                TALOS_RT_WAIT_EDGE_TRANSITION_MASK &&
                            (waitEntries[index].edge &
                             TALOS_RT_WAIT_EDGE_TRANSITION_CLASSES) != 0);
          bool validClockCondition =
              waitEntries[index].edge == TALOS_RT_WAIT_EDGE_NONE ||
              (waitEntries[index].edge >= TALOS_RT_WAIT_CONDITION_KNOWN_ONE &&
               waitEntries[index].edge <=
                   TALOS_RT_WAIT_CONDITION_CASE_NE_ONE) ||
              (waitEntries[index].edge >= TALOS_RT_WAIT_CONDITION_OBSERVER &&
               waitEntries[index].edge <=
                   TALOS_RT_WAIT_CONDITION_OBSERVER_LAST);
          bool observerCondition =
              iffCondition &&
              waitEntries[index].edge >= TALOS_RT_WAIT_CONDITION_OBSERVER &&
              waitEntries[index].edge <=
                  TALOS_RT_WAIT_CONDITION_OBSERVER_LAST;
          if (observerCondition) {
            const talos_rt_observer_descriptor_v1 *descriptor =
                talos::process::findObserverDescriptor(
                    context->execution, waitEntries[index].stable_id);
            uint64_t captureBytes = uint64_t{waitEntries[index].reserved} *
                                    sizeof(talos_rt_computed_capture_v1);
            if (!descriptor || descriptor->result_width != 1 ||
                descriptor->capture_count != waitEntries[index].reserved ||
                captureBytes > task.scratchOffset - action.payload - waitSize) {
              finalizeStatus = TALOS_RT_INVALID_FRAME;
              break;
            }
            waitSize += captureBytes;
            hasObserverCondition = true;
          }
          bool managed =
              signalWait && !iffCondition &&
              waitEntries[index].reserved == TALOS_RT_WAIT_WIDTH_MANAGED;
          talos_rt_stable_handle_v1 decodedSignal;
          bool validSignalHandle =
              !signalWait ||
              (observerCondition || managed
                   ? true
                   : waitEntries[index].stable_id == UINT64_MAX ||
                         talos_rt_stable_handle_decode(
                             waitEntries[index].stable_id, &decodedSignal));
          if (signalWait
                  ? (!validSignalHandle ||
                     (managed &&
                      waitEntries[index].edge != TALOS_RT_WAIT_EDGE_CHANGE) ||
                     (!(behaviorFlags == TALOS_RT_WAIT_CLOCK_OCCURRENCE
                            ? iffCondition ? validClockCondition
                                           : validClockPrimary
                            : validEdge || iffCondition)) ||
                     (behaviorFlags != TALOS_RT_WAIT_CLOCK_OCCURRENCE &&
                      iffCondition &&
                      waitEntries[index].edge != TALOS_RT_WAIT_EDGE_NONE) ||
                     (!managed && !observerCondition &&
                      waitEntries[index].reserved == 0))
                  : (waitEntries[index].edge != TALOS_RT_WAIT_EDGE_NONE ||
                     waitEntries[index].reserved != 0)) {
            finalizeStatus = TALOS_RT_INVALID_FRAME;
            break;
          }
        }
        if (finalizeStatus == TALOS_RT_OK &&
            (((wait->flags & TALOS_RT_WAIT_CLOCK_OCCURRENCE_OBSERVERS) !=
              0) != hasObserverCondition))
          finalizeStatus = TALOS_RT_INVALID_FRAME;
        if (finalizeStatus != TALOS_RT_OK)
          break;
        bool sameSignalWait =
            signalWait && (behaviorFlags == TALOS_RT_WAIT_CLOCK_OCCURRENCE
                               ? talos_rt_same_clock_occurrence_wait_unlocked(
                                     context, wait, task.id, true)
                               : hasSameDirectSignalWait(task, wait));
        if (!signalWait && (!task.signalSubscriptions.empty() ||
                            task.computedObserverWaitRegistered))
          talos_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        task.suspendKind = action.suspend_kind;
        task.waitOffset = action.payload;
        task.waitSize = waitSize;
        if (action.suspend_kind == TALOS_RT_SUSPEND_SEMAPHORE) {
          if (context->nextWaitSequence == 0) {
            finalizeStatus = TALOS_RT_OUT_OF_RESOURCES;
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
        if (!talos_rt_next_queued_region(
                task.homeRegion, action.suspend_kind, wait->payload,
                action.flags, task.queuedRegion, task.postObserved)) {
          finalizeStatus = TALOS_RT_INVALID_BYTECODE;
          break;
        }
        if (task.postObserved)
          context->postObservedResumptions.push_back(task.id);
        if (action.suspend_kind == TALOS_RT_SUSPEND_EVENT) {
          task.waitGenerations.reserve(wait->count);
          for (uint32_t index = 0; index != wait->count; ++index) {
            auto event = context->events.find(waitEntries[index].stable_id);
            task.waitGenerations.push_back(
                event == context->events.end() ? 0 : event->second.generation);
          }
        }
        if (action.suspend_kind == TALOS_RT_SUSPEND_EVENT_ORDER &&
            !talos_rt_initialize_event_order_wait_unlocked(
                context, wait, task.waitOrderIndex, task.waitOrderReady,
                task.waitOrderFailed)) {
          finalizeStatus = TALOS_RT_INVALID_FRAME;
          break;
        }
        if (action.suspend_kind == TALOS_RT_SUSPEND_DELAY)
          task.wakeTime = wait->payload > UINT64_MAX - context->schedulerTime
                              ? UINT64_MAX
                              : context->schedulerTime + wait->payload;
        if (signalWait && !sameSignalWait &&
            !talos_rt_register_signal_wait_unlocked(
                context, wait, task.signalSubscriptions, task.signalLatch,
                task.id, true))
          finalizeStatus = context->schedulerStatus;
        break;
      }
      case TALOS_RT_FRAGMENT_PROCESS_SUSPEND:
        if (!task.signalSubscriptions.empty() ||
            task.computedObserverWaitRegistered)
          talos_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        task.suspendKind = TALOS_RT_SUSPEND_NONE;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
        task.explicitlySuspended = true;
        task.urgent = false;
        task.queuedRegion = task.homeRegion;
        break;
      case TALOS_RT_FRAGMENT_TASK_CALL: {
        if (!pendingActivation) {
          finalizeStatus = TALOS_RT_INVALID_BYTECODE;
          break;
        }
        uint64_t frameDepth = 0;
        if (context->vpiState) {
          finalizeStatus =
              talos_rt_vpi_frame_depth_process(context, task.id, &frameDepth);
          if (finalizeStatus != TALOS_RT_OK)
            break;
        }
        task.callers.reserve(checkedSizeSum(task.callers.size(), 1));
        if (!task.signalSubscriptions.empty() ||
            task.computedObserverWaitRegistered)
          talos_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        task.callers.push_back(
            {task.function, task.continuation, std::move(task.frame),
             task.scratchOffset, task.scratchSize, task.scheduleRank,
             task.controls.size(), frameDepth, task.vpiSourceCallID});
        DesignActivation activation = std::move(pendingActivation->activation);
        task.function = activation.function;
        task.continuation = activation.continuation;
        task.frame = std::move(activation.frame);
        task.scratchOffset = activation.scratchOffset;
        task.scratchSize = activation.scratchSize;
        task.vpiSourceCallID = activation.vpiSourceCallID;
        task.suspendKind = TALOS_RT_SUSPEND_NONE;
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
      case TALOS_RT_FRAGMENT_TERMINATE:
        if (!task.signalSubscriptions.empty() ||
            task.computedObserverWaitRegistered)
          talos_rt_unregister_signal_wait_unlocked(
              context, task.signalSubscriptions, task.id, true);
        if (!task.callers.empty()) {
          if (context->vpiState) {
            finalizeStatus = talos_rt_vpi_frame_unwind_process(
                context, task.id, task.callers.back().vpiFrameDepth);
            if (finalizeStatus != TALOS_RT_OK)
              break;
          }
          DesignActivation caller = std::move(task.callers.back());
          task.callers.pop_back();
          context->designTaskFrames.release(std::move(task.frame));
          task.function = caller.function;
          task.continuation = caller.continuation;
          task.frame = std::move(caller.frame);
          task.scratchOffset = caller.scratchOffset;
          task.scratchSize = caller.scratchSize;
          task.vpiSourceCallID = caller.vpiSourceCallID;
          task.scheduleRank = caller.scheduleRank;
          task.suspendKind = TALOS_RT_SUSPEND_NONE;
          task.waitOffset = 0;
          task.waitSize = 0;
          task.waitGenerations.clear();
          task.signalTriggered = false;
          task.urgent = true;
          task.queuedRegion = task.homeRegion;
          currentFrameReleased = false;
        } else {
          talos_rt_program_complete_unlocked(context, task.id,
                                               task.programOwner);
          talos_rt_retire_process_parent_unlocked(context, task.id,
                                                    task.parent);
          context->terminatedDesignTasks.insert(task.id, task.random);
          releaseDesignTaskOwnedStatesUnlocked(context, task.id);
          talos_rt_release_controls_unlocked(context, task.controls);
          task.controls.clear();
          if (context->vpiState)
            (void)talos_rt_vpi_frame_unwind_process(context, task.id, 0);
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
        finalizeStatus = TALOS_RT_INVALID_BYTECODE;
        break;
      }
      if (finalizeStatus == TALOS_RT_OK) {
        if (task.terminated)
          context->designTaskFrames.release(std::move(task.frame));
        else {
          context->scheduledDesignTasks.push_back(std::move(task));
          uint64_t scheduledID = context->scheduledDesignTasks.back().id;
          TALOS_RT_TRY {
            context->scheduledDesignTaskIndices[scheduledID] =
                context->scheduledDesignTasks.size() - 1;
            if (!indexedSignalBlocked(context->scheduledDesignTasks.back()))
              context->designPollCandidates.insert(scheduledID);
          }
          TALOS_RT_CATCH_ALL {
            context->scheduledDesignTaskIndices.erase(scheduledID);
            context->designPollCandidates.erase(scheduledID);
            task = std::move(context->scheduledDesignTasks.back());
            context->scheduledDesignTasks.pop_back();
            TALOS_RT_RETHROW;
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
    if (finalizeStatus != TALOS_RT_OK)
      return abandonTask(finalizeStatus);
    *outProgress = true;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) {
    return abandonTask(TALOS_RT_OUT_OF_MEMORY);
  }
  TALOS_RT_CATCH_ALL { return abandonTask(TALOS_RT_INVALID_BYTECODE); }
}

TALOS_RT_FEATURE_TEXT talos_rt_status
runOneDesignTaskCohort(talos_rt_context *context, uint32_t maximumRegion,
                       uint32_t maximumRank, uint64_t maximumInsertionSequence,
                       bool *outProgress, bool stopBeforeExecution) noexcept {
  return runOneDesignTaskImpl<true>(context, maximumRegion, maximumRank,
                                    maximumInsertionSequence, outProgress,
                                    stopBeforeExecution);
}

talos_rt_status talos_rt_run_one_design_task(
    talos_rt_context *context, uint32_t maximumRegion, uint32_t maximumRank,
    uint64_t maximumInsertionSequence, bool *outProgress,
    bool stopBeforeExecution) noexcept {
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
                                  maximumInsertionSequence, outProgress,
                                  stopBeforeExecution);
  return runOneDesignTaskImpl<false>(context, maximumRegion, maximumRank,
                                     maximumInsertionSequence, outProgress,
                                     stopBeforeExecution);
}

talos_rt_status talos_rt_prime_design_task(talos_rt_context *context,
                                               uint64_t taskID) noexcept {
  if (!context || taskID == 0)
    return TALOS_RT_INVALID_ARGUMENT;
  struct ActiveStateGuard {
    talos_rt_context *context;
    talos_rt_process_instance_v1 *native = context->activeNativeProcess;
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
    talos_rt_random_state_v1 *random = context->activeRandom;
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
      talos_rt_set_design_task_filter_unlocked(context, designFilter,
                                                 forcedDesignTask);
    }
  } activeState{context};
  TALOS_RT_TRY {
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
    talos_rt_set_design_task_filter_unlocked(context, true, taskID);

    for (uint32_t step = 0; step != 1024; ++step) {
      bool progress = false;
      talos_rt_status status = talos_rt_run_one_design_task(
          context, UINT32_MAX, UINT32_MAX, UINT64_MAX, &progress);
      if (status != TALOS_RT_OK || !progress)
        return status != TALOS_RT_OK ? status : TALOS_RT_INVALID_LIFECYCLE;
      auto indexed = context->scheduledDesignTaskIndices.find(taskID);
      if (indexed == context->scheduledDesignTaskIndices.end() ||
          indexed->second >= context->scheduledDesignTasks.size())
        return TALOS_RT_INVALID_LIFECYCLE;
      const ScheduledDesignTask &task =
          context->scheduledDesignTasks[indexed->second];
      if (task.id != taskID || task.terminated || task.explicitlySuspended)
        return TALOS_RT_INVALID_LIFECYCLE;
      if (task.started && task.suspendKind != TALOS_RT_SUSPEND_NONE)
        return TALOS_RT_OK;
    }
    return TALOS_RT_OUT_OF_RESOURCES;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}
