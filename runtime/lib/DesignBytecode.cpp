//===- DesignBytecode.cpp - Design-wide validated bytecode interpreter ----===//

#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"

#include <algorithm>
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
constexpr uint64_t kInstructionSize = OBELISK_RT_DESIGN_BYTECODE_INSTRUCTION_SIZE;
constexpr uint64_t kOperandSize = 8;
constexpr uint64_t kContinuationSize = 16;
constexpr uint64_t kIntrinsicSize = 16;
constexpr uint32_t kInvalidRegister = UINT32_MAX;
constexpr uint32_t kNetStateDescriptor = UINT32_MAX - 1;
constexpr uint32_t kDriverStateDescriptor = UINT32_MAX;
constexpr uint32_t kAutomaticHandleKind = UINT32_C(1) << 30;
constexpr uint32_t kLocalHandleKind = UINT32_C(1) << 31;
constexpr int64_t kInvalidHandleStart = INT64_MIN;

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
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_AUTOMATIC,
                                         id, offset);
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
  return (count == 0 || stride <= std::numeric_limits<uint64_t>::max() / count) &&
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
  uint64_t stateBitCount = 0;
};

struct Function {
  uint64_t id = 0;
  uint64_t name = 0;
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

bool parseImage(const obelisk_rt_design_bytecode_entry_v1 &entry,
                Image &image) {
  const auto *execution = entry.execution;
  if (!execution || entry.reserved != 0 ||
      execution->version != OBELISK_RT_EXECUTION_DESCRIPTOR_VERSION ||
      execution->abi_generation != OBELISK_RT_ABI_GENERATION ||
      execution->reserved != 0 ||
      (execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0 ||
      !execution->bytecode ||
      execution->bytecode_size < OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE)
    return false;
  const uint8_t *data = execution->bytecode;
  if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0 ||
      read32(data + 8) != OBELISK_RT_DESIGN_BYTECODE_VERSION ||
      read32(data + 12) != OBELISK_RT_ABI_GENERATION ||
      read32(data + 16) != OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE ||
      read64(data + 24) != execution->bytecode_size ||
      read64(data + 32) == 0 ||
      read64(data + 32) != execution->checksum ||
      read64(data + 32) != imageChecksum(data, execution->bytecode_size))
    return false;
  image = {data,
           execution->bytecode_size,
           read64(data + 40), read64(data + 48),
           read64(data + 56), read64(data + 64),
           read64(data + 72), read64(data + 80),
           read64(data + 88), read64(data + 96),
           read64(data + 104), read64(data + 112),
           read64(data + 120), read64(data + 128),
           read64(data + 136), read64(data + 144),
           read64(data + 152), read64(data + 160),
           read64(data + 168), read64(data + 176),
           execution->state_bit_count};
  if (read32(data + 20) != 0 || read64(data + 184) != 0 ||
      image.functionCount > UINT32_MAX ||
      entry.function >= image.functionCount ||
      !validRange(image.functions, image.functionCount, kFunctionSize,
                  image.size) ||
      !validRange(image.layouts, image.layoutCount, kLayoutSize, image.size) ||
      !validRange(image.code, image.instructionCount, kInstructionSize,
                  image.size) ||
      !validRange(image.operands, image.operandCount, kOperandSize, image.size) ||
      !validRange(image.constants, image.constantSize, 1, image.size) ||
      !validRange(image.continuations, image.continuationCount,
                  kContinuationSize, image.size) ||
      !validRange(image.intrinsics, image.intrinsicCount, kIntrinsicSize,
                  image.size) ||
      !validRange(image.sites, image.siteCount, kIntrinsicSize, image.size) ||
      !validRange(image.stateDescriptors, image.stateDescriptorCount, 32,
                  image.size))
    return false;
  uint64_t cursor = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  auto canonicalTable = [&](uint64_t offset, uint64_t count,
                            uint64_t stride) {
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
         cursor == image.size;
}

Function functionAt(const Image &image, uint32_t index) {
  const uint8_t *data = image.data + image.functions + uint64_t{index} * kFunctionSize;
  return {read64(data),      read64(data + 8),  read64(data + 16),
          read64(data + 24), read64(data + 32), read64(data + 40),
          read32(data + 48), read32(data + 52), read64(data + 56),
          read64(data + 64), read64(data + 72), read64(data + 80),
          read64(data + 88)};
}

Layout layoutAt(const Image &image, const Function &function, uint32_t index) {
  const uint8_t *data = image.data + image.layouts +
                        (function.firstLayout + index) * kLayoutSize;
  return {data[0], data[1], read32(data + 4), read64(data + 8),
          read64(data + 16), read64(data + 24)};
}

Instruction instructionAt(const Image &image, uint64_t index) {
  const uint8_t *data = image.data + image.code + index * kInstructionSize;
  return {read16(data),      read16(data + 2), read32(data + 4),
          read32(data + 8), read32(data + 12), read32(data + 16),
          read32(data + 20), read64(data + 24)};
}

IntrinsicSignature intrinsicAt(const Image &image, uint32_t index) {
  const uint8_t *data = image.data + image.intrinsics + uint64_t{index} * 16;
  return {read32(data), read32(data + 4), read32(data + 8),
          read32(data + 12)};
}

IntrinsicSite siteAt(const Image &image, uint32_t index) {
  const uint8_t *data = image.data + image.sites + uint64_t{index} * 16;
  return {read32(data), read32(data + 4), read32(data + 8),
          read32(data + 12)};
}

CaptureRecord captureAt(const Image &image, uint64_t index) {
  const uint8_t *data = image.data + image.stateDescriptors + index * 32;
  return {read32(data), read32(data + 4), read64(data + 8),
          read64(data + 16), read64(data + 24)};
}

obelisk_rt_status releaseCapturedAutomaticStates(const Image &image,
                                                 uint32_t functionIndex,
                                                 obelisk_rt_context *context,
                                                 const uint8_t *canonicalFrame,
                                                 uint64_t canonicalFrameSize) {
  if (!context || !canonicalFrame)
    return OBELISK_RT_OK;
  Function function = functionAt(image, functionIndex);
  if ((function.flags & 1) == 0)
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
  std::lock_guard<std::mutex> lock(context->mutex);
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
    if (state->second.referenceCount == 0)
      state = context->nativeAutomaticStates.erase(state);
    else
      ++state;
  return OBELISK_RT_OK;
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
    if (state->second.referenceCount <= 1)
      state = context->nativeAutomaticStates.erase(state);
    else {
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
    auto [destinationRegister, sourceRegister] = operandAt(image, first + index);
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
  auto bits = [&](const std::optional<Layout> &layout, uint32_t width) {
    return numeric(layout) && layout->width == width;
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
  auto cursor = [&](const std::optional<Layout> &layout) {
    return bits(layout, 64);
  };
  switch (signature.id) {
  case OBELISK_RT_INTRINSIC_V1_SPAWN: {
    if (signature.flags >= image.functionCount || site.outputCount != 1 ||
        !handle(output(0)))
      return false;
    Function callee = functionAt(image, signature.flags);
    if ((callee.flags & 1) == 0 || site.inputCount != callee.argumentCount)
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
           site.outputCount == 0 && numeric(input(0)) && handle(input(1)) &&
           (site.inputCount == 2 || bits(input(2), 64));
  case OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER:
    return signature.flags <= 1 && site.inputCount == 1 &&
           site.outputCount == 0 && handle(input(0));
  case OBELISK_RT_INTRINSIC_V1_STATE_ALLOC:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && numeric(input(0)) && handle(output(0));
  case OBELISK_RT_INTRINSIC_V1_IMPORT:
    if (signature.flags == 0)
      return false;
    for (uint32_t index = 0; index != site.inputCount; ++index)
      if (!input(index) ||
          (input(index)->kind != OBELISK_RT_DBREG_BITS &&
           input(index)->kind != OBELISK_RT_DBREG_LOGIC &&
           input(index)->kind != OBELISK_RT_DBREG_HANDLE &&
           input(index)->kind != OBELISK_RT_DBREG_STATUS))
        return false;
    for (uint32_t index = 0; index != site.outputCount; ++index)
      if (!output(index) ||
          (output(index)->kind != OBELISK_RT_DBREG_BITS &&
           output(index)->kind != OBELISK_RT_DBREG_LOGIC &&
           output(index)->kind != OBELISK_RT_DBREG_HANDLE &&
           output(index)->kind != OBELISK_RT_DBREG_STATUS))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_DISPLAY:
    if (site.inputCount < 2 || site.outputCount != 0 || !bytes(input(0)) ||
        !bits(input(1), 32))
      return false;
    for (uint32_t index = 2; index < site.inputCount; ++index)
      if (!bytes(input(index)) && !numeric(input(index)))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN_MCD:
    return site.inputCount == 1 && site.outputCount == 1 && bytes(input(0)) &&
           bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_OPEN:
    return site.inputCount == 2 && site.outputCount == 1 && bytes(input(0)) &&
           bytes(input(1)) && bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_CLOSE:
  case OBELISK_RT_INTRINSIC_V1_FILE_FLUSH:
    return site.inputCount == 1 && site.outputCount == 0 && bits(input(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_GETC:
  case OBELISK_RT_INTRINSIC_V1_FILE_EOF:
  case OBELISK_RT_INTRINSIC_V1_FILE_REWIND:
    return site.inputCount == 1 && site.outputCount == 1 && bits(input(0), 32) &&
           bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_UNGETC:
    return site.inputCount == 2 && site.outputCount == 1 && bits(input(0), 32) &&
           bits(input(1), 32) && bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_GETLINE:
  case OBELISK_RT_INTRINSIC_V1_FILE_READ_PACKED:
    return site.inputCount == 1 && site.outputCount == 2 && bits(input(0), 32) &&
           numeric(output(0)) && bits(output(1), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_SEEK:
    return site.inputCount == 3 && site.outputCount == 1 && bits(input(0), 32) &&
           bits(input(1), 64) && bits(input(2), 32) && bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_TELL:
    return site.inputCount == 1 && site.outputCount == 1 && bits(input(0), 32) &&
           bits(output(0), 64);
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
        !bits(output(0), 32) || !bits(output(1), 32) ||
        !bits(output(2), 64) || !cursor(output(3)) ||
        !bits(output(4), 64) || !bits(output(5), 64) ||
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
  if ((function.flags & 1) == 0)
    std::fill(seed.begin(), seed.begin() + function.argumentCount, 1);
  for (uint64_t index = 0; index != function.continuationCount; ++index) {
    const uint8_t *entry = image.data + image.continuations +
                           (function.firstContinuation + index) *
                               kContinuationSize;
    State entryState = seed;
    if (index != 0 || (function.flags & 1) != 0)
      std::fill(entryState.begin(), entryState.end(), 0);
    if (!merge(read64(entry + 8), entryState))
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
    case OBELISK_RT_DB_INTRINSIC: {
      IntrinsicSite site =
          siteAt(image, static_cast<uint32_t>(instruction.immediate));
      for (uint32_t index = 0; index != site.outputCount; ++index) {
        uint32_t destination = operandAt(
            image, site.firstOperand + site.inputCount + index).first;
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
      valid = mapSourcesInitialized(state, instruction.source1,
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
    bool process = (function.flags & 1) != 0;
    if ((!process && function.flags != 0) ||
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
    bool process = (function.flags & 1) != 0;
    if ((!process && function.flags != 0) ||
        (process && function.resultCount != 0))
      return false;
    uint64_t canonicalSize = function.flags >> 1;
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
      if (layout.kind == OBELISK_RT_DBREG_LOGIC) {
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
  for (; captureIndex != image.stateDescriptorCount; ++captureIndex) {
    CaptureRecord net = captureAt(image, captureIndex);
    if (net.function != kNetStateDescriptor)
      break;
    if (net.argument > 1 || net.planeSize == 0 ||
        net.valueOffset < previousNetEnd || net.unknownOffset != UINT64_MAX ||
        net.valueOffset > image.stateBitCount ||
        net.planeSize > image.stateBitCount - net.valueOffset)
      return false;
    previousNetEnd = net.valueOffset + net.planeSize;
  }
  uint64_t driverStart = captureIndex;
  uint64_t previousDriverEnd = 0;
  for (; captureIndex != image.stateDescriptorCount; ++captureIndex) {
    CaptureRecord driver = captureAt(image, captureIndex);
    if (driver.function != kDriverStateDescriptor || driver.argument > 1 ||
        driver.planeSize == 0 || driver.valueOffset < previousDriverEnd ||
        driver.valueOffset > image.stateBitCount ||
        driver.planeSize > image.stateBitCount - driver.valueOffset ||
        driver.unknownOffset > image.stateBitCount ||
        driver.planeSize > image.stateBitCount - driver.unknownOffset)
      return false;
    for (uint64_t previous = driverStart; previous != captureIndex;
         ++previous) {
      CaptureRecord other = captureAt(image, previous);
      if (other.unknownOffset == driver.unknownOffset &&
          (other.argument != driver.argument ||
           other.planeSize != driver.planeSize))
        return false;
    }
    previousDriverEnd = driver.valueOffset + driver.planeSize;
  }
  uint64_t previousID = 0;
  for (uint32_t functionIndex = 0; functionIndex != image.functionCount;
       ++functionIndex) {
    Function function = functionAt(image, functionIndex);
    if (function.id == 0 || function.name != 0 ||
        (functionIndex != 0 && function.id <= previousID) ||
        function.firstInstruction > image.instructionCount ||
        function.instructionCount == 0 ||
        function.instructionCount > image.instructionCount - function.firstInstruction ||
        function.firstLayout > image.layoutCount ||
        function.layoutCount > image.layoutCount - function.firstLayout ||
        function.argumentCount > function.layoutCount ||
        function.resultCount > function.layoutCount - function.argumentCount ||
        function.scratchAlignment == 0 || function.scratchAlignment > 4096 ||
        (function.scratchAlignment & (function.scratchAlignment - 1)) != 0 ||
        function.scratchSize % function.scratchAlignment != 0 ||
        ((function.flags & 1) == 0 && function.flags != 0) ||
        function.continuationCount == 0 ||
        function.firstContinuation > image.continuationCount ||
        function.continuationCount > image.continuationCount - function.firstContinuation)
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
      const uint8_t *entry = image.data + image.continuations +
                             (function.firstContinuation + index) * kContinuationSize;
      uint32_t owner = read32(entry), continuation = read32(entry + 4);
      uint64_t instruction = read64(entry + 8);
      if (owner != functionIndex ||
          (index == 0 ? continuation != 0
                      : continuation <= previousContinuation) ||
          instruction < function.firstInstruction ||
          instruction >= function.firstInstruction + function.instructionCount)
        return false;
      previousContinuation = continuation;
    }
    if ((function.flags & 1) == 0 &&
        read64(image.data + image.continuations +
               function.firstContinuation * kContinuationSize + 8) !=
            function.firstInstruction)
      return false;
    uint64_t codeEnd = function.firstInstruction + function.instructionCount;
    auto hasContinuation = [&](uint64_t id) {
      if (id > UINT32_MAX)
        return false;
      for (uint64_t index = 0; index != function.continuationCount; ++index) {
        const uint8_t *entry =
            image.data + image.continuations +
            (function.firstContinuation + index) * kContinuationSize;
        if (read32(entry + 4) == id)
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
        return kind == OBELISK_RT_DBREG_BITS ||
               kind == OBELISK_RT_DBREG_LOGIC;
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
        if (instruction.flags || instruction.destination || instruction.source0 ||
            instruction.source1 || instruction.source2 || instruction.auxiliary ||
            instruction.immediate)
          return false;
        break;
      case OBELISK_RT_DB_CONSTANT: {
        if (!reg(instruction.destination) || instruction.flags ||
            instruction.source0 || instruction.source1 ||
            instruction.source2 || instruction.auxiliary)
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
            !numeric(instruction.destination) ||
            !numeric(instruction.source0) || instruction.flags > 1 ||
            (instruction.source1 != kInvalidRegister &&
             !numeric(instruction.source1)))
          return false;
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
      case OBELISK_RT_DB_SHL:
      case OBELISK_RT_DB_LSHR:
      case OBELISK_RT_DB_ASHR:
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.destination) ||
            !reg(instruction.source0) ||
            !numeric(instruction.source1) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return false;
        break;
      case OBELISK_RT_DB_COMPARE:
        if (instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.destination) ||
            !reg(instruction.source0) ||
            !reg(instruction.source1) ||
            instruction.flags > OBELISK_RT_DB_CMP_CASE_NE ||
            !compatible(layoutAt(image, function, instruction.source0),
                        layoutAt(image, function, instruction.source1)) ||
            !numeric(instruction.destination) ||
            layoutAt(image, function, instruction.destination).width != 1)
          return false;
        break;
      case OBELISK_RT_DB_SELECT:
        if (instruction.flags || instruction.auxiliary ||
            instruction.immediate || !binary() ||
            !numeric(instruction.source2) ||
            layoutAt(image, function, instruction.source2).width != 1)
          return false;
        break;
      case OBELISK_RT_DB_CONCAT: {
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.destination) ||
            !reg(instruction.source0) ||
            !reg(instruction.source1))
          return false;
        Layout destination =
            layoutAt(image, function, instruction.destination);
        Layout left = layoutAt(image, function, instruction.source0);
        Layout right = layoutAt(image, function, instruction.source1);
        if (destination.kind != left.kind || left.kind != right.kind ||
            uint64_t{left.width} + right.width != destination.width)
          return false;
        break;
      }
      case OBELISK_RT_DB_INSERT: {
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            !numeric(instruction.destination) ||
            !numeric(instruction.source0) || !numeric(instruction.source1) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return false;
        Layout destination =
            layoutAt(image, function, instruction.destination);
        Layout inserted = layoutAt(image, function, instruction.source1);
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
            !numeric(instruction.source0) || instruction.source0 > UINT16_MAX)
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
            !numeric(instruction.destination) || !reg(instruction.source0) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return false;
        break;
      case OBELISK_RT_DB_STORE_STATE:
        if (instruction.flags || instruction.destination ||
            instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.source0) ||
            !numeric(instruction.source1) ||
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
        if ((callee.flags & 1) != 0 ||
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
      case OBELISK_RT_DB_RETURN:
        if (instruction.flags || instruction.destination ||
            instruction.source2 || instruction.auxiliary ||
            instruction.immediate || (function.flags & 1) != 0 ||
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
            instruction.source0 || instruction.source1 ||
            instruction.source2 || instruction.auxiliary ||
            (function.flags & 1) == 0 ||
            !hasContinuation(instruction.immediate))
          return false;
        break;
      case OBELISK_RT_DB_SUSPEND:
        if (instruction.flags < OBELISK_RT_SUSPEND_DELAY ||
            instruction.flags > OBELISK_RT_SUSPEND_FRONTIER ||
            instruction.destination || instruction.source1 ||
            instruction.source2 || instruction.auxiliary ||
            !numeric(instruction.source0) ||
            layoutAt(image, function, instruction.source0).width != 64 ||
            (function.flags & 1) == 0 ||
            !hasContinuation(instruction.immediate))
          return false;
        break;
      case OBELISK_RT_DB_TERMINATE:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 ||
            instruction.source2 || instruction.auxiliary ||
            (function.flags & 1) == 0)
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
        if (instruction.flags || instruction.destination || instruction.source0 ||
            instruction.source1 || instruction.source2 || instruction.auxiliary ||
            instruction.immediate > UINT32_MAX ||
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
              result.value.size() * sizeof(uint64_t));
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
  std::memcpy(frame + layout.offset, plane.data(), limbs * sizeof(uint64_t));
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

Logic add(const Logic &left, const Logic &right, bool subtract) {
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
  Logic result{left.width, left.fourState,
               std::vector<uint64_t>(left.value.size()),
               std::vector<uint64_t>(left.value.size())};
  for (size_t index = 0; index != result.value.size(); ++index) {
    uint64_t lv = left.value[index], rv = right.value[index];
    uint64_t lu = left.unknown[index], ru = right.unknown[index];
    if (!left.fourState) {
      result.value[index] = opcode == OBELISK_RT_DB_AND
                                ? lv & rv
                                : opcode == OBELISK_RT_DB_OR ? lv | rv : lv ^ rv;
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
    auto [destinationRegister, sourceRegister] = operandAt(image, first + index);
    (void)destinationRegister;
    if (!validRegister(source.function, sourceRegister))
      return false;
    Layout layout = layoutAt(image, source.function, sourceRegister);
    values.emplace_back(source.data + layout.offset,
                        source.data + layout.offset + layout.size);
  }
  for (uint64_t index = 0; index != count; ++index) {
    auto [destinationRegister, sourceRegister] = operandAt(image, first + index);
    (void)sourceRegister;
    if (!validRegister(destination.function, destinationRegister))
      return false;
    Layout layout = layoutAt(image, destination.function, destinationRegister);
    if (layout.size != values[index].size())
      return false;
    std::memcpy(destination.data + layout.offset, values[index].data(), layout.size);
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

bool appendSignalEvent(obelisk_rt_context *context, uint64_t bitOffset,
                       bool oldValue, bool oldUnknown, bool newValue,
                       bool newUnknown) {
  uint32_t edges =
      transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
  if (edges == 0)
    return true;
  if (context->nextSchedulerSequence == 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  context->scheduledSignalEvents.push_back(
      {context->nextSchedulerSequence++, bitOffset, 1, edges});
  return true;
}

bool rangesOverlap(uint64_t left, uint64_t leftWidth, uint64_t right,
                   uint64_t rightWidth) {
  uint32_t leftID = 0, rightID = 0;
  int64_t leftOffset = 0, rightOffset = 0;
  bool leftAutomatic =
      decodeAutomaticHandle(left, leftID, leftOffset);
  bool rightAutomatic =
      decodeAutomaticHandle(right, rightID, rightOffset);
  if (leftAutomatic != rightAutomatic ||
      (leftAutomatic && leftID != rightID))
    return false;
  if (!leftAutomatic) {
    bool leftStatic = decodeStaticHandle(left, leftID, leftOffset);
    bool rightStatic = decodeStaticHandle(right, rightID, rightOffset);
    if (leftStatic != rightStatic || (leftStatic && leftID != rightID))
      return false;
    if (!leftStatic &&
        (!decodeGlobalHandle(left, leftOffset) ||
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

bool resolveDrivenNets(const Image &image, obelisk_rt_context *context,
                       int64_t changedBegin, int64_t changedEnd,
                       bool &changed) {
  if (!context || changedBegin < 0 || changedEnd < changedBegin)
    return false;
  std::vector<uint64_t> nets;
  for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
    CaptureRecord driver = captureAt(image, index);
    if (driver.function != kDriverStateDescriptor)
      continue;
    uint64_t driverEnd = driver.valueOffset + driver.planeSize;
    if (static_cast<uint64_t>(changedBegin) < driverEnd &&
        static_cast<uint64_t>(changedEnd) > driver.valueOffset &&
        std::find(nets.begin(), nets.end(), driver.unknownOffset) == nets.end())
      nets.push_back(driver.unknownOffset);
  }
  if (nets.empty())
    return false;
  for (uint64_t netOffset : nets) {
    const CaptureRecord *representative = nullptr;
    CaptureRecord representativeStorage;
    for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
      CaptureRecord driver = captureAt(image, index);
      if (driver.function == kDriverStateDescriptor &&
          driver.unknownOffset == netOffset) {
        representativeStorage = driver;
        representative = &representativeStorage;
        break;
      }
    }
    if (!representative)
      return false;
    bool fourState = representative->argument != 0;
    for (uint64_t bitIndex = 0; bitIndex != representative->planeSize;
         ++bitIndex) {
      bool resolvedValue = fourState;
      bool resolvedUnknown = fourState;
      for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
        CaptureRecord driver = captureAt(image, index);
        if (driver.function != kDriverStateDescriptor ||
            driver.unknownOffset != netOffset)
          continue;
        bool driverValue =
            bit(context->stateValue, driver.valueOffset + bitIndex);
        if (!fourState) {
          resolvedValue = driverValue;
          continue;
        }
        bool driverUnknown =
            bit(context->stateUnknown, driver.valueOffset + bitIndex);
        bool currentZ = resolvedUnknown && resolvedValue;
        bool driverZ = driverUnknown && driverValue;
        bool currentX = resolvedUnknown && !resolvedValue;
        bool driverX = driverUnknown && !driverValue;
        bool conflict = currentX || driverX || resolvedValue != driverValue;
        bool mergedValue = conflict ? false : resolvedValue;
        bool mergedUnknown = conflict;
        bool withoutCurrentZ = driverZ ? resolvedValue : mergedValue;
        bool withoutCurrentZUnknown =
            driverZ ? resolvedUnknown : mergedUnknown;
        resolvedValue = currentZ ? driverValue : withoutCurrentZ;
        resolvedUnknown = currentZ ? driverUnknown : withoutCurrentZUnknown;
      }
      uint64_t destination = netOffset + bitIndex;
      bool oldValue = bit(context->stateValue, destination);
      bool oldUnknown = bit(context->stateUnknown, destination);
      changed |= oldValue != resolvedValue;
      changed |= oldUnknown != resolvedUnknown;
      setBit(context->stateValue, destination, resolvedValue);
      setBit(context->stateUnknown, destination, resolvedUnknown);
      uint64_t signalHandle = destination;
      for (const auto &[id, state] : context->nativeStaticStates)
        if (state.bitOffset == netOffset &&
            state.bitWidth == representative->planeSize) {
          signalHandle = encodeStaticHandle(id, static_cast<int64_t>(bitIndex));
          break;
        }
      if (!appendSignalEvent(context, signalHandle, oldValue, oldUnknown,
                             resolvedValue, resolvedUnknown))
        return false;
    }
  }
  return true;
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
  auto writeStatus = [&](uint32_t index, obelisk_rt_status value) {
    Layout output =
        layoutAt(image, frame.function, outputRegister(index));
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
  auto cursorInput = [&](uint32_t index,
                         obelisk_rt_design_cursor_v1 &cursor) {
    auto encoded = scalar(index);
    if (!encoded)
      return false;
    cursor.offset = *encoded;
    return true;
  };
  switch (signature.id) {
  case OBELISK_RT_INTRINSIC_V1_SPAWN: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Function callee = functionAt(image, signature.flags);
    uint64_t canonicalSize = callee.flags >> 1;
    if (callee.scratchAlignment == 0 ||
        canonicalSize > UINT64_MAX - (callee.scratchAlignment - 1))
      return OBELISK_RT_INVALID_BYTECODE;
    uint64_t scratchOffset =
        (canonicalSize + callee.scratchAlignment - 1) &
        ~(callee.scratchAlignment - 1);
    if (scratchOffset > UINT64_MAX - callee.scratchSize ||
        scratchOffset + callee.scratchSize >
            std::numeric_limits<size_t>::max())
      return OBELISK_RT_OUT_OF_MEMORY;
    ScheduledDesignTask task;
    task.parent = context->activeDesignTaskID;
    task.function = signature.flags;
    task.scratchOffset = scratchOffset;
    task.scratchSize = callee.scratchSize;
    task.frame.resize(static_cast<size_t>(scratchOffset + callee.scratchSize));
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
      std::lock_guard<std::mutex> lock(context->mutex);
      if (context->nextDesignTaskID == 0 ||
          context->nextDesignTaskID > uint64_t{INT64_MAX})
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
      task.observedEpoch = context->schedulerEpoch;
      task.observedSignalSequence = context->nextSchedulerSequence;
      for (const auto &[automaticID, count] : retainedAutomaticStates)
        context->nativeAutomaticStates.find(automaticID)
            ->second.referenceCount += count;
      try {
        context->scheduledDesignTasks.push_back(std::move(task));
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
    Layout destination =
        layoutAt(image, frame.function, inputRegister(1));
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
    Logic value = readLogic(
        frame.data, layoutAt(image, frame.function, inputRegister(0)));
    uint64_t delay = 0;
    if (site.inputCount == 3) {
      auto encodedDelay = scalar(2);
      if (!encodedDelay)
        return OBELISK_RT_INVALID_BYTECODE;
      delay = *encodedDelay;
    }
    if (automatic || boundedStatic) {
      int64_t first = start < begin ? begin - start : 0;
      int64_t last = static_cast<int64_t>(value.width);
      if (start > end || end - start < last)
        last = end - start;
      if (first >= last)
        return OBELISK_RT_OK;
      int64_t selectedStart = start + first;
      uint64_t stable = automatic ? encodeAutomaticHandle(objectID,
                                                           selectedStart)
                                  : encodeStaticHandle(objectID,
                                                       selectedStart);
      if (stable == UINT64_MAX)
        return OBELISK_RT_INVALID_HANDLE;
      ScheduledNBA update;
      update.valuePlane = nullptr;
      update.unknownPlane = nullptr;
      update.planeBitCount = automatic
                                 ? static_cast<uint64_t>(end)
                                 : context->execution->state_bit_count;
      update.bitOffset = stable;
      update.bitWidth = static_cast<uint64_t>(last - first);
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
      std::lock_guard<std::mutex> lock(context->mutex);
      if (context->nextSchedulerSequence == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
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
    ScheduledDesignNBA update;
    update.handleKind = kind;
    update.begin = begin;
    update.start = start;
    update.end = end;
    update.bitWidth = value.width;
    update.value = std::move(value.value);
    update.unknown = std::move(value.unknown);
    {
      std::lock_guard<std::mutex> lock(context->mutex);
      if (context->nextSchedulerSequence == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
      update.sequence = context->nextSchedulerSequence++;
      update.dueTime =
          delay > UINT64_MAX - context->schedulerTime
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
    std::lock_guard<std::mutex> lock(context->mutex);
    if (signature.flags != 0) {
      if (context->nextSchedulerSequence == 0)
        return OBELISK_RT_OUT_OF_RESOURCES;
      context->scheduledDesignEvents.push_back(
          {context->nextSchedulerSequence++, stableID});
      return OBELISK_RT_OK;
    }
    uint64_t &generation = context->eventGenerations[stableID];
    if (++generation == 0)
      generation = 1;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_STATE_ALLOC: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    Logic initial = readLogic(
        frame.data, layoutAt(image, frame.function, inputRegister(0)));
    uint64_t stable = UINT64_MAX;
    obelisk_rt_status status = obelisk_rt_v1_native_state_alloc(
        context, initial.width,
        reinterpret_cast<const uint8_t *>(initial.value.data()),
        initial.fourState
            ? reinterpret_cast<const uint8_t *>(initial.unknown.data())
            : nullptr,
        &stable);
    if (status != OBELISK_RT_OK)
      return status;
    uint32_t id = 0;
    int64_t offset = 0;
    if (!decodeAutomaticHandle(stable, id, offset) || offset != 0)
      return OBELISK_RT_INVALID_HANDLE;
    Layout destination =
        layoutAt(image, frame.function, outputRegister(0));
    uint8_t *address = frame.data + destination.offset;
    std::memset(address, 0, destination.size);
    uint32_t kind = kAutomaticHandleKind | OBELISK_RT_DESCRIPTOR_STORAGE;
    uint64_t base = stable;
    int64_t begin = 0;
    int64_t start = 0;
    int64_t end = initial.width;
    std::memcpy(address, &kind, sizeof(kind));
    std::memcpy(address + 8, &base, sizeof(base));
    std::memcpy(address + 16, &start, sizeof(start));
    std::memcpy(address + 24, &end, sizeof(end));
    (void)begin;
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_INTRINSIC_V1_IMPORT: {
    if (!context)
      return OBELISK_RT_INVALID_ARGUMENT;
    ImportBinding binding;
    {
      std::lock_guard<std::mutex> lock(context->mutex);
      auto found = context->imports.find(signature.flags);
      if (found == context->imports.end())
        return OBELISK_RT_TIER_UNAVAILABLE;
      binding = found->second;
    }
    std::vector<obelisk_rt_import_input_v1> inputs;
    std::vector<obelisk_rt_import_output_v1> outputs;
    inputs.reserve(site.inputCount);
    outputs.reserve(site.outputCount);
    auto describe = [&](Layout layout, uint8_t *address) {
      uint64_t limbs = layout.kind == OBELISK_RT_DBREG_STATUS
                           ? 1
                           : layout.kind == OBELISK_RT_DBREG_HANDLE
                                 ? 4
                                 : limbCount(layout.width);
      uint32_t width = layout.kind == OBELISK_RT_DBREG_STATUS
                           ? 32
                           : layout.width;
      return std::tuple<uint32_t, uint64_t, uint64_t *, uint64_t *>{
          width, limbs, reinterpret_cast<uint64_t *>(address),
          layout.kind == OBELISK_RT_DBREG_LOGIC
              ? reinterpret_cast<uint64_t *>(address + limbs * 8)
              : nullptr};
    };
    for (uint32_t index = 0; index != site.inputCount; ++index) {
      Layout layout = layoutAt(image, frame.function, inputRegister(index));
      auto [width, limbs, value, unknown] =
          describe(layout, frame.data + layout.offset);
      inputs.push_back({layout.kind, layout.flags, 0, width, value, unknown,
                        limbs});
    }
    for (uint32_t index = 0; index != site.outputCount; ++index) {
      Layout layout = layoutAt(image, frame.function, outputRegister(index));
      uint8_t *address = frame.data + layout.offset;
      std::memset(address, 0, layout.size);
      auto [width, limbs, value, unknown] = describe(layout, address);
      outputs.push_back({layout.kind, layout.flags, 0, width, value, unknown,
                         limbs});
    }
    obelisk_rt_status status = binding.callback(
        context, signature.flags, inputs.data(), site.inputCount,
        outputs.data(), site.outputCount, binding.userData);
    if (status != OBELISK_RT_OK)
      return status;
    for (uint32_t index = 0; index != site.outputCount; ++index) {
      Layout layout = layoutAt(image, frame.function, outputRegister(index));
      if (layout.kind == OBELISK_RT_DBREG_STATUS) {
        uint64_t encoded = read64(frame.data + layout.offset) & UINT32_MAX;
        std::memcpy(frame.data + layout.offset, &encoded, sizeof(encoded));
      } else {
        Logic normalized = readLogic(frame.data, layout);
        writeLogic(frame.data, layout, normalized);
      }
    }
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
    std::vector<obelisk_rt_arg_v1> arguments;
    arguments.reserve(itemCount);
    for (uint32_t index = 0; index != itemCount; ++index) {
      uint32_t itemFlags = read32(flags + uint64_t{index} * 4);
      if ((itemFlags & ~uint32_t{3}) != 0)
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
        if (!value || (itemFlags & 1) != 0)
          return OBELISK_RT_INVALID_BYTECODE;
        arguments.push_back(
            {OBELISK_RT_ARG_STRING, OBELISK_RT_ARG_FORMAT_STRING, value->size,
             value->data, nullptr});
      } else {
        values.push_back(readLogic(frame.data, layout));
        Logic &value = values.back();
        arguments.push_back(
            {OBELISK_RT_ARG_LOGIC,
             static_cast<obelisk_rt_arg_flags>(
                 (itemFlags & 1) ? OBELISK_RT_ARG_SIGNED : 0),
             value.width,
             value.value.data(), value.fourState ? value.unknown.data() : nullptr});
      }
    }
    if (physical != site.inputCount)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_format_env_v1 environment{
        scope, scopeSize, library, librarySize, 0, 0, nullptr, 0, multiplier};
    return obelisk_rt_v1_display(
        context, static_cast<uint32_t>(*descriptor), newline,
        static_cast<obelisk_rt_radix>(radix), arguments.data(),
        arguments.size(), &environment);
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
    obelisk_rt_status status = obelisk_rt_v1_file_ungetc(
        context, static_cast<uint32_t>(*descriptor),
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
    bool packed = packBytes(image, frame, outputRegister(0), line.data,
                            line.size, false);
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
    obelisk_rt_status status = obelisk_rt_v1_file_read(
        context, static_cast<uint32_t>(*descriptor), data.data(), capacity,
        &count);
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
    obelisk_rt_status status = obelisk_rt_v1_file_seek(
        context, static_cast<uint32_t>(*descriptor),
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
    return sentinel(0, status == OBELISK_RT_OK
                           ? static_cast<uint64_t>(offset)
                           : UINT64_MAX);
  }
  case OBELISK_RT_INTRINSIC_V1_FILE_REWIND: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return OBELISK_RT_INVALID_BYTECODE;
    obelisk_rt_status status = obelisk_rt_v1_file_rewind(
        context, static_cast<uint32_t>(*descriptor));
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
            : obelisk_rt_v1_design_sibling(context->execution, cursor,
                                            &result);
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
    std::array<uint64_t, 7> outputs{
        info.kind, info.capabilities, info.handle.id, info.type_offset,
        static_cast<uint64_t>(info.range_left),
        static_cast<uint64_t>(info.range_right), info.bit_width};
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
    obelisk_rt_status status = obelisk_rt_v1_design_name(
        context->execution, cursor, &data, &size);
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
    std::array<uint64_t, 11> outputs{
        info.kind,
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
    Logic value = readLogic(
        frame.data, layoutAt(image, frame.function, inputRegister(1)));
    obelisk_rt_status status = obelisk_rt_v1_design_write(
        context, cursor, value.value.data(),
        value.fourState ? value.unknown.data() : nullptr, value.width);
    return finishVPI(0, status);
  }
  default:
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status executeFunction(const Image &image, Frame &frame,
                                  obelisk_rt_context *context,
                                  uint8_t *canonicalFrame,
                                  uint64_t canonicalFrameSize, uint64_t startPC,
                                  StepBudget &budget,
                                  obelisk_rt_fragment_action_v1 *action,
                                  ExecutionState &state,
                                  uint64_t returnFirst = 0,
                                  uint64_t returnCount = 0,
                                  Frame *caller = nullptr) {
  uint64_t begin = frame.function.firstInstruction;
  uint64_t end = begin + frame.function.instructionCount;
  uint64_t pc = startPC;
  while (pc >= begin && pc < end) {
    if (!budget.consume())
      return OBELISK_RT_STEP_LIMIT;
    Instruction instruction = instructionAt(image, pc++);
    auto layout = [&](uint32_t reg) { return layoutAt(image, frame.function, reg); };
    auto read = [&](uint32_t reg) { return readLogic(frame.data, layout(reg)); };
    auto write = [&](uint32_t reg, const Logic &value) {
      writeLogic(frame.data, layout(reg), value);
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
      case 0: value = !anyKnownZero && !unknown; resultUnknown = !anyKnownZero && unknown; break;
      case 1: value = anyKnownOne; resultUnknown = !anyKnownOne && unknown; break;
      case 2: value = parity && !unknown; resultUnknown = unknown; break;
      case 3: value = anyKnownZero; resultUnknown = !anyKnownZero && unknown; break;
      case 4: value = !anyKnownOne && !unknown; resultUnknown = !anyKnownOne && unknown; break;
      case 5: value = !parity && !unknown; resultUnknown = unknown; break;
      case 6: value = anyKnownOne; resultUnknown = false; break;
      case 7: value = !anyKnownOne && !unknown; resultUnknown = !anyKnownOne && unknown; break;
      case 8: value = anyKnownOne; resultUnknown = !anyKnownOne && unknown; break;
      default: return OBELISK_RT_INVALID_BYTECODE;
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
      bool caseComparison = instruction.flags == OBELISK_RT_DB_CMP_CASE_EQ ||
                            instruction.flags == OBELISK_RT_DB_CMP_CASE_NE;
      Logic result{1, layout(instruction.destination).kind == OBELISK_RT_DBREG_LOGIC,
                   {0}, {0}};
      if (!caseComparison && (anyUnknown(left) || anyUnknown(right))) {
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
        case OBELISK_RT_DB_CMP_EQ: value = compared == 0; break;
        case OBELISK_RT_DB_CMP_NE: value = compared != 0; break;
        case OBELISK_RT_DB_CMP_ULT:
        case OBELISK_RT_DB_CMP_SLT: value = compared < 0; break;
        case OBELISK_RT_DB_CMP_ULE:
        case OBELISK_RT_DB_CMP_SLE: value = compared <= 0; break;
        case OBELISK_RT_DB_CMP_UGT:
        case OBELISK_RT_DB_CMP_SGT: value = compared > 0; break;
        case OBELISK_RT_DB_CMP_UGE:
        case OBELISK_RT_DB_CMP_SGE: value = compared >= 0; break;
        case OBELISK_RT_DB_CMP_CASE_EQ:
        case OBELISK_RT_DB_CMP_CASE_NE: {
          value = left.value == right.value && left.unknown == right.unknown;
          if (instruction.flags == OBELISK_RT_DB_CMP_CASE_NE)
            value = !value;
          break;
        }
        default: return OBELISK_RT_INVALID_BYTECODE;
        }
        result.value[0] = value;
      }
      write(instruction.destination, result);
      break;
    }
    case OBELISK_RT_DB_SELECT: {
      Logic condition = read(instruction.source2);
      if (anyUnknown(condition))
        write(instruction.destination, allX(layout(instruction.destination).width,
                                            layout(instruction.destination).kind ==
                                                OBELISK_RT_DBREG_LOGIC));
      else if (!copyRegister(image, frame,
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
          write(instruction.destination, allX(destination.width, result.fourState));
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
      for (uint64_t bitIndex = 0; bitIndex != destination.width; ++bitIndex) {
        bool inRange = false;
        uint64_t source = 0;
        if (negative) {
          if (bitIndex >= negativeMagnitude) {
            source = bitIndex - negativeMagnitude;
            inRange = source < input.width;
          }
        } else if (low != UINT64_MAX &&
                   bitIndex <= UINT64_MAX - low) {
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
        std::lock_guard<std::mutex> lock(context->mutex);
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
           storage.kind != OBELISK_RT_DBREG_LOGIC) ||
          frame.id == 0 || frame.id > UINT32_C(0x7fff) ||
          instruction.source0 > UINT16_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      uint8_t *address = frame.data + destination.offset;
      std::memset(address, 0, destination.size);
      uint32_t kind = kLocalHandleKind | (frame.id << 16) |
                      instruction.source0;
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
      bool boundedStatic = !automatic &&
                           decodeStaticHandle(base, objectID, sourceBegin);
      if (automatic || boundedStatic) {
        int64_t nextStart = 0, nextEnd = 0;
        bool decoded = boundedStatic ||
                       decodeAutomaticHandle(base, objectID, sourceBegin);
        bool failed = invalid || !decoded ||
                      start == kInvalidHandleStart || end < sourceBegin ||
                      (offset > 0 && start > INT64_MAX - offset) ||
                      (offset < 0 && start < INT64_MIN - offset);
        if (!failed)
          nextStart = start + offset;
        failed |= !failed &&
                  nextStart > INT64_MAX -
                                  static_cast<int64_t>(instruction.auxiliary);
        if (!failed)
          nextEnd = nextStart + static_cast<int64_t>(instruction.auxiliary);
        if (failed) {
          start = kInvalidHandleStart;
          end = 0;
          base = decoded
                     ? (automatic ? encodeAutomaticHandle(objectID, 0)
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
        if (nextStart > INT64_MAX -
                            static_cast<int64_t>(instruction.auxiliary)) {
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
      Logic base = read(instruction.source0), inserted = read(instruction.source1);
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
      uint64_t transferSize = value.kind == OBELISK_RT_DBREG_HANDLE
                                  ? 8
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
          std::lock_guard<std::mutex> lock(context->mutex);
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
        } else if (decodeStaticHandle(stable, automaticID,
                                      automaticOffset)) {
          if (!context || kind > OBELISK_RT_DESCRIPTOR_DRIVER)
            return OBELISK_RT_INVALID_HANDLE;
          std::lock_guard<std::mutex> lock(context->mutex);
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
                    ? static_cast<int64_t>(
                          context->execution->state_bit_count)
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
    case OBELISK_RT_DB_LOAD_STATE:
    case OBELISK_RT_DB_STORE_STATE: {
      uint32_t valueRegister = instruction.opcode == OBELISK_RT_DB_LOAD_STATE
                                   ? instruction.destination
                                   : instruction.source1;
      Layout valueLayout = layout(valueRegister);
      Logic value = instruction.opcode == OBELISK_RT_DB_STORE_STATE
                        ? read(valueRegister)
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
      bool boundedStatic = !local && !automatic &&
                           decodeStaticHandle(automaticBase, staticID,
                                              staticBegin);
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
          (!local &&
           (descriptorKind < OBELISK_RT_DESCRIPTOR_STORAGE ||
            descriptorKind > OBELISK_RT_DESCRIPTOR_DRIVER)) ||
          (automatic && descriptorKind != OBELISK_RT_DESCRIPTOR_STORAGE) ||
          begin > end)
        return OBELISK_RT_INVALID_HANDLE;
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
            localLayout.kind != OBELISK_RT_DBREG_LOGIC)
          return OBELISK_RT_INVALID_HANDLE;
        localValue = readLogic(localFrame->data, localLayout);
      } else if (!context) {
        return OBELISK_RT_INVALID_ARGUMENT;
      }
      {
        std::unique_lock<std::mutex> lock;
        if (!local)
          lock = std::unique_lock<std::mutex>(context->mutex);
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
                  context->execution->state_bit_count -
                      found->second.bitOffset)
            return OBELISK_RT_INVALID_HANDLE;
          staticState = &found->second;
        }
        auto automaticBit = [](const std::vector<uint8_t> &plane,
                               uint64_t index) {
          return index / 8 < plane.size() &&
                 ((plane[index / 8] >> (index % 8)) & 1) != 0;
        };
        auto setAutomaticBit = [](std::vector<uint8_t> &plane,
                                  uint64_t index, bool enabled) {
          if (index / 8 >= plane.size())
            return;
          uint8_t mask = static_cast<uint8_t>(1u << (index % 8));
          plane[index / 8] = enabled ? plane[index / 8] | mask
                                     : plane[index / 8] &
                                           static_cast<uint8_t>(~mask);
        };
        bool changed = false;
        for (uint64_t bitIndex = 0; bitIndex != value.width; ++bitIndex) {
          bool valid = bitIndex <= uint64_t{INT64_MAX} &&
                       start <= INT64_MAX - static_cast<int64_t>(bitIndex);
          int64_t coordinate =
              valid ? start + static_cast<int64_t>(bitIndex) : -1;
          valid &= coordinate >= begin && coordinate < end && coordinate >= 0;
          uint64_t absolute = valid ? static_cast<uint64_t>(coordinate) : 0;
          uint64_t available =
              local ? localValue.width
                    : automatic ? automaticState->bitWidth
                    : boundedStatic ? staticState->bitWidth
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
          if (instruction.opcode == OBELISK_RT_DB_LOAD_STATE) {
            bool loadedValue =
                automatic
                    ? automaticBit(automaticState->value, absolute)
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
            bool oldValue =
                automatic
                    ? automaticBit(automaticState->value, absolute)
                    : bit(local ? localValue.value : context->stateValue,
                          storageBit);
            bool oldUnknown =
                automatic
                    ? automaticBit(automaticState->unknown, absolute)
                    : bit(local ? localValue.unknown : context->stateUnknown,
                          storageBit);
            bool newValue = bit(value.value, bitIndex);
            bool newUnknown = bit(value.unknown, bitIndex);
            changed |= oldValue != newValue;
            changed |= oldUnknown != newUnknown;
            if (automatic) {
              setAutomaticBit(automaticState->value, absolute, newValue);
              setAutomaticBit(automaticState->unknown, absolute, newUnknown);
            } else {
              setBit(local ? localValue.value : context->stateValue,
                     storageBit, newValue);
              setBit(local ? localValue.unknown : context->stateUnknown,
                     storageBit, newUnknown);
            }
            if (!local &&
                !appendSignalEvent(
                    context,
                    automatic
                        ? (automaticBase & ~uint64_t{UINT32_MAX}) |
                              static_cast<uint32_t>(absolute)
                    : boundedStatic ? encodeStaticHandle(staticID,
                                                         coordinate)
                                    : absolute,
                    oldValue, oldUnknown,
                                   newValue, newUnknown))
              return context->schedulerStatus;
          }
        }
        if (!local && !automatic &&
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
        if (!local && changed && ++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
      }
      if (local && instruction.opcode == OBELISK_RT_DB_STORE_STATE)
        writeLogic(localFrame->data, localLayout, localValue);
      if (instruction.opcode == OBELISK_RT_DB_LOAD_STATE)
        write(valueRegister, value);
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
      std::vector<uint8_t> storage(static_cast<size_t>(calleeFunction.scratchSize));
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
          instruction.auxiliary, instruction.immediate, &frame);
      --state.callDepth;
      state.frames.erase(callee.id);
      if (status != OBELISK_RT_OK)
        return status;
      break;
    }
    case OBELISK_RT_DB_RETURN:
      if (!caller)
        return OBELISK_RT_INVALID_BYTECODE;
      if (!copyMap(image, frame, frame, instruction.source0,
                   instruction.source1))
        return OBELISK_RT_INVALID_BYTECODE;
      if (!copyMap(image, frame, *caller, returnFirst, returnCount))
        return OBELISK_RT_INVALID_BYTECODE;
      return OBELISK_RT_OK;
    case OBELISK_RT_DB_CONTINUE:
      *action = {OBELISK_RT_FRAGMENT_CONTINUE, OBELISK_RT_SUSPEND_NONE,
                 static_cast<uint32_t>(instruction.immediate), 0, 0, 0};
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
                 static_cast<uint32_t>(instruction.immediate), 0, payload, 0};
      return OBELISK_RT_OK;
    }
    case OBELISK_RT_DB_TERMINATE:
      *action = {OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE,
                 0, 0, instruction.immediate, 0};
      return OBELISK_RT_OK;
    case OBELISK_RT_DB_FAIL: {
      Layout status = layout(instruction.source0);
      if (status.kind != OBELISK_RT_DBREG_STATUS)
        return OBELISK_RT_INVALID_BYTECODE;
      uint64_t value;
      std::memcpy(&value, frame.data + status.offset, sizeof(value));
      if (value == 0)
        break;
      if (value > OBELISK_RT_PERMISSION_DENIED)
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
    const uint8_t *entry = image.data + image.continuations +
                           (function.firstContinuation + middle) *
                               kContinuationSize;
    if (read32(entry + 4) < continuation)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == function.continuationCount)
    return std::nullopt;
  const uint8_t *entry = image.data + image.continuations +
                         (function.firstContinuation + low) * kContinuationSize;
  if (read32(entry + 4) != continuation)
    return std::nullopt;
  return read64(entry + 8);
}

} // namespace

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
      if (driver.function == kNetStateDescriptor && driver.argument != 0) {
        for (uint64_t bitIndex = 0; bitIndex != driver.planeSize; ++bitIndex)
          setBit(context->stateValue, driver.valueOffset + bitIndex, true);
      } else if (driver.function == kDriverStateDescriptor) {
        for (uint64_t bitIndex = 0; bitIndex != driver.planeSize; ++bitIndex)
          setBit(context->stateValue, driver.valueOffset + bitIndex, true);
      }
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status obelisk_rt_resolve_design_drivers(
    obelisk_rt_context *context, uint64_t begin, uint64_t end) noexcept {
  if (!context || !context->execution || begin > end || end > uint64_t{INT64_MAX})
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
    Image image;
    if (!parseImage(entry, image) || !validateImage(image))
      return OBELISK_RT_INVALID_BYTECODE;
    std::lock_guard<std::mutex> lock(context->mutex);
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

obelisk_rt_status obelisk_rt_validate_design_bytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    uint64_t *outScratchSize, uint64_t *outScratchAlignment) noexcept {
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

obelisk_rt_status obelisk_rt_execute_design_bytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    obelisk_rt_context *context, void *frame, uint64_t frameSize,
    uint64_t scratchOffset, uint64_t scratchSize, uint32_t continuation,
    uint64_t instructionLimit,
    obelisk_rt_fragment_action_v1 *outAction) noexcept {
  if (!outAction || (frameSize != 0 && !frame))
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
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
    *outAction = {OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE,
                  0, 0, 0, 0};
    obelisk_rt_status status = executeFunction(
        image, top, context, static_cast<uint8_t *>(frame), scratchOffset, *pc,
        budget, outAction, state);
    if (status != OBELISK_RT_OK ||
        outAction->kind != OBELISK_RT_FRAGMENT_TERMINATE)
      return status;
    return releaseCapturedAutomaticStates(
        image, entry.function, context, static_cast<const uint8_t *>(frame),
        scratchOffset);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

obelisk_rt_status obelisk_rt_run_one_design_task(obelisk_rt_context *context,
                                                 bool *outProgress) noexcept {
  if (!context || !outProgress)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outProgress = false;
  ScheduledDesignTask task;
  bool taskDequeued = false;
  auto abandonTask = [&](obelisk_rt_status failure) noexcept {
    if (taskDequeued && !task.frame.empty() && context->execution) {
      obelisk_rt_design_bytecode_entry_v1 entry{context->execution,
                                                task.function, 0};
      Image image;
      if (parseImage(entry, image) && task.function < image.functionCount &&
          task.scratchOffset <= task.frame.size())
        (void)releaseCapturedAutomaticStates(image, task.function, context,
                                             task.frame.data(),
                                             task.scratchOffset);
    }
    std::lock_guard<std::mutex> lock(context->mutex);
    if (taskDequeued)
      releaseDesignTaskOwnedStatesUnlocked(context, task.id);
    context->activeDesignTaskID = 0;
    context->designTaskExecuting = false;
    return failure;
  };
  try {
    {
      std::lock_guard<std::mutex> lock(context->mutex);
      if (context->designTaskExecuting)
        return OBELISK_RT_OK;
      auto found = context->scheduledDesignTasks.end();
      for (auto iterator = context->scheduledDesignTasks.begin();
           iterator != context->scheduledDesignTasks.end(); ++iterator) {
        bool awaited = false;
        bool eventTriggered = false;
        bool signalTriggered = false;
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
          if (wait->version == OBELISK_RT_WAIT_RECORD_SIGNAL_WIDTH_VERSION)
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
              for (uint32_t index = 0; index != wait->count; ++index)
                eventTriggered |=
                    context->eventGenerations[entries[index].stable_id] !=
                    iterator->waitGenerations[index];
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
        bool runnable =
            !iterator->terminated &&
            (!iterator->started || awaited || eventTriggered ||
             signalTriggered ||
             iterator->suspendKind == OBELISK_RT_SUSPEND_NONE ||
             (iterator->suspendKind == OBELISK_RT_SUSPEND_DELAY
                  ? iterator->wakeTime <= context->schedulerTime
                  : (iterator->suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_EDGE &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_EVENT &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_AWAIT &&
                     iterator->suspendKind != OBELISK_RT_SUSPEND_JOIN &&
                     iterator->observedEpoch != context->schedulerEpoch)));
        if (runnable) {
          found = iterator;
          break;
        }
      }
      if (found == context->scheduledDesignTasks.end())
        return OBELISK_RT_OK;
      task = std::move(*found);
      context->scheduledDesignTasks.erase(found);
      taskDequeued = true;
      context->designTaskExecuting = true;
      context->activeDesignTaskID = task.id;
    }
    obelisk_rt_design_bytecode_entry_v1 entry{context->execution, task.function,
                                              0};
    obelisk_rt_fragment_action_v1 action{};
    obelisk_rt_status status = obelisk_rt_execute_design_bytecode(
        entry, context, task.frame.data(), task.frame.size(),
        task.scratchOffset, task.scratchSize, task.continuation, 0, &action);
    if (status != OBELISK_RT_OK)
      return abandonTask(status);
    obelisk_rt_status finalizeStatus = OBELISK_RT_OK;
    {
      std::lock_guard<std::mutex> lock(context->mutex);
      context->activeDesignTaskID = 0;
      context->designTaskExecuting = false;
      task.started = true;
      task.continuation = action.continuation;
      task.observedEpoch = context->schedulerEpoch;
      task.observedSignalSequence = context->nextSchedulerSequence;
      switch (action.kind) {
      case OBELISK_RT_FRAGMENT_CONTINUE:
        task.suspendKind = OBELISK_RT_SUSPEND_NONE;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        break;
      case OBELISK_RT_FRAGMENT_SUSPEND: {
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
        if (wait->version != (signalWait
                                  ? OBELISK_RT_WAIT_RECORD_SIGNAL_WIDTH_VERSION
                                  : OBELISK_RT_WAIT_RECORD_VERSION) ||
            wait->kind != action.suspend_kind ||
            entries > task.scratchOffset - action.payload -
                          sizeof(obelisk_rt_wait_record_v1)) {
          finalizeStatus = OBELISK_RT_INVALID_FRAME;
          break;
        }
        const auto *waitEntries =
            reinterpret_cast<const obelisk_rt_wait_entry_v1 *>(wait + 1);
        for (uint32_t index = 0; index != wait->count; ++index) {
          bool validEdge =
              waitEntries[index].edge >= OBELISK_RT_WAIT_EDGE_CHANGE &&
              waitEntries[index].edge <= OBELISK_RT_WAIT_EDGE_BOTH;
          if (signalWait
                  ? (!validEdge || waitEntries[index].reserved == 0)
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
        if (action.suspend_kind == OBELISK_RT_SUSPEND_EVENT) {
          task.waitGenerations.reserve(wait->count);
          for (uint32_t index = 0; index != wait->count; ++index)
            task.waitGenerations.push_back(
                context->eventGenerations[waitEntries[index].stable_id]);
        }
        if (action.suspend_kind == OBELISK_RT_SUSPEND_DELAY)
          task.wakeTime = wait->payload > UINT64_MAX - context->schedulerTime
                              ? UINT64_MAX
                              : context->schedulerTime + wait->payload;
        break;
      }
      case OBELISK_RT_FRAGMENT_TERMINATE:
        releaseDesignTaskOwnedStatesUnlocked(context, task.id);
        task.terminated = true;
        task.waitOffset = 0;
        task.waitSize = 0;
        task.waitGenerations.clear();
        task.frame.clear();
        context->terminatedDesignTasks.insert(task.id);
        if (++context->schedulerEpoch == 0)
          context->schedulerEpoch = 1;
        break;
      default:
        finalizeStatus = OBELISK_RT_INVALID_BYTECODE;
        break;
      }
      if (finalizeStatus == OBELISK_RT_OK) {
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
