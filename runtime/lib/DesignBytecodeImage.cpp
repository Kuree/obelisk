//===- DesignBytecodeImage.cpp - Bytecode image parsing and validation --===//

#include "DesignBytecodeImage.h"
#include "DesignBytecodeLogic.h"
#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <optional>
#include <unordered_map>
#include <vector>

namespace obelisk::designbytecode {

using BytecodeHeader = obelisk_rt_design_bytecode_header_v1;

constexpr char kMagic[] = OBELISK_RT_DESIGN_BYTECODE_MAGIC;
static_assert(sizeof(kMagic) == sizeof(BytecodeHeader::magic));
constexpr uint64_t kFunctionSize = 96;
constexpr uint64_t kLayoutSize = 40;
constexpr uint64_t kInstructionSize =
    OBELISK_RT_DESIGN_BYTECODE_INSTRUCTION_SIZE;
constexpr uint64_t kOperandSize = 8;
constexpr uint64_t kContinuationSize = 24;
constexpr uint64_t kIntrinsicSize = 16;
constexpr uint64_t kConnectivitySize = 32;

static bool rejectImage(unsigned line, const char *reason,
                        uint64_t functionIndex = UINT64_MAX,
                        uint64_t pc = UINT64_MAX,
                        uint32_t opcode = UINT32_MAX) {
#ifdef OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  if (functionIndex == UINT64_MAX) {
    std::fprintf(stderr,
                 "obelisk-bytecode-validation: rejected image: %s "
                 "(DesignBytecodeImage.cpp:%u)\n",
                 reason, line);
  } else if (pc == UINT64_MAX) {
    std::fprintf(stderr,
                 "obelisk-bytecode-validation: rejected image: %s; "
                 "function=%llu (DesignBytecodeImage.cpp:%u)\n",
                 reason, static_cast<unsigned long long>(functionIndex), line);
  } else {
    std::fprintf(stderr,
                 "obelisk-bytecode-validation: rejected image: %s; "
                 "function=%llu pc=%llu opcode=%u "
                 "(DesignBytecodeImage.cpp:%u)\n",
                 reason, static_cast<unsigned long long>(functionIndex),
                 static_cast<unsigned long long>(pc), opcode, line);
  }
#else
  (void)line;
  (void)reason;
  (void)functionIndex;
  (void)pc;
  (void)opcode;
#endif
  return false;
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
  uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
  for (uint64_t index = 0; index != size; ++index) {
    uint8_t byte = index >= 32 && index < 40 ? 0 : data[index];
    hash = obelisk_stable_hash_append_byte(hash, byte);
  }
  return hash;
}

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

