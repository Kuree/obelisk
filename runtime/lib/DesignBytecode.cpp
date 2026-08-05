//===- DesignBytecode.cpp - Design-wide validated bytecode interpreter ----===//

#include "DesignBytecodeExecution.h"
#include "DesignBytecodeImage.h"
#include "DesignBytecodeLogic.h"
#include "DesignBytecodeNets.h"
#include "DesignBytecodeRoots.h"
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

namespace {

using namespace obelisk::designbytecode;

size_t checkedSizeSum(size_t lhs, size_t rhs) {
  if (rhs > std::numeric_limits<size_t>::max() - lhs)
    throw std::bad_alloc();
  return lhs + rhs;
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
    try {
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
    } catch (...) {
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
      throw std::bad_alloc();
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
            bool same = leftValue == bit(right.value, bitIndex) &&
                        leftUnknown == bit(right.unknown, bitIndex);
            setBit(result.value, bitIndex, same && leftValue);
            setBit(result.unknown, bitIndex, !same || leftUnknown);
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
      obelisk_rt_stable_handle_v1 decoded;
      if (!obelisk_rt_stable_handle_decode(instruction.immediate, &decoded) ||
          decoded.kind == OBELISK_RT_STABLE_HANDLE_AUTOMATIC ||
          decoded.offset < 0 ||
          width > uint64_t{INT64_MAX} - static_cast<uint64_t>(decoded.offset))
        return OBELISK_RT_INVALID_HANDLE;
      int64_t begin = decoded.offset;
      int64_t end = begin + static_cast<int64_t>(width);
      uint64_t base = static_cast<uint64_t>(begin);
      if (decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC) {
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
      } else if (context && kind <= OBELISK_RT_DESCRIPTOR_DRIVER) {
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
      if (start == kInvalidHandleStart)
        return OBELISK_RT_INVALID_HANDLE;
      uint64_t stable = encodeGlobalHandle(start);
      if ((kind & kAutomaticHandleKind) != 0) {
        uint64_t base = 0;
        std::memcpy(&base, frame.data + handle.offset + 8, 8);
        uint32_t id = 0;
        int64_t begin = 0;
        if (!decodeAutomaticHandle(base, id, begin))
          return OBELISK_RT_INVALID_HANDLE;
        stable = encodeAutomaticHandle(id, start);
      } else {
        uint64_t base = 0;
        std::memcpy(&base, frame.data + handle.offset + 8, 8);
        uint32_t id = 0;
        int64_t begin = 0;
        if (decodeStaticHandle(base, id, begin))
          stable = encodeStaticHandle(id, start);
      }
      if (stable == UINT64_MAX)
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
      for (uint64_t bitIndex = 0; bitIndex != inserted.width; ++bitIndex) {
        uint64_t destination = instruction.immediate + bitIndex;
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
        uint64_t registerPlane =
            value.kind == OBELISK_RT_DBREG_LOGIC
                ? limbCount(value.width) * sizeof(uint64_t)
                : 0;
        uint64_t framePlane = registerPlane != 0 ? transferSize / 2
                                                 : transferSize;
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
                        canonicalFrame + instruction.immediate + framePlane,
                        framePlane);
        } else {
          std::memcpy(canonicalFrame + instruction.immediate,
                      frame.data + value.offset, framePlane);
          if (registerPlane != 0)
            std::memcpy(canonicalFrame + instruction.immediate + framePlane,
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
        if (decodeAutomaticHandle(stable, automaticID, automaticOffset)) {
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
          sizeof(void *) > canonicalFrameSize - instruction.immediate)
        return OBELISK_RT_INVALID_FRAME;
      std::memset(canonicalFrame + instruction.immediate, 0, sizeof(void *));
      break;
    case OBELISK_RT_DB_LOAD_STATE:
    case OBELISK_RT_DB_STORE_STATE:
    case OBELISK_RT_DB_OVERRIDE_STATE: {
      bool isLoad = instruction.opcode == OBELISK_RT_DB_LOAD_STATE;
      bool isOverride = instruction.opcode == OBELISK_RT_DB_OVERRIDE_STATE;
      bool isAssignOverride =
          isOverride && instruction.flags == OBELISK_RT_DB_OVERRIDE_ASSIGN;
      if (!isLoad && context &&
          context->activeExecRegion == OBELISK_RT_REGION_POSTPONED)
        return OBELISK_RT_INVALID_LIFECYCLE;
      uint32_t valueRegister =
          isLoad ? instruction.destination : instruction.source1;
      Layout valueLayout = layout(valueRegister);
      Logic value = !isLoad ? read(valueRegister)
                            : Logic{valueLayout.width,
                                    valueLayout.kind == OBELISK_RT_DBREG_LOGIC,
                                    LimbVector(limbCount(valueLayout.width)),
                                    LimbVector(limbCount(valueLayout.width))};
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
      if (isOverride && (local || automatic ||
                         (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE &&
                          descriptorKind != OBELISK_RT_DESCRIPTOR_NET) ||
                         (isAssignOverride &&
                          descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE)))
        return OBELISK_RT_INVALID_HANDLE;
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
        obelisk_rt_status status = obelisk_rt_force_design_nets(
            context, absolute, value.width,
            reinterpret_cast<const uint8_t *>(value.value.data()),
            value.fourState
                ? reinterpret_cast<const uint8_t *>(value.unknown.data())
                : nullptr);
        if (status != OBELISK_RT_OK)
          return status;
        break;
      }
      if (valueLayout.kind == OBELISK_RT_DBREG_MANAGED) {
        if (isOverride)
          return OBELISK_RT_INVALID_HANDLE;
        if (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE ||
            valueLayout.width != 64 || start < 0 || start % 64 != 0 ||
            end - start < 64 || local)
          return OBELISK_RT_INVALID_HANDLE;
        obelisk_rt_object_v1 *managed = nullptr;
        obelisk_rt_object_v1 *previous = nullptr;
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
              managed = state.managedValue;
            else {
              previous = state.managedValue;
              std::memcpy(&managed, frame.data + valueLayout.offset,
                          sizeof(managed));
              state.managedValue = managed;
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
              std::memcpy(state.value.data() + byteOffset,
                          frame.data + valueLayout.offset, sizeof(managed));
              std::memcpy(&managed, frame.data + valueLayout.offset,
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
            std::memcpy(&managed, &slot, sizeof(managed));
          else {
            std::memcpy(&previous, &slot, sizeof(previous));
            std::memcpy(&slot, frame.data + valueLayout.offset,
                        sizeof(managed));
            std::memcpy(&managed, frame.data + valueLayout.offset,
                        sizeof(managed));
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
        bool equalStringContents = false;
        if (valueLayout.kind == OBELISK_RT_DBREG_STRING && !isLoad) {
          if (isOverride || local ||
              descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE ||
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
              std::memcpy(&previous, &state.managedValue, sizeof(previous));
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
        for (uint64_t bitIndex = 0; bitIndex != value.width; ++bitIndex) {
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
            bool newValue = bit(value.value, bitIndex);
            bool newUnknown = bit(value.unknown, bitIndex);
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
          instruction.flags == OBELISK_RT_DB_STORE_STATE_CHANGED) {
        Logic changedValue{1, false, LimbVector(1), LimbVector(1)};
        setBit(changedValue.value, 0, changed);
        write(instruction.destination, changedValue);
      }
      if (local && instruction.opcode == OBELISK_RT_DB_STORE_STATE)
        writeLogic(localFrame->data, localLayout, localValue);
      if (isLoad)
        write(valueRegister, value);
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
            if (descriptorKind == OBELISK_RT_DESCRIPTOR_STORAGE &&
                limb < context->assignMask.size() &&
                (context->assignMask[limb] & mask) != 0) {
              newValue = bit(context->assignValue, storageBit);
              newUnknown = bit(context->assignUnknown, storageBit);
              setBit(context->stateValue, storageBit, newValue);
              setBit(context->stateUnknown, storageBit, newUnknown);
            }
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
        break;
      }
      if (!transitions.empty() && ++context->schedulerEpoch == 0)
        context->schedulerEpoch = 1;
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
      obelisk_rt_status status =
          executeFunction(image, callee, context, canonicalFrame,
                          canonicalFrameSize, calleeFunction.firstInstruction,
                          budget, action, state, pendingActivation,
                          instruction.auxiliary, instruction.immediate, &frame);
      --state.callDepth;
      if (status != OBELISK_RT_OK)
        return status;
      break;
    }
    case OBELISK_RT_DB_VIRTUAL_CALL: {
      Layout receiverLayout = layout(instruction.source0);
      obelisk_rt_object_v1 *receiver = nullptr;
      std::memcpy(&receiver, frame.data + receiverLayout.offset,
                  sizeof(receiver));
      const obelisk_rt_method_descriptor_v1 *method = nullptr;
      obelisk_rt_status status = obelisk_rt_v1_method_resolve(
          receiver, instruction.destination, instruction.immediate, &method);
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
      status = executeFunction(
          image, callee, context, canonicalFrame, canonicalFrameSize,
          calleeFunction.firstInstruction, budget, action, state,
          pendingActivation, instruction.auxiliary, instruction.flags, &frame);
      --state.callDepth;
      if (status != OBELISK_RT_OK)
        return status;
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
      if (!context)
        return OBELISK_RT_INVALID_ARGUMENT;
      if (!pendingActivation || *pendingActivation)
        return OBELISK_RT_INVALID_LIFECYCLE;
      Function callee = functionAt(image, instruction.source0);
      uint64_t canonicalSize =
          (callee.flags & OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK) >> 1;
      if (callee.scratchAlignment == 0 ||
          canonicalSize > UINT64_MAX - (callee.scratchAlignment - 1))
        return OBELISK_RT_INVALID_BYTECODE;
      uint64_t scratchOffset = (canonicalSize + callee.scratchAlignment - 1) &
                               ~(callee.scratchAlignment - 1);
      if (scratchOffset > UINT64_MAX - callee.scratchSize ||
          scratchOffset + callee.scratchSize >
              std::numeric_limits<size_t>::max())
        return OBELISK_RT_OUT_OF_MEMORY;
      auto pending = std::make_unique<PendingDesignActivation>();
      pending->context = context;
      DesignActivation &activation = pending->activation;
      activation.function = instruction.source0;
      activation.scheduleRank =
          static_cast<uint32_t>(callee.initialScheduleRank);
      activation.scratchOffset = scratchOffset;
      activation.scratchSize = callee.scratchSize;
      activation.frame = context->designTaskFrames.acquire(
          static_cast<size_t>(scratchOffset + callee.scratchSize));
      uint32_t copied = 0;
      std::unordered_map<uint32_t, uint64_t> retainedAutomaticStates;
      for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
        CaptureRecord capture = captureAt(image, index);
        if (capture.function != instruction.source0)
          continue;
        ++copied;
        if (capture.valueOffset == UINT64_MAX)
          continue;
        uint32_t sourceRegister =
            operandAt(image, instruction.source1 + capture.argument).second;
        Layout source = layoutAt(image, frame.function, sourceRegister);
        if (source.kind == OBELISK_RT_DBREG_HANDLE) {
          uint64_t stable = UINT64_MAX;
          if (!encodeCanonicalHandle(frame.data + source.offset, stable))
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
                    frame.data + source.offset, capture.planeSize);
        if (capture.unknownOffset != UINT64_MAX)
          std::memcpy(activation.frame.data() + capture.unknownOffset,
                      frame.data + source.offset + capture.planeSize,
                      capture.planeSize);
      }
      if (copied != callee.argumentCount)
        return OBELISK_RT_INVALID_BYTECODE;
      pending->retainedAutomaticStates.reserve(retainedAutomaticStates.size());
      for (const auto &[automaticID, count] : retainedAutomaticStates)
        pending->retainedAutomaticStates.emplace_back(automaticID, count);
      {
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        for (const auto &[automaticID, count] :
             pending->retainedAutomaticStates) {
          auto found = context->nativeAutomaticStates.find(automaticID);
          if (found == context->nativeAutomaticStates.end())
            return OBELISK_RT_INVALID_HANDLE;
          if (count > UINT64_MAX - found->second.referenceCount)
            return OBELISK_RT_OUT_OF_RESOURCES;
        }
        for (const auto &[automaticID, count] :
             pending->retainedAutomaticStates)
          context->nativeAutomaticStates.find(automaticID)
              ->second.referenceCount += count;
      }
      pending->ownsRetainedAutomaticStates = true;
      *pendingActivation = std::move(pending);
      *action = {OBELISK_RT_FRAGMENT_TASK_CALL,
                 OBELISK_RT_SUSPEND_NONE,
                 static_cast<uint32_t>(instruction.immediate),
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
  try {
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
        if (layout.kind != OBELISK_RT_DBREG_HANDLE || layout.size != 32)
          return OBELISK_RT_INVALID_BYTECODE;
        uint8_t *address = frame.data + layout.offset;
        uint64_t stable = captures[index].stable_id;
        const obelisk_rt_observer_capture_abi_v1 &abi =
            descriptor->capture_abi[index];
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
        if (decodeAutomaticHandle(stable, dynamicID, dynamicOffset)) {
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
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

static obelisk_rt_status executeDesignBytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    obelisk_rt_context *context, void *frame, uint64_t frameSize,
    uint64_t scratchOffset, uint64_t scratchSize, uint32_t continuation,
    uint64_t instructionLimit, obelisk_rt_fragment_action_v1 *outAction,
    std::unique_ptr<PendingDesignActivation> *pendingActivation) noexcept {
  if (!outAction || (frameSize != 0 && !frame))
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
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
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
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
  try {
    std::vector<uint64_t> descendants;
    std::vector<obelisk_rt_process_instance_v1 *> nativeInstances;
    std::vector<CancelledDesignTask> designTasks;
    std::vector<uint64_t> insertedNativeTerminations;
    std::vector<uint64_t> insertedDesignTerminations;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
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
          throw std::bad_alloc();
        size_t count = process.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - nativeActivationCount)
          throw std::bad_alloc();
        nativeActivationCount += count;
        ++nativeTaskCount;
      }
      for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || !contains(task.id) || task.id == root)
          continue;
        if (task.callers.size() == std::numeric_limits<size_t>::max())
          throw std::bad_alloc();
        size_t count = task.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - designActivationCount)
          throw std::bad_alloc();
        designActivationCount += count;
        ++designTaskCount;
      }
      nativeInstances.reserve(nativeActivationCount);
      designTasks.reserve(designActivationCount);
      insertedNativeTerminations.reserve(nativeTaskCount);
      insertedDesignTerminations.reserve(designTaskCount);
      context->terminatedNativeProcesses.reserveRanges(checkedSizeSum(
          context->terminatedNativeProcesses.rangeCount(), nativeTaskCount));
      context->terminatedDesignTasks.reserveRanges(checkedSizeSum(
          context->terminatedDesignTasks.rangeCount(), designTaskCount));
      try {
        for (const ScheduledProcess &process : context->scheduledProcesses) {
          uint64_t token = (UINT64_C(1) << 63) | process.token;
          if (!process.instance || !contains(token) || token == root)
            continue;
          if (context->terminatedNativeProcesses.insert(process.token).second)
            insertedNativeTerminations.push_back(process.token);
        }
        for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
          if (task.terminated || !contains(task.id) || task.id == root)
            continue;
          if (context->terminatedDesignTasks.insert(task.id).second)
            insertedDesignTerminations.push_back(task.id);
        }
      } catch (...) {
        for (uint64_t token : insertedNativeTerminations)
          context->terminatedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignTerminations)
          context->terminatedDesignTasks.erase(token);
        throw;
      }
      for (ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = (UINT64_C(1) << 63) | process.token;
        if (!process.instance || !contains(token) || token == root)
          continue;
        nativeInstances.push_back(process.instance);
        nativeInstances.insert(nativeInstances.end(), process.callers.begin(),
                               process.callers.end());
        process.callers.clear();
        obelisk_rt_unregister_signal_wait_unlocked(
            context, process.signalSubscriptions, process.token, false);
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
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
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
    std::vector<uint8_t> frame;
  };
  try {
    std::vector<uint64_t> targets;
    std::vector<obelisk_rt_process_instance_v1 *> nativeInstances;
    std::vector<CancelledDesignTask> designTasks;
    std::vector<uint64_t> insertedNativeTerminations;
    std::vector<uint64_t> insertedDesignTerminations;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      uint64_t current = context->activeLogicalProcessToken;
      if (current == 0)
        return OBELISK_RT_INVALID_LIFECYCLE;
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
      if (targets.empty())
        return OBELISK_RT_OK;
      auto isTargetMember = [&](const std::vector<uint64_t> &controls) {
        for (uint64_t token : targets)
          if (std::find(controls.begin(), controls.end(), token) !=
              controls.end())
            return true;
        return false;
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
      size_t nativeActivationCount = 0;
      size_t designActivationCount = 0;
      size_t nativeTaskCount = 0;
      size_t designTaskCount = 0;
      for (const ScheduledProcess &process : context->scheduledProcesses) {
        uint64_t token = (UINT64_C(1) << 63) | process.token;
        if (!process.instance || (token == current && !cancelCurrent) ||
            !isTargetMember(process.controls))
          continue;
        if (process.callers.size() == std::numeric_limits<size_t>::max())
          throw std::bad_alloc();
        size_t count = process.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - nativeActivationCount)
          throw std::bad_alloc();
        nativeActivationCount += count;
        ++nativeTaskCount;
      }
      for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
        if (task.terminated || (task.id == current && !cancelCurrent) ||
            !isTargetMember(task.controls))
          continue;
        if (task.callers.size() == std::numeric_limits<size_t>::max())
          throw std::bad_alloc();
        size_t count = task.callers.size() + 1;
        if (count > std::numeric_limits<size_t>::max() - designActivationCount)
          throw std::bad_alloc();
        designActivationCount += count;
        ++designTaskCount;
      }
      nativeInstances.reserve(nativeActivationCount);
      designTasks.reserve(designActivationCount);
      insertedNativeTerminations.reserve(nativeTaskCount);
      insertedDesignTerminations.reserve(designTaskCount);
      context->terminatedNativeProcesses.reserveRanges(checkedSizeSum(
          context->terminatedNativeProcesses.rangeCount(), nativeTaskCount));
      context->terminatedDesignTasks.reserveRanges(checkedSizeSum(
          context->terminatedDesignTasks.rangeCount(), designTaskCount));
      try {
        for (const ScheduledProcess &process : context->scheduledProcesses) {
          uint64_t token = (UINT64_C(1) << 63) | process.token;
          if (!process.instance || (token == current && !cancelCurrent) ||
              !isTargetMember(process.controls))
            continue;
          if (context->terminatedNativeProcesses.insert(process.token).second)
            insertedNativeTerminations.push_back(process.token);
        }
        for (const ScheduledDesignTask &task : context->scheduledDesignTasks) {
          if (task.terminated || (task.id == current && !cancelCurrent) ||
              !isTargetMember(task.controls))
            continue;
          if (context->terminatedDesignTasks.insert(task.id).second)
            insertedDesignTerminations.push_back(task.id);
        }
      } catch (...) {
        for (uint64_t token : insertedNativeTerminations)
          context->terminatedNativeProcesses.erase(token);
        for (uint64_t token : insertedDesignTerminations)
          context->terminatedDesignTasks.erase(token);
        throw;
      }

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
        nativeInstances.push_back(process.instance);
        nativeInstances.insert(nativeInstances.end(), process.callers.begin(),
                               process.callers.end());
        process.callers.clear();
        obelisk_rt_unregister_signal_wait_unlocked(
            context, process.signalSubscriptions, process.token, false);
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
        designTasks.push_back({task.id, task.function, task.scratchOffset,
                               std::move(task.frame)});
        for (DesignActivation &caller : task.callers)
          designTasks.push_back({task.id, caller.function, caller.scratchOffset,
                                 std::move(caller.frame)});
        task.callers.clear();
        obelisk_rt_unregister_signal_wait_unlocked(
            context, task.signalSubscriptions, task.id, true);
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
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

obelisk_rt_status obelisk_rt_run_one_design_task(
    obelisk_rt_context *context, uint32_t maximumRegion, uint32_t maximumRank,
    uint64_t maximumInsertionSequence, bool *outProgress) noexcept {
  if (!context || !outProgress)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outProgress = false;
  ScheduledDesignTask task;
  bool taskDequeued = false;
  bool currentFrameReleased = false;
  auto abandonTask = [&](obelisk_rt_status failure) noexcept {
    try {
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
    } catch (...) {
    }
    try {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      obelisk_rt_unregister_signal_wait_unlocked(
          context, task.signalSubscriptions, task.id, true);
      if (taskDequeued) {
        task.controls = std::move(context->activeControls);
        obelisk_rt_release_controls_unlocked(context, task.controls);
        releaseDesignTaskOwnedStatesUnlocked(context, task.id);
      }
      context->activeDesignTaskID = 0;
      context->activeRandom = nullptr;
      context->activeDesignTaskPhase = 0;
      context->activeHomeRegion = UINT32_MAX;
      context->activeExecRegion = UINT32_MAX;
      context->activeLogicalProcessToken = 0;
      context->designTaskExecuting = false;
    } catch (...) {
    }
    if (taskDequeued) {
      context->designTaskFrames.release(std::move(task.frame));
      for (DesignActivation &activation : task.callers)
        context->designTaskFrames.release(std::move(activation.frame));
    }
    return failure;
  };
  try {
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
      for (uint64_t candidateID : context->designPollCandidates) {
        if (context->nativeScheduleDesignTaskFilterActive &&
            candidateID != context->nativeScheduleForcedDesignTask)
          continue;
        auto indexed = context->scheduledDesignTaskIndices.find(candidateID);
        if (indexed == context->scheduledDesignTaskIndices.end() ||
            indexed->second >= context->scheduledDesignTasks.size())
          continue;
        size_t candidateIndex = indexed->second;
        auto iterator = context->scheduledDesignTasks.begin() + candidateIndex;
        if (context->signalDiagnosticsEnabled)
          ++context->signalDiagnostics.candidateScans;
        if (iterator->phase != (context->schedulerRunningFinals ? 1u : 0u))
          continue;
        bool awaited = false;
        bool childrenDone = false;
        bool eventTriggered = false;
        bool signalTriggered =
            iterator->signalTriggered ||
            ((iterator->suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
              iterator->suspendKind == OBELISK_RT_SUSPEND_EDGE) &&
             iterator->signalLatch && iterator->signalLatch->triggered);
        if (context->signalDiagnosticsEnabled && iterator->started &&
            (iterator->suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
             iterator->suspendKind == OBELISK_RT_SUSPEND_EDGE ||
             iterator->suspendKind == OBELISK_RT_SUSPEND_OBSERVER))
          ++context->signalDiagnostics.readinessCalls;
        if (iterator->started &&
            (iterator->suspendKind == OBELISK_RT_SUSPEND_EVENT ||
             iterator->suspendKind == OBELISK_RT_SUSPEND_AWAIT ||
             iterator->suspendKind == OBELISK_RT_SUSPEND_JOIN) &&
            iterator->waitSize >= sizeof(obelisk_rt_wait_record_v1) &&
            iterator->waitOffset <= iterator->scratchOffset &&
            iterator->waitSize <=
                iterator->scratchOffset - iterator->waitOffset) {
          const auto *wait =
              reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
                  iterator->frame.data() + iterator->waitOffset);
          const auto *entries =
              reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(
                  reinterpret_cast<const uint8_t *>(wait) + sizeof(*wait));
          if (iterator->suspendKind == OBELISK_RT_SUSPEND_EVENT) {
            if (iterator->waitGenerations.size() == wait->count)
              for (uint32_t index = 0; index != wait->count; ++index) {
                auto event = context->events.find(entries[index].stable_id);
                uint64_t generation = event == context->events.end()
                                          ? 0
                                          : event->second.generation;
                eventTriggered |=
                    generation != iterator->waitGenerations[index];
              }
          } else if (iterator->suspendKind == OBELISK_RT_SUSPEND_AWAIT)
            awaited = wait->count == 1 && context->terminatedDesignTasks.count(
                                              entries[0].stable_id) != 0;
          else if (wait->count != 0) {
            awaited = wait->flags == 0;
            if (wait->flags == 0)
              for (uint32_t index = 0; index != wait->count; ++index)
                awaited &= context->terminatedDesignTasks.count(
                               entries[index].stable_id) != 0;
            else
              for (uint32_t index = 0; index != wait->count; ++index)
                awaited |= context->terminatedDesignTasks.count(
                               entries[index].stable_id) != 0;
          }
        }
        if (iterator->started &&
            iterator->suspendKind == OBELISK_RT_SUSPEND_CHILDREN) {
          childrenDone = true;
          for (const ScheduledDesignTask &child : context->scheduledDesignTasks)
            childrenDone &= child.terminated || child.parent != iterator->id;
          for (const ScheduledProcess &child : context->scheduledProcesses)
            childrenDone &= !child.instance || child.parent != iterator->id;
        }
        bool runnable =
            !iterator->terminated &&
            (!iterator->started || awaited || eventTriggered ||
             signalTriggered || childrenDone ||
             iterator->suspendKind == OBELISK_RT_SUSPEND_NONE ||
             (iterator->suspendKind == OBELISK_RT_SUSPEND_DELAY
                  ? iterator->wakeTime <= context->schedulerTime
                  : (iterator->suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_EDGE &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_EVENT &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_AWAIT &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_JOIN &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_FOREVER &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_CHILDREN &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_OBSERVER &&
                     iterator->observedEpoch != context->schedulerEpoch)));
        auto key = std::tuple{iterator->queuedRegion, iterator->scheduleRank,
                              iterator->insertionSequence};
        auto selectedKey =
            std::tuple{selectedRegion, selectedRank, selectedInsertionSequence};
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
          selectedRegion = iterator->queuedRegion;
          selectedRank = iterator->scheduleRank;
          selectedInsertionSequence = iterator->insertionSequence;
        }
      }
      auto maximumKey =
          std::tuple{maximumRegion, maximumRank, maximumInsertionSequence};
      if (found == context->scheduledDesignTasks.end() ||
          (!foundUrgent &&
           !(std::tuple{selectedRegion, selectedRank,
                        selectedInsertionSequence} < maximumKey)))
        return OBELISK_RT_OK;
      size_t selectedIndex =
          static_cast<size_t>(found - context->scheduledDesignTasks.begin());
      task = std::move(context->scheduledDesignTasks[selectedIndex]);
      obelisk_rt_unregister_signal_wait_unlocked(
          context, task.signalSubscriptions, task.id, true);
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
      context->activeRandom = &task.random;
      context->activeDesignTaskPhase = task.phase;
      context->activeHomeRegion = task.homeRegion;
      context->activeExecRegion = task.queuedRegion;
      context->activeLogicalProcessToken = task.id;
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
    if (terminationRequested) {
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
    if (status != OBELISK_RT_OK)
      return abandonTask(status);
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
      context->activeDesignTaskID = 0;
      context->activeRandom = nullptr;
      context->activeDesignTaskPhase = 0;
      context->activeHomeRegion = UINT32_MAX;
      context->activeExecRegion = UINT32_MAX;
      context->activeLogicalProcessToken = 0;
      context->designTaskExecuting = false;
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
        task.suspendKind = OBELISK_RT_SUSPEND_NONE;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.signalTriggered = false;
        task.urgent = false;
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
          const auto *computed =
              reinterpret_cast<const obelisk_rt_computed_wait_record_v1 *>(
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
          task.urgent = false;
          if (!obelisk_rt_next_queued_region(task.homeRegion,
                                             action.suspend_kind, 1,
                                             action.flags, task.queuedRegion)) {
            finalizeStatus = OBELISK_RT_INVALID_BYTECODE;
            break;
          }
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
        bool validFlags = wait->flags == OBELISK_RT_WAIT_FLAGS_NONE ||
                          (action.suspend_kind == OBELISK_RT_SUSPEND_JOIN &&
                           wait->flags <= 1) ||
                          (action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE &&
                           wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE) ||
                          (action.suspend_kind == OBELISK_RT_SUSPEND_EDGE &&
                           wait->flags == OBELISK_RT_WAIT_EDGE_IFF);
        if (!validFlags ||
            (action.suspend_kind == OBELISK_RT_SUSPEND_CHANGE &&
             wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE && wait->count != 1) ||
            (action.suspend_kind == OBELISK_RT_SUSPEND_EDGE &&
             wait->flags == OBELISK_RT_WAIT_EDGE_IFF && wait->count != 2) ||
            (action.suspend_kind == OBELISK_RT_SUSPEND_FOREVER &&
             wait->count != 0)) {
          finalizeStatus = OBELISK_RT_INVALID_FRAME;
          break;
        }
        for (uint32_t index = 0; index != wait->count; ++index) {
          bool validEdge =
              waitEntries[index].edge >= OBELISK_RT_WAIT_EDGE_CHANGE &&
              waitEntries[index].edge <= OBELISK_RT_WAIT_EDGE_BOTH;
          bool iffCondition =
              wait->flags == OBELISK_RT_WAIT_EDGE_IFF && index == 1;
          obelisk_rt_stable_handle_v1 decodedSignal;
          bool validSignalHandle =
              !signalWait || obelisk_rt_stable_handle_decode(
                                 waitEntries[index].stable_id, &decodedSignal);
          if (signalWait
                  ? (!validSignalHandle || (!validEdge && !iffCondition) ||
                     (iffCondition &&
                      waitEntries[index].edge != OBELISK_RT_WAIT_EDGE_NONE) ||
                     waitEntries[index].reserved == 0)
                  : (waitEntries[index].edge != OBELISK_RT_WAIT_EDGE_NONE ||
                     waitEntries[index].reserved != 0)) {
            finalizeStatus = OBELISK_RT_INVALID_FRAME;
            break;
          }
        }
        if (finalizeStatus != OBELISK_RT_OK)
          break;
        task.suspendKind = action.suspend_kind;
        task.waitOffset = action.payload;
        task.waitSize = sizeof(obelisk_rt_wait_record_v1) + entries;
        task.waitGenerations.clear();
        task.signalTriggered = false;
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
        if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY)
          task.wakeTime = wait->payload > UINT64_MAX - context->schedulerTime
                              ? UINT64_MAX
                              : context->schedulerTime + wait->payload;
        if (signalWait && !obelisk_rt_register_signal_wait_unlocked(
                              context, wait, task.signalSubscriptions,
                              task.signalLatch, task.id, true))
          finalizeStatus = context->schedulerStatus;
        break;
      }
      case OBELISK_RT_FRAGMENT_TASK_CALL: {
        if (!pendingActivation) {
          finalizeStatus = OBELISK_RT_INVALID_BYTECODE;
          break;
        }
        task.callers.reserve(checkedSizeSum(task.callers.size(), 1));
        task.callers.push_back({task.function, task.continuation,
                                std::move(task.frame), task.scratchOffset,
                                task.scratchSize, task.scheduleRank});
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
          context->terminatedDesignTasks.insert(task.id);
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
          try {
            context->scheduledDesignTaskIndices[scheduledID] =
                context->scheduledDesignTasks.size() - 1;
            if (!indexedSignalBlocked(context->scheduledDesignTasks.back()))
              context->designPollCandidates.insert(scheduledID);
          } catch (...) {
            context->scheduledDesignTaskIndices.erase(scheduledID);
            context->designPollCandidates.erase(scheduledID);
            task = std::move(context->scheduledDesignTasks.back());
            context->scheduledDesignTasks.pop_back();
            throw;
          }
        }
        taskDequeued = false;
      }
    }
    if (finalizeStatus != OBELISK_RT_OK)
      return abandonTask(finalizeStatus);
    *outProgress = true;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return abandonTask(OBELISK_RT_OUT_OF_MEMORY);
  } catch (...) {
    return abandonTask(OBELISK_RT_INVALID_BYTECODE);
  }
}
