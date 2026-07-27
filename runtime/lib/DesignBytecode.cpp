//===- DesignBytecode.cpp - Design-wide validated bytecode interpreter ----===//

#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

constexpr char kMagic[8] = {'O', 'B', 'B', 'C', 'D', 'S', '1', '\0'};
constexpr uint64_t kFunctionSize = 96;
constexpr uint64_t kLayoutSize = 40;
constexpr uint64_t kInstructionSize =
    OBELISK_RT_DESIGN_BYTECODE_INSTRUCTION_SIZE;
constexpr uint64_t kOperandSize = 8;
constexpr uint64_t kContinuationSize = 24;
constexpr uint64_t kIntrinsicSize = 16;
constexpr uint64_t kConnectivitySize = 32;
constexpr uint32_t kInvalidRegister = UINT32_MAX;
constexpr uint32_t kNetStateDescriptor = UINT32_MAX - 1;
constexpr uint32_t kDriverStateDescriptor = UINT32_MAX;
constexpr uint32_t kAutomaticHandleKind = UINT32_C(1) << 30;
constexpr uint32_t kLocalHandleKind = UINT32_C(1) << 31;
constexpr int64_t kInvalidHandleStart = INT64_MIN;

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

bool decodeAutomaticHandle(uint64_t handle, uint32_t &id, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_AUTOMATIC)
    return false;
  id = decoded.id;
  offset = decoded.offset;
  return true;
}

bool decodeStaticHandle(uint64_t handle, uint32_t &id, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC)
    return false;
  id = decoded.id;
  offset = decoded.offset;
  return true;
}

bool decodeGlobalHandle(uint64_t handle, int64_t &offset) {
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(handle, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL)
    return false;
  offset = decoded.offset;
  return true;
}

uint64_t encodeGlobalHandle(int64_t offset) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_GLOBAL, 0,
                                         offset);
}

uint64_t encodeAutomaticHandle(uint32_t id, int64_t offset) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_AUTOMATIC, id,
                                         offset);
}

uint64_t encodeStaticHandle(uint32_t id, int64_t offset) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, id,
                                         offset);
}

bool encodeCanonicalHandle(const uint8_t *address, uint64_t &stable) {
  uint32_t kind = 0;
  int64_t start = kInvalidHandleStart;
  std::memcpy(&kind, address, sizeof(kind));
  std::memcpy(&start, address + 16, sizeof(start));
  if ((kind & kLocalHandleKind) != 0)
    return false;
  if (start == kInvalidHandleStart) {
    stable = UINT64_MAX;
    return true;
  }
  stable = encodeGlobalHandle(start);
  if ((kind & kAutomaticHandleKind) != 0) {
    uint64_t base = 0;
    std::memcpy(&base, address + 8, sizeof(base));
    uint32_t id = 0;
    int64_t begin = 0;
    if (!decodeAutomaticHandle(base, id, begin))
      return false;
    stable = encodeAutomaticHandle(id, start);
  } else {
    uint64_t base = 0;
    std::memcpy(&base, address + 8, sizeof(base));
    uint32_t id = 0;
    int64_t begin = 0;
    if (decodeStaticHandle(base, id, begin))
      stable = encodeStaticHandle(id, start);
  }
  return stable != UINT64_MAX;
}

uint16_t read16(const uint8_t *data) {
  return uint16_t{data[0]} |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}
uint32_t read32(const uint8_t *data) {
  uint32_t value = 0;
  for (unsigned byte = 0; byte != 4; ++byte)
    value |= uint32_t{data[byte]} << (byte * 8);
  return value;
}
uint64_t read64(const uint8_t *data) {
  uint64_t value = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    value |= uint64_t{data[byte]} << (byte * 8);
  return value;
}

bool validRange(uint64_t offset, uint64_t count, uint64_t stride,
                uint64_t size) {
  return (count == 0 ||
          stride <= std::numeric_limits<uint64_t>::max() / count) &&
         offset <= size && count * stride <= size - offset;
}