bool decodeImageHeader(const obelisk_rt_design_bytecode_entry_v1 &entry,
                       Image &image) {
  const auto *execution = entry.execution;
  if (!execution || entry.reserved != 0 ||
      execution->version != OBELISK_RT_VERSION || execution->reserved != 0 ||
      (execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0 ||
      !execution->bytecode ||
      execution->bytecode_size < OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE)
    return rejectImage(__LINE__, "invalid execution bytecode descriptor");
  const uint8_t *data = execution->bytecode;
  image = {data,
           execution->bytecode_size,
           read64(data + offsetof(BytecodeHeader, function_offset)),
           read64(data + offsetof(BytecodeHeader, function_count)),
           read64(data + offsetof(BytecodeHeader, layout_offset)),
           read64(data + offsetof(BytecodeHeader, layout_count)),
           read64(data + offsetof(BytecodeHeader, code_offset)),
           read64(data + offsetof(BytecodeHeader, instruction_count)),
           read64(data + offsetof(BytecodeHeader, operand_offset)),
           read64(data + offsetof(BytecodeHeader, operand_count)),
           read64(data + offsetof(BytecodeHeader, constant_offset)),
           read64(data + offsetof(BytecodeHeader, constant_size)),
           read64(data + offsetof(BytecodeHeader, continuation_offset)),
           read64(data + offsetof(BytecodeHeader, continuation_count)),
           read64(data + offsetof(BytecodeHeader, intrinsic_offset)),
           read64(data + offsetof(BytecodeHeader, intrinsic_count)),
           read64(data + offsetof(BytecodeHeader, site_offset)),
           read64(data + offsetof(BytecodeHeader, site_count)),
           read64(data + offsetof(BytecodeHeader, state_offset)),
           read64(data + offsetof(BytecodeHeader, state_count)),
           read64(data + offsetof(BytecodeHeader, connectivity_offset)),
           read64(data + offsetof(BytecodeHeader, connectivity_count)),
           execution->state_bit_count};
  if (image.functionCount > UINT32_MAX || entry.function >= image.functionCount)
    return rejectImage(__LINE__, "bytecode function index is out of range");
  return true;
}

bool parseImage(const obelisk_rt_design_bytecode_entry_v1 &entry,
                Image &image) {
  if (!decodeImageHeader(entry, image))
    return false;
  const auto *execution = entry.execution;
  const uint8_t *data = execution->bytecode;
  if (std::memcmp(data + offsetof(BytecodeHeader, magic), kMagic,
                  sizeof(kMagic)) != 0 ||
      read32(data + offsetof(BytecodeHeader, version)) != OBELISK_RT_VERSION ||
      read32(data + offsetof(BytecodeHeader, reserved)) != 0 ||
      read32(data + offsetof(BytecodeHeader, header_size)) !=
          OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE ||
      read64(data + offsetof(BytecodeHeader, image_size)) !=
          execution->bytecode_size ||
      read64(data + offsetof(BytecodeHeader, checksum)) == 0 ||
      read64(data + offsetof(BytecodeHeader, checksum)) !=
          execution->checksum ||
      read64(data + offsetof(BytecodeHeader, checksum)) !=
          imageChecksum(data, execution->bytecode_size))
    return rejectImage(__LINE__,
                       "invalid bytecode header identity, size, or checksum");
  if (read32(data + offsetof(BytecodeHeader, flags)) != 0 ||
      read64(data + offsetof(BytecodeHeader, tail_reserved)) != 0 ||
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
    return rejectImage(
        __LINE__, "invalid bytecode header flags, ranges, or reserved data");
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
  if (!canonicalTable(image.functions, image.functionCount, kFunctionSize) ||
      !canonicalTable(image.layouts, image.layoutCount, kLayoutSize) ||
      !canonicalTable(image.code, image.instructionCount, kInstructionSize) ||
      !canonicalTable(image.operands, image.operandCount, kOperandSize) ||
      !canonicalTable(image.constants, image.constantSize, 1) ||
      !canonicalTable(image.continuations, image.continuationCount,
                      kContinuationSize) ||
      !canonicalTable(image.intrinsics, image.intrinsicCount, kIntrinsicSize) ||
      !canonicalTable(image.sites, image.siteCount, kIntrinsicSize) ||
      !canonicalTable(image.stateDescriptors, image.stateDescriptorCount, 32) ||
      !canonicalTable(image.connectivity, image.connectivityCount,
                      kConnectivitySize) ||
      cursor != image.size)
    return rejectImage(__LINE__, "noncanonical bytecode table layout");
  return true;
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

std::pair<uint32_t, uint32_t> operandAt(const Image &image, uint64_t index) {
  const uint8_t *data = image.data + image.operands + index * kOperandSize;
  return {read32(data), read32(data + 4)};
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
  case OBELISK_RT_INTRINSIC_V1_STATIC_NBA:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 0 &&
           (numeric(input(0)) || floating(input(0)) || string(input(0)) ||
            managed(input(0))) &&
           handle(input(1)) && bits(input(2), 64);
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
  case OBELISK_RT_INTRINSIC_V1_QUEUE_DELETE:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 0 && managed(input(0)) &&
           twoStateBits(input(1), 64);
  case OBELISK_RT_INTRINSIC_V1_QUEUE_INSERT:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 0 && managed(input(0)) &&
           twoStateBits(input(1), 64) && managedValue(input(2));
  case OBELISK_RT_INTRINSIC_V1_RANDOM_BOUNDED:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && twoStateBits(input(0), 64) &&
           twoStateBits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_RANDOM_DISTRIBUTION:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 1 && twoStateBits(input(0), 64) &&
           twoStateBits(input(1), 32) && twoStateBits(input(2), 32) &&
           twoStateBits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_RANDOM_NEXT:
    return signature.flags == 0 && site.inputCount == 0 &&
           site.outputCount == 1 && twoStateBits(output(0), 64);
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SEED:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 0 && twoStateBits(input(0), 64);
  case OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE:
    if (signature.flags != 0 || site.inputCount < 3 || site.outputCount != 2 ||
        !bytes(input(0)) || !twoStateBits(output(0), 64) ||
        !twoStateBits(output(1), 1))
      return false;
    for (uint32_t index = 1; index != site.inputCount; ++index)
      if (!twoStateBits(input(index), 64))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_CREATE:
    if (signature.flags != 0 || site.inputCount < 2 || site.outputCount != 1 ||
        !twoStateBits(output(0), 64))
      return false;
    for (uint32_t index = 0; index != site.inputCount; ++index)
      if (!twoStateBits(input(index), 64))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_SET_ENABLED:
    return signature.flags == 0 && site.inputCount == 2 &&
           site.outputCount == 0 && twoStateBits(input(0), 64) &&
           twoStateBits(input(1), 64);
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_SAMPLE_ENABLED:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 1 && twoStateBits(input(0), 64) &&
           twoStateBits(output(0), 1);
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_BIN_HIT:
    return signature.flags == 0 && site.inputCount == 3 &&
           site.outputCount == 0 && twoStateBits(input(0), 64) &&
           twoStateBits(input(1), 64) && twoStateBits(input(2), 64);
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_SAMPLE:
    if (signature.flags != 0 || site.inputCount < 2 || site.outputCount != 0 ||
        !twoStateBits(input(0), 64))
      return false;
    for (uint32_t index = 1; index != site.inputCount; ++index)
      if (!twoStateBits(input(index), 1))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_INSTANCE_QUERY:
    return signature.flags == 0 && site.inputCount == 1 &&
           site.outputCount == 3 && twoStateBits(input(0), 64) && output(0) &&
           output(0)->kind == OBELISK_RT_DBREG_REAL64 &&
           twoStateBits(output(1), 32) && twoStateBits(output(2), 32);
  case OBELISK_RT_INTRINSIC_V1_COVERGROUP_TYPE_QUERY:
    if (signature.flags != 0 || site.inputCount < 2 || site.outputCount != 3 ||
        !output(0) || output(0)->kind != OBELISK_RT_DBREG_REAL64 ||
        !twoStateBits(output(1), 32) || !twoStateBits(output(2), 32))
      return false;
    for (uint32_t index = 0; index != site.inputCount; ++index)
      if (!twoStateBits(input(index), 64))
        return false;
    return true;
  case OBELISK_RT_INTRINSIC_V1_ASSOC_CREATE:
    if (signature.flags != 0 || site.inputCount != 9 || site.outputCount != 1 ||
        !managed(output(0)))
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
           site.outputCount == 0 && managed(input(0)) && managedValue(input(1));
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
           site.outputCount == 1 && bits(input(0), 64) && bits(input(1), 64) &&
           bits(input(2), 64) && string(output(0));
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
           input(0)->kind == OBELISK_RT_DBREG_REAL64 && twoStateBits(output(0));
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
  case OBELISK_RT_INTRINSIC_V1_TIME_FORMAT:
    return site.inputCount == 4 && site.outputCount == 0 &&
           bits(input(0), 32) && bits(input(1), 32) && bytes(input(2)) &&
           bits(input(3), 32);
  case OBELISK_RT_INTRINSIC_V1_STRING_SCAN_FIELD:
    return signature.flags == 0 && site.inputCount == 4 &&
           site.outputCount == 3 && string(input(0)) && bits(input(1), 32) &&
           bytes(input(2)) && bits(input(3), 64) && string(output(0)) &&
           bits(output(1), 32) && bits(output(2), 32);
  case OBELISK_RT_INTRINSIC_V1_PLUSARG_TEST:
    return site.inputCount == 1 && site.outputCount == 1 && string(input(0)) &&
           bits(output(0), 32);
  case OBELISK_RT_INTRINSIC_V1_PLUSARG_VALUE:
    return site.inputCount == 1 && site.outputCount == 2 && string(input(0)) &&
           string(output(0)) && bits(output(1), 32);
  case OBELISK_RT_INTRINSIC_V1_FILE_GETLINE_STRING:
  case OBELISK_RT_INTRINSIC_V1_FILE_ERROR_STRING:
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

bool validateInitialization(const Image &image, const Function &function,
                            uint32_t functionIndex) {
  const uint64_t begin = function.firstInstruction;
  const uint64_t count = function.instructionCount;
  const uint64_t registerCount = function.layoutCount;
  const uint64_t stateWordCount =
      registerCount / 64 + (registerCount % 64 != 0);
  if (stateWordCount > std::numeric_limits<size_t>::max())
    return false;
  const size_t stateWords = static_cast<size_t>(stateWordCount);
  using State = std::vector<uint64_t>;
  auto setInitialized = [&](State &state, uint32_t reg) {
    if (reg >= registerCount)
      return false;
    state[reg / 64] |= uint64_t{1} << (reg % 64);
    return true;
  };
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
      for (size_t word = 0; word != stateWords; ++word) {
        uint64_t next = (*target)[word] & state[word];
        changed |= next != (*target)[word];
        (*target)[word] = next;
      }
    }
    if (changed)
      worklist.push_back(pc);
    return true;
  };
  State seed(stateWords);
  if ((function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0)
    for (uint32_t argument = 0; argument != function.argumentCount; ++argument)
      if (!setInitialized(seed, argument))
        return false;
  for (uint64_t index = 0; index != function.continuationCount; ++index) {
    Continuation entry =
        continuationAt(image, function.firstContinuation + index);
    State entryState = seed;
    if (index != 0 ||
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0)
      std::fill(entryState.begin(), entryState.end(), uint64_t{0});
    if (!merge(entry.instruction, entryState))
      return false;
  }
  auto defineMap = [&](State &state, uint64_t first, uint64_t mapCount) {
    for (uint64_t index = 0; index != mapCount; ++index) {
      uint32_t destination = operandAt(image, first + index).first;
      if (!setInitialized(state, destination))
        return false;
    }
    return true;
  };
  while (!worklist.empty()) {
    uint64_t pc = worklist.front();
    worklist.pop_front();
    State state = *incoming[static_cast<size_t>(pc - begin)];
    Instruction instruction = instructionAt(image, pc);
    auto defineDestination = [&] {
      return setInitialized(state, instruction.destination);
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
    case OBELISK_RT_DB_STORE_STATE:
      if (instruction.flags == OBELISK_RT_DB_STORE_STATE_CHANGED &&
          !defineDestination())
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
        if (!setInitialized(state, destination))
          return false;
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
    return reg < registerCount &&
           ((state[reg / 64] >> (reg % 64)) & uint64_t{1}) != 0;
  };
  uint32_t uninitializedRegister = kInvalidRegister;
  auto requireInitialized = [&](const State &state, uint32_t reg) {
    if (initialized(state, reg))
      return true;
    uninitializedRegister = reg;
    return false;
  };
  auto mapSourcesInitialized = [&](const State &state, uint64_t first,
                                   uint64_t mapCount) {
    for (uint64_t index = 0; index != mapCount; ++index)
      if (!requireInitialized(state, operandAt(image, first + index).second))
        return false;
    return true;
  };
  for (uint64_t offset = 0; offset != count; ++offset) {
    if (!incoming[static_cast<size_t>(offset)])
      continue;
    const State &state = *incoming[static_cast<size_t>(offset)];
    Instruction instruction = instructionAt(image, begin + offset);
    uninitializedRegister = kInvalidRegister;
    auto sources = [&](std::initializer_list<uint32_t> registers) {
      for (uint32_t reg : registers)
        if (!requireInitialized(state, reg))
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
      valid = requireInitialized(state, instruction.destination) &&
              mapSourcesInitialized(state, instruction.source0,
                                    instruction.source1);
      break;
    case OBELISK_RT_DB_CALL:
    case OBELISK_RT_DB_TASK_CALL:
      valid = mapSourcesInitialized(state, instruction.source1,
                                    instruction.source2);
      break;
    case OBELISK_RT_DB_VIRTUAL_CALL:
      valid = requireInitialized(state, instruction.source0) &&
              mapSourcesInitialized(state, instruction.source1,
                                    instruction.source2);
      break;
    case OBELISK_RT_DB_RETURN:
      valid = mapSourcesInitialized(state, instruction.source0,
                                    instruction.source1);
      break;
    case OBELISK_RT_DB_SUSPEND:
      valid = instruction.source0 == kInvalidRegister ||
              requireInitialized(state, instruction.source0);
      break;
    case OBELISK_RT_DB_INTRINSIC: {
      IntrinsicSite site =
          siteAt(image, static_cast<uint32_t>(instruction.immediate));
      for (uint32_t index = 0; index != site.inputCount; ++index)
        valid &= requireInitialized(
            state, operandAt(image, site.firstOperand + index).second);
      break;
    }
    default:
      break;
    }
    if (!valid) {
#ifdef OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
      std::fprintf(stderr,
                   "obelisk-bytecode-validation: rejected image: "
                   "instruction reads uninitialized register %u; "
                   "function=%u pc=%llu opcode=%u "
                   "(DesignBytecodeImage.cpp:%u)\n",
                   uninitializedRegister, functionIndex,
                   static_cast<unsigned long long>(begin + offset),
                   instruction.opcode, __LINE__);
#endif
      return false;
    }
  }
  return true;
}

bool validateImage(const Image &image) {
  auto reject = [](unsigned line, const char *reason,
                   uint64_t functionIndex = UINT64_MAX,
                   uint64_t pc = UINT64_MAX, uint32_t opcode = UINT32_MAX) {
    return rejectImage(line, reason, functionIndex, pc, opcode);
  };

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
      return reject(__LINE__, "invalid function flags or layout range",
                    functionIndex);
  }
  uint64_t captureIndex = 0;
  for (uint32_t functionIndex = 0; functionIndex != image.functionCount;
       ++functionIndex) {
    Function function = functionAt(image, functionIndex);
    bool process = (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0;
    if (!validProcessFunctionFlags(function) ||
        (process && function.resultCount != 0))
      return reject(__LINE__, "invalid process function flags or result count",
                    functionIndex);
    uint64_t canonicalSize =
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_MASK) >> 1;
    if (!process)
      continue;
    for (uint32_t argument = 0; argument != function.argumentCount;
         ++argument) {
      if (captureIndex >= image.stateDescriptorCount)
        return reject(__LINE__, "missing process argument capture record",
                      functionIndex);
      CaptureRecord capture = captureAt(image, captureIndex++);
      if (capture.function != functionIndex || capture.argument != argument)
        return reject(__LINE__, "misordered process argument capture record",
                      functionIndex);
      Layout layout = layoutAt(image, function, argument);
      if (capture.valueOffset == UINT64_MAX) {
        if (capture.unknownOffset != UINT64_MAX || capture.planeSize != 0 ||
            layout.kind != OBELISK_RT_DBREG_HANDLE)
          return reject(__LINE__, "invalid uncaptured handle record",
                        functionIndex);
        continue;
      }
      if (capture.planeSize == 0 || capture.valueOffset > canonicalSize ||
          capture.planeSize > canonicalSize - capture.valueOffset)
        return reject(__LINE__,
                      "process argument capture exceeds the canonical frame",
                      functionIndex);
      if (layout.kind == OBELISK_RT_DBREG_LOGIC ||
          layout.kind == OBELISK_RT_DBREG_MANAGED_REF) {
        if (capture.planeSize * 2 != layout.size ||
            capture.unknownOffset != capture.valueOffset + capture.planeSize ||
            capture.unknownOffset > canonicalSize ||
            capture.planeSize > canonicalSize - capture.unknownOffset)
          return reject(__LINE__, "invalid four-state capture plane layout",
                        functionIndex);
      } else if (capture.unknownOffset != UINT64_MAX ||
                 ((layout.kind == OBELISK_RT_DBREG_HANDLE)
                      ? capture.planeSize != 8
                      : capture.planeSize > layout.size)) {
        return reject(__LINE__, "invalid process argument capture layout",
                      functionIndex);
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
      return reject(__LINE__, "invalid or misordered net state record");
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
      return reject(__LINE__, "invalid or misordered driver state record");
    for (uint64_t previous = driverStart; previous != captureIndex;
         ++previous) {
      CaptureRecord other = captureAt(image, previous);
      if (other.unknownOffset == driver.unknownOffset &&
          ((other.argument >> 1) != (driver.argument >> 1) ||
           other.planeSize != driver.planeSize))
        return reject(__LINE__, "incompatible drivers share a target range");
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
      return reject(__LINE__, "invalid or noncanonical connectivity record");
    previousConnection = key;
    firstConnection = false;
    connectionRecords.push_back(connection);
    expandedConnections += connection.width;
    // A corrupt image must not turn validation into an unbounded expansion.
    if ((image.stateBitCount <= UINT64_MAX / 8 &&
         expandedConnections > image.stateBitCount * 8) ||
        expandedConnections > UINT32_MAX)
      return reject(__LINE__, "connectivity expansion exceeds image bounds");
    for (uint64_t bit = 0; bit != connection.width; ++bit) {
      uint64_t lhsBit = connection.lhsOffset + bit;
      uint64_t rhsBit = (connection.flags & 1) ? connection.rhsOffset - bit
                                               : connection.rhsOffset + bit;
      if (lhsBit >= rhsBit)
        return reject(
            __LINE__,
            "connectivity edge endpoints are not canonically ordered");
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
      return reject(__LINE__, "duplicate scalar connectivity edge");

  // The serialized table is the unique maximal interval encoding of its
  // canonical scalar edges. Reject alternative spellings so malformed images
  // cannot hide duplicates in overlaps, swapped endpoints, or split runs.
  auto withinNet = [](const CaptureRecord *net, uint64_t bit) {
    return net && bit >= net->valueOffset &&
           bit - net->valueOffset < net->planeSize;
  };
  std::vector<ConnectivityRecord> canonicalRecords;
  for (size_t scalar = 0; scalar != scalarConnections.size();) {
    const ScalarConnection &first = scalarConnections[scalar];
    uint64_t width = 1;
    int direction = 0;
    size_t next = scalar + 1;
    // A record must lie inside one net on each side, so a run stops at a net
    // boundary even when the neighbouring edge continues the stride: two
    // unrelated net pairs can sit adjacent in the state layout.
    const CaptureRecord *lhsNet = containingNet(first.lhs, 1, false);
    const CaptureRecord *rhsNet = containingNet(first.rhs, 1, false);
    while (next != scalarConnections.size()) {
      const ScalarConnection &candidate = scalarConnections[next];
      if (candidate.lhsResolution != first.lhsResolution ||
          candidate.rhsResolution != first.rhsResolution ||
          candidate.lhs != first.lhs + width)
        break;
      if (!withinNet(lhsNet, candidate.lhs) ||
          !withinNet(rhsNet, candidate.rhs))
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
    return reject(__LINE__, "connectivity table is not maximally encoded");
  for (size_t index = 0; index != canonicalRecords.size(); ++index) {
    const ConnectivityRecord &actual = connectionRecords[index];
    const ConnectivityRecord &expected = canonicalRecords[index];
    if (actual.lhsOffset != expected.lhsOffset ||
        actual.rhsOffset != expected.rhsOffset ||
        actual.width != expected.width ||
        actual.lhsResolution != expected.lhsResolution ||
        actual.rhsResolution != expected.rhsResolution ||
        actual.flags != expected.flags)
      return reject(__LINE__,
                    "connectivity table differs from its canonical encoding");
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
        return reject(__LINE__,
                      "uwire connectivity component has multiple drivers");
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
      return reject(__LINE__, "invalid function metadata or table range",
                    functionIndex);
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
        return reject(__LINE__, "invalid or overlapping register layout",
                      functionIndex);
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
        return reject(__LINE__, "invalid or misordered continuation record",
                      functionIndex);
      previousContinuation = entry.id;
    }
    if ((function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 &&
        continuationAt(image, function.firstContinuation).instruction !=
            function.firstInstruction)
      return reject(__LINE__,
                    "zero-time function entry continuation is invalid",
                    functionIndex);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_CONSTANT: {
        if (!reg(instruction.destination) || instruction.flags ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        Layout layout = layoutAt(image, function, instruction.destination);
        if (instruction.immediate > image.constantSize ||
            layout.size > image.constantSize - instruction.immediate)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      }
      case OBELISK_RT_DB_MOVE:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_NOT:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_REDUCE:
        if (instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !numeric(instruction.destination) ||
            !numeric(instruction.source0) ||
            instruction.flags > OBELISK_RT_DB_REDUCE_LOGICAL_VALUE ||
            layoutAt(image, function, instruction.destination).width != 1)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_EXTRACT:
        if (instruction.source2 || instruction.auxiliary ||
            (instruction.source1 != kInvalidRegister &&
             !numeric(instruction.source1)))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        if (instruction.flags == OBELISK_RT_DB_AGGREGATE_MANAGED) {
          if (!reg(instruction.destination) || !reg(instruction.source0))
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
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
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
          if (!extractHandle && !insertHandle)
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
        } else if (instruction.flags > OBELISK_RT_DB_EXTRACT_SIGN_EXTEND ||
                   !numeric(instruction.destination) ||
                   !numeric(instruction.source0)) {
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_FADD:
      case OBELISK_RT_DB_FSUB:
      case OBELISK_RT_DB_FMUL:
      case OBELISK_RT_DB_FDIV:
      case OBELISK_RT_DB_FPOW:
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !binary() ||
            !floating(instruction.destination))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_FNEG:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            !floating(instruction.destination) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_FCOMPARE: {
        if (instruction.flags > OBELISK_RT_DB_FCMP_GE || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) ||
            !numeric(instruction.destination) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_BITS ||
            layoutAt(image, function, instruction.destination).width != 1 ||
            !floating(instruction.source0) || !floating(instruction.source1) ||
            !compatible(layoutAt(image, function, instruction.source0),
                        layoutAt(image, function, instruction.source1)))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_FTRUNC:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) || !reg(instruction.source0) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_REAL32 ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_REAL64)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_SHL:
      case OBELISK_RT_DB_LSHR:
      case OBELISK_RT_DB_ASHR:
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.destination) ||
            !reg(instruction.source0) || !numeric(instruction.source1) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      }
      case OBELISK_RT_DB_SELECT:
        if (instruction.flags > OBELISK_RT_DB_SELECT_FOUR_STATE ||
            instruction.auxiliary || instruction.immediate || !binary() ||
            !numeric(instruction.source2) ||
            layoutAt(image, function, instruction.source2).width != 1 ||
            (instruction.flags == OBELISK_RT_DB_SELECT_FOUR_STATE &&
             (layoutAt(image, function, instruction.destination).kind !=
                  OBELISK_RT_DBREG_LOGIC ||
              layoutAt(image, function, instruction.source0).kind !=
                  OBELISK_RT_DBREG_LOGIC ||
              layoutAt(image, function, instruction.source1).kind !=
                  OBELISK_RT_DBREG_LOGIC ||
              layoutAt(image, function, instruction.source2).kind !=
                  OBELISK_RT_DBREG_LOGIC)))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_CONCAT: {
        if (instruction.flags || instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.destination) ||
            !reg(instruction.source0) || !reg(instruction.source1))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        Layout destination = layoutAt(image, function, instruction.destination);
        Layout left = layoutAt(image, function, instruction.source0);
        Layout right = layoutAt(image, function, instruction.source1);
        if (destination.kind != left.kind || left.kind != right.kind ||
            uint64_t{left.width} + right.width != destination.width)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      }
      case OBELISK_RT_DB_INSERT: {
        if (instruction.source2 || instruction.auxiliary ||
            !numeric(instruction.destination) ||
            !numeric(instruction.source0) ||
            !compatible(layoutAt(image, function, instruction.destination),
                        layoutAt(image, function, instruction.source0)))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        Layout destination = layoutAt(image, function, instruction.destination);
        Layout inserted = layoutAt(image, function, instruction.source1);
        if (instruction.flags == OBELISK_RT_DB_AGGREGATE_MANAGED) {
          if ((inserted.kind != OBELISK_RT_DBREG_MANAGED &&
               inserted.kind != OBELISK_RT_DBREG_STRING) ||
              inserted.width != 64 || (instruction.immediate & 63) != 0)
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
        } else if (instruction.flags != 0 || !numeric(instruction.source1)) {
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        }
        if (instruction.immediate > destination.width ||
            inserted.width > destination.width - instruction.immediate)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      }
      case OBELISK_RT_DB_LOAD_FRAME:
      case OBELISK_RT_DB_STORE_FRAME: {
        if (instruction.source1 || instruction.source2 ||
            (instruction.opcode == OBELISK_RT_DB_LOAD_FRAME
                 ? instruction.source0 != 0
                 : instruction.destination != 0))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        uint32_t valueRegister = instruction.opcode == OBELISK_RT_DB_LOAD_FRAME
                                     ? instruction.destination
                                     : instruction.source0;
        if (!reg(valueRegister))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        Layout value = layoutAt(image, function, valueRegister);
        if (value.kind == OBELISK_RT_DBREG_HANDLE) {
          if (instruction.flags < OBELISK_RT_DESCRIPTOR_STORAGE ||
              instruction.flags > OBELISK_RT_DESCRIPTOR_PROCESS ||
              (instruction.flags <= OBELISK_RT_DESCRIPTOR_DRIVER
                   ? instruction.auxiliary == 0
                   : instruction.auxiliary != 0))
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
        } else if (instruction.flags != 0 ||
                   instruction.auxiliary > value.size) {
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        } else if (value.kind == OBELISK_RT_DBREG_LOGIC &&
                   instruction.auxiliary % 2 != 0) {
          // A four-state transfer always names both canonical frame planes,
          // so its size is the value plane size doubled. An odd size cannot
          // describe one, and would truncate the unknown plane.
          return reject(__LINE__,
                        "four-state frame transfer is not a plane pair",
                        functionIndex, pc, instruction.opcode);
        }
        break;
      }
      case OBELISK_RT_DB_CLEAR_FRAME_ROOT:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_MAKE_HANDLE:
        if (instruction.flags || !reg(instruction.destination) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_HANDLE ||
            instruction.source0 < OBELISK_RT_DESCRIPTOR_STORAGE ||
            instruction.source0 > OBELISK_RT_DESCRIPTOR_EVENT ||
            instruction.source2 || instruction.auxiliary)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_MAKE_LOCAL_HANDLE:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.destination) ||
            layoutAt(image, function, instruction.destination).kind !=
                OBELISK_RT_DBREG_HANDLE ||
            (!numeric(instruction.source0) && !floating(instruction.source0)) ||
            instruction.source0 > UINT16_MAX)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_HANDLE_ID:
        if (instruction.flags || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !numeric(instruction.destination) ||
            layoutAt(image, function, instruction.destination).width != 64 ||
            !reg(instruction.source0) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_STORE_STATE:
        if (instruction.flags > OBELISK_RT_DB_STORE_STATE_CHANGED ||
            (instruction.flags == OBELISK_RT_DB_STORE_STATE_CHANGED
                 ? (!reg(instruction.destination) ||
                    layoutAt(image, function, instruction.destination).width !=
                        1)
                 : instruction.destination != 0) ||
            instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.source0) ||
            (!numeric(instruction.source1) && !floating(instruction.source1) &&
             (!reg(instruction.source1) ||
              (layoutAt(image, function, instruction.source1).kind !=
                   OBELISK_RT_DBREG_MANAGED &&
               layoutAt(image, function, instruction.source1).kind !=
                   OBELISK_RT_DBREG_STRING))) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_OVERRIDE_STATE:
        if (instruction.flags > OBELISK_RT_DB_OVERRIDE_ASSIGN ||
            instruction.destination || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.source0) || !numeric(instruction.source1) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_RELEASE_STATE:
        if (instruction.flags > OBELISK_RT_DB_OVERRIDE_ASSIGN ||
            instruction.destination || instruction.source1 ||
            instruction.source2 || instruction.auxiliary ||
            instruction.immediate || !reg(instruction.source0) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_HANDLE)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_CALL: {
        if (instruction.flags || instruction.destination ||
            instruction.source0 >= image.functionCount)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        Function callee = functionAt(image, instruction.source0);
        if ((callee.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0 ||
            instruction.source2 != callee.argumentCount ||
            instruction.immediate != callee.resultCount ||
            !validMap(image, function, callee, instruction.source1,
                      instruction.source2) ||
            !validMap(image, callee, function, instruction.auxiliary,
                      instruction.immediate))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        for (uint32_t index = 0; index != callee.argumentCount; ++index)
          if (operandAt(image, instruction.source1 + index).first != index)
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
        for (uint32_t index = 0; index != callee.resultCount; ++index)
          if (operandAt(image, instruction.auxiliary + index).second !=
              callee.argumentCount + index)
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
        break;
      }
      case OBELISK_RT_DB_TASK_CALL: {
        if (instruction.flags || instruction.destination ||
            instruction.source0 >= image.functionCount ||
            instruction.auxiliary ||
            (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
            !hasContinuation(instruction.immediate))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        Function callee = functionAt(image, instruction.source0);
        if ((callee.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
            callee.resultCount != 0 ||
            instruction.source2 != callee.argumentCount ||
            !validMap(image, function, callee, instruction.source1,
                      instruction.source2))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        for (uint32_t index = 0; index != callee.argumentCount; ++index)
          if (operandAt(image, instruction.source1 + index).first != index)
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        for (uint32_t index = 0; index != instruction.source2; ++index) {
          auto [destination, source] =
              operandAt(image, instruction.source1 + index);
          if (destination != index || !reg(source))
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
        }
        for (uint32_t index = 0; index != instruction.flags; ++index) {
          auto [destination, source] =
              operandAt(image, instruction.auxiliary + index);
          if (!reg(destination) || source != instruction.source2 + index)
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        for (uint32_t index = 0; index != function.resultCount; ++index)
          if (operandAt(image, instruction.source0 + index).first !=
              function.argumentCount + index)
            return reject(__LINE__, "invalid instruction encoding or operands",
                          functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_CONTINUE:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary ||
            (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
            !hasContinuation(instruction.immediate))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
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
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      }
      case OBELISK_RT_DB_TERMINATE:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary ||
            (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_FAIL:
        if (instruction.flags || instruction.destination ||
            instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate ||
            !reg(instruction.source0) ||
            layoutAt(image, function, instruction.source0).kind !=
                OBELISK_RT_DBREG_STATUS)
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      case OBELISK_RT_DB_INTRINSIC:
        if (instruction.flags || instruction.destination ||
            instruction.source0 || instruction.source1 || instruction.source2 ||
            instruction.auxiliary || instruction.immediate > UINT32_MAX ||
            !validIntrinsic(image, function,
                            static_cast<uint32_t>(instruction.immediate)))
          return reject(__LINE__, "invalid instruction encoding or operands",
                        functionIndex, pc, instruction.opcode);
        break;
      default:
        return reject(__LINE__, "unknown bytecode opcode", functionIndex, pc,
                      instruction.opcode);
      }
    }
    // The experimental whole-cycle native body is retained in the image only
    // so generic root initialization can spawn its native descriptor.  Its
    // large irreducible CFG is never interpreted in eval mode and exceeds the
    // current bytecode validator's forward dataflow model.
    if (function.instructionCount <= 2000 &&
        !validateInitialization(image, function, functionIndex))
      return false;
  }
  return true;
}

bool loadValidatedImage(const obelisk_rt_design_bytecode_entry_v1 &entry,
                        obelisk_rt_context *context, Image &image) {
  if (entry.reserved != 0)
    return rejectImage(__LINE__, "bytecode entry reserved field is nonzero");
  // Context creation validates the complete inventory and immutable bytecode
  // image before publishing the context. Standalone entries retain full
  // checksum and structural validation.
  if (context && context->execution == entry.execution &&
      context->designBytecodeImageValidated) {
    const Image &cached = context->designBytecodeImage;
    if (cached.data != entry.execution->bytecode ||
        cached.size != entry.execution->bytecode_size ||
        cached.stateBitCount != entry.execution->state_bit_count ||
        entry.function >= cached.functionCount)
      return rejectImage(__LINE__,
                         "cached bytecode image does not match entry");
    image = cached;
    return true;
  }
  return parseImage(entry, image) && validateImage(image);
}

} // namespace obelisk::designbytecode

using namespace obelisk::designbytecode;

static bool matchesActivationBytecodeInventory(
    const obelisk_rt_execution_descriptor_v1 &execution, const Image &image) {
  for (uint64_t index = 0; index != execution.activation_count; ++index) {
    const obelisk_rt_activation_descriptor_v1 &activation =
        execution.activations[index];
    if ((activation.flags & OBELISK_RT_ACTIVATION_HAS_BYTECODE) == 0)
      continue;
    if (activation.bytecode_function >= image.functionCount)
      return rejectImage(__LINE__,
                         "activation bytecode function is out of range");
    Function function = functionAt(image, activation.bytecode_function);
    if (function.id != activation.code_unit_id ||
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) == 0 ||
        function.resultCount != 0)
      return rejectImage(
          __LINE__, "activation descriptor does not match bytecode function");
  }
  for (uint64_t index = 0; index != execution.observer_count; ++index) {
    const obelisk_rt_observer_descriptor_v1 &observer =
        execution.observers[index];
    if (observer.bytecode_function == OBELISK_RT_OBSERVER_NO_BYTECODE)
      continue;
    if (observer.bytecode_function >= image.functionCount)
      return rejectImage(__LINE__,
                         "observer bytecode function is out of range");
    Function function = functionAt(image, observer.bytecode_function);
    if (function.id != observer.code_unit_id ||
        (function.flags & OBELISK_RT_DESIGN_FUNCTION_PROCESS) != 0 ||
        function.resultCount != 1 ||
        function.argumentCount != observer.capture_count + 1)
      return rejectImage(
          __LINE__, "observer descriptor does not match bytecode function");
    Layout result = layoutAt(image, function, function.argumentCount);
    bool fourState = (observer.flags & OBELISK_RT_OBSERVER_FOUR_STATE) != 0;
    uint32_t expectedKind = (observer.flags & OBELISK_RT_OBSERVER_REAL32) != 0
                                ? OBELISK_RT_DBREG_REAL32
                            : (observer.flags & OBELISK_RT_OBSERVER_REAL64) != 0
                                ? OBELISK_RT_DBREG_REAL64
                            : fourState ? OBELISK_RT_DBREG_LOGIC
                                        : OBELISK_RT_DBREG_BITS;
    if (layoutAt(image, function, 0).kind != OBELISK_RT_DBREG_HANDLE ||
        layoutAt(image, function, 0).size != 32 ||
        result.width != observer.result_width || result.kind != expectedKind)
      return rejectImage(__LINE__,
                         "observer layout does not match bytecode function");
    for (uint32_t capture = 0; capture != observer.capture_count; ++capture)
      if (layoutAt(image, function, capture + 1).kind !=
              OBELISK_RT_DBREG_HANDLE ||
          layoutAt(image, function, capture + 1).size != 32)
        return rejectImage(__LINE__,
                           "observer capture layout is not a 32-bit handle");
  }
  return true;
}

obelisk_rt_status obelisk_rt_initialize_design_bytecode_image(
    const obelisk_rt_execution_descriptor_v1 &execution,
    Image &outImage) noexcept {
  if ((execution.flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    obelisk_rt_design_bytecode_entry_v1 entry{&execution, 0, 0};
    Image image;
    if (!parseImage(entry, image) || !validateImage(image) ||
        !matchesActivationBytecodeInventory(execution, image))
      return OBELISK_RT_INVALID_DESIGN;
    outImage = image;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    rejectImage(__LINE__, "exception while validating bytecode image");
    return OBELISK_RT_INVALID_DESIGN;
  }
}

obelisk_rt_status obelisk_rt_validate_design_bytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    obelisk_rt_context *context, uint64_t *outScratchSize,
    uint64_t *outScratchAlignment) noexcept {
  try {
    Image image;
    if (!loadValidatedImage(entry, context, image))
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
    rejectImage(__LINE__, "exception while loading bytecode image");
    return OBELISK_RT_INVALID_BYTECODE;
  }
}