uint64_t imageChecksum(const uint8_t *data, uint64_t size) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (uint64_t index = 0; index != size; ++index) {
    uint8_t byte = index >= 32 && index < 40 ? 0 : data[index];
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

struct Image {
  const uint8_t *data = nullptr;
  uint64_t size = 0;
  uint64_t functions = 0, functionCount = 0;
  uint64_t layouts = 0, layoutCount = 0;
  uint64_t code = 0, instructionCount = 0;
  uint64_t operands = 0, operandCount = 0;
  uint64_t constants = 0, constantSize = 0;
  uint64_t continuations = 0, continuationCount = 0;
  uint64_t intrinsics = 0, intrinsicCount = 0;
  uint64_t sites = 0, siteCount = 0;
  uint64_t stateDescriptors = 0, stateDescriptorCount = 0;
  uint64_t connectivity = 0, connectivityCount = 0;
  uint64_t stateBitCount = 0;
};

struct Function {
  uint64_t id = 0;
  uint64_t initialScheduleRank = UINT32_MAX;
  uint64_t firstInstruction = 0, instructionCount = 0;
  uint64_t firstLayout = 0, layoutCount = 0;
  uint32_t argumentCount = 0, resultCount = 0;
  uint64_t scratchSize = 0, scratchAlignment = 0;
  uint64_t firstContinuation = 0, continuationCount = 0;
  uint64_t flags = 0;
};

struct Layout {
  uint8_t kind = 0, flags = 0;
  uint32_t width = 0;
  uint64_t offset = 0, size = 0, auxiliary = 0;
};

struct Instruction {
  uint16_t opcode = 0, flags = 0;
  uint32_t destination = 0, source0 = 0, source1 = 0, source2 = 0;
  uint32_t auxiliary = 0;
  uint64_t immediate = 0;
};

struct Continuation {
  uint32_t function = 0;
  uint32_t id = 0;
  uint64_t instruction = 0;
  uint32_t scheduleRank = UINT32_MAX;
  uint32_t reserved = 0;
};

uint32_t functionHomeRegion(const Function &function) {
  return static_cast<uint32_t>(
      (function.flags & OBELISK_RT_DESIGN_FUNCTION_HOME_MASK) >>
      OBELISK_RT_DESIGN_FUNCTION_HOME_SHIFT);
}

bool validProcessFunctionFlags(const Function &function) {
  if ((function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0)
    return function.flags == 0;
  uint32_t homeRegion = functionHomeRegion(function);
  return obelisk_rt_is_process_home_region(homeRegion) &&
         ((function.flags & OBELISK_RT_DESIGN_FUNCTION_FINAL) == 0 ||
          homeRegion == OBELISK_RT_REGION_ACTIVE);
}

struct IntrinsicSignature {
  uint32_t id = 0, inputCount = 0, outputCount = 0, flags = 0;
};

struct IntrinsicSite {
  uint32_t intrinsic = 0, firstOperand = 0, inputCount = 0, outputCount = 0;
};

struct CaptureRecord {
  uint32_t function = 0, argument = 0;
  uint64_t valueOffset = 0, unknownOffset = 0, planeSize = 0;
};

struct ConnectivityRecord {
  uint64_t lhsOffset = 0, rhsOffset = 0, width = 0;
  uint8_t lhsResolution = 0, rhsResolution = 0, flags = 0, reserved = 0;
  uint32_t tailReserved = 0;
};

bool parseImage(const obelisk_rt_design_bytecode_entry_v1 &entry,
                Image &image) {
  const auto *execution = entry.execution;
  if (!execution || entry.reserved != 0 ||
      execution->version != OBELISK_RT_VERSION || execution->reserved != 0 ||
      (execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0 ||
      !execution->bytecode ||
      execution->bytecode_size < OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE)
    return false;
  const uint8_t *data = execution->bytecode;
  if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0 ||
      read32(data + 8) != OBELISK_RT_VERSION || read32(data + 12) != 0 ||
      read32(data + 16) != OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE ||
      read64(data + 24) != execution->bytecode_size || read64(data + 32) == 0 ||
      read64(data + 32) != execution->checksum ||
      read64(data + 32) != imageChecksum(data, execution->bytecode_size))
    return false;
  image = {data,
           execution->bytecode_size,
           read64(data + 40),
           read64(data + 48),
           read64(data + 56),
           read64(data + 64),
           read64(data + 72),
           read64(data + 80),
           read64(data + 88),
           read64(data + 96),
           read64(data + 104),
           read64(data + 112),
           read64(data + 120),
           read64(data + 128),
           read64(data + 136),
           read64(data + 144),
           read64(data + 152),
           read64(data + 160),
           read64(data + 168),
           read64(data + 176),
           read64(data + 184),
           read64(data + 192),
           execution->state_bit_count};
  if (read32(data + 20) != 0 || read64(data + 200) != 0 ||
      image.functionCount > UINT32_MAX ||
      entry.function >= image.functionCount ||
      !validRange(image.functions, image.functionCount, kFunctionSize,
                  image.size) ||
      !validRange(image.layouts, image.layoutCount, kLayoutSize, image.size) ||
      !validRange(image.code, image.instructionCount, kInstructionSize,
                  image.size) ||
      !validRange(image.operands, image.operandCount, kOperandSize,
                  image.size) ||
      !validRange(image.constants, image.constantSize, 1, image.size) ||
      !validRange(image.continuations, image.continuationCount,
                  kContinuationSize, image.size) ||
      !validRange(image.intrinsics, image.intrinsicCount, kIntrinsicSize,
                  image.size) ||
      !validRange(image.sites, image.siteCount, kIntrinsicSize, image.size) ||
      !validRange(image.stateDescriptors, image.stateDescriptorCount, 32,
                  image.size) ||
      !validRange(image.connectivity, image.connectivityCount,
                  kConnectivitySize, image.size))
    return false;
  uint64_t cursor = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  auto canonicalTable = [&](uint64_t offset, uint64_t count, uint64_t stride) {
    if (cursor > UINT64_MAX - 7)
      return false;
    uint64_t aligned = (cursor + 7) & ~uint64_t{7};
    if (offset != aligned)
      return false;
    for (uint64_t padding = cursor; padding != aligned; ++padding)
      if (data[padding] != 0)
        return false;
    cursor = offset + count * stride;
    return true;
  };
  return canonicalTable(image.functions, image.functionCount, kFunctionSize) &&
         canonicalTable(image.layouts, image.layoutCount, kLayoutSize) &&
         canonicalTable(image.code, image.instructionCount, kInstructionSize) &&
         canonicalTable(image.operands, image.operandCount, kOperandSize) &&
         canonicalTable(image.constants, image.constantSize, 1) &&
         canonicalTable(image.continuations, image.continuationCount,
                        kContinuationSize) &&
         canonicalTable(image.intrinsics, image.intrinsicCount,
                        kIntrinsicSize) &&
         canonicalTable(image.sites, image.siteCount, kIntrinsicSize) &&
         canonicalTable(image.stateDescriptors, image.stateDescriptorCount,
                        32) &&
         canonicalTable(image.connectivity, image.connectivityCount,
                        kConnectivitySize) &&
         cursor == image.size;
}

Function functionAt(const Image &image, uint32_t index) {
  const uint8_t *data =
      image.data + image.functions + uint64_t{index} * kFunctionSize;
  return {read64(data),      read64(data + 8),  read64(data + 16),
          read64(data + 24), read64(data + 32), read64(data + 40),
          read32(data + 48), read32(data + 52), read64(data + 56),
          read64(data + 64), read64(data + 72), read64(data + 80),
          read64(data + 88)};
}

Continuation continuationAt(const Image &image, uint64_t index) {
  const uint8_t *data =
      image.data + image.continuations + index * kContinuationSize;
  return {read32(data), read32(data + 4), read64(data + 8), read32(data + 16),
          read32(data + 20)};
}

Layout layoutAt(const Image &image, const Function &function, uint32_t index) {
  const uint8_t *data =
      image.data + image.layouts + (function.firstLayout + index) * kLayoutSize;
  return {data[0],          data[1],           read32(data + 4),
          read64(data + 8), read64(data + 16), read64(data + 24)};
}

Instruction instructionAt(const Image &image, uint64_t index) {
  const uint8_t *data = image.data + image.code + index * kInstructionSize;
  return {read16(data),      read16(data + 2),  read32(data + 4),
          read32(data + 8),  read32(data + 12), read32(data + 16),
          read32(data + 20), read64(data + 24)};
}

IntrinsicSignature intrinsicAt(const Image &image, uint32_t index) {
  const uint8_t *data = image.data + image.intrinsics + uint64_t{index} * 16;
  return {read32(data), read32(data + 4), read32(data + 8), read32(data + 12)};
}

IntrinsicSite siteAt(const Image &image, uint32_t index) {
  const uint8_t *data = image.data + image.sites + uint64_t{index} * 16;
  return {read32(data), read32(data + 4), read32(data + 8), read32(data + 12)};
}

CaptureRecord captureAt(const Image &image, uint64_t index) {
  const uint8_t *data = image.data + image.stateDescriptors + index * 32;
  return {read32(data), read32(data + 4), read64(data + 8), read64(data + 16),
          read64(data + 24)};
}

ConnectivityRecord connectivityAt(const Image &image, uint64_t index) {
  const uint8_t *data =
      image.data + image.connectivity + index * kConnectivitySize;
  return {read64(data), read64(data + 8), read64(data + 16), data[24],
          data[25],     data[26],         data[27],          read32(data + 28)};
}

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

std::pair<uint32_t, uint32_t> operandAt(const Image &image, uint64_t index) {
  const uint8_t *data = image.data + image.operands + index * kOperandSize;
  return {read32(data), read32(data + 4)};
}

uint64_t limbCount(uint64_t width) { return (width + 63) / 64; }
uint64_t finalMask(uint64_t width) {
  unsigned tail = static_cast<unsigned>(width % 64);
  return tail == 0 ? UINT64_MAX : (uint64_t{1} << tail) - 1;
}

uint64_t layoutSize(uint8_t kind, uint32_t width) {
  uint64_t limbs = limbCount(width);
  switch (kind) {
  case OBELISK_RT_DBREG_BITS:
    return limbs * 8;
  case OBELISK_RT_DBREG_LOGIC:
    return limbs * 16;
  case OBELISK_RT_DBREG_HANDLE:
    return 32;
  case OBELISK_RT_DBREG_STATUS:
  case OBELISK_RT_DBREG_RESOURCE:
    return 8;
  case OBELISK_RT_DBREG_BYTES:
    return 16;
  case OBELISK_RT_DBREG_MANAGED:
  case OBELISK_RT_DBREG_STRING:
  case OBELISK_RT_DBREG_REAL64:
    return 8;
  case OBELISK_RT_DBREG_REAL32:
    return 4;
  case OBELISK_RT_DBREG_MANAGED_REF:
    return 16;
  case OBELISK_RT_DBREG_ARGUMENT_REF:
    return 24;
  default:
    return 0;
  }
}

bool compatible(const Layout &left, const Layout &right) {
  return left.kind == right.kind && left.flags == right.flags &&
         left.width == right.width && left.size == right.size &&
         left.auxiliary == right.auxiliary;
}

bool validRegister(const Function &function, uint32_t index) {
  return index < function.layoutCount;
}

bool validMap(const Image &image, const Function &source,
              const Function &destination, uint64_t first, uint64_t count) {
  if (first > image.operandCount || count > image.operandCount - first)
    return false;
  for (uint64_t index = 0; index != count; ++index) {
    auto [destinationRegister, sourceRegister] =
        operandAt(image, first + index);
    if (!validRegister(source, sourceRegister) ||
        !validRegister(destination, destinationRegister) ||
        !compatible(layoutAt(image, source, sourceRegister),
                    layoutAt(image, destination, destinationRegister)))
      return false;
  }
  return true;
}

bool validIntrinsic(const Image &image, const Function &function,
                    uint32_t siteIndex) {
  if (siteIndex >= image.siteCount)
    return false;
  IntrinsicSite site = siteAt(image, siteIndex);
  if (site.intrinsic >= image.intrinsicCount ||
      site.firstOperand > image.operandCount ||
      uint64_t{site.inputCount} + site.outputCount >
          image.operandCount - site.firstOperand)
    return false;
  IntrinsicSignature signature = intrinsicAt(image, site.intrinsic);
  if (signature.inputCount != site.inputCount ||
      signature.outputCount != site.outputCount)
    return false;
  if (signature.id != OBELISK_RT_INTRINSIC_V1_SPAWN &&
      signature.id != OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER &&
      signature.id != OBELISK_RT_INTRINSIC_V1_IMPORT &&
      signature.id != OBELISK_RT_INTRINSIC_V1_CONTROL_ENTER &&
      signature.id != OBELISK_RT_INTRINSIC_V1_CONTROL_DISABLE &&
      signature.id != OBELISK_RT_INTRINSIC_V1_STATIC_ONCE &&
      signature.id != OBELISK_RT_INTRINSIC_V1_MONITOR_CONTROL &&
      signature.id != OBELISK_RT_INTRINSIC_V1_REAL_FROM_INTEGER &&
      signature.id != OBELISK_RT_INTRINSIC_V1_REAL_TO_INTEGER &&
      signature.id != OBELISK_RT_INTRINSIC_V1_REAL_COMPARE &&
      signature.flags != 0)
    return false;
  auto input = [&](uint32_t index) -> std::optional<Layout> {
    if (index >= site.inputCount)
      return std::nullopt;
    uint32_t reg = operandAt(image, site.firstOperand + index).second;
    if (!validRegister(function, reg))
      return std::nullopt;
    return layoutAt(image, function, reg);
  };
  auto output = [&](uint32_t index) -> std::optional<Layout> {
    if (index >= site.outputCount)
      return std::nullopt;
    uint32_t reg =
        operandAt(image, site.firstOperand + site.inputCount + index).first;
    if (!validRegister(function, reg))
      return std::nullopt;
    return layoutAt(image, function, reg);
  };
  auto numeric = [](const std::optional<Layout> &layout) {
    return layout && (layout->kind == OBELISK_RT_DBREG_BITS ||
                      layout->kind == OBELISK_RT_DBREG_LOGIC);
  };
  auto floating = [](const std::optional<Layout> &layout) {
    return layout && (layout->kind == OBELISK_RT_DBREG_REAL32 ||
                      layout->kind == OBELISK_RT_DBREG_REAL64);
  };
  auto bits = [&](const std::optional<Layout> &layout, uint32_t width) {
    return numeric(layout) && layout->width == width;
  };
  auto twoStateBits = [](const std::optional<Layout> &layout,
                         std::optional<uint32_t> width = std::nullopt) {
    return layout && layout->kind == OBELISK_RT_DBREG_BITS &&
           (!width || layout->width == *width);
  };
  auto bytes = [](const std::optional<Layout> &layout) {
    return layout && layout->kind == OBELISK_RT_DBREG_BYTES;
  };
  auto handle = [](const std::optional<Layout> &layout) {
    return layout && layout->kind == OBELISK_RT_DBREG_HANDLE;
  };
  auto status = [](const std::optional<Layout> &layout) {
    return layout && layout->kind == OBELISK_RT_DBREG_STATUS;
  };
  auto managed = [](const std::optional<Layout> &layout) {
    return layout && layout->kind == OBELISK_RT_DBREG_MANAGED;
  };
  auto string = [](const std::optional<Layout> &layout) {
    return layout && layout->kind == OBELISK_RT_DBREG_STRING;
  };
  auto managedRef = [](const std::optional<Layout> &layout) {
    return layout && layout->kind == OBELISK_RT_DBREG_MANAGED_REF;
  };
  auto argumentRef = [](const std::optional<Layout> &layout) {
    return layout && layout->kind == OBELISK_RT_DBREG_ARGUMENT_REF;
  };
  auto managedValue = [&](const std::optional<Layout> &layout) {
    return numeric(layout) || floating(layout) || managed(layout) ||
           string(layout) || handle(layout);
  };
  auto assocKey = [&](const std::optional<Layout> &layout) {
    return string(layout) ||
           (numeric(layout) && layout->width >= 1 && layout->width <= 64);
  };
  auto cursor = [&](const std::optional<Layout> &layout) {
    return bits(layout, 64);
  };
  switch (signature.id) {
  case OBELISK_RT_INTRINSIC_V1_SPAWN: {
    if (signature.flags >= image.functionCount || site.outputCount != 1 ||
        !handle(output(0)))
      return false;
    Function callee = functionAt(image, signature.flags);
    if ((callee.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
        site.inputCount != callee.argumentCount)
      return false;
    for (uint32_t index = 0; index != site.inputCount; ++index)
      if (!input(index) ||
          !compatible(*input(index), layoutAt(image, callee, index)))
        return false;
    return true;
  }
  case OBELISK_RT_INTRINSIC_V1_NBA:
    return signature.flags == 0 &&
           (site.inputCount == 2 || site.inputCount == 3) &&
           site.outputCount == 0 &&
           (numeric(input(0)) || floating(input(0)) || string(input(0)) ||
            managed(input(0))) &&
           handle(input(1)) && (site.inputCount == 2 || bits(input(2), 64));
  case OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER:
    return signature.flags <= 1 &&
           (site.inputCount == 1 || site.inputCount == 2) &&
           site.outputCount == 0 && handle(input(0)) &&
           (site.inputCount == 1 ||
            (signature.flags == 1 && bits(input(1), 64)));
  case OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGERED:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && handle(input(0)) && bits(output(0), 1);
  case OBELISK_RT_INTRINSIC_V1_STATE_ALLOC:
    if (signature.flags != 0 || site.inputCount == 0 || site.outputCount != 1 ||
        (!numeric(input(0)) && !floating(input(0)) && !managed(input(0)) &&
         !string(input(0))) ||
        !handle(output(0)) ||
        ((managed(input(0)) || string(input(0))) && site.inputCount != 1))
      return false;
    for (uint32_t index = 1; index != site.inputCount; ++index)
      if (!twoStateBits(input(index), 64))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_DISABLE_CHILDREN:
    return signature.flags == 0 && site.inputCount == 0 &&
           site.outputCount == 0;
  case OBELISK_RT_INTRINSIC_V1_CONTROL_ENTER:
    return signature.flags != 0 && site.inputCount == 0 &&
           site.outputCount == 1 && bits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_CONTROL_LEAVE:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 0 && bits(input(0), 64);
  case OBELISK_RT_INTRINSIC_V1_CONTROL_DISABLE:
    return (signature.flags & ~(UINT32_C(1) << 31)) != 0 &&
           site.inputCount <= 1 && site.outputCount == 0 &&
           (site.inputCount == 0 || bits(input(0), 64)) &&
           ((signature.flags >> 31) == 0 || site.inputCount == 0);
  case OBELISK_RT_INTRINSIC_V1_STATIC_ONCE:
    return signature.flags != 0 && site.inputCount == 0 &&
           site.outputCount == 1 && bits(output(0), 1);
  case OBELISK_RT_INTRINSIC_V1_DEFERRED_ONCE:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && bits(input(0), 64) && bits(output(0), 1);
  case OBELISK_RT_INTRINSIC_V1_MONITOR_REGISTER:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 0 && handle(input(0));
  case OBELISK_RT_INTRINSIC_V1_MONITOR_CONTROL:
    return signature.flags <= 1 && site.inputCount == 0 &&
           site.outputCount == 0;
  case OBELISK_RT_INTRINSIC_V1_MONITOR_CURRENT:
    return signature.flags == 0 && site.inputCount == 0 &&
           site.outputCount == 1 && bits(output(0), 1);
  case OBELISK_RT_INTRINSIC_V1_IMPORT:
    if (signature.flags == 0)
      return false;
    for (uint32_t index = 0; index != site.inputCount; ++index)
      if (!input(index) || (input(index)->kind != OBELISK_RT_DBREG_BITS &&
                            input(index)->kind != OBELISK_RT_DBREG_LOGIC &&
                            input(index)->kind != OBELISK_RT_DBREG_HANDLE &&
                            input(index)->kind != OBELISK_RT_DBREG_STATUS))
        return false;
    for (uint32_t index = 0; index != site.outputCount; ++index)
      if (!output(index) || (output(index)->kind != OBELISK_RT_DBREG_BITS &&
                             output(index)->kind != OBELISK_RT_DBREG_LOGIC &&
                             output(index)->kind != OBELISK_RT_DBREG_HANDLE &&
                             output(index)->kind != OBELISK_RT_DBREG_STATUS))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_DPI_IMPORT:
    if (signature.flags != 0 || site.inputCount == 0 || site.outputCount == 0 ||
        !bytes(input(0)) || !status(output(site.outputCount - 1)))
      return false;
    for (uint32_t index = 1; index != site.inputCount; ++index)
      if (!input(index) || (input(index)->kind != OBELISK_RT_DBREG_BITS &&
                            input(index)->kind != OBELISK_RT_DBREG_LOGIC))
        return false;
    for (uint32_t index = 0; index + 1 != site.outputCount; ++index)
      if (!output(index) || (output(index)->kind != OBELISK_RT_DBREG_BITS &&
                             output(index)->kind != OBELISK_RT_DBREG_LOGIC))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_CLASS_ALLOC:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && twoStateBits(input(0), 64) &&
           managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_CLASS_COPY:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && managed(input(0)) &&
           twoStateBits(input(1), 64) && managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_CLASS_IS_INSTANCE:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && managed(input(0)) &&
           twoStateBits(input(1), 64) && twoStateBits(output(0), 1);
  case OBELISK_RT_INTRINSIC_V1_CLASS_ID:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && managed(input(0)) &&
           twoStateBits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_CLASS_CAST:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && managed(input(0)) &&
           twoStateBits(input(1), 64) && managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_CLASS_FIELD_REF:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && managed(input(0)) &&
           twoStateBits(input(1), 64) && managedRef(output(0));
  case OBELISK_RT_INTRINSIC_V1_MANAGED_LOAD:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && managedRef(input(0)) &&
           twoStateBits(input(1), 64) && managedValue(output(0));
  case OBELISK_RT_INTRINSIC_V1_MANAGED_STORE:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 0 && managedRef(input(0)) &&
           managedValue(input(1)) && twoStateBits(input(2), 64);
  case OBELISK_RT_INTRINSIC_V1_MANAGED_NBA:
    return signature.flags == 0 &&
           (site.inputCount == 3 || site.inputCount == 4) &&
           site.outputCount == 0 &&
           (managedRef(input(0)) || managed(input(0))) &&
           managedValue(input(1)) && twoStateBits(input(2), 64) &&
           (site.inputCount == 3 || twoStateBits(input(3), 64));
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_REF:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && handle(input(0)) && argumentRef(output(0));
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_MANAGED:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && managedRef(input(0)) &&
           argumentRef(output(0));
  case OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_INDEX:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 1 && managed(input(0)) &&
           twoStateBits(input(1), 64) && argumentRef(input(2)) &&
           managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_SIZE:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && managed(input(0)) &&
           twoStateBits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE_LIKE:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 1 && managed(input(0)) && managed(input(1)) &&
           twoStateBits(input(2), 64) && managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_READ:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && managed(input(0)) &&
           twoStateBits(input(1), 64) && managedValue(output(0));
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_WRITE:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 0 && managed(input(0)) &&
           twoStateBits(input(1), 64) && managedValue(input(2));
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE:
    if (signature.flags != 0 || site.inputCount != 10 ||
        site.outputCount != 1 || !managed(output(0)))
      return false;
    for (uint32_t index = 0; index != 7; ++index)
      if (!twoStateBits(input(index), 64))
        return false;
    return bytes(input(7)) && twoStateBits(input(8), 64) &&
           twoStateBits(input(9), 64);
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_CLONE:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && managed(input(0)) && managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_DELETE:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 0 && managed(input(0));
  case OBELISK_RT_INTRINSIC_V1_RANDOM_BOUNDED:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && twoStateBits(input(0), 64) &&
           twoStateBits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_RANDOM_NEXT:
    return signature.flags == 0 && site.inputCount == 0 &&
           site.outputCount == 1 && twoStateBits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SEED:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 0 && twoStateBits(input(0), 64);
  case OBELISK_RT_INTRINSIC_V1_ASSOC_CREATE:
    if (signature.flags != 0 || site.inputCount != 9 ||
        site.outputCount != 1 || !managed(output(0)))
      return false;
    for (uint32_t index = 0; index != 6; ++index)
      if (!twoStateBits(input(index), 64))
        return false;
    return bytes(input(6)) && twoStateBits(input(7), 64) &&
           twoStateBits(input(8), 64);
  case OBELISK_RT_INTRINSIC_V1_ASSOC_READ:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && managed(input(0)) && assocKey(input(1)) &&
           managedValue(output(0));
  case OBELISK_RT_INTRINSIC_V1_ASSOC_WRITE:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 0 && managed(input(0)) && assocKey(input(1)) &&
           managedValue(input(2));
  case OBELISK_RT_INTRINSIC_V1_ASSOC_EXISTS:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && managed(input(0)) && assocKey(input(1)) &&
           twoStateBits(output(0), 1);
  case OBELISK_RT_INTRINSIC_V1_ASSOC_DELETE:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 0 && managed(input(0)) && assocKey(input(1));
  case OBELISK_RT_INTRINSIC_V1_ASSOC_DEFAULT:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 0 && managed(input(0)) &&
           managedValue(input(1));
  case OBELISK_RT_INTRINSIC_V1_ASSOC_TRAVERSE:
    return signature.flags == 0 && site.inputCount == 4 &&
           site.outputCount == 2 && managed(input(0)) && assocKey(input(1)) &&
           twoStateBits(input(2), 64) && twoStateBits(input(3), 64) &&
           input(1) && output(0) && compatible(*input(1), *output(0)) &&
           twoStateBits(output(1), 1);
  case OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_ASSOC:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 1 && managed(input(0)) && assocKey(input(1)) &&
           argumentRef(input(2)) && managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_PATH:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && managed(input(0)) && argumentRef(output(0));
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_LOAD:
    return signature.flags == 0 && site.inputCount == 4 &&
           site.outputCount == 1 && argumentRef(input(0)) &&
           twoStateBits(input(1), 64) && twoStateBits(input(2), 64) &&
           twoStateBits(input(3), 64) && managedValue(output(0));
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_STORE:
    return signature.flags == 0 && site.inputCount == 5 &&
           site.outputCount == 0 && argumentRef(input(0)) &&
           managedValue(input(1)) && twoStateBits(input(2), 64) &&
           twoStateBits(input(3), 64) && twoStateBits(input(4), 64);
  case OBELISK_RT_INTRINSIC_V1_MANAGED_ROOT_EXTRACT:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && numeric(input(0)) &&
           twoStateBits(input(1), 64) && managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_WEAK_CREATE:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && managed(input(0)) &&
           twoStateBits(input(1), 64) && managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_WEAK_GET:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && managed(input(0)) && managed(output(0));
  case OBELISK_RT_INTRINSIC_V1_WEAK_CLEAR:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 0 && managed(input(0));
  case OBELISK_RT_INTRINSIC_V1_GC_SAFEPOINT:
    return signature.flags == 0 && site.inputCount == 0 &&
           site.outputCount == 0;
  case OBELISK_RT_INTRINSIC_V1_STRING_LITERAL:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && bytes(input(0)) && string(output(0));
  case OBELISK_RT_INTRINSIC_V1_STRING_FROM_PACKED:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && numeric(input(0)) && string(output(0));
  case OBELISK_RT_INTRINSIC_V1_STRING_TO_PACKED:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && string(input(0)) && numeric(output(0));
  case OBELISK_RT_INTRINSIC_V1_STRING_CONCAT:
    if (signature.flags != 0 || site.outputCount != 1 || !string(output(0)))
      return false;
    for (uint32_t index = 0; index != site.inputCount; ++index)
      if (!string(input(index)))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_STRING_REPEAT:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && string(input(0)) && bits(input(1), 64) &&
           string(output(0));
  case OBELISK_RT_INTRINSIC_V1_STRING_LENGTH:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && string(input(0)) && bits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_STRING_GETC:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && string(input(0)) && bits(input(1), 64) &&
           bits(output(0), 8);
  case OBELISK_RT_INTRINSIC_V1_STRING_PUTC:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 1 && string(input(0)) && bits(input(1), 64) &&
           bits(input(2), 8) && string(output(0));
  case OBELISK_RT_INTRINSIC_V1_STRING_SUBSTR:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 1 && string(input(0)) && bits(input(1), 64) &&
           bits(input(2), 64) && string(output(0));
  case OBELISK_RT_INTRINSIC_V1_STRING_COMPARE:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 1 && string(input(0)) && string(input(1)) &&
           bits(input(2), 64) && bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_STRING_CASE_CONVERT:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && string(input(0)) && bits(input(1), 64) &&
           string(output(0));
  case OBELISK_RT_INTRINSIC_V1_STRING_PARSE_INTEGER:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && string(input(0)) && bits(input(1), 64) &&
           bits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_STRING_PARSE_REAL:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && string(input(0)) && output(0) &&
           output(0)->kind == OBELISK_RT_DBREG_REAL64;
  case OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_INTEGER:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 1 && bits(input(0), 64) &&
           bits(input(1), 64) && bits(input(2), 64) && string(output(0));
  case OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_REAL:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && input(0) &&
           input(0)->kind == OBELISK_RT_DBREG_REAL64 && string(output(0));
  case OBELISK_RT_INTRINSIC_V1_DISPLAY:
    if (site.inputCount < 2 || site.outputCount != 0 || !bytes(input(0)) ||
        !bits(input(1), 32))
      return false;
    for (uint32_t index = 2; index < site.inputCount; ++index)
      if (!bytes(input(index)) && !numeric(input(index)) &&
          !floating(input(index)) && !string(input(index)) &&
          !managed(input(index)))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_FINISH:
  case OBELISK_RT_INTRINSIC_V1_FATAL:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 0 && bits(input(0), 32);
  case OBELISK_RT_INTRINSIC_V1_TERMINATION_REQUESTED:
    return signature.flags == 0 && site.inputCount == 0 &&
           site.outputCount == 1 && bits(output(0), 1);
  case OBELISK_RT_INTRINSIC_V1_TIME_NOW:
    return signature.flags == 0 && site.inputCount == 0 &&
           site.outputCount == 1 && twoStateBits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_TIME_TO_REAL:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 1 && twoStateBits(input(0), 64) &&
           twoStateBits(input(1), 64) && output(0) &&
           output(0)->kind == OBELISK_RT_DBREG_REAL64;
  case OBELISK_RT_INTRINSIC_V1_TIME_FROM_REAL:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 1 && input(0) &&
           input(0)->kind == OBELISK_RT_DBREG_REAL64 &&
           twoStateBits(input(1), 64) && twoStateBits(input(2), 64) &&
           twoStateBits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_REAL_FROM_INTEGER:
    return signature.flags <= 1 && site.inputCount == 1 &&
           site.outputCount == 1 && twoStateBits(input(0)) &&
           floating(output(0));
  case OBELISK_RT_INTRINSIC_V1_REAL_TO_INTEGER:
    return signature.flags <= 1 && site.inputCount == 1 &&
           site.outputCount == 1 && input(0) &&
           input(0)->kind == OBELISK_RT_DBREG_REAL64 &&
           twoStateBits(output(0));
  case OBELISK_RT_INTRINSIC_V1_REAL_COMPARE:
    return signature.flags <= 5 && site.inputCount == 2 &&
           site.outputCount == 1 && input(0) && input(1) &&
           input(0)->kind == OBELISK_RT_DBREG_REAL64 &&
           input(1)->kind == OBELISK_RT_DBREG_REAL64 &&
           twoStateBits(output(0), 1);
  case OBELISK_RT_INTRINSIC_V1_COUNT_BITS:
    if (signature.flags != 0 || site.inputCount < 2 || site.outputCount != 1 ||
        !numeric(input(0)) || !twoStateBits(output(0), 32))
      return false;
    for (uint32_t index = 1; index != site.inputCount; ++index)
      if (!bits(input(index), 1))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_CLOG2:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && numeric(input(0)) &&
           twoStateBits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_MCD:
    return site.inputCount == 1 && site.outputCount == 1 && bytes(input(0)) &&
           bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN:
    return site.inputCount == 2 && site.outputCount == 1 && bytes(input(0)) &&
           bytes(input(1)) && bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING_MCD:
    return site.inputCount == 1 && site.outputCount == 1 && string(input(0)) &&
           bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING:
    return site.inputCount == 2 && site.outputCount == 1 && string(input(0)) &&
           string(input(1)) && bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_GETLINE_STRING:
    return site.inputCount == 1 && site.outputCount == 2 &&
           bits(input(0), 32) && string(output(0)) && bits(output(1), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_CLOSE:
  case OBELISK_RT_INTRINSIC_V1_FILE_FLUSH:
    return site.inputCount == 1 && site.outputCount == 0 && bits(input(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_GETC:
  case OBELISK_RT_INTRINSIC_V1_FILE_EOF:
  case OBELISK_RT_INTRINSIC_V1_FILE_REWIND:
    return site.inputCount == 1 && site.outputCount == 1 &&
           bits(input(0), 32) && bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_UNGETC:
    return site.inputCount == 2 && site.outputCount == 1 &&
           bits(input(0), 32) && bits(input(1), 32) && bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_GETLINE:
  case OBELISK_RT_INTRINSIC_V1_FILE_READ_PACKED:
    return site.inputCount == 1 && site.outputCount == 2 &&
           bits(input(0), 32) && numeric(output(0)) && bits(output(1), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_SEEK:
    return site.inputCount == 3 && site.outputCount == 1 &&
           bits(input(0), 32) && bits(input(1), 64) && bits(input(2), 32) &&
           bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_TELL:
    return site.inputCount == 1 && site.outputCount == 1 &&
           bits(input(0), 32) && bits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_VPI_ROOT:
    return site.inputCount == 0 && site.outputCount == 2 && cursor(output(0)) &&
           status(output(1));
  case OBELISK_RT_INTRINSIC_V1_VPI_CHILD:
  case OBELISK_RT_INTRINSIC_V1_VPI_SIBLING:
    return site.inputCount == 1 && site.outputCount == 2 && cursor(input(0)) &&
           cursor(output(0)) && status(output(1));
  case OBELISK_RT_INTRINSIC_V1_VPI_CHILD_AT:
  case OBELISK_RT_INTRINSIC_V1_VPI_TYPE_CHILD:
    return site.inputCount == 2 && site.outputCount == 2 && cursor(input(0)) &&
           bits(input(1), 64) && cursor(output(0)) && status(output(1));
  case OBELISK_RT_INTRINSIC_V1_VPI_LOOKUP:
    return site.inputCount == 1 && site.outputCount == 2 && bytes(input(0)) &&
           cursor(output(0)) && status(output(1));
  case OBELISK_RT_INTRINSIC_V1_VPI_INFO:
    if (site.inputCount != 1 || site.outputCount != 8 || !cursor(input(0)) ||
        !bits(output(0), 32) || !bits(output(1), 32) || !bits(output(2), 64) ||
        !cursor(output(3)) || !bits(output(4), 64) || !bits(output(5), 64) ||
        !bits(output(6), 64) || !status(output(7)))
      return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_VPI_NAME:
    return site.inputCount == 1 && site.outputCount == 3 && cursor(input(0)) &&
           bits(output(0), 64) && bits(output(1), 64) && status(output(2));
  case OBELISK_RT_INTRINSIC_V1_VPI_TYPE_INFO:
    if (site.inputCount != 1 || site.outputCount != 12 || !cursor(input(0)) ||
        !bits(output(0), 32) || !bits(output(1), 32))
      return false;
    for (uint32_t index = 2; index != 11; ++index)
      if (!bits(output(index), 64))
        return false;
    return status(output(11));
  case OBELISK_RT_INTRINSIC_V1_VPI_READ:
    return site.inputCount == 1 && site.outputCount == 2 && cursor(input(0)) &&
           numeric(output(0)) && status(output(1));
  case OBELISK_RT_INTRINSIC_V1_VPI_WRITE:
    return site.inputCount == 2 && site.outputCount == 1 && cursor(input(0)) &&
           numeric(input(1)) && status(output(0));
  default:
    return false;
  }
}

bool validateInitialization(const Image &image, const Function &function) {
  const uint64_t begin = function.firstInstruction;
  const uint64_t count = function.instructionCount;
  using State = std::vector<uint8_t>;
  std::vector<std::optional<State>> incoming(static_cast<size_t>(count));
  std::deque<uint64_t> worklist;
  auto merge = [&](uint64_t pc, const State &state) {
    if (pc < begin || pc >= begin + count)
      return false;
    std::optional<State> &target = incoming[static_cast<size_t>(pc - begin)];
    bool changed = false;
    if (!target) {
      target = state;
      changed = true;
    } else {
      for (uint32_t reg = 0; reg != function.layoutCount; ++reg) {
        uint8_t next = (*target)[reg] & state[reg];
        changed |= next != (*target)[reg];
        (*target)[reg] = next;
      }
    }
    if (changed)
      worklist.push_back(pc);
    return true;
  };
  State seed(static_cast<size_t>(function.layoutCount));
  if ((function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0)
    std::fill(seed.begin(), seed.begin() + function.argumentCount, 1);
  for (uint64_t index = 0; index != function.continuationCount; ++index) {
    Continuation entry =
        continuationAt(image, function.firstContinuation + index);
    State entryState = seed;
    if (index != 0 ||
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0)
      std::fill(entryState.begin(), entryState.end(), 0);
    if (!merge(entry.instruction, entryState))
      return false;
  }
  auto defineMap = [&](State &state, uint64_t first, uint64_t mapCount) {
    for (uint64_t index = 0; index != mapCount; ++index) {
      uint32_t destination = operandAt(image, first + index).first;
      if (destination >= state.size())
        return false;
      state[destination] = 1;
    }
    return true;
  };
  while (!worklist.empty()) {
    uint64_t pc = worklist.front();
    worklist.pop_front();
    State state = *incoming[static_cast<size_t>(pc - begin)];
    Instruction instruction = instructionAt(image, pc);
    auto defineDestination = [&] {
      if (instruction.destination >= state.size())
        return false;
      state[instruction.destination] = 1;
      return true;
    };
    bool fallthrough = true;
    switch (instruction.opcode) {
    case OBELISK_RT_DB_CONSTANT:
    case OBELISK_RT_DB_MOVE:
    case OBELISK_RT_DB_NOT:
    case OBELISK_RT_DB_AND:
    case OBELISK_RT_DB_OR:
    case OBELISK_RT_DB_XOR:
    case OBELISK_RT_DB_ADD:
    case OBELISK_RT_DB_SUB:
    case OBELISK_RT_DB_MUL:
    case OBELISK_RT_DB_UDIV:
    case OBELISK_RT_DB_SDIV:
    case OBELISK_RT_DB_UREM:
    case OBELISK_RT_DB_SREM:
    case OBELISK_RT_DB_SHL:
    case OBELISK_RT_DB_LSHR:
    case OBELISK_RT_DB_ASHR:
    case OBELISK_RT_DB_COMPARE:
    case OBELISK_RT_DB_SELECT:
    case OBELISK_RT_DB_REDUCE:
    case OBELISK_RT_DB_CONCAT:
    case OBELISK_RT_DB_EXTRACT:
    case OBELISK_RT_DB_INSERT:
    case OBELISK_RT_DB_LOAD_FRAME:
    case OBELISK_RT_DB_MAKE_HANDLE:
    case OBELISK_RT_DB_MAKE_LOCAL_HANDLE:
    case OBELISK_RT_DB_HANDLE_OFFSET:
    case OBELISK_RT_DB_HANDLE_ID:
    case OBELISK_RT_DB_LOAD_STATE:
    case OBELISK_RT_DB_FADD:
    case OBELISK_RT_DB_FSUB:
    case OBELISK_RT_DB_FMUL:
    case OBELISK_RT_DB_FDIV:
    case OBELISK_RT_DB_FNEG:
    case OBELISK_RT_DB_FCOMPARE:
    case OBELISK_RT_DB_FEXT:
    case OBELISK_RT_DB_FTRUNC:
    case OBELISK_RT_DB_FPOW:
      if (!defineDestination())
        return false;
      break;
    case OBELISK_RT_DB_JUMP:
      if (!defineMap(state, instruction.source0, instruction.source1) ||
          !merge(instruction.immediate, state))
        return false;
      fallthrough = false;
      break;
    case OBELISK_RT_DB_BRANCH: {
      State taken = state;
      if (!defineMap(taken, instruction.source0, instruction.source1) ||
          !merge(instruction.immediate, taken))
        return false;
      break;
    }
    case OBELISK_RT_DB_CALL:
      if (!defineMap(state, instruction.auxiliary, instruction.immediate))
        return false;
      break;
    case OBELISK_RT_DB_VIRTUAL_CALL:
      if (!defineMap(state, instruction.auxiliary, instruction.flags))
        return false;
      break;
    case OBELISK_RT_DB_CLEAR_FRAME_ROOT:
      break;
    case OBELISK_RT_DB_INTRINSIC: {
      IntrinsicSite site =
          siteAt(image, static_cast<uint32_t>(instruction.immediate));
      for (uint32_t index = 0; index != site.outputCount; ++index) {
        uint32_t destination =
            operandAt(image, site.firstOperand + site.inputCount + index).first;
        if (destination >= state.size())
          return false;
        state[destination] = 1;
      }
      break;
    }
    case OBELISK_RT_DB_RETURN:
    case OBELISK_RT_DB_CONTINUE:
    case OBELISK_RT_DB_SUSPEND:
    case OBELISK_RT_DB_TERMINATE:
    case OBELISK_RT_DB_TASK_CALL:
      fallthrough = false;
      break;
    default:
      break;
    }
    if (fallthrough) {
      if (pc + 1 >= begin + count || !merge(pc + 1, state))
        return false;
    }
  }
  auto initialized = [&](const State &state, uint32_t reg) {
    return reg < state.size() && state[reg] != 0;
  };
  auto mapSourcesInitialized = [&](const State &state, uint64_t first,
                                   uint64_t mapCount) {
    for (uint64_t index = 0; index != mapCount; ++index)
      if (!initialized(state, operandAt(image, first + index).second))
        return false;
    return true;
  };
  for (uint64_t offset = 0; offset != count; ++offset) {
    if (!incoming[static_cast<size_t>(offset)])
      continue;
    const State &state = *incoming[static_cast<size_t>(offset)];
    Instruction instruction = instructionAt(image, begin + offset);
    auto sources = [&](std::initializer_list<uint32_t> registers) {
      for (uint32_t reg : registers)
        if (!initialized(state, reg))
          return false;
      return true;
    };
    bool valid = true;
    switch (instruction.opcode) {
    case OBELISK_RT_DB_MOVE:
    case OBELISK_RT_DB_NOT:
    case OBELISK_RT_DB_REDUCE:
    case OBELISK_RT_DB_STORE_FRAME:
    case OBELISK_RT_DB_MAKE_LOCAL_HANDLE:
    case OBELISK_RT_DB_HANDLE_ID:
    case OBELISK_RT_DB_LOAD_STATE:
    case OBELISK_RT_DB_FAIL:
    case OBELISK_RT_DB_RELEASE_STATE:
    case OBELISK_RT_DB_FNEG:
    case OBELISK_RT_DB_FEXT:
    case OBELISK_RT_DB_FTRUNC:
      valid = sources({instruction.source0});
      break;
    case OBELISK_RT_DB_AND:
    case OBELISK_RT_DB_OR:
    case OBELISK_RT_DB_XOR:
    case OBELISK_RT_DB_ADD:
    case OBELISK_RT_DB_SUB:
    case OBELISK_RT_DB_MUL:
    case OBELISK_RT_DB_UDIV:
    case OBELISK_RT_DB_SDIV:
    case OBELISK_RT_DB_UREM:
    case OBELISK_RT_DB_SREM:
    case OBELISK_RT_DB_SHL:
    case OBELISK_RT_DB_LSHR:
    case OBELISK_RT_DB_ASHR:
    case OBELISK_RT_DB_COMPARE:
    case OBELISK_RT_DB_CONCAT:
    case OBELISK_RT_DB_INSERT:
    case OBELISK_RT_DB_STORE_STATE:
    case OBELISK_RT_DB_OVERRIDE_STATE:
    case OBELISK_RT_DB_FADD:
    case OBELISK_RT_DB_FSUB:
    case OBELISK_RT_DB_FMUL:
    case OBELISK_RT_DB_FDIV:
    case OBELISK_RT_DB_FCOMPARE:
    case OBELISK_RT_DB_FPOW:
      valid = sources({instruction.source0, instruction.source1});
      break;
    case OBELISK_RT_DB_SELECT:
      valid = sources(
          {instruction.source0, instruction.source1, instruction.source2});
      break;
    case OBELISK_RT_DB_EXTRACT:
    case OBELISK_RT_DB_HANDLE_OFFSET:
      valid = sources({instruction.source0}) &&
              (instruction.source1 == kInvalidRegister ||
               sources({instruction.source1}));
      break;
    case OBELISK_RT_DB_JUMP:
      valid = mapSourcesInitialized(state, instruction.source0,
                                    instruction.source1);
      break;
    case OBELISK_RT_DB_BRANCH:
      valid = initialized(state, instruction.destination) &&
              mapSourcesInitialized(state, instruction.source0,
                                    instruction.source1);
      break;
    case OBELISK_RT_DB_CALL:
    case OBELISK_RT_DB_TASK_CALL:
      valid = mapSourcesInitialized(state, instruction.source1,
                                    instruction.source2);
      break;
    case OBELISK_RT_DB_VIRTUAL_CALL:
      valid = initialized(state, instruction.source0) &&
              mapSourcesInitialized(state, instruction.source1,
                                    instruction.source2);
      break;
    case OBELISK_RT_DB_RETURN:
      valid = mapSourcesInitialized(state, instruction.source0,
                                    instruction.source1);
      break;
    case OBELISK_RT_DB_SUSPEND:
      valid = instruction.source0 == kInvalidRegister ||
              initialized(state, instruction.source0);
      break;
    case OBELISK_RT_DB_INTRINSIC: {
      IntrinsicSite site =
          siteAt(image, static_cast<uint32_t>(instruction.immediate));
      for (uint32_t index = 0; index != site.inputCount; ++index)
        valid &= initialized(
            state, operandAt(image, site.firstOperand + index).second);
      break;
    }
    default:
      break;
    }
    if (!valid)
      return false;
  }
  return true;
}

bool validateImage(const Image &image) {
  // State capture validation indexes argument layouts. Prove those ranges
  // before following any function-owned offsets from an untrusted image.
  for (uint32_t functionIndex = 0; functionIndex != image.functionCount;
       ++functionIndex) {
    Function function = functionAt(image, functionIndex);
    bool process = (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0;
    if (!validProcessFunctionFlags(function) ||
        (process && function.resultCount != 0) ||
        function.firstLayout > image.layoutCount ||
        function.layoutCount > image.layoutCount - function.firstLayout ||
        function.argumentCount > function.layoutCount ||
        function.resultCount > function.layoutCount - function.argumentCount)
      return false;
  }
  uint64_t captureIndex = 0;
  for (uint32_t functionIndex = 0; functionIndex != image.functionCount;
       ++functionIndex) {
    Function function = functionAt(image, functionIndex);
    bool process = (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0;
    if (!validProcessFunctionFlags(function) ||
        (process && function.resultCount != 0))
      return false;
    uint64_t canonicalSize =
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK) >> 1;
    if (!process)
      continue;
    for (uint32_t argument = 0; argument != function.argumentCount;
         ++argument) {
      if (captureIndex >= image.stateDescriptorCount)
        return false;
      CaptureRecord capture = captureAt(image, captureIndex++);
      if (capture.function != functionIndex || capture.argument != argument)
        return false;
      Layout layout = layoutAt(image, function, argument);
      if (capture.valueOffset == UINT64_MAX) {
        if (capture.unknownOffset != UINT64_MAX || capture.planeSize != 0 ||
            layout.kind != OBELISK_RT_DBREG_HANDLE)
          return false;
        continue;
      }
      if (capture.planeSize == 0 || capture.valueOffset > canonicalSize ||
          capture.planeSize > canonicalSize - capture.valueOffset)
        return false;
      if (layout.kind == OBELISK_RT_DBREG_LOGIC ||
          layout.kind == OBELISK_RT_DBREG_MANAGED_REF) {
        if (capture.planeSize * 2 != layout.size ||
            capture.unknownOffset != capture.valueOffset + capture.planeSize ||
            capture.unknownOffset > canonicalSize ||
            capture.planeSize > canonicalSize - capture.unknownOffset)
          return false;
      } else if (capture.unknownOffset != UINT64_MAX ||
                 ((layout.kind == OBELISK_RT_DBREG_HANDLE)
                      ? capture.planeSize != 8
                      : capture.planeSize > layout.size)) {
        return false;
      }
    }
  }
  uint64_t previousNetEnd = 0;
  std::vector<CaptureRecord> netRecords;
  for (; captureIndex != image.stateDescriptorCount; ++captureIndex) {
    CaptureRecord net = captureAt(image, captureIndex);
    if (net.function != kNetStateDescriptor)
      break;
    if ((net.argument & ~uint32_t{7}) != 0 || (net.argument >> 1) > 2 ||
        net.planeSize == 0 || net.valueOffset < previousNetEnd ||
        net.unknownOffset != UINT64_MAX ||
        net.valueOffset > image.stateBitCount ||
        net.planeSize > image.stateBitCount - net.valueOffset)
      return false;
    netRecords.push_back(net);
    previousNetEnd = net.valueOffset + net.planeSize;
  }
  auto containingNet = [&](uint64_t bit, uint64_t width,
                           bool reversed) -> const CaptureRecord * {
    for (const CaptureRecord &net : netRecords) {
      if (bit < net.valueOffset || bit >= net.valueOffset + net.planeSize)
        continue;
      if (reversed) {
        if (width <= bit - net.valueOffset + 1)
          return &net;
      } else if (width <= net.valueOffset + net.planeSize - bit) {
        return &net;
      }
      return nullptr;
    }
    return nullptr;
  };
  uint64_t driverStart = captureIndex;
  uint64_t previousDriverEnd = 0;
  std::vector<CaptureRecord> driverRecords;
  for (; captureIndex != image.stateDescriptorCount; ++captureIndex) {
    CaptureRecord driver = captureAt(image, captureIndex);
    const CaptureRecord *target =
        containingNet(driver.unknownOffset, driver.planeSize, false);
    if (driver.function != kDriverStateDescriptor ||
        (driver.argument & ~uint32_t{7}) != 0 || (driver.argument & 1) == 0 ||
        (driver.argument >> 1) > 2 || driver.planeSize == 0 ||
        driver.valueOffset < previousDriverEnd ||
        driver.valueOffset > image.stateBitCount ||
        driver.planeSize > image.stateBitCount - driver.valueOffset ||
        driver.unknownOffset > image.stateBitCount ||
        driver.planeSize > image.stateBitCount - driver.unknownOffset ||
        !target || (driver.argument >> 1) != (target->argument >> 1))
      return false;
    for (uint64_t previous = driverStart; previous != captureIndex;
         ++previous) {
      CaptureRecord other = captureAt(image, previous);
      if (other.unknownOffset == driver.unknownOffset &&
          ((other.argument >> 1) != (driver.argument >> 1) ||
           other.planeSize != driver.planeSize))
        return false;
    }
    driverRecords.push_back(driver);
    previousDriverEnd = driver.valueOffset + driver.planeSize;
  }

  std::tuple<uint64_t, uint64_t, uint64_t, uint8_t> previousConnection;
  bool firstConnection = true;
  uint64_t expandedConnections = 0;
  struct ScalarConnection {
    uint64_t lhs = 0, rhs = 0;
    uint8_t lhsResolution = 0, rhsResolution = 0;
    auto tie() const {
      return std::tie(lhs, rhs, lhsResolution, rhsResolution);
    }
  };
  std::vector<ConnectivityRecord> connectionRecords;
  std::vector<ScalarConnection> scalarConnections;
  std::unordered_map<uint64_t, uint64_t> connectivityParents;
  auto findConnectivity = [&](uint64_t bit) {
    connectivityParents.try_emplace(bit, bit);
    uint64_t root = bit;
    while (connectivityParents[root] != root)
      root = connectivityParents[root];
    while (connectivityParents[bit] != bit) {
      uint64_t next = connectivityParents[bit];
      connectivityParents[bit] = root;
      bit = next;
    }
    return root;
  };
  for (uint64_t index = 0; index != image.connectivityCount; ++index) {
    ConnectivityRecord connection = connectivityAt(image, index);
    auto key = std::make_tuple(connection.lhsOffset, connection.rhsOffset,
                               connection.width, connection.flags);
    const CaptureRecord *lhs =
        containingNet(connection.lhsOffset, connection.width, false);
    const CaptureRecord *rhs = containingNet(
        connection.rhsOffset, connection.width, (connection.flags & 1) != 0);
    if (connection.width == 0 || connection.flags > 1 ||
        connection.reserved != 0 || connection.tailReserved != 0 ||
        connection.lhsResolution > 2 || connection.rhsResolution > 2 || !lhs ||
        !rhs || connection.lhsResolution != (lhs->argument >> 1) ||
        connection.rhsResolution != (rhs->argument >> 1) ||
        ((lhs->argument ^ rhs->argument) & 1) != 0 ||
        ((connection.lhsResolution == 2) != (connection.rhsResolution == 2)) ||
        (!firstConnection && key <= previousConnection) ||
        connection.width > UINT64_MAX - expandedConnections)
      return false;
    previousConnection = key;
    firstConnection = false;
    connectionRecords.push_back(connection);
    expandedConnections += connection.width;
    // A corrupt image must not turn validation into an unbounded expansion.
    if ((image.stateBitCount <= UINT64_MAX / 8 &&
         expandedConnections > image.stateBitCount * 8) ||
        expandedConnections > UINT32_MAX)
      return false;
    for (uint64_t bit = 0; bit != connection.width; ++bit) {
      uint64_t lhsBit = connection.lhsOffset + bit;
      uint64_t rhsBit = (connection.flags & 1) ? connection.rhsOffset - bit
                                               : connection.rhsOffset + bit;
      if (lhsBit >= rhsBit)
        return false;
      scalarConnections.push_back(
          {lhsBit, rhsBit, connection.lhsResolution, connection.rhsResolution});
      uint64_t lhsRoot = findConnectivity(lhsBit);
      uint64_t rhsRoot = findConnectivity(rhsBit);
      if (lhsRoot != rhsRoot)
        connectivityParents[std::max(lhsRoot, rhsRoot)] =
            std::min(lhsRoot, rhsRoot);
    }
  }
  std::sort(scalarConnections.begin(), scalarConnections.end(),
            [](const ScalarConnection &lhs, const ScalarConnection &rhs) {
              return lhs.tie() < rhs.tie();
            });
  for (size_t index = 1; index < scalarConnections.size(); ++index)
    if (scalarConnections[index - 1].lhs == scalarConnections[index].lhs &&
        scalarConnections[index - 1].rhs == scalarConnections[index].rhs)
      return false;

  // The serialized table is the unique maximal interval encoding of its
  // canonical scalar edges. Reject alternative spellings so malformed images
  // cannot hide duplicates in overlaps, swapped endpoints, or split runs.
  std::vector<ConnectivityRecord> canonicalRecords;
  for (size_t scalar = 0; scalar != scalarConnections.size();) {
    const ScalarConnection &first = scalarConnections[scalar];
    uint64_t width = 1;
    int direction = 0;
    size_t next = scalar + 1;
    while (next != scalarConnections.size()) {
      const ScalarConnection &candidate = scalarConnections[next];
      if (candidate.lhsResolution != first.lhsResolution ||
          candidate.rhsResolution != first.rhsResolution ||
          candidate.lhs != first.lhs + width)
        break;
      int candidateDirection = 0;
      if (candidate.rhs == first.rhs + width)
        candidateDirection = 1;
      else if (first.rhs >= width && candidate.rhs == first.rhs - width)
        candidateDirection = -1;
      if (candidateDirection == 0 ||
          (direction != 0 && direction != candidateDirection))
        break;
      direction = candidateDirection;
      ++width;
      ++next;
    }
    canonicalRecords.push_back({first.lhs, first.rhs, width,
                                first.lhsResolution, first.rhsResolution,
                                static_cast<uint8_t>(direction < 0), 0, 0});
    scalar = next;
  }
  if (canonicalRecords.size() != connectionRecords.size())
    return false;
  for (size_t index = 0; index != canonicalRecords.size(); ++index) {
    const ConnectivityRecord &actual = connectionRecords[index];
    const ConnectivityRecord &expected = canonicalRecords[index];
    if (actual.lhsOffset != expected.lhsOffset ||
        actual.rhsOffset != expected.rhsOffset ||
        actual.width != expected.width ||
        actual.lhsResolution != expected.lhsResolution ||
        actual.rhsResolution != expected.rhsResolution ||
        actual.flags != expected.flags)
      return false;
  }
  // A uwire component has at most one design-lifetime driver for every
  // connected scalar equivalence class, including aliases of its target.
  std::unordered_map<uint64_t, uint32_t> uwireDrivers;
  for (const CaptureRecord &driver : driverRecords) {
    if ((driver.argument >> 1) != 2)
      continue;
    for (uint64_t bit = 0; bit != driver.planeSize; ++bit) {
      uint64_t root = findConnectivity(driver.unknownOffset + bit);
      if (++uwireDrivers[root] > 1)
        return false;
    }
  }
  uint64_t previousID = 0;
  for (uint32_t functionIndex = 0; functionIndex != image.functionCount;
       ++functionIndex) {
    Function function = functionAt(image, functionIndex);
    if (function.id == 0 || function.initialScheduleRank > UINT32_MAX ||
        (functionIndex != 0 && function.id <= previousID) ||
        function.firstInstruction > image.instructionCount ||
        function.instructionCount == 0 ||
        function.instructionCount >
            image.instructionCount - function.firstInstruction ||
        function.firstLayout > image.layoutCount ||
        function.layoutCount > image.layoutCount - function.firstLayout ||
        function.argumentCount > function.layoutCount ||
        function.resultCount > function.layoutCount - function.argumentCount ||
        function.scratchAlignment == 0 || function.scratchAlignment > 4096 ||
        (function.scratchAlignment & (function.scratchAlignment - 1)) != 0 ||
        function.scratchSize % function.scratchAlignment != 0 ||
        !validProcessFunctionFlags(function) ||
        function.continuationCount == 0 ||
        function.firstContinuation > image.continuationCount ||
        function.continuationCount >
            image.continuationCount - function.firstContinuation)
      return false;
    previousID = function.id;
    uint64_t previousEnd = 0;
    for (uint32_t registerIndex = 0; registerIndex != function.layoutCount;
         ++registerIndex) {
      Layout layout = layoutAt(image, function, registerIndex);
      const uint8_t *layoutRecord =
          image.data + image.layouts +
          (function.firstLayout + registerIndex) * kLayoutSize;
      uint64_t expected = layoutSize(layout.kind, layout.width);
      if (layout.width == 0 || expected == 0 || layout.size != expected ||
          (layout.kind == OBELISK_RT_DBREG_REAL32 && layout.width != 32) ||
          (layout.kind == OBELISK_RT_DBREG_REAL64 && layout.width != 64) ||
          (layout.kind == OBELISK_RT_DBREG_STRING && layout.width != 64) ||
          read16(layoutRecord + 2) != 0 || read64(layoutRecord + 32) != 0 ||
          (layout.flags & ~OBELISK_RT_DBREG_SIGNED) != 0 ||
          (layout.kind != OBELISK_RT_DBREG_BITS &&
           layout.kind != OBELISK_RT_DBREG_LOGIC && layout.flags != 0) ||
          layout.offset % 8 != 0 || layout.offset < previousEnd ||
          layout.offset > function.scratchSize ||
          layout.size > function.scratchSize - layout.offset)
        return false;
      previousEnd = layout.offset + layout.size;
    }
    uint32_t previousContinuation = 0;
    for (uint64_t index = 0; index != function.continuationCount; ++index) {
      Continuation entry =
          continuationAt(image, function.firstContinuation + index);
      if (entry.function != functionIndex || entry.reserved != 0 ||
          (index == 0 ? entry.id != 0 : entry.id <= previousContinuation) ||
          entry.instruction < function.firstInstruction ||
          entry.instruction >=
              function.firstInstruction + function.instructionCount)
        return false;
      previousContinuation = entry.id;
    }
    if ((function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 &&
        continuationAt(image, function.firstContinuation).instruction !=
            function.firstInstruction)
      return false;
    uint64_t codeEnd = function.firstInstruction + function.instructionCount;
    auto hasContinuation = [&](uint64_t id) {
      if (id > UINT32_MAX)
        return false;
      for (uint64_t index = 0; index != function.continuationCount; ++index) {
        if (continuationAt(image, function.firstContinuation + index).id == id)
          return true;
      }
      return false;
    };
    for (uint64_t pc = function.firstInstruction; pc != codeEnd; ++pc) {
      Instruction instruction = instructionAt(image, pc);
      auto reg = [&](uint32_t index) { return validRegister(function, index); };
      auto numeric = [&](uint32_t index) {
        if (!reg(index))
          return false;
        uint8_t kind = layoutAt(image, function, index).kind;
        return kind == OBELISK_RT_DBREG_BITS || kind == OBELISK_RT_DBREG_LOGIC;
      };
      auto floating = [&](uint32_t index) {
        if (!reg(index))
          return false;
        uint8_t kind = layoutAt(image, function, index).kind;
        return kind == OBELISK_RT_DBREG_REAL32 ||
               kind == OBELISK_RT_DBREG_REAL64;
      };
      auto binary = [&] {
        return reg(instruction.destination) && reg(instruction.source0) &&
               reg(instruction.source1) &&
               compatible(layoutAt(image, function, instruction.destination),
                          layoutAt(image, function, instruction.source0)) &&
               compatible(layoutAt(image, function, instruction.destination),
                          layoutAt(image, function, instruction.source1));
      };
      switch (instruction.opcode) {
      case OBELISK_RT_DB_NOP:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate)
          return false;
        break;
      case OBELISK_RT_DB_CONSTANT: {
        if (!reg(instruction.destination) || instruction.flags ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary)
          return false;
        Layout layout = layoutAt(image, function, instruction.destination);
        if (instruction.immediate > image.constantSize ||
            layout.size > image.constantSize - instruction.immediate)
          return false;
        break;
      }
      case OBELISK_RT_DB_MOVE:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return false;
        break;
      case OBELISK_RT_DB_NOT:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return false;
        break;
      case OBELISK_RT_DB_REDUCE:
        if (instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !numeric(instruction.destination) ||
            !numeric(instruction.source0) || instruction.flags > 8 ||
            layoutAt(image, function, instruction.destination).width != 1)
          return false;
        break;
      case OBELISK_RT_DB_EXTRACT:
        if (instruction.source2 || instruction.auxiliary ||
            (instruction.source1 != kInvalidRegister &&
             !numeric(instruction.source1)))
          return false;
        if (instruction.flags == OBELISK_RT_DB_AGGREGATE_MANAGED) {
          if (!reg(instruction.destination) || !reg(instruction.source0))
            return false;
          Layout destination =
              layoutAt(image, function, instruction.destination);
          Layout source = layoutAt(image, function, instruction.source0);
          bool extractHandle = (destination.kind == OBELISK_RT_DBREG_MANAGED ||
                                destination.kind == OBELISK_RT_DBREG_STRING) &&
                               destination.width == 64 &&
                               numeric(instruction.source0);
          bool insertHandle =
              numeric(instruction.destination) &&
              (source.kind == OBELISK_RT_DBREG_MANAGED ||
               source.kind == OBELISK_RT_DBREG_STRING) &&
              source.width == 64 && instruction.source1 == kInvalidRegister &&
              instruction.immediate == 0 && destination.width >= 64;
          if (extractHandle && instruction.source1 == kInvalidRegister &&
              ((instruction.immediate & 63) != 0 ||
               instruction.immediate > source.width ||
               uint64_t{64} > source.width - instruction.immediate))
            return false;
          if (!extractHandle && !insertHandle)
            return false;
        } else if (instruction.flags > 1 || !numeric(instruction.destination) ||
                   !numeric(instruction.source0)) {
          return false;
        }
        break;
      case OBELISK_RT_DB_AND:
      case OBELISK_RT_DB_OR:
      case OBELISK_RT_DB_XOR:
      case OBELISK_RT_DB_ADD:
      case OBELISK_RT_DB_SUB:
      case OBELISK_RT_DB_MUL:
      case OBELISK_RT_DB_UDIV:
      case OBELISK_RT_DB_SDIV:
      case OBELISK_RT_DB_UREM:
      case OBELISK_RT_DB_SREM:
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !binary())
          return false;
        break;
      case OBELISK_RT_DB_FADD:
      case OBELISK_RT_DB_FSUB:
      case OBELISK_RT_DB_FMUL:
      case OBELISK_RT_DB_FDIV:
      case OBELISK_RT_DB_FPOW:
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !binary() ||
            !floating(instruction.destination))
          return false;
        break;
      case OBELISK_RT_DB_FNEG:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            !floating(instruction.destination) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return false;
        break;
      case OBELISK_RT_DB_FCOMPARE: {
        if (instruction.flags > 5 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) ||
            !numeric(instruction.destination) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_BITS ||
            layoutAt(image, function, instruction.destination).width != 1 ||
            !floating(instruction.source0) ||
            !floating(instruction.source1) ||
            !compatible(layoutAt(image, function, instruction.source0),
                        layoutAt(image, function, instruction.source1)))
          return false;
        break;
      }
      case OBELISK_RT_DB_FEXT:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_REAL64 ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_REAL32)
          return false;
        break;
      case OBELISK_RT_DB_FTRUNC:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_REAL32 ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_REAL64)
          return false;
        break;
      case OBELISK_RT_DB_SHL:
      case OBELISK_RT_DB_LSHR:
      case OBELISK_RT_DB_ASHR:
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.destination) ||
            !reg(instruction.source0) || !numeric(instruction.source1) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return false;
        break;
      case OBELISK_RT_DB_COMPARE: {
        bool deterministic = instruction.flags == OBELISK_RT_DB_CMP_CASE_EQ ||
                             instruction.flags == OBELISK_RT_DB_CMP_CASE_NE ||
                             instruction.flags == OBELISK_RT_DB_CMP_CASEZ_EQ ||
                             instruction.flags == OBELISK_RT_DB_CMP_CASEXZ_EQ;
        if (instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.destination) ||
            !reg(instruction.source0) || !reg(instruction.source1) ||
            instruction.flags > OBELISK_RT_DB_CMP_CASEXZ_EQ ||
            !compatible(layoutAt(image, function, instruction.source0),
                        layoutAt(image, function, instruction.source1)) ||
            !numeric(instruction.destination) ||
            layoutAt(image, function, instruction.destination).width != 1 ||
            (deterministic &&
             layoutAt(image, function, instruction.destination).kind !=
                 OBELISK_RT_DBREG_BITS))
          return false;
        break;
      }
      case OBELISK_RT_DB_SELECT:
        if (instruction.flags > 1 || instruction.auxiliary ||
            instruction.immediate || !binary() ||
            !numeric(instruction.source2) ||
            layoutAt(image, function, instruction.source2).width != 1 ||
            (instruction.flags == 1 &&
             (layoutAt(image, function, instruction.destination).kind !=
                  OBELISK_RT_DBREG_LOGIC ||
              layoutAt(image, function, instruction.source0).kind !=
                  OBELISK_RT_DBREG_LOGIC ||
              layoutAt(image, function, instruction.source1).kind !=
                  OBELISK_RT_DBREG_LOGIC ||
              layoutAt(image, function, instruction.source2).kind !=
                  OBELISK_RT_DBREG_LOGIC)))
          return false;
        break;
      case OBELISK_RT_DB_CONCAT: {
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.destination) ||
            !reg(instruction.source0) || !reg(instruction.source1))
          return false;
        Layout destination = layoutAt(image, function, instruction.destination);
        Layout left = layoutAt(image, function, instruction.source0);
        Layout right = layoutAt(image, function, instruction.source1);
        if (destination.kind != left.kind || left.kind != right.kind ||
            uint64_t{left.width} + right.width != destination.width)
          return false;
        break;
      }
      case OBELISK_RT_DB_INSERT: {
        if (instruction.source2 || instruction.auxiliary ||
            !numeric(instruction.destination) ||
            !numeric(instruction.source0) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return false;
        Layout destination = layoutAt(image, function, instruction.destination);
        Layout inserted = layoutAt(image, function, instruction.source1);
        if (instruction.flags == OBELISK_RT_DB_AGGREGATE_MANAGED) {
          if ((inserted.kind != OBELISK_RT_DBREG_MANAGED &&
               inserted.kind != OBELISK_RT_DBREG_STRING) ||
              inserted.width != 64 || (instruction.immediate & 63) != 0)
            return false;
        } else if (instruction.flags != 0 || !numeric(instruction.source1)) {
          return false;
        }
        if (instruction.immediate > destination.width ||
            inserted.width > destination.width - instruction.immediate)
          return false;
        break;
      }
      case OBELISK_RT_DB_LOAD_FRAME:
      case OBELISK_RT_DB_STORE_FRAME: {
        if (instruction.source1 || instruction.source2 ||
            (instruction.opcode == OBELISK_RT_DB_LOAD_FRAME
                 ? instruction.source0 != 0
                 : instruction.destination != 0))
          return false;
        uint32_t valueRegister = instruction.opcode == OBELISK_RT_DB_LOAD_FRAME
                                     ? instruction.destination
                                     : instruction.source0;
        if (!reg(valueRegister))
          return false;
        Layout value = layoutAt(image, function, valueRegister);
        if (value.kind == OBELISK_RT_DBREG_HANDLE) {
          if (instruction.flags < OBELISK_RT_DESCRIPTOR_STORAGE ||
              instruction.flags > OBELISK_RT_DESCRIPTOR_PROCESS ||
              (instruction.flags <= OBELISK_RT_DESCRIPTOR_DRIVER
                   ? instruction.auxiliary == 0
                   : instruction.auxiliary != 0))
            return false;
        } else if (instruction.flags != 0 ||
                   instruction.auxiliary > value.size) {
          return false;
        }
        break;
      }
      case OBELISK_RT_DB_CLEAR_FRAME_ROOT:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary)
          return false;
        break;
      case OBELISK_RT_DB_MAKE_HANDLE:
        if (instruction.flags || !reg(instruction.destination) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_HANDLE ||
            instruction.source0 < OBELISK_RT_DESCRIPTOR_STORAGE ||
            instruction.source0 > OBELISK_RT_DESCRIPTOR_EVENT ||
            instruction.source2 || instruction.auxiliary)
          return false;
        break;
      case OBELISK_RT_DB_MAKE_LOCAL_HANDLE:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_HANDLE ||
            (!numeric(instruction.source0) &&
             !floating(instruction.source0)) ||
            instruction.source0 > UINT16_MAX)
          return false;
        break;
      case OBELISK_RT_DB_HANDLE_OFFSET:
        if (instruction.flags || instruction.source2 ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_HANDLE ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE ||
            instruction.auxiliary == 0 ||
            (instruction.source1 != kInvalidRegister &&
             !numeric(instruction.source1)))
          return false;
        break;
      case OBELISK_RT_DB_HANDLE_ID:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !numeric(instruction.destination) ||
            layoutAt(image, function, instruction.destination).width != 64 ||
            !reg(instruction.source0) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return false;
        break;
      case OBELISK_RT_DB_LOAD_STATE:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            (!numeric(instruction.destination) &&
             !floating(instruction.destination) &&
             (!reg(instruction.destination) ||
              (layoutAt(image, function, instruction.destination).kind !=
                   OBELISK_RT_DBREG_MANAGED &&
               layoutAt(image, function, instruction.destination).kind !=
                   OBELISK_RT_DBREG_STRING))) ||
            !reg(instruction.source0) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return false;
        break;
      case OBELISK_RT_DB_STORE_STATE:
        if (instruction.flags || instruction.destination ||
            instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.source0) ||
            (!numeric(instruction.source1) &&
             !floating(instruction.source1) &&
             (!reg(instruction.source1) ||
              (layoutAt(image, function, instruction.source1).kind !=
                   OBELISK_RT_DBREG_MANAGED &&
               layoutAt(image, function, instruction.source1).kind !=
                   OBELISK_RT_DBREG_STRING))) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return false;
        break;
      case OBELISK_RT_DB_OVERRIDE_STATE:
        if (instruction.flags > 1 || instruction.destination ||
            instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.source0) ||
            !numeric(instruction.source1) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return false;
        break;
      case OBELISK_RT_DB_RELEASE_STATE:
        if (instruction.flags > 1 || instruction.destination ||
            instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.source0) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return false;
        break;
      case OBELISK_RT_DB_JUMP:
      case OBELISK_RT_DB_BRANCH:
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            (instruction.opcode == OBELISK_RT_DB_JUMP
                 ? instruction.destination != 0
                 : false) ||
            instruction.immediate < function.firstInstruction ||
            instruction.immediate >= codeEnd ||
            (instruction.opcode == OBELISK_RT_DB_BRANCH &&
             (!numeric(instruction.destination) ||
              layoutAt(image, function, instruction.destination).width != 1)) ||
            !validMap(image, function, function, instruction.source0,
                      instruction.source1))
          return false;
        break;
      case OBELISK_RT_DB_CALL: {
        if (instruction.flags || instruction.destination ||
            instruction.source0 >= image.functionCount)
          return false;
        Function callee = functionAt(image, instruction.source0);
        if ((callee.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0 ||
            instruction.source2 != callee.argumentCount ||
            instruction.immediate != callee.resultCount ||
            !validMap(image, function, callee, instruction.source1,
                      instruction.source2) ||
            !validMap(image, callee, function, instruction.auxiliary,
                      instruction.immediate))
          return false;
        for (uint32_t index = 0; index != callee.argumentCount; ++index)
          if (operandAt(image, instruction.source1 + index).first != index)
            return false;
        for (uint32_t index = 0; index != callee.resultCount; ++index)
          if (operandAt(image, instruction.auxiliary + index).second !=
              callee.argumentCount + index)
            return false;
        break;
      }
      case OBELISK_RT_DB_TASK_CALL: {
        if (instruction.flags || instruction.destination ||
            instruction.source0 >= image.functionCount ||
            instruction.auxiliary ||
            (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
            !hasContinuation(instruction.immediate))
          return false;
        Function callee = functionAt(image, instruction.source0);
        if ((callee.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
            callee.resultCount != 0 ||
            instruction.source2 != callee.argumentCount ||
            !validMap(image, function, callee, instruction.source1,
                      instruction.source2))
          return false;
        for (uint32_t index = 0; index != callee.argumentCount; ++index)
          if (operandAt(image, instruction.source1 + index).first != index)
            return false;
        break;
      }
      case OBELISK_RT_DB_VIRTUAL_CALL: {
        if (instruction.flags > function.layoutCount ||
            instruction.immediate == 0 || !reg(instruction.source0) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_MANAGED ||
            instruction.source1 > image.operandCount ||
            instruction.source2 > image.operandCount - instruction.source1 ||
            instruction.auxiliary > image.operandCount ||
            instruction.flags > image.operandCount - instruction.auxiliary)
          return false;
        for (uint32_t index = 0; index != instruction.source2; ++index) {
          auto [destination, source] =
              operandAt(image, instruction.source1 + index);
          if (destination != index || !reg(source))
            return false;
        }
        for (uint32_t index = 0; index != instruction.flags; ++index) {
          auto [destination, source] =
              operandAt(image, instruction.auxiliary + index);
          if (!reg(destination) || source != instruction.source2 + index)
            return false;
        }
        break;
      }
      case OBELISK_RT_DB_RETURN:
        if (instruction.flags || instruction.destination ||
            instruction.source2 || instruction.auxiliary ||
            instruction.immediate ||
            (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0 ||
            instruction.source1 != function.resultCount ||
            !validMap(image, function, function, instruction.source0,
                      instruction.source1))
          return false;
        for (uint32_t index = 0; index != function.resultCount; ++index)
          if (operandAt(image, instruction.source0 + index).first !=
              function.argumentCount + index)
            return false;
        break;
      case OBELISK_RT_DB_CONTINUE:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary ||
            (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
            !hasContinuation(instruction.immediate))
          return false;
        break;
      case OBELISK_RT_DB_SUSPEND: {
        constexpr uint32_t resumeFlags = OBELISK_RT_ACTION_RESUME_REGION_VALID |
                                         OBELISK_RT_ACTION_RESUME_REGION_MASK;
        uint32_t resumeRegion =
            (instruction.auxiliary & OBELISK_RT_ACTION_RESUME_REGION_MASK) >>
            OBELISK_RT_ACTION_RESUME_REGION_SHIFT;
        if (instruction.flags < OBELISK_RT_SUSPEND_DELAY ||
            instruction.flags > OBELISK_RT_SUSPEND_OBSERVER ||
            instruction.destination || instruction.source1 ||
            instruction.source2 ||
            (instruction.auxiliary & ~resumeFlags) != 0 ||
            ((instruction.auxiliary & OBELISK_RT_ACTION_RESUME_REGION_MASK) !=
                 0 &&
             (instruction.auxiliary & OBELISK_RT_ACTION_RESUME_REGION_VALID) ==
                 0) ||
            ((instruction.auxiliary & OBELISK_RT_ACTION_RESUME_REGION_VALID) !=
                 0 &&
             !obelisk_rt_is_process_home_region(resumeRegion)) ||
            !numeric(instruction.source0) ||
            layoutAt(image, function, instruction.source0).width != 64 ||
            (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
            !hasContinuation(instruction.immediate))
          return false;
        break;
      }
      case OBELISK_RT_DB_TERMINATE:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary ||
            (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0)
          return false;
        break;
      case OBELISK_RT_DB_FAIL:
        if (instruction.flags || instruction.destination ||
            instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.source0) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_STATUS)
          return false;
        break;
      case OBELISK_RT_DB_INTRINSIC:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate > UINT32_MAX ||
            !validIntrinsic(image, function,
                            static_cast<uint32_t>(instruction.immediate)))
          return false;
        break;
      default:
        return false;
      }
    }
    if (!validateInitialization(image, function))
      return false;
  }
  return true;
}

struct Logic {
  uint32_t width = 0;
  bool fourState = false;
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
};

Logic readLogic(const uint8_t *frame, const Layout &layout) {
  Logic result{layout.width, layout.kind == OBELISK_RT_DBREG_LOGIC,
               std::vector<uint64_t>(limbCount(layout.width)),
               std::vector<uint64_t>(limbCount(layout.width))};
  std::memcpy(result.value.data(), frame + layout.offset,
              std::min<uint64_t>(layout.size,
                                 result.value.size() * sizeof(uint64_t)));
  if (result.fourState)
    std::memcpy(result.unknown.data(),
                frame + layout.offset + result.value.size() * sizeof(uint64_t),
                result.unknown.size() * sizeof(uint64_t));
  result.value.back() &= finalMask(result.width);
  result.unknown.back() &= finalMask(result.width);
  return result;
}

void writeLogic(uint8_t *frame, const Layout &layout, const Logic &value) {
  uint64_t limbs = limbCount(layout.width);
  std::vector<uint64_t> plane(limbs, 0);
  for (uint64_t index = 0;
       index != std::min<uint64_t>(limbs, value.value.size()); ++index) {
    plane[index] = value.value[index];
    // Four-state to two-state conversion maps both X and Z to zero.
    if (layout.kind == OBELISK_RT_DBREG_BITS && index < value.unknown.size())
      plane[index] &= ~value.unknown[index];
  }
  plane.back() &= finalMask(layout.width);
  std::memcpy(frame + layout.offset, plane.data(),
              std::min<uint64_t>(layout.size, limbs * sizeof(uint64_t)));
  if (layout.kind == OBELISK_RT_DBREG_LOGIC) {
    std::fill(plane.begin(), plane.end(), 0);
    for (uint64_t index = 0;
         index != std::min<uint64_t>(limbs, value.unknown.size()); ++index)
      plane[index] = value.unknown[index];
    plane.back() &= finalMask(layout.width);
    std::memcpy(frame + layout.offset + limbs * sizeof(uint64_t), plane.data(),
                limbs * sizeof(uint64_t));
  }
}

bool anyUnknown(const Logic &value) {
  return std::any_of(value.unknown.begin(), value.unknown.end(),
                     [](uint64_t limb) { return limb != 0; });
}
bool isZero(const Logic &value) {
  return std::all_of(value.value.begin(), value.value.end(),
                     [](uint64_t limb) { return limb == 0; });
}
Logic allX(uint32_t width, bool fourState = true) {
  Logic result{width, fourState, std::vector<uint64_t>(limbCount(width), 0),
               std::vector<uint64_t>(limbCount(width), UINT64_MAX)};
  result.unknown.back() &= finalMask(width);
  return result;
}
void mask(Logic &value) {
  value.value.back() &= finalMask(value.width);
  value.unknown.back() &= finalMask(value.width);
}

int compareUnsigned(const std::vector<uint64_t> &left,
                    const std::vector<uint64_t> &right) {
  // Callers always pass equal-width operands (enforced upstream by the
  // SameOperandsAndResultType / SameTypeOperands op traits). Guard the
  // invariant so a future IR regression trips here instead of reading past
  // the shorter vector.
  assert(left.size() == right.size() && "compareUnsigned width mismatch");
  for (size_t index = left.size(); index != 0; --index)
    if (left[index - 1] != right[index - 1])
      return left[index - 1] < right[index - 1] ? -1 : 1;
  return 0;
}

Logic negate(Logic value) {
  for (uint64_t &limb : value.value)
    limb = ~limb;
  uint64_t carry = 1;
  for (uint64_t &limb : value.value) {
    uint64_t old = limb;
    limb += carry;
    carry = carry && limb < old;
  }
  mask(value);
  return value;
}

double integerToDouble(Logic integer, bool isSigned) {
  bool negative =
      isSigned &&
      ((integer.value[(integer.width - 1) / 64] >> ((integer.width - 1) % 64)) &
       1) != 0;
  if (negative)
    integer = negate(std::move(integer));

  size_t high = integer.value.size();
  while (high != 0 && integer.value[high - 1] == 0)
    --high;
  if (high == 0)
    return 0.0;

  uint64_t highLimb = integer.value[high - 1];
  unsigned highBits = 0;
  for (uint64_t scan = highLimb; scan != 0; scan >>= 1)
    ++highBits;
  uint64_t activeBits = (high - 1) * 64 + highBits;
  if (activeBits <= 64) {
    double result = static_cast<double>(integer.value.front());
    return negative ? -result : result;
  }

  uint64_t exponent = activeBits - 1;
  if (exponent > 1023)
    return negative ? -std::numeric_limits<double>::infinity()
                    : std::numeric_limits<double>::infinity();

  uint64_t shift = activeBits - 53;
  size_t word = static_cast<size_t>(shift / 64);
  unsigned bit = static_cast<unsigned>(shift % 64);
  uint64_t mantissa = integer.value[word] >> bit;
  if (bit != 0 && word + 1 < integer.value.size())
    mantissa |= integer.value[word + 1] << (64 - bit);
  mantissa &= UINT64_C(0x1fffffffffffff);

  uint64_t halfwayPosition = shift - 1;
  size_t halfwayWord = static_cast<size_t>(halfwayPosition / 64);
  unsigned halfwayBit = static_cast<unsigned>(halfwayPosition % 64);
  bool halfway = ((integer.value[halfwayWord] >> halfwayBit) & 1) != 0;
  bool lower = false;
  for (size_t index = 0; index < halfwayWord; ++index)
    lower |= integer.value[index] != 0;
  if (halfwayBit != 0)
    lower |=
        (integer.value[halfwayWord] & ((uint64_t{1} << halfwayBit) - 1)) != 0;
  if (halfway && (lower || (mantissa & 1) != 0))
    ++mantissa;
  if (mantissa == (uint64_t{1} << 53)) {
    mantissa >>= 1;
    if (++exponent > 1023)
      return negative ? -std::numeric_limits<double>::infinity()
                      : std::numeric_limits<double>::infinity();
  }

  uint64_t encoded = (negative ? uint64_t{1} << 63 : 0) |
                     ((exponent + 1023) << 52) |
                     (mantissa & UINT64_C(0x000fffffffffffff));
  double result;
  std::memcpy(&result, &encoded, sizeof(result));
  return result;
}

float integerToFloat(Logic integer, bool isSigned) {
  bool negative =
      isSigned &&
      ((integer.value[(integer.width - 1) / 64] >> ((integer.width - 1) % 64)) &
       1) != 0;
  if (negative)
    integer = negate(std::move(integer));

  size_t high = integer.value.size();
  while (high != 0 && integer.value[high - 1] == 0)
    --high;
  if (high == 0)
    return 0.0f;

  uint64_t highLimb = integer.value[high - 1];
  unsigned highBits = 0;
  for (uint64_t scan = highLimb; scan != 0; scan >>= 1)
    ++highBits;
  uint64_t activeBits = (high - 1) * 64 + highBits;
  if (activeBits <= 64) {
    float result = static_cast<float>(integer.value.front());
    return negative ? -result : result;
  }

  uint64_t exponent = activeBits - 1;
  if (exponent > 127)
    return negative ? -std::numeric_limits<float>::infinity()
                    : std::numeric_limits<float>::infinity();

  uint64_t shift = activeBits - 24;
  size_t word = static_cast<size_t>(shift / 64);
  unsigned bit = static_cast<unsigned>(shift % 64);
  uint64_t mantissa = integer.value[word] >> bit;
  if (bit != 0 && word + 1 < integer.value.size())
    mantissa |= integer.value[word + 1] << (64 - bit);
  mantissa &= UINT64_C(0xffffff);

  uint64_t halfwayPosition = shift - 1;
  size_t halfwayWord = static_cast<size_t>(halfwayPosition / 64);
  unsigned halfwayBit = static_cast<unsigned>(halfwayPosition % 64);
  bool halfway = ((integer.value[halfwayWord] >> halfwayBit) & 1) != 0;
  bool lower = false;
  for (size_t index = 0; index < halfwayWord; ++index)
    lower |= integer.value[index] != 0;
  if (halfwayBit != 0)
    lower |=
        (integer.value[halfwayWord] & ((uint64_t{1} << halfwayBit) - 1)) != 0;
  if (halfway && (lower || (mantissa & 1) != 0))
    ++mantissa;
  if (mantissa == (uint64_t{1} << 24)) {
    mantissa >>= 1;
    if (++exponent > 127)
      return negative ? -std::numeric_limits<float>::infinity()
                      : std::numeric_limits<float>::infinity();
  }

  uint32_t encoded =
      (negative ? uint32_t{1} << 31 : 0) |
      (static_cast<uint32_t>(exponent + 127) << 23) |
      (static_cast<uint32_t>(mantissa) & UINT32_C(0x007fffff));
  float result;
  std::memcpy(&result, &encoded, sizeof(result));
  return result;
}

Logic doubleToInteger(double value, uint32_t width) {
  uint64_t encoded;
  std::memcpy(&encoded, &value, sizeof(encoded));
  bool negative = (encoded >> 63) != 0;
  uint64_t exponentBits = (encoded >> 52) & UINT64_C(0x7ff);
  int64_t exponent = static_cast<int64_t>(exponentBits) - 1023;
  uint64_t mantissa =
      (encoded & UINT64_C(0x000fffffffffffff)) | (uint64_t{1} << 52);

  Logic integer{width, false, std::vector<uint64_t>(limbCount(width)),
                std::vector<uint64_t>(limbCount(width))};
  if (exponent == -1) {
    integer.value.front() = 1;
  } else if (exponent >= 0 && exponentBits != UINT64_C(0x7ff)) {
    if (exponent < 52) {
      unsigned shift = static_cast<unsigned>(52 - exponent);
      integer.value.front() =
          (mantissa >> shift) + ((mantissa >> (shift - 1)) & 1);
    } else {
      uint64_t shift = static_cast<uint64_t>(exponent - 52);
      size_t word = static_cast<size_t>(shift / 64);
      unsigned bit = static_cast<unsigned>(shift % 64);
      if (word < integer.value.size())
        integer.value[word] |= mantissa << bit;
      if (bit != 0 && word + 1 < integer.value.size())
        integer.value[word + 1] |= mantissa >> (64 - bit);
    }
  }
  mask(integer);
  return negative ? negate(std::move(integer)) : integer;
}

Logic add(const Logic &left, const Logic &right, bool subtract) {
  assert(left.value.size() == right.value.size() && "add width mismatch");
  if (anyUnknown(left) || anyUnknown(right))
    return allX(left.width, left.fourState);
  Logic rhs = subtract ? negate(right) : right;
  Logic result{left.width, left.fourState,
               std::vector<uint64_t>(left.value.size()),
               std::vector<uint64_t>(left.value.size())};
  uint64_t carry = 0;
  for (size_t index = 0; index != result.value.size(); ++index) {
    uint64_t first = left.value[index] + rhs.value[index];
    uint64_t carry0 = first < left.value[index];
    uint64_t second = first + carry;
    uint64_t carry1 = second < first;
    result.value[index] = second;
    carry = carry0 | carry1;
  }
  mask(result);
  return result;
}

struct WideProduct {
  uint64_t low;
  uint64_t high;
};

constexpr WideProduct multiply64Portable(uint64_t left, uint64_t right) {
  uint64_t leftLow = static_cast<uint32_t>(left);
  uint64_t leftHigh = left >> 32;
  uint64_t rightLow = static_cast<uint32_t>(right);
  uint64_t rightHigh = right >> 32;
  uint64_t lowLow = leftLow * rightLow;
  uint64_t lowHigh = leftLow * rightHigh;
  uint64_t highLow = leftHigh * rightLow;
  uint64_t highHigh = leftHigh * rightHigh;
  uint64_t middle = (lowLow >> 32) + static_cast<uint32_t>(lowHigh) +
                    static_cast<uint32_t>(highLow);
  return {(middle << 32) | static_cast<uint32_t>(lowLow),
          highHigh + (lowHigh >> 32) + (highLow >> 32) + (middle >> 32)};
}

static_assert(multiply64Portable(UINT64_MAX, UINT64_MAX).low == 1);
static_assert(multiply64Portable(UINT64_MAX, UINT64_MAX).high ==
              UINT64_MAX - 1);
static_assert(multiply64Portable(UINT64_C(1) << 63, UINT64_C(1) << 63).high ==
              (UINT64_C(1) << 62));

Logic multiply(const Logic &left, const Logic &right) {
  assert(left.value.size() == right.value.size() && "multiply width mismatch");
  if (anyUnknown(left) || anyUnknown(right))
    return allX(left.width, left.fourState);
  Logic result{left.width, left.fourState,
               std::vector<uint64_t>(left.value.size()),
               std::vector<uint64_t>(left.value.size())};
  for (size_t i = 0; i != left.value.size(); ++i) {
    uint64_t carry = 0;
    for (size_t j = 0; j + i < result.value.size(); ++j) {
      WideProduct product = multiply64Portable(left.value[i], right.value[j]);
      uint64_t sum = product.low + result.value[i + j];
      uint64_t carry0 = sum < product.low;
      uint64_t withCarry = sum + carry;
      uint64_t carry1 = withCarry < sum;
      result.value[i + j] = withCarry;
      carry = product.high + carry0 + carry1;
    }
  }
  mask(result);
  return result;
}

bool bit(const std::vector<uint64_t> &value, uint64_t index) {
  return ((value[index / 64] >> (index % 64)) & 1) != 0;
}
void setBit(std::vector<uint64_t> &value, uint64_t index, bool enabled) {
  uint64_t mask = uint64_t{1} << (index % 64);
  value[index / 64] =
      enabled ? value[index / 64] | mask : value[index / 64] & ~mask;
}

std::pair<Logic, Logic> divide(const Logic &dividend, const Logic &divisor,
                               bool isSigned) {
  assert(dividend.value.size() == divisor.value.size() &&
         "divide width mismatch");
  if (anyUnknown(dividend) || anyUnknown(divisor) || isZero(divisor))
    return {allX(dividend.width, dividend.fourState),
            allX(dividend.width, dividend.fourState)};
  bool dividendNegative = isSigned && bit(dividend.value, dividend.width - 1);
  bool divisorNegative = isSigned && bit(divisor.value, divisor.width - 1);
  Logic numerator = dividendNegative ? negate(dividend) : dividend;
  Logic denominator = divisorNegative ? negate(divisor) : divisor;
  Logic quotient{dividend.width, dividend.fourState,
                 std::vector<uint64_t>(dividend.value.size()),
                 std::vector<uint64_t>(dividend.value.size())};
  Logic remainder = quotient;
  for (uint64_t index = dividend.width; index != 0; --index) {
    uint64_t carry = bit(numerator.value, index - 1);
    for (uint64_t &limb : remainder.value) {
      uint64_t next = limb >> 63;
      limb = (limb << 1) | carry;
      carry = next;
    }
    mask(remainder);
    if (compareUnsigned(remainder.value, denominator.value) >= 0) {
      remainder = add(remainder, denominator, true);
      setBit(quotient.value, index - 1, true);
    }
  }
  if (dividendNegative != divisorNegative)
    quotient = negate(quotient);
  if (dividendNegative)
    remainder = negate(remainder);
  return {quotient, remainder};
}

Logic bitwise(const Logic &left, const Logic &right, uint16_t opcode) {
  assert(left.value.size() == right.value.size() && "bitwise width mismatch");
  Logic result{left.width, left.fourState,
               std::vector<uint64_t>(left.value.size()),
               std::vector<uint64_t>(left.value.size())};
  for (size_t index = 0; index != result.value.size(); ++index) {
    uint64_t lv = left.value[index], rv = right.value[index];
    uint64_t lu = left.unknown[index], ru = right.unknown[index];
    if (!left.fourState) {
      result.value[index] = opcode == OBELISK_RT_DB_AND  ? lv & rv
                            : opcode == OBELISK_RT_DB_OR ? lv | rv
                                                         : lv ^ rv;
      continue;
    }
    uint64_t lk = ~lu, rk = ~ru;
    if (opcode == OBELISK_RT_DB_AND) {
      uint64_t knownZero = (~lv & lk) | (~rv & rk);
      uint64_t knownOne = (lv & lk) & (rv & rk);
      result.value[index] = knownOne;
      result.unknown[index] = ~(knownZero | knownOne);
    } else if (opcode == OBELISK_RT_DB_OR) {
      uint64_t knownOne = (lv & lk) | (rv & rk);
      uint64_t knownZero = (~lv & lk) & (~rv & rk);
      result.value[index] = knownOne;
      result.unknown[index] = ~(knownZero | knownOne);
    } else {
      result.unknown[index] = lu | ru;
      result.value[index] = (lv ^ rv) & ~result.unknown[index];
    }
  }
  mask(result);
  return result;
}

Logic shift(const Logic &input, const Logic &amount, uint16_t opcode) {
  if (anyUnknown(amount))
    return allX(input.width, input.fourState);
  bool oversized = false;
  uint64_t distance = amount.value.empty() ? 0 : amount.value[0];
  for (size_t index = 1; index < amount.value.size(); ++index)
    oversized |= amount.value[index] != 0;
  oversized |= distance >= input.width;
  Logic result{input.width, input.fourState,
               std::vector<uint64_t>(input.value.size()),
               std::vector<uint64_t>(input.value.size())};
  bool arithmetic = opcode == OBELISK_RT_DB_ASHR;
  bool signValue = arithmetic && bit(input.value, input.width - 1);
  bool signUnknown = arithmetic && bit(input.unknown, input.width - 1);
  for (uint64_t destination = 0; destination != input.width; ++destination) {
    bool fill = false;
    uint64_t source = 0;
    if (opcode == OBELISK_RT_DB_SHL) {
      fill = !oversized && destination >= distance;
      source = destination - std::min<uint64_t>(destination, distance);
    } else {
      fill = !oversized && destination + distance < input.width;
      source = destination + distance;
    }
    setBit(result.value, destination,
           fill ? bit(input.value, source) : arithmetic && signValue);
    setBit(result.unknown, destination,
           fill ? bit(input.unknown, source) : arithmetic && signUnknown);
  }
  return result;
}

struct Frame {
  Function function;
  uint32_t functionIndex = 0;
  uint8_t *data = nullptr;
  uint32_t id = 0;
};

struct BytecodeFrameRoots {
  const Image *image = nullptr;
  Frame *frame = nullptr;
};

void visitObjectWord(const uint8_t *address, ManagedRootVisit visit,
                     void *visitorEnvironment) {
  // Byte-backed interpreter and automatic-state storage does not promise
  // pointer alignment. The collector is non-moving, so an aligned temporary
  // is sufficient for precise marking and avoids undefined typed loads.
  obelisk_rt_object_v1 *object = nullptr;
  std::memcpy(&object, address, sizeof(object));
  visit(visitorEnvironment, &object);
}

void visitManagedWord(const uint8_t *address, ManagedRootVisit visit,
                      void *visitorEnvironment) {
  obelisk_rt_managed_word_v1 word = 0;
  std::memcpy(&word, address, sizeof(word));
  if (word == 0 || (word & 3) == 1)
    return;
  if ((word & 3) != 0)
    return;
  obelisk_rt_object_v1 *object =
      reinterpret_cast<obelisk_rt_object_v1 *>(static_cast<uintptr_t>(word));
  visit(visitorEnvironment, &object);
}

void enumerateBytecodeFrameRoots(void *environment, ManagedRootVisit visit,
                                 void *visitorEnvironment) {
  auto *roots = static_cast<BytecodeFrameRoots *>(environment);
  if (!roots || !roots->image || !roots->frame || !visit)
    return;
  for (uint32_t index = 0; index != roots->frame->function.layoutCount;
       ++index) {
    Layout layout = layoutAt(*roots->image, roots->frame->function, index);
    if (layout.kind == OBELISK_RT_DBREG_STRING) {
      visitManagedWord(roots->frame->data + layout.offset, visit,
                       visitorEnvironment);
      continue;
    }
    if (layout.kind != OBELISK_RT_DBREG_MANAGED &&
        layout.kind != OBELISK_RT_DBREG_MANAGED_REF &&
        layout.kind != OBELISK_RT_DBREG_ARGUMENT_REF)
      continue;
    visitObjectWord(roots->frame->data + layout.offset, visit,
                    visitorEnvironment);
  }
}

class ScopedBytecodeFrameRoots {
public:
  ScopedBytecodeFrameRoots(const Image &image, Frame &frame,
                           obelisk_rt_context *context)
      : roots{&image, &frame} {
    bool hasManaged = false;
    for (uint32_t index = 0; index != frame.function.layoutCount; ++index) {
      uint8_t kind = layoutAt(image, frame.function, index).kind;
      hasManaged |= kind == OBELISK_RT_DBREG_MANAGED ||
                    kind == OBELISK_RT_DBREG_MANAGED_REF ||
                    kind == OBELISK_RT_DBREG_ARGUMENT_REF ||
                    kind == OBELISK_RT_DBREG_STRING;
    }
    if (!hasManaged)
      return;
    lane = obelisk_rt_v1_gc_current_lane(context);
    status = lane ? obelisk_rt_managed_roots_push(
                        lane, &provider, enumerateBytecodeFrameRoots, &roots)
                  : OBELISK_RT_INVALID_LIFECYCLE;
    pushed = status == OBELISK_RT_OK;
  }
  ScopedBytecodeFrameRoots(const ScopedBytecodeFrameRoots &) = delete;
  ScopedBytecodeFrameRoots &
  operator=(const ScopedBytecodeFrameRoots &) = delete;
  ~ScopedBytecodeFrameRoots() {
    if (pushed)
      (void)obelisk_rt_managed_roots_pop(lane, &provider);
  }

  obelisk_rt_status getStatus() const { return status; }

private:
  BytecodeFrameRoots roots;
  ManagedRootProvider provider;
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  obelisk_rt_status status = OBELISK_RT_OK;
  bool pushed = false;
};

struct ExecutionState {
  uint32_t callDepth = 0;
  std::unordered_map<uint32_t, Frame *> frames;
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

bool copyMap(const Image &image, const Frame &source, Frame &destination,
             uint64_t first, uint64_t count) {
  if (first > image.operandCount || count > image.operandCount - first)
    return false;
  // Snapshot sources to make parallel block-argument assignment well defined.
  std::vector<std::vector<uint8_t>> values;
  values.reserve(static_cast<size_t>(count));
  for (uint64_t index = 0; index != count; ++index) {
    auto [destinationRegister, sourceRegister] =
        operandAt(image, first + index);
    (void)destinationRegister;
    if (!validRegister(source.function, sourceRegister))
      return false;
    Layout layout = layoutAt(image, source.function, sourceRegister);
    values.emplace_back(source.data + layout.offset,
                        source.data + layout.offset + layout.size);
  }
  for (uint64_t index = 0; index != count; ++index) {
    auto [destinationRegister, sourceRegister] =
        operandAt(image, first + index);
    (void)sourceRegister;
    if (!validRegister(destination.function, destinationRegister))
      return false;
    Layout layout = layoutAt(image, destination.function, destinationRegister);
    if (layout.size != values[index].size())
      return false;
    std::memcpy(destination.data + layout.offset, values[index].data(),
                layout.size);
  }
  return true;
}

struct StepBudget {
  uint64_t limit = 0;
  uint64_t used = 0;
  bool consume() { return limit == 0 || ++used <= limit; }
};

struct ByteSpan {
  const uint8_t *data = nullptr;
  uint64_t size = 0;
};

std::optional<ByteSpan> readByteSpan(const Image &image, const Frame &frame,
                                     uint32_t reg) {
  if (!validRegister(frame.function, reg))
    return std::nullopt;
  Layout layout = layoutAt(image, frame.function, reg);
  if (layout.kind != OBELISK_RT_DBREG_BYTES || layout.size != 16)
    return std::nullopt;
  uint64_t offset = read64(frame.data + layout.offset);
  uint64_t size = read64(frame.data + layout.offset + 8);
  if (offset > image.constantSize || size > image.constantSize - offset)
    return std::nullopt;
  return ByteSpan{image.data + image.constants + offset, size};
}

std::optional<uint64_t> readScalar(const Image &image, const Frame &frame,
                                   uint32_t reg) {
  if (!validRegister(frame.function, reg))
    return std::nullopt;
  Layout layout = layoutAt(image, frame.function, reg);
  if ((layout.kind != OBELISK_RT_DBREG_BITS &&
       layout.kind != OBELISK_RT_DBREG_LOGIC) ||
      layout.width > 64)
    return std::nullopt;
  Logic value = readLogic(frame.data, layout);
  if (anyUnknown(value))
    return std::nullopt;
  return value.value[0];
}

bool writeScalar(const Image &image, Frame &frame, uint32_t reg,
                 uint64_t value) {
  if (!validRegister(frame.function, reg))
    return false;
  Layout layout = layoutAt(image, frame.function, reg);
  if ((layout.kind != OBELISK_RT_DBREG_BITS &&
       layout.kind != OBELISK_RT_DBREG_LOGIC) ||
      layout.width > 64)
    return false;
  Logic result{layout.width, layout.kind == OBELISK_RT_DBREG_LOGIC,
               std::vector<uint64_t>(1, value), std::vector<uint64_t>(1)};
  writeLogic(frame.data, layout, result);
  return true;
}

bool packBytes(const Image &image, Frame &frame, uint32_t reg,
               const uint8_t *data, uint64_t size, bool highAlignment) {
  if (!validRegister(frame.function, reg))
    return false;
  Layout layout = layoutAt(image, frame.function, reg);
  if (layout.kind != OBELISK_RT_DBREG_BITS &&
      layout.kind != OBELISK_RT_DBREG_LOGIC)
    return false;
  uint64_t capacity = (uint64_t{layout.width} + 7) / 8;
  uint64_t count = std::min(size, capacity);
  Logic result{layout.width, layout.kind == OBELISK_RT_DBREG_LOGIC,
               std::vector<uint64_t>(limbCount(layout.width)),
               std::vector<uint64_t>(limbCount(layout.width))};
  for (uint64_t index = 0; index != count; ++index) {
    uint64_t byte = highAlignment ? capacity - 1 - index : count - 1 - index;
    for (unsigned bitIndex = 0; bitIndex != 8; ++bitIndex) {
      uint64_t destination = byte * 8 + bitIndex;
      if (destination < layout.width)
        setBit(result.value, destination,
               (data[index] & (uint8_t{1} << bitIndex)) != 0);
    }
  }
  writeLogic(frame.data, layout, result);
  return true;
}

bool appendSignalEvent(obelisk_rt_context *context, uint64_t bitOffset,
                       bool oldValue, bool oldUnknown, bool newValue,
                       bool newUnknown, bool evaluateComputedObservers = true) {
  return obelisk_rt_append_signal_event_unlocked(
      context, bitOffset, oldValue, oldUnknown, newValue, newUnknown,
      evaluateComputedObservers);
}

bool rangesOverlap(uint64_t left, uint64_t leftWidth, uint64_t right,
                   uint64_t rightWidth) {
  uint32_t leftID = 0, rightID = 0;
  int64_t leftOffset = 0, rightOffset = 0;
  bool leftAutomatic = decodeAutomaticHandle(left, leftID, leftOffset);
  bool rightAutomatic = decodeAutomaticHandle(right, rightID, rightOffset);
  if (leftAutomatic != rightAutomatic || (leftAutomatic && leftID != rightID))
    return false;
  if (!leftAutomatic) {
    bool leftStatic = decodeStaticHandle(left, leftID, leftOffset);
    bool rightStatic = decodeStaticHandle(right, rightID, rightOffset);
    if (leftStatic != rightStatic || (leftStatic && leftID != rightID))
      return false;
    if (!leftStatic && (!decodeGlobalHandle(left, leftOffset) ||
                        !decodeGlobalHandle(right, rightOffset)))
      return false;
  }
  __int128 leftEnd = static_cast<__int128>(leftOffset) + leftWidth;
  __int128 rightEnd = static_cast<__int128>(rightOffset) + rightWidth;
  return leftWidth != 0 && rightWidth != 0 &&
         static_cast<__int128>(leftOffset) < rightEnd &&
         static_cast<__int128>(rightOffset) < leftEnd;
}

bool signalEdgeMatches(uint32_t requested, uint32_t observed) {
  switch (requested) {
  case OBELISK_RT_WAIT_EDGE_CHANGE:
    return (observed & OBELISK_RT_SIGNAL_CHANGE) != 0;
  case OBELISK_RT_WAIT_EDGE_POSEDGE:
    return (observed & OBELISK_RT_SIGNAL_POSEDGE) != 0;
  case OBELISK_RT_WAIT_EDGE_NEGEDGE:
    return (observed & OBELISK_RT_SIGNAL_NEGEDGE) != 0;
  case OBELISK_RT_WAIT_EDGE_BOTH:
    return (observed &
            (OBELISK_RT_SIGNAL_POSEDGE | OBELISK_RT_SIGNAL_NEGEDGE)) != 0;
  default:
    return false;
  }
}

uint32_t transitionEdges(bool oldValue, bool oldUnknown, bool newValue,
                         bool newUnknown) {
  if (oldValue == newValue && oldUnknown == newUnknown)
    return 0;
  uint32_t result = OBELISK_RT_SIGNAL_CHANGE;
  bool oldZero = !oldUnknown && !oldValue;
  bool oldOne = !oldUnknown && oldValue;
  bool newZero = !newUnknown && !newValue;
  bool newOne = !newUnknown && newValue;
  if ((oldZero && !newZero) || (oldUnknown && newOne))
    result |= OBELISK_RT_SIGNAL_POSEDGE;
  if ((oldOne && !newOne) || (oldUnknown && newZero))
    result |= OBELISK_RT_SIGNAL_NEGEDGE;
  return result;
}

const obelisk_rt_observer_descriptor_v1 *
findObserverDescriptor(const obelisk_rt_execution_descriptor_v1 *execution,
                       uint64_t codeUnitID) {
  if (!execution || (execution->observer_count != 0 && !execution->observers))
    return nullptr;
  uint64_t first = 0;
  uint64_t count = execution->observer_count;
  while (count != 0) {
    uint64_t step = count / 2;
    uint64_t index = first + step;
    if (execution->observers[index].code_unit_id < codeUnitID) {
      first = index + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  return first < execution->observer_count &&
                 execution->observers[first].code_unit_id == codeUnitID
             ? &execution->observers[first]
             : nullptr;
}

template <typename T>
T *computedSpan(obelisk_rt_computed_wait_record_v1 *wait, uint64_t offset,
                uint64_t count) {
  if (!wait || offset > wait->total_size ||
      count > (wait->total_size - offset) / sizeof(T))
    return nullptr;
  return reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(wait) + offset);
}

NetAliasCache *getNetAliasCache(const Image &image,
                                obelisk_rt_context *context) {
  if (context->netAliases.execution == context->execution)
    return &context->netAliases;
  NetAliasCache cache;
  cache.execution = context->execution;
  std::vector<CaptureRecord> nets;
  std::vector<CaptureRecord> drivers;
  for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
    CaptureRecord record = captureAt(image, index);
    if (record.function == kNetStateDescriptor) {
      nets.push_back(record);
      cache.nets.push_back({record.valueOffset, record.valueOffset,
                            record.planeSize, (record.argument & 1) != 0});
    } else if (record.function == kDriverStateDescriptor) {
      drivers.push_back(record);
      cache.drivers.push_back(
          {record.valueOffset, record.unknownOffset, record.planeSize, true});
    }
  }

  std::unordered_map<uint64_t, uint64_t> parents;
  auto findRoot = [&](uint64_t value) {
    parents.try_emplace(value, value);
    uint64_t root = value;
    while (parents[root] != root)
      root = parents[root];
    while (parents[value] != value) {
      uint64_t next = parents[value];
      parents[value] = root;
      value = next;
    }
    return root;
  };
  for (const CaptureRecord &net : nets)
    for (uint64_t bit = 0; bit != net.planeSize; ++bit)
      parents.try_emplace(net.valueOffset + bit, net.valueOffset + bit);
  for (uint64_t index = 0; index != image.connectivityCount; ++index) {
    ConnectivityRecord connection = connectivityAt(image, index);
    for (uint64_t bitIndex = 0; bitIndex != connection.width; ++bitIndex) {
      uint64_t lhs = connection.lhsOffset + bitIndex;
      uint64_t rhs = (connection.flags & 1) ? connection.rhsOffset - bitIndex
                                            : connection.rhsOffset + bitIndex;
      uint64_t lhsRoot = findRoot(lhs);
      uint64_t rhsRoot = findRoot(rhs);
      if (lhsRoot != rhsRoot)
        parents[std::max(lhsRoot, rhsRoot)] = std::min(lhsRoot, rhsRoot);
    }
  }

  for (const CaptureRecord &net : nets)
    for (uint64_t bit = 0; bit != net.planeSize; ++bit) {
      uint64_t logicalBit = net.valueOffset + bit;
      uint64_t root = findRoot(logicalBit);
      cache.rootByBit.emplace(logicalBit, root);
      cache.members[root].push_back(logicalBit);
    }
  for (const CaptureRecord &driver : drivers)
    for (uint64_t bit = 0; bit != driver.planeSize; ++bit)
      cache.driverBits[findRoot(driver.unknownOffset + bit)].push_back(
          driver.valueOffset + bit);
  for (auto &[root, component] : cache.members) {
    std::sort(component.begin(), component.end());
    component.erase(std::unique(component.begin(), component.end()),
                    component.end());
  }
  context->netAliases = std::move(cache);
  return &context->netAliases;
}

struct NetPublication {
  uint64_t destination;
  bool oldValue;
  bool oldUnknown;
  bool value;
  bool unknown;
};

bool publishNetBits(obelisk_rt_context *context, const NetAliasCache &cache,
                    std::vector<NetPublication> &publications, bool &changed) {
  std::sort(publications.begin(), publications.end(),
            [](const NetPublication &lhs, const NetPublication &rhs) {
              return lhs.destination < rhs.destination;
            });
  for (const NetPublication &publication : publications) {
    changed |= publication.oldValue != publication.value ||
               publication.oldUnknown != publication.unknown;
    setBit(context->stateValue, publication.destination, publication.value);
    setBit(context->stateUnknown, publication.destination, publication.unknown);
  }
  // Publish every logical alias first, then report transitions in stable bit
  // order so observers never see a partially updated component.
  std::vector<uint64_t> signalHandles;
  std::vector<std::pair<uint64_t, uint64_t>> observerPublications;
  signalHandles.reserve(publications.size());
  for (const NetPublication &publication : publications) {
    uint64_t signalHandle = publication.destination;
    uint64_t publicationHandle = publication.destination;
    uint64_t publicationWidth = 1;
    uint32_t chosenID = UINT32_MAX;
    for (const auto &[id, state] : context->nativeStaticStates)
      if (publication.destination >= state.bitOffset &&
          publication.destination < state.bitOffset + state.bitWidth &&
          id < chosenID) {
        chosenID = id;
        signalHandle = encodeStaticHandle(
            id,
            static_cast<int64_t>(publication.destination - state.bitOffset));
        publicationHandle = encodeStaticHandle(id, 0);
        publicationWidth = state.bitWidth;
      }
    if (chosenID == UINT32_MAX)
      for (const NetAliasRange &net : cache.nets)
        if (publication.destination >= net.valueOffset &&
            publication.destination < net.valueOffset + net.width) {
          publicationHandle = net.valueOffset;
          publicationWidth = net.width;
          break;
        }
    obelisk_rt_invalidate_signal_snapshots_unlocked(context, signalHandle, 1);
    signalHandles.push_back(signalHandle);
    if (publication.oldValue != publication.value ||
        publication.oldUnknown != publication.unknown)
      observerPublications.emplace_back(publicationHandle, publicationWidth);
  }
  for (size_t index = 0; index != publications.size(); ++index) {
    const NetPublication &publication = publications[index];
    if (!appendSignalEvent(context, signalHandles[index], publication.oldValue,
                           publication.oldUnknown, publication.value,
                           publication.unknown, false))
      return false;
  }
  std::sort(observerPublications.begin(), observerPublications.end());
  observerPublications.erase(
      std::unique(observerPublications.begin(), observerPublications.end()),
      observerPublications.end());
  for (auto [handle, width] : observerPublications)
    if (!obelisk_rt_notify_observer_signal_unlocked(context, handle, width))
      return false;
  return true;
}

bool resolveNetRoots(const NetAliasCache &cache, obelisk_rt_context *context,
                     std::vector<uint64_t> affectedRoots, bool &changed) {
  if (affectedRoots.empty())
    return false;
  std::sort(affectedRoots.begin(), affectedRoots.end());
  affectedRoots.erase(std::unique(affectedRoots.begin(), affectedRoots.end()),
                      affectedRoots.end());
  std::vector<NetPublication> publications;
  for (uint64_t root : affectedRoots) {
    auto members = cache.members.find(root);
    if (members == cache.members.end())
      return false;
    bool resolvedValue = true;
    bool resolvedUnknown = true;
    auto componentDrivers = cache.driverBits.find(root);
    if (componentDrivers != cache.driverBits.end()) {
      for (uint64_t driverBit : componentDrivers->second) {
        bool driverValue = bit(context->stateValue, driverBit);
        bool driverUnknown = bit(context->stateUnknown, driverBit);
        bool currentZ = resolvedUnknown && resolvedValue;
        bool driverZ = driverUnknown && driverValue;
        bool currentX = resolvedUnknown && !resolvedValue;
        bool driverX = driverUnknown && !driverValue;
        bool conflict = currentX || driverX || resolvedValue != driverValue;
        bool mergedValue = conflict ? false : resolvedValue;
        bool mergedUnknown = conflict;
        bool withoutCurrentZ = driverZ ? resolvedValue : mergedValue;
        bool withoutCurrentZUnknown = driverZ ? resolvedUnknown : mergedUnknown;
        resolvedValue = currentZ ? driverValue : withoutCurrentZ;
        resolvedUnknown = currentZ ? driverUnknown : withoutCurrentZUnknown;
      }
    }
    for (uint64_t destination : members->second) {
      const NetAliasRange *net = nullptr;
      for (const NetAliasRange &candidate : cache.nets)
        if (destination >= candidate.valueOffset &&
            destination < candidate.valueOffset + candidate.width) {
          net = &candidate;
          break;
        }
      if (!net)
        return false;
      bool publishUnknown = net->fourState && resolvedUnknown;
      bool publishValue = net->fourState
                              ? resolvedValue
                              : (resolvedUnknown ? false : resolvedValue);
      uint64_t mask = uint64_t{1} << (destination % 64);
      bool forced = destination / 64 < context->forceMask.size() &&
                    (context->forceMask[destination / 64] & mask) != 0;
      bool assigned = destination / 64 < context->assignMask.size() &&
                      (context->assignMask[destination / 64] & mask) != 0;
      if (forced || assigned) {
        publishValue = bit(context->stateValue, destination);
        publishUnknown = bit(context->stateUnknown, destination);
      }
      publications.push_back({destination,
                              bit(context->stateValue, destination),
                              bit(context->stateUnknown, destination),
                              publishValue, publishUnknown});
    }
  }
  return publishNetBits(context, cache, publications, changed);
}

bool resolveDrivenNets(const Image &image, obelisk_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd,
                       bool &changed) {
  if (!context || changedBegin < 0 || changedEnd < changedBegin)
    return false;
  NetAliasCache *cache = getNetAliasCache(image, context);
  std::vector<uint64_t> affectedRoots;
  // A net force release names resolved-net state rather than a driver slot.
  // Seed the same component roots from an overlapping net range so release
  // can republish from all current drivers even when no driver just changed.
  for (const NetAliasRange &net : cache->nets) {
    uint64_t netEnd = net.valueOffset + net.width;
    uint64_t overlapBegin = std::max<uint64_t>(
        static_cast<uint64_t>(changedBegin), net.valueOffset);
    uint64_t overlapEnd =
        std::min<uint64_t>(static_cast<uint64_t>(changedEnd), netEnd);
    for (uint64_t netBit = overlapBegin; netBit < overlapEnd; ++netBit) {
      uint64_t root = cache->rootByBit.at(netBit);
      if (std::find(affectedRoots.begin(), affectedRoots.end(), root) ==
          affectedRoots.end())
        affectedRoots.push_back(root);
    }
  }
  for (const NetAliasRange &driver : cache->drivers) {
    uint64_t driverEnd = driver.valueOffset + driver.width;
    uint64_t overlapBegin = std::max<uint64_t>(
        static_cast<uint64_t>(changedBegin), driver.valueOffset);
    uint64_t overlapEnd =
        std::min<uint64_t>(static_cast<uint64_t>(changedEnd), driverEnd);
    for (uint64_t driverBit = overlapBegin; driverBit < overlapEnd;
         ++driverBit) {
      uint64_t root = cache->rootByBit.at(driver.targetOffset + driverBit -
                                          driver.valueOffset);
      if (std::find(affectedRoots.begin(), affectedRoots.end(), root) ==
          affectedRoots.end())
        affectedRoots.push_back(root);
    }
  }
  return resolveNetRoots(*cache, context, std::move(affectedRoots), changed);
}

obelisk_rt_status invokeIntrinsic(const Image &image, Frame &frame,
                                  obelisk_rt_context *context,
                                  uint32_t siteIndex) {
  if (!validIntrinsic(image, frame.function, siteIndex))
    return OBELISK_RT_INVALID_BYTECODE;
  IntrinsicSite site = siteAt(image, siteIndex);
  IntrinsicSignature signature = intrinsicAt(image, site.intrinsic);
  auto inputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + index).second;
  };
  auto outputRegister = [&](uint32_t index) {
    return operandAt(image, site.firstOperand + site.inputCount + index).first;
  };
  auto scalar = [&](uint32_t index) {
    return readScalar(image, frame, inputRegister(index));
  };
  auto bytes = [&](uint32_t index) {
    return readByteSpan(image, frame, inputRegister(index));
  };
  auto sentinel = [&](uint32_t index, uint64_t value) {
    return writeScalar(image, frame, outputRegister(index), value)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_BYTECODE;
  };
  auto realInput = [&](uint32_t index) -> std::optional<double> {
    uint32_t reg = inputRegister(index);
    if (!validRegister(frame.function, reg))
      return std::nullopt;
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_REAL64 || layout.size != sizeof(double))
      return std::nullopt;
    double value = 0.0;
    std::memcpy(&value, frame.data + layout.offset, sizeof(value));
    return value;
  };
  auto writeReal = [&](uint32_t index, double value) {
    uint32_t reg = outputRegister(index);
    if (!validRegister(frame.function, reg))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_REAL64 || layout.size != sizeof(value))
      return OBELISK_RT_INVALID_BYTECODE;
    std::memcpy(frame.data + layout.offset, &value, sizeof(value));
    return OBELISK_RT_OK;
  };
  auto writeStatus = [&](uint32_t index, obelisk_rt_status value) {
    Layout output = layoutAt(image, frame.function, outputRegister(index));
    if (output.kind != OBELISK_RT_DBREG_STATUS || output.size != 8)
      return false;
    uint64_t encoded = static_cast<uint32_t>(value);
    std::memcpy(frame.data + output.offset, &encoded, sizeof(encoded));
    return true;
  };
  auto finishVPI = [&](uint32_t statusIndex, obelisk_rt_status value) {
    return writeStatus(statusIndex, value) ? OBELISK_RT_OK
                                           : OBELISK_RT_INVALID_BYTECODE;
  };
  auto cursorInput = [&](uint32_t index, obelisk_rt_design_cursor_v1 &cursor) {
    auto encoded = scalar(index);
    if (!encoded)
      return false;
    cursor.offset = *encoded;
    return true;
  };
  auto readManaged = [&](uint32_t reg) -> obelisk_rt_object_v1 * {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8)
      return nullptr;
    obelisk_rt_object_v1 *object = nullptr;
    std::memcpy(&object, frame.data + layout.offset, sizeof(object));
    return object;
  };
  auto writeManaged = [&](uint32_t reg, obelisk_rt_object_v1 *object) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8)
      return false;
    std::memcpy(frame.data + layout.offset, &object, sizeof(object));
    return true;
  };
  auto readString = [&](uint32_t reg,
                        obelisk_rt_string_v1 &string) -> bool {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_STRING || layout.size != 8)
      return false;
    std::memcpy(&string, frame.data + layout.offset, sizeof(string));
    return obelisk_rt_validate_string(context, string) == OBELISK_RT_OK;
  };
  auto writeString = [&](uint32_t reg, obelisk_rt_string_v1 string) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_STRING || layout.size != 8)
      return false;
    std::memcpy(frame.data + layout.offset, &string, sizeof(string));
    return true;
  };
  auto readManagedRef = [&](uint32_t reg, obelisk_rt_object_v1 *&object,
                            uint64_t &offset) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_MANAGED_REF || layout.size != 16)
      return false;
    std::memcpy(&object, frame.data + layout.offset, sizeof(object));
    std::memcpy(&offset, frame.data + layout.offset + 8, sizeof(offset));
    return true;
  };
  auto readArgumentRef = [&](uint32_t reg, obelisk_rt_object_v1 *&owner,
                             uint64_t &payload, uint32_t &managed) {
    Layout layout = layoutAt(image, frame.function, reg);
    if (layout.kind != OBELISK_RT_DBREG_ARGUMENT_REF || layout.size != 24)
      return false;
    std::memcpy(&owner, frame.data + layout.offset, sizeof(owner));
    std::memcpy(&payload, frame.data + layout.offset + 8, sizeof(payload));
    uint64_t tag = 0;
    std::memcpy(&tag, frame.data + layout.offset + 16, sizeof(tag));
    if (tag > 2)
      return false;
    managed = static_cast<uint32_t>(tag);
    return true;
  };
  auto readAssocKey = [&](obelisk_rt_object_v1 *array, uint32_t reg,
                          obelisk_rt_assoc_key_v1 &key) {
    Layout layout = layoutAt(image, frame.function, reg);
    key = {};
    if (obelisk_rt_v1_assoc_key_info(array, &key.kind, &key.width) !=
        OBELISK_RT_OK)
      return false;
    if (layout.kind == OBELISK_RT_DBREG_STRING) {
      if (key.kind != OBELISK_RT_ASSOC_KEY_STRING || layout.size != 8)
        return false;
      std::memcpy(&key.string, frame.data + layout.offset, sizeof(key.string));
      return true;
    }
    if ((layout.kind != OBELISK_RT_DBREG_BITS &&
         layout.kind != OBELISK_RT_DBREG_LOGIC) ||
        key.kind == OBELISK_RT_ASSOC_KEY_STRING || layout.width != key.width ||
        layout.width == 0 || layout.width > 64)
      return false;
    uint64_t planeSize =
        layout.kind == OBELISK_RT_DBREG_LOGIC ? layout.size / 2 : layout.size;
    if (planeSize == 0 || planeSize > sizeof(uint64_t))
      return false;
    std::memcpy(&key.value, frame.data + layout.offset,
                static_cast<size_t>(planeSize));
    if (layout.kind == OBELISK_RT_DBREG_LOGIC)
      std::memcpy(&key.unknown, frame.data + layout.offset + planeSize,
                  static_cast<size_t>(planeSize));
    return true;
  };
  auto writeAssocKey = [&](uint32_t reg,
                           const obelisk_rt_assoc_key_v1 &key) {
    Layout layout = layoutAt(image, frame.function, reg);
    std::memset(frame.data + layout.offset, 0, layout.size);
    if (layout.kind == OBELISK_RT_DBREG_STRING) {
      if (key.kind != OBELISK_RT_ASSOC_KEY_STRING || layout.size != 8)
        return false;
      std::memcpy(frame.data + layout.offset, &key.string, sizeof(key.string));
      return true;
    }
    if ((layout.kind != OBELISK_RT_DBREG_BITS &&
         layout.kind != OBELISK_RT_DBREG_LOGIC) ||
        key.kind == OBELISK_RT_ASSOC_KEY_STRING || layout.width != key.width)
      return false;
    uint64_t planeSize =
        layout.kind == OBELISK_RT_DBREG_LOGIC ? layout.size / 2 : layout.size;
    if (planeSize == 0 || planeSize > sizeof(uint64_t))
      return false;
    std::memcpy(frame.data + layout.offset, &key.value,
                static_cast<size_t>(planeSize));
    if (layout.kind == OBELISK_RT_DBREG_LOGIC)
      std::memcpy(frame.data + layout.offset + planeSize, &key.unknown,
                  static_cast<size_t>(planeSize));
    return true;
  };
  switch (signature.id) {
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_SIZE:
    return sentinel(
        0, obelisk_rt_v1_container_size(readManaged(inputRegister(0))));
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE_LIKE: {
    auto size = scalar(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!size)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *result = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_container_create_like(
        lane, readManaged(inputRegister(0)), readManaged(inputRegister(1)),
        *size, &result);
    return status == OBELISK_RT_OK &&
                   !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_READ: {
    auto index = scalar(1);
    if (!index)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    std::memset(frame.data + output.offset, 0, output.size);
    if (output.kind == OBELISK_RT_DBREG_HANDLE) {
      uint64_t event = UINT64_MAX;
      obelisk_rt_status status = obelisk_rt_v1_container_read_checked(
          readManaged(inputRegister(0)), static_cast<int64_t>(*index), &event,
          sizeof(event), nullptr, 0);
      if (status != OBELISK_RT_OK)
        return status;
      uint32_t kind = OBELISK_RT_DESCRIPTOR_EVENT;
      int64_t start = static_cast<int64_t>(event);
      int64_t begin = start == kInvalidHandleStart ? 0 : start;
      int64_t end = start == kInvalidHandleStart
                        ? 0
                        : (start == INT64_MAX ? start : start + 1);
      std::memcpy(frame.data + output.offset, &kind, sizeof(kind));
      std::memcpy(frame.data + output.offset + 8, &begin, sizeof(begin));
      std::memcpy(frame.data + output.offset + 16, &start, sizeof(start));
      std::memcpy(frame.data + output.offset + 24, &end, sizeof(end));
      return OBELISK_RT_OK;
    }
    uint64_t planeSize =
        output.kind == OBELISK_RT_DBREG_LOGIC ? output.size / 2 : output.size;
    void *unknown = output.kind == OBELISK_RT_DBREG_LOGIC
                        ? frame.data + output.offset + planeSize
                        : nullptr;
    return obelisk_rt_v1_container_read_checked(
        readManaged(inputRegister(0)), static_cast<int64_t>(*index),
        frame.data + output.offset, planeSize, unknown,
        unknown ? planeSize : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_WRITE: {
    auto index = scalar(1);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!index)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    Layout input = layoutAt(image, frame.function, inputRegister(2));
    if (input.kind == OBELISK_RT_DBREG_HANDLE) {
      int64_t event = kInvalidHandleStart;
      std::memcpy(&event, frame.data + input.offset + 16, sizeof(event));
      return obelisk_rt_v1_container_write_checked(
          lane, readManaged(inputRegister(0)), static_cast<int64_t>(*index),
          &event, sizeof(event), nullptr, 0);
    }
    uint64_t planeSize =
        input.kind == OBELISK_RT_DBREG_LOGIC ? input.size / 2 : input.size;
    const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                              ? frame.data + input.offset + planeSize
                              : nullptr;
    return obelisk_rt_v1_container_write_checked(
        lane, readManaged(inputRegister(0)), static_cast<int64_t>(*index),
        frame.data + input.offset, planeSize, unknown,
        unknown ? planeSize : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE: {
    std::array<std::optional<uint64_t>, 9> inputs;
    for (uint32_t index = 0; index != 7; ++index)
      inputs[index] = scalar(index);
    inputs[7] = scalar(8);
    inputs[8] = scalar(9);
    if (std::any_of(inputs.begin(), inputs.end(),
                    [](const auto &value) { return !value; }))
      return OBELISK_RT_INVALID_BYTECODE;
    std::optional<ByteSpan> trace =
        readByteSpan(image, frame, inputRegister(7));
    if (!trace || trace->size % sizeof(obelisk_rt_element_trace_slot_v1) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<obelisk_rt_element_trace_slot_v1> traceSlots(
        trace->size / sizeof(obelisk_rt_element_trace_slot_v1));
    if (!traceSlots.empty())
      std::memcpy(traceSlots.data(), trace->data, trace->size);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *result = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_container_create_typed(
        lane, static_cast<uint32_t>(*inputs[0]), *inputs[1],
        static_cast<uint32_t>(*inputs[2]), static_cast<uint32_t>(*inputs[3]),
        *inputs[4], *inputs[5], *inputs[6], traceSlots.data(),
        traceSlots.size(), *inputs[7], *inputs[8], &result);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_CLONE: {
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *result = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_container_clone(
        lane, readManaged(inputRegister(0)), &result);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CONTAINER_DELETE:
    return obelisk_rt_v1_container_delete(readManaged(inputRegister(0)));
  case OBELISK_RT_INTRINSIC_V1_ASSOC_CREATE: {
    std::array<std::optional<uint64_t>, 8> inputs;
    for (uint32_t index = 0; index != 6; ++index)
      inputs[index] = scalar(index);
    inputs[6] = scalar(7);
    inputs[7] = scalar(8);
    if (std::any_of(inputs.begin(), inputs.end(),
                    [](const auto &value) { return !value; }))
      return OBELISK_RT_INVALID_BYTECODE;
    std::optional<ByteSpan> trace =
        readByteSpan(image, frame, inputRegister(6));
    if (!trace || trace->size % sizeof(obelisk_rt_element_trace_slot_v1) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    std::vector<obelisk_rt_element_trace_slot_v1> slots(
        trace->size / sizeof(obelisk_rt_element_trace_slot_v1));
    if (!slots.empty())
      std::memcpy(slots.data(), trace->data, trace->size);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *result = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_assoc_create_typed(
        lane, *inputs[0], static_cast<uint32_t>(*inputs[1]),
        static_cast<uint32_t>(*inputs[2]), *inputs[3], *inputs[4], *inputs[5],
        slots.data(), slots.size(), static_cast<uint32_t>(*inputs[6]),
        *inputs[7], &result);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_READ: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    if (!readAssocKey(array, inputRegister(1), key))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    std::memset(frame.data + output.offset, 0, output.size);
    uint32_t present = 0;
    if (output.kind == OBELISK_RT_DBREG_HANDLE) {
      uint64_t event = UINT64_MAX;
      obelisk_rt_status status = obelisk_rt_v1_assoc_read_checked(
          array, &key, &event, sizeof(event), nullptr, 0, &present);
      if (status != OBELISK_RT_OK)
        return status;
      uint32_t kind = OBELISK_RT_DESCRIPTOR_EVENT;
      int64_t start = static_cast<int64_t>(event);
      int64_t begin = start == kInvalidHandleStart ? 0 : start;
      int64_t end = start == kInvalidHandleStart
                        ? 0
                        : (start == INT64_MAX ? start : start + 1);
      std::memcpy(frame.data + output.offset, &kind, sizeof(kind));
      std::memcpy(frame.data + output.offset + 8, &begin, sizeof(begin));
      std::memcpy(frame.data + output.offset + 16, &start, sizeof(start));
      std::memcpy(frame.data + output.offset + 24, &end, sizeof(end));
      return OBELISK_RT_OK;
    }
    uint64_t planeSize =
        output.kind == OBELISK_RT_DBREG_LOGIC ? output.size / 2 : output.size;
    void *unknown = output.kind == OBELISK_RT_DBREG_LOGIC
                        ? frame.data + output.offset + planeSize
                        : nullptr;
    return obelisk_rt_v1_assoc_read_checked(
        array, &key, frame.data + output.offset, planeSize, unknown,
        unknown ? planeSize : 0, &present);
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_WRITE: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    if (!readAssocKey(array, inputRegister(1), key))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(2));
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (input.kind == OBELISK_RT_DBREG_HANDLE) {
      int64_t event = kInvalidHandleStart;
      std::memcpy(&event, frame.data + input.offset + 16, sizeof(event));
      return obelisk_rt_v1_assoc_write_checked(
          lane, array, &key, &event, sizeof(event), nullptr, 0);
    }
    uint64_t planeSize =
        input.kind == OBELISK_RT_DBREG_LOGIC ? input.size / 2 : input.size;
    const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                              ? frame.data + input.offset + planeSize
                              : nullptr;
    return obelisk_rt_v1_assoc_write_checked(
        lane, array, &key, frame.data + input.offset, planeSize, unknown,
        unknown ? planeSize : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_EXISTS: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    if (!readAssocKey(array, inputRegister(1), key))
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t exists = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_assoc_exists(array, &key, &exists);
    return status == OBELISK_RT_OK ? sentinel(0, exists) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_DELETE: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    return readAssocKey(array, inputRegister(1), key)
               ? obelisk_rt_v1_assoc_delete(array, &key)
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_DEFAULT: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (input.kind == OBELISK_RT_DBREG_HANDLE) {
      int64_t event = kInvalidHandleStart;
      std::memcpy(&event, frame.data + input.offset + 16, sizeof(event));
      return obelisk_rt_v1_assoc_set_default_checked(
          lane, array, &event, sizeof(event), nullptr, 0);
    }
    uint64_t planeSize =
        input.kind == OBELISK_RT_DBREG_LOGIC ? input.size / 2 : input.size;
    const void *unknown = input.kind == OBELISK_RT_DBREG_LOGIC
                              ? frame.data + input.offset + planeSize
                              : nullptr;
    return obelisk_rt_v1_assoc_set_default_checked(
        lane, array, frame.data + input.offset, planeSize, unknown,
        unknown ? planeSize : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_ASSOC_TRAVERSE: {
    auto direction = scalar(2);
    auto endpoint = scalar(3);
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    if (!direction || !endpoint || (*endpoint != 0 && *endpoint != 1) ||
        (static_cast<int64_t>(*direction) != -1 &&
         static_cast<int64_t>(*direction) != 1) ||
        !readAssocKey(array, inputRegister(1), key))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    uint32_t succeeded = 0;
    obelisk_rt_status status;
    if (*endpoint)
      status = static_cast<int64_t>(*direction) > 0
                   ? obelisk_rt_v1_assoc_first(lane, array, &key, &succeeded)
                   : obelisk_rt_v1_assoc_last(lane, array, &key, &succeeded);
    else
      status = static_cast<int64_t>(*direction) > 0
                   ? obelisk_rt_v1_assoc_next(lane, array, &key, &succeeded)
                   : obelisk_rt_v1_assoc_prev(lane, array, &key, &succeeded);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeAssocKey(outputRegister(0), key))
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, succeeded);
  }
  case OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_ASSOC: {
    obelisk_rt_object_v1 *array = readManaged(inputRegister(0));
    obelisk_rt_assoc_key_v1 key{};
    obelisk_rt_object_v1 *watchOwner = nullptr;
    uint64_t ownerPayload = 0;
    uint32_t ownerManaged = 0;
    if (!readAssocKey(array, inputRegister(1), key) ||
        !readArgumentRef(inputRegister(2), watchOwner, ownerPayload,
                         ownerManaged))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *path = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_reference_path_assoc_create(
        lane, array, &key, watchOwner, ownerPayload, ownerManaged, &path);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), path)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_BOUNDED: {
    auto bound = scalar(0);
    uint64_t result = 0;
    if (!bound)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_random_bounded(context, *bound, &result);
    return status == OBELISK_RT_OK ? sentinel(0, result) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_NEXT: {
    uint64_t result = 0;
    obelisk_rt_status status = obelisk_rt_v1_random_next(context, &result);
    return status == OBELISK_RT_OK ? sentinel(0, result) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SEED: {
    auto seed = scalar(0);
    return seed ? obelisk_rt_v1_random_seed(context, *seed)
                : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_LITERAL: {
    auto literal = bytes(0);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!literal || !lane)
      return literal ? OBELISK_RT_INVALID_LIFECYCLE
                     : OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_create(
        lane, reinterpret_cast<const char *>(literal->data), literal->size,
        &result);
    return status == OBELISK_RT_OK &&
                   !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_FROM_PACKED: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    uint64_t planeSize = ((uint64_t{input.width} + 63) / 64) * 8;
    const void *unknown =
        input.kind == OBELISK_RT_DBREG_LOGIC
            ? frame.data + input.offset + planeSize
            : nullptr;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_from_packed(
        lane, frame.data + input.offset, unknown, input.width, &result);
    return status == OBELISK_RT_OK &&
                   !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_TO_PACKED: {
    obelisk_rt_string_v1 input = 0;
    if (!readString(inputRegister(0), input))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    uint64_t planeSize = ((uint64_t{output.width} + 63) / 64) * 8;
    void *unknown = output.kind == OBELISK_RT_DBREG_LOGIC
                        ? frame.data + output.offset + planeSize
                        : nullptr;
    return obelisk_rt_v1_string_to_packed(
        input, frame.data + output.offset, unknown, output.width);
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_CONCAT: {
    std::vector<obelisk_rt_string_span_v1> spans(site.inputCount);
    for (uint32_t index = 0; index != site.inputCount; ++index)
      if (!readString(inputRegister(index), spans[index].string))
        return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_concat_many(
        lane, spans.data(), spans.size(), &result);
    return status == OBELISK_RT_OK &&
                   !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_REPEAT: {
    obelisk_rt_string_v1 input = 0;
    auto count = scalar(1);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !count)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_string_repeat(lane, input, *count, &result);
    return status == OBELISK_RT_OK &&
                   !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_LENGTH: {
    obelisk_rt_string_v1 input = 0;
    return readString(inputRegister(0), input)
               ? sentinel(0, obelisk_rt_v1_string_length(input))
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_GETC: {
    obelisk_rt_string_v1 input = 0;
    auto index = scalar(1);
    return readString(inputRegister(0), input) && index
               ? sentinel(0, obelisk_rt_v1_string_getc(
                                 input, static_cast<int64_t>(*index)))
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_PUTC: {
    obelisk_rt_string_v1 input = 0;
    auto index = scalar(1);
    auto character = scalar(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !index || !character)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_putc(
        lane, input, static_cast<int64_t>(*index),
        static_cast<uint32_t>(*character), &result);
    return status == OBELISK_RT_OK &&
                   !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_SUBSTR: {
    obelisk_rt_string_v1 input = 0;
    auto left = scalar(1);
    auto right = scalar(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !left || !right)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_substr(
        lane, input, static_cast<int64_t>(*left), static_cast<int64_t>(*right),
        &result);
    return status == OBELISK_RT_OK &&
                   !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_COMPARE: {
    obelisk_rt_string_v1 left = 0;
    obelisk_rt_string_v1 right = 0;
    auto insensitive = scalar(2);
    if (!readString(inputRegister(0), left) ||
        !readString(inputRegister(1), right) || !insensitive ||
        *insensitive > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    int32_t result = *insensitive
                         ? obelisk_rt_v1_string_compare_insensitive(left, right)
                         : obelisk_rt_v1_string_compare(left, right);
    return sentinel(0, static_cast<uint32_t>(result));
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_CASE_CONVERT: {
    obelisk_rt_string_v1 input = 0;
    auto upper = scalar(1);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!readString(inputRegister(0), input) || !upper || *upper > 1)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_case_convert(
        lane, input, static_cast<uint32_t>(*upper), &result);
    return status == OBELISK_RT_OK &&
                   !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_PARSE_INTEGER: {
    obelisk_rt_string_v1 input = 0;
    auto radix = scalar(1);
    if (!readString(inputRegister(0), input) || !radix)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_parse_integer(
        input, static_cast<uint32_t>(*radix), &result);
    return status == OBELISK_RT_OK ? sentinel(0, result) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_PARSE_REAL: {
    obelisk_rt_string_v1 input = 0;
    if (!readString(inputRegister(0), input))
      return OBELISK_RT_INVALID_BYTECODE;
    double result = 0.0;
    obelisk_rt_status status =
        obelisk_rt_v1_string_parse_real(input, &result);
    return status == OBELISK_RT_OK ? writeReal(0, result) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_INTEGER: {
    auto value = scalar(0);
    auto radix = scalar(1);
    auto signedMode = scalar(2);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!value || !radix || !signedMode)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status = obelisk_rt_v1_string_format_integer(
        lane, *value, static_cast<uint32_t>(*radix),
        static_cast<uint32_t>(*signedMode), &result);
    return status == OBELISK_RT_OK &&
                   !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_STRING_FORMAT_REAL: {
    auto value = realInput(0);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!value)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 result = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_string_format_real(lane, *value, &result);
    return status == OBELISK_RT_OK &&
                   !writeString(outputRegister(0), result)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_ALLOC: {
    auto id = scalar(0);
    const obelisk_rt_class_descriptor_v1 *descriptor =
        id ? obelisk_rt_managed_class_lookup(context, *id) : nullptr;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!descriptor || !lane)
      return OBELISK_RT_INVALID_DESIGN;
    obelisk_rt_object_v1 *object = nullptr;
    obelisk_rt_status status =
        obelisk_rt_v1_object_allocate(lane, descriptor, &object);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), object)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_COPY: {
    auto id = scalar(1);
    const obelisk_rt_class_descriptor_v1 *descriptor =
        id ? obelisk_rt_managed_class_lookup(context, *id) : nullptr;
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!descriptor || !lane)
      return OBELISK_RT_INVALID_DESIGN;
    obelisk_rt_object_v1 *object = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_object_shallow_copy(
        lane, descriptor, readManaged(inputRegister(0)), &object);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), object)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_IS_INSTANCE: {
    auto id = scalar(1);
    const obelisk_rt_class_descriptor_v1 *descriptor =
        id ? obelisk_rt_managed_class_lookup(context, *id) : nullptr;
    if (!descriptor)
      return OBELISK_RT_INVALID_DESIGN;
    return sentinel(0, obelisk_rt_v1_object_is_instance(
                           readManaged(inputRegister(0)), descriptor));
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_ID:
    return sentinel(0, obelisk_rt_v1_object_id(readManaged(inputRegister(0))));
  case OBELISK_RT_INTRINSIC_V1_CLASS_CAST: {
    auto id = scalar(1);
    const obelisk_rt_class_descriptor_v1 *descriptor =
        id ? obelisk_rt_managed_class_lookup(context, *id) : nullptr;
    if (!descriptor)
      return OBELISK_RT_INVALID_DESIGN;
    obelisk_rt_object_v1 *object = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_object_cast(
        readManaged(inputRegister(0)), descriptor, &object);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), object)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CLASS_FIELD_REF: {
    auto offset = scalar(1);
    if (!offset)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    obelisk_rt_object_v1 *object = readManaged(inputRegister(0));
    std::memcpy(frame.data + output.offset, &object, sizeof(object));
    std::memcpy(frame.data + output.offset + 8, &*offset, sizeof(*offset));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_REFERENCE_PATH_INDEX: {
    auto index = scalar(1);
    if (!index)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout ownerReference = layoutAt(image, frame.function, inputRegister(2));
    if (ownerReference.kind != OBELISK_RT_DBREG_ARGUMENT_REF ||
        ownerReference.size != 24)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t ownerPayload = 0;
    uint32_t ownerManaged = 0;
    obelisk_rt_object_v1 *watchOwner = nullptr;
    std::memcpy(&watchOwner, frame.data + ownerReference.offset,
                sizeof(watchOwner));
    std::memcpy(&ownerPayload, frame.data + ownerReference.offset + 8,
                sizeof(ownerPayload));
    std::memcpy(&ownerManaged, frame.data + ownerReference.offset + 16,
                sizeof(ownerManaged));
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_object_v1 *path = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_reference_path_index_create(
        lane, readManaged(inputRegister(0)), static_cast<int64_t>(*index),
        watchOwner, ownerPayload, ownerManaged, &path);
    if (status != OBELISK_RT_OK)
      return status;
    return writeManaged(outputRegister(0), path) ? OBELISK_RT_OK
                                                 : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_PATH: {
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (output.kind != OBELISK_RT_DBREG_ARGUMENT_REF || output.size != 24)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_object_v1 *path = readManaged(inputRegister(0));
    std::memset(frame.data + output.offset, 0, output.size);
    std::memcpy(frame.data + output.offset, &path, sizeof(path));
    uint64_t managed = 2;
    std::memcpy(frame.data + output.offset + 16, &managed, sizeof(managed));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_REF: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    uint64_t stable = UINT64_MAX;
    if (!encodeCanonicalHandle(frame.data + input.offset, stable))
      return OBELISK_RT_INVALID_HANDLE;
    std::memset(frame.data + output.offset, 0, output.size);
    std::memcpy(frame.data + output.offset + 8, &stable, sizeof(stable));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_MANAGED: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    std::memset(frame.data + output.offset, 0, output.size);
    std::memcpy(frame.data + output.offset, frame.data + input.offset, 16);
    uint64_t managed = 1;
    std::memcpy(frame.data + output.offset + 16, &managed, sizeof(managed));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_LOAD: {
    obelisk_rt_object_v1 *owner = nullptr;
    uint64_t payload = 0;
    uint32_t managed = 0;
    auto planeSize = scalar(1);
    auto bitWidth = scalar(2);
    auto flags = scalar(3);
    if (!readArgumentRef(inputRegister(0), owner, payload, managed) ||
        !planeSize || !bitWidth || !flags || *planeSize == 0 ||
        *bitWidth == 0 || (*flags & ~uint64_t{7}) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    bool fourState = (*flags & 1) != 0;
    uint32_t valueKind = static_cast<uint32_t>(*flags >> 1);
    uint64_t scratchPlaneSize = ((uint64_t{output.width} + 63) / 64) * 8;
    if (*bitWidth != output.width || *planeSize > scratchPlaneSize ||
        output.size != scratchPlaneSize * (fourState ? 2 : 1) ||
        fourState != (output.kind == OBELISK_RT_DBREG_LOGIC) ||
        (valueKind == OBELISK_RT_ARGUMENT_VALUE_CLASS) !=
            (output.kind == OBELISK_RT_DBREG_MANAGED) ||
        (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) !=
            (output.kind == OBELISK_RT_DBREG_STRING) ||
        valueKind > OBELISK_RT_ARGUMENT_VALUE_STRING)
      return OBELISK_RT_INVALID_BYTECODE;
    uint8_t dummy = 0;
    const uint8_t *stateValue =
        context->stateValue.empty()
            ? &dummy
            : reinterpret_cast<const uint8_t *>(context->stateValue.data());
    const uint8_t *stateUnknown =
        context->stateUnknown.empty()
            ? &dummy
            : reinterpret_cast<const uint8_t *>(context->stateUnknown.data());
    return obelisk_rt_v1_argument_ref_load(
        context, stateValue, stateUnknown, image.stateBitCount, owner, payload,
        managed, *bitWidth, *planeSize, fourState, valueKind,
        frame.data + output.offset,
        fourState ? frame.data + output.offset + scratchPlaneSize : nullptr);
  }
  case OBELISK_RT_INTRINSIC_V1_ARGUMENT_REF_STORE: {
    obelisk_rt_object_v1 *owner = nullptr;
    uint64_t payload = 0;
    uint32_t managed = 0;
    auto planeSize = scalar(2);
    auto bitWidth = scalar(3);
    auto flags = scalar(4);
    if (!readArgumentRef(inputRegister(0), owner, payload, managed) ||
        !planeSize || !bitWidth || !flags || *planeSize == 0 ||
        *bitWidth == 0 || (*flags & ~uint64_t{7}) != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    bool fourState = (*flags & 1) != 0;
    uint32_t valueKind = static_cast<uint32_t>(*flags >> 1);
    uint64_t scratchPlaneSize = ((uint64_t{input.width} + 63) / 64) * 8;
    if (*bitWidth != input.width || *planeSize > scratchPlaneSize ||
        input.size != scratchPlaneSize * (fourState ? 2 : 1) ||
        fourState != (input.kind == OBELISK_RT_DBREG_LOGIC) ||
        (valueKind == OBELISK_RT_ARGUMENT_VALUE_CLASS) !=
            (input.kind == OBELISK_RT_DBREG_MANAGED) ||
        (valueKind == OBELISK_RT_ARGUMENT_VALUE_STRING) !=
            (input.kind == OBELISK_RT_DBREG_STRING) ||
        valueKind > OBELISK_RT_ARGUMENT_VALUE_STRING)
      return OBELISK_RT_INVALID_BYTECODE;
    uint8_t dummy = 0;
    uint8_t *stateValue =
        context->stateValue.empty()
            ? &dummy
            : reinterpret_cast<uint8_t *>(context->stateValue.data());
    uint8_t *stateUnknown =
        context->stateUnknown.empty()
            ? &dummy
            : reinterpret_cast<uint8_t *>(context->stateUnknown.data());
    return obelisk_rt_v1_argument_ref_store(
        context, stateValue, stateUnknown, image.stateBitCount, owner, payload,
        managed, *bitWidth, *planeSize, fourState, valueKind,
        frame.data + input.offset,
        fourState ? frame.data + input.offset + scratchPlaneSize : nullptr);
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_ROOT_EXTRACT: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    std::optional<uint64_t> bitOffset = scalar(1);
    if (!bitOffset || (*bitOffset & 63) != 0 || *bitOffset > input.width ||
        64 > input.width - *bitOffset)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_object_v1 *object = nullptr;
    std::memcpy(&object, frame.data + input.offset + *bitOffset / 8,
                sizeof(object));
    return writeManaged(outputRegister(0), object)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_BYTECODE;
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_LOAD: {
    obelisk_rt_object_v1 *object = nullptr;
    uint64_t offset = 0;
    auto planeSize = scalar(1);
    if (!readManagedRef(inputRegister(0), object, offset) || !planeSize ||
        *planeSize == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (output.kind == OBELISK_RT_DBREG_MANAGED) {
      if (*planeSize != sizeof(obelisk_rt_object_v1 *))
        return OBELISK_RT_INVALID_BYTECODE;
      obelisk_rt_object_v1 *value = nullptr;
      obelisk_rt_status status =
          obelisk_rt_v1_object_field_load(object, offset, &value);
      return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), value)
                 ? OBELISK_RT_INVALID_BYTECODE
                 : status;
    }
    uint64_t scratchPlaneSize = ((uint64_t{output.width} + 63) / 64) * 8;
    bool fourState = output.kind == OBELISK_RT_DBREG_LOGIC;
    if (*planeSize > scratchPlaneSize ||
        output.size != scratchPlaneSize * (fourState ? 2 : 1))
      return OBELISK_RT_INVALID_BYTECODE;
    std::memset(frame.data + output.offset, 0,
                static_cast<size_t>(output.size));
    if (fourState)
      return obelisk_rt_v1_object_read_planes(
          object, offset, frame.data + output.offset,
          frame.data + output.offset + scratchPlaneSize, *planeSize);
    return obelisk_rt_v1_object_read(object, offset, frame.data + output.offset,
                                     *planeSize);
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_STORE: {
    obelisk_rt_object_v1 *object = nullptr;
    uint64_t offset = 0;
    auto planeSize = scalar(2);
    if (!readManagedRef(inputRegister(0), object, offset) || !planeSize ||
        *planeSize == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    if (input.kind == OBELISK_RT_DBREG_MANAGED) {
      if (*planeSize != sizeof(obelisk_rt_object_v1 *))
        return OBELISK_RT_INVALID_BYTECODE;
      return obelisk_rt_v1_object_field_store(object, offset,
                                              readManaged(inputRegister(1)));
    }
    uint64_t scratchPlaneSize = ((uint64_t{input.width} + 63) / 64) * 8;
    bool fourState = input.kind == OBELISK_RT_DBREG_LOGIC;
    if (*planeSize > scratchPlaneSize ||
        input.size != scratchPlaneSize * (fourState ? 2 : 1))
      return OBELISK_RT_INVALID_BYTECODE;
    if (fourState)
      return obelisk_rt_v1_object_write_planes(
          object, offset, frame.data + input.offset,
          frame.data + input.offset + scratchPlaneSize, *planeSize);
    return obelisk_rt_v1_object_write(object, offset, frame.data + input.offset,
                                      *planeSize);
  }
  case OBELISK_RT_INTRINSIC_V1_MANAGED_NBA: {
    obelisk_rt_object_v1 *object = nullptr;
    uint64_t offset = 0;
    auto planeSize = scalar(2);
    Layout destination = layoutAt(image, frame.function, inputRegister(0));
    bool path = destination.kind == OBELISK_RT_DBREG_MANAGED;
    if (path)
      object = readManaged(inputRegister(0));
    else if (!readManagedRef(inputRegister(0), object, offset))
      return OBELISK_RT_INVALID_BYTECODE;
    if (!planeSize || *planeSize == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    if (path)
      offset = UINT64_MAX;
    uint64_t delay = 0;
    if (site.inputCount == 4) {
      auto encodedDelay = scalar(3);
      if (!encodedDelay)
        return OBELISK_RT_INVALID_BYTECODE;
      delay = *encodedDelay;
    }
    Layout input = layoutAt(image, frame.function, inputRegister(1));
    const void *value = frame.data + input.offset;
    const void *unknown = nullptr;
    if (input.kind == OBELISK_RT_DBREG_MANAGED) {
      if (*planeSize != sizeof(obelisk_rt_object_v1 *))
        return OBELISK_RT_INVALID_BYTECODE;
    } else {
      uint64_t scratchPlaneSize = ((uint64_t{input.width} + 63) / 64) * 8;
      bool fourState = input.kind == OBELISK_RT_DBREG_LOGIC;
      if (*planeSize > scratchPlaneSize ||
          input.size != scratchPlaneSize * (fourState ? 2 : 1))
        return OBELISK_RT_INVALID_BYTECODE;
      if (fourState)
        unknown = frame.data + input.offset + scratchPlaneSize;
    }
    return obelisk_rt_v1_scheduler_managed_nba(context, object, offset, value,
                                               unknown, *planeSize, delay);
  }
  case OBELISK_RT_INTRINSIC_V1_WEAK_CREATE: {
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    std::optional<uint64_t> classID = scalar(1);
    if (!classID)
      return OBELISK_RT_INVALID_BYTECODE;
    const obelisk_rt_class_descriptor_v1 *descriptor =
        obelisk_rt_managed_class_lookup(context, *classID);
    if (!descriptor)
      return OBELISK_RT_INVALID_DESIGN;
    obelisk_rt_object_v1 *weak = nullptr;
    obelisk_rt_status status = obelisk_rt_v1_weak_create(
        lane, descriptor, readManaged(inputRegister(0)), &weak);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), weak)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_WEAK_GET: {
    obelisk_rt_object_v1 *referent = nullptr;
    obelisk_rt_status status =
        obelisk_rt_v1_weak_get(readManaged(inputRegister(0)), &referent);
    return status == OBELISK_RT_OK && !writeManaged(outputRegister(0), referent)
               ? OBELISK_RT_INVALID_BYTECODE
               : status;
  }
  case OBELISK_RT_INTRINSIC_V1_WEAK_CLEAR:
    return obelisk_rt_v1_weak_clear(readManaged(inputRegister(0)));
  case OBELISK_RT_INTRINSIC_V1_GC_SAFEPOINT: {
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    return lane ? obelisk_rt_v1_gc_safepoint(lane)
                : OBELISK_RT_INVALID_LIFECYCLE;
  }
  case OBELISK_RT_INTRINSIC_V1_SPAWN: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Function callee = functionAt(image, signature.flags);
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
    ScheduledDesignTask task;
    task.parent = context->activeLogicalProcessToken;
    obelisk_rt_random_split_unlocked(context, task.random);
    task.function = signature.flags;
    task.scheduleRank = static_cast<uint32_t>(callee.initialScheduleRank);
    task.scratchOffset = scratchOffset;
    task.scratchSize = callee.scratchSize;
    task.frame = context->designTaskFrames.acquire(
        static_cast<size_t>(scratchOffset + callee.scratchSize));
    uint32_t copied = 0;
    std::unordered_map<uint32_t, uint64_t> retainedAutomaticStates;
    for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
      CaptureRecord capture = captureAt(image, index);
      if (capture.function != signature.flags)
        continue;
      ++copied;
      if (capture.valueOffset == UINT64_MAX)
        continue;
      uint32_t sourceRegister = inputRegister(capture.argument);
      Layout source = layoutAt(image, frame.function, sourceRegister);
      if (source.kind == OBELISK_RT_DBREG_HANDLE) {
        uint64_t stable = UINT64_MAX;
        if (!encodeCanonicalHandle(frame.data + source.offset, stable))
          return OBELISK_RT_INVALID_HANDLE;
        std::memcpy(task.frame.data() + capture.valueOffset, &stable, 8);
        uint32_t automaticID = 0;
        int64_t automaticOffset = 0;
        if (decodeAutomaticHandle(stable, automaticID, automaticOffset) &&
            ++retainedAutomaticStates[automaticID] == 0)
          return OBELISK_RT_OUT_OF_RESOURCES;
        continue;
      }
      std::memcpy(task.frame.data() + capture.valueOffset,
                  frame.data + source.offset, capture.planeSize);
      if (capture.unknownOffset != UINT64_MAX)
        std::memcpy(task.frame.data() + capture.unknownOffset,
                    frame.data + source.offset + capture.planeSize,
                    capture.planeSize);
    }
    if (copied != callee.argumentCount)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t id = 0;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->nextDesignTaskID == 0 ||
          context->nextDesignTaskID > uint64_t{INT64_MAX} ||
          context->nextProcessInsertionSequence == 0 ||
          context->nextProcessInsertionSequence == UINT64_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      for (const auto &[automaticID, count] : retainedAutomaticStates) {
        auto found = context->nativeAutomaticStates.find(automaticID);
        if (found == context->nativeAutomaticStates.end())
          return OBELISK_RT_INVALID_HANDLE;
        if (count > UINT64_MAX - found->second.referenceCount)
          return OBELISK_RT_OUT_OF_RESOURCES;
      }
      id = context->nextDesignTaskID++;
      task.id = id;
      task.phase =
          (callee.flags & OBELISK_RT_DESIGN_FUNCTION_FINAL) != 0 ? 1 : 0;
      task.homeRegion = functionHomeRegion(callee);
      task.queuedRegion = task.homeRegion;
      if (task.phase == 0 && context->activeDesignTaskID != 0)
        task.phase = context->activeDesignTaskPhase;
      if (task.phase == 0 && context->activeNativeProcess)
        for (const ScheduledProcess &process : context->scheduledProcesses)
          if (process.instance == context->activeNativeProcess) {
            task.phase = process.phase;
            break;
          }
      task.controls = context->activeControls;
      task.insertionSequence = context->nextProcessInsertionSequence++;
      task.observedEpoch = context->schedulerEpoch;
      task.observedSignalSequence = context->nextSchedulerSequence;
      for (const auto &[automaticID, count] : retainedAutomaticStates)
        context->nativeAutomaticStates.find(automaticID)
            ->second.referenceCount += count;
      try {
        context->scheduledDesignTasks.push_back(std::move(task));
        obelisk_rt_retain_controls_unlocked(
            context, context->scheduledDesignTasks.back().controls);
      } catch (...) {
        for (const auto &[automaticID, count] : retainedAutomaticStates)
          context->nativeAutomaticStates.find(automaticID)
              ->second.referenceCount -= count;
        throw;
      }
    }
    uint32_t destinationRegister = outputRegister(0);
    Layout destination = layoutAt(image, frame.function, destinationRegister);
    uint8_t *address = frame.data + destination.offset;
    std::memset(address, 0, destination.size);
    uint32_t kind = OBELISK_RT_DESCRIPTOR_PROCESS;
    int64_t begin = static_cast<int64_t>(id);
    int64_t end = id == uint64_t{INT64_MAX} ? begin : begin + 1;
    std::memcpy(address, &kind, 4);
    std::memcpy(address + 8, &begin, 8);
    std::memcpy(address + 16, &begin, 8);
    std::memcpy(address + 24, &end, 8);
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_NBA: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout destination = layoutAt(image, frame.function, inputRegister(1));
    uint32_t kind = 0;
    int64_t begin = 0, start = kInvalidHandleStart, end = 0;
    const uint8_t *address = frame.data + destination.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&start, address + 16, 8);
    std::memcpy(&end, address + 24, 8);
    bool automatic = (kind & kAutomaticHandleKind) != 0;
    uint32_t descriptorKind = kind & ~kAutomaticHandleKind;
    uint64_t objectBase = 0;
    uint32_t objectID = 0;
    std::memcpy(&objectBase, address + 8, 8);
    bool boundedStatic =
        !automatic && decodeStaticHandle(objectBase, objectID, begin);
    if (automatic) {
      if (!decodeAutomaticHandle(objectBase, objectID, begin))
        return OBELISK_RT_INVALID_HANDLE;
    } else if (!boundedStatic) {
      begin = static_cast<int64_t>(objectBase);
    }
    if (descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE || begin > end)
      return OBELISK_RT_INVALID_HANDLE;
    // An invalid dynamic selection is an ignored assignment, matching direct
    // state stores and the native scheduler ABI.
    if (start == kInvalidHandleStart)
      return OBELISK_RT_OK;
    Layout valueLayout =
        layoutAt(image, frame.function, inputRegister(0));
    bool stringValue = valueLayout.kind == OBELISK_RT_DBREG_STRING;
    bool managedValue = valueLayout.kind == OBELISK_RT_DBREG_MANAGED;
    obelisk_rt_string_v1 rootedString = 0;
    if (stringValue &&
        !readString(inputRegister(0), rootedString))
      return OBELISK_RT_INVALID_HANDLE;
    obelisk_rt_object_v1 *rootedManaged =
        managedValue ? readManaged(inputRegister(0)) : nullptr;
    Logic value = readLogic(frame.data, valueLayout);
    uint64_t delay = 0;
    if (site.inputCount == 3) {
      auto encodedDelay = scalar(2);
      if (!encodedDelay)
        return OBELISK_RT_INVALID_BYTECODE;
      delay = *encodedDelay;
    }
    if (managedValue || automatic || boundedStatic) {
      int64_t first = start < begin ? begin - start : 0;
      int64_t last = static_cast<int64_t>(value.width);
      if (start > end || end - start < last)
        last = end - start;
      if (first >= last)
        return OBELISK_RT_OK;
      if ((stringValue || managedValue) && (first != 0 || last != 64))
        return OBELISK_RT_INVALID_BYTECODE;
      int64_t selectedStart = start + first;
      uint64_t stable =
          automatic       ? encodeAutomaticHandle(objectID, selectedStart)
          : boundedStatic ? encodeStaticHandle(objectID, selectedStart)
                          : static_cast<uint64_t>(selectedStart);
      if (stable == UINT64_MAX)
        return OBELISK_RT_INVALID_HANDLE;
      ScheduledNBA update;
      update.valuePlane = nullptr;
      update.unknownPlane = nullptr;
      update.planeBitCount = automatic ? static_cast<uint64_t>(end)
                                       : context->execution->state_bit_count;
      update.bitOffset = stable;
      update.bitWidth = static_cast<uint64_t>(last - first);
      update.stringValue = stringValue;
      update.managedValue = managedValue;
      update.rootedString = rootedString;
      update.rootedManaged = rootedManaged;
      uint64_t bytes = (update.bitWidth + 7) / 8;
      update.value.assign(static_cast<size_t>(bytes), 0);
      if (value.fourState)
        update.unknown.assign(static_cast<size_t>(bytes), 0);
      for (uint64_t bitIndex = 0; bitIndex != update.bitWidth; ++bitIndex) {
        uint64_t source = static_cast<uint64_t>(first) + bitIndex;
        uint8_t mask = static_cast<uint8_t>(1u << (bitIndex % 8));
        if (bit(value.value, source))
          update.value[bitIndex / 8] |= mask;
        if (value.fourState && bit(value.unknown, source))
          update.unknown[bitIndex / 8] |= mask;
      }
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->nextSchedulerSequence == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
      update.execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (update.execRegion == UINT32_MAX)
        return OBELISK_RT_INVALID_LIFECYCLE;
      if (automatic) {
        auto found = context->nativeAutomaticStates.find(objectID);
        if (found == context->nativeAutomaticStates.end())
          return OBELISK_RT_INVALID_HANDLE;
        if (found->second.referenceCount == UINT64_MAX)
          return OBELISK_RT_OUT_OF_RESOURCES;
        update.retainedAutomaticID = objectID;
      }
      update.sequence = context->nextSchedulerSequence++;
      update.dueTime = delay > UINT64_MAX - context->schedulerTime
                           ? UINT64_MAX
                           : context->schedulerTime + delay;
      context->scheduledNBAs.push_back(std::move(update));
      if (automatic)
        ++context->nativeAutomaticStates.find(objectID)->second.referenceCount;
      return OBELISK_RT_OK;
    }
    if (stringValue &&
        (value.width != 64 || start < begin || start < 0 || end < start ||
         end - start < 64))
      return OBELISK_RT_INVALID_BYTECODE;
    ScheduledDesignNBA update;
    update.handleKind = kind;
    update.begin = begin;
    update.start = start;
    update.end = end;
    update.bitWidth = value.width;
    update.stringValue = stringValue;
    update.rootedString = rootedString;
    update.value = std::move(value.value);
    update.unknown = std::move(value.unknown);
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->nextSchedulerSequence == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
      update.execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (update.execRegion == UINT32_MAX)
        return OBELISK_RT_INVALID_LIFECYCLE;
      update.sequence = context->nextSchedulerSequence++;
      update.dueTime = delay > UINT64_MAX - context->schedulerTime
                           ? UINT64_MAX
                           : context->schedulerTime + delay;
      context->scheduledDesignNBAs.push_back(std::move(update));
    }
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout event = layoutAt(image, frame.function, inputRegister(0));
    uint32_t kind = 0;
    int64_t start = -1;
    const uint8_t *address = frame.data + event.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&start, address + 16, 8);
    if (kind != OBELISK_RT_DESCRIPTOR_EVENT || start < 0)
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t stableID = static_cast<uint64_t>(start);
    uint64_t delay = 0;
    if (site.inputCount == 2) {
      std::optional<uint64_t> encodedDelay = scalar(1);
      if (!encodedDelay)
        return OBELISK_RT_INVALID_BYTECODE;
      delay = *encodedDelay;
    }
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint32_t retainedAutomaticID = 0;
    int64_t automaticOffset = 0;
    if (decodeAutomaticHandle(stableID, retainedAutomaticID, automaticOffset)) {
      auto found = context->nativeAutomaticStates.find(retainedAutomaticID);
      if (found == context->nativeAutomaticStates.end())
        return OBELISK_RT_INVALID_HANDLE;
      if (signature.flags != 0 && found->second.referenceCount == UINT64_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
    }
    if (signature.flags != 0) {
      if (context->nextSchedulerSequence == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
      uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                             ? UINT64_MAX
                             : context->schedulerTime + delay;
      uint32_t execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (execRegion == UINT32_MAX)
        return OBELISK_RT_INVALID_LIFECYCLE;
      context->scheduledDesignEvents.push_back({context->nextSchedulerSequence,
                                                dueTime, execRegion, stableID,
                                                retainedAutomaticID});
      if (retainedAutomaticID != 0)
        ++context->nativeAutomaticStates.find(retainedAutomaticID)
              ->second.referenceCount;
      ++context->nextSchedulerSequence;
      return OBELISK_RT_OK;
    }
    EventState &eventState = context->events[stableID];
    if (++eventState.generation == 0)
      eventState.generation = 1;
    eventState.lastTriggeredTime = context->schedulerTime;
    if (!obelisk_rt_notify_observer_event_unlocked(context, stableID))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_DESIGN
                 : context->schedulerStatus;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGERED: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout event = layoutAt(image, frame.function, inputRegister(0));
    uint32_t kind = 0;
    int64_t start = -1;
    const uint8_t *address = frame.data + event.offset;
    std::memcpy(&kind, address, 4);
    std::memcpy(&start, address + 16, 8);
    if (kind != OBELISK_RT_DESCRIPTOR_EVENT || start < 0)
      return OBELISK_RT_INVALID_HANDLE;
    uint32_t triggered = obelisk_rt_v1_scheduler_event_triggered(
        context, static_cast<uint64_t>(start));
    return sentinel(0, triggered);
  }
  case OBELISK_RT_INTRINSIC_V1_STATE_ALLOC: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout initialLayout = layoutAt(image, frame.function, inputRegister(0));
    uint64_t stable = UINT64_MAX;
    uint64_t initialWidth = initialLayout.width;
    obelisk_rt_status status = OBELISK_RT_OK;
    if (initialLayout.kind == OBELISK_RT_DBREG_MANAGED) {
      obelisk_rt_object_v1 *initial = readManaged(inputRegister(0));
      status = obelisk_rt_native_state_alloc_managed(context, initial, &stable);
    } else {
      Logic initial = readLogic(frame.data, initialLayout);
      initialWidth = initial.width;
      const uint8_t *value =
          reinterpret_cast<const uint8_t *>(initial.value.data());
      const uint8_t *unknown =
          initial.fourState
              ? reinterpret_cast<const uint8_t *>(initial.unknown.data())
              : nullptr;
      if (site.inputCount > 1) {
        std::vector<uint64_t> rootOffsets;
        rootOffsets.reserve(site.inputCount - 1);
        for (uint32_t index = 1; index != site.inputCount; ++index) {
          std::optional<uint64_t> rootOffset = scalar(index);
          if (!rootOffset)
            return OBELISK_RT_INVALID_BYTECODE;
          rootOffsets.push_back(*rootOffset);
        }
        status = obelisk_rt_native_state_alloc_with_root_offsets(
            context, initial.width, value, unknown, std::move(rootOffsets),
            &stable);
      } else
        status = obelisk_rt_v1_native_state_alloc(context, initial.width, value,
                                                  unknown, &stable);
    }
    if (status != OBELISK_RT_OK)
      return status;
    uint32_t id = 0;
    int64_t offset = 0;
    if (!decodeAutomaticHandle(stable, id, offset) || offset != 0)
      return OBELISK_RT_INVALID_HANDLE;
    Layout destination = layoutAt(image, frame.function, outputRegister(0));
    uint8_t *address = frame.data + destination.offset;
    std::memset(address, 0, destination.size);
    uint32_t kind = kAutomaticHandleKind | OBELISK_RT_DESCRIPTOR_STORAGE;
    uint64_t base = stable;
    int64_t begin = 0;
    int64_t start = 0;
    int64_t end = static_cast<int64_t>(initialWidth);
    std::memcpy(address, &kind, sizeof(kind));
    std::memcpy(address + 8, &base, sizeof(base));
    std::memcpy(address + 16, &start, sizeof(start));
    std::memcpy(address + 24, &end, sizeof(end));
    (void)begin;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_DISABLE_CHILDREN:
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    return obelisk_rt_v1_scheduler_disable_children(context);
  case OBELISK_RT_INTRINSIC_V1_CONTROL_ENTER: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t activation = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_control_enter(context, signature.flags, &activation);
    return status == OBELISK_RT_OK ? sentinel(0, activation) : status;
  }
  case OBELISK_RT_INTRINSIC_V1_CONTROL_LEAVE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> activation = scalar(0);
    if (!activation)
      return OBELISK_RT_INVALID_BYTECODE;
    return obelisk_rt_v1_control_leave(context, *activation);
  }
  case OBELISK_RT_INTRINSIC_V1_CONTROL_DISABLE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t activation = 0;
    if (site.inputCount != 0) {
      std::optional<uint64_t> value = scalar(0);
      if (!value)
        return OBELISK_RT_INVALID_BYTECODE;
      activation = *value;
    }
    return obelisk_rt_v1_control_disable(context,
                                         signature.flags & ~(UINT32_C(1) << 31),
                                         activation, signature.flags >> 31);
  }
  case OBELISK_RT_INTRINSIC_V1_STATIC_ONCE:
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    return sentinel(0, obelisk_rt_v1_static_once(context, signature.flags));
  case OBELISK_RT_INTRINSIC_V1_DEFERRED_ONCE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    std::optional<uint64_t> siteID = scalar(0);
    if (!siteID || *siteID == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(0, obelisk_rt_v1_deferred_once(context, *siteID));
  }
  case OBELISK_RT_INTRINSIC_V1_MONITOR_REGISTER: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Layout process = layoutAt(image, frame.function, inputRegister(0));
    if (process.kind != OBELISK_RT_DBREG_HANDLE || process.size < 32)
      return OBELISK_RT_INVALID_BYTECODE;
    const uint8_t *address = frame.data + process.offset;
    uint32_t kind = 0;
    int64_t begin = 0, start = kInvalidHandleStart, end = 0;
    std::memcpy(&kind, address, 4);
    std::memcpy(&begin, address + 8, 8);
    std::memcpy(&start, address + 16, 8);
    std::memcpy(&end, address + 24, 8);
    if (kind != OBELISK_RT_DESCRIPTOR_PROCESS || begin <= 0 || begin != start ||
        end != begin + 1)
      return OBELISK_RT_INVALID_HANDLE;
    return obelisk_rt_v1_monitor_register(context, static_cast<uint64_t>(begin),
                                          1);
  }
  case OBELISK_RT_INTRINSIC_V1_MONITOR_CONTROL:
    return obelisk_rt_v1_monitor_control(context, signature.flags);
  case OBELISK_RT_INTRINSIC_V1_MONITOR_CURRENT:
    return sentinel(0, obelisk_rt_v1_monitor_current(context));
  case OBELISK_RT_INTRINSIC_V1_IMPORT:
  case OBELISK_RT_INTRINSIC_V1_DPI_IMPORT: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint32_t firstInput = 0;
    obelisk_rt_import_site_v1 importSite{
        OBELISK_RT_VERSION,
        0,
        signature.flags,
        0,
        UINT64_MAX,
        nullptr,
        0,
        0,
        0,
        0,
    };
    uint32_t dataOutputCount = site.outputCount;
    std::vector<uint8_t> dpiInputFlags;
    std::vector<uint8_t> dpiOutputFlags;
    if (signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT) {
      auto metadata = bytes(0);
      if (!metadata || metadata->size < 56 || site.outputCount == 0)
        return OBELISK_RT_INVALID_BYTECODE;
      importSite.version = read32(metadata->data);
      importSite.flags = read32(metadata->data + 4);
      importSite.import_id = read32(metadata->data + 8);
      importSite.reserved = read32(metadata->data + 12);
      importSite.scope_id = read64(metadata->data + 16);
      importSite.source_line = read32(metadata->data + 24);
      importSite.source_column = read32(metadata->data + 28);
      importSite.source_file_size = read64(metadata->data + 32);
      importSite.abi_signature = read64(metadata->data + 40);
      uint32_t logicalInputs = read32(metadata->data + 48);
      uint32_t logicalOutputs = read32(metadata->data + 52);
      uint64_t entryCount = uint64_t{logicalInputs} + uint64_t{logicalOutputs};
      if (entryCount > (UINT64_MAX - 56) / 16)
        return OBELISK_RT_INVALID_BYTECODE;
      uint64_t sourceOffset = 56 + entryCount * 16;
      if (sourceOffset > metadata->size ||
          importSite.source_file_size != metadata->size - sourceOffset ||
          uint64_t{site.inputCount} != uint64_t{logicalInputs} + 1 ||
          uint64_t{site.outputCount} != uint64_t{logicalOutputs} + 1)
        return OBELISK_RT_INVALID_BYTECODE;
      importSite.source_file =
          importSite.source_file_size
              ? reinterpret_cast<const char *>(metadata->data + sourceOffset)
              : nullptr;

      struct ABIEntry {
        uint32_t kind;
        uint32_t direction;
        uint32_t width;
        uint32_t flags;
      };
      auto entry = [&](uint64_t index) {
        const uint8_t *data = metadata->data + 56 + index * 16;
        return ABIEntry{read32(data), read32(data + 4), read32(data + 8),
                        read32(data + 12)};
      };
      auto validEntry = [](ABIEntry abi) {
        if (abi.kind > 7 || abi.direction > 3 || abi.width == 0 ||
            (abi.flags & ~uint32_t{3}) != 0)
          return false;
        bool fourState = (abi.flags & 1) != 0;
        switch (abi.kind) {
        case 0:
          return abi.width == 1 && !fourState;
        case 1:
          return abi.width == 1 && fourState;
        case 2:
          return abi.width == 8 && !fourState;
        case 3:
          return abi.width == 16 && !fourState;
        case 4:
          return abi.width == 32 && !fourState;
        case 5:
          return abi.width == 64 && !fourState;
        case 6:
          return !fourState;
        case 7:
          return fourState;
        }
        return false;
      };
      auto sameValue = [](ABIEntry left, ABIEntry right) {
        return left.kind == right.kind && left.width == right.width &&
               left.flags == right.flags;
      };
      auto matchesLayout = [](ABIEntry abi, Layout layout) {
        uint8_t expectedKind = (abi.flags & 1) != 0 ? OBELISK_RT_DBREG_LOGIC
                                                    : OBELISK_RT_DBREG_BITS;
        return layout.kind == expectedKind && layout.width == abi.width;
      };
      uint64_t hash = UINT64_C(14695981039346656037);
      auto appendHash = [&](uint64_t value, unsigned bytes) {
        for (unsigned index = 0; index != bytes; ++index) {
          hash ^= static_cast<uint8_t>(value >> (index * 8));
          hash *= UINT64_C(1099511628211);
        }
      };
      appendHash(logicalInputs, 8);
      appendHash(logicalOutputs, 8);
      for (uint64_t index = 0; index != entryCount; ++index) {
        ABIEntry abi = entry(index);
        if (!validEntry(abi))
          return OBELISK_RT_INVALID_BYTECODE;
        appendHash(abi.kind, 4);
        appendHash(abi.direction, 4);
        appendHash(abi.width, 4);
        appendHash((abi.flags & 1) != 0, 1);
        appendHash((abi.flags & 2) != 0, 1);
      }
      if (hash == 0)
        hash = 1;
      if (hash != importSite.abi_signature)
        return OBELISK_RT_INVALID_BYTECODE;
      for (uint32_t index = 0; index != logicalInputs; ++index) {
        ABIEntry abi = entry(index);
        if (abi.direction == 3 ||
            !matchesLayout(
                abi, layoutAt(image, frame.function, inputRegister(index + 1))))
          return OBELISK_RT_INVALID_BYTECODE;
        dpiInputFlags.push_back((abi.flags & 2) != 0 ? OBELISK_RT_DBREG_SIGNED
                                                     : 0);
      }
      for (uint32_t index = 0; index != logicalOutputs; ++index) {
        ABIEntry abi = entry(uint64_t{logicalInputs} + index);
        if (!matchesLayout(
                abi, layoutAt(image, frame.function, outputRegister(index))))
          return OBELISK_RT_INVALID_BYTECODE;
        dpiOutputFlags.push_back((abi.flags & 2) != 0 ? OBELISK_RT_DBREG_SIGNED
                                                      : 0);
      }
      Layout statusLayout =
          layoutAt(image, frame.function, outputRegister(logicalOutputs));
      if (statusLayout.kind != OBELISK_RT_DBREG_STATUS)
        return OBELISK_RT_INVALID_BYTECODE;

      uint64_t outputCursor = logicalInputs;
      bool task = (importSite.flags & OBELISK_RT_IMPORT_TASK) != 0;
      if (!task) {
        if (outputCursor >= entryCount || entry(outputCursor).direction != 3)
          return OBELISK_RT_INVALID_BYTECODE;
        ++outputCursor;
      }
      for (uint32_t index = 0; index != logicalInputs; ++index) {
        ABIEntry input = entry(index);
        if (input.direction == 0)
          continue;
        if (outputCursor >= entryCount)
          return OBELISK_RT_INVALID_BYTECODE;
        ABIEntry output = entry(outputCursor++);
        if (output.direction != 1 || !sameValue(input, output))
          return OBELISK_RT_INVALID_BYTECODE;
      }
      if (outputCursor != entryCount)
        return OBELISK_RT_INVALID_BYTECODE;

      firstInput = 1;
      dataOutputCount = logicalOutputs;
    }
    uint32_t inputCount = site.inputCount - firstInput;
    std::vector<obelisk_rt_import_input_v1> inputs;
    std::vector<obelisk_rt_import_output_v1> outputs;
    inputs.reserve(inputCount);
    outputs.reserve(dataOutputCount);
    auto describe = [&](Layout layout, uint8_t *address) {
      uint64_t limbs = layout.kind == OBELISK_RT_DBREG_STATUS ? 1
                       : layout.kind == OBELISK_RT_DBREG_HANDLE
                           ? 4
                           : limbCount(layout.width);
      uint32_t width =
          layout.kind == OBELISK_RT_DBREG_STATUS ? 32 : layout.width;
      return std::tuple<uint32_t, uint64_t, uint64_t *, uint64_t *>{
          width, limbs, reinterpret_cast<uint64_t *>(address),
          layout.kind == OBELISK_RT_DBREG_LOGIC
              ? reinterpret_cast<uint64_t *>(address + limbs * 8)
              : nullptr};
    };
    for (uint32_t index = 0; index != inputCount; ++index) {
      Layout layout =
          layoutAt(image, frame.function, inputRegister(index + firstInput));
      auto [width, limbs, value, unknown] =
          describe(layout, frame.data + layout.offset);
      uint8_t flags = signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT
                          ? dpiInputFlags[index]
                          : layout.flags;
      inputs.push_back({layout.kind, flags, 0, width, value, unknown, limbs});
    }
    for (uint32_t index = 0; index != dataOutputCount; ++index) {
      Layout layout = layoutAt(image, frame.function, outputRegister(index));
      uint8_t *address = frame.data + layout.offset;
      auto [width, limbs, value, unknown] = describe(layout, address);
      uint8_t flags = signature.id == OBELISK_RT_INTRINSIC_V1_DPI_IMPORT
                          ? dpiOutputFlags[index]
                          : layout.flags;
      outputs.push_back({layout.kind, flags, 0, width, value, unknown, limbs});
    }
    obelisk_rt_status importStatus =
        obelisk_rt_v1_import_call(context, &importSite, inputs.data(),
                                  inputCount, outputs.data(), dataOutputCount);
    if (signature.id != OBELISK_RT_INTRINSIC_V1_DPI_IMPORT)
      return importStatus;
    Layout statusLayout =
        layoutAt(image, frame.function, outputRegister(dataOutputCount));
    uint8_t *statusAddress = frame.data + statusLayout.offset;
    std::memset(statusAddress, 0, statusLayout.size);
    uint64_t statusBits = static_cast<uint32_t>(importStatus);
    std::memcpy(statusAddress, &statusBits, sizeof(statusBits));
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_DISPLAY: {
    auto metadata = bytes(0);
    auto descriptor = scalar(1);
    if (!metadata || !descriptor || descriptor.value() > UINT32_MAX ||
        metadata->size < 40 || read32(metadata->data) != 1)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t newline = read32(metadata->data + 4);
    uint32_t radix = read32(metadata->data + 8);
    uint32_t itemCount = read32(metadata->data + 12);
    uint64_t scopeSize = read64(metadata->data + 16);
    uint64_t librarySize = read64(metadata->data + 24);
    uint64_t multiplier = read64(metadata->data + 32);
    uint64_t flagsSize = uint64_t{itemCount} * 4;
    if (newline > 1 ||
        (radix != OBELISK_RT_RADIX_BINARY && radix != OBELISK_RT_RADIX_OCTAL &&
         radix != OBELISK_RT_RADIX_DECIMAL && radix != OBELISK_RT_RADIX_HEX) ||
        multiplier == 0 || flagsSize > metadata->size - 40 ||
        scopeSize > metadata->size - 40 - flagsSize ||
        librarySize > metadata->size - 40 - flagsSize - scopeSize)
      return OBELISK_RT_INVALID_BYTECODE;
    const uint8_t *flags = metadata->data + 40;
    const char *scope = reinterpret_cast<const char *>(flags + flagsSize);
    const char *library = scope + scopeSize;
    uint32_t physical = 2;
    std::vector<Logic> values;
    values.reserve(site.inputCount - 2);
    std::vector<double> realValues;
    realValues.reserve(itemCount);
    std::vector<obelisk_rt_arg_v1> arguments;
    arguments.reserve(itemCount);
    for (uint32_t index = 0; index != itemCount; ++index) {
      uint32_t itemFlags = read32(flags + uint64_t{index} * 4);
      if ((itemFlags & ~uint32_t{31}) != 0 ||
          ((itemFlags & 4) != 0 && (itemFlags & 3) != 0))
        return OBELISK_RT_INVALID_BYTECODE;
      if ((itemFlags & 2) != 0) {
        arguments.push_back({OBELISK_RT_ARG_EMPTY, 0, 0, nullptr, nullptr});
        continue;
      }
      if (physical >= site.inputCount)
        return OBELISK_RT_INVALID_BYTECODE;
      uint32_t reg = inputRegister(physical++);
      Layout layout = layoutAt(image, frame.function, reg);
      if (layout.kind == OBELISK_RT_DBREG_BYTES) {
        auto value = readByteSpan(image, frame, reg);
        if (!value || (itemFlags & 5) != 0)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back({OBELISK_RT_ARG_STRING,
                             OBELISK_RT_ARG_FORMAT_STRING, value->size,
                             value->data, nullptr});
      } else if ((itemFlags & 8) != 0) {
        if (layout.kind != OBELISK_RT_DBREG_STRING || layout.size != 8 ||
            (itemFlags & 7) != 0)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back(
            {OBELISK_RT_ARG_MANAGED_STRING, OBELISK_RT_ARG_FORMAT_STRING, 0,
             frame.data + layout.offset, nullptr});
      } else if ((itemFlags & 16) != 0) {
        if (layout.kind != OBELISK_RT_DBREG_MANAGED || layout.size != 8 ||
            (itemFlags & 15) != 0)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back({OBELISK_RT_ARG_MANAGED_CONTAINER, 0, 0,
                             frame.data + layout.offset, nullptr});
      } else if ((itemFlags & 4) != 0) {
        if (layout.kind != OBELISK_RT_DBREG_REAL32 &&
            layout.kind != OBELISK_RT_DBREG_REAL64)
          return OBELISK_RT_INVALID_BYTECODE;
        double real = 0.0;
        if (layout.kind == OBELISK_RT_DBREG_REAL32) {
          float value = 0.0f;
          std::memcpy(&value, frame.data + layout.offset, sizeof(value));
          real = value;
        } else {
          std::memcpy(&real, frame.data + layout.offset, sizeof(real));
        }
        realValues.push_back(real);
        arguments.push_back(
            {OBELISK_RT_ARG_REAL, 0, 0, &realValues.back(), nullptr});
      } else {
        values.push_back(readLogic(frame.data, layout));
        Logic &value = values.back();
        arguments.push_back({OBELISK_RT_ARG_LOGIC,
                             static_cast<obelisk_rt_arg_flags>(
                                 (itemFlags & 1) ? OBELISK_RT_ARG_SIGNED : 0),
                             value.width, value.value.data(),
                             value.fourState ? value.unknown.data() : nullptr});
      }
    }
    if (physical != site.inputCount)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_format_env_v1 environment{
        scope, scopeSize, library, librarySize, 0, 0, nullptr, 0, multiplier};
    return obelisk_rt_v1_display(context, static_cast<uint32_t>(*descriptor),
                                 newline, static_cast<obelisk_rt_radix>(radix),
                                 arguments.data(), arguments.size(),
                                 &environment);
  }
  case OBELISK_RT_INTRINSIC_V1_FINISH:
  case OBELISK_RT_INTRINSIC_V1_FATAL: {
    auto verbosity = scalar(0);
    if (!verbosity || *verbosity > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    return signature.id == OBELISK_RT_INTRINSIC_V1_FINISH
               ? obelisk_rt_v1_scheduler_finish(
                     context, static_cast<uint32_t>(*verbosity))
               : obelisk_rt_v1_scheduler_fatal(
                     context, static_cast<uint32_t>(*verbosity));
  }
  case OBELISK_RT_INTRINSIC_V1_TERMINATION_REQUESTED:
    return sentinel(0, obelisk_rt_v1_scheduler_termination_requested(context));
  case OBELISK_RT_INTRINSIC_V1_TIME_NOW:
    return sentinel(0, obelisk_rt_v1_scheduler_time(context));
  case OBELISK_RT_INTRINSIC_V1_TIME_TO_REAL: {
    auto ticks = scalar(0);
    auto scale = scalar(1);
    if (!ticks || !scale || *scale == 0)
      return OBELISK_RT_INVALID_BYTECODE;
    return writeReal(0,
                     static_cast<double>(*ticks) / static_cast<double>(*scale));
  }
  case OBELISK_RT_INTRINSIC_V1_TIME_FROM_REAL: {
    auto value = realInput(0);
    auto scale = scalar(1);
    auto quantum = scalar(2);
    if (!value || !scale || !quantum || *scale == 0 || *quantum == 0 ||
        *scale % *quantum != 0)
      return OBELISK_RT_INVALID_BYTECODE;
    double nonnegative = *value >= 0.0 ? *value : 0.0;
    double steps = nonnegative * (static_cast<double>(*scale) /
                                  static_cast<double>(*quantum));
    double rounded = steps + 0.5;
    uint64_t maximumSteps = UINT64_MAX / *quantum;
    uint64_t tickSteps =
        !std::isfinite(rounded) || rounded >= std::ldexp(1.0, 64)
            ? maximumSteps
            : std::min(static_cast<uint64_t>(rounded), maximumSteps);
    return sentinel(0, tickSteps * *quantum);
  }
  case OBELISK_RT_INTRINSIC_V1_REAL_FROM_INTEGER: {
    Layout input = layoutAt(image, frame.function, inputRegister(0));
    Logic integer = readLogic(frame.data, input);
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (output.kind == OBELISK_RT_DBREG_REAL32) {
      float value = integerToFloat(std::move(integer), signature.flags != 0);
      std::memcpy(frame.data + output.offset, &value, sizeof(value));
      return OBELISK_RT_OK;
    }
    return writeReal(0,
                     integerToDouble(std::move(integer), signature.flags != 0));
  }
  case OBELISK_RT_INTRINSIC_V1_REAL_TO_INTEGER: {
    auto value = realInput(0);
    if (!value)
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    Logic integer = doubleToInteger(*value, output.width);
    writeLogic(frame.data, output, integer);
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_REAL_COMPARE: {
    auto lhs = realInput(0);
    auto rhs = realInput(1);
    if (!lhs || !rhs)
      return OBELISK_RT_INVALID_BYTECODE;
    bool result;
    switch (signature.flags) {
    case 0:
      result = *lhs == *rhs;
      break;
    case 1:
      result = *lhs != *rhs;
      break;
    case 2:
      result = *lhs < *rhs;
      break;
    case 3:
      result = *lhs <= *rhs;
      break;
    case 4:
      result = *lhs > *rhs;
      break;
    case 5:
      result = *lhs >= *rhs;
      break;
    default:
      return OBELISK_RT_INVALID_BYTECODE;
    }
    return sentinel(0, result ? 1 : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_COUNT_BITS: {
    Logic input = readLogic(frame.data,
                            layoutAt(image, frame.function, inputRegister(0)));
    bool selected[4] = {};
    for (uint32_t index = 1; index != site.inputCount; ++index) {
      Logic control = readLogic(
          frame.data, layoutAt(image, frame.function, inputRegister(index)));
      unsigned state = (bit(control.unknown, 0) ? 2u : 0u) |
                       (bit(control.value, 0) ? 1u : 0u);
      selected[state] = true;
    }
    uint32_t count = 0;
    for (size_t index = 0; index != input.value.size(); ++index) {
      uint64_t value = input.value[index];
      uint64_t unknown = input.unknown[index];
      uint64_t known = ~unknown;
      uint64_t matches = 0;
      if (selected[0])
        matches |= ~value & known;
      if (selected[1])
        matches |= value & known;
      if (selected[2])
        matches |= ~value & unknown;
      if (selected[3])
        matches |= value & unknown;
      if (index + 1 == input.value.size())
        matches &= finalMask(input.width);
      while (matches) {
        matches &= matches - 1;
        ++count;
      }
    }
    return sentinel(0, count);
  }
  case OBELISK_RT_INTRINSIC_V1_CLOG2: {
    Logic input = readLogic(frame.data,
                            layoutAt(image, frame.function, inputRegister(0)));
    std::vector<uint64_t> value(input.value.size());
    bool nonzero = false;
    for (size_t index = 0; index != value.size(); ++index) {
      value[index] = input.value[index] & ~input.unknown[index];
      nonzero |= value[index] != 0;
    }
    if (!nonzero)
      return sentinel(0, 0);
    for (size_t index = 0; index != value.size(); ++index) {
      uint64_t previous = value[index];
      --value[index];
      if (previous != 0)
        break;
    }
    uint32_t result = 0;
    for (size_t index = value.size(); index != 0; --index) {
      uint64_t limb = value[index - 1];
      if (limb == 0)
        continue;
      unsigned activeBits = 0;
      while (limb) {
        ++activeBits;
        limb >>= 1;
      }
      result = static_cast<uint32_t>((index - 1) * 64 + activeBits);
      break;
    }
    return sentinel(0, result);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_MCD:
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN: {
    auto path = bytes(0);
    auto mode = signature.id == OBELISK_RT_INTRINSIC_V1_FILE_OPEN
                    ? bytes(1)
                    : std::optional<ByteSpan>{};
    if (!path || (signature.id == OBELISK_RT_INTRINSIC_V1_FILE_OPEN && !mode))
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t descriptor = 0;
    obelisk_rt_status status =
        signature.id == OBELISK_RT_INTRINSIC_V1_FILE_OPEN_MCD
            ? obelisk_rt_v1_file_open_mcd(
                  context, reinterpret_cast<const char *>(path->data),
                  path->size, &descriptor)
            : obelisk_rt_v1_file_open(
                  context, reinterpret_cast<const char *>(path->data),
                  path->size, reinterpret_cast<const char *>(mode->data),
                  mode->size, &descriptor);
    return sentinel(0, status == OBELISK_RT_OK ? descriptor : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING_MCD:
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING: {
    obelisk_rt_string_v1 path = 0;
    obelisk_rt_string_v1 mode = 0;
    bool withMode =
        signature.id == OBELISK_RT_INTRINSIC_V1_FILE_OPEN_STRING;
    if (!readString(inputRegister(0), path) ||
        (withMode && !readString(inputRegister(1), mode)))
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t descriptor = 0;
    obelisk_rt_status status =
        withMode ? obelisk_rt_v1_file_open_string(context, path, mode,
                                                  &descriptor)
                 : obelisk_rt_v1_file_open_string_mcd(context, path,
                                                      &descriptor);
    return sentinel(0, status == OBELISK_RT_OK ? descriptor : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_GETLINE_STRING: {
    auto descriptor = scalar(0);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    obelisk_rt_string_v1 string = 0;
    uint32_t count = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_getline_string(
        context, lane, static_cast<uint32_t>(*descriptor), &string, &count);
    if (status != OBELISK_RT_OK)
      return status;
    if (!writeString(outputRegister(0), string))
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, count);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_CLOSE:
  case OBELISK_RT_INTRINSIC_V1_FILE_FLUSH: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    return signature.id == OBELISK_RT_INTRINSIC_V1_FILE_CLOSE
               ? obelisk_rt_v1_file_close(context,
                                          static_cast<uint32_t>(*descriptor))
               : obelisk_rt_v1_file_flush(context,
                                          static_cast<uint32_t>(*descriptor));
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_GETC: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    uint8_t byte = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_getc(
        context, static_cast<uint32_t>(*descriptor), &byte);
    return sentinel(0, status == OBELISK_RT_OK ? byte : UINT32_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_UNGETC: {
    auto byte = scalar(0), descriptor = scalar(1);
    if (!byte || !descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_file_ungetc(context, static_cast<uint32_t>(*descriptor),
                                  static_cast<uint8_t>(*byte));
    return sentinel(0, status == OBELISK_RT_OK ? 0 : UINT32_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_GETLINE: {
    auto descriptor = scalar(0);
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_buffer_v1 line{};
    obelisk_rt_status status = obelisk_rt_v1_file_getline(
        context, static_cast<uint32_t>(*descriptor), output.width / 8, &line);
    bool packed =
        packBytes(image, frame, outputRegister(0), line.data, line.size, false);
    uint64_t count = status == OBELISK_RT_OK ? line.size : 0;
    obelisk_rt_v1_buffer_release(&line);
    if (!packed)
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, count);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_READ_PACKED: {
    auto descriptor = scalar(0);
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t capacity = (uint64_t{output.width} + 7) / 8;
    std::vector<uint8_t> data(static_cast<size_t>(capacity));
    uint64_t count = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_file_read(context, static_cast<uint32_t>(*descriptor),
                                data.data(), capacity, &count);
    if (!packBytes(image, frame, outputRegister(0), data.data(), count, true))
      return OBELISK_RT_INVALID_BYTECODE;
    return sentinel(1, status == OBELISK_RT_OK ? count : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_EOF: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    uint32_t eof = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_eof(
        context, static_cast<uint32_t>(*descriptor), &eof);
    return sentinel(0, status == OBELISK_RT_OK ? eof : 0);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_SEEK: {
    auto descriptor = scalar(0), offset = scalar(1), origin = scalar(2);
    if (!descriptor || !offset || !origin || *descriptor > UINT32_MAX ||
        *origin > OBELISK_RT_SEEK_END)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_file_seek(context, static_cast<uint32_t>(*descriptor),
                                static_cast<int64_t>(*offset),
                                static_cast<obelisk_rt_seek_origin>(*origin));
    return sentinel(0, status == OBELISK_RT_OK ? 0 : UINT32_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_TELL: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    int64_t offset = 0;
    obelisk_rt_status status = obelisk_rt_v1_file_tell(
        context, static_cast<uint32_t>(*descriptor), &offset);
    return sentinel(0, status == OBELISK_RT_OK ? static_cast<uint64_t>(offset)
                                               : UINT64_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_REWIND: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        obelisk_rt_v1_file_rewind(context, static_cast<uint32_t>(*descriptor));
    return sentinel(0, status == OBELISK_RT_OK ? 0 : UINT32_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_ROOT: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    obelisk_rt_status status =
        obelisk_rt_v1_design_root(context->execution, &cursor);
    if (!writeScalar(image, frame, outputRegister(0), cursor.offset))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_CHILD:
  case OBELISK_RT_INTRINSIC_V1_VPI_SIBLING: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{}, result{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        signature.id == OBELISK_RT_INTRINSIC_V1_VPI_CHILD
            ? obelisk_rt_v1_design_child(context->execution, cursor, &result)
            : obelisk_rt_v1_design_sibling(context->execution, cursor, &result);
    if (!writeScalar(image, frame, outputRegister(0), result.offset))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_CHILD_AT:
  case OBELISK_RT_INTRINSIC_V1_VPI_TYPE_CHILD: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{}, result{};
    auto index = scalar(1);
    if (!cursorInput(0, cursor) || !index)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status =
        signature.id == OBELISK_RT_INTRINSIC_V1_VPI_CHILD_AT
            ? obelisk_rt_v1_design_child_at(context->execution, cursor, *index,
                                            &result)
            : obelisk_rt_v1_design_type_child(context->execution, cursor,
                                              *index, &result);
    if (!writeScalar(image, frame, outputRegister(0), result.offset))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_LOOKUP: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    auto name = bytes(0);
    if (!name)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_design_cursor_v1 cursor{};
    obelisk_rt_status status = obelisk_rt_v1_design_lookup(
        context->execution, name->data, name->size, &cursor);
    if (!writeScalar(image, frame, outputRegister(0), cursor.offset))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_INFO: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_design_info_v1 info{};
    obelisk_rt_status status =
        obelisk_rt_v1_design_info(context->execution, cursor, &info);
    std::array<uint64_t, 7> outputs{info.kind,
                                    info.capabilities,
                                    info.handle.id,
                                    info.type_offset,
                                    static_cast<uint64_t>(info.range_left),
                                    static_cast<uint64_t>(info.range_right),
                                    info.bit_width};
    for (uint32_t index = 0; index != outputs.size(); ++index)
      if (!writeScalar(image, frame, outputRegister(index), outputs[index]))
        return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(7, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_NAME: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    const uint8_t *data = nullptr;
    uint64_t size = 0, offset = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_design_name(context->execution, cursor, &data, &size);
    if (status == OBELISK_RT_OK) {
      const uint8_t *begin = context->execution->design_database;
      uint64_t databaseSize = context->execution->design_database_size;
      uintptr_t dataAddress = reinterpret_cast<uintptr_t>(data);
      uintptr_t beginAddress = reinterpret_cast<uintptr_t>(begin);
      if (dataAddress < beginAddress ||
          dataAddress - beginAddress > databaseSize ||
          size > databaseSize - (dataAddress - beginAddress))
        status = OBELISK_RT_INVALID_DESIGN;
      else
        offset = dataAddress - beginAddress;
    }
    if (!writeScalar(image, frame, outputRegister(0), offset) ||
        !writeScalar(image, frame, outputRegister(1), size))
      return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(2, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_TYPE_INFO: {
    if (!context || !context->execution)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_design_type_info_v1 info{};
    obelisk_rt_status status =
        obelisk_rt_v1_design_type_info(context->execution, cursor, &info);
    std::array<uint64_t, 11> outputs{info.kind,
                                     info.flags,
                                     info.bit_width,
                                     static_cast<uint64_t>(info.range_left),
                                     static_cast<uint64_t>(info.range_right),
                                     info.element_type.offset,
                                     info.first_child.offset,
                                     info.child_count,
                                     info.ordinal,
                                     info.tag_bits,
                                     info.packed_offset};
    for (uint32_t index = 0; index != outputs.size(); ++index)
      if (!writeScalar(image, frame, outputRegister(index), outputs[index]))
        return OBELISK_RT_INVALID_BYTECODE;
    return finishVPI(11, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_READ: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    Layout output = layoutAt(image, frame.function, outputRegister(0));
    Logic value{output.width, output.kind == OBELISK_RT_DBREG_LOGIC,
                std::vector<uint64_t>(limbCount(output.width)),
                std::vector<uint64_t>(limbCount(output.width))};
    obelisk_rt_status status = obelisk_rt_v1_design_read(
        context, cursor, value.value.data(), value.unknown.data(), value.width);
    writeLogic(frame.data, output, value);
    return finishVPI(1, status);
  }
  case OBELISK_RT_INTRINSIC_V1_VPI_WRITE: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_design_cursor_v1 cursor{};
    if (!cursorInput(0, cursor))
      return OBELISK_RT_INVALID_BYTECODE;
    Logic value = readLogic(frame.data,
                            layoutAt(image, frame.function, inputRegister(1)));
    obelisk_rt_status status = obelisk_rt_v1_design_write(
        context, cursor, value.value.data(),
        value.fourState ? value.unknown.data() : nullptr, value.width);
    return finishVPI(0, status);
  }
  default:
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

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
        Logic normalized = readLogic(frame.data, destination);
        writeLogic(frame.data, destination, normalized);
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
      case 0:
        value = !anyKnownZero && !unknown;
        resultUnknown = !anyKnownZero && unknown;
        break;
      case 1:
        value = anyKnownOne;
        resultUnknown = !anyKnownOne && unknown;
        break;
      case 2:
        value = parity && !unknown;
        resultUnknown = unknown;
        break;
      case 3:
        value = anyKnownZero;
        resultUnknown = !anyKnownZero && unknown;
        break;
      case 4:
        value = !anyKnownOne && !unknown;
        resultUnknown = !anyKnownOne && unknown;
        break;
      case 5:
        value = !parity && !unknown;
        resultUnknown = unknown;
        break;
      case 6:
        value = anyKnownOne;
        resultUnknown = false;
        break;
      case 7:
        value = !anyKnownOne && !unknown;
        resultUnknown = !anyKnownOne && unknown;
        break;
      case 8:
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
    case OBELISK_RT_DB_XOR:
      write(instruction.destination,
            bitwise(read(instruction.source0), read(instruction.source1),
                    instruction.opcode));
      break;
    case OBELISK_RT_DB_ADD:
    case OBELISK_RT_DB_SUB:
      write(instruction.destination,
            add(read(instruction.source0), read(instruction.source1),
                instruction.opcode == OBELISK_RT_DB_SUB));
      break;
    case OBELISK_RT_DB_MUL:
      write(instruction.destination,
            multiply(read(instruction.source0), read(instruction.source1)));
      break;
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
        case 0:
          return lhs == rhs;
        case 1:
          return lhs != rhs;
        case 2:
          return lhs < rhs;
        case 3:
          return lhs <= rhs;
        case 4:
          return lhs > rhs;
        default:
          return lhs >= rhs;
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
    case OBELISK_RT_DB_ASHR:
      write(instruction.destination,
            shift(read(instruction.source0), read(instruction.source1),
                  instruction.opcode));
      break;
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
        if (instruction.flags == 1) {
          Logic left = read(instruction.source0);
          Logic right = read(instruction.source1);
          Logic result{left.width, true,
                       std::vector<uint64_t>(limbCount(left.width)),
                       std::vector<uint64_t>(limbCount(left.width))};
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
                   std::vector<uint64_t>(limbCount(destination.width)),
                   std::vector<uint64_t>(limbCount(destination.width))};
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
      uint64_t stateOffset = instruction.immediate;
      uint64_t width = instruction.source1;
      if (stateOffset > uint64_t{INT64_MAX} ||
          width > uint64_t{INT64_MAX} - stateOffset)
        return OBELISK_RT_INVALID_HANDLE;
      int64_t begin = static_cast<int64_t>(stateOffset);
      int64_t end = static_cast<int64_t>(stateOffset + width);
      uint64_t base = static_cast<uint64_t>(begin);
      if (context && kind <= OBELISK_RT_DESCRIPTOR_DRIVER) {
        std::lock_guard<std::recursive_mutex> lock(context->mutex);
        for (const auto &[id, state] : context->nativeStaticStates) {
          if (state.bitOffset != stateOffset || state.bitWidth != width)
            continue;
          base = encodeStaticHandle(id, 0);
          if (base == UINT64_MAX)
            return OBELISK_RT_INVALID_HANDLE;
          begin = 0;
          end = static_cast<int64_t>(width);
          break;
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
                   std::vector<uint64_t>(limbCount(destination.width)),
                   std::vector<uint64_t>(limbCount(destination.width))};
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
        if (instruction.opcode == OBELISK_RT_DB_LOAD_FRAME) {
          if (transferSize != value.size)
            std::memset(frame.data + value.offset, 0, value.size);
          std::memcpy(frame.data + value.offset,
                      canonicalFrame + instruction.immediate, transferSize);
        } else
          std::memcpy(canonicalFrame + instruction.immediate,
                      frame.data + value.offset, transferSize);
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
      bool isAssignOverride = isOverride && instruction.flags != 0;
      if (!isLoad && context &&
          context->activeExecRegion == OBELISK_RT_REGION_POSTPONED)
        return OBELISK_RT_INVALID_LIFECYCLE;
      uint32_t valueRegister =
          isLoad ? instruction.destination : instruction.source1;
      Layout valueLayout = layout(valueRegister);
      Logic value =
          !isLoad ? read(valueRegister)
                  : Logic{valueLayout.width,
                          valueLayout.kind == OBELISK_RT_DBREG_LOGIC,
                          std::vector<uint64_t>(limbCount(valueLayout.width)),
                          std::vector<uint64_t>(limbCount(valueLayout.width))};
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
          if (changedHandle == UINT64_MAX ||
              context->nextSchedulerSequence == 0)
            return OBELISK_RT_OUT_OF_RESOURCES;
          context->scheduledSignalEvents.push_back(
              {context->nextSchedulerSequence++, changedHandle, 64,
               OBELISK_RT_SIGNAL_CHANGE});
          obelisk_rt_invalidate_signal_snapshots_unlocked(context,
                                                          changedHandle, 64);
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
        auto found = state.frames.find(frameID);
        if (found == state.frames.end() ||
            !validRegister(found->second->function, registerIndex))
          return OBELISK_RT_INVALID_HANDLE;
        localFrame = found->second;
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
        bool changed = false;
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
              absolute =
                  found->second.bitOffset + static_cast<uint64_t>(start);
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
        Logic oldReal{value.width, false,
                      std::vector<uint64_t>(limbCount(value.width)),
                      std::vector<uint64_t>(limbCount(value.width))};
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
                          : bit(local ? localValue.value : context->stateValue,
                                storageBit);
            bool loadedUnknown =
                automatic
                    ? automaticBit(automaticState->unknown, absolute)
                    : bit(local ? localValue.unknown : context->stateUnknown,
                          storageBit);
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
            bool oldValue =
                automatic ? automaticBit(automaticState->value, absolute)
                          : bit(local ? localValue.value : context->stateValue,
                                storageBit);
            bool oldUnknown =
                automatic
                    ? automaticBit(automaticState->unknown, absolute)
                    : bit(local ? localValue.unknown : context->stateUnknown,
                          storageBit);
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
            } else {
              setBit(local ? localValue.value : context->stateValue, storageBit,
                     newValue);
              setBit(local ? localValue.unknown : context->stateUnknown,
                     storageBit, newUnknown);
            }
            if (!local && !realValue && !equalStringContents)
              transitions.push_back(
                  {automatic ? (automaticBase & ~uint64_t{UINT32_MAX}) |
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
                automatic
                    ? (automaticBase & ~uint64_t{UINT32_MAX}) |
                          static_cast<uint32_t>(start)
                : boundedStatic ? encodeStaticHandle(staticID, start)
                                : static_cast<uint64_t>(start);
            if (context->nextSchedulerSequence == 0)
              return OBELISK_RT_OUT_OF_RESOURCES;
            context->scheduledSignalEvents.push_back(
                {context->nextSchedulerSequence++, realHandle, value.width,
                 OBELISK_RT_SIGNAL_CHANGE});
            obelisk_rt_invalidate_signal_snapshots_unlocked(
                context, realHandle, value.width);
            if (!obelisk_rt_notify_observer_signal_unlocked(
                    context, realHandle, value.width))
              return context->schedulerStatus;
            if (++context->schedulerEpoch == 0)
              context->schedulerEpoch = 1;
            realNotified = true;
          }
        }
        // A blocking assignment publishes its complete packed value before
        // any observer samples it. Append scalar transition records only
        // after every destination bit has been committed.
        for (const PendingTransition &transition : transitions)
          obelisk_rt_invalidate_signal_snapshots_unlocked(context,
                                                          transition.handle, 1);
        for (const PendingTransition &transition : transitions)
          if (!appendSignalEvent(context, transition.handle,
                                 transition.oldValue, transition.oldUnknown,
                                 transition.newValue, transition.newUnknown,
                                 false))
            return context->schedulerStatus;
        if (changed && !transitions.empty() &&
            !obelisk_rt_notify_observer_signal_unlocked(
                context, transitions.front().handle, transitions.size()))
          return context->schedulerStatus;
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
          (instruction.flags != 0 &&
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
      bool releaseAssign = instruction.flags != 0;

      struct PendingTransition {
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
                {boundedStatic ? encodeStaticHandle(staticID, coordinate)
                               : storageBit,
                 oldValue, oldUnknown, newValue, newUnknown});
          }
        }
        for (const PendingTransition &transition : transitions)
          obelisk_rt_invalidate_signal_snapshots_unlocked(context,
                                                          transition.handle, 1);
        for (const PendingTransition &transition : transitions)
          if (!appendSignalEvent(context, transition.handle,
                                 transition.oldValue, transition.oldUnknown,
                                 transition.newValue, transition.newUnknown,
                                 false))
            return context->schedulerStatus;
        for (const PendingTransition &transition : transitions)
          if (!obelisk_rt_notify_observer_signal_unlocked(context,
                                                          transition.handle, 1))
            return context->schedulerStatus;
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
                   instruction.source1))
        return OBELISK_RT_INVALID_BYTECODE;
      pc = instruction.immediate;
      break;
    case OBELISK_RT_DB_BRANCH: {
      Logic condition = read(instruction.destination);
      if (anyUnknown(condition) || !isZero(condition)) {
        if (!copyMap(image, frame, frame, instruction.source0,
                     instruction.source1))
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
                   instruction.source2)) {
        --state.callDepth;
        state.frames.erase(callee.id);
        return OBELISK_RT_INVALID_BYTECODE;
      }
      obelisk_rt_status status = executeFunction(
          image, callee, context, canonicalFrame, canonicalFrameSize,
          calleeFunction.firstInstruction, budget, action, state,
          pendingActivation,
          instruction.auxiliary, instruction.immediate, &frame);
      --state.callDepth;
      state.frames.erase(callee.id);
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
                   instruction.source2)) {
        --state.callDepth;
        state.frames.erase(callee.id);
        return OBELISK_RT_INVALID_BYTECODE;
      }
      status = executeFunction(
          image, callee, context, canonicalFrame, canonicalFrameSize,
          calleeFunction.firstInstruction, budget, action, state,
          pendingActivation,
          instruction.auxiliary, instruction.flags, &frame);
      --state.callDepth;
      state.frames.erase(callee.id);
      if (status != OBELISK_RT_OK)
        return status;
      break;
    }
    case OBELISK_RT_DB_RETURN:
      if (!copyMap(image, frame, frame, instruction.source0,
                   instruction.source1))
        return OBELISK_RT_INVALID_BYTECODE;
      if (!caller)
        return OBELISK_RT_OK;
      if (!copyMap(image, frame, *caller, returnFirst, returnCount))
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
      *action = {
          OBELISK_RT_FRAGMENT_TASK_CALL,
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

} // namespace

void obelisk_rt_enumerate_design_managed_roots(
    obelisk_rt_context *context, ManagedRootVisit visit,
    void *visitorEnvironment) noexcept {
  if (!context || !visit)
    return;
  try {
    bool hasBytecode =
        context->execution &&
        (context->execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) != 0;
    Image image;
    if (hasBytecode) {
      obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
      hasBytecode = parseImage(entry, image);
    }
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    for (auto &[id, state] : context->nativeAutomaticStates) {
      (void)id;
      if (state.managedRootRegistered)
        visit(visitorEnvironment, &state.managedValue);
      for (uint64_t offset : state.managedRootByteOffsets) {
        if (offset > state.value.size() ||
            sizeof(obelisk_rt_object_v1 *) > state.value.size() - offset)
          continue;
        visitObjectWord(state.value.data() + offset, visit, visitorEnvironment);
      }
    }
    for (obelisk_rt_process_instance_v1 *instance :
         context->managedRootProcesses) {
      if (!instance || !instance->descriptor ||
          !instance->descriptor->frame_layout || !instance->frame)
        continue;
      const obelisk_rt_frame_layout_v1 &layout =
          *instance->descriptor->frame_layout;
      for (uint32_t index = 0; index != layout.field_count; ++index) {
        const obelisk_rt_frame_field_v1 &field = layout.fields[index];
        if (field.flags != OBELISK_RT_FRAME_MANAGED_ROOT)
          continue;
        auto **slot = reinterpret_cast<obelisk_rt_object_v1 **>(
            static_cast<uint8_t *>(instance->frame) + field.offset);
        visit(visitorEnvironment, slot);
      }
    }
    for (ScheduledNBA &update : context->scheduledNBAs)
      if (update.managedValue)
        visit(visitorEnvironment, &update.rootedManaged);
      else if (update.stringValue)
        visitManagedWord(
            reinterpret_cast<const uint8_t *>(&update.rootedString), visit,
            visitorEnvironment);
    for (ScheduledDesignNBA &update : context->scheduledDesignNBAs)
      if (update.stringValue)
        visitManagedWord(
            reinterpret_cast<const uint8_t *>(&update.rootedString), visit,
            visitorEnvironment);
    if (!hasBytecode)
      return;
    for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
      if (task.terminated || task.function >= image.functionCount)
        continue;
      Function function = functionAt(image, task.function);
      auto visitOffset = [&](uint64_t offset) {
        if (offset > task.scratchOffset ||
            sizeof(obelisk_rt_object_v1 *) > task.scratchOffset - offset)
          return;
        visitObjectWord(task.frame.data() + offset, visit, visitorEnvironment);
      };
      for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
        CaptureRecord capture = captureAt(image, index);
        if (capture.function != task.function ||
            capture.argument >= function.layoutCount ||
            capture.valueOffset == UINT64_MAX)
          continue;
        uint8_t kind = layoutAt(image, function, capture.argument).kind;
        if (kind == OBELISK_RT_DBREG_STRING) {
          if (capture.valueOffset <= task.scratchOffset &&
              sizeof(obelisk_rt_managed_word_v1) <=
                  task.scratchOffset - capture.valueOffset)
            visitManagedWord(task.frame.data() + capture.valueOffset, visit,
                             visitorEnvironment);
        } else if (kind == OBELISK_RT_DBREG_MANAGED ||
            kind == OBELISK_RT_DBREG_MANAGED_REF ||
            kind == OBELISK_RT_DBREG_ARGUMENT_REF)
          visitOffset(capture.valueOffset);
      }
      uint64_t end = function.firstInstruction + function.instructionCount;
      for (uint64_t pc = function.firstInstruction; pc != end; ++pc) {
        Instruction instruction = instructionAt(image, pc);
        if (instruction.opcode != OBELISK_RT_DB_STORE_FRAME ||
            instruction.source0 >= function.layoutCount)
          continue;
        uint8_t kind = layoutAt(image, function, instruction.source0).kind;
        if (kind == OBELISK_RT_DBREG_STRING) {
          if (instruction.immediate <= task.scratchOffset &&
              sizeof(obelisk_rt_managed_word_v1) <=
                  task.scratchOffset - instruction.immediate)
            visitManagedWord(task.frame.data() + instruction.immediate, visit,
                             visitorEnvironment);
        } else if (kind == OBELISK_RT_DBREG_MANAGED ||
            kind == OBELISK_RT_DBREG_MANAGED_REF ||
            kind == OBELISK_RT_DBREG_ARGUMENT_REF)
          visitOffset(instruction.immediate);
      }
    }
  } catch (...) {
    // The image was validated when the context was created. A collector cannot
    // report through the C ABI, so malformed or concurrently destroyed state
    // is conservatively ignored here and is diagnosed by its owning entry.
  }
}

bool obelisk_rt_validate_activation_bytecode_inventory(
    const obelisk_rt_execution_descriptor_v1 &execution) noexcept {
  if ((execution.flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0)
    return true;
  try {
    obelisk_rt_design_bytecode_entry_v1 entry{&execution, 0, 0};
    Image image;
    if (!parseImage(entry, image))
      return false;
    if (!validateImage(image))
      return false;
    for (uint64_t index = 0; index != execution.activation_count; ++index) {
      const obelisk_rt_activation_descriptor_v1 &activation =
          execution.activations[index];
      if ((activation.flags & OBELISK_RT_ACTIVATION_HAS_BYTECODE) == 0)
        continue;
      if (activation.bytecode_function >= image.functionCount)
        return false;
      Function function = functionAt(image, activation.bytecode_function);
      if (function.id != activation.code_unit_id ||
          (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
          function.resultCount != 0)
        return false;
    }
    for (uint64_t index = 0; index != execution.observer_count; ++index) {
      const obelisk_rt_observer_descriptor_v1 &observer =
          execution.observers[index];
      if (observer.bytecode_function == OBELISK_RT_OBSERVER_NO_BYTECODE)
        continue;
      if (observer.bytecode_function >= image.functionCount)
        return false;
      Function function = functionAt(image, observer.bytecode_function);
      if (function.id != observer.code_unit_id ||
          (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0 ||
          function.resultCount != 1 ||
          function.argumentCount != observer.capture_count + 1)
        return false;
      Layout result = layoutAt(image, function, function.argumentCount);
      bool fourState = (observer.flags & OBELISK_RT_OBSERVER_FOUR_STATE) != 0;
      uint32_t expectedKind =
          (observer.flags & OBELISK_RT_OBSERVER_REAL32) != 0
              ? OBELISK_RT_DBREG_REAL32
          : (observer.flags & OBELISK_RT_OBSERVER_REAL64) != 0
              ? OBELISK_RT_DBREG_REAL64
          : fourState ? OBELISK_RT_DBREG_LOGIC
                      : OBELISK_RT_DBREG_BITS;
      if (layoutAt(image, function, 0).kind != OBELISK_RT_DBREG_HANDLE ||
          layoutAt(image, function, 0).size != 32 ||
          result.width != observer.result_width ||
          result.kind != expectedKind)
        return false;
      for (uint32_t capture = 0; capture != observer.capture_count; ++capture)
        if (layoutAt(image, function, capture + 1).kind !=
                OBELISK_RT_DBREG_HANDLE ||
            layoutAt(image, function, capture + 1).size != 32)
          return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

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
    if (!parseImage(entry, image) || !validateImage(image) ||
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
        : fourState ? OBELISK_RT_DBREG_LOGIC : OBELISK_RT_DBREG_BITS;
    if (contextLayout.kind != OBELISK_RT_DBREG_HANDLE ||
        contextLayout.size != 32 ||
        resultLayout.kind != expectedKind ||
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
    obelisk_rt_status status =
        executeFunction(image, frame, context, nullptr, 0,
                        function.firstInstruction, budget, &action, state,
                        nullptr);
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

bool obelisk_rt_evaluate_design_observers_unlocked(obelisk_rt_context *context,
                                                   uint32_t dependencyKind,
                                                   uint64_t publishedHandle,
                                                   uint64_t publishedWidth) {
  if (!context || context->schedulerStatus != OBELISK_RT_OK)
    return context != nullptr;
  if (!context->execution)
    return true;
  auto findTask = [&](uint64_t id) -> ScheduledDesignTask * {
    for (ScheduledDesignTask &task : context->scheduledDesignTasks)
      if (task.id == id)
        return &task;
    return nullptr;
  };
  auto currentWait =
      [](ScheduledDesignTask &task) -> obelisk_rt_computed_wait_record_v1 * {
    if (task.terminated || !task.started ||
        task.suspendKind != OBELISK_RT_SUSPEND_OBSERVER ||
        task.waitSize < sizeof(obelisk_rt_computed_wait_record_v1) ||
        task.waitOffset > task.scratchOffset ||
        task.waitSize > task.scratchOffset - task.waitOffset)
      return nullptr;
    auto *wait = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
        task.frame.data() + task.waitOffset);
    return wait->version == OBELISK_RT_VERSION &&
                   wait->kind == OBELISK_RT_SUSPEND_OBSERVER &&
                   wait->total_size <= task.waitSize
               ? wait
               : nullptr;
  };
  auto evaluate = [&](uint64_t taskID, uint32_t observerIndex,
                      std::vector<uint64_t> &value,
                      std::vector<uint64_t> &unknown) {
    ScheduledDesignTask *task = findTask(taskID);
    obelisk_rt_computed_wait_record_v1 *wait =
        task ? currentWait(*task) : nullptr;
    if (!task || !wait || observerIndex >= wait->observer_count)
      return false;
    auto *observers = computedSpan<obelisk_rt_computed_observer_v1>(
        wait, wait->observers_offset, wait->observer_count);
    auto *captures = computedSpan<obelisk_rt_computed_capture_v1>(
        wait, wait->captures_offset, wait->capture_count);
    if (!observers || !captures)
      return false;
    obelisk_rt_computed_observer_v1 binding = observers[observerIndex];
    const obelisk_rt_observer_descriptor_v1 *descriptor =
        findObserverDescriptor(context->execution, binding.code_unit_id);
    if (!descriptor || descriptor->capture_count != binding.capture_count ||
        descriptor->bytecode_function == OBELISK_RT_OBSERVER_NO_BYTECODE)
      return false;
    if (context->observerDepth >= 256) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return false;
    }
    std::vector<obelisk_rt_computed_capture_v1> copiedCaptures(
        captures + binding.capture_begin,
        captures + binding.capture_begin + binding.capture_count);
    uint32_t limbs =
        static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
    value.assign(limbs, 0);
    unknown.assign(limbs, 0);

    std::vector<uint64_t> retainedCaptures;
    retainedCaptures.reserve(binding.capture_count);
    obelisk_rt_status status = OBELISK_RT_OK;
    for (uint32_t index = 0; index != binding.capture_count; ++index) {
      if (descriptor->capture_abi[index].kind !=
          OBELISK_RT_OBSERVER_CAPTURE_STORAGE)
        continue;
      status = obelisk_rt_v1_native_state_retain(
          context, copiedCaptures[index].stable_id);
      if (status != OBELISK_RT_OK)
        break;
      retainedCaptures.push_back(copiedCaptures[index].stable_id);
    }
    if (status != OBELISK_RT_OK) {
      for (auto capture = retainedCaptures.rbegin();
           capture != retainedCaptures.rend(); ++capture)
        (void)obelisk_rt_v1_native_state_release(context, *capture, 0);
      context->schedulerStatus = status;
      return false;
    }

    obelisk_rt_process_instance_v1 *producer = context->activeNativeProcess;
    uint64_t producerToken = context->activeLogicalProcessToken;
    uint64_t producerTask = context->activeDesignTaskID;
    bool producerExecuting = context->designTaskExecuting;
    std::vector<uint64_t> producerControls = std::move(context->activeControls);
    std::vector<uint64_t> waiterControls = task->controls;
    context->activeNativeProcess = nullptr;
    context->activeLogicalProcessToken = taskID;
    context->activeDesignTaskID = taskID;
    context->designTaskExecuting = true;
    context->activeControls = std::move(waiterControls);
    ++context->observerDepth;
    {
      ContextCallbackUnlock unlock(context);
      status = obelisk_rt_execute_design_observer(
          *context->execution, context, descriptor->bytecode_function,
          copiedCaptures.data(), binding.capture_count, value.data(),
          unknown.data(), limbs);
    }
    --context->observerDepth;
    waiterControls = std::move(context->activeControls);
    if (ScheduledDesignTask *updated = findTask(taskID);
        updated && !updated->terminated)
      updated->controls = std::move(waiterControls);
    context->activeControls = std::move(producerControls);
    context->activeNativeProcess = producer;
    context->activeLogicalProcessToken = producerToken;
    context->activeDesignTaskID = producerTask;
    context->designTaskExecuting = producerExecuting;
    for (auto capture = retainedCaptures.rbegin();
         capture != retainedCaptures.rend(); ++capture) {
      obelisk_rt_status releaseStatus =
          obelisk_rt_v1_native_state_release(context, *capture, 0);
      if (status == OBELISK_RT_OK && releaseStatus != OBELISK_RT_OK)
        status = releaseStatus;
    }
    if (status != OBELISK_RT_OK) {
      context->schedulerStatus = status;
      return false;
    }
    if (descriptor->result_width % 64 != 0) {
      uint64_t mask = (uint64_t{1} << (descriptor->result_width % 64)) - 1;
      value.back() &= mask;
      unknown.back() &= mask;
    }
    if ((descriptor->flags & OBELISK_RT_OBSERVER_FOUR_STATE) == 0)
      std::fill(unknown.begin(), unknown.end(), 0);
    return true;
  };

  std::vector<uint64_t> taskIDs;
  taskIDs.reserve(context->scheduledDesignTasks.size());
  for (const ScheduledDesignTask &task : context->scheduledDesignTasks)
    if (!task.terminated && task.started &&
        task.suspendKind == OBELISK_RT_SUSPEND_OBSERVER &&
        !task.signalTriggered)
      taskIDs.push_back(task.id);

  for (uint64_t taskID : taskIDs) {
    ScheduledDesignTask *task = findTask(taskID);
    obelisk_rt_computed_wait_record_v1 *wait =
        task ? currentWait(*task) : nullptr;
    if (!task || task->signalTriggered || !wait)
      continue;
    auto *observers = computedSpan<obelisk_rt_computed_observer_v1>(
        wait, wait->observers_offset, wait->observer_count);
    auto *dependencies = computedSpan<obelisk_rt_computed_dependency_v1>(
        wait, wait->dependencies_offset, wait->dependency_count);
    auto *clauses = computedSpan<obelisk_rt_computed_clause_v1>(
        wait, wait->clauses_offset, wait->clause_count);
    if (!observers || !dependencies || !clauses)
      return false;
    for (uint32_t clauseIndex = 0; clauseIndex != wait->clause_count;
         ++clauseIndex) {
      obelisk_rt_computed_clause_v1 clause = clauses[clauseIndex];
      obelisk_rt_computed_observer_v1 primary =
          observers[clause.primary_observer];
      bool affected = false;
      for (uint32_t dependencyIndex = 0;
           dependencyIndex != primary.dependency_count; ++dependencyIndex) {
        const obelisk_rt_computed_dependency_v1 &dependency =
            dependencies[primary.dependency_begin + dependencyIndex];
        if (dependency.kind != dependencyKind)
          continue;
        if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT
                ? dependency.stable_id == publishedHandle
                : rangesOverlap(dependency.stable_id, dependency.width,
                                publishedHandle, publishedWidth)) {
          affected = true;
          break;
        }
      }
      if (!affected)
        continue;

      std::vector<uint64_t> value;
      std::vector<uint64_t> unknown;
      if (!evaluate(taskID, clause.primary_observer, value, unknown))
        return false;
      task = findTask(taskID);
      wait = task ? currentWait(*task) : nullptr;
      if (!task || task->terminated || task->signalTriggered || !wait)
        break;
      const obelisk_rt_observer_descriptor_v1 *descriptor =
          findObserverDescriptor(context->execution, primary.code_unit_id);
      if (!descriptor)
        return false;
      uint32_t limbs =
          static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
      auto *previousValue = reinterpret_cast<uint64_t *>(
          reinterpret_cast<uint8_t *>(wait) + primary.previous_offset);
      auto *previousUnknown = previousValue + limbs;
      bool changed = false;
      if ((descriptor->flags & OBELISK_RT_OBSERVER_REAL32) != 0) {
        float previous = 0.0f;
        float current = 0.0f;
        std::memcpy(&previous, previousValue, sizeof(previous));
        std::memcpy(&current, value.data(), sizeof(current));
        changed = previous != current || std::isnan(current);
      } else if ((descriptor->flags & OBELISK_RT_OBSERVER_REAL64) != 0) {
        double previous = 0.0;
        double current = 0.0;
        std::memcpy(&previous, previousValue, sizeof(previous));
        std::memcpy(&current, value.data(), sizeof(current));
        changed = previous != current || std::isnan(current);
      } else {
        for (uint32_t limb = 0; limb != limbs; ++limb)
          changed |= previousValue[limb] != value[limb] ||
                     previousUnknown[limb] != unknown[limb];
      }
      uint32_t observedEdges = transitionEdges(
          (previousValue[0] & 1) != 0, (previousUnknown[0] & 1) != 0,
          (value[0] & 1) != 0, (unknown[0] & 1) != 0);
      for (uint32_t limb = 0; limb != limbs; ++limb) {
        previousValue[limb] = value[limb];
        previousUnknown[limb] = unknown[limb];
      }
      bool occurrence =
          (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT &&
           (clause.flags & OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY) != 0) ||
          (clause.edge == OBELISK_RT_WAIT_EDGE_CHANGE
               ? changed
               : signalEdgeMatches(clause.edge, observedEdges));
      if (!occurrence)
        continue;
      if (clause.condition_observer != OBELISK_RT_OBSERVER_CONDITION_NONE) {
        if (!evaluate(taskID, clause.condition_observer, value, unknown))
          return false;
        task = findTask(taskID);
        wait = task ? currentWait(*task) : nullptr;
        if (!task || task->terminated || task->signalTriggered || !wait)
          break;
        occurrence = !value.empty() && (value[0] & 1) != 0 &&
                     (unknown.empty() || (unknown[0] & 1) == 0);
      }
      if (occurrence) {
        if (ScheduledDesignTask *updated = findTask(taskID);
            updated && !updated->terminated)
          updated->signalTriggered = true;
        break;
      }
    }
  }
  return context->schedulerStatus == OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_initialize_design_state(obelisk_rt_context *context) noexcept {
  if (!context || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  if ((context->execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0)
    return OBELISK_RT_OK;
  try {
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!parseImage(entry, image) || !validateImage(image))
      return OBELISK_RT_INVALID_BYTECODE;
    if (context->stateValue.size() !=
            static_cast<size_t>((image.stateBitCount + 63) / 64) ||
        context->stateUnknown.size() != context->stateValue.size())
      return OBELISK_RT_INVALID_DESIGN;
    for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
      CaptureRecord driver = captureAt(image, index);
      if (driver.function == kNetStateDescriptor) {
        bool fourState = (driver.argument & 1) != 0;
        for (uint64_t bitIndex = 0; bitIndex != driver.planeSize; ++bitIndex) {
          setBit(context->stateValue, driver.valueOffset + bitIndex, fourState);
          setBit(context->stateUnknown, driver.valueOffset + bitIndex,
                 fourState);
        }
      } else if (driver.function == kDriverStateDescriptor) {
        for (uint64_t bitIndex = 0; bitIndex != driver.planeSize; ++bitIndex) {
          setBit(context->stateValue, driver.valueOffset + bitIndex, true);
          setBit(context->stateUnknown, driver.valueOffset + bitIndex, true);
        }
      }
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status obelisk_rt_resolve_design_drivers(obelisk_rt_context *context,
                                                    uint64_t begin,
                                                    uint64_t end) noexcept {
  if (!context || !context->execution || begin > end ||
      end > uint64_t{INT64_MAX})
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ContextTransaction transaction(context);
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!parseImage(entry, image) || !validateImage(image))
      return OBELISK_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    bool changed = false;
    if (!resolveDrivenNets(image, context, static_cast<int64_t>(begin),
                           static_cast<int64_t>(end), changed))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_HANDLE
                 : context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status
obelisk_rt_force_design_nets(obelisk_rt_context *context, uint64_t begin,
                             uint64_t width, const uint8_t *value,
                             const uint8_t *unknown) noexcept {
  if (!context || !context->execution || !value || width == 0 ||
      begin > UINT64_MAX - width)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ContextTransaction transaction(context);
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!parseImage(entry, image) || !validateImage(image))
      return OBELISK_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NetAliasCache *cache = getNetAliasCache(image, context);
    std::map<uint64_t, std::pair<bool, bool>> forcedRoots;
    for (uint64_t index = 0; index != width; ++index) {
      auto found = cache->rootByBit.find(begin + index);
      if (found == cache->rootByBit.end())
        return OBELISK_RT_INVALID_HANDLE;
      bool nextValue =
          (value[index / 8] & static_cast<uint8_t>(1u << (index % 8))) != 0;
      bool nextUnknown =
          unknown &&
          (unknown[index / 8] & static_cast<uint8_t>(1u << (index % 8))) != 0;
      forcedRoots[found->second] = {nextValue, nextUnknown};
    }
    if (context->forceMask.empty())
      context->forceMask.assign(context->stateValue.size(), 0);
    std::vector<NetPublication> publications;
    for (const auto &[root, forced] : forcedRoots) {
      auto members = cache->members.find(root);
      if (members == cache->members.end())
        return OBELISK_RT_INVALID_HANDLE;
      for (uint64_t destination : members->second) {
        bool fourState = false;
        for (const NetAliasRange &net : cache->nets)
          if (destination >= net.valueOffset &&
              destination < net.valueOffset + net.width) {
            fourState = net.fourState;
            break;
          }
        bool nextValue = forced.first;
        bool nextUnknown = fourState && forced.second;
        if (!fourState && forced.second)
          nextValue = false;
        publications.push_back(
            {destination, bit(context->stateValue, destination),
             bit(context->stateUnknown, destination), nextValue, nextUnknown});
        context->forceMask[destination / 64] |= uint64_t{1}
                                                << (destination % 64);
      }
    }
    bool changed = false;
    if (!publishNetBits(context, *cache, publications, changed))
      return context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status obelisk_rt_release_design_nets(obelisk_rt_context *context,
                                                 uint64_t begin,
                                                 uint64_t width) noexcept {
  if (!context || !context->execution || width == 0 ||
      begin > UINT64_MAX - width)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ContextTransaction transaction(context);
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!parseImage(entry, image) || !validateImage(image))
      return OBELISK_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NetAliasCache *cache = getNetAliasCache(image, context);
    std::vector<uint64_t> roots;
    for (uint64_t index = 0; index != width; ++index) {
      auto found = cache->rootByBit.find(begin + index);
      if (found == cache->rootByBit.end())
        return OBELISK_RT_INVALID_HANDLE;
      roots.push_back(found->second);
    }
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    for (uint64_t root : roots) {
      auto members = cache->members.find(root);
      if (members == cache->members.end())
        return OBELISK_RT_INVALID_HANDLE;
      for (uint64_t destination : members->second)
        if (destination / 64 < context->forceMask.size())
          context->forceMask[destination / 64] &=
              ~(uint64_t{1} << (destination % 64));
    }
    bool changed = false;
    if (!resolveNetRoots(*cache, context, std::move(roots), changed))
      return context->schedulerStatus == OBELISK_RT_OK
                 ? OBELISK_RT_INVALID_HANDLE
                 : context->schedulerStatus;
    if (changed && ++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status
obelisk_rt_design_net_is_connected(obelisk_rt_context *context, uint64_t begin,
                                   uint64_t end, bool *outConnected) noexcept {
  if (!context || !context->execution || !outConnected || begin > end)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!parseImage(entry, image) || !validateImage(image))
      return OBELISK_RT_INVALID_BYTECODE;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    NetAliasCache *cache = getNetAliasCache(image, context);
    *outConnected = false;
    for (uint64_t bitIndex = begin; bitIndex != end; ++bitIndex) {
      auto root = cache->rootByBit.find(bitIndex);
      if (root != cache->rootByBit.end()) {
        auto members = cache->members.find(root->second);
        if (members != cache->members.end() && members->second.size() > 1) {
          *outConnected = true;
          break;
        }
      }
      auto drivers = root == cache->rootByBit.end()
                         ? cache->driverBits.end()
                         : cache->driverBits.find(root->second);
      if (drivers != cache->driverBits.end() && !drivers->second.empty()) {
        *outConnected = true;
        break;
      }
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status obelisk_rt_validate_design_bytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry, uint64_t *outScratchSize,
    uint64_t *outScratchAlignment) noexcept {
  try {
    Image image;
    if (!parseImage(entry, image) || !validateImage(image))
      return OBELISK_RT_INVALID_BYTECODE;
    Function function = functionAt(image, entry.function);
    if (outScratchSize)
      *outScratchSize = function.scratchSize;
    if (outScratchAlignment)
      *outScratchAlignment = function.scratchAlignment;
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
    uint64_t instructionLimit,
    obelisk_rt_fragment_action_v1 *outAction,
    std::unique_ptr<PendingDesignActivation> *pendingActivation) noexcept {
  if (!outAction || (frameSize != 0 && !frame))
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    ManagedExecutionScope managedExecution(context);
    if (managedExecution.getStatus() != OBELISK_RT_OK)
      return managedExecution.getStatus();
    Image image;
    if (!parseImage(entry, image) || !validateImage(image))
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
    obelisk_rt_status status =
        executeFunction(image, top, context, static_cast<uint8_t *>(frame),
                        scratchOffset, *pc, budget, outAction, state,
                        pendingActivation);
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
        if (!parseImage(entry, image))
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
        if (!parseImage(entry, image))
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
        if (parseImage(entry, image)) {
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
      auto found = context->scheduledDesignTasks.end();
      bool foundUrgent = false;
      uint32_t selectedRegion = UINT32_MAX;
      uint32_t selectedRank = UINT32_MAX;
      uint64_t selectedInsertionSequence = UINT64_MAX;
      for (auto iterator = context->scheduledDesignTasks.begin();
           iterator != context->scheduledDesignTasks.end(); ++iterator) {
        if (iterator->phase != (context->schedulerRunningFinals ? 1u : 0u))
          continue;
        bool awaited = false;
        bool childrenDone = false;
        bool eventTriggered = false;
        bool signalTriggered =
            iterator->suspendKind == OBELISK_RT_SUSPEND_OBSERVER &&
            iterator->signalTriggered;
        if (iterator->started &&
            (iterator->suspendKind == OBELISK_RT_SUSPEND_CHANGE ||
             iterator->suspendKind == OBELISK_RT_SUSPEND_EDGE) &&
            iterator->waitSize >= sizeof(obelisk_rt_wait_record_v1) &&
            iterator->waitOffset <= iterator->scratchOffset &&
            iterator->waitSize <=
                iterator->scratchOffset - iterator->waitOffset) {
          const auto *wait =
              reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
                  iterator->frame.data() + iterator->waitOffset);
          const auto *entries =
              reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(wait + 1);
          if (wait->flags == OBELISK_RT_WAIT_LEVEL_TRUE ||
              wait->flags == OBELISK_RT_WAIT_EDGE_IFF)
            signalTriggered = iterator->signalTriggered;
          else if (wait->version == OBELISK_RT_VERSION)
            for (uint32_t index = 0; index != wait->count; ++index)
              for (const ScheduledSignalEvent &event :
                   context->scheduledSignalEvents)
                signalTriggered |=
                    event.sequence >= iterator->observedSignalSequence &&
                    rangesOverlap(entries[index].stable_id,
                                  entries[index].reserved, event.bitOffset,
                                  event.bitWidth) &&
                    signalEdgeMatches(entries[index].edge, event.edges);
        }
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
          found = iterator;
          foundUrgent = true;
          selectedRegion = 0;
          selectedRank = 0;
          selectedInsertionSequence = 0;
          break;
        }
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
      task = std::move(*found);
      context->scheduledDesignTasks.erase(found);
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
      if (!parseImage(entry, image) || !validateImage(image))
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
      if (!parseImage(entry, image) || !validateImage(image))
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
      task.observedSignalSequence = context->nextSchedulerSequence;
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
          if (signalWait
                  ? ((!validEdge && !iffCondition) ||
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
        else
          context->scheduledDesignTasks.push_back(std::move(task));
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
