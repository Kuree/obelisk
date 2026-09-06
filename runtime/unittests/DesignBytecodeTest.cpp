//===- DesignBytecodeTest.cpp - Design bytecode/reflection tests ----------===//

#include "../lib/ProcessShared.h"
#include "../lib/RuntimeInternal.h"
#include "../lib/VPIHandleToken.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "obelisk/Runtime/Runtime.h"

#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <vector>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include "VPICallbackTestConfig.h"
#include <dlfcn.h>
#endif

namespace {

constexpr uint64_t kSemanticDirectorySize =
    obelisk::reflection::SemanticDirectoryLayout.size;

void put16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
  for (unsigned index = 0; index != 2; ++index)
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

void put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
  for (unsigned index = 0; index != 4; ++index)
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

void put64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value) {
  for (unsigned index = 0; index != 8; ++index)
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

uint32_t designRecordKind(uint32_t physicalKind, uint32_t vpiKind) {
  return physicalKind | (vpiKind << 16);
}

uint16_t designRelationSource(uint16_t table, uint16_t vpiKind,
                              bool iterate = false) {
  return static_cast<uint16_t>((uint32_t{table} << 14) |
                               (iterate ? uint32_t{1} << 13 : 0) | vpiKind);
}

uint64_t get64(const std::vector<uint8_t> &bytes, size_t offset) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= uint64_t{bytes[offset + index]} << (index * 8);
  return value;
}

uint64_t imageChecksum(const std::vector<uint8_t> &bytes) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t index = 0; index != bytes.size(); ++index) {
    uint8_t value = index >= 32 && index < 40 ? 0 : bytes[index];
    hash ^= value;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint64_t nameHash(std::string_view name) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (uint8_t value : name) {
    hash ^= value;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint64_t appendHash(uint64_t hash, const void *data, size_t size) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (size_t index = 0; index != size; ++index) {
    hash ^= bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint64_t frameChecksum(const obelisk_rt_frame_layout_v1 &layout) {
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = appendHash(hash, &layout.version, sizeof(layout.version));
  hash = appendHash(hash, &layout.flags, sizeof(layout.flags));
  hash = appendHash(hash, &layout.frame_size, sizeof(layout.frame_size));
  hash =
      appendHash(hash, &layout.frame_alignment, sizeof(layout.frame_alignment));
  hash = appendHash(hash, &layout.field_count, sizeof(layout.field_count));
  hash = appendHash(hash, &layout.continuation_count,
                    sizeof(layout.continuation_count));
  for (uint32_t index = 0; index != layout.field_count; ++index)
    hash =
        appendHash(hash, &layout.fields[index], sizeof(layout.fields[index]));
  for (uint32_t index = 0; index != layout.continuation_count; ++index)
    hash = appendHash(hash, &layout.continuations[index],
                      sizeof(layout.continuations[index]));
  return hash;
}

void instruction(std::vector<uint8_t> &bytes, size_t code, size_t index,
                 uint16_t opcode, uint16_t flags = 0, uint32_t destination = 0,
                 uint32_t source0 = 0, uint32_t source1 = 0,
                 uint32_t source2 = 0, uint32_t auxiliary = 0,
                 uint64_t immediate = 0) {
  size_t offset = code + index * OBELISK_RT_DESIGN_BYTECODE_INSTRUCTION_SIZE;
  put16(bytes, offset, opcode);
  put16(bytes, offset + 2, flags);
  put32(bytes, offset + 4, destination);
  put32(bytes, offset + 8, source0);
  put32(bytes, offset + 12, source1);
  put32(bytes, offset + 16, source2);
  put32(bytes, offset + 20, auxiliary);
  put64(bytes, offset + 24, immediate);
}

std::vector<uint8_t> makeBytecode() {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 40;
  constexpr size_t constantOffset = codeOffset + 3 * 32;
  constexpr size_t continuationOffset = constantOffset + 32;
  std::vector<uint8_t> bytes(continuationOffset + 24, 0);
  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, 1);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, 3);
  put64(bytes, 88, constantOffset);
  put64(bytes, 96, 0);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, 32);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, bytes.size());
  put64(bytes, 152, bytes.size());
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 16, 0);
  put64(bytes, functionOffset + 24, 3);
  put64(bytes, functionOffset + 32, 0);
  put64(bytes, functionOffset + 40, 1);
  put32(bytes, functionOffset + 48, 0);
  put32(bytes, functionOffset + 52, 0);
  put64(bytes, functionOffset + 56, 32);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 72, 0);
  put64(bytes, functionOffset + 80, 1);
  put64(bytes, functionOffset + 88, 1);

  bytes[layoutOffset] = OBELISK_RT_DBREG_LOGIC;
  put32(bytes, layoutOffset + 4, 65);
  put64(bytes, layoutOffset + 8, 0);
  put64(bytes, layoutOffset + 16, 32);

  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_CONSTANT);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_STORE_FRAME, 0, 0, 0);
  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_TERMINATE);
  put64(bytes, constantOffset, UINT64_C(0xfedcba9876543210));
  put64(bytes, constantOffset + 8, 1);
  put64(bytes, constantOffset + 16, UINT64_C(0x00000000000000f0));
  put64(bytes, constantOffset + 24, 1);
  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeDynamicScanIntrinsicBytecode(uint32_t intrinsicID) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 3 * 40;
  constexpr size_t operandOffset = codeOffset + 2 * 32;
  constexpr size_t continuationOffset = operandOffset + 14 * 8;
  constexpr size_t intrinsicOffset = continuationOffset + 24;
  constexpr size_t siteOffset = intrinsicOffset + 16;
  std::vector<uint8_t> bytes(siteOffset + 16, 0);
  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, 3);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, 2);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, 14);
  put64(bytes, 104, continuationOffset);
  put64(bytes, 112, 0);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, intrinsicOffset);
  put64(bytes, 144, 1);
  put64(bytes, 152, siteOffset);
  put64(bytes, 160, 1);
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 24, 2);
  put64(bytes, functionOffset + 40, 3);
  put32(bytes, functionOffset + 48, 3);
  put64(bytes, functionOffset + 56, 24);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 80, 1);
  put64(bytes, functionOffset + 88, 0);
  auto layout = [&](size_t index, uint8_t kind, uint32_t width,
                    uint64_t offset) {
    size_t record = layoutOffset + index * 40;
    bytes[record] = kind;
    put32(bytes, record + 4, width);
    put64(bytes, record + 8, offset);
    put64(bytes, record + 16, 8);
  };
  layout(0, OBELISK_RT_DBREG_STRING, 64, 0);
  layout(1, OBELISK_RT_DBREG_BITS, 32, 8);
  layout(2, OBELISK_RT_DBREG_BITS, 64, 16);
  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_INTRINSIC);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_RETURN);

  std::vector<uint32_t> inputs;
  std::vector<uint32_t> outputs;
  if (intrinsicID == OBELISK_RT_INTRINSIC_V1_STRING_SCAN_DYNAMIC) {
    inputs = {0, 1, 0, 1, 1, 2, 2, 2, 2};
    outputs = {0, 1, 1, 1, 1};
  } else if (intrinsicID == OBELISK_RT_INTRINSIC_V1_FILE_SCAN_DYNAMIC) {
    inputs = {1, 0, 1, 1, 2, 2, 2, 2};
    outputs = {0, 1, 1, 1, 1};
  } else {
    inputs = {0, 1, 2, 2, 2};
    outputs = {1};
  }
  for (size_t index = 0; index != inputs.size(); ++index)
    put32(bytes, operandOffset + index * 8 + 4, inputs[index]);
  for (size_t index = 0; index != outputs.size(); ++index)
    put32(bytes, operandOffset + (inputs.size() + index) * 8, outputs[index]);
  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put32(bytes, intrinsicOffset, intrinsicID);
  put32(bytes, intrinsicOffset + 4, inputs.size());
  put32(bytes, intrinsicOffset + 8, outputs.size());
  put32(bytes, siteOffset, 0);
  put32(bytes, siteOffset + 4, 0);
  put32(bytes, siteOffset + 8, inputs.size());
  put32(bytes, siteOffset + 12, outputs.size());
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeComparisonBytecode(uint8_t resultKind,
                                            uint16_t comparisonKind) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 3 * 40;
  constexpr size_t continuationOffset = codeOffset + 2 * 32;
  std::vector<uint8_t> bytes(continuationOffset + 24, 0);
  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, 3);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, 2);
  put64(bytes, 88, continuationOffset);
  put64(bytes, 104, continuationOffset);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, bytes.size());
  put64(bytes, 152, bytes.size());
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 16, 0);
  put64(bytes, functionOffset + 24, 2);
  put64(bytes, functionOffset + 32, 0);
  put64(bytes, functionOffset + 40, 3);
  put32(bytes, functionOffset + 48, 2);
  put32(bytes, functionOffset + 52, 0);
  uint64_t resultSize =
      resultKind == OBELISK_RT_DBREG_LOGIC ? uint64_t{16} : uint64_t{8};
  put64(bytes, functionOffset + 56, 32 + resultSize);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 72, 0);
  put64(bytes, functionOffset + 80, 1);

  for (unsigned index = 0; index != 2; ++index) {
    size_t layout = layoutOffset + index * 40;
    bytes[layout] = OBELISK_RT_DBREG_LOGIC;
    put32(bytes, layout + 4, 1);
    put64(bytes, layout + 8, index * 16);
    put64(bytes, layout + 16, 16);
  }
  bytes[layoutOffset + 80] = resultKind;
  put32(bytes, layoutOffset + 84, 1);
  put64(bytes, layoutOffset + 88, 32);
  put64(bytes, layoutOffset + 96, resultSize);

  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_COMPARE, comparisonKind, 2, 0,
              1);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_RETURN);
  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeInitializationBoundaryBytecode(bool invalidJoin) {
  constexpr uint64_t registerCount = 66;
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t layoutSize = registerCount * 40;
  constexpr size_t codeOffset = layoutOffset + layoutSize;
  const size_t instructionCount = invalidJoin ? 6 : 4;
  const size_t operandOffset = codeOffset + instructionCount * 32;
  const size_t operandCount = invalidJoin ? 0 : 3;
  const size_t constantOffset = operandOffset + operandCount * 8;
  const size_t continuationOffset = constantOffset + 32;
  std::vector<uint8_t> bytes(continuationOffset + 24, 0);
  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, registerCount);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, instructionCount);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, operandCount);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, 32);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, bytes.size());
  put64(bytes, 152, bytes.size());
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 16, 0);
  put64(bytes, functionOffset + 24, instructionCount);
  put64(bytes, functionOffset + 32, 0);
  put64(bytes, functionOffset + 40, registerCount);
  put32(bytes, functionOffset + 48, 1);
  put32(bytes, functionOffset + 52, invalidJoin ? 0 : 3);
  put64(bytes, functionOffset + 56, registerCount * 8);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 72, 0);
  put64(bytes, functionOffset + 80, 1);

  for (uint64_t index = 0; index != registerCount; ++index) {
    size_t layout = layoutOffset + index * 40;
    bytes[layout] = OBELISK_RT_DBREG_BITS;
    put32(bytes, layout + 4, 1);
    put64(bytes, layout + 8, index * 8);
    put64(bytes, layout + 16, 8);
  }

  if (invalidJoin) {
    instruction(bytes, codeOffset, 0, OBELISK_RT_DB_CONSTANT, 0, 63);
    instruction(bytes, codeOffset, 1, OBELISK_RT_DB_BRANCH, 0, 0, 0, 0, 0, 4);
    instruction(bytes, codeOffset, 2, OBELISK_RT_DB_CONSTANT, 0, 64);
    instruction(bytes, codeOffset, 3, OBELISK_RT_DB_JUMP, 0, 0, 0, 0, 0, 4);
    instruction(bytes, codeOffset, 4, OBELISK_RT_DB_MOVE, 0, 65, 64);
    instruction(bytes, codeOffset, 5, OBELISK_RT_DB_RETURN);
  } else {
    instruction(bytes, codeOffset, 0, OBELISK_RT_DB_CONSTANT, 0, 63);
    instruction(bytes, codeOffset, 1, OBELISK_RT_DB_CONSTANT, 0, 64);
    instruction(bytes, codeOffset, 2, OBELISK_RT_DB_CONSTANT, 0, 65);
    instruction(bytes, codeOffset, 3, OBELISK_RT_DB_RETURN, 0, 0, 0, 3);
    put32(bytes, operandOffset, 1);
    put32(bytes, operandOffset + 4, 63);
    put32(bytes, operandOffset + 8, 2);
    put32(bytes, operandOffset + 12, 64);
    put32(bytes, operandOffset + 16, 3);
    put32(bytes, operandOffset + 20, 65);
  }
  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeObserverBytecode(uint8_t resultKind) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 2 * 40;
  constexpr size_t operandOffset = codeOffset + 2 * 32;
  constexpr size_t constantOffset = operandOffset + 8;
  constexpr size_t continuationOffset = constantOffset + 32;
  std::vector<uint8_t> bytes(continuationOffset + 24, 0);
  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, 2);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, 2);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, 1);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, 32);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, bytes.size());
  put64(bytes, 152, bytes.size());
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  put64(bytes, functionOffset, 7);
  put64(bytes, functionOffset + 16, 0);
  put64(bytes, functionOffset + 24, 2);
  put64(bytes, functionOffset + 32, 0);
  put64(bytes, functionOffset + 40, 2);
  put32(bytes, functionOffset + 48, 1);
  put32(bytes, functionOffset + 52, 1);
  put64(bytes, functionOffset + 56, 64);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 72, 0);
  put64(bytes, functionOffset + 80, 1);
  put64(bytes, functionOffset + 88, 0);

  bytes[layoutOffset] = OBELISK_RT_DBREG_HANDLE;
  put32(bytes, layoutOffset + 4, 256);
  put64(bytes, layoutOffset + 8, 0);
  put64(bytes, layoutOffset + 16, 32);
  bytes[layoutOffset + 40] = resultKind;
  put32(bytes, layoutOffset + 44, 256);
  put64(bytes, layoutOffset + 48, 32);
  put64(bytes, layoutOffset + 56, 32);

  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_CONSTANT, 0, 1);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_RETURN, 0, 0, 0, 1);
  put32(bytes, operandOffset, 1);
  put32(bytes, operandOffset + 4, 1);
  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeImportBytecode(uint32_t importID) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 2 * 40;
  constexpr size_t operandOffset = codeOffset + 4 * 32;
  constexpr size_t constantOffset = operandOffset + 2 * 8;
  constexpr size_t continuationOffset = constantOffset + 32;
  constexpr size_t intrinsicOffset = continuationOffset + 24;
  constexpr size_t siteOffset = intrinsicOffset + 16;
  std::vector<uint8_t> bytes(siteOffset + 16, 0);
  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, 2);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, 4);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, 2);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, 32);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, intrinsicOffset);
  put64(bytes, 144, 1);
  put64(bytes, 152, siteOffset);
  put64(bytes, 160, 1);
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 16, 0);
  put64(bytes, functionOffset + 24, 4);
  put64(bytes, functionOffset + 32, 0);
  put64(bytes, functionOffset + 40, 2);
  put64(bytes, functionOffset + 56, 64);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 72, 0);
  put64(bytes, functionOffset + 80, 1);
  put64(bytes, functionOffset + 88, 1);

  for (size_t index = 0; index != 2; ++index) {
    size_t layout = layoutOffset + index * 40;
    bytes[layout] = OBELISK_RT_DBREG_LOGIC;
    put32(bytes, layout + 4, 65);
    put64(bytes, layout + 8, index * 32);
    put64(bytes, layout + 16, 32);
  }
  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_CONSTANT, 0, 0);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              0);
  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_STORE_FRAME, 0, 0, 1);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_TERMINATE);
  // Input register zero, then output register one.
  put32(bytes, operandOffset + 4, 0);
  put32(bytes, operandOffset + 8, 1);
  put64(bytes, constantOffset, UINT64_C(0xfedcba9876543210));
  put64(bytes, constantOffset + 8, 1);
  put64(bytes, constantOffset + 16, UINT64_C(0x30));
  put64(bytes, constantOffset + 24, 1);
  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);
  put32(bytes, intrinsicOffset, OBELISK_RT_INTRINSIC_V1_IMPORT);
  put32(bytes, intrinsicOffset + 4, 1);
  put32(bytes, intrinsicOffset + 8, 1);
  put32(bytes, intrinsicOffset + 12, importID);
  put32(bytes, siteOffset, 0);
  put32(bytes, siteOffset + 4, 0);
  put32(bytes, siteOffset + 8, 1);
  put32(bytes, siteOffset + 12, 1);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeRandomSolveProgram(bool solveBefore) {
  constexpr size_t instructionCount = 2;
  size_t metadataSize = solveBefore ? OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE +
                                          OBELISK_RT_RANDOM_SOLVE_EDGE_SIZE
                                    : 0;
  std::vector<uint8_t> bytes(
      OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE +
          instructionCount * OBELISK_RT_RANDOM_INSTRUCTION_SIZE + metadataSize,
      0);
  put32(bytes, 0, OBELISK_RT_RANDOM_PROGRAM_MAGIC);
  put16(bytes, 4, OBELISK_RT_RANDOM_PROGRAM_VERSION);
  put16(bytes, 6, OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE);
  put32(bytes, 8, 2);
  put32(bytes, 12, instructionCount);
  put32(bytes, 16, 0);
  put32(bytes, 20,
        solveBefore ? OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE : 0);

  size_t instruction = OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE;
  bytes[instruction] = OBELISK_RT_RANDOM_PUSH_LITERAL_V1;
  bytes[instruction + 1] = 1;
  put64(bytes, instruction + 8, 1);
  instruction += OBELISK_RT_RANDOM_INSTRUCTION_SIZE;
  bytes[instruction] = OBELISK_RT_RANDOM_END_HARD_V1;
  bytes[instruction + 1] = 1;
  put32(bytes, instruction + 4, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1);

  if (solveBefore) {
    size_t edge = instruction + OBELISK_RT_RANDOM_INSTRUCTION_SIZE;
    put32(bytes, edge, 1);
    edge += OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE;
    put64(bytes, edge, 1);
    put64(bytes, edge + 8, 2);
    put32(bytes, edge + 16, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1);
  }
  return bytes;
}

std::vector<uint8_t> makeRandomSolveBytecode(bool stateful, uint64_t rngState,
                                             uint64_t rngIncrement) {
  std::vector<uint8_t> program = makeRandomSolveProgram(stateful);
  uint32_t inputCount = stateful ? 7 : 5;
  uint32_t outputCount = stateful ? 3 : 2;
  uint32_t registerCount = inputCount + outputCount;
  uint32_t instructionCount = inputCount + 1 + outputCount + 1;
  uint32_t operandCount = inputCount + outputCount;

  size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  size_t layoutOffset = functionOffset + 96;
  size_t codeOffset = layoutOffset + registerCount * 40;
  size_t operandOffset = codeOffset + instructionCount * 32;
  size_t constantOffset = operandOffset + operandCount * 8;
  size_t programOffset = 16 + (inputCount - 1) * 8;
  size_t constantSize = (programOffset + program.size() + 7) & ~size_t{7};
  size_t continuationOffset = constantOffset + constantSize;
  size_t intrinsicOffset = continuationOffset + 24;
  size_t siteOffset = intrinsicOffset + 16;
  std::vector<uint8_t> bytes(siteOffset + 16, 0);

  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, registerCount);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, instructionCount);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, operandCount);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, constantSize);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, intrinsicOffset);
  put64(bytes, 144, 1);
  put64(bytes, 152, siteOffset);
  put64(bytes, 160, 1);
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 24, instructionCount);
  put64(bytes, functionOffset + 40, registerCount);
  put64(bytes, functionOffset + 56, 16 + (registerCount - 1) * 8);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 80, 1);
  put64(bytes, functionOffset + 88, 1);

  bytes[layoutOffset] = OBELISK_RT_DBREG_BYTES;
  put32(bytes, layoutOffset + 4, 128);
  put64(bytes, layoutOffset + 16, 16);
  uint32_t successRegister = inputCount + 1;
  for (uint32_t reg = 1; reg != registerCount; ++reg) {
    size_t layout = layoutOffset + reg * 40;
    bytes[layout] = OBELISK_RT_DBREG_BITS;
    put32(bytes, layout + 4, reg == successRegister ? 1 : 64);
    put64(bytes, layout + 8, 16 + (reg - 1) * 8);
    put64(bytes, layout + 16, 8);
  }

  for (uint32_t reg = 0; reg != inputCount; ++reg) {
    uint64_t constant = reg == 0 ? 0 : 16 + (reg - 1) * 8;
    instruction(bytes, codeOffset, reg, OBELISK_RT_DB_CONSTANT, 0, reg, 0, 0, 0,
                0, constant);
  }
  uint32_t pc = inputCount;
  instruction(bytes, codeOffset, pc++, OBELISK_RT_DB_INTRINSIC);
  for (uint32_t output = 0; output != outputCount; ++output)
    instruction(bytes, codeOffset, pc++, OBELISK_RT_DB_STORE_FRAME, 0, 0,
                inputCount + output, 0, 0, 0, output * 8);
  instruction(bytes, codeOffset, pc, OBELISK_RT_DB_TERMINATE);

  for (uint32_t input = 0; input != inputCount; ++input)
    put32(bytes, operandOffset + input * 8 + 4, input);
  for (uint32_t output = 0; output != outputCount; ++output)
    put32(bytes, operandOffset + (inputCount + output) * 8,
          inputCount + output);

  put64(bytes, constantOffset, programOffset);
  put64(bytes, constantOffset + 8, program.size());
  std::array<uint64_t, 6> scalarInputs{{0, 3, 0, 4, rngState, rngIncrement}};
  for (uint32_t input = 1; input != inputCount; ++input)
    put64(bytes, constantOffset + 16 + (input - 1) * 8,
          scalarInputs[input - 1]);
  std::copy(program.begin(), program.end(),
            bytes.begin() + constantOffset + programOffset);

  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);
  put32(bytes, intrinsicOffset,
        stateful ? OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE_STATE
                 : OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE);
  put32(bytes, intrinsicOffset + 4, inputCount);
  put32(bytes, intrinsicOffset + 8, outputCount);
  put32(bytes, siteOffset, 0);
  put32(bytes, siteOffset + 4, 0);
  put32(bytes, siteOffset + 8, inputCount);
  put32(bytes, siteOffset + 12, outputCount);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeRandomSolveWideBytecode() {
  constexpr uint32_t inputCount = 7;
  constexpr uint32_t outputCount = 3;
  constexpr uint32_t registerCount = inputCount + outputCount;
  constexpr uint32_t instructionCount = inputCount + 1 + outputCount + 1;
  constexpr uint32_t operandCount = inputCount + outputCount;

  constexpr uint32_t programInstructionCount = 4;
  std::vector<uint8_t> program(OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2 +
                                   programInstructionCount *
                                       OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2 +
                                   2 * sizeof(uint64_t),
                               0);
  put32(program, 0, OBELISK_RT_RANDOM_PROGRAM_MAGIC);
  put16(program, 4, OBELISK_RT_RANDOM_PROGRAM_VERSION_V2);
  put16(program, 6, OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2);
  put32(program, 8, 65);
  put32(program, 12, programInstructionCount);
  put32(program, 24, 2);
  size_t programInstruction = OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2;
  program[programInstruction] = OBELISK_RT_RANDOM_PUSH_VARIABLE_V1;
  put32(program, programInstruction + 4, 65);
  programInstruction += OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
  program[programInstruction] = OBELISK_RT_RANDOM_PUSH_LITERAL_V1;
  put32(program, programInstruction + 4, 65);
  programInstruction += OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
  program[programInstruction] = OBELISK_RT_RANDOM_EQ_V1;
  put32(program, programInstruction + 4, 1);
  programInstruction += OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
  program[programInstruction] = OBELISK_RT_RANDOM_END_HARD_V1;
  put32(program, programInstruction + 4, 1);
  put32(program, programInstruction + 8,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1);
  size_t literalOffset =
      OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2 +
      programInstructionCount * OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
  put64(program, literalOffset, 5);
  put64(program, literalOffset + 8, 1);

  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + registerCount * 40;
  constexpr size_t operandOffset = codeOffset + instructionCount * 32;
  constexpr size_t constantOffset = operandOffset + operandCount * 8;
  constexpr size_t programOffset = 80;
  size_t constantSize = (programOffset + program.size() + 7) & ~size_t{7};
  size_t continuationOffset = constantOffset + constantSize;
  size_t intrinsicOffset = continuationOffset + 24;
  size_t siteOffset = intrinsicOffset + 16;
  std::vector<uint8_t> bytes(siteOffset + 16, 0);

  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, registerCount);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, instructionCount);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, operandCount);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, constantSize);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, intrinsicOffset);
  put64(bytes, 144, 1);
  put64(bytes, 152, siteOffset);
  put64(bytes, 160, 1);
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 24, instructionCount);
  put64(bytes, functionOffset + 40, registerCount);
  put64(bytes, functionOffset + 56, 112);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 80, 1);
  put64(bytes, functionOffset + 88, 1);

  constexpr std::array<uint32_t, registerCount> widths{128, 65, 65, 64, 64,
                                                       64,  64, 65, 1,  64};
  constexpr std::array<uint64_t, registerCount> offsets{0,  16, 32, 48, 56,
                                                        64, 72, 80, 96, 104};
  constexpr std::array<uint64_t, registerCount> extents{16, 16, 16, 8, 8,
                                                        8,  8,  16, 8, 8};
  for (uint32_t reg = 0; reg != registerCount; ++reg) {
    size_t layout = layoutOffset + reg * 40;
    bytes[layout] = reg == 0 ? OBELISK_RT_DBREG_BYTES : OBELISK_RT_DBREG_BITS;
    put32(bytes, layout + 4, widths[reg]);
    put64(bytes, layout + 8, offsets[reg]);
    put64(bytes, layout + 16, extents[reg]);
  }

  constexpr std::array<uint64_t, inputCount> constants{0,  16, 32, 48,
                                                       56, 64, 72};
  for (uint32_t reg = 0; reg != inputCount; ++reg)
    instruction(bytes, codeOffset, reg, OBELISK_RT_DB_CONSTANT, 0, reg, 0, 0, 0,
                0, constants[reg]);
  uint32_t pc = inputCount;
  instruction(bytes, codeOffset, pc++, OBELISK_RT_DB_INTRINSIC);
  constexpr std::array<uint64_t, outputCount> frameOffsets{0, 16, 24};
  for (uint32_t output = 0; output != outputCount; ++output)
    instruction(bytes, codeOffset, pc++, OBELISK_RT_DB_STORE_FRAME, 0, 0,
                inputCount + output, 0, 0, 0, frameOffsets[output]);
  instruction(bytes, codeOffset, pc, OBELISK_RT_DB_TERMINATE);

  for (uint32_t input = 0; input != inputCount; ++input)
    put32(bytes, operandOffset + input * 8 + 4, input);
  for (uint32_t output = 0; output != outputCount; ++output)
    put32(bytes, operandOffset + (inputCount + output) * 8,
          inputCount + output);

  put64(bytes, constantOffset, programOffset);
  put64(bytes, constantOffset + 8, program.size());
  put64(bytes, constantOffset + 16, 0);
  put64(bytes, constantOffset + 24, 1);
  put64(bytes, constantOffset + 32, 7);
  put64(bytes, constantOffset + 40, 0);
  put64(bytes, constantOffset + 48, 0);
  put64(bytes, constantOffset + 56, 8);
  put64(bytes, constantOffset + 64, 73);
  put64(bytes, constantOffset + 72, 5);
  std::copy(program.begin(), program.end(),
            bytes.begin() + constantOffset + programOffset);

  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);
  put32(bytes, intrinsicOffset,
        OBELISK_RT_INTRINSIC_V1_RANDOM_SOLVE_WIDE_STATE);
  put32(bytes, intrinsicOffset + 4, inputCount);
  put32(bytes, intrinsicOffset + 8, outputCount);
  put32(bytes, siteOffset, 0);
  put32(bytes, siteOffset + 4, 0);
  put32(bytes, siteOffset + 8, inputCount);
  put32(bytes, siteOffset + 12, outputCount);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeRandomCycleBytecode(uint64_t key, uint64_t position,
                                             uint64_t width) {
  constexpr uint32_t inputCount = 3;
  constexpr uint32_t outputCount = 2;
  constexpr uint32_t registerCount = inputCount + outputCount;
  constexpr uint32_t instructionCount = inputCount + 1 + outputCount + 1;
  constexpr uint32_t operandCount = inputCount + outputCount;
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + registerCount * 40;
  constexpr size_t operandOffset = codeOffset + instructionCount * 32;
  constexpr size_t constantOffset = operandOffset + operandCount * 8;
  constexpr size_t constantSize = inputCount * 8;
  constexpr size_t continuationOffset = constantOffset + constantSize;
  constexpr size_t intrinsicOffset = continuationOffset + 24;
  constexpr size_t siteOffset = intrinsicOffset + 16;
  std::vector<uint8_t> bytes(siteOffset + 16, 0);

  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, registerCount);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, instructionCount);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, operandCount);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, constantSize);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, intrinsicOffset);
  put64(bytes, 144, 1);
  put64(bytes, 152, siteOffset);
  put64(bytes, 160, 1);
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 24, instructionCount);
  put64(bytes, functionOffset + 40, registerCount);
  put64(bytes, functionOffset + 56, registerCount * 8);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 80, 1);
  put64(bytes, functionOffset + 88, 1);

  for (uint32_t reg = 0; reg != registerCount; ++reg) {
    size_t layout = layoutOffset + reg * 40;
    bytes[layout] = OBELISK_RT_DBREG_BITS;
    put32(bytes, layout + 4, 64);
    put64(bytes, layout + 8, reg * 8);
    put64(bytes, layout + 16, 8);
  }

  for (uint32_t reg = 0; reg != inputCount; ++reg)
    instruction(bytes, codeOffset, reg, OBELISK_RT_DB_CONSTANT, 0, reg, 0, 0, 0,
                0, reg * 8);
  uint32_t pc = inputCount;
  instruction(bytes, codeOffset, pc++, OBELISK_RT_DB_INTRINSIC);
  for (uint32_t output = 0; output != outputCount; ++output)
    instruction(bytes, codeOffset, pc++, OBELISK_RT_DB_STORE_FRAME, 0, 0,
                inputCount + output, 0, 0, 0, output * 8);
  instruction(bytes, codeOffset, pc, OBELISK_RT_DB_TERMINATE);

  for (uint32_t input = 0; input != inputCount; ++input)
    put32(bytes, operandOffset + input * 8 + 4, input);
  for (uint32_t output = 0; output != outputCount; ++output)
    put32(bytes, operandOffset + (inputCount + output) * 8,
          inputCount + output);
  put64(bytes, constantOffset, key);
  put64(bytes, constantOffset + 8, position);
  put64(bytes, constantOffset + 16, width);

  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);
  put32(bytes, intrinsicOffset, OBELISK_RT_INTRINSIC_V1_RANDOM_CYCLE_NEXT);
  put32(bytes, intrinsicOffset + 4, inputCount);
  put32(bytes, intrinsicOffset + 8, outputCount);
  put32(bytes, siteOffset, 0);
  put32(bytes, siteOffset + 4, 0);
  put32(bytes, siteOffset + 8, inputCount);
  put32(bytes, siteOffset + 12, outputCount);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

struct ImportObservation {
  uint32_t calls = 0;
};

obelisk_rt_status importedLogic(obelisk_rt_context *, uint32_t importID,
                                const obelisk_rt_import_input_v1 *inputs,
                                uint32_t inputCount,
                                obelisk_rt_import_output_v1 *outputs,
                                uint32_t outputCount, void *userData) {
  auto *observation = static_cast<ImportObservation *>(userData);
  ++observation->calls;
  EXPECT_NE(importID, 0u);
  EXPECT_EQ(inputCount, 1u);
  EXPECT_EQ(outputCount, 1u);
  EXPECT_EQ(inputs[0].kind, OBELISK_RT_DBREG_LOGIC);
  EXPECT_EQ(inputs[0].bit_width, 65u);
  EXPECT_EQ(inputs[0].limb_count, 2u);
  EXPECT_EQ(outputs[0].kind, OBELISK_RT_DBREG_LOGIC);
  EXPECT_EQ(outputs[0].bit_width, 65u);
  outputs[0].value[0] = inputs[0].value[0] ^ UINT64_C(0xffff);
  outputs[0].value[1] = UINT64_MAX;
  outputs[0].unknown[0] = inputs[0].unknown[0];
  outputs[0].unknown[1] = UINT64_MAX;
  return OBELISK_RT_OK;
}

std::vector<uint8_t> makeSchedulerBytecode(uint64_t stateHandle = 0,
                                           uint64_t eventHandle = 7) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 4 * 40;
  constexpr size_t operandOffset = codeOffset + 7 * 32;
  constexpr size_t constantOffset = operandOffset + 4 * 8;
  constexpr size_t continuationOffset = constantOffset + 56;
  constexpr size_t intrinsicOffset = continuationOffset + 24;
  constexpr size_t siteOffset = intrinsicOffset + 2 * 16;
  constexpr size_t stateOffset = siteOffset + 2 * 16;
  std::vector<uint8_t> bytes(stateOffset, 0);
  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, 4);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, 7);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, 4);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, 56);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, intrinsicOffset);
  put64(bytes, 144, 2);
  put64(bytes, 152, siteOffset);
  put64(bytes, 160, 2);
  put64(bytes, 168, stateOffset);
  put64(bytes, 176, 0);
  put64(bytes, 184, stateOffset);

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 16, 0);
  put64(bytes, functionOffset + 24, 7);
  put64(bytes, functionOffset + 32, 0);
  put64(bytes, functionOffset + 40, 4);
  put32(bytes, functionOffset + 48, 0);
  put32(bytes, functionOffset + 52, 0);
  put64(bytes, functionOffset + 56, 88);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 72, 0);
  put64(bytes, functionOffset + 80, 1);
  put64(bytes, functionOffset + 88, 1);

  auto layout = [&](size_t index, uint8_t kind, uint32_t width, uint64_t offset,
                    uint64_t size) {
    size_t record = layoutOffset + index * 40;
    bytes[record] = kind;
    put32(bytes, record + 4, width);
    put64(bytes, record + 8, offset);
    put64(bytes, record + 16, size);
  };
  layout(0, OBELISK_RT_DBREG_LOGIC, 8, 0, 16);
  layout(1, OBELISK_RT_DBREG_HANDLE, 256, 16, 32);
  layout(2, OBELISK_RT_DBREG_BITS, 64, 48, 8);
  layout(3, OBELISK_RT_DBREG_HANDLE, 256, 56, 32);

  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_CONSTANT, 0, 0, 0, 0, 0, 0,
              0);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_MAKE_HANDLE, 0, 1,
              OBELISK_RT_DESCRIPTOR_STORAGE, 8, 0, 0, stateHandle);
  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_CONSTANT, 0, 2, 0, 0, 0, 0,
              16);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              0);
  if (eventHandle == UINT64_MAX)
    instruction(bytes, codeOffset, 4, OBELISK_RT_DB_CONSTANT, 0, 3, 0, 0, 0, 0,
                24);
  else
    instruction(bytes, codeOffset, 4, OBELISK_RT_DB_MAKE_HANDLE, 0, 3,
                OBELISK_RT_DESCRIPTOR_EVENT, 0, 0, 0, eventHandle);
  instruction(bytes, codeOffset, 5, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              1);
  instruction(bytes, codeOffset, 6, OBELISK_RT_DB_TERMINATE);

  put32(bytes, operandOffset + 4, 0);
  put32(bytes, operandOffset + 8 + 4, 1);
  put32(bytes, operandOffset + 16 + 4, 2);
  put32(bytes, operandOffset + 24 + 4, 3);
  put64(bytes, constantOffset, UINT64_C(0xa5));
  put64(bytes, constantOffset + 8, UINT64_C(0x04));
  put64(bytes, constantOffset + 16, 5);
  put32(bytes, constantOffset + 24, OBELISK_RT_DESCRIPTOR_EVENT);
  put64(bytes, constantOffset + 32, UINT64_MAX);
  put64(bytes, constantOffset + 40, UINT64_MAX);
  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);

  put32(bytes, intrinsicOffset, OBELISK_RT_INTRINSIC_V1_NBA);
  put32(bytes, intrinsicOffset + 4, 3);
  put32(bytes, intrinsicOffset + 16, OBELISK_RT_INTRINSIC_V1_EVENT_TRIGGER);
  put32(bytes, intrinsicOffset + 16 + 4, 1);
  put32(bytes, intrinsicOffset + 16 + 12, 1);
  put32(bytes, siteOffset, 0);
  put32(bytes, siteOffset + 4, 0);
  put32(bytes, siteOffset + 8, 3);
  put32(bytes, siteOffset + 16, 1);
  put32(bytes, siteOffset + 16 + 4, 3);
  put32(bytes, siteOffset + 16 + 8, 1);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeDriverBytecode() {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 4 * 40;
  std::vector<uint8_t> bytes = makeSchedulerBytecode();
  size_t stateOffset = bytes.size();
  bytes.resize(stateOffset + 2 * 32, 0);
  // Reinterpret the first handle as a 65-bit driver at state offset 65 and
  // replace the queued NBA with an immediate driver update.
  put32(bytes, codeOffset + 1 * 32 + 8, OBELISK_RT_DESCRIPTOR_DRIVER);
  put32(bytes, codeOffset + 1 * 32 + 12, 65);
  put64(bytes, codeOffset + 1 * 32 + 24, 65);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_STORE_STATE, 0, 0, 1, 0);

  // Net [0,65), then its four-state driver [65,130).
  put32(bytes, stateOffset, UINT32_MAX - 1);
  put32(bytes, stateOffset + 4, 1);
  put64(bytes, stateOffset + 8, 0);
  put64(bytes, stateOffset + 16, UINT64_MAX);
  put64(bytes, stateOffset + 24, 65);
  put32(bytes, stateOffset + 32, UINT32_MAX);
  put32(bytes, stateOffset + 32 + 4, 1);
  put64(bytes, stateOffset + 32 + 8, 65);
  put64(bytes, stateOffset + 32 + 16, 0);
  put64(bytes, stateOffset + 32 + 24, 65);
  put64(bytes, 24, bytes.size());
  put64(bytes, 176, 2);
  put64(bytes, 184, bytes.size());
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

uint32_t driverFlags(uint8_t strength0, uint8_t strength1) {
  return 1u | ((static_cast<uint32_t>(strength0) + 1) << 3) |
         ((static_cast<uint32_t>(strength1) + 1) << 7);
}

uint32_t resolutionFlags(uint8_t resolution, bool driver) {
  return ((resolution & 3u) << 1) | ((resolution & 12u) << (driver ? 10 : 3));
}

std::vector<uint8_t> makeStrengthDriverBytecode(uint8_t resolution = 0) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 4 * 40;
  std::vector<uint8_t> bytes = makeDriverBytecode();
  size_t stateOffset = get64(bytes, 168);
  size_t constantOffset = get64(bytes, 104);
  size_t secondDriver = bytes.size();
  size_t thirdDriver = secondDriver + 32;
  bytes.resize(thirdDriver + 32, 0);

  // Drivers zero and one are the polarity banks of a conditional gate. Their
  // x values encode strong L and strong H respectively. Driver two is an
  // ordinary strong driver used to test how those ranges combine.
  put32(bytes, stateOffset + 4, 1u | resolutionFlags(resolution, false));
  put32(bytes, stateOffset + 32 + 4,
        driverFlags(6, 0) | resolutionFlags(resolution, true));
  put32(bytes, secondDriver, UINT32_MAX);
  put32(bytes, secondDriver + 4,
        driverFlags(0, 6) | (uint32_t{1} << 11) |
            resolutionFlags(resolution, true));
  put64(bytes, secondDriver + 8, 130);
  put64(bytes, secondDriver + 16, 0);
  put64(bytes, secondDriver + 24, 65);
  put32(bytes, thirdDriver, UINT32_MAX);
  put32(bytes, thirdDriver + 4,
        driverFlags(6, 6) | resolutionFlags(resolution, true));
  put64(bytes, thirdDriver + 8, 195);
  put64(bytes, thirdDriver + 16, 0);
  put64(bytes, thirdDriver + 24, 65);

  // Exercise an atomic transition from a preloaded 0 to 1: release the low
  // bank without resolving, then drive the high bank and resolve once. The
  // branch also proves that a deferred changed-store reports no logical net
  // transition. Both handles are scalar views even though the descriptors are
  // wide.
  put32(bytes, layoutOffset + 2 * 40 + 4, 1);
  put32(bytes, codeOffset + 1 * 32 + 12, 1);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_STORE_STATE,
              OBELISK_RT_DB_STORE_STATE_CHANGED |
                  OBELISK_RT_DB_STORE_STATE_DEFER_NET_RESOLUTION,
              2, 1, 0);
  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_MAKE_HANDLE, 0, 3,
              OBELISK_RT_DESCRIPTOR_DRIVER, 1, 0, 0, 130);
  instruction(bytes, codeOffset, 4, OBELISK_RT_DB_BRANCH, 0, 2, 0, 0, 0, 0, 6);
  instruction(bytes, codeOffset, 5, OBELISK_RT_DB_STORE_STATE, 0, 0, 3, 0);
  instruction(bytes, codeOffset, 6, OBELISK_RT_DB_TERMINATE);
  put64(bytes, constantOffset, 1);
  put64(bytes, constantOffset + 8, 0);
  put64(bytes, 24, bytes.size());
  put64(bytes, 176, 4);
  put64(bytes, 184, bytes.size());
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeConnectedDriverBytecode() {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 4 * 40;
  std::vector<uint8_t> bytes = makeSchedulerBytecode();
  size_t stateOffset = bytes.size();
  size_t connectivityOffset = stateOffset + 3 * 32;
  bytes.resize(connectivityOffset + 32, 0);

  // The process drives net one. Net zero is a distinct logical descriptor in
  // the same scalar connectivity components.
  put32(bytes, codeOffset + 1 * 32 + 8, OBELISK_RT_DESCRIPTOR_DRIVER);
  put32(bytes, codeOffset + 1 * 32 + 12, 130);
  put64(bytes, codeOffset + 1 * 32 + 24, 130);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_STORE_STATE, 0, 0, 1, 0);

  auto state = [&](size_t index, uint32_t kind, uint32_t flags,
                   uint64_t valueOffset, uint64_t targetOffset) {
    size_t record = stateOffset + index * 32;
    put32(bytes, record, kind);
    put32(bytes, record + 4, flags);
    put64(bytes, record + 8, valueOffset);
    put64(bytes, record + 16, targetOffset);
    put64(bytes, record + 24, 65);
  };
  state(0, UINT32_MAX - 1, 1, 0, UINT64_MAX);
  state(1, UINT32_MAX - 1, 1, 65, UINT64_MAX);
  state(2, UINT32_MAX, 1, 130, 65);

  put64(bytes, connectivityOffset, 0);
  put64(bytes, connectivityOffset + 8, 65);
  put64(bytes, connectivityOffset + 16, 65);
  put64(bytes, 24, bytes.size());
  put64(bytes, 176, 3);
  put64(bytes, 184, connectivityOffset);
  put64(bytes, 192, 1);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeMixedUWireDriverBytecode(bool overlap) {
  std::vector<uint8_t> bytes = makeConnectedDriverBytecode();
  size_t stateOffset = get64(bytes, 168);
  size_t connectivityOffset = get64(bytes, 184);
  bytes.insert(bytes.begin() + connectivityOffset, 32, 0);

  // Net zero is uwire and net one is wire. Both drivers target the wire side
  // of the aliases, so component-wide effective uwire semantics must still
  // reject overlap. The second driver otherwise targets distinct component 1.
  put32(bytes, stateOffset + 4, 5);
  put32(bytes, stateOffset + 32 + 4, 1);
  put32(bytes, stateOffset + 64 + 4, 1);
  put64(bytes, stateOffset + 64 + 24, 1);
  put32(bytes, connectivityOffset, UINT32_MAX);
  put32(bytes, connectivityOffset + 4, 1);
  put64(bytes, connectivityOffset + 8, 131);
  put64(bytes, connectivityOffset + 16, overlap ? 65 : 66);
  put64(bytes, connectivityOffset + 24, 1);

  size_t movedConnectivity = connectivityOffset + 32;
  bytes[movedConnectivity + 24] = 2;
  bytes[movedConnectivity + 25] = 0;
  bytes[movedConnectivity + 26] = 2;
  put64(bytes, 24, bytes.size());
  put64(bytes, 176, 4);
  put64(bytes, 184, movedConnectivity);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeMixedWiredDriverBytecode(bool rhsDominates) {
  std::vector<uint8_t> bytes = makeConnectedDriverBytecode();
  size_t stateOffset = get64(bytes, 168);
  size_t connectivityOffset = get64(bytes, 184);
  bytes.insert(bytes.begin() + connectivityOffset, 32, 0);

  // Net zero is wand, net one is wor, and each side contributes one strong
  // driver. The preserved port-dominance bit selects the simulated net kind.
  put32(bytes, stateOffset + 4, 1u | resolutionFlags(3, false));
  put32(bytes, stateOffset + 32 + 4, 1u | resolutionFlags(4, false));
  put32(bytes, stateOffset + 64 + 4,
        driverFlags(6, 6) | resolutionFlags(4, true));
  put32(bytes, connectivityOffset, UINT32_MAX);
  put32(bytes, connectivityOffset + 4,
        driverFlags(6, 6) | resolutionFlags(3, true));
  put64(bytes, connectivityOffset + 8, 195);
  put64(bytes, connectivityOffset + 16, 0);
  put64(bytes, connectivityOffset + 24, 65);

  size_t movedConnectivity = connectivityOffset + 32;
  bytes[movedConnectivity + 24] = 3;
  bytes[movedConnectivity + 25] = 4;
  bytes[movedConnectivity + 26] = rhsDominates ? 6 : 2;
  put64(bytes, 24, bytes.size());
  put64(bytes, 176, 4);
  put64(bytes, 184, movedConnectivity);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeMultiSinkWiredDriverBytecode() {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 4 * 40;
  std::vector<uint8_t> bytes = makeConnectedDriverBytecode();
  size_t stateOffset = get64(bytes, 168);
  size_t oldConnectivity = get64(bytes, 184);

  // Insert a second wand net before the driver. Net zero (wire) is dominated
  // independently by both wand endpoints, yielding two sinks of one kind.
  bytes.insert(bytes.begin() + stateOffset + 64, 32, 0);
  size_t connectivity = oldConnectivity + 32;
  bytes.resize(bytes.size() + 32, 0);
  put32(bytes, stateOffset + 32 + 4, 1u | resolutionFlags(3, false));
  put32(bytes, stateOffset + 64, UINT32_MAX - 1);
  put32(bytes, stateOffset + 64 + 4, 1u | resolutionFlags(3, false));
  put64(bytes, stateOffset + 64 + 8, 130);
  put64(bytes, stateOffset + 64 + 16, UINT64_MAX);
  put64(bytes, stateOffset + 64 + 24, 65);
  put32(bytes, stateOffset + 96 + 4,
        driverFlags(6, 6) | resolutionFlags(3, true));
  put64(bytes, stateOffset + 96 + 8, 195);
  put64(bytes, stateOffset + 96 + 16, 65);

  put32(bytes, codeOffset + 1 * 32 + 12, 195);
  put64(bytes, codeOffset + 1 * 32 + 24, 195);
  bytes.resize(connectivity + 130 * 32, 0);
  for (uint64_t bit = 0; bit != 65; ++bit) {
    for (uint64_t sink = 0; sink != 2; ++sink) {
      size_t record = connectivity + (bit * 2 + sink) * 32;
      put64(bytes, record, bit);
      put64(bytes, record + 8, (sink == 0 ? 65 : 130) + bit);
      put64(bytes, record + 16, 1);
      bytes[record + 24] = 0;
      bytes[record + 25] = 3;
      bytes[record + 26] = 6;
    }
  }
  put64(bytes, 24, bytes.size());
  put64(bytes, 176, 4);
  put64(bytes, 184, connectivity);
  put64(bytes, 192, 130);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeVPIBytecode() {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 10 * 40;
  constexpr size_t operandOffset = codeOffset + 11 * 32;
  constexpr size_t constantOffset = operandOffset + 13 * 8;
  constexpr size_t continuationOffset = constantOffset + 32;
  constexpr size_t intrinsicOffset = continuationOffset + 24;
  constexpr size_t siteOffset = intrinsicOffset + 5 * 16;
  constexpr size_t stateOffset = siteOffset + 5 * 16;
  std::vector<uint8_t> bytes(stateOffset, 0);
  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 1);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, 10);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, 11);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, 13);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, 32);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 1);
  put64(bytes, 136, intrinsicOffset);
  put64(bytes, 144, 5);
  put64(bytes, 152, siteOffset);
  put64(bytes, 160, 5);
  put64(bytes, 168, stateOffset);
  put64(bytes, 176, 0);
  put64(bytes, 184, stateOffset);

  put64(bytes, functionOffset, 1);
  put64(bytes, functionOffset + 16, 0);
  put64(bytes, functionOffset + 24, 11);
  put64(bytes, functionOffset + 32, 0);
  put64(bytes, functionOffset + 40, 10);
  put64(bytes, functionOffset + 56, 176);
  put64(bytes, functionOffset + 64, 8);
  put64(bytes, functionOffset + 72, 0);
  put64(bytes, functionOffset + 80, 1);
  put64(bytes, functionOffset + 88, 1);

  auto layout = [&](size_t index, uint8_t kind, uint32_t width, uint64_t offset,
                    uint64_t size) {
    size_t record = layoutOffset + index * 40;
    bytes[record] = kind;
    put32(bytes, record + 4, width);
    put64(bytes, record + 8, offset);
    put64(bytes, record + 16, size);
  };
  layout(0, OBELISK_RT_DBREG_BITS, 64, 0, 8);
  layout(1, OBELISK_RT_DBREG_STATUS, 64, 8, 8);
  layout(2, OBELISK_RT_DBREG_BITS, 64, 16, 8);
  layout(3, OBELISK_RT_DBREG_STATUS, 64, 24, 8);
  layout(4, OBELISK_RT_DBREG_LOGIC, 65, 32, 32);
  layout(5, OBELISK_RT_DBREG_STATUS, 64, 64, 8);
  layout(6, OBELISK_RT_DBREG_LOGIC, 65, 72, 32);
  layout(7, OBELISK_RT_DBREG_STATUS, 64, 104, 8);
  layout(8, OBELISK_RT_DBREG_HANDLE, 256, 112, 32);
  layout(9, OBELISK_RT_DBREG_LOGIC, 65, 144, 32);

  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              0);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              1);
  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_CONSTANT, 0, 6, 0, 0, 0, 0,
              0);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              2);
  instruction(bytes, codeOffset, 4, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              3);
  instruction(bytes, codeOffset, 5, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              4);
  instruction(bytes, codeOffset, 6, OBELISK_RT_DB_STORE_STATE, 0, 0, 8, 6);
  instruction(bytes, codeOffset, 7, OBELISK_RT_DB_LOAD_STATE, 0, 9, 8);
  instruction(bytes, codeOffset, 8, OBELISK_RT_DB_STORE_FRAME, 0, 0, 9);
  instruction(bytes, codeOffset, 9, OBELISK_RT_DB_STORE_FRAME,
              OBELISK_RT_DESCRIPTOR_STORAGE, 0, 8, 0, 0, 65, 32);
  // Keep the process live so the test can inspect its process-owned automatic
  // state. Destroying the instance below releases that owner reference.
  instruction(bytes, codeOffset, 10, OBELISK_RT_DB_CONTINUE);

  // ROOT -> cursor, status.
  put32(bytes, operandOffset, 0);
  put32(bytes, operandOffset + 8, 1);
  // CHILD(cursor) -> cursor, status.
  put32(bytes, operandOffset + 2 * 8 + 4, 0);
  put32(bytes, operandOffset + 3 * 8, 2);
  put32(bytes, operandOffset + 4 * 8, 3);
  // WRITE(cursor, value) -> status.
  put32(bytes, operandOffset + 5 * 8 + 4, 2);
  put32(bytes, operandOffset + 6 * 8 + 4, 6);
  put32(bytes, operandOffset + 7 * 8, 7);
  // READ(cursor) -> value, status.
  put32(bytes, operandOffset + 8 * 8 + 4, 2);
  put32(bytes, operandOffset + 9 * 8, 4);
  put32(bytes, operandOffset + 10 * 8, 5);
  // STATE_ALLOC(value) -> handle.
  put32(bytes, operandOffset + 11 * 8 + 4, 6);
  put32(bytes, operandOffset + 12 * 8, 8);

  put64(bytes, constantOffset, UINT64_C(0x123456789abcdef0));
  put64(bytes, constantOffset + 8, 1);
  put64(bytes, constantOffset + 16, UINT64_C(0x30));
  put32(bytes, continuationOffset, 0);
  put32(bytes, continuationOffset + 4, 0);
  put64(bytes, continuationOffset + 8, 0);

  struct Intrinsic {
    uint32_t id, inputs, outputs;
  };
  std::array<Intrinsic, 5> intrinsics{{
      {OBELISK_RT_INTRINSIC_V1_VPI_ROOT, 0, 2},
      {OBELISK_RT_INTRINSIC_V1_VPI_CHILD, 1, 2},
      {OBELISK_RT_INTRINSIC_V1_VPI_WRITE, 2, 1},
      {OBELISK_RT_INTRINSIC_V1_VPI_READ, 1, 2},
      {OBELISK_RT_INTRINSIC_V1_STATE_ALLOC, 1, 1},
  }};
  std::array<uint32_t, 5> firstOperands{{0, 2, 5, 8, 11}};
  for (size_t index = 0; index != intrinsics.size(); ++index) {
    size_t intrinsic = intrinsicOffset + index * 16;
    put32(bytes, intrinsic, intrinsics[index].id);
    put32(bytes, intrinsic + 4, intrinsics[index].inputs);
    put32(bytes, intrinsic + 8, intrinsics[index].outputs);
    size_t site = siteOffset + index * 16;
    put32(bytes, site, index);
    put32(bytes, site + 4, firstOperands[index]);
    put32(bytes, site + 8, intrinsics[index].inputs);
    put32(bytes, site + 12, intrinsics[index].outputs);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makePartialAutomaticBytecode(bool nba) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 10 * 40;
  constexpr size_t operandOffset = codeOffset + 11 * 32;
  constexpr size_t constantOffset = operandOffset + 13 * 8;
  constexpr size_t continuationOffset = constantOffset + 32;
  constexpr size_t intrinsicOffset = continuationOffset + 24;
  constexpr size_t siteOffset = intrinsicOffset + 5 * 16;
  std::vector<uint8_t> bytes = makeVPIBytecode();
  // The 65-bit initial/replacement value is also read as signed i64 -3 for a
  // partially overlapping view [-3, 62).
  put64(bytes, constantOffset, UINT64_MAX - 2);
  instruction(bytes, codeOffset, 4, OBELISK_RT_DB_CONSTANT, 0, 2, 0, 0, 0, 0,
              0);
  instruction(bytes, codeOffset, 6, OBELISK_RT_DB_HANDLE_OFFSET, 0, 8, 8, 2, 0,
              65, 0);
  if (!nba) {
    instruction(bytes, codeOffset, 7, OBELISK_RT_DB_STORE_STATE, 0, 0, 8, 6);
    instruction(bytes, codeOffset, 8, OBELISK_RT_DB_LOAD_STATE, 0, 9, 8);
    instruction(bytes, codeOffset, 9, OBELISK_RT_DB_STORE_FRAME, 0, 0, 9);
  } else {
    instruction(bytes, codeOffset, 7, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
                3);
    instruction(bytes, codeOffset, 8, OBELISK_RT_DB_NOP);
    instruction(bytes, codeOffset, 9, OBELISK_RT_DB_STORE_FRAME,
                OBELISK_RT_DESCRIPTOR_STORAGE, 0, 8, 0, 0, 65, 0);
    size_t intrinsic = intrinsicOffset + 3 * 16;
    put32(bytes, intrinsic, OBELISK_RT_INTRINSIC_V1_NBA);
    put32(bytes, intrinsic + 4, 2);
    put32(bytes, intrinsic + 8, 0);
    put32(bytes, intrinsic + 12, 0);
    size_t site = siteOffset + 3 * 16;
    put32(bytes, site, 3);
    put32(bytes, site + 4, 8);
    put32(bytes, site + 8, 2);
    put32(bytes, site + 12, 0);
    put32(bytes, operandOffset + 8 * 8 + 4, 6);
    put32(bytes, operandOffset + 9 * 8 + 4, 8);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeAutomaticFrameLoadBytecode() {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 10 * 40;
  std::vector<uint8_t> bytes = makeVPIBytecode();
  put64(bytes, functionOffset + 24, 4);
  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_LOAD_FRAME,
              OBELISK_RT_DESCRIPTOR_STORAGE, 8, 0, 0, 0, 65, 0);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_LOAD_STATE, 0, 9, 8);
  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_STORE_FRAME, 0, 0, 9);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_TERMINATE);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeStaticHandleRoundTripBytecode() {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 10 * 40;
  std::vector<uint8_t> bytes = makeVPIBytecode();
  put64(bytes, functionOffset + 24, 8);
  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_LOAD_FRAME,
              OBELISK_RT_DESCRIPTOR_STORAGE, 8, 0, 0, 0, 65, 0);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_HANDLE_ID, 0, 2, 8);
  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_HANDLE_OFFSET, 0, 8, 8,
              UINT32_MAX, 0, 4, 3);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_LOAD_STATE, 0, 9, 8);
  instruction(bytes, codeOffset, 4, OBELISK_RT_DB_STORE_STATE, 0, 0, 8, 9);
  instruction(bytes, codeOffset, 5, OBELISK_RT_DB_STORE_FRAME,
              OBELISK_RT_DESCRIPTOR_STORAGE, 0, 8, 0, 0, 65, 0);
  instruction(bytes, codeOffset, 6, OBELISK_RT_DB_STORE_FRAME, 0, 0, 2, 0, 0, 8,
              8);
  instruction(bytes, codeOffset, 7, OBELISK_RT_DB_TERMINATE);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeStaticNBABytecode() {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 10 * 40;
  constexpr size_t operandOffset = codeOffset + 11 * 32;
  constexpr size_t constantOffset = operandOffset + 13 * 8;
  constexpr size_t continuationOffset = constantOffset + 32;
  constexpr size_t intrinsicOffset = continuationOffset + 24;
  constexpr size_t siteOffset = intrinsicOffset + 5 * 16;
  std::vector<uint8_t> bytes = makeVPIBytecode();
  put64(bytes, functionOffset + 24, 4);
  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_LOAD_FRAME,
              OBELISK_RT_DESCRIPTOR_STORAGE, 8, 0, 0, 0, 65, 0);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_CONSTANT, 0, 6);
  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              3);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_TERMINATE);
  size_t intrinsic = intrinsicOffset + 3 * 16;
  put32(bytes, intrinsic, OBELISK_RT_INTRINSIC_V1_NBA);
  put32(bytes, intrinsic + 4, 2);
  put32(bytes, intrinsic + 8, 0);
  put32(bytes, intrinsic + 12, 0);
  size_t site = siteOffset + 3 * 16;
  put32(bytes, site, 3);
  put32(bytes, site + 4, 8);
  put32(bytes, site + 8, 2);
  put32(bytes, site + 12, 0);
  put32(bytes, operandOffset + 8 * 8 + 4, 6);
  put32(bytes, operandOffset + 9 * 8 + 4, 8);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeAutomaticSpawnBytecode(uint32_t childRank = 0) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 2 * 96;
  constexpr size_t codeOffset = layoutOffset + 5 * 40;
  constexpr size_t operandOffset = codeOffset + 8 * 32;
  constexpr size_t constantOffset = operandOffset + 4 * 8;
  constexpr size_t continuationOffset = constantOffset + 32;
  constexpr size_t intrinsicOffset = continuationOffset + 2 * 24;
  constexpr size_t siteOffset = intrinsicOffset + 2 * 16;
  constexpr size_t stateOffset = siteOffset + 2 * 16;
  std::vector<uint8_t> bytes(stateOffset + 2 * 32, 0);
  std::memcpy(bytes.data(), "OBBCDS1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, functionOffset);
  put64(bytes, 48, 2);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, 5);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, 8);
  put64(bytes, 88, operandOffset);
  put64(bytes, 96, 4);
  put64(bytes, 104, constantOffset);
  put64(bytes, 112, 32);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 2);
  put64(bytes, 136, intrinsicOffset);
  put64(bytes, 144, 2);
  put64(bytes, 152, siteOffset);
  put64(bytes, 160, 2);
  put64(bytes, 168, stateOffset);
  put64(bytes, 176, 2);
  put64(bytes, 184, bytes.size());

  auto function = [&](size_t index, uint64_t id, uint32_t scheduleRank,
                      uint64_t firstInstruction, uint64_t instructionCount,
                      uint64_t firstLayout, uint64_t layoutCount,
                      uint64_t scratchSize, uint64_t firstContinuation) {
    size_t record = functionOffset + index * 96;
    put64(bytes, record, id);
    put64(bytes, record + 8, scheduleRank);
    put64(bytes, record + 16, firstInstruction);
    put64(bytes, record + 24, instructionCount);
    put64(bytes, record + 32, firstLayout);
    put64(bytes, record + 40, layoutCount);
    put32(bytes, record + 48, 1);
    put64(bytes, record + 56, scratchSize);
    put64(bytes, record + 64, 8);
    put64(bytes, record + 72, firstContinuation);
    put64(bytes, record + 80, 1);
    // One eight-byte canonical capture plus the process flag.
    put64(bytes, record + 88, 17);
  };
  function(0, 1, 0, 0, 3, 0, 2, 64, 0);
  function(1, 2, childRank, 3, 5, 2, 3, 96, 1);

  auto layout = [&](size_t index, uint8_t kind, uint32_t width, uint64_t offset,
                    uint64_t size) {
    size_t record = layoutOffset + index * 40;
    bytes[record] = kind;
    put32(bytes, record + 4, width);
    put64(bytes, record + 8, offset);
    put64(bytes, record + 16, size);
  };
  layout(0, OBELISK_RT_DBREG_HANDLE, 256, 0, 32);
  layout(1, OBELISK_RT_DBREG_HANDLE, 256, 32, 32);
  layout(2, OBELISK_RT_DBREG_HANDLE, 256, 0, 32);
  layout(3, OBELISK_RT_DBREG_LOGIC, 65, 32, 32);
  layout(4, OBELISK_RT_DBREG_HANDLE, 256, 64, 32);

  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_LOAD_FRAME,
              OBELISK_RT_DESCRIPTOR_STORAGE, 0, 0, 0, 0, 65, 0);
  instruction(bytes, codeOffset, 1, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              0);
  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_TERMINATE);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_LOAD_FRAME,
              OBELISK_RT_DESCRIPTOR_STORAGE, 0, 0, 0, 0, 65, 0);
  instruction(bytes, codeOffset, 4, OBELISK_RT_DB_CONSTANT, 0, 1);
  instruction(bytes, codeOffset, 5, OBELISK_RT_DB_INTRINSIC, 0, 0, 0, 0, 0, 0,
              1);
  instruction(bytes, codeOffset, 6, OBELISK_RT_DB_STORE_STATE, 0, 0, 0, 1);
  instruction(bytes, codeOffset, 7, OBELISK_RT_DB_TERMINATE);

  // SPAWN input and output, followed by STATE_ALLOC input and output.
  put32(bytes, operandOffset + 4, 0);
  put32(bytes, operandOffset + 8, 1);
  put32(bytes, operandOffset + 2 * 8 + 4, 1);
  put32(bytes, operandOffset + 3 * 8, 2);
  put64(bytes, constantOffset, UINT64_C(0x123456789abcdef0));
  put64(bytes, constantOffset + 8, 1);
  put64(bytes, constantOffset + 16, UINT64_C(0x30));
  put64(bytes, constantOffset + 24, 0);
  for (size_t index = 0; index != 2; ++index) {
    size_t continuation = continuationOffset + index * 24;
    put32(bytes, continuation, index);
    put32(bytes, continuation + 4, 0);
    put64(bytes, continuation + 8, index == 0 ? 0 : 3);
    put32(bytes, continuation + 16, index == 0 ? 0 : childRank);
  }
  put32(bytes, intrinsicOffset, OBELISK_RT_INTRINSIC_V1_SPAWN);
  put32(bytes, intrinsicOffset + 4, 1);
  put32(bytes, intrinsicOffset + 8, 1);
  put32(bytes, intrinsicOffset + 12, 1);
  put32(bytes, intrinsicOffset + 16, OBELISK_RT_INTRINSIC_V1_STATE_ALLOC);
  put32(bytes, intrinsicOffset + 16 + 4, 1);
  put32(bytes, intrinsicOffset + 16 + 8, 1);
  put32(bytes, siteOffset, 0);
  put32(bytes, siteOffset + 4, 0);
  put32(bytes, siteOffset + 8, 1);
  put32(bytes, siteOffset + 12, 1);
  put32(bytes, siteOffset + 16, 1);
  put32(bytes, siteOffset + 16 + 4, 2);
  put32(bytes, siteOffset + 16 + 8, 1);
  put32(bytes, siteOffset + 16 + 12, 1);
  for (size_t index = 0; index != 2; ++index) {
    size_t capture = stateOffset + index * 32;
    put32(bytes, capture, index);
    put32(bytes, capture + 4, 0);
    put64(bytes, capture + 8, 0);
    put64(bytes, capture + 16, UINT64_MAX);
    put64(bytes, capture + 24, 8);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makePreponedEventSpawnBytecode() {
  std::vector<uint8_t> bytes = makeAutomaticSpawnBytecode();
  size_t codeOffset = get64(bytes, 72);
  // The root reconstructs the canonical event capture and passes it through a
  // bytecode spawn. The child reconstructs the same capture and terminates.
  instruction(bytes, codeOffset, 0, OBELISK_RT_DB_LOAD_FRAME,
              OBELISK_RT_DESCRIPTOR_EVENT, 0, 0, 0, 0, 0, 0);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_LOAD_FRAME,
              OBELISK_RT_DESCRIPTOR_EVENT, 0, 0, 0, 0, 0, 0);
  instruction(bytes, codeOffset, 4, OBELISK_RT_DB_NOP);
  instruction(bytes, codeOffset, 5, OBELISK_RT_DB_NOP);
  instruction(bytes, codeOffset, 6, OBELISK_RT_DB_NOP);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeSignalWaitSpawnBytecode() {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 2 * 96;
  constexpr size_t codeOffset = layoutOffset + 5 * 40;
  constexpr size_t operandOffset = codeOffset + 8 * 32;
  constexpr size_t constantOffset = operandOffset + 4 * 8;
  constexpr size_t continuationOffset = constantOffset + 32;
  constexpr size_t intrinsicOffset = continuationOffset + 2 * 24;
  constexpr size_t siteOffset = intrinsicOffset + 2 * 16;
  constexpr size_t stateOffset = siteOffset + 2 * 16;
  std::vector<uint8_t> bytes = makeAutomaticSpawnBytecode();

  // The child materializes the scratch offset of its wait record, suspends on
  // that record, and resumes at its terminating continuation.
  bytes.insert(bytes.begin() + intrinsicOffset, 48, 0);
  put64(bytes, 24, bytes.size());
  put64(bytes, 128, 4);
  put64(bytes, 136, intrinsicOffset + 48);
  put64(bytes, 152, siteOffset + 48);
  put64(bytes, 168, stateOffset + 48);
  put64(bytes, 184, bytes.size());
  put64(bytes, functionOffset + 96 + 80, 3);
  put64(bytes, functionOffset + 96 + 88, 129);
  bytes[layoutOffset + 3 * 40] = OBELISK_RT_DBREG_LOGIC;
  put32(bytes, layoutOffset + 3 * 40 + 4, 64);
  put64(bytes, layoutOffset + 3 * 40 + 16, 16);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_CONSTANT, 0, 1);
  instruction(bytes, codeOffset, 4, OBELISK_RT_DB_SUSPEND,
              OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0, 0, 1);
  instruction(bytes, codeOffset, 5, OBELISK_RT_DB_CONSTANT, 0, 1);
  instruction(bytes, codeOffset, 6, OBELISK_RT_DB_SUSPEND,
              OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0, 0, 2);
  instruction(bytes, codeOffset, 7, OBELISK_RT_DB_TERMINATE);
  put64(bytes, constantOffset, 8);
  put64(bytes, constantOffset + 8, 0);
  put32(bytes, continuationOffset + 2 * 24, 1);
  put32(bytes, continuationOffset + 2 * 24 + 4, 1);
  put64(bytes, continuationOffset + 2 * 24 + 8, 5);
  put32(bytes, continuationOffset + 2 * 24 + 16, 0);
  put32(bytes, continuationOffset + 3 * 24, 1);
  put32(bytes, continuationOffset + 3 * 24 + 4, 2);
  put64(bytes, continuationOffset + 3 * 24 + 8, 7);
  put32(bytes, continuationOffset + 3 * 24 + 16, 0);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

struct DesignEventOnlyComputedWait {
  obelisk_rt_computed_wait_record_v1 wait{};
  obelisk_rt_computed_observer_v1 observer{};
  obelisk_rt_computed_dependency_v1 dependency{};
  obelisk_rt_computed_clause_v1 clause{};
  uint64_t previousValue = 0;
  uint64_t previousUnknown = 0;
};

constexpr uint64_t designEventOnlyObserverID = 7;
constexpr uint64_t designEventOnlyDependencyID = 0x7d02;

std::vector<uint8_t> makeEventOnlyComputedWaitBytecode() {
  std::vector<uint8_t> bytes = makeObserverBytecode(OBELISK_RT_DBREG_BITS);
  size_t functionOffset = get64(bytes, 40);
  size_t layoutOffset = get64(bytes, 56);
  size_t codeOffset = get64(bytes, 72);
  size_t operandOffset = get64(bytes, 88);
  size_t constantOffset = get64(bytes, 104);
  size_t continuationOffset = get64(bytes, 120);

  bytes.insert(bytes.begin() + layoutOffset, 96, 0);
  layoutOffset += 96;
  codeOffset += 96;
  operandOffset += 96;
  constantOffset += 96;
  continuationOffset += 96;
  bytes.insert(bytes.begin() + codeOffset, 40, 0);
  codeOffset += 40;
  operandOffset += 40;
  constantOffset += 40;
  continuationOffset += 40;
  bytes.insert(bytes.begin() + operandOffset, 3 * 32, 0);
  operandOffset += 3 * 32;
  constantOffset += 3 * 32;
  continuationOffset += 3 * 32;
  bytes.resize(bytes.size() + 2 * 24, 0);

  put64(bytes, 24, bytes.size());
  put64(bytes, 48, 2);
  put64(bytes, 56, layoutOffset);
  put64(bytes, 64, 3);
  put64(bytes, 72, codeOffset);
  put64(bytes, 80, 5);
  put64(bytes, 88, operandOffset);
  put64(bytes, 104, constantOffset);
  put64(bytes, 120, continuationOffset);
  put64(bytes, 128, 3);
  put64(bytes, 136, bytes.size());
  put64(bytes, 152, bytes.size());
  put64(bytes, 168, bytes.size());
  put64(bytes, 184, bytes.size());

  size_t processFunction = functionOffset + 96;
  put64(bytes, processFunction, 8);
  put64(bytes, processFunction + 8, 0);
  put64(bytes, processFunction + 16, 2);
  put64(bytes, processFunction + 24, 3);
  put64(bytes, processFunction + 32, 2);
  put64(bytes, processFunction + 40, 1);
  put64(bytes, processFunction + 56, 8);
  put64(bytes, processFunction + 64, 8);
  put64(bytes, processFunction + 72, 1);
  put64(bytes, processFunction + 80, 2);
  // Process-function flags encode the canonical-frame byte count in two-byte
  // units alongside the process bit.
  put64(bytes, processFunction + 88,
        sizeof(DesignEventOnlyComputedWait) * 2 + 1);

  size_t processLayout = layoutOffset + 2 * 40;
  bytes[processLayout] = OBELISK_RT_DBREG_BITS;
  put32(bytes, processLayout + 4, 64);
  put64(bytes, processLayout + 8, 0);
  put64(bytes, processLayout + 16, 8);
  // The observer result is scalar for this event-primary wait.
  put32(bytes, layoutOffset + 40 + 4, 1);
  put64(bytes, layoutOffset + 40 + 16, 8);

  instruction(bytes, codeOffset, 2, OBELISK_RT_DB_CONSTANT, 0, 0);
  instruction(bytes, codeOffset, 3, OBELISK_RT_DB_SUSPEND,
              OBELISK_RT_SUSPEND_OBSERVER, 0, 0, 0, 0, 0, 1);
  instruction(bytes, codeOffset, 4, OBELISK_RT_DB_TERMINATE);
  put32(bytes, continuationOffset + 24, 1);
  put32(bytes, continuationOffset + 24 + 4, 0);
  put64(bytes, continuationOffset + 24 + 8, 2);
  put32(bytes, continuationOffset + 48, 1);
  put32(bytes, continuationOffset + 48 + 4, 1);
  put64(bytes, continuationOffset + 48 + 8, 4);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeDatabase(bool writable = true,
                                  bool withSource = true) {
  constexpr uint64_t scopeOffset = 176;
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t typeOffset = 336;
  constexpr uint64_t stringOffset = 416;
  constexpr uint64_t stringSize = 28;
  constexpr uint64_t indexOffset = 448;
  std::vector<uint8_t> bytes(indexOffset + 48, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16,
        OBELISK_RT_DESIGN_PROFILE_READ |
            (writable ? OBELISK_RT_DESIGN_PROFILE_WRITE : 0));
  put32(bytes, 20, OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, scopeOffset);
  put64(bytes, 48, scopeOffset);
  put64(bytes, 56, 1);
  put64(bytes, 64, objectOffset);
  put64(bytes, 72, 1);
  put64(bytes, 80, typeOffset);
  put64(bytes, 88, 1);
  put64(bytes, 96, stringOffset);
  put64(bytes, 104, stringSize);
  put64(bytes, 112, indexOffset);
  put64(bytes, 120, 2);
  put64(bytes, 128, stringOffset);
  put64(bytes, 144, stringOffset);
  put64(bytes, 160, stringOffset);

  put32(bytes, scopeOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, vpiModule));
  put32(bytes, scopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, scopeOffset + 8, 1);
  put64(bytes, scopeOffset + 24, objectOffset);
  put64(bytes, scopeOffset + 40, stringOffset);
  if (withSource) {
    put64(bytes, scopeOffset + 48, stringOffset + 20);
    put64(bytes, scopeOffset + 56, (uint64_t{3} << 32) | 1);
  }

  put32(bytes, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STORAGE, vpiReg));
  put32(bytes, objectOffset + 4,
        OBELISK_RT_DESIGN_CAP_READ |
            (writable ? OBELISK_RT_DESIGN_CAP_WRITE : 0));
  put64(bytes, objectOffset + 8, 7);
  put64(bytes, objectOffset + 16, scopeOffset);
  if (withSource)
    put64(bytes, objectOffset + 32, stringOffset + 20);
  put64(bytes, objectOffset + 40, stringOffset + 4);
  put64(bytes, objectOffset + 48, typeOffset);
  put64(bytes, objectOffset + 56, 65);
  put64(bytes, objectOffset + 64, 64);
  put64(bytes, objectOffset + 72, 0);
  put64(bytes, objectOffset + 80, 0);
  if (withSource)
    put64(bytes, objectOffset + 88, (uint64_t{7} << 32) | 5);

  put32(bytes, typeOffset, OBELISK_RT_DESIGN_RECORD_TYPE);
  put32(bytes, typeOffset + 4,
        OBELISK_RT_DESIGN_TYPE_SCALAR |
            ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_PACKED)
             << 8));
  put64(bytes, typeOffset + 8, 65);
  put64(bytes, typeOffset + 16, 64);
  put64(bytes, typeOffset + 72, stringOffset + 14);
  std::memcpy(bytes.data() + stringOffset, "top\0top.value\0logic\0test.sv\0",
              stringSize);

  struct Entry {
    uint64_t hash;
    uint64_t name;
    uint64_t record;
  };
  std::array<Entry, 2> index{{
      {nameHash("top"), stringOffset, scopeOffset},
      {nameHash("top.value"), stringOffset + 4, objectOffset},
  }};
  std::sort(index.begin(), index.end(),
            [](const Entry &left, const Entry &right) {
              return std::tie(left.hash, left.name) <
                     std::tie(right.hash, right.name);
            });
  for (size_t entry = 0; entry != index.size(); ++entry) {
    put64(bytes, indexOffset + entry * 24, index[entry].hash);
    put64(bytes, indexOffset + entry * 24 + 8, index[entry].name);
    put64(bytes, indexOffset + entry * 24 + 16, index[entry].record);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeFixedPropertyDatabase(bool protectObject = true) {
  std::vector<uint8_t> bytes = makeDatabase();
  constexpr uint64_t stringOffset = 416;
  const uint32_t directoryOffset = static_cast<uint32_t>(bytes.size());
  const uint64_t semanticRootOffset = directoryOffset + kSemanticDirectorySize;
  const uint64_t propertyOffset = semanticRootOffset + 4;
  const uint64_t propertyCount = protectObject ? 3 : 2;
  bytes.resize(propertyOffset + propertyCount * 16, 0);
  put32(bytes, 12, directoryOffset);
  put64(bytes, 24, bytes.size());
  put64(bytes, directoryOffset + 32, semanticRootOffset);
  put64(bytes, directoryOffset + 40, 1);
  put64(bytes, directoryOffset + 112, propertyOffset);
  put64(bytes, directoryOffset + 120, propertyCount);
  put32(bytes, semanticRootOffset, UINT32_MAX);
  auto property = [&](uint64_t index, uint32_t source, uint16_t selector,
                      uint16_t kind, uint64_t payload) {
    uint64_t offset = propertyOffset + index * 16;
    put32(bytes, offset, source);
    put16(bytes, offset + 4, selector);
    put16(bytes, offset + 6, kind);
    put64(bytes, offset + 8, payload);
  };
  // The definition site is deliberately distinct from the use site encoded
  // in the scope record.  "logic" is reused from the canonical string pool.
  property(0, 0, vpiDefFile, 3, stringOffset + 14);
  property(1, 0, vpiDefLineNo, 1, 29);
  if (protectObject)
    property(2, uint32_t{1} << 30, vpiIsProtected, 0, 1);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeProtectedScopeDatabase() {
  std::vector<uint8_t> bytes = makeFixedPropertyDatabase(false);
  const uint32_t directoryOffset = static_cast<uint32_t>(bytes[12]) |
                                   (static_cast<uint32_t>(bytes[13]) << 8) |
                                   (static_cast<uint32_t>(bytes[14]) << 16) |
                                   (static_cast<uint32_t>(bytes[15]) << 24);
  const uint64_t propertyOffset = get64(bytes, directoryOffset + 112);
  const uint64_t propertyCount = get64(bytes, directoryOffset + 120);
  EXPECT_EQ(propertyCount, 2u);
  bytes.resize(bytes.size() + 16, 0);
  put64(bytes, 24, bytes.size());
  put64(bytes, directoryOffset + 120, propertyCount + 1);
  put32(bytes, propertyOffset + propertyCount * 16, 0);
  put16(bytes, propertyOffset + propertyCount * 16 + 4, vpiIsProtected);
  put16(bytes, propertyOffset + propertyCount * 16 + 6, 0);
  put64(bytes, propertyOffset + propertyCount * 16 + 8, 1);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeSemanticTraversalDatabase(bool wildcardAssoc = false) {
  std::vector<uint8_t> bytes = makeDatabase(false, true);
  constexpr uint32_t typeCount = 11;
  constexpr uint32_t edgeCount = 10;
  const uint32_t directoryOffset = static_cast<uint32_t>(bytes.size());
  const uint64_t typeOffset = directoryOffset + kSemanticDirectorySize;
  const uint64_t edgeOffset = typeOffset + uint64_t{typeCount} * 64;
  const uint64_t rootOffset = edgeOffset + uint64_t{edgeCount} * 24;
  bytes.resize(rootOffset + 4, 0);

  // HeaderReserved points at the optional semantic extension directory.
  put32(bytes, 12, directoryOffset);
  put64(bytes, 24, bytes.size());
  put64(bytes, directoryOffset, typeOffset);
  put64(bytes, directoryOffset + 8, typeCount);
  put64(bytes, directoryOffset + 16, edgeOffset);
  put64(bytes, directoryOffset + 24, edgeCount);
  put64(bytes, directoryOffset + 32, rootOffset);
  put64(bytes, directoryOffset + 40, 1);

  auto type = [&](uint32_t index, uint32_t kind, uint32_t flags,
                  uint32_t firstEdge, uint32_t edges, int64_t left = 0,
                  int64_t right = 0, uint64_t bitWidth = 0,
                  uint32_t queueBound = 0) {
    const uint64_t offset = typeOffset + uint64_t{index} * 64;
    uint32_t publicKind = 0;
    switch (kind) {
    case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_QUEUE:
      publicKind = vpiArrayTypespec;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_STRING:
      publicKind = vpiStringTypespec;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY:
      publicKind = vpiPackedArrayTypespec;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_LOGIC:
      publicKind = vpiLogicTypespec;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT:
      publicKind = vpiStructTypespec;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_ENUM:
      publicKind = vpiEnumTypespec;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_MAILBOX:
      publicKind = vpiClassTypespec;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_BIT:
      publicKind = vpiBitTypespec;
      break;
    default:
      break;
    }
    put32(bytes, offset,
          kind | flags |
              (publicKind << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
    put32(bytes, offset + 4, firstEdge);
    put32(bytes, offset + 8, edges);
    put32(bytes, offset + 12, UINT32_MAX);
    put32(bytes, offset + 16, UINT32_MAX);
    put32(bytes, offset + 28, queueBound);
    put64(bytes, offset + 32, static_cast<uint64_t>(left));
    put64(bytes, offset + 40, static_cast<uint64_t>(right));
    put64(bytes, offset + 48, bitWidth);
  };
  type(0, OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY,
       OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE, 0, 1, 3, 0);
  type(1, OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY, 0, 1, 1);
  type(2, OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY,
       wildcardAssoc ? OBELISK_RT_DESIGN_SEMANTIC_WILDCARD_INDEX : 0, 2, 2);
  type(3,
       wildcardAssoc ? OBELISK_RT_DESIGN_SEMANTIC_UNTYPED
                     : OBELISK_RT_DESIGN_SEMANTIC_STRING,
       0, 4, 0);
  type(4, OBELISK_RT_DESIGN_SEMANTIC_QUEUE, 0, 4, 1, 0, 0, 0, 9);
  type(5, OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY,
       OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE |
           OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE,
       5, 1, 7, 4);
  type(6, OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT,
       OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE, 6, 2, 0, 0, 2);
  type(7, OBELISK_RT_DESIGN_SEMANTIC_LOGIC,
       OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE, 8, 0);
  type(8, OBELISK_RT_DESIGN_SEMANTIC_ENUM, 0, 8, 1);
  type(9, OBELISK_RT_DESIGN_SEMANTIC_BIT, 0, 9, 0);
  type(10, OBELISK_RT_DESIGN_SEMANTIC_MAILBOX, 0, 9, 1);

  auto edge = [&](uint32_t index, uint32_t child, uint32_t role,
                  uint32_t ordinal, uint32_t name = 0,
                  uint64_t packedOffset = 0, uint32_t flags = 0) {
    const uint64_t offset = edgeOffset + uint64_t{index} * 24;
    put32(bytes, offset, child);
    put32(bytes, offset + 4,
          role | ((role == OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER
                       ? (flags == 0
                              ? static_cast<uint32_t>(
                                    OBELISK_RT_DESIGN_SEMANTIC_EDGE_NOT_RANDOM)
                              : flags)
                       : 0)
                  << 8));
    put32(bytes, offset + 8, ordinal);
    put32(bytes, offset + 12, name);
    put64(bytes, offset + 16, packedOffset);
  };
  edge(0, 1, OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT, 0);
  edge(1, 2, OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT, 0);
  edge(2, 3, OBELISK_RT_DESIGN_SEMANTIC_EDGE_ASSOC_INDEX, 0);
  edge(3, 4, OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT, 1);
  edge(4, 5, OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT, 0);
  edge(5, 6, OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT, 0);
  // Relative string-table offsets 14 and 8 spell "logic" and "value".
  edge(6, 7, OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER, 0, 14, 1,
       OBELISK_RT_DESIGN_SEMANTIC_EDGE_RANDOM);
  edge(7, 8, OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER, 1, 8, 0,
       OBELISK_RT_DESIGN_SEMANTIC_EDGE_RANDOM_CYCLIC);
  edge(8, 9, OBELISK_RT_DESIGN_SEMANTIC_EDGE_ENUM_BASE, 0);
  edge(9, 9, OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT, 0);
  put32(bytes, rootOffset, 0);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeNamedSemanticTypespecDatabase() {
  std::vector<uint8_t> bytes = makeSemanticTraversalDatabase();
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t directoryOffset = 496;
  constexpr uint64_t semanticTypeOffset =
      directoryOffset + kSemanticDirectorySize;
  constexpr uint64_t semanticEdgeOffset = semanticTypeOffset + 11 * 64;
  constexpr uint64_t semanticRootOffset = semanticEdgeOffset + 10 * 24;
  put32(bytes, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                         vpiStructTypespec));
  put32(bytes, objectOffset + 4, OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC);
  put64(bytes, objectOffset + 80, 0);
  // A primary named aggregate has no typedef alias. Its semantic name proves
  // that this declaration defines the type rather than aliasing a built-in.
  put32(bytes, semanticTypeOffset + 6 * 64 + 20, 14);
  put32(bytes, semanticRootOffset, 6);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeBuiltinAliasTypespecDatabase() {
  std::vector<uint8_t> bytes = makeSemanticTraversalDatabase();
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t semanticRootOffset =
      496 + kSemanticDirectorySize + 11 * 64 + 10 * 24;
  put32(bytes, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                         vpiLogicTypespec));
  put32(bytes, objectOffset + 4, OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC);
  put64(bytes, objectOffset + 80, 0);
  put32(bytes, semanticRootOffset, 7);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeDirectIntegralVectorDatabase() {
  std::vector<uint8_t> bytes = makeSemanticTraversalDatabase();
  constexpr uint64_t semanticTypeOffset = 496 + kSemanticDirectorySize;
  constexpr uint64_t semanticRootOffset =
      496 + kSemanticDirectorySize + 11 * 64 + 10 * 24;
  constexpr uint64_t logic = semanticTypeOffset + 7 * 64;
  put32(bytes, logic,
        OBELISK_RT_DESIGN_SEMANTIC_LOGIC |
            OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE |
            OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE |
            (vpiLogicTypespec
             << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
  put64(bytes, logic + 32, static_cast<uint64_t>(-2));
  put64(bytes, logic + 40, 5);
  put32(bytes, semanticRootOffset, 7);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeMailboxSemanticDatabase() {
  std::vector<uint8_t> bytes = makeSemanticTraversalDatabase();
  constexpr uint64_t semanticRootOffset =
      496 + kSemanticDirectorySize + 11 * 64 + 10 * 24;
  put32(bytes, semanticRootOffset, 10);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeRootStaticRelationDatabase() {
  constexpr uint64_t scopeOffset = 176;
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t classOffset = 336;
  constexpr uint64_t nestedClassOffset = 432;
  constexpr uint64_t methodOffset = 528;
  constexpr uint64_t relationOffset = 624;
  constexpr uint64_t stringOffset = 736;
  constexpr uint64_t stringSize = 49;
  constexpr uint64_t indexOffset = 792;
  std::vector<uint8_t> bytes(indexOffset + 120, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_PROFILE_READ);
  put32(bytes, 20, OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, scopeOffset);
  put64(bytes, 48, scopeOffset);
  put64(bytes, 56, 1);
  put64(bytes, 64, objectOffset);
  put64(bytes, 72, 4);
  put64(bytes, 80, relationOffset);
  put64(bytes, 96, stringOffset);
  put64(bytes, 104, stringSize);
  put64(bytes, 112, indexOffset);
  put64(bytes, 120, 5);
  put64(bytes, 128, relationOffset);
  put64(bytes, 144, relationOffset);
  put64(bytes, 160, relationOffset);
  put64(bytes, 168, 7);

  put32(bytes, scopeOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, 0));
  put32(bytes, scopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, scopeOffset + 8, 1);
  put64(bytes, scopeOffset + 24, objectOffset);
  put64(bytes, scopeOffset + 40, stringOffset);

  put32(bytes, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, vpiPackage));
  put64(bytes, objectOffset + 8, 2);
  put64(bytes, objectOffset + 16, scopeOffset);
  put64(bytes, objectOffset + 24, classOffset);
  put64(bytes, objectOffset + 40, stringOffset + 6);

  put32(bytes, classOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, vpiClassDefn));
  put64(bytes, classOffset + 8, 3);
  put64(bytes, classOffset + 16, scopeOffset);
  put64(bytes, classOffset + 24, nestedClassOffset);
  put64(bytes, classOffset + 40, stringOffset + 12);

  put32(bytes, nestedClassOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, vpiClassDefn));
  put64(bytes, nestedClassOffset + 8, 4);
  put64(bytes, nestedClassOffset + 16, scopeOffset);
  put64(bytes, nestedClassOffset + 24, methodOffset);
  put64(bytes, nestedClassOffset + 40, stringOffset + 19);

  // A source-visible class method keeps its executable code-unit record, but
  // lexical containment is relation-backed just like a static class record.
  put32(bytes, methodOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_FUNCTION, vpiFunction));
  put32(bytes, methodOffset + 4, OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR);
  put64(bytes, methodOffset + 8, 5);
  put64(bytes, methodOffset + 16, scopeOffset);
  put64(bytes, methodOffset + 40, stringOffset + 34);

  put32(bytes, relationOffset, 0);
  put32(bytes, relationOffset + 4, uint32_t{1} << 30);
  put32(bytes, relationOffset + 8, 0);
  put16(bytes, relationOffset + 12, vpiInstance);
  put16(bytes, relationOffset + 14, designRelationSource(0, 0, true));

  // The static records' physical sibling chain only provides image
  // reachability. These paired relations encode their actual lexical graph.
  put32(bytes, relationOffset + 16, 0);
  put32(bytes, relationOffset + 16 + 4, (uint32_t{1} << 30) | uint32_t{1});
  put16(bytes, relationOffset + 16 + 12, vpiClassDefn);
  put16(bytes, relationOffset + 16 + 14,
        designRelationSource(1, vpiPackage, true));

  put32(bytes, relationOffset + 32, 1);
  put32(bytes, relationOffset + 32 + 4, uint32_t{1} << 30);
  put16(bytes, relationOffset + 32 + 12, vpiScope);
  put16(bytes, relationOffset + 32 + 14, designRelationSource(1, vpiClassDefn));

  put32(bytes, relationOffset + 48, 1);
  put32(bytes, relationOffset + 48 + 4, (uint32_t{1} << 30) | uint32_t{2});
  put16(bytes, relationOffset + 48 + 12, vpiInternalScope);
  put16(bytes, relationOffset + 48 + 14,
        designRelationSource(1, vpiClassDefn, true));

  put32(bytes, relationOffset + 64, 1);
  put32(bytes, relationOffset + 64 + 4, (uint32_t{1} << 30) | uint32_t{3});
  put32(bytes, relationOffset + 64 + 8, 1);
  put16(bytes, relationOffset + 64 + 12, vpiInternalScope);
  put16(bytes, relationOffset + 64 + 14,
        designRelationSource(1, vpiClassDefn, true));

  put32(bytes, relationOffset + 80, 2);
  put32(bytes, relationOffset + 80 + 4, (uint32_t{1} << 30) | uint32_t{1});
  put16(bytes, relationOffset + 80 + 12, vpiScope);
  put16(bytes, relationOffset + 80 + 14, designRelationSource(1, vpiClassDefn));

  put32(bytes, relationOffset + 96, 3);
  put32(bytes, relationOffset + 96 + 4, (uint32_t{1} << 30) | uint32_t{1});
  put16(bytes, relationOffset + 96 + 12, vpiScope);
  put16(bytes, relationOffset + 96 + 14, designRelationSource(1, vpiFunction));

  std::memcpy(bytes.data() + stringOffset,
              "$root\0pkg::\0pkg::C\0pkg::C::Nested\0pkg::C::method\0",
              stringSize);
  struct Entry {
    uint64_t hash, name, record;
  };
  std::array<Entry, 5> index{{
      {nameHash("$root"), stringOffset, scopeOffset},
      {nameHash("pkg::"), stringOffset + 6, objectOffset},
      {nameHash("pkg::C"), stringOffset + 12, classOffset},
      {nameHash("pkg::C::Nested"), stringOffset + 19, nestedClassOffset},
      {nameHash("pkg::C::method"), stringOffset + 34, methodOffset},
  }};
  std::sort(index.begin(), index.end(),
            [](const Entry &left, const Entry &right) {
              return std::tie(left.hash, left.name) <
                     std::tie(right.hash, right.name);
            });
  for (size_t entry = 0; entry != index.size(); ++entry) {
    put64(bytes, indexOffset + entry * 24, index[entry].hash);
    put64(bytes, indexOffset + entry * 24 + 8, index[entry].name);
    put64(bytes, indexOffset + entry * 24 + 16, index[entry].record);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeClassMethodRelationDatabase() {
  constexpr uint64_t rootOffset = 176;
  constexpr uint64_t moduleOffset = 240;
  constexpr uint64_t packageOffset = 304;
  constexpr uint64_t classOffset = 400;
  constexpr uint64_t methodOffset = 496;
  constexpr uint64_t relationOffset = 592;
  constexpr uint64_t stringOffset = 704;
  constexpr uint64_t stringSize = 38;
  constexpr uint64_t indexOffset = 744;
  std::vector<uint8_t> bytes(indexOffset + 120, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_PROFILE_READ);
  put32(bytes, 20, OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, rootOffset);
  put64(bytes, 48, rootOffset);
  put64(bytes, 56, 2);
  put64(bytes, 64, packageOffset);
  put64(bytes, 72, 3);
  put64(bytes, 80, relationOffset);
  put64(bytes, 96, stringOffset);
  put64(bytes, 104, stringSize);
  put64(bytes, 112, indexOffset);
  put64(bytes, 120, 5);
  put64(bytes, 128, relationOffset);
  put64(bytes, 144, relationOffset);
  put64(bytes, 160, relationOffset);
  put64(bytes, 168, 7);

  put32(bytes, rootOffset, designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, 0));
  put32(bytes, rootOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, rootOffset + 8, 1);
  put64(bytes, rootOffset + 24, moduleOffset);
  put64(bytes, rootOffset + 40, stringOffset);

  put32(bytes, moduleOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, vpiModule));
  put32(bytes, moduleOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, moduleOffset + 8, 2);
  put64(bytes, moduleOffset + 16, rootOffset);
  put64(bytes, moduleOffset + 24, packageOffset);
  put64(bytes, moduleOffset + 40, stringOffset + 6);

  put32(bytes, packageOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, vpiPackage));
  put64(bytes, packageOffset + 8, 3);
  put64(bytes, packageOffset + 16, moduleOffset);
  put64(bytes, packageOffset + 24, classOffset);
  put64(bytes, packageOffset + 40, stringOffset + 10);

  put32(bytes, classOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, vpiClassDefn));
  put64(bytes, classOffset + 8, 4);
  put64(bytes, classOffset + 16, moduleOffset);
  put64(bytes, classOffset + 24, methodOffset);
  put64(bytes, classOffset + 40, stringOffset + 16);

  put32(bytes, methodOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_FUNCTION, vpiFunction));
  put32(bytes, methodOffset + 4, OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR);
  put64(bytes, methodOffset + 8, 5);
  put64(bytes, methodOffset + 16, moduleOffset);
  put64(bytes, methodOffset + 40, stringOffset + 23);

  put32(bytes, relationOffset, 0);
  put32(bytes, relationOffset + 4, 1);
  put16(bytes, relationOffset + 12, vpiModule);
  put16(bytes, relationOffset + 14, designRelationSource(0, 0, true));

  put32(bytes, relationOffset + 16, 0);
  put32(bytes, relationOffset + 16 + 4, uint32_t{1} << 30);
  put16(bytes, relationOffset + 16 + 12, vpiInstance);
  put16(bytes, relationOffset + 16 + 14, designRelationSource(0, 0, true));

  put32(bytes, relationOffset + 32, 0);
  put32(bytes, relationOffset + 32 + 4, (uint32_t{1} << 30) | uint32_t{1});
  put16(bytes, relationOffset + 32 + 12, vpiClassDefn);
  put16(bytes, relationOffset + 32 + 14,
        designRelationSource(1, vpiPackage, true));

  put32(bytes, relationOffset + 48, 1);
  put32(bytes, relationOffset + 48 + 4, uint32_t{1} << 30);
  put16(bytes, relationOffset + 48 + 12, vpiScope);
  put16(bytes, relationOffset + 48 + 14, designRelationSource(1, vpiClassDefn));

  put32(bytes, relationOffset + 64, 1);
  put32(bytes, relationOffset + 64 + 4, (uint32_t{1} << 30) | uint32_t{2});
  put16(bytes, relationOffset + 64 + 12, vpiInternalScope);
  put16(bytes, relationOffset + 64 + 14,
        designRelationSource(1, vpiClassDefn, true));

  put32(bytes, relationOffset + 80, 1);
  put32(bytes, relationOffset + 80 + 4, (uint32_t{1} << 30) | uint32_t{2});
  put16(bytes, relationOffset + 80 + 12, vpiMethods);
  put16(bytes, relationOffset + 80 + 14,
        designRelationSource(1, vpiClassDefn, true));

  put32(bytes, relationOffset + 96, 2);
  put32(bytes, relationOffset + 96 + 4, (uint32_t{1} << 30) | uint32_t{1});
  put16(bytes, relationOffset + 96 + 12, vpiScope);
  put16(bytes, relationOffset + 96 + 14, designRelationSource(1, vpiFunction));

  std::memcpy(bytes.data() + stringOffset,
              "$root\0top\0pkg::\0pkg::C\0pkg::C::method\0", stringSize);
  struct Entry {
    uint64_t hash, name, record;
  };
  std::array<Entry, 5> index{{
      {nameHash("$root"), stringOffset, rootOffset},
      {nameHash("top"), stringOffset + 6, moduleOffset},
      {nameHash("pkg::"), stringOffset + 10, packageOffset},
      {nameHash("pkg::C"), stringOffset + 16, classOffset},
      {nameHash("pkg::C::method"), stringOffset + 23, methodOffset},
  }};
  std::sort(index.begin(), index.end(),
            [](const Entry &left, const Entry &right) {
              return std::tie(left.hash, left.name) <
                     std::tie(right.hash, right.name);
            });
  for (size_t entry = 0; entry != index.size(); ++entry) {
    put64(bytes, indexOffset + entry * 24, index[entry].hash);
    put64(bytes, indexOffset + entry * 24 + 8, index[entry].name);
    put64(bytes, indexOffset + entry * 24 + 16, index[entry].record);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeCodeUnitDatabase() {
  constexpr uint64_t scopeOffset = 176;
  constexpr uint64_t processOffset = 240;
  constexpr uint64_t functionOffset = 336;
  constexpr uint64_t stringOffset = 432;
  constexpr uint64_t stringSize = 20;
  constexpr uint64_t indexOffset = 456;
  std::vector<uint8_t> bytes(indexOffset + 72, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16, OBELISK_RT_DESIGN_PROFILE_READ);
  put32(bytes, 20, OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, scopeOffset);
  put64(bytes, 48, scopeOffset);
  put64(bytes, 56, 1);
  put64(bytes, 64, processOffset);
  put64(bytes, 72, 2);
  put64(bytes, 80, stringOffset);
  put64(bytes, 88, 0);
  put64(bytes, 96, stringOffset);
  put64(bytes, 104, stringSize);
  put64(bytes, 112, indexOffset);
  put64(bytes, 120, 3);
  put64(bytes, 128, stringOffset);
  put64(bytes, 144, stringOffset);
  put64(bytes, 160, stringOffset);

  put32(bytes, scopeOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, vpiModule));
  put32(bytes, scopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, scopeOffset + 8, 1);
  put64(bytes, scopeOffset + 24, processOffset);
  put64(bytes, scopeOffset + 40, stringOffset);

  put32(bytes, processOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_PROCESS, vpiInitial));
  put64(bytes, processOffset + 8, 71);
  put64(bytes, processOffset + 16, scopeOffset);
  put64(bytes, processOffset + 24, functionOffset);
  put64(bytes, processOffset + 40, stringOffset + 4);

  put32(bytes, functionOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_FUNCTION, vpiFunction));
  put64(bytes, functionOffset + 8, 72);
  put64(bytes, functionOffset + 16, scopeOffset);
  put64(bytes, functionOffset + 40, stringOffset + 13);

  std::memcpy(bytes.data() + stringOffset, "top\0top.proc\0top.fn\0",
              stringSize);
  struct Entry {
    uint64_t hash;
    uint64_t name;
    uint64_t record;
  };
  std::array<Entry, 3> index{{
      {nameHash("top"), stringOffset, scopeOffset},
      {nameHash("top.proc"), stringOffset + 4, processOffset},
      {nameHash("top.fn"), stringOffset + 13, functionOffset},
  }};
  std::sort(index.begin(), index.end(),
            [](const Entry &left, const Entry &right) {
              return std::tie(left.hash, left.name) <
                     std::tie(right.hash, right.name);
            });
  for (size_t entry = 0; entry != index.size(); ++entry) {
    put64(bytes, indexOffset + entry * 24, index[entry].hash);
    put64(bytes, indexOffset + entry * 24 + 8, index[entry].name);
    put64(bytes, indexOffset + entry * 24 + 16, index[entry].record);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeStatementDatabase() {
  constexpr uint64_t scopeOffset = 176;
  constexpr uint64_t childScopeOffset = 240;
  constexpr uint64_t processOffset = 304;
  constexpr uint64_t statementOffset = 400;
  constexpr uint64_t siteOffset = 520;
  constexpr uint64_t relationOffset = 568;
  constexpr uint64_t stringOffset = 616;
  constexpr uint64_t stringSize = 42;
  constexpr uint64_t indexOffset = 664;
  std::vector<uint8_t> bytes(indexOffset + 72, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_PROFILE_READ);
  put32(bytes, 20, OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, scopeOffset);
  put64(bytes, 48, scopeOffset);
  put64(bytes, 56, 2);
  put64(bytes, 64, processOffset);
  put64(bytes, 72, 1);
  put64(bytes, 80, statementOffset);
  put64(bytes, 88, 0);
  put64(bytes, 96, stringOffset);
  put64(bytes, 104, stringSize);
  put64(bytes, 112, indexOffset);
  put64(bytes, 120, 3);
  put64(bytes, 128, statementOffset);
  put64(bytes, 136, 3);
  put64(bytes, 144, siteOffset);
  put64(bytes, 152, 3);
  put64(bytes, 160, relationOffset);
  put64(bytes, 168, 3);

  put32(bytes, scopeOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, vpiModule));
  put32(bytes, scopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, scopeOffset + 8, 1);
  put64(bytes, scopeOffset + 24, childScopeOffset);
  put64(bytes, scopeOffset + 40, stringOffset);

  put32(bytes, childScopeOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, vpiModule));
  put32(bytes, childScopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, childScopeOffset + 8, 2);
  put64(bytes, childScopeOffset + 16, scopeOffset);
  put64(bytes, childScopeOffset + 24, processOffset);
  put64(bytes, childScopeOffset + 40, stringOffset + 4);

  put32(bytes, processOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_PROCESS, vpiInitial));
  put64(bytes, processOffset + 8, 10);
  put64(bytes, processOffset + 16, childScopeOffset);
  put64(bytes, processOffset + 40, stringOffset + 14);

  auto statement = [&](size_t index, uint64_t id, uint32_t parent,
                       uint16_t kind, uint16_t flags, uint32_t line,
                       uint32_t column, uint32_t name) {
    size_t offset = statementOffset + index * 40;
    put64(bytes, offset, id);
    put32(bytes, offset + 8, 0);
    put32(bytes, offset + 12, 1);
    put32(bytes, offset + 16, parent);
    put32(bytes, offset + 20, 29);
    put32(bytes, offset + 24, name);
    put32(bytes, offset + 28, line);
    put32(bytes, offset + 32, column);
    put16(bytes, offset + 36, kind);
    put16(bytes, offset + 38, flags);
  };
  statement(0, 100, UINT32_MAX, vpiNamedBegin,
            OBELISK_RT_DESIGN_STATEMENT_SCOPE, 8, 1, 37);
  statement(1, 200, 0, vpiFor, 0, 9, 3, 0);
  statement(2, 300, 1, vpiNullStmt, 0, 10, 5, 0);
  auto site = [&](size_t index, uint64_t id, uint32_t statement,
                  uint16_t phase) {
    size_t offset = siteOffset + index * 16;
    put64(bytes, offset, id);
    put32(bytes, offset + 8, statement);
    put16(bytes, offset + 12, phase);
  };
  site(0, 1000, 0, 0);
  site(1, 1100, 1, 1);
  site(2, 1200, 1, 2);

  auto relation = [&](size_t index, uint32_t source, uint32_t target,
                      uint32_t ordinal, uint16_t selector,
                      uint16_t sourceKindAndTable) {
    size_t offset = relationOffset + index * 16;
    put32(bytes, offset, source);
    put32(bytes, offset + 4, (uint32_t{2} << 30) | target);
    put32(bytes, offset + 8, ordinal);
    put16(bytes, offset + 12, selector);
    put16(bytes, offset + 14, sourceKindAndTable);
  };
  relation(0, 0, 0, 0, vpiStmt, designRelationSource(1, vpiInitial));
  relation(1, 0, 1, 0, vpiStmt, designRelationSource(2, vpiNamedBegin, true));
  relation(2, 1, 2, 0, vpiStmt, designRelationSource(2, vpiFor));

  std::memcpy(bytes.data() + stringOffset,
              "top\0top.child\0top.child.proc\0test.sv\0body\0", stringSize);
  struct Entry {
    uint64_t hash, name, record;
  };
  std::array<Entry, 3> index{{
      {nameHash("top"), stringOffset, scopeOffset},
      {nameHash("top.child"), stringOffset + 4, childScopeOffset},
      {nameHash("top.child.proc"), stringOffset + 14, processOffset},
  }};
  std::sort(index.begin(), index.end(),
            [](const Entry &left, const Entry &right) {
              return std::tie(left.hash, left.name) <
                     std::tie(right.hash, right.name);
            });
  for (size_t entry = 0; entry != index.size(); ++entry) {
    put64(bytes, indexOffset + entry * 24, index[entry].hash);
    put64(bytes, indexOffset + entry * 24 + 8, index[entry].name);
    put64(bytes, indexOffset + entry * 24 + 16, index[entry].record);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeNestedModuleRelationDatabase() {
  constexpr uint64_t scopeOffset = 176;
  constexpr uint64_t childScopeOffset = 240;
  constexpr uint64_t leafScopeOffset = 304;
  constexpr uint64_t relationOffset = 368;
  constexpr uint64_t stringOffset = 400;
  constexpr uint64_t stringSize = 29;
  constexpr uint64_t indexOffset = 432;
  std::vector<uint8_t> bytes(indexOffset + 72, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 16, OBELISK_RT_DESIGN_PROFILE_READ);
  put32(bytes, 20, OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, scopeOffset);
  put64(bytes, 48, scopeOffset);
  put64(bytes, 56, 3);
  put64(bytes, 64, relationOffset);
  put64(bytes, 80, relationOffset);
  put64(bytes, 96, stringOffset);
  put64(bytes, 104, stringSize);
  put64(bytes, 112, indexOffset);
  put64(bytes, 120, 3);
  put64(bytes, 128, relationOffset);
  put64(bytes, 144, relationOffset);
  put64(bytes, 160, relationOffset);
  put64(bytes, 168, 2);

  auto scope = [&](uint64_t offset, uint64_t id, uint64_t parent,
                   uint64_t firstChild, uint64_t name) {
    put32(bytes, offset,
          designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, vpiModule));
    put32(bytes, offset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
    put64(bytes, offset + 8, id);
    put64(bytes, offset + 16, parent);
    put64(bytes, offset + 24, firstChild);
    put64(bytes, offset + 40, name);
  };
  scope(scopeOffset, 1, 0, childScopeOffset, stringOffset);
  scope(childScopeOffset, 2, scopeOffset, leafScopeOffset, stringOffset + 4);
  scope(leafScopeOffset, 3, childScopeOffset, 0, stringOffset + 14);

  auto relation = [&](size_t index, bool iterate, uint32_t targetScope) {
    size_t offset = relationOffset + index * 16;
    put32(bytes, offset, 1);
    put32(bytes, offset + 4, targetScope);
    put32(bytes, offset + 8, 0);
    put16(bytes, offset + 12, vpiModule);
    put16(bytes, offset + 14, designRelationSource(0, vpiModule, iterate));
  };
  relation(0, false, 0);
  relation(1, true, 2);

  std::memcpy(bytes.data() + stringOffset, "top\0top.child\0top.child.leaf\0",
              stringSize);
  struct Entry {
    uint64_t hash, name, record;
  };
  std::array<Entry, 3> index{{
      {nameHash("top"), stringOffset, scopeOffset},
      {nameHash("top.child"), stringOffset + 4, childScopeOffset},
      {nameHash("top.child.leaf"), stringOffset + 14, leafScopeOffset},
  }};
  std::sort(index.begin(), index.end(),
            [](const Entry &left, const Entry &right) {
              return std::tie(left.hash, left.name) <
                     std::tie(right.hash, right.name);
            });
  for (size_t entry = 0; entry != index.size(); ++entry) {
    put64(bytes, indexOffset + entry * 24, index[entry].hash);
    put64(bytes, indexOffset + entry * 24 + 8, index[entry].name);
    put64(bytes, indexOffset + entry * 24 + 16, index[entry].record);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeAggregateDatabase() {
  constexpr uint64_t scopeOffset = 176;
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t typeOffset = 336;
  constexpr uint64_t fieldTypeOffset = typeOffset + 80;
  constexpr uint64_t scalarTypeOffset = fieldTypeOffset + 80;
  constexpr uint64_t stringOffset = scalarTypeOffset + 80;
  constexpr uint64_t stringSize = 33;
  constexpr uint64_t indexOffset = 616;
  std::vector<uint8_t> bytes(indexOffset + 48, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16,
        OBELISK_RT_DESIGN_PROFILE_READ | OBELISK_RT_DESIGN_PROFILE_WRITE);
  put32(bytes, 20, OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  put64(bytes, 24, bytes.size());
  put64(bytes, 40, scopeOffset);
  put64(bytes, 48, scopeOffset);
  put64(bytes, 56, 1);
  put64(bytes, 64, objectOffset);
  put64(bytes, 72, 1);
  put64(bytes, 80, typeOffset);
  put64(bytes, 88, 3);
  put64(bytes, 96, stringOffset);
  put64(bytes, 104, stringSize);
  put64(bytes, 112, indexOffset);
  put64(bytes, 120, 2);
  put64(bytes, 128, stringOffset);
  put64(bytes, 144, stringOffset);
  put64(bytes, 160, stringOffset);

  put32(bytes, scopeOffset, OBELISK_RT_DESIGN_RECORD_SCOPE);
  put32(bytes, scopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, scopeOffset + 8, 1);
  put64(bytes, scopeOffset + 24, objectOffset);
  put64(bytes, scopeOffset + 40, stringOffset);

  put32(bytes, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STORAGE, vpiReg));
  put32(bytes, objectOffset + 4,
        OBELISK_RT_DESIGN_CAP_READ | OBELISK_RT_DESIGN_CAP_WRITE);
  put64(bytes, objectOffset + 8, 7);
  put64(bytes, objectOffset + 16, scopeOffset);
  put64(bytes, objectOffset + 40, stringOffset + 4);
  put64(bytes, objectOffset + 48, typeOffset);
  put64(bytes, objectOffset + 56, 65);
  put64(bytes, objectOffset + 64, 0);
  put64(bytes, objectOffset + 72, 0);
  put64(bytes, objectOffset + 80, 0);

  put32(bytes, typeOffset, OBELISK_RT_DESIGN_RECORD_TYPE);
  put32(bytes, typeOffset + 4,
        OBELISK_RT_DESIGN_TYPE_STRUCT |
            ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_PACKED)
             << 8));
  put64(bytes, typeOffset + 8, 65);
  put64(bytes, typeOffset + 16, 64);
  put64(bytes, typeOffset + 40, fieldTypeOffset);
  put64(bytes, typeOffset + 48, 1);
  put64(bytes, typeOffset + 72, stringOffset + 14);

  put32(bytes, fieldTypeOffset, OBELISK_RT_DESIGN_RECORD_TYPE);
  put32(bytes, fieldTypeOffset + 4,
        OBELISK_RT_DESIGN_TYPE_FIELD |
            (OBELISK_RT_DESIGN_TYPE_FOUR_STATE << 8));
  put64(bytes, fieldTypeOffset + 8, 65);
  put64(bytes, fieldTypeOffset + 16, 64);
  put64(bytes, fieldTypeOffset + 32, scalarTypeOffset);
  put64(bytes, fieldTypeOffset + 56, 0);
  put64(bytes, fieldTypeOffset + 64, 0);
  put64(bytes, fieldTypeOffset + 72, stringOffset + 21);

  put32(bytes, scalarTypeOffset, OBELISK_RT_DESIGN_RECORD_TYPE);
  put32(bytes, scalarTypeOffset + 4,
        OBELISK_RT_DESIGN_TYPE_SCALAR |
            ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_PACKED)
             << 8));
  put64(bytes, scalarTypeOffset + 8, 65);
  put64(bytes, scalarTypeOffset + 16, 64);
  put64(bytes, scalarTypeOffset + 72, stringOffset + 27);

  std::memcpy(bytes.data() + stringOffset,
              "top\0top.value\0record\0value\0logic\0", stringSize);
  struct Entry {
    uint64_t hash;
    uint64_t name;
    uint64_t record;
  };
  std::array<Entry, 2> index{{
      {nameHash("top"), stringOffset, scopeOffset},
      {nameHash("top.value"), stringOffset + 4, objectOffset},
  }};
  std::sort(index.begin(), index.end(),
            [](const Entry &left, const Entry &right) {
              return std::tie(left.hash, left.name) <
                     std::tie(right.hash, right.name);
            });
  for (size_t entry = 0; entry != index.size(); ++entry) {
    put64(bytes, indexOffset + entry * 24, index[entry].hash);
    put64(bytes, indexOffset + entry * 24 + 8, index[entry].name);
    put64(bytes, indexOffset + entry * 24 + 16, index[entry].record);
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

enum class VPIShapeType {
  BasicScalar,
  BasicVector,
  PackedArrayOneBit,
  UnpackedArrayOfScalar,
  UnpackedArrayOfVector,
  PackedStructOneBit,
  PackedUnionOneBit,
  UnpackedStruct,
  UnpackedUnion,
  ShortReal,
  Real,
};

std::vector<uint8_t> makeVPIShapeDatabase(VPIShapeType shape,
                                          uint32_t exactVpiType) {
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t rootTypeOffset = 336;
  constexpr uint64_t nestedTypeOffset = rootTypeOffset + 80;
  constexpr uint64_t leafTypeOffset = nestedTypeOffset + 80;
  constexpr uint64_t stringOffset = leafTypeOffset + 80;
  const uint32_t physicalKind =
      exactVpiType == vpiNet || exactVpiType == vpiNetBit ||
              exactVpiType == vpiNetArray || exactVpiType == vpiRealNet ||
              exactVpiType == vpiShortRealNet
          ? OBELISK_RT_DESIGN_RECORD_NET
          : OBELISK_RT_DESIGN_RECORD_STORAGE;
  if (shape == VPIShapeType::BasicScalar ||
      shape == VPIShapeType::BasicVector || shape == VPIShapeType::ShortReal ||
      shape == VPIShapeType::Real) {
    std::vector<uint8_t> bytes = makeDatabase();
    const uint64_t width = shape == VPIShapeType::BasicScalar   ? 1
                           : shape == VPIShapeType::BasicVector ? 8
                           : shape == VPIShapeType::ShortReal   ? 32
                                                                : 64;
    const uint32_t flags =
        shape == VPIShapeType::ShortReal || shape == VPIShapeType::Real
            ? 0
            : OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_PACKED;
    put32(bytes, objectOffset, designRecordKind(physicalKind, exactVpiType));
    put64(bytes, objectOffset + 56, width);
    put64(bytes, objectOffset + 64, width - 1);
    put32(bytes, rootTypeOffset + 4,
          OBELISK_RT_DESIGN_TYPE_SCALAR | (flags << 8));
    put64(bytes, rootTypeOffset + 8, width);
    put64(bytes, rootTypeOffset + 16, width - 1);
    put64(bytes, 32, imageChecksum(bytes));
    return bytes;
  }

  std::vector<uint8_t> bytes = makeAggregateDatabase();
  put32(bytes, objectOffset, designRecordKind(physicalKind, exactVpiType));

  auto type = [&](uint64_t offset, uint32_t kind, uint32_t flags,
                  uint64_t width, uint64_t element, uint64_t firstChild,
                  uint64_t childCount, uint64_t ordinalOrTag,
                  uint64_t packedOffset, uint64_t name) {
    std::fill(bytes.begin() + offset, bytes.begin() + offset + 80, 0);
    put32(bytes, offset, OBELISK_RT_DESIGN_RECORD_TYPE);
    put32(bytes, offset + 4, kind | (flags << 8));
    put64(bytes, offset + 8, width);
    put64(bytes, offset + 16, width - 1);
    put64(bytes, offset + 24, 0);
    put64(bytes, offset + 32, element);
    put64(bytes, offset + 40, firstChild);
    put64(bytes, offset + 48, childCount);
    put64(bytes, offset + 56, ordinalOrTag);
    put64(bytes, offset + 64, packedOffset);
    put64(bytes, offset + 72, name);
  };
  auto setObjectWidth = [&](uint64_t width) {
    put64(bytes, objectOffset + 56, width);
    put64(bytes, objectOffset + 64, width - 1);
    put64(bytes, objectOffset + 72, 0);
  };

  const uint32_t fourState = OBELISK_RT_DESIGN_TYPE_FOUR_STATE;
  const uint32_t packed = OBELISK_RT_DESIGN_TYPE_PACKED;
  switch (shape) {
  case VPIShapeType::BasicScalar:
  case VPIShapeType::BasicVector:
  case VPIShapeType::ShortReal:
  case VPIShapeType::Real:
    break;
  case VPIShapeType::PackedArrayOneBit:
    type(rootTypeOffset, OBELISK_RT_DESIGN_TYPE_ARRAY, fourState | packed, 1,
         nestedTypeOffset, 0, 0, 0, 0, stringOffset + 14);
    type(nestedTypeOffset, OBELISK_RT_DESIGN_TYPE_ARRAY, fourState | packed, 1,
         leafTypeOffset, 0, 0, 0, 0, stringOffset + 21);
    type(leafTypeOffset, OBELISK_RT_DESIGN_TYPE_SCALAR, fourState | packed, 1,
         0, 0, 0, 0, 0, stringOffset + 27);
    put64(bytes, 88, 3);
    setObjectWidth(1);
    break;
  case VPIShapeType::UnpackedArrayOfScalar:
  case VPIShapeType::UnpackedArrayOfVector: {
    const uint64_t width = shape == VPIShapeType::UnpackedArrayOfScalar ? 1 : 8;
    type(rootTypeOffset, OBELISK_RT_DESIGN_TYPE_ARRAY, fourState, width,
         nestedTypeOffset, 0, 0, 0, 0, stringOffset + 14);
    type(nestedTypeOffset, OBELISK_RT_DESIGN_TYPE_ARRAY, fourState, width,
         leafTypeOffset, 0, 0, 0, 0, stringOffset + 21);
    put64(bytes, rootTypeOffset + 16, 0);
    put64(bytes, nestedTypeOffset + 16, 0);
    type(leafTypeOffset, OBELISK_RT_DESIGN_TYPE_SCALAR, fourState | packed,
         width, 0, 0, 0, 0, 0, stringOffset + 27);
    put64(bytes, 88, 3);
    setObjectWidth(width);
    break;
  }
  case VPIShapeType::PackedStructOneBit:
  case VPIShapeType::PackedUnionOneBit:
  case VPIShapeType::UnpackedStruct:
  case VPIShapeType::UnpackedUnion: {
    const bool isUnion = shape == VPIShapeType::PackedUnionOneBit ||
                         shape == VPIShapeType::UnpackedUnion;
    const bool isPacked = shape == VPIShapeType::PackedStructOneBit ||
                          shape == VPIShapeType::PackedUnionOneBit;
    type(rootTypeOffset,
         isUnion ? OBELISK_RT_DESIGN_TYPE_UNION : OBELISK_RT_DESIGN_TYPE_STRUCT,
         fourState | (isPacked ? packed : 0), 1, 0, nestedTypeOffset, 1, 0, 0,
         stringOffset + 14);
    type(nestedTypeOffset, OBELISK_RT_DESIGN_TYPE_FIELD, fourState, 1,
         leafTypeOffset, 0, 0, 0, 0, stringOffset + 21);
    type(leafTypeOffset, OBELISK_RT_DESIGN_TYPE_SCALAR, fourState | packed, 1,
         0, 0, 0, 0, 0, stringOffset + 27);
    put64(bytes, 88, 3);
    setObjectWidth(1);
    break;
  }
  }
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t>
makeVPIIndexedDatabase(int64_t outerLeft = 0, int64_t outerRight = 1,
                       int64_t packedLeft = 7, int64_t packedRight = 4,
                       uint32_t exactType = vpiRegArray,
                       uint32_t recordKind = OBELISK_RT_DESIGN_RECORD_STORAGE,
                       bool innerPacked = true, bool elementSigned = false,
                       bool outerPacked = false) {
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t outerTypeOffset = 336;
  constexpr uint64_t packedTypeOffset = outerTypeOffset + 80;
  constexpr uint64_t scalarTypeOffset = packedTypeOffset + 80;
  constexpr uint64_t stringOffset = scalarTypeOffset + 80;
  std::vector<uint8_t> bytes = makeAggregateDatabase();
  const uint32_t fourState = OBELISK_RT_DESIGN_TYPE_FOUR_STATE;
  const uint32_t packed = OBELISK_RT_DESIGN_TYPE_PACKED;
  const uint32_t signedFlag = OBELISK_RT_DESIGN_TYPE_SIGNED;
  const uint64_t outerExtent = static_cast<uint64_t>(
      outerLeft >= outerRight ? outerLeft - outerRight + 1
                              : outerRight - outerLeft + 1);
  const uint64_t packedExtent = static_cast<uint64_t>(
      packedLeft >= packedRight ? packedLeft - packedRight + 1
                                : packedRight - packedLeft + 1);
  const uint64_t width = outerExtent * packedExtent;
  auto type = [&](uint64_t offset, uint32_t kind, uint32_t flags,
                  uint64_t width, int64_t left, int64_t right, uint64_t element,
                  uint64_t name) {
    std::fill(bytes.begin() + offset, bytes.begin() + offset + 80, 0);
    put32(bytes, offset, OBELISK_RT_DESIGN_RECORD_TYPE);
    put32(bytes, offset + 4, kind | (flags << 8));
    put64(bytes, offset + 8, width);
    put64(bytes, offset + 16, static_cast<uint64_t>(left));
    put64(bytes, offset + 24, static_cast<uint64_t>(right));
    put64(bytes, offset + 32, element);
    put64(bytes, offset + 72, name);
  };

  put32(bytes, objectOffset, designRecordKind(recordKind, exactType));
  put64(bytes, objectOffset + 48, outerTypeOffset);
  put64(bytes, objectOffset + 56, width);
  put64(bytes, objectOffset + 64, static_cast<uint64_t>(outerLeft));
  put64(bytes, objectOffset + 72, static_cast<uint64_t>(outerRight));
  type(outerTypeOffset, OBELISK_RT_DESIGN_TYPE_ARRAY,
       fourState | (outerPacked ? packed : 0), width, outerLeft, outerRight,
       packedTypeOffset, stringOffset + 14);
  type(packedTypeOffset, OBELISK_RT_DESIGN_TYPE_ARRAY,
       fourState | (innerPacked ? packed : 0) |
           (elementSigned ? signedFlag : 0),
       packedExtent, packedLeft, packedRight, scalarTypeOffset,
       stringOffset + 21);
  type(scalarTypeOffset, OBELISK_RT_DESIGN_TYPE_SCALAR,
       fourState | packed | (elementSigned ? signedFlag : 0), 1, 0, 0, 0,
       stringOffset + 27);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeVPITypedArrayDatabase(
    uint32_t publicElementTypespec, uint32_t semanticElementKind,
    uint64_t elementWidth, uint32_t semanticElementFlags = 0,
    uint32_t rootType = vpiRegArray,
    uint32_t recordKind = OBELISK_RT_DESIGN_RECORD_STORAGE,
    uint32_t elementCount = 3) {
  std::vector<uint8_t> bytes =
      makeVPIIndexedDatabase(0, static_cast<int64_t>(elementCount - 1),
                             static_cast<int64_t>(elementWidth - 1), 0,
                             rootType, recordKind, true, false);
  const uint32_t directoryOffset = static_cast<uint32_t>(bytes.size());
  const uint64_t typeOffset = directoryOffset + kSemanticDirectorySize;
  const uint64_t edgeOffset = typeOffset + 2 * 64;
  const uint64_t rootOffset = edgeOffset + 24;
  bytes.resize(rootOffset + 4, 0);

  put32(bytes, 12, directoryOffset);
  put64(bytes, 24, bytes.size());
  put64(bytes, directoryOffset, typeOffset);
  put64(bytes, directoryOffset + 8, 2);
  put64(bytes, directoryOffset + 16, edgeOffset);
  put64(bytes, directoryOffset + 24, 1);
  put64(bytes, directoryOffset + 32, rootOffset);
  put64(bytes, directoryOffset + 40, 1);

  put32(bytes, typeOffset,
        OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY |
            OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE |
            (semanticElementFlags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) |
            (vpiArrayTypespec
             << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
  put32(bytes, typeOffset + 4, 0);
  put32(bytes, typeOffset + 8, 1);
  put32(bytes, typeOffset + 12, UINT32_MAX);
  put32(bytes, typeOffset + 16, UINT32_MAX);
  put64(bytes, typeOffset + 32, 0);
  put64(bytes, typeOffset + 40, elementCount - 1);

  const uint64_t elementOffset = typeOffset + 64;
  put32(bytes, elementOffset,
        semanticElementKind | semanticElementFlags |
            (publicElementTypespec
             << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
  put32(bytes, elementOffset + 12, UINT32_MAX);
  put32(bytes, elementOffset + 16, UINT32_MAX);

  put32(bytes, edgeOffset, 1);
  put32(bytes, edgeOffset + 4, OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT);
  put32(bytes, rootOffset, 0);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

std::vector<uint8_t> makeVPIPortDatabase() {
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t typeOffset = 336;
  std::vector<uint8_t> bytes = makeDatabase();
  put32(bytes, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_PORT, vpiPort));
  put32(bytes, objectOffset + 4,
        OBELISK_RT_DESIGN_CAP_READ | OBELISK_RT_DESIGN_CAP_PORT_INPUT);
  put64(bytes, objectOffset + 56, 8);
  put64(bytes, objectOffset + 64, 7);
  put64(bytes, objectOffset + 72, 0);
  put32(bytes, typeOffset + 4,
        OBELISK_RT_DESIGN_TYPE_SCALAR |
            ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_PACKED)
             << 8));
  put64(bytes, typeOffset + 8, 8);
  put64(bytes, typeOffset + 16, 7);
  put64(bytes, typeOffset + 24, 0);
  put64(bytes, 32, imageChecksum(bytes));
  return bytes;
}

struct Fixture {
  std::vector<uint8_t> bytecode = makeBytecode();
  std::vector<uint8_t> database = makeDatabase();
  obelisk_rt_execution_descriptor_v1 execution{};
  obelisk_rt_design_bytecode_entry_v1 entry{};
  std::array<uint32_t, 1> continuations{{0}};
  obelisk_rt_frame_layout_v1 layout{};
  obelisk_rt_process_descriptor_v1 descriptor{};

  Fixture() {
    execution = {OBELISK_RT_VERSION,
                 OBELISK_RT_EXECUTION_HAS_BYTECODE |
                     OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                     OBELISK_RT_EXECUTION_VPI_READ |
                     OBELISK_RT_EXECUTION_VPI_WRITE,
                 0,
                 bytecode.data(),
                 bytecode.size(),
                 database.data(),
                 database.size(),
                 65,
                 imageChecksum(bytecode)};
    entry = {&execution, 0, 0};
    layout = {OBELISK_RT_VERSION,
              0,
              32,
              8,
              nullptr,
              0,
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = frameChecksum(layout);
    descriptor = {{OBELISK_RT_DESCRIPTOR_PROCESS, 0, 9},
                  OBELISK_RT_VERSION,
                  0,
                  OBELISK_RT_TIER_MASK_BYTECODE,
                  0,
                  &layout,
                  nullptr,
                  nullptr,
                  nullptr,
                  nullptr,
                  &execution,
                  &entry};
  }
};

TEST(DesignBytecode, ContextBoundProcessCreationReusesValidatedDesign) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create_for_context(
                context, &fixture.descriptor, &instance),
            OBELISK_RT_OK);
  ASSERT_NE(instance, nullptr);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);

  Fixture otherDesign;
  instance = reinterpret_cast<obelisk_rt_process_instance_v1 *>(uintptr_t{1});
  EXPECT_EQ(obelisk_rt_v1_process_instance_create_for_context(
                context, &otherDesign.descriptor, &instance),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(instance, nullptr);

  fixture.entry.reserved = 1;
  instance = reinterpret_cast<obelisk_rt_process_instance_v1 *>(uintptr_t{1});
  EXPECT_EQ(obelisk_rt_v1_process_instance_create_for_context(
                context, &fixture.descriptor, &instance),
            OBELISK_RT_INVALID_BYTECODE);
  EXPECT_EQ(instance, nullptr);
  fixture.entry.reserved = 0;

  fixture.entry.function = 1;
  instance = reinterpret_cast<obelisk_rt_process_instance_v1 *>(uintptr_t{1});
  EXPECT_EQ(obelisk_rt_v1_process_instance_create_for_context(
                context, &fixture.descriptor, &instance),
            OBELISK_RT_INVALID_BYTECODE);
  EXPECT_EQ(instance, nullptr);
  fixture.entry.function = 0;

  instance = reinterpret_cast<obelisk_rt_process_instance_v1 *>(uintptr_t{1});
  EXPECT_EQ(obelisk_rt_v1_process_instance_create_for_context(
                nullptr, &fixture.descriptor, &instance),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(instance, nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, FailInstructionAcceptsFatalStatus) {
  Fixture fixture;
  const size_t layoutOffset = get64(fixture.bytecode, 56);
  const size_t codeOffset = get64(fixture.bytecode, 72);
  const size_t constantOffset = get64(fixture.bytecode, 104);
  fixture.bytecode[layoutOffset] = OBELISK_RT_DBREG_STATUS;
  put32(fixture.bytecode, layoutOffset + 4, 64);
  put64(fixture.bytecode, layoutOffset + 16, 8);
  instruction(fixture.bytecode, codeOffset, 1, OBELISK_RT_DB_FAIL, 0, 0, 0);
  put64(fixture.bytecode, constantOffset, OBELISK_RT_FATAL);
  put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_FATAL);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

uint32_t designWriteObserverCalls = 0;
uint32_t designWriteDirectExecutions = 0;

obelisk_rt_status designWriteObserverEvaluator(obelisk_rt_context *,
                                               const uint64_t *, uint32_t,
                                               uint64_t *value,
                                               uint64_t *unknown,
                                               uint32_t limbs) {
  if (!value || !unknown || limbs != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++designWriteObserverCalls;
  value[0] = 1;
  unknown[0] = 0;
  return OBELISK_RT_OK;
}

obelisk_rt_status designWriteWaitRequirements(uint64_t *size,
                                              uint64_t *alignment) {
  if (!size || !alignment)
    return OBELISK_RT_INVALID_ARGUMENT;
  *size = 0;
  *alignment = 1;
  return OBELISK_RT_OK;
}

obelisk_rt_status
designWriteWaitExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                       OBELISK_RT_SUSPEND_OBSERVER,
                       1,
                       OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       176};
  return OBELISK_RT_OK;
}

void designWriteWaitDestroy(obelisk_rt_process_instance_v1 *) {}

obelisk_rt_status
designWriteDirectExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++designWriteDirectExecutions;
  if (instance->continuation == 0) {
    *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                         OBELISK_RT_SUSPEND_CHANGE,
                         1,
                         OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                         0,
                         48};
  } else {
    *instance->action = {
        OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  }
  return OBELISK_RT_OK;
}

void populateDesignWriteWait(void *frame) {
  std::memset(frame, 0, 176);
  auto *wait = static_cast<obelisk_rt_computed_wait_record_v1 *>(frame);
  *wait = {OBELISK_RT_VERSION,
           OBELISK_RT_SUSPEND_OBSERVER,
           OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
           1,
           1,
           0,
           1,
           1,
           96,
           128,
           128,
           144,
           160,
           0,
           176,
           0};
  auto *binding = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
      static_cast<uint8_t *>(frame) + wait->observers_offset);
  *binding = {7, 0, 0, 0, 1, 160, 0};
  auto *dependency = reinterpret_cast<obelisk_rt_computed_dependency_v1 *>(
      static_cast<uint8_t *>(frame) + wait->dependencies_offset);
  *dependency = {0, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, 65};
  auto *clause = reinterpret_cast<obelisk_rt_computed_clause_v1 *>(
      static_cast<uint8_t *>(frame) + wait->clauses_offset);
  *clause = {0, OBELISK_RT_OBSERVER_CONDITION_NONE, OBELISK_RT_WAIT_EDGE_CHANGE,
             0};
}

TEST(DesignBytecode, RejectsMalformedActivationBytecodeInventory) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  obelisk_rt_activation_descriptor_v1 activation{
      1, nullptr, 99, OBELISK_RT_ACTIVATION_HAS_BYTECODE};
  fixture.execution.activations = &activation;
  fixture.execution.activation_count = 1;

#ifdef OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  testing::internal::CaptureStderr();
#endif
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_INVALID_DESIGN);
#ifdef OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  EXPECT_NE(testing::internal::GetCapturedStderr().find(
                "activation bytecode function is out of range"),
            std::string::npos);
#endif
  EXPECT_EQ(context, nullptr);

  activation.bytecode_function = 0;
  activation.code_unit_id = 2;
#ifdef OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  testing::internal::CaptureStderr();
#endif
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_INVALID_DESIGN);
#ifdef OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  EXPECT_NE(testing::internal::GetCapturedStderr().find(
                "activation descriptor does not match bytecode function"),
            std::string::npos);
#endif
  EXPECT_EQ(context, nullptr);
}

TEST(DesignBytecode, ReportsMalformedImageReason) {
  Fixture fixture;
  fixture.bytecode[0] ^= 1;

#ifdef OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  testing::internal::CaptureStderr();
#endif
  obelisk_rt_context *context = nullptr;
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_INVALID_DESIGN);
#ifdef OBELISK_RT_BYTECODE_VALIDATION_DIAGNOSTICS
  const std::string diagnostic = testing::internal::GetCapturedStderr();
  EXPECT_NE(diagnostic.find("invalid bytecode header identity, size, or "
                            "checksum"),
            std::string::npos);
  EXPECT_NE(diagnostic.find("DesignBytecodeImage.cpp:"), std::string::npos);
#endif
  EXPECT_EQ(context, nullptr);
}

TEST(DesignBytecode, DesignWritePublishesToComputedObservers) {
  Fixture fixture;
  obelisk_rt_observer_descriptor_v1 observer{7,
                                             nullptr,
                                             0,
                                             1,
                                             0,
                                             OBELISK_RT_OBSERVER_NO_BYTECODE,
                                             designWriteObserverEvaluator,
                                             0};
  fixture.execution.observers = &observer;
  fixture.execution.observer_count = 1;

  obelisk_rt_frame_field_v1 field{
      OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0, 176, 8, 0};
  std::array<uint32_t, 2> continuations{{0, 1}};
  obelisk_rt_frame_layout_v1 layout{OBELISK_RT_VERSION,
                                    0,
                                    176,
                                    8,
                                    &field,
                                    1,
                                    static_cast<uint32_t>(continuations.size()),
                                    continuations.data(),
                                    0};
  layout.checksum = frameChecksum(layout);
  obelisk_rt_process_descriptor_v1 process{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 83},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_NATIVE,
      0,
      &layout,
      designWriteWaitRequirements,
      designWriteWaitExecute,
      designWriteWaitDestroy,
      nullptr,
      &fixture.execution,
      nullptr};

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&process, &instance),
            OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  ASSERT_EQ(frameSize, 176u);
  populateDesignWriteWait(frame);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  static constexpr std::string_view name = "top.value";
  obelisk_rt_design_cursor_v1 cursor{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>(name.data()), name.size(),
                &cursor),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> value{{1, 0}};
  std::array<uint64_t, 2> unknown{};
  context->signalValueSnapshots[0] = {1, false, false};
  context->signalValueSnapshots[64] = {1, false, false};
  designWriteObserverCalls = 0;
  ASSERT_EQ(obelisk_rt_v1_design_write(context, cursor, value.data(),
                                       unknown.data(), 65),
            OBELISK_RT_OK);
  EXPECT_EQ(designWriteObserverCalls, 1u);
  EXPECT_TRUE(context->signalValueSnapshots.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, DesignWritePublishesCanonicalStaticSignalIdentity) {
  Fixture fixture;
  obelisk_rt_frame_field_v1 field{
      OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0, 48, 8, 0};
  std::array<uint32_t, 2> continuations{{0, 1}};
  obelisk_rt_frame_layout_v1 layout{OBELISK_RT_VERSION,
                                    0,
                                    48,
                                    8,
                                    &field,
                                    1,
                                    static_cast<uint32_t>(continuations.size()),
                                    continuations.data(),
                                    0};
  layout.checksum = frameChecksum(layout);
  obelisk_rt_process_descriptor_v1 process{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 84},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_NATIVE,
      0,
      &layout,
      designWriteWaitRequirements,
      designWriteDirectExecute,
      designWriteWaitDestroy,
      nullptr,
      &fixture.execution,
      nullptr};

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 65),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&process, &instance),
            OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  ASSERT_EQ(frameSize, 48u);
  auto *wait = static_cast<obelisk_rt_wait_record_v1 *>(frame);
  auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0};
  *entry = {obelisk_rt_v1_native_state_static_handle(1),
            OBELISK_RT_WAIT_EDGE_CHANGE, 65};
  designWriteDirectExecutions = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(designWriteDirectExecutions, 1u);

  static constexpr std::string_view name = "top.value";
  obelisk_rt_design_cursor_v1 cursor{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>(name.data()), name.size(),
                &cursor),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> value{{1, 0}};
  std::array<uint64_t, 2> unknown{};
  ASSERT_EQ(obelisk_rt_v1_design_write(context, cursor, value.data(),
                                       unknown.data(), 65),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(designWriteDirectExecutions, 2u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, RejectsNonPackedObserverResultLayout) {
  auto create = [](std::vector<uint8_t> &bytecode,
                   obelisk_rt_observer_descriptor_v1 &observer,
                   obelisk_rt_execution_descriptor_v1 &execution,
                   uint8_t resultKind, obelisk_rt_context **context) {
    bytecode = makeObserverBytecode(resultKind);
    observer = {7, nullptr, 0, 256, 0, 0, nullptr, 0};
    execution = {};
    execution.version = OBELISK_RT_VERSION;
    execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE;
    execution.bytecode = bytecode.data();
    execution.bytecode_size = bytecode.size();
    execution.checksum = imageChecksum(bytecode);
    execution.observers = &observer;
    execution.observer_count = 1;
    return obelisk_rt_v1_context_create_for_design(&execution, context);
  };

  std::vector<uint8_t> bytecode;
  obelisk_rt_observer_descriptor_v1 observer{};
  obelisk_rt_execution_descriptor_v1 execution{};
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      create(bytecode, observer, execution, OBELISK_RT_DBREG_BITS, &context),
      OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);

  context = nullptr;
  EXPECT_EQ(
      create(bytecode, observer, execution, OBELISK_RT_DBREG_HANDLE, &context),
      OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
}

TEST(DesignBytecode, BytecodeComputedObserverWakesOnlyForAffectedSignal) {
  constexpr uint64_t taskID = 17;
  constexpr uint32_t resultWidth = 256;
  constexpr uint32_t dependencyWidth = 65;
  constexpr uint32_t previousLimbCount = resultWidth / 64;
  constexpr uint64_t observersOffset =
      sizeof(obelisk_rt_computed_wait_record_v1);
  constexpr uint64_t capturesOffset =
      observersOffset + sizeof(obelisk_rt_computed_observer_v1);
  constexpr uint64_t dependenciesOffset = capturesOffset;
  constexpr uint64_t clausesOffset =
      dependenciesOffset + sizeof(obelisk_rt_computed_dependency_v1);
  constexpr uint64_t previousOffset =
      clausesOffset + sizeof(obelisk_rt_computed_clause_v1);
  constexpr uint64_t waitSize =
      previousOffset + 2 * previousLimbCount * sizeof(uint64_t);

  Fixture fixture;
  fixture.bytecode = makeObserverBytecode(OBELISK_RT_DBREG_BITS);
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  obelisk_rt_observer_descriptor_v1 observer{7, nullptr, 0,       resultWidth,
                                             0, 0,       nullptr, 0};
  fixture.execution.observers = &observer;
  fixture.execution.observer_count = 1;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);

  ScheduledDesignTask task;
  task.id = taskID;
  task.started = true;
  task.suspendKind = OBELISK_RT_SUSPEND_OBSERVER;
  task.waitSize = waitSize;
  task.scratchOffset = waitSize;
  task.frame.resize(waitSize);

  auto *wait =
      reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(task.frame.data());
  *wait = {OBELISK_RT_VERSION,
           OBELISK_RT_SUSPEND_OBSERVER,
           OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
           1,
           1,
           0,
           1,
           previousLimbCount,
           observersOffset,
           capturesOffset,
           dependenciesOffset,
           clausesOffset,
           previousOffset,
           0,
           waitSize,
           0};
  auto *binding = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
      task.frame.data() + wait->observers_offset);
  *binding = {7, 0, 0, 0, 1, static_cast<uint32_t>(wait->previous_value_offset),
              0};
  auto *dependency = reinterpret_cast<obelisk_rt_computed_dependency_v1 *>(
      task.frame.data() + wait->dependencies_offset);
  *dependency = {0, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, dependencyWidth};
  auto *clause = reinterpret_cast<obelisk_rt_computed_clause_v1 *>(
      task.frame.data() + wait->clauses_offset);
  *clause = {0, OBELISK_RT_OBSERVER_CONDITION_NONE, OBELISK_RT_WAIT_EDGE_CHANGE,
             0};
  auto *previous = reinterpret_cast<uint64_t *>(task.frame.data() +
                                                wait->previous_value_offset);
  previous[0] = 1;
  ASSERT_TRUE(obelisk_rt_validate_computed_wait_record(&fixture.execution, wait,
                                                       waitSize));

  context->scheduledDesignTasks.push_back(std::move(task));
  context->scheduledDesignTaskIndices.emplace(taskID, 0);
  context->pendingDesignComputedWaiters.push_back(taskID);

  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    ASSERT_TRUE(obelisk_rt_evaluate_design_observers_unlocked(
        context, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, 128, 1));
    EXPECT_EQ(context->pendingDesignComputedWaiters,
              std::vector<uint64_t>({taskID}));
    EXPECT_FALSE(context->scheduledDesignTasks.front().signalTriggered);

    uint64_t selectionGeneration = context->schedulerSelectionGeneration;
    ASSERT_TRUE(obelisk_rt_evaluate_design_observers_unlocked(
        context, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, 0, dependencyWidth));
    EXPECT_TRUE(context->pendingDesignComputedWaiters.empty());
    EXPECT_TRUE(context->scheduledDesignTasks.front().signalTriggered);
    EXPECT_EQ(context->designPollCandidates.count(taskID), 1u);
    EXPECT_NE(context->schedulerSelectionGeneration, selectionGeneration);
  }
  obelisk_rt_v1_context_destroy(context);
}

uint64_t mixedTierObservedHandle = UINT64_MAX;
uint8_t mixedTierObservedValue = UINT8_MAX;

obelisk_rt_status mixedTierRequirements(uint64_t *size, uint64_t *alignment) {
  if (!size || !alignment)
    return OBELISK_RT_INVALID_ARGUMENT;
  *size = 0;
  *alignment = 1;
  return OBELISK_RT_OK;
}

obelisk_rt_status mixedTierExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->context || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::array<uint8_t, 9> dummy{}, value{};
  obelisk_rt_status status = obelisk_rt_v1_native_state_load_plane(
      instance->context, dummy.data(), 65, mixedTierObservedHandle, 65, 0, 0,
      value.data());
  if (status != OBELISK_RT_OK)
    return status;
  mixedTierObservedValue = value[0];
  instance->native_handle = instance;
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}

void mixedTierDestroy(obelisk_rt_process_instance_v1 *instance) {
  instance->native_handle = nullptr;
}

TEST(DesignBytecode, ExecutesArbitraryWidthLogicInSharedScratch) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  void *frame = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &size),
            OBELISK_RT_OK);
  ASSERT_EQ(size, 32u);
  std::array<uint64_t, 4> planes{};
  std::memcpy(planes.data(), frame, sizeof(planes));
  EXPECT_EQ(planes[0], UINT64_C(0xfedcba9876543210));
  EXPECT_EQ(planes[1], 1u);
  EXPECT_EQ(planes[2], UINT64_C(0xf0));
  EXPECT_EQ(planes[3], 1u);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ExecutesRegisteredImportedZeroTimeCall) {
  constexpr std::string_view symbol = "external_logic";
  uint32_t importID = obelisk_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(symbol.data()), symbol.size());
  ASSERT_NE(importID, 0u);
  Fixture fixture;
  fixture.bytecode = makeImportBytecode(importID);
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE;
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.design_database = nullptr;
  fixture.execution.design_database_size = 0;
  fixture.execution.state_bit_count = 8;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_TIER_UNAVAILABLE);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);

  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ImportObservation observation;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(context, importID,
                                                  importedLogic, &observation),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(observation.calls, 1u);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  ASSERT_EQ(frameSize, 32u);
  const auto *planes = static_cast<const uint64_t *>(frame);
  EXPECT_EQ(planes[0], UINT64_C(0xfedcba987654cdef));
  EXPECT_EQ(planes[1], 1u);
  EXPECT_EQ(planes[2], UINT64_C(0x30));
  EXPECT_EQ(planes[3], 1u);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ExecutesLegacyRandomSolveIntrinsic) {
  Fixture fixture;
  fixture.bytecode = makeRandomSolveBytecode(false, 0, 0);
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE;
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.design_database = nullptr;
  fixture.execution.design_database_size = 0;
  fixture.execution.state_bit_count = 8;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  void *frame = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &size),
            OBELISK_RT_OK);
  ASSERT_GE(size, 16u);
  std::array<uint64_t, 2> outputs{};
  std::memcpy(outputs.data(), frame, sizeof(outputs));
  EXPECT_EQ(outputs[0], 0u);
  EXPECT_EQ(outputs[1], 1u);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ExecutesStatefulRandomSolveIntrinsic) {
  obelisk_rt_random_state_v1 initial;
  obelisk_rt_v1_random_state_seed(&initial, 17, 9);
  obelisk_rt_random_state_v1 expected = initial;
  uint64_t expectedX = 0;
  uint64_t expectedY = 0;
  ASSERT_EQ(obelisk_rt_v1_random_state_bounded(&expected, 2, &expectedX),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_random_state_bounded(&expected, 2, &expectedY),
            OBELISK_RT_OK);

  Fixture fixture;
  fixture.bytecode =
      makeRandomSolveBytecode(true, initial.state, initial.increment);
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE;
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.design_database = nullptr;
  fixture.execution.design_database_size = 0;
  fixture.execution.state_bit_count = 8;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  void *frame = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &size),
            OBELISK_RT_OK);
  ASSERT_GE(size, 24u);
  std::array<uint64_t, 3> outputs{};
  std::memcpy(outputs.data(), frame, sizeof(outputs));
  EXPECT_EQ(outputs[0], expectedX | (expectedY << 1));
  EXPECT_EQ(outputs[1], 1u);
  EXPECT_EQ(outputs[2], expected.state);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ExecutesWideRandomSolveIntrinsic) {
  Fixture fixture;
  fixture.bytecode = makeRandomSolveWideBytecode();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE;
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.design_database = nullptr;
  fixture.execution.design_database_size = 0;
  fixture.execution.state_bit_count = 8;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  void *frame = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &size),
            OBELISK_RT_OK);
  ASSERT_GE(size, 32u);
  std::array<uint64_t, 4> outputs{};
  std::memcpy(outputs.data(), frame, sizeof(outputs));
  EXPECT_EQ(outputs[0], 5u);
  EXPECT_EQ(outputs[1], 1u);
  EXPECT_EQ(outputs[2], 1u);
  EXPECT_EQ(outputs[3], 73u);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ExecutesRandomCycleIntrinsic) {
  constexpr uint64_t key = UINT64_C(0x7b8124c9d6a53e10);
  constexpr uint64_t position = 37;
  constexpr uint64_t width = 11;
  uint64_t expectedPosition = 0;
  uint64_t expectedValue = 0;
  ASSERT_EQ(obelisk_rt_v1_random_cycle_next(key, position, width,
                                            &expectedPosition, &expectedValue),
            OBELISK_RT_OK);

  Fixture fixture;
  fixture.bytecode = makeRandomCycleBytecode(key, position, width);
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE;
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.design_database = nullptr;
  fixture.execution.design_database_size = 0;
  fixture.execution.state_bit_count = 8;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  void *frame = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &size),
            OBELISK_RT_OK);
  ASSERT_GE(size, 16u);
  std::array<uint64_t, 2> outputs{};
  std::memcpy(outputs.data(), frame, sizeof(outputs));
  EXPECT_EQ(outputs[0], expectedPosition);
  EXPECT_EQ(outputs[1], expectedValue);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, SchedulerCommitsDelayedNBAAndDeferredEvent) {
  Fixture fixture;
  fixture.bytecode = makeSchedulerBytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 0;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  obelisk_rt_design_cursor_v1 object{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>("top.value"), 9, &object),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> value{}, unknown{};
  ASSERT_EQ(obelisk_rt_v1_design_read(context, object, value.data(),
                                      unknown.data(), 65),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0] & UINT64_C(0xff), UINT64_C(0xa5));
  EXPECT_EQ(unknown[0] & UINT64_C(0xff), UINT64_C(0x04));
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ConstructsReservedPreponedEventHandle) {
  Fixture fixture;
  fixture.bytecode = makeSchedulerBytecode(
      /*stateHandle=*/0, OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT);
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 0;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, TriggeringCanonicalNullEventHasNoEffect) {
  Fixture fixture;
  fixture.bytecode =
      makeSchedulerBytecode(/*stateHandle=*/0, /*eventHandle=*/UINT64_MAX);
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 0;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_TRUE(context->events.empty());
  EXPECT_TRUE(context->scheduledDesignEvents.empty());
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ResolvesEncodedStaticStateHandlesByIdentity) {
  Fixture fixture;
  uint64_t stateHandle = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(stateHandle, UINT64_MAX);
  fixture.bytecode = makeSchedulerBytecode(stateHandle);
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 0;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  obelisk_rt_design_cursor_v1 object{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>("top.value"), 9, &object),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> value{}, unknown{};
  ASSERT_EQ(obelisk_rt_v1_design_read(context, object, value.data(),
                                      unknown.data(), 65),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0] & UINT64_C(0xff), UINT64_C(0xa5));
  EXPECT_EQ(unknown[0] & UINT64_C(0xff), UINT64_C(0x04));
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, CanonicalizesFlatHandlesToRegisteredStaticState) {
  Fixture fixture;
  fixture.bytecode = makeSchedulerBytecode(/*stateHandle=*/0);
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 0;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 8),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);

  ASSERT_EQ(context->scheduledNBAs.size(), 1u);
  EXPECT_TRUE(context->scheduledDesignNBAs.empty());
  EXPECT_EQ(context->scheduledNBAs.front().bitOffset,
            obelisk_rt_v1_native_state_static_handle(1));

  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ResolvesFourStateDriversFromInitialHighImpedance) {
  Fixture fixture;
  fixture.bytecode = makeDriverBytecode();
  fixture.database = makeDatabase();
  put32(fixture.database, 240,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_NET, vpiNet));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.state_bit_count = 130;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 0;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_design_cursor_v1 net{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>("top.value"), 9, &net),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> value{}, unknown{};
  ASSERT_EQ(
      obelisk_rt_v1_design_read(context, net, value.data(), unknown.data(), 65),
      OBELISK_RT_OK);
  EXPECT_EQ(value[0], UINT64_MAX);
  EXPECT_EQ(value[1], 1u);
  EXPECT_EQ(unknown[0], UINT64_MAX);
  EXPECT_EQ(unknown[1], 1u);

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_design_read(context, net, value.data(), unknown.data(), 65),
      OBELISK_RT_OK);
  EXPECT_EQ(value[0] & UINT64_C(0xff), UINT64_C(0xa5));
  EXPECT_EQ(unknown[0] & UINT64_C(0xff), UINT64_C(0x04));
  EXPECT_EQ(value[0] >> 8, UINT64_C(0x00ffffffffffffff));
  EXPECT_EQ(unknown[0] >> 8, UINT64_C(0x00ffffffffffffff));
  EXPECT_EQ(value[1], 1u);
  EXPECT_EQ(unknown[1], 1u);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ResolvesIEEEAmbiguousStrengthRangesBeforeFourState) {
  Fixture fixture;
  fixture.bytecode = makeStrengthDriverBytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 260;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  auto setState = [&](uint64_t offset, bool value, bool unknown) {
    uint64_t mask = UINT64_C(1) << (offset % 64);
    if (value)
      context->stateValue[offset / 64] |= mask;
    else
      context->stateValue[offset / 64] &= ~mask;
    if (unknown)
      context->stateUnknown[offset / 64] |= mask;
    else
      context->stateUnknown[offset / 64] &= ~mask;
  };
  auto resolve = [&] {
    ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 65, 196),
              OBELISK_RT_OK);
  };

  // IEEE 1800-2017 28.12.3: strong L plus strong 0 becomes known 0. An
  // implementation that prematurely collapses L to x produces the wrong x.
  setState(65, false, true);
  setState(130, true, true);
  setState(195, false, false);
  resolve();
  EXPECT_EQ(context->stateValue[0] & 1, 0u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);

  // The symmetric H case is the output of bufif1(1, x). A separate strong1
  // driver eliminates the weaker-or-equal ambiguous levels and yields 1.
  setState(65, true, true);
  setState(130, false, true);
  setState(195, true, false);
  resolve();
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);

  // Equal-strength opposite values span both polarities and resolve to x.
  setState(65, false, false);
  setState(130, true, true);
  setState(195, true, false);
  resolve();
  EXPECT_EQ(context->stateValue[0] & 1, 0u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 1u);

  // highz1 replaces a driven 1 with z and therefore contributes no drive.
  setState(65, true, false);
  setState(130, true, true);
  setState(195, true, true);
  resolve();
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 1u);

  obelisk_rt_v1_context_destroy(context);
}

// A generated native schedule owns the state planes its code reads. These
// stand in for that ownership so a resolution can be observed reaching them.
struct SchedulePlanState {
  std::array<obelisk_rt_process_instance_v1 *, 2> actors{};
};

obelisk_rt_status planBind(void *opaque, obelisk_rt_context *, uint32_t slot,
                           obelisk_rt_process_instance_v1 *instance) {
  auto *state = static_cast<SchedulePlanState *>(opaque);
  if (!state || slot >= state->actors.size())
    return OBELISK_RT_INVALID_ARGUMENT;
  state->actors[slot] = instance;
  return OBELISK_RT_OK;
}

obelisk_rt_status planRun(void *, obelisk_rt_context *) {
  return OBELISK_RT_OK;
}

obelisk_rt_status planSnapshot(void *, obelisk_rt_context *context,
                               obelisk_rt_aot_deopt_snapshot *snapshot) {
  return obelisk_rt_v1_scheduler_snapshot_aot(context, snapshot);
}

TEST(DesignBytecode, ResolvedNetsReachGeneratedSchedulePlanes) {
  // IEEE 1800-2017 10.3.3: the value a delayed continuous assignment finally
  // publishes has to be the one the design reads. A generated schedule reads
  // its own state planes, so publishing only into the canonical image leaves
  // the design seeing the net's previous value forever.
  Fixture fixture;
  fixture.bytecode = makeStrengthDriverBytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 260;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  SchedulePlanState state;
  std::vector<uint8_t> planValue((260 + 7) / 8, 0);
  std::vector<uint8_t> planUnknown((260 + 7) / 8, 0);
  obelisk_rt_native_schedule_plan plan{};
  plan.size = sizeof(plan);
  plan.graph_layout_checksum = fixture.execution.checksum;
  plan.mutable_state = &state;
  plan.mutable_state_size = sizeof(state);
  plan.actor_capacity = 2;
  plan.state_value = planValue.data();
  plan.state_unknown = planUnknown.data();
  plan.state_bit_count = 260;
  plan.bind = planBind;
  plan.run = planRun;
  plan.fallback_snapshot = planSnapshot;
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  auto setState = [&](uint64_t offset, bool value, bool unknown) {
    uint64_t mask = UINT64_C(1) << (offset % 64);
    if (value)
      context->stateValue[offset / 64] |= mask;
    else
      context->stateValue[offset / 64] &= ~mask;
    if (unknown)
      context->stateUnknown[offset / 64] |= mask;
    else
      context->stateUnknown[offset / 64] &= ~mask;
  };
  // A strong1 driver against two weaker ambiguous ones resolves to a known 1.
  setState(65, true, true);
  setState(130, false, true);
  setState(195, true, false);
  ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 65, 196), OBELISK_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
  EXPECT_EQ(planValue[0] & 1, 1u);
  EXPECT_EQ(planUnknown[0] & 1, 0u);

  // Equal-strength opposite values resolve to x, so both planes change back.
  setState(65, false, false);
  setState(130, true, true);
  setState(195, true, false);
  ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 65, 196), OBELISK_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 1, 0u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 1u);
  EXPECT_EQ(planValue[0] & 1, 0u);
  EXPECT_EQ(planUnknown[0] & 1, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ResolvesWiredNetTruthTablesWithStrengths) {
  for (uint8_t resolution : {uint8_t{3}, uint8_t{4}}) {
    SCOPED_TRACE(resolution == 3 ? "wand/triand" : "wor/trior");
    Fixture fixture;
    fixture.bytecode = makeStrengthDriverBytecode(resolution);
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.state_bit_count = 260;
    fixture.execution.checksum = imageChecksum(fixture.bytecode);

    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    auto setState = [&](uint64_t offset, bool value, bool unknown) {
      uint64_t mask = UINT64_C(1) << (offset % 64);
      if (value)
        context->stateValue[offset / 64] |= mask;
      else
        context->stateValue[offset / 64] &= ~mask;
      if (unknown)
        context->stateUnknown[offset / 64] |= mask;
      else
        context->stateUnknown[offset / 64] &= ~mask;
    };
    auto resolve = [&](bool expectedValue, bool expectedUnknown) {
      ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 65, 196),
                OBELISK_RT_OK);
      EXPECT_EQ((context->stateValue[0] & 1) != 0, expectedValue);
      EXPECT_EQ((context->stateUnknown[0] & 1) != 0, expectedUnknown);
    };

    // Equal-strength 0/1 conflict: wired AND selects 0, wired OR selects 1.
    setState(65, false, false);
    setState(130, true, true);
    setState(195, true, false);
    resolve(resolution == 4, false);

    // Table 6-3/6-4: 0 combined with x is 0 for wand and x for wor.
    setState(65, false, false);
    setState(130, true, true);
    setState(195, false, true);
    resolve(false, resolution == 4);

    // The symmetric 1/x row is x for wand and 1 for wor.
    setState(65, true, true);
    setState(130, true, false);
    setState(195, false, true);
    resolve(resolution == 4, resolution == 3);

    // An undriven wired net retains the ordinary high-impedance identity.
    setState(65, true, true);
    setState(130, true, true);
    setState(195, true, true);
    resolve(true, true);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(DesignBytecode, WiredResolutionPreservesStrongerDriveDominance) {
  // IEEE 1800-2017 28.12.1: wired logic applies only to equal-strength
  // conflicts. A strong 0 beats a weak 1 even on wor, and vice versa on wand.
  EXPECT_EQ(obelisk_rt_v1_strength_resolve_kind(uint16_t{1} << 1,
                                                uint16_t{1} << 10, 4),
            uint16_t{1} << 1);
  EXPECT_EQ(obelisk_rt_v1_strength_resolve_kind(uint16_t{1} << 4,
                                                uint16_t{1} << 13, 3),
            uint16_t{1} << 13);
}

TEST(DesignBytecode, ResolvesImplicitPullAndSupplyNetDrives) {
  // IEEE 1800-2017 6.6.5, 6.6.6, and 28.15: tri0/tri1 contribute an
  // implicit pull drive, while supply0/supply1 contribute a supply drive.
  for (uint8_t resolution : {uint8_t{5}, uint8_t{6}, uint8_t{7}, uint8_t{8}}) {
    SCOPED_TRACE(static_cast<unsigned>(resolution));
    Fixture fixture;
    fixture.bytecode = makeStrengthDriverBytecode(resolution);
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.state_bit_count = 260;
    fixture.execution.checksum = imageChecksum(fixture.bytecode);

    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    bool implicitValue = resolution == 6 || resolution == 8;
    EXPECT_EQ((context->stateValue[0] & 1) != 0, implicitValue);
    EXPECT_EQ(context->stateUnknown[0] & 1, 0u);

    auto setState = [&](uint64_t offset, bool value, bool unknown) {
      uint64_t mask = UINT64_C(1) << (offset % 64);
      if (value)
        context->stateValue[offset / 64] |= mask;
      else
        context->stateValue[offset / 64] &= ~mask;
      if (unknown)
        context->stateUnknown[offset / 64] |= mask;
      else
        context->stateUnknown[offset / 64] &= ~mask;
    };
    setState(65, true, true);
    setState(130, true, true);
    setState(195, !implicitValue, false);
    ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 65, 196),
              OBELISK_RT_OK);
    bool expected = resolution >= 7 ? implicitValue : !implicitValue;
    EXPECT_EQ((context->stateValue[0] & 1) != 0, expected);
    EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(DesignBytecode, PullNetConflictsWithEqualPullStrength) {
  for (uint8_t resolution : {uint8_t{5}, uint8_t{6}}) {
    Fixture fixture;
    fixture.bytecode = makeStrengthDriverBytecode(resolution);
    size_t state = get64(fixture.bytecode, 168);
    put32(fixture.bytecode, state + 96 + 4,
          driverFlags(5, 5) | resolutionFlags(resolution, true));
    put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.state_bit_count = 260;
    fixture.execution.checksum = imageChecksum(fixture.bytecode);

    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    auto setState = [&](uint64_t offset, bool value, bool unknown) {
      uint64_t mask = UINT64_C(1) << (offset % 64);
      if (value)
        context->stateValue[offset / 64] |= mask;
      else
        context->stateValue[offset / 64] &= ~mask;
      if (unknown)
        context->stateUnknown[offset / 64] |= mask;
      else
        context->stateUnknown[offset / 64] &= ~mask;
    };
    setState(65, true, true);
    setState(130, true, true);
    setState(195, resolution == 5, false);
    ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 65, 196),
              OBELISK_RT_OK);
    EXPECT_EQ(context->stateValue[0] & 1, 0u);
    EXPECT_EQ(context->stateUnknown[0] & 1, 1u);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(DesignBytecode, RetainsDefaultTriregChargeIndefinitely) {
  Fixture fixture;
  fixture.bytecode = makeStrengthDriverBytecode(9);
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 260;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  auto state = [&](uint64_t offset) {
    return std::pair((context->stateValue[offset / 64] &
                      (UINT64_C(1) << (offset % 64))) != 0,
                     (context->stateUnknown[offset / 64] &
                      (UINT64_C(1) << (offset % 64))) != 0);
  };
  auto setState = [&](uint64_t offset, bool value, bool unknown) {
    uint64_t mask = UINT64_C(1) << (offset % 64);
    if (value)
      context->stateValue[offset / 64] |= mask;
    else
      context->stateValue[offset / 64] &= ~mask;
    if (unknown)
      context->stateUnknown[offset / 64] |= mask;
    else
      context->stateUnknown[offset / 64] &= ~mask;
  };
  auto resolve = [&] {
    ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 65, 196),
              OBELISK_RT_OK);
  };

  // IEEE 1800-2017 6.7.1: trireg starts at x, not z.
  EXPECT_EQ(state(0), std::pair(false, true));
  setState(65, true, true);
  setState(130, true, true);

  // IEEE 1800-2017 6.6.4: a non-z driver enters the driven state, and all-z
  // drivers enter the capacitive state without propagating z.
  setState(195, true, false);
  resolve();
  EXPECT_EQ(state(0), std::pair(true, false));
  setState(195, true, true);
  resolve();
  EXPECT_EQ(state(0), std::pair(true, false));

  setState(195, false, false);
  resolve();
  EXPECT_EQ(state(0), std::pair(false, false));
  setState(195, true, true);
  resolve();
  EXPECT_EQ(state(0), std::pair(false, false));

  setState(195, false, true);
  resolve();
  EXPECT_EQ(state(0), std::pair(false, true));
  setState(195, true, true);
  resolve();
  EXPECT_EQ(state(0), std::pair(false, true));
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, RetainsEffectiveCollapsedTriregChargeAcrossAliases) {
  Fixture fixture;
  fixture.bytecode = makeConnectedDriverBytecode();
  size_t stateOffset = get64(fixture.bytecode, 168);
  size_t connectivity = get64(fixture.bytecode, 184);
  put32(fixture.bytecode, stateOffset + 4, 1);
  put32(fixture.bytecode, stateOffset + 32 + 4, 1u | resolutionFlags(9, false));
  put32(fixture.bytecode, stateOffset + 64 + 4,
        driverFlags(6, 6) | resolutionFlags(9, true));
  fixture.bytecode[connectivity + 24] = 0;
  fixture.bytecode[connectivity + 25] = 9;
  fixture.bytecode[connectivity + 26] = 6;
  put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 195;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  auto setDriver = [&](bool value, bool unknown) {
    uint64_t mask = UINT64_C(1) << (130 % 64);
    if (value)
      context->stateValue[130 / 64] |= mask;
    else
      context->stateValue[130 / 64] &= ~mask;
    if (unknown)
      context->stateUnknown[130 / 64] |= mask;
    else
      context->stateUnknown[130 / 64] &= ~mask;
    ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 130, 131),
              OBELISK_RT_OK);
  };
  auto expectAliases = [&](bool value, bool unknown) {
    EXPECT_EQ((context->stateValue[0] & 1) != 0, value);
    EXPECT_EQ((context->stateUnknown[0] & 1) != 0, unknown);
    EXPECT_EQ((context->stateValue[1] & 2) != 0, value);
    EXPECT_EQ((context->stateUnknown[1] & 2) != 0, unknown);
  };
  expectAliases(false, true);
  setDriver(true, false);
  expectAliases(true, false);
  setDriver(true, true);
  expectAliases(true, false);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, SharesCollapsedTriregChargeByDeclaredStrength) {
  Fixture fixture;
  fixture.bytecode = makeConnectedDriverBytecode();
  size_t stateOffset = get64(fixture.bytecode, 168);

  // IEEE 1800-2017 28.16 and 28.16.2: both aliases are trireg nets. Net zero
  // retains a small-strength zero and net one retains a large-strength one.
  // When the active driver is z, the large stored charge wins and the result
  // is shared atomically by the whole connected component.
  put32(fixture.bytecode, stateOffset + 4,
        1u | resolutionFlags(9, false) | (1u << 7));
  put32(fixture.bytecode, stateOffset + 32 + 4,
        1u | resolutionFlags(9, false) | (3u << 7));
  put32(fixture.bytecode, stateOffset + 64 + 4,
        driverFlags(6, 6) | resolutionFlags(9, true));
  size_t connectivity = get64(fixture.bytecode, 184);
  fixture.bytecode[connectivity + 24] = 9;
  fixture.bytecode[connectivity + 25] = 9;
  put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 195;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  auto setState = [&](uint64_t offset, bool value, bool unknown) {
    uint64_t mask = UINT64_C(1) << (offset % 64);
    if (value)
      context->stateValue[offset / 64] |= mask;
    else
      context->stateValue[offset / 64] &= ~mask;
    if (unknown)
      context->stateUnknown[offset / 64] |= mask;
    else
      context->stateUnknown[offset / 64] &= ~mask;
  };
  setState(0, false, false);
  setState(65, true, false);
  setState(130, true, true);
  ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 130, 131),
            OBELISK_RT_OK);
  EXPECT_EQ((context->stateValue[0] & 1) != 0, true);
  EXPECT_EQ((context->stateUnknown[0] & 1) != 0, false);
  EXPECT_EQ((context->stateValue[1] & 2) != 0, true);
  EXPECT_EQ((context->stateUnknown[1] & 2) != 0, false);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, PublishesSplitStrengthBanksAtomically) {
  Fixture fixture;
  fixture.bytecode = makeStrengthDriverBytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 260;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 0;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  auto setState = [&](uint64_t offset, bool value, bool unknown) {
    uint64_t mask = UINT64_C(1) << (offset % 64);
    if (value)
      context->stateValue[offset / 64] |= mask;
    else
      context->stateValue[offset / 64] &= ~mask;
    if (unknown)
      context->stateUnknown[offset / 64] |= mask;
    else
      context->stateUnknown[offset / 64] &= ~mask;
  };
  setState(0, false, false);
  setState(65, false, false);
  context->signalDiagnosticsEnabled = true;
  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } waitRecord{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0},
               {0, OBELISK_RT_WAIT_EDGE_CHANGE, 1}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &waitRecord.wait, subscriptions, latch));

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
  ASSERT_TRUE(latch);
  EXPECT_TRUE(latch->triggered);
  // Two physical driver-state publications and one resolved-net transition.
  // Resolving after the low bank as well would add a spurious fourth one.
  EXPECT_EQ(context->signalDiagnostics.publications, 3u);

  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ResolvesDriversAcrossLogicalNetAliases) {
  Fixture fixture;
  fixture.bytecode = makeConnectedDriverBytecode();
  fixture.database = makeDatabase();
  put32(fixture.database, 240,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_NET, vpiNet));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.state_bit_count = 195;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 0;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_design_cursor_v1 alias{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>("top.value"), 9, &alias),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> value{}, unknown{};
  ASSERT_EQ(obelisk_rt_v1_design_read(context, alias, value.data(),
                                      unknown.data(), 65),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0] & UINT64_C(0xff), UINT64_C(0xa5));
  EXPECT_EQ(unknown[0] & UINT64_C(0xff), UINT64_C(0x04));
  EXPECT_EQ(obelisk_rt_v1_design_write(context, alias, value.data(),
                                       unknown.data(), 65),
            OBELISK_RT_PERMISSION_DENIED);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, UsesDominatingWiredKindAcrossNetAliases) {
  for (bool rhsDominates : {false, true}) {
    SCOPED_TRACE(rhsDominates ? "wor dominates" : "wand dominates");
    Fixture fixture;
    fixture.bytecode = makeMixedWiredDriverBytecode(rhsDominates);
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.state_bit_count = 260;
    fixture.execution.checksum = imageChecksum(fixture.bytecode);
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);

    // The wor-side driver is 1 and the wand-side driver is 0. Both aliases
    // publish the result selected by the dominating port endpoint.
    context->stateValue[130 / 64] |= UINT64_C(1) << (130 % 64);
    context->stateValue[195 / 64] &= ~(UINT64_C(1) << (195 % 64));
    context->stateUnknown[130 / 64] &= ~(UINT64_C(1) << (130 % 64));
    context->stateUnknown[195 / 64] &= ~(UINT64_C(1) << (195 % 64));
    ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 130, 260),
              OBELISK_RT_OK);
    EXPECT_EQ((context->stateValue[0] & 1) != 0, rhsDominates);
    EXPECT_EQ((context->stateValue[1] & 2) != 0, rhsDominates);
    EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
    EXPECT_EQ(context->stateUnknown[1] & 2, 0u);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(DesignBytecode, AcceptsMultipleDominatingEndpointsOfOneWiredKind) {
  Fixture fixture;
  fixture.bytecode = makeMultiSinkWiredDriverBytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 260;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, AcceptsDisjointUWireDriverComponents) {
  Fixture fixture;
  fixture.bytecode = makeMixedUWireDriverBytecode(false);
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 195;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, AcceptsLegacyMixedUWireWithoutDominanceFlags) {
  Fixture fixture;
  fixture.bytecode = makeMixedUWireDriverBytecode(false);
  size_t connectivity = fixture.bytecode.size() - 32;
  fixture.bytecode[connectivity + 26] = 0;
  put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 195;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, UsesOnlyDominatingCollapsedNetImplicitDrive) {
  Fixture fixture;
  fixture.bytecode = makeConnectedDriverBytecode();
  size_t state = get64(fixture.bytecode, 168);
  size_t connectivity = get64(fixture.bytecode, 184);
  put32(fixture.bytecode, state + 4, 1u | resolutionFlags(5, false)); // tri0
  put32(fixture.bytecode, state + 32 + 4,
        1u | resolutionFlags(3, false)); // wand
  put32(fixture.bytecode, state + 64 + 4,
        driverFlags(6, 6) | resolutionFlags(3, true));
  fixture.bytecode[connectivity + 24] = 5;
  fixture.bytecode[connectivity + 25] = 3;
  fixture.bytecode[connectivity + 26] = 6;
  put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 195;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  // IEEE 1800-2017 23.3.3.7: the dominating wand supplies the collapsed
  // net type, so the dominated tri0 declaration contributes no pull0 drive.
  EXPECT_EQ(context->stateValue[0] & 1, 1u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 1u);
  EXPECT_EQ((context->stateValue[1] >> 1) & 1, 1u);
  EXPECT_EQ((context->stateUnknown[1] >> 1) & 1, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, SupplyDominanceDisablesCollapsedUWireDriverLimit) {
  Fixture fixture;
  fixture.bytecode = makeMixedUWireDriverBytecode(true);
  size_t state = get64(fixture.bytecode, 168);
  size_t connectivity = get64(fixture.bytecode, 184);
  put32(fixture.bytecode, state + 4, 1u | resolutionFlags(2, false));
  put32(fixture.bytecode, state + 32 + 4, 1u | resolutionFlags(7, false));
  put32(fixture.bytecode, state + 64 + 4,
        driverFlags(6, 6) | resolutionFlags(7, true));
  put32(fixture.bytecode, connectivity - 32 + 4,
        driverFlags(6, 6) | resolutionFlags(7, true));
  fixture.bytecode[connectivity + 24] = 2;
  fixture.bytecode[connectivity + 25] = 7;
  fixture.bytecode[connectivity + 26] = 6;
  put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.state_bit_count = 195;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  EXPECT_EQ(context->stateValue[0] & 1, 0u);
  EXPECT_EQ(context->stateUnknown[0] & 1, 0u);
  obelisk_rt_v1_context_destroy(context);
}

struct VPILifecycleProbe {
  int calls = 0;
  int lastReason = 0;
  p_cb_data registration = nullptr;
  p_cb_data invocation = nullptr;
  bool actionFieldsWereNull = false;
  bool mutateInvocation = false;
  bool removeSelf = false;
  bool removePeer = false;
  bool registerSameReason = false;
  vpiHandle self = nullptr;
  vpiHandle peer = nullptr;
  vpiHandle registered = nullptr;
};

PLI_INT32 lifecycleProbeCallback(p_cb_data data) {
  auto *probe = reinterpret_cast<VPILifecycleProbe *>(data->user_data);
  ++probe->calls;
  probe->lastReason = data->reason;
  probe->invocation = data;
  probe->actionFieldsWereNull = data->obj == nullptr && data->time == nullptr &&
                                data->value == nullptr && data->index == 0;
  if (probe->removePeer) {
    EXPECT_EQ(vpi_remove_cb(probe->peer), 1);
  }
  if (probe->removeSelf) {
    EXPECT_EQ(vpi_remove_cb(probe->self), 1);
  }
  if (probe->registerSameReason) {
    s_cb_data nested{};
    nested.reason = data->reason;
    nested.cb_rtn = lifecycleProbeCallback;
    nested.user_data = reinterpret_cast<PLI_BYTE8 *>(probe);
    probe->registered = vpi_register_cb(&nested);
  }
  if (probe->mutateInvocation) {
    data->reason = -1;
    data->cb_rtn = nullptr;
    data->user_data = nullptr;
  }
  return 73;
}

struct VPIRegistrationProbe {
  std::vector<int> reasons;
  VPILifecycleProbe start;
  VPILifecycleProbe end;
};

PLI_INT32 registerLaterLifecycleCallbacks(p_cb_data data) {
  auto *probe = reinterpret_cast<VPIRegistrationProbe *>(data->user_data);
  probe->reasons.push_back(data->reason);
  s_cb_data start{};
  start.reason = cbStartOfSimulation;
  start.cb_rtn = lifecycleProbeCallback;
  start.user_data = reinterpret_cast<PLI_BYTE8 *>(&probe->start);
  probe->start.self = vpi_register_cb(&start);
  s_cb_data end{};
  end.reason = cbEndOfSimulation;
  end.cb_rtn = lifecycleProbeCallback;
  end.user_data = reinterpret_cast<PLI_BYTE8 *>(&probe->end);
  probe->end.self = vpi_register_cb(&end);
  char rootName[] = "$root";
  EXPECT_NE(vpi_handle_by_name(rootName, nullptr), nullptr);
  return 0;
}

struct VPIDestroyProbe {
  obelisk_rt_context *context = nullptr;
  bool destroyReturned = false;
  bool vpiRemainedUsable = false;
};

PLI_INT32 destroyContextFromCallback(p_cb_data data) {
  auto *probe = reinterpret_cast<VPIDestroyProbe *>(data->user_data);
  obelisk_rt_v1_context_destroy(probe->context);
  probe->destroyReturned = true;
  s_vpi_vlog_info info{};
  probe->vpiRemainedUsable = vpi_get_vlog_info(&info) == 1;
  return 0;
}

PLI_INT32 shutdownVPIFromCallback(p_cb_data data) {
  auto *probe = reinterpret_cast<VPIDestroyProbe *>(data->user_data);
  obelisk_rt_v1_vpi_shutdown(probe->context);
  probe->destroyReturned = true;
  s_vpi_vlog_info info{};
  probe->vpiRemainedUsable = vpi_get_vlog_info(&info) == 1;
  return 0;
}

TEST(VPI, StartupRequiresAnObservableDesignAndOwnsOneContext) {
  EXPECT_EQ(obelisk_rt_v1_vpi_startup(nullptr, nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0),
            OBELISK_RT_PERMISSION_DENIED);
  obelisk_rt_v1_context_destroy(context);

  Fixture fixture;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 1),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_compile(context),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_start_simulation(context),
            OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_v1_vpi_end_simulation(context);
  obelisk_rt_v1_vpi_end_simulation(context);
  obelisk_rt_v1_vpi_shutdown(context);

  char rootName[] = "$root";
  EXPECT_EQ(vpi_handle_by_name(rootName, nullptr), nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ContextDestroyRevokesAndFreesStateAcrossThreads) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char rootName[] = "$root";
  ASSERT_NE(vpi_handle_by_name(rootName, nullptr), nullptr);
  s_cb_data callback{};
  callback.reason = cbEndOfSimulation;
  callback.cb_rtn = lifecycleProbeCallback;
  ASSERT_NE(vpi_register_cb(&callback), nullptr);

  obelisk_rt_status duplicateStartup = OBELISK_RT_OK;
  std::thread starter([&] {
    duplicateStartup = obelisk_rt_v1_vpi_startup(context, nullptr, 0);
  });
  starter.join();
  EXPECT_EQ(duplicateStartup, OBELISK_RT_INVALID_ARGUMENT);

  std::thread destroyer([&] { obelisk_rt_v1_context_destroy(context); });
  destroyer.join();

  // Cross-thread destruction atomically revokes the originating simulation
  // thread's binding. A fresh context can then activate VPI on this thread.
  EXPECT_EQ(vpi_handle_by_name(rootName, nullptr), nullptr);
  context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ContextCanOutliveItsVPIOwnerThread) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  std::array<obelisk_rt_status, 4> statuses{};
  std::thread owner([&] {
    statuses[0] =
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context);
    if (statuses[0] != OBELISK_RT_OK)
      return;
    statuses[1] = obelisk_rt_v1_vpi_startup(context, nullptr, 0);
    statuses[2] = obelisk_rt_v1_vpi_end_compile(context);
    statuses[3] = obelisk_rt_v1_vpi_start_simulation(context);
    char rootName[] = "$root";
    EXPECT_NE(vpi_handle_by_name(rootName, nullptr), nullptr);
  });
  owner.join();
  EXPECT_EQ(statuses,
            (std::array<obelisk_rt_status, 4>{OBELISK_RT_OK, OBELISK_RT_OK,
                                              OBELISK_RT_OK, OBELISK_RT_OK}));
  ASSERT_NE(context, nullptr);

  // The shared revocation slot remains valid after its thread-local owner has
  // exited, so destruction on this thread releases the complete VPI state.
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, OpaqueHandleTokenAllocationSaturatesWithoutWrapping) {
  const uintptr_t exhausted = std::numeric_limits<uintptr_t>::max();
  std::atomic<uintptr_t> next{exhausted - 1};
  uintptr_t token = 0;
  EXPECT_TRUE(obelisk::runtime::allocateVPIHandleToken(next, token));
  EXPECT_EQ(token, exhausted - 1);
  EXPECT_EQ(next.load(), exhausted);

  token = 17;
  EXPECT_FALSE(obelisk::runtime::allocateVPIHandleToken(next, token));
  EXPECT_EQ(token, 17u);
  EXPECT_EQ(next.load(), exhausted);
  EXPECT_FALSE(obelisk::runtime::allocateVPIHandleToken(next, token));
  EXPECT_EQ(next.load(), exhausted);
}

#if defined(OBELISK_VPI_CALLBACK_TEST_MODULE)
TEST(VPI, LoadedStartupModuleEnforcesRestrictedPhaseAndRollsBackFailure) {
  void *module =
      dlopen(OBELISK_VPI_CALLBACK_TEST_MODULE, RTLD_NOW | RTLD_LOCAL);
  ASSERT_NE(module, nullptr) << dlerror();
  using Reset = void (*)(int);
  using Query = int (*)(int);
  auto reset =
      reinterpret_cast<Reset>(dlsym(module, "obelisk_vpi_callback_test_reset"));
  auto query =
      reinterpret_cast<Query>(dlsym(module, "obelisk_vpi_callback_test_query"));
  ASSERT_NE(reset, nullptr);
  ASSERT_NE(query, nullptr);
  const char *modules[] = {OBELISK_VPI_CALLBACK_TEST_MODULE};

  Fixture passiveFixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&passiveFixture.execution,
                                                    &context),
            OBELISK_RT_OK);
  reset(0);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, modules, 1), OBELISK_RT_OK);
  EXPECT_EQ(query(0), 1);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_FALSE(context->vpiObservationDemand);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  EXPECT_FALSE(context->vpiObservationDemand);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  EXPECT_FALSE(context->vpiObservationDemand);
  obelisk_rt_v1_vpi_end_simulation(context);
  EXPECT_FALSE(context->vpiObservationDemand);
  EXPECT_EQ(query(1), 0);
  obelisk_rt_v1_context_destroy(context);

  Fixture activeFixture;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&activeFixture.execution,
                                                    &context),
            OBELISK_RT_OK);
  reset(1);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, modules, 1), OBELISK_RT_OK);
  EXPECT_EQ(query(0), 1);
  EXPECT_EQ(query(2), 1);
  EXPECT_EQ(query(3), 1);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_FALSE(context->vpiObservationDemand);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  EXPECT_EQ(query(1), 1);
  EXPECT_FALSE(context->vpiObservationDemand);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  EXPECT_FALSE(context->vpiObservationDemand);
  obelisk_rt_v1_vpi_end_simulation(context);
  EXPECT_FALSE(context->vpiObservationDemand);
  obelisk_rt_v1_context_destroy(context);

  Fixture failingFixture;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&failingFixture.execution,
                                                    &context),
            OBELISK_RT_OK);
  reset(2);
  EXPECT_EQ(obelisk_rt_v1_vpi_startup(context, modules, 1),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context->vpiState, nullptr);
  EXPECT_EQ(query(0), 1);
  EXPECT_EQ(query(1), 0);
  // A failed module transaction leaves neither active state nor callbacks.
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  EXPECT_EQ(query(1), 0);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  obelisk_rt_v1_vpi_end_simulation(context);
  obelisk_rt_v1_context_destroy(context);

  Fixture throwingFixture;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&throwingFixture.execution,
                                                    &context),
            OBELISK_RT_OK);
  reset(3);
  EXPECT_EQ(obelisk_rt_v1_vpi_startup(context, modules, 1),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context->vpiState, nullptr);
  EXPECT_EQ(query(0), 1);
  // Exception rollback revokes the TLS binding, so activation can be retried.
  reset(0);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, modules, 1), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);

  EXPECT_EQ(dlclose(module), 0);
}
#endif

TEST(VPI, LifecycleCallbackDefersContextDestruction) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);

  VPIDestroyProbe probe{context};
  s_cb_data callback{};
  callback.reason = cbStartOfSimulation;
  callback.cb_rtn = destroyContextFromCallback;
  callback.user_data = reinterpret_cast<PLI_BYTE8 *>(&probe);
  ASSERT_NE(vpi_register_cb(&callback), nullptr);
  EXPECT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  EXPECT_TRUE(probe.destroyReturned);
  EXPECT_TRUE(probe.vpiRemainedUsable);

  // The transaction destroys the context only after callback dispatch and the
  // lifecycle transition are finished, and revokes this thread's VPI binding.
  char rootName[] = "$root";
  EXPECT_EQ(vpi_handle_by_name(rootName, nullptr), nullptr);
}

TEST(VPI, LifecycleCallbackCannotDirectlyShutdownActiveVPI) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);

  VPIDestroyProbe probe{context};
  s_cb_data callback{};
  callback.reason = cbStartOfSimulation;
  callback.cb_rtn = shutdownVPIFromCallback;
  callback.user_data = reinterpret_cast<PLI_BYTE8 *>(&probe);
  ASSERT_NE(vpi_register_cb(&callback), nullptr);
  EXPECT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  EXPECT_TRUE(probe.destroyReturned);
  EXPECT_TRUE(probe.vpiRemainedUsable);
  EXPECT_NE(context->vpiState, nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, CopiesAndDispatchesLifecycleCallbackData) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);

  VPILifecycleProbe probe;
  probe.mutateInvocation = true;
  s_vpi_time ignoredTime{};
  s_vpi_value ignoredValue{};
  s_cb_data registration{};
  registration.reason = cbStartOfSimulation;
  registration.cb_rtn = lifecycleProbeCallback;
  registration.obj = reinterpret_cast<vpiHandle>(uintptr_t{1});
  registration.time = &ignoredTime;
  registration.value = &ignoredValue;
  registration.index = 91;
  registration.user_data = reinterpret_cast<PLI_BYTE8 *>(&probe);
  probe.registration = &registration;
  probe.self = vpi_register_cb(&registration);
  ASSERT_NE(probe.self, nullptr);
  EXPECT_EQ(vpi_get(vpiType, probe.self), vpiCallback);
  registration = {};

  s_cb_data copied{};
  vpi_get_cb_info(probe.self, &copied);
  EXPECT_EQ(copied.reason, cbStartOfSimulation);
  EXPECT_EQ(copied.cb_rtn, lifecycleProbeCallback);
  EXPECT_EQ(copied.user_data, reinterpret_cast<PLI_BYTE8 *>(&probe));
  EXPECT_EQ(copied.obj, nullptr);
  EXPECT_EQ(copied.time, nullptr);
  EXPECT_EQ(copied.value, nullptr);
  EXPECT_EQ(copied.index, 0);

  EXPECT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  EXPECT_EQ(probe.calls, 1);
  EXPECT_EQ(probe.lastReason, cbStartOfSimulation);
  EXPECT_NE(probe.invocation, probe.registration);
  EXPECT_TRUE(probe.actionFieldsWereNull);
  vpi_get_cb_info(probe.self, &copied);
  EXPECT_EQ(copied.reason, cbStartOfSimulation);
  EXPECT_EQ(copied.cb_rtn, lifecycleProbeCallback);
  EXPECT_EQ(copied.user_data, reinterpret_cast<PLI_BYTE8 *>(&probe));

  obelisk_rt_v1_vpi_end_simulation(context);
  EXPECT_EQ(probe.calls, 1);
  EXPECT_EQ(vpi_remove_cb(probe.self), 1);
  EXPECT_EQ(vpi_remove_cb(probe.self), 0);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, LifecycleCallbacksAreColdAndCanRegisterLaterPhases) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_FALSE(context->vpiObservationDemand);

  VPIRegistrationProbe probe;
  s_cb_data callback{};
  callback.reason = cbEndOfCompile;
  callback.cb_rtn = registerLaterLifecycleCallbacks;
  callback.user_data = reinterpret_cast<PLI_BYTE8 *>(&probe);
  vpiHandle endCompile = vpi_register_cb(&callback);
  ASSERT_NE(endCompile, nullptr);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_FALSE(context->vpiObservationDemand);

  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(probe.reasons, std::vector<int>({cbEndOfCompile}));
  ASSERT_NE(probe.start.self, nullptr);
  ASSERT_NE(probe.end.self, nullptr);
  EXPECT_TRUE(context->vpiObservationDemand);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  EXPECT_EQ(probe.start.calls, 1);
  EXPECT_EQ(probe.start.lastReason, cbStartOfSimulation);
  EXPECT_TRUE(probe.start.actionFieldsWereNull);
  obelisk_rt_v1_vpi_end_simulation(context);
  EXPECT_EQ(probe.end.calls, 1);
  EXPECT_EQ(probe.end.lastReason, cbEndOfSimulation);
  EXPECT_TRUE(probe.end.actionFieldsWereNull);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  obelisk_rt_v1_vpi_shutdown(context);
  EXPECT_FALSE(context->vpiObservationDemand);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, CallbackRemovalAndDispatchMutationAreStable) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);

  VPILifecycleProbe duplicate;
  s_cb_data data{};
  EXPECT_EQ(vpi_register_cb(nullptr), nullptr);
  EXPECT_EQ(vpi_register_cb(&data), nullptr);
  data.reason = cbValueChange;
  data.cb_rtn = lifecycleProbeCallback;
  EXPECT_EQ(vpi_register_cb(&data), nullptr);
  data.reason = cbStartOfSimulation;
  data.user_data = reinterpret_cast<PLI_BYTE8 *>(&duplicate);
  vpiHandle first = vpi_register_cb(&data);
  vpiHandle second = vpi_register_cb(&data);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(vpi_get(vpiAllocScheme, second), vpiOtherScheme);
  EXPECT_EQ(vpi_compare_objects(first, second), 0);
  EXPECT_EQ(vpi_release_handle(first), 1);

  vpiHandle iterator = vpi_iterate(vpiCallback, nullptr);
  ASSERT_NE(iterator, nullptr);
  EXPECT_EQ(vpi_get(vpiIteratorType, iterator), vpiCallback);
  EXPECT_EQ(vpi_get(vpiAllocScheme, iterator), vpiOtherScheme);
  EXPECT_EQ(vpi_handle(vpiUse, iterator), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  vpiHandle firstEquivalent = nullptr;
  vpiHandle scanned = nullptr;
  while ((scanned = vpi_scan(iterator)) != nullptr) {
    if (!vpi_compare_objects(scanned, second))
      firstEquivalent = scanned;
  }
  ASSERT_NE(firstEquivalent, nullptr);

  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  // Duplicate registration is multiplicative, and releasing the first handle
  // does not remove its underlying registration.
  EXPECT_EQ(duplicate.calls, 2);
  EXPECT_EQ(vpi_remove_cb(firstEquivalent), 1);
  EXPECT_EQ(vpi_remove_cb(firstEquivalent), 0);
  EXPECT_EQ(vpi_remove_cb(second), 1);

  VPILifecycleProbe peerRemover;
  VPILifecycleProbe peer;
  VPILifecycleProbe selfRemover;
  VPILifecycleProbe nested;
  data.reason = cbEndOfSimulation;
  data.user_data = reinterpret_cast<PLI_BYTE8 *>(&peerRemover);
  peerRemover.self = vpi_register_cb(&data);
  data.user_data = reinterpret_cast<PLI_BYTE8 *>(&peer);
  peer.self = vpi_register_cb(&data);
  peerRemover.removePeer = true;
  peerRemover.peer = peer.self;
  data.user_data = reinterpret_cast<PLI_BYTE8 *>(&selfRemover);
  selfRemover.removeSelf = true;
  selfRemover.self = vpi_register_cb(&data);
  data.user_data = reinterpret_cast<PLI_BYTE8 *>(&nested);
  nested.registerSameReason = true;
  nested.self = vpi_register_cb(&data);
  ASSERT_NE(peerRemover.self, nullptr);
  ASSERT_NE(peer.self, nullptr);
  ASSERT_NE(selfRemover.self, nullptr);
  ASSERT_NE(nested.self, nullptr);

  obelisk_rt_v1_vpi_end_simulation(context);
  EXPECT_EQ(peerRemover.calls, 1);
  EXPECT_EQ(peer.calls, 0);
  EXPECT_EQ(selfRemover.calls, 1);
  EXPECT_EQ(nested.calls, 1);
  ASSERT_NE(nested.registered, nullptr);
  EXPECT_EQ(vpi_remove_cb(selfRemover.self), 0);
  char rootName[] = "$root";
  vpiHandle root = vpi_handle_by_name(rootName, nullptr);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(vpi_remove_cb(root), 0);
  EXPECT_EQ(vpi_release_handle(root), 1);
  EXPECT_EQ(vpi_remove_cb(peerRemover.self), 1);
  EXPECT_EQ(vpi_remove_cb(nested.self), 1);
  EXPECT_EQ(vpi_remove_cb(nested.registered), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ObservationDemandTracksFirstAndLastRunningCallback) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);

  VPILifecycleProbe firstProbe;
  VPILifecycleProbe secondProbe;
  s_cb_data callback{};
  callback.reason = cbEndOfSimulation;
  callback.cb_rtn = lifecycleProbeCallback;
  callback.user_data = reinterpret_cast<PLI_BYTE8 *>(&firstProbe);
  vpiHandle first = vpi_register_cb(&callback);
  callback.user_data = reinterpret_cast<PLI_BYTE8 *>(&secondProbe);
  vpiHandle second = vpi_register_cb(&callback);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_TRUE(context->vpiObservationDemand);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);

  EXPECT_EQ(vpi_remove_cb(first), 1);
  EXPECT_TRUE(context->vpiObservationDemand);
  EXPECT_EQ(vpi_remove_cb(second), 1);
  EXPECT_FALSE(context->vpiObservationDemand);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, TraversesReflectionAndTracksHandleState) {
  Fixture fixture;
  fixture.database = makeFixedPropertyDatabase(false);
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char rootName[] = "$root";
  char valueName[] = "value";
  char absoluteName[] = "$root.top.value";
  vpiHandle root = vpi_handle_by_name(rootName, nullptr);
  ASSERT_NE(root, nullptr);
  vpiHandle value = vpi_handle_by_name(valueName, root);
  ASSERT_NE(value, nullptr);
  vpiHandle absolute = vpi_handle_by_name(absoluteName, root);
  ASSERT_NE(absolute, nullptr);
  vpiHandle scope = vpi_handle(vpiScope, value);
  ASSERT_NE(scope, nullptr);

  EXPECT_EQ(vpi_get(vpiType, root), vpiModule);
  EXPECT_STREQ(vpi_get_str(vpiType, root), "vpiModule");
  EXPECT_EQ(vpi_get(vpiIsProtected, root), 0);
  EXPECT_EQ(vpi_get(vpiSize, root), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_STREQ(vpi_get_str(vpiName, root), "top");
  EXPECT_EQ(vpi_get64(vpiType, value), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(vpi_get(vpiType, value), vpiReg);
  EXPECT_STREQ(vpi_get_str(vpiType, value), "vpiReg");
  EXPECT_EQ(vpi_get(vpiIsProtected, value), 0);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_get64(vpiObjId, value), vpiUndefined);
  s_vpi_error_info propertyError{};
  EXPECT_EQ(vpi_chk_error(&propertyError), vpiNotice);
  EXPECT_STREQ(propertyError.message,
               "property is not defined for this VPI object");
  EXPECT_EQ(vpi_get(vpiType, value), vpiReg);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_get(vpiSize, value), 65);
  EXPECT_STREQ(vpi_get_str(vpiName, value), "value");
  EXPECT_STREQ(vpi_get_str(vpiFullName, value), "top.value");
  EXPECT_STREQ(vpi_get_str(vpiFile, value), "test.sv");
  EXPECT_EQ(vpi_get(vpiLineNo, value), 7);
  const char *definitionFile = vpi_get_str(vpiDefFile, root);
  EXPECT_STREQ(definitionFile, "logic");
  EXPECT_NE(definitionFile,
            reinterpret_cast<const char *>(fixture.database.data() + 430));
  EXPECT_EQ(vpi_get(vpiDefLineNo, root), 29);
  EXPECT_EQ(vpi_compare_objects(value, absolute), 1);
  EXPECT_EQ(vpi_compare_objects(root, scope), 1);

  vpiHandle iterator = vpi_iterate(vpiReg, root);
  ASSERT_NE(iterator, nullptr);
  EXPECT_EQ(vpi_get(vpiType, iterator), vpiIterator);
  EXPECT_STREQ(vpi_get_str(vpiType, iterator), "vpiIterator");
  EXPECT_EQ(vpi_get(vpiIsProtected, iterator), 0);
  vpiHandle iteratorUse = vpi_handle(vpiUse, iterator);
  ASSERT_NE(iteratorUse, nullptr);
  EXPECT_EQ(vpi_compare_objects(iteratorUse, root), 1);
  EXPECT_EQ(vpi_release_handle(iteratorUse), 1);
  EXPECT_EQ(vpi_handle(vpiScope, iterator), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_get_str(vpiName, iterator), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  vpiHandle scanned = vpi_scan(iterator);
  ASSERT_NE(scanned, nullptr);
  EXPECT_EQ(vpi_compare_objects(value, scanned), 1);
  EXPECT_EQ(vpi_scan(iterator), nullptr);

  int userData = 42;
  EXPECT_EQ(vpi_put_userdata(value, &userData), 1);
  EXPECT_EQ(vpi_get_userdata(value), &userData);
  EXPECT_EQ(vpi_get(999, value), vpiUndefined);
  s_vpi_error_info error{};
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.product, "Obelisk");
  EXPECT_STREQ(error.code, "OBELISK_VPI");
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);

  EXPECT_EQ(vpi_release_handle(scanned), 1);
  EXPECT_EQ(vpi_release_handle(scanned), 0);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_free_object(absolute), 1);
  EXPECT_EQ(vpi_release_handle(scope), 1);
  EXPECT_EQ(vpi_release_handle(value), 1);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, PriorityZeroReadPropertiesFollowGeneratedApplicability) {
  Fixture fixture;
  fixture.database = makeFixedPropertyDatabase(false);
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  const auto *compatibility =
      obelisk::reflection::findVPIProperty(0, vpiCompatibilityMode);
  ASSERT_NE(compatibility, nullptr);
  EXPECT_EQ(compatibility->valueKind,
            obelisk::reflection::VPIPropertyValueKind::Integer);
  EXPECT_EQ(vpi_get(vpiCompatibilityMode, nullptr), vpiMode1800v2009);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_get(vpiSize, nullptr), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_get(vpiCompatibilityMode, nullptr), vpiMode1800v2009);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  char rootName[] = "$root";
  char valueName[] = "value";
  vpiHandle root = vpi_handle_by_name(rootName, nullptr);
  vpiHandle value = vpi_handle_by_name(valueName, root);
  ASSERT_NE(root, nullptr);
  ASSERT_NE(value, nullptr);
  vpiHandle iterator = vpi_iterate(vpiReg, root);
  ASSERT_NE(iterator, nullptr);

  struct PropertyCase {
    vpiHandle handle;
    PLI_INT32 exactType;
    PLI_INT32 property;
    PLI_INT32 expected;
  };
  // This is the deliberately narrow Priority-0 runtime oracle. Every row is
  // first checked against the generated catalog, then exercised through the
  // public API so catalog growth cannot silently turn a handler into a stub.
  const PropertyCase cases[] = {
      {root, vpiModule, vpiAllocScheme, vpiOtherScheme},
      {root, vpiModule, vpiProtected, 0},
      {value, vpiReg, vpiAllocScheme, vpiOtherScheme},
      {value, vpiReg, vpiValid, 1},
      {iterator, vpiIterator, vpiAllocScheme, vpiOtherScheme},
      {iterator, vpiIterator, vpiIteratorType, vpiReg},
  };
  for (const PropertyCase &item : cases) {
    const auto *descriptor =
        obelisk::reflection::findVPIProperty(item.exactType, item.property);
    ASSERT_NE(descriptor, nullptr) << item.exactType << ":" << item.property;
    EXPECT_NE(descriptor->realization,
              obelisk::reflection::VPIPropertyRealization::FixedImage);
    EXPECT_EQ(vpi_get(item.property, item.handle), item.expected)
        << descriptor->apiName;
    EXPECT_EQ(vpi_chk_error(nullptr), 0) << descriptor->apiName;
  }

  EXPECT_EQ(vpi_release_handle(iterator), 1);
  EXPECT_EQ(vpi_release_handle(value), 1);
  EXPECT_EQ(vpi_get(vpiValid, value), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, LegacyProtectedPropertyDefaultsFalseAndFailsClosed) {
  Fixture fixture;
  fixture.database = makeProtectedScopeDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);

  char rootName[] = "$root";
  vpiHandle root = vpi_handle_by_name(rootName, nullptr);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(vpi_get(vpiIsProtected, root), 1);
  EXPECT_EQ(vpi_get(vpiProtected, root), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, FixedPropertiesEnforceProtectedObjectAccess) {
  Fixture fixture;
  fixture.database = makeFixedPropertyDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char rootName[] = "$root";
  char valueName[] = "top.value";
  vpiHandle root = vpi_handle_by_name(rootName, nullptr);
  vpiHandle value = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(root, nullptr);
  ASSERT_NE(value, nullptr); // Direct lookup may return a protected target.
  EXPECT_EQ(vpi_get(vpiType, value), vpiReg);
  EXPECT_STREQ(vpi_get_str(vpiType, value), "vpiReg");
  EXPECT_EQ(vpi_get(vpiIsProtected, value), 1);
  EXPECT_EQ(vpi_get(vpiSize, value), 65); // LRM protected-expression exception.
  EXPECT_EQ(vpi_get(vpiAllocScheme, value), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_get(vpiValid, value), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_get_str(vpiName, value), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_get64(vpiObjId, value), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_handle(vpiScope, value), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_iterate(vpiReg, value), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_handle_by_index(value, 0), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  PLI_INT32 indices[] = {0, 0};
  EXPECT_EQ(vpi_handle_by_multi_index(value, 2, indices), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);

  s_vpi_value read{};
  read.format = vpiIntVal;
  read.value.integer = 123;
  vpi_get_value(value, &read);
  EXPECT_EQ(read.value.integer, 123);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  s_vpi_arrayvalue arrayRead{};
  vpi_get_value_array(value, &arrayRead, 0, 1);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  s_vpi_delay delays{};
  vpi_get_delays(value, &delays);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  s_vpi_time objectTime{};
  objectTime.type = vpiSimTime;
  objectTime.low = 123;
  vpi_get_time(value, &objectTime);
  EXPECT_EQ(objectTime.low, 123u);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_handle_multi(vpiInterModPath, value, root), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  char relative[] = "child";
  EXPECT_EQ(vpi_handle_by_name(relative, value), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  char absolute[] = "$root";
  EXPECT_EQ(vpi_handle_by_name(absolute, value), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);

  vpiHandle iterator = vpi_iterate(vpiReg, root);
  ASSERT_NE(iterator, nullptr);
  vpiHandle traversed = vpi_scan(iterator);
  ASSERT_NE(traversed, nullptr); // Unprotected traversal may yield it.
  EXPECT_EQ(vpi_get(vpiIsProtected, traversed), 1);
  EXPECT_EQ(vpi_handle(vpiScope, traversed), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);

  EXPECT_EQ(vpi_release_handle(traversed), 1);
  EXPECT_EQ(vpi_release_handle(value), 1);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignDatabase, RejectsMalformedFixedPropertySections) {
  auto rejected = [](auto mutate) {
    Fixture fixture;
    fixture.database = makeFixedPropertyDatabase();
    // HeaderReserved is the low 32-bit word at offset 12.
    const uint32_t directoryOffset =
        static_cast<uint32_t>(fixture.database[12]) |
        (static_cast<uint32_t>(fixture.database[13]) << 8) |
        (static_cast<uint32_t>(fixture.database[14]) << 16) |
        (static_cast<uint32_t>(fixture.database[15]) << 24);
    const uint64_t properties = get64(fixture.database, directoryOffset + 112);
    mutate(fixture.database, directoryOffset, properties);
    put64(fixture.database, 32, imageChecksum(fixture.database));
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
              OBELISK_RT_INVALID_DESIGN);
  };
  rejected([](auto &bytes, uint32_t directory, uint64_t) {
    put64(bytes, directory + 112, bytes.size() + 1);
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put16(bytes, properties + 16 + 4, vpiDefFile); // duplicate key
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put16(bytes, properties + 6, 0); // string descriptor encoded as Boolean
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put16(bytes, properties + 2 * 16 + 4, vpiDefFile); // invalid for vpiReg
    put16(bytes, properties + 2 * 16 + 6, 3);
    put64(bytes, properties + 2 * 16 + 8, 430);
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put64(bytes, properties + 8, bytes.size()); // outside string pool
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put64(bytes, properties + 8, 431); // interior of "logic"
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put64(bytes, properties + 2 * 16 + 8, 0); // false must be absent
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put64(bytes, properties + 2 * 16 + 8, 2); // invalid Boolean payload
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put32(bytes, properties + 2 * 16, (uint32_t{1} << 30) | 1);
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put32(bytes, properties + 2 * 16, uint32_t{3} << 30); // invalid table
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put16(bytes, properties + 2 * 16 + 6, 4); // reserved flags
  });
  rejected([](auto &bytes, uint32_t, uint64_t properties) {
    put16(bytes, properties + 2 * 16 + 4, vpiSize); // Derived, not FixedImage
    put16(bytes, properties + 2 * 16 + 6, 1);
  });
}

TEST(DesignDatabase, RejectsProtectedStatementWithoutFixedProperty) {
  Fixture fixture;
  fixture.database = makeStatementDatabase();
  put16(fixture.database, 400 + 38,
        OBELISK_RT_DESIGN_STATEMENT_PROTECTED |
            OBELISK_RT_DESIGN_STATEMENT_SCOPE);
  const uint32_t directoryOffset =
      static_cast<uint32_t>(fixture.database.size());
  const uint64_t semanticRootOffset = directoryOffset + kSemanticDirectorySize;
  const uint64_t propertyOffset = semanticRootOffset + 4;
  fixture.database.resize(propertyOffset, 0);
  put32(fixture.database, 12, directoryOffset);
  put64(fixture.database, 24, fixture.database.size());
  put64(fixture.database, directoryOffset + 32, semanticRootOffset);
  put64(fixture.database, directoryOffset + 40, 1);
  put64(fixture.database, directoryOffset + 112, propertyOffset);
  put32(fixture.database, semanticRootOffset, UINT32_MAX);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);
}

TEST(VPI, LegacyStatementProtectionFlagIsFailClosed) {
  Fixture fixture;
  fixture.database = makeStatementDatabase();
  put16(fixture.database, 400 + 38,
        OBELISK_RT_DESIGN_STATEMENT_PROTECTED |
            OBELISK_RT_DESIGN_STATEMENT_SCOPE);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char processName[] = "top.child.proc";
  vpiHandle process = vpi_handle_by_name(processName, nullptr);
  ASSERT_NE(process, nullptr);
  vpiHandle statement = vpi_handle(vpiStmt, process);
  ASSERT_NE(statement, nullptr);
  EXPECT_EQ(vpi_get(vpiType, statement), vpiNamedBegin);
  EXPECT_EQ(vpi_get(vpiIsProtected, statement), 1);
  EXPECT_EQ(vpi_get_str(vpiName, statement), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_release_handle(statement), 1);
  EXPECT_EQ(vpi_release_handle(process), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ProtectedPackageStyleNameCannotBeHierarchicalLookupIntermediate) {
  Fixture fixture;
  fixture.database = makeFixedPropertyDatabase(false);
  constexpr uint64_t scopeOffset = 176;
  constexpr uint64_t stringOffset = 416;
  constexpr uint64_t indexOffset = 448;
  const uint32_t directoryOffset =
      static_cast<uint32_t>(fixture.database[12]) |
      (static_cast<uint32_t>(fixture.database[13]) << 8) |
      (static_cast<uint32_t>(fixture.database[14]) << 16) |
      (static_cast<uint32_t>(fixture.database[15]) << 24);
  const uint64_t propertyOffset =
      get64(fixture.database, directoryOffset + 112);
  fixture.database.resize(propertyOffset + 3 * 16, 0);
  put64(fixture.database, 24, fixture.database.size());
  put64(fixture.database, directoryOffset + 120, 3);
  put32(fixture.database, propertyOffset + 2 * 16, 0);
  put16(fixture.database, propertyOffset + 2 * 16 + 4, vpiIsProtected);
  put16(fixture.database, propertyOffset + 2 * 16 + 6, 0);
  put64(fixture.database, propertyOffset + 2 * 16 + 8, 1);

  // Exercise the package spelling convention directly: the intermediate
  // name retained in the immutable index includes the trailing `::`.
  std::memcpy(fixture.database.data() + stringOffset + 20, "pkg::\0", 6);
  put64(fixture.database, scopeOffset + 40, stringOffset + 20);
  struct Entry {
    uint64_t hash, name, record;
  };
  std::array<Entry, 2> entries;
  for (size_t index = 0; index != entries.size(); ++index) {
    entries[index] = {get64(fixture.database, indexOffset + index * 24),
                      get64(fixture.database, indexOffset + index * 24 + 8),
                      get64(fixture.database, indexOffset + index * 24 + 16)};
    if (entries[index].record == scopeOffset) {
      entries[index].hash = nameHash("pkg::");
      entries[index].name = stringOffset + 20;
    }
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry &left, const Entry &right) {
              return std::tie(left.hash, left.name) <
                     std::tie(right.hash, right.name);
            });
  for (size_t index = 0; index != entries.size(); ++index) {
    put64(fixture.database, indexOffset + index * 24, entries[index].hash);
    put64(fixture.database, indexOffset + index * 24 + 8, entries[index].name);
    put64(fixture.database, indexOffset + index * 24 + 16,
          entries[index].record);
  }
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char packageName[] = "pkg::";
  char methodName[] = "pkg::C";
  vpiHandle package = vpi_handle_by_name(packageName, nullptr);
  ASSERT_NE(package, nullptr); // A protected target itself may be returned.
  EXPECT_EQ(vpi_get(vpiIsProtected, package), 1);
  EXPECT_EQ(vpi_handle_by_name(methodName, nullptr), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_release_handle(package), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ReleasedAndExhaustedHandlesDoNotAliasNewObjects) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  std::unordered_set<uintptr_t> tokens;
  auto remember = [&](vpiHandle handle) {
    ASSERT_NE(handle, nullptr);
    EXPECT_TRUE(tokens.insert(reinterpret_cast<uintptr_t>(handle)).second);
  };
  char rootName[] = "$root";
  for (unsigned iteration = 0; iteration != 1024; ++iteration) {
    vpiHandle root = vpi_handle_by_name(rootName, nullptr);
    remember(root);
    vpiHandle iterator = vpi_iterate(vpiReg, root);
    remember(iterator);
    vpiHandle value = vpi_scan(iterator);
    remember(value);
    EXPECT_EQ(vpi_scan(iterator), nullptr);
    EXPECT_EQ(vpi_release_handle(value), 1);

    s_cb_data callback{};
    callback.reason = cbEndOfSimulation;
    callback.cb_rtn = lifecycleProbeCallback;
    vpiHandle registration = vpi_register_cb(&callback);
    remember(registration);
    EXPECT_EQ(vpi_remove_cb(registration), 1);
    EXPECT_EQ(vpi_release_handle(root), 1);
  }
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ScalarAndVectorQueriesFollowNetAndVariableTypeShape) {
  struct Case {
    VPIShapeType shape;
    uint32_t exactType;
    bool scalar;
    bool vector;
  };
  constexpr std::array<Case, 13> cases{{
      {VPIShapeType::BasicScalar, vpiReg, true, false},
      {VPIShapeType::BasicVector, vpiNet, false, true},
      {VPIShapeType::BasicVector, vpiNetBit, true, false},
      {VPIShapeType::BasicVector, vpiRegBit, true, false},
      {VPIShapeType::PackedArrayOneBit, vpiPackedArrayVar, false, true},
      {VPIShapeType::UnpackedArrayOfScalar, vpiRegArray, true, false},
      {VPIShapeType::UnpackedArrayOfVector, vpiNetArray, false, true},
      {VPIShapeType::PackedStructOneBit, vpiStructVar, false, true},
      {VPIShapeType::PackedUnionOneBit, vpiUnionVar, false, true},
      {VPIShapeType::UnpackedStruct, vpiStructVar, false, false},
      {VPIShapeType::UnpackedUnion, vpiUnionVar, false, false},
      {VPIShapeType::ShortReal, vpiShortRealVar, false, false},
      {VPIShapeType::Real, vpiRealVar, false, false},
  }};

  for (const Case &testCase : cases) {
    SCOPED_TRACE(static_cast<unsigned>(testCase.shape));
    Fixture fixture;
    fixture.database = makeVPIShapeDatabase(testCase.shape, testCase.exactType);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char valueName[] = "top.value";
    vpiHandle value = vpi_handle_by_name(valueName, nullptr);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(vpi_get(vpiType, value), testCase.exactType);
    EXPECT_EQ(vpi_get(vpiScalar, value), testCase.scalar);
    EXPECT_EQ(vpi_get(vpiVector, value), testCase.vector);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);

    EXPECT_EQ(vpi_release_handle(value), 1);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(VPI, PhysicalArrayAndPackedPropertiesFollowImageShape) {
  auto start = [](Fixture &fixture) {
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    EXPECT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    return context;
  };
  char name[] = "top.value";

  // One unpacked dimension is an IEEE 1364 memory.  Its selected variable is
  // also reported through the deprecated vpiArray compatibility property.
  Fixture oneDimension;
  oneDimension.database = makeVPITypedArrayDatabase(
      vpiLogicTypespec, OBELISK_RT_DESIGN_SEMANTIC_LOGIC, 1,
      OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE);
  obelisk_rt_context *context = start(oneDimension);
  ASSERT_NE(context, nullptr);
  vpiHandle root = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(vpi_get(vpiArray, root), 0);
  EXPECT_EQ(vpi_get(vpiIsMemory, root), 1);
  EXPECT_EQ(vpi_get(vpiArrayType, root), vpiStaticArray);
  vpiHandle member = vpi_handle_by_index(root, 0);
  ASSERT_NE(member, nullptr);
  EXPECT_EQ(vpi_get(vpiArray, member), 1);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_release_handle(member), 1);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);

  // A multidimensional array is not a legacy memory; selecting its leftmost
  // dimension leaves a one-dimensional vpiRegArray that is one.
  Fixture multidimensional;
  multidimensional.database = makeVPIIndexedDatabase(
      1, 0, 2, 0, vpiRegArray, OBELISK_RT_DESIGN_RECORD_STORAGE, false);
  context = start(multidimensional);
  ASSERT_NE(context, nullptr);
  root = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(vpi_get(vpiArray, root), 0);
  EXPECT_EQ(vpi_get(vpiIsMemory, root), 0);
  EXPECT_EQ(vpi_get(vpiArrayType, root), vpiStaticArray);
  vpiHandle row = vpi_handle_by_index(root, 1);
  ASSERT_NE(row, nullptr);
  EXPECT_EQ(vpi_get(vpiType, row), vpiRegArray);
  EXPECT_EQ(vpi_get(vpiArray, row), 1);
  EXPECT_EQ(vpi_get(vpiIsMemory, row), 1);
  EXPECT_EQ(vpi_get(vpiArrayType, row), vpiStaticArray);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_release_handle(row), 1);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);

  // In current IEEE 1800 mode vpiArray is membership provenance, not a
  // synonym for having an unpacked physical type.  Ordinary non-members with
  // no physical type therefore return false without an image lookup error.
  Fixture ordinaryModule;
  ordinaryModule.database = makeDatabase();
  context = start(ordinaryModule);
  ASSERT_NE(context, nullptr);
  char rootName[] = "$root";
  vpiHandle module = vpi_handle_by_name(rootName, nullptr);
  ASSERT_NE(module, nullptr);
  EXPECT_EQ(vpi_get(vpiType, module), vpiModule);
  EXPECT_EQ(vpi_get(vpiArray, module), 0);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_release_handle(module), 1);
  obelisk_rt_v1_context_destroy(context);

  struct PackedCase {
    VPIShapeType shape;
    uint32_t exactType;
    bool packed;
  };
  constexpr PackedCase packedCases[]{
      {VPIShapeType::PackedArrayOneBit, vpiPackedArrayVar, true},
      {VPIShapeType::PackedStructOneBit, vpiStructVar, true},
      {VPIShapeType::PackedUnionOneBit, vpiUnionVar, true},
      {VPIShapeType::BasicVector, vpiEnumVar, true},
      {VPIShapeType::UnpackedStruct, vpiStructVar, false},
      {VPIShapeType::UnpackedUnion, vpiUnionVar, false},
  };
  for (const PackedCase &testCase : packedCases) {
    SCOPED_TRACE(testCase.exactType);
    Fixture packedFixture;
    packedFixture.database =
        makeVPIShapeDatabase(testCase.shape, testCase.exactType);
    context = start(packedFixture);
    ASSERT_NE(context, nullptr);
    root = vpi_handle_by_name(name, nullptr);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(
        obelisk::reflection::findVPIProperty(testCase.exactType, vpiPacked),
        nullptr);
    EXPECT_EQ(vpi_get(vpiPacked, root), testCase.packed);
    if (testCase.exactType == vpiPackedArrayVar) {
      EXPECT_EQ(vpi_get(vpiArray, root), 0);
    }
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_release_handle(root), 1);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(VPI, PhysicalVariableRandomizationPropertiesHaveExactDefault) {
  // These are exactly the physical variable kinds in the generated
  // RandomizationTypeObjects/OrdinaryVariablePropertyObjects intersection.
  // TypespecMember rand/randc values are covered separately by semantic-edge
  // tests; the physical image currently represents none of those fields.
  constexpr uint32_t variableTypes[]{
      vpiShortRealVar,        vpiRealVar,    vpiByteVar,
      vpiShortIntVar,         vpiIntVar,     vpiLongIntVar,
      vpiIntegerVar,          vpiTimeVar,    vpiRegArray,
      vpiPackedArrayVar,      vpiBitVar,     vpiReg,
      vpiStructVar,           vpiUnionVar,   vpiEnumVar,
      vpiStringVar,           vpiChandleVar, vpiClassVar,
      vpiVirtualInterfaceVar, vpiRegBit,
  };
  char name[] = "top.value";
  for (uint32_t exactType : variableTypes) {
    SCOPED_TRACE(exactType);
    ASSERT_NE(obelisk::reflection::findVPIProperty(exactType, vpiRandType),
              nullptr);
    ASSERT_NE(obelisk::reflection::findVPIProperty(exactType, vpiIsRandomized),
              nullptr);
    Fixture fixture;
    fixture.database =
        makeVPIShapeDatabase(VPIShapeType::BasicScalar, exactType);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    vpiHandle variable = vpi_handle_by_name(name, nullptr);
    ASSERT_NE(variable, nullptr);
    EXPECT_EQ(vpi_get(vpiRandType, variable), vpiNotRand);
    EXPECT_EQ(vpi_get(vpiIsRandomized, variable), 0);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_release_handle(variable), 1);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(VPI, IndexedAndMultiIndexedQueriesPreserveDeclaredIndicesAndValues) {
  Fixture fixture;
  fixture.database = makeVPIIndexedDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  auto integerValue = [](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    return value.value.integer;
  };
  auto release = [](vpiHandle handle) {
    if (handle) {
      EXPECT_EQ(vpi_release_handle(handle), 1);
    }
  };

  char name[] = "top.value";
  vpiHandle root = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(root, nullptr);
  ASSERT_EQ(vpi_get(vpiType, root), vpiRegArray);
  EXPECT_EQ(vpi_get(vpiSize, root), 2);
  EXPECT_EQ(vpi_get(vpiArrayMember, root), 0);
  EXPECT_EQ(vpi_get(vpiConstantSelect, root), 1);
  EXPECT_EQ(vpi_get(vpiSigned, root), 0);
  EXPECT_EQ(vpi_get(vpiScalar, root), 0);
  EXPECT_EQ(vpi_get(vpiVector, root), 1);
  vpiHandle rootLeft = vpi_handle(vpiLeftRange, root);
  vpiHandle rootRight = vpi_handle(vpiRightRange, root);
  ASSERT_NE(rootLeft, nullptr);
  ASSERT_NE(rootRight, nullptr);
  EXPECT_EQ(vpi_get(vpiSize, rootLeft), 64);
  EXPECT_EQ(vpi_get(vpiConstType, rootLeft), vpiIntConst);
  EXPECT_EQ(integerValue(rootLeft), 0);
  EXPECT_EQ(integerValue(rootRight), 1);
  release(rootLeft);
  release(rootRight);
  s_vpi_vecval vector[]{{0, 0}};
  s_vpi_value write{};
  write.format = vpiVectorVal;
  write.value.vector = vector;
  EXPECT_EQ(vpi_put_value(root, &write, nullptr, vpiNoDelay), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  // Read-only indexed inspection must stay on the generated tier-1 state
  // planes and must not mark the schedule deoptimized.
  SchedulePlanState scheduleState;
  std::vector<uint8_t> planValue((fixture.execution.state_bit_count + 7) / 8,
                                 0);
  std::vector<uint8_t> planUnknown(planValue.size(), 0);
  planValue[0] = 0xa5;
  obelisk_rt_native_schedule_plan plan{};
  plan.size = sizeof(plan);
  plan.graph_layout_checksum = fixture.execution.checksum;
  plan.mutable_state = &scheduleState;
  plan.mutable_state_size = sizeof(scheduleState);
  plan.actor_capacity = scheduleState.actors.size();
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = planValue.data();
  plan.state_unknown = planUnknown.data();
  plan.state_bit_count = fixture.execution.state_bit_count;
  plan.bind = planBind;
  plan.run = planRun;
  plan.fallback_snapshot = planSnapshot;
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);

  vpiHandle outer0 = vpi_handle_by_index(root, 0);
  ASSERT_NE(outer0, nullptr);
  EXPECT_EQ(vpi_get(vpiType, outer0), vpiReg);
  EXPECT_EQ(vpi_get(vpiSize, outer0), 4);
  EXPECT_EQ(vpi_get(vpiArrayMember, outer0), 1);
  EXPECT_EQ(vpi_get(vpiConstantSelect, outer0), 1);
  EXPECT_EQ(vpi_get(vpiSigned, outer0), 0);
  EXPECT_EQ(vpi_get(vpiScalar, outer0), 0);
  EXPECT_EQ(vpi_get(vpiVector, outer0), 1);
  EXPECT_STREQ(vpi_get_str(vpiFullName, outer0), "top.value[0]");
  EXPECT_EQ(integerValue(outer0), 5);
  vpiHandle packedLeft = vpi_handle(vpiLeftRange, outer0);
  vpiHandle packedRight = vpi_handle(vpiRightRange, outer0);
  ASSERT_NE(packedLeft, nullptr);
  ASSERT_NE(packedRight, nullptr);
  EXPECT_EQ(integerValue(packedLeft), 7);
  EXPECT_EQ(integerValue(packedRight), 4);
  release(packedLeft);
  release(packedRight);

  vpiHandle bit04 = vpi_handle_by_index(outer0, 4);
  vpiHandle bit07 = vpi_handle_by_index(outer0, 7);
  ASSERT_NE(bit04, nullptr);
  ASSERT_NE(bit07, nullptr);
  EXPECT_EQ(vpi_get(vpiType, bit04), vpiRegBit);
  EXPECT_EQ(vpi_get(vpiArrayMember, bit04), 0);
  EXPECT_EQ(vpi_get(vpiConstantSelect, bit04), 1);
  EXPECT_EQ(vpi_get(vpiSigned, bit04), 0);
  EXPECT_EQ(integerValue(bit04), 1);
  EXPECT_EQ(integerValue(bit07), 0);
  EXPECT_STREQ(vpi_get_str(vpiFullName, bit04), "top.value[0][4]");

  PLI_INT32 multiIndices[]{1, 7};
  vpiHandle multi = vpi_handle_by_multi_index(root, 2, multiIndices);
  ASSERT_NE(multi, nullptr);
  EXPECT_EQ(vpi_get(vpiType, multi), vpiRegBit);
  EXPECT_EQ(vpi_get(vpiAllocScheme, multi), vpiOtherScheme);
  EXPECT_EQ(integerValue(multi), 1);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_STREQ(vpi_get_str(vpiFullName, multi), "top.value[1][7]");

  vpiHandle lastIndex = vpi_handle(vpiIndex, multi);
  ASSERT_NE(lastIndex, nullptr);
  EXPECT_EQ(vpi_get(vpiSize, lastIndex), 64);
  EXPECT_EQ(vpi_get(vpiConstType, lastIndex), vpiIntConst);
  EXPECT_EQ(integerValue(lastIndex), 7);

  vpiHandle indices = vpi_iterate(vpiIndex, multi);
  ASSERT_NE(indices, nullptr);
  EXPECT_EQ(vpi_get(vpiIteratorType, indices), vpiIndex);
  vpiHandle indexUse = vpi_handle(vpiUse, indices);
  ASSERT_NE(indexUse, nullptr);
  EXPECT_EQ(vpi_compare_objects(indexUse, multi), 1);
  release(indexUse);
  vpiHandle packedIndex = vpi_scan(indices);
  vpiHandle unpackedIndex = vpi_scan(indices);
  ASSERT_NE(packedIndex, nullptr);
  ASSERT_NE(unpackedIndex, nullptr);
  EXPECT_EQ(vpi_compare_objects(lastIndex, packedIndex), 1);
  EXPECT_EQ(integerValue(packedIndex), 7);
  EXPECT_EQ(integerValue(unpackedIndex), 1);
  EXPECT_EQ(vpi_scan(indices), nullptr);
  release(packedIndex);
  release(unpackedIndex);
  release(lastIndex);

  vpiHandle packedParent = vpi_handle(vpiParent, multi);
  ASSERT_NE(packedParent, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, packedParent), "top.value[1]");
  EXPECT_EQ(integerValue(packedParent), 10);
  vpiHandle arrayParent = vpi_handle(vpiParent, packedParent);
  ASSERT_NE(arrayParent, nullptr);
  EXPECT_EQ(vpi_compare_objects(arrayParent, root), 1);

  EXPECT_EQ(vpi_handle_by_index(root, -1), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_handle_by_index(root, 2), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_handle_by_index(outer0, 3), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_handle_by_multi_index(root, 0, multiIndices), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_handle_by_multi_index(root, 1, nullptr), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);

  // Derived selections own their complete recipe and remain valid after the
  // base handle is released.
  EXPECT_EQ(vpi_release_handle(root), 1);
  root = nullptr;
  EXPECT_EQ(integerValue(multi), 1);

  for (vpiHandle handle :
       {arrayParent, packedParent, multi, bit07, bit04, outer0})
    release(handle);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, IndexedQueriesHandleCrossLimbWindowsAndPortBitContracts) {
  {
    Fixture fixture;
    fixture.database = makeVPIIndexedDatabase(0, 4, 12, 0);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char name[] = "top.value";
    vpiHandle root = vpi_handle_by_name(name, nullptr);
    ASSERT_NE(root, nullptr);
    std::string bits = "1010101010101" + std::string(52, '0');
    s_vpi_value write{};
    write.format = vpiBinStrVal;
    write.value.str = reinterpret_cast<PLI_BYTE8 *>(bits.data());
    EXPECT_EQ(vpi_put_value(root, &write, nullptr, vpiNoDelay), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);

    vpiHandle window = vpi_handle_by_index(root, 4);
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(vpi_get(vpiSize, window), 13);
    s_vpi_value read{};
    read.format = vpiIntVal;
    vpi_get_value(window, &read);
    EXPECT_EQ(read.value.integer, 0x1555);
    std::string fourStateBits = "10xz101010101" + std::string(52, '0');
    write.value.str = reinterpret_cast<PLI_BYTE8 *>(fourStateBits.data());
    EXPECT_EQ(vpi_put_value(root, &write, nullptr, vpiNoDelay), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    read = {};
    read.format = vpiBinStrVal;
    vpi_get_value(window, &read);
    ASSERT_NE(read.value.str, nullptr);
    EXPECT_STREQ(reinterpret_cast<char *>(read.value.str), "10xz101010101");
    PLI_INT32 indices[]{4, 12};
    vpiHandle topBit = vpi_handle_by_multi_index(root, 2, indices);
    ASSERT_NE(topBit, nullptr);
    read = {};
    read.format = vpiIntVal;
    vpi_get_value(topBit, &read);
    EXPECT_EQ(read.value.integer, 1);
    EXPECT_EQ(vpi_release_handle(topBit), 1);
    EXPECT_EQ(vpi_release_handle(window), 1);
    EXPECT_EQ(vpi_release_handle(root), 1);
    obelisk_rt_v1_context_destroy(context);
  }

  {
    Fixture fixture;
    fixture.database = makeVPIPortDatabase();
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char name[] = "top.value";
    vpiHandle port = vpi_handle_by_name(name, nullptr);
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(vpi_get(vpiType, port), vpiPort);
    vpiHandle bit = vpi_handle_by_index(port, 7);
    ASSERT_NE(bit, nullptr);
    EXPECT_EQ(vpi_get(vpiType, bit), vpiPortBit);
    EXPECT_EQ(vpi_get_str(vpiName, bit), nullptr);
    EXPECT_STREQ(vpi_get_str(vpiFullName, bit), "top.value[7]");
    EXPECT_EQ(vpi_handle(vpiIndex, bit), nullptr);
    vpiHandle parent = vpi_handle(vpiParent, bit);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(vpi_compare_objects(parent, port), 1);
    EXPECT_EQ(vpi_release_handle(parent), 1);
    EXPECT_EQ(vpi_release_handle(bit), 1);
    EXPECT_EQ(vpi_release_handle(port), 1);
    obelisk_rt_v1_context_destroy(context);
  }

  struct NetCase {
    uint32_t rootType;
    uint32_t elementType;
  };
  constexpr NetCase netCases[]{{vpiNetArray, vpiNet},
                               {vpiInterconnectArray, vpiInterconnectNet}};
  for (const NetCase &testCase : netCases) {
    Fixture fixture;
    fixture.database = makeVPIIndexedDatabase(0, 1, 7, 4, testCase.rootType,
                                              OBELISK_RT_DESIGN_RECORD_NET);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char name[] = "top.value";
    vpiHandle root = vpi_handle_by_name(name, nullptr);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(vpi_get(vpiType, root), testCase.rootType);
    vpiHandle element = vpi_handle_by_index(root, 0);
    ASSERT_NE(element, nullptr);
    EXPECT_EQ(vpi_get(vpiType, element), testCase.elementType);
    vpiHandle indices = vpi_iterate(vpiIndex, element);
    ASSERT_NE(indices, nullptr);
    vpiHandle indexUse = vpi_handle(vpiUse, indices);
    ASSERT_NE(indexUse, nullptr);
    EXPECT_EQ(vpi_compare_objects(indexUse, element), 1);
    EXPECT_EQ(vpi_release_handle(indexUse), 1);
    vpiHandle elementIndex = vpi_scan(indices);
    ASSERT_NE(elementIndex, nullptr);
    s_vpi_value indexValue{};
    indexValue.format = vpiIntVal;
    vpi_get_value(elementIndex, &indexValue);
    EXPECT_EQ(indexValue.value.integer, 0);
    EXPECT_EQ(vpi_release_handle(elementIndex), 1);
    EXPECT_EQ(vpi_scan(indices), nullptr);
    vpiHandle bit = vpi_handle_by_index(element, 7);
    ASSERT_NE(bit, nullptr);
    EXPECT_EQ(vpi_get(vpiType, bit), vpiNetBit);
    EXPECT_EQ(vpi_release_handle(bit), 1);
    EXPECT_EQ(vpi_release_handle(element), 1);
    EXPECT_EQ(vpi_release_handle(root), 1);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(VPI, IndexedQueriesPreserveNegativeAscendingAndDescendingBounds) {
  Fixture fixture;
  fixture.database = makeVPIIndexedDatabase(-2, -1, -3, -6);
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char name[] = "top.value";
  vpiHandle root = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(root, nullptr);
  s_vpi_vecval vector[]{{1, 0}};
  s_vpi_value write{};
  write.format = vpiVectorVal;
  write.value.vector = vector;
  EXPECT_EQ(vpi_put_value(root, &write, nullptr, vpiNoDelay), nullptr);
  PLI_INT32 indices[]{-2, -6};
  vpiHandle bit = vpi_handle_by_multi_index(root, 2, indices);
  ASSERT_NE(bit, nullptr);
  s_vpi_value read{};
  read.format = vpiIntVal;
  vpi_get_value(bit, &read);
  EXPECT_EQ(read.value.integer, 1);
  vpiHandle index = vpi_handle(vpiIndex, bit);
  ASSERT_NE(index, nullptr);
  read = {};
  read.format = vpiIntVal;
  vpi_get_value(index, &read);
  EXPECT_EQ(read.value.integer, -6);
  EXPECT_EQ(vpi_release_handle(index), 1);
  EXPECT_EQ(vpi_release_handle(bit), 1);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, IndexedQueriesCountRemainingMultidimensionalUnpackedElements) {
  Fixture fixture;
  fixture.database = makeVPIIndexedDatabase(
      1, 0, 2, 0, vpiRegArray, OBELISK_RT_DESIGN_RECORD_STORAGE, false);
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char name[] = "top.value";
  vpiHandle root = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(vpi_get(vpiSize, root), 6);
  vpiHandle row = vpi_handle_by_index(root, 1);
  ASSERT_NE(row, nullptr);
  EXPECT_EQ(vpi_get(vpiType, row), vpiRegArray);
  EXPECT_EQ(vpi_get(vpiSize, row), 3);
  vpiHandle element = vpi_handle_by_index(row, 2);
  ASSERT_NE(element, nullptr);
  EXPECT_EQ(vpi_get(vpiType, element), vpiReg);
  EXPECT_EQ(vpi_get(vpiArrayMember, element), 1);
  EXPECT_EQ(vpi_release_handle(element), 1);
  EXPECT_EQ(vpi_release_handle(row), 1);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ArrayValueQueryUsesLRMMultidimensionalOrderAndUserStorage) {
  Fixture fixture;
  fixture.database = makeVPIIndexedDatabase(
      2, 0, 3, 5, vpiRegArray, OBELISK_RT_DESIGN_RECORD_STORAGE, false);
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  // Internal planes encode IEEE A/B as value=A^B and unknown=B.
  constexpr uint16_t aval = 0b101101011;
  constexpr uint16_t bval = 0b010001100;
  context->stateValue[0] = 0;
  context->stateUnknown[0] = 0;
  SchedulePlanState scheduleState;
  std::vector<uint8_t> planValue((fixture.execution.state_bit_count + 7) / 8,
                                 0);
  std::vector<uint8_t> planUnknown(planValue.size(), 0);
  planValue[0] = static_cast<uint8_t>(aval ^ bval);
  planValue[1] = static_cast<uint8_t>((aval ^ bval) >> 8);
  planUnknown[0] = static_cast<uint8_t>(bval);
  planUnknown[1] = static_cast<uint8_t>(bval >> 8);
  obelisk_rt_native_schedule_plan plan{};
  plan.size = sizeof(plan);
  plan.graph_layout_checksum = fixture.execution.checksum;
  plan.mutable_state = &scheduleState;
  plan.mutable_state_size = sizeof(scheduleState);
  plan.actor_capacity = scheduleState.actors.size();
  plan.flags = OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE;
  plan.state_value = planValue.data();
  plan.state_unknown = planUnknown.data();
  plan.state_bit_count = fixture.execution.state_bit_count;
  plan.bind = planBind;
  plan.run = planRun;
  plan.fallback_snapshot = planSnapshot;
  ASSERT_EQ(obelisk_rt_v1_scheduler_install_aot(context, &plan), OBELISK_RT_OK);

  char name[] = "top.value";
  vpiHandle root = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(root, nullptr);
  PLI_INT32 start[]{1, 4};
  std::array<PLI_BYTE8, 10> storage{};
  s_vpi_arrayvalue values{};
  values.format = vpiRawFourStateVal;
  values.flags = vpiUserAllocFlag;
  values.value.rawvals = storage.data();
  vpi_get_value_array(root, &values, start, 5);
  ASSERT_EQ(values.value.rawvals, storage.data());
  for (size_t index = 0; index != 5; ++index) {
    const size_t physical = 4 + index;
    EXPECT_EQ(storage[index * 2], (aval >> physical) & 1) << index;
    EXPECT_EQ(storage[index * 2 + 1], (bval >> physical) & 1) << index;
  }
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_FALSE(context->nativeScheduleDirtyRootsPresent);

  vpiHandle row = vpi_handle_by_index(root, 1);
  ASSERT_NE(row, nullptr);
  PLI_INT32 rowStart[]{4};
  values = {};
  values.format = vpiRawTwoStateVal;
  vpi_get_value_array(row, &values, rowStart, 2);
  ASSERT_NE(values.value.rawvals, nullptr);
  EXPECT_EQ(values.value.rawvals[0], (aval >> 4) & 1);
  EXPECT_EQ(values.value.rawvals[1], (aval >> 5) & 1);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  EXPECT_EQ(vpi_release_handle(row), 1);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ArrayValueQuerySupportsAllNineLRMFormats) {
  auto run = [](uint32_t publicTypespec, uint32_t semanticKind, uint64_t width,
                uint32_t semanticFlags, bool isSigned, uint32_t format,
                const std::array<uint64_t, 3> &bits, auto check) {
    SCOPED_TRACE(format);
    ASSERT_EQ(isSigned,
              (semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED) != 0);
    Fixture fixture;
    const size_t count = std::min<size_t>(bits.size(), 65 / width);
    fixture.database = makeVPITypedArrayDatabase(
        publicTypespec, semanticKind, width, semanticFlags, vpiRegArray,
        OBELISK_RT_DESIGN_RECORD_STORAGE, count);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    std::fill(context->stateUnknown.begin(), context->stateUnknown.end(), 0);
    for (size_t element = 0; element != count; ++element)
      for (uint64_t bit = 0; bit != width; ++bit)
        if ((bits[element] & (uint64_t{1} << bit)) != 0)
          context->stateValue[(element * width + bit) / 64] |=
              uint64_t{1} << ((element * width + bit) % 64);
    char name[] = "top.value";
    vpiHandle root = vpi_handle_by_name(name, nullptr);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(context->stateValue[0] & bits[0], bits[0]);
    PLI_INT32 start[]{0};
    s_vpi_arrayvalue values{};
    values.format = format;
    vpi_get_value_array(root, &values, start, count);
    ASSERT_NE(values.value.rawvals, nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    check(values);
    EXPECT_EQ(vpi_release_handle(root), 1);
    obelisk_rt_v1_context_destroy(context);
  };

  run(vpiIntTypespec, OBELISK_RT_DESIGN_SEMANTIC_INT, 32,
      OBELISK_RT_DESIGN_SEMANTIC_SIGNED, true, vpiIntVal,
      {1, UINT32_C(0xffffffff), UINT32_C(0x80000000)},
      [](const s_vpi_arrayvalue &values) {
        EXPECT_EQ(values.value.integers[0], 1);
        EXPECT_EQ(values.value.integers[1], -1);
      });
  run(vpiByteTypespec, OBELISK_RT_DESIGN_SEMANTIC_BYTE, 8,
      OBELISK_RT_DESIGN_SEMANTIC_SIGNED, true, vpiShortIntVal,
      {UINT8_C(0x80), UINT8_C(0x7f), UINT8_C(0xff)},
      [](const s_vpi_arrayvalue &values) {
        EXPECT_EQ(values.value.shortints[0], -128);
        EXPECT_EQ(values.value.shortints[1], 127);
        EXPECT_EQ(values.value.shortints[2], -1);
      });
  run(vpiLongIntTypespec, OBELISK_RT_DESIGN_SEMANTIC_LONG_INT, 64,
      OBELISK_RT_DESIGN_SEMANTIC_SIGNED, true, vpiLongIntVal,
      {1, UINT64_MAX, UINT64_C(0x8000000000000000)},
      [](const s_vpi_arrayvalue &values) {
        EXPECT_EQ(values.value.longints[0], 1);
      });
  run(vpiTimeTypespec, OBELISK_RT_DESIGN_SEMANTIC_TIME, 64,
      OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE, false, vpiTimeVal,
      {1, UINT64_C(0x123456789abcdef0), UINT64_MAX},
      [](const s_vpi_arrayvalue &values) {
        EXPECT_EQ(values.value.times[0].type, vpiSimTime);
      });

  const std::array<double, 3> reals{{1.5, -0.25, 1234.0}};
  std::array<uint64_t, 3> realBits{};
  std::memcpy(realBits.data(), reals.data(), sizeof(reals));
  run(vpiRealTypespec, OBELISK_RT_DESIGN_SEMANTIC_REAL, 64, 0, false,
      vpiRealVal, realBits, [&](const s_vpi_arrayvalue &values) {
        EXPECT_DOUBLE_EQ(values.value.reals[0], reals[0]);
      });
  const std::array<float, 3> shortReals{{1.5F, -0.25F, 1234.0F}};
  std::array<uint32_t, 3> shortRealBits{};
  std::memcpy(shortRealBits.data(), shortReals.data(), sizeof(shortReals));
  run(vpiShortRealTypespec, OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL, 32, 0, false,
      vpiShortRealVal, {shortRealBits[0], shortRealBits[1], shortRealBits[2]},
      [&](const s_vpi_arrayvalue &values) {
        for (size_t index = 0; index != 2; ++index)
          EXPECT_FLOAT_EQ(values.value.shortreals[index], shortReals[index]);
      });

  // The three fixed bit-stream formats are also covered by a non-word width.
  for (uint32_t format :
       {vpiVectorVal, vpiRawTwoStateVal, vpiRawFourStateVal}) {
    SCOPED_TRACE(format);
    run(vpiLogicTypespec, OBELISK_RT_DESIGN_SEMANTIC_LOGIC, 37,
        OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE, false, format,
        {UINT64_C(0x1fffffffff), UINT64_C(0x123456789), 0},
        [format](const s_vpi_arrayvalue &values) {
          if (format == vpiVectorVal) {
            EXPECT_EQ(values.value.vectors[0].aval, UINT32_MAX);
            EXPECT_EQ(values.value.vectors[1].aval, 0x1fU);
          } else {
            EXPECT_EQ(static_cast<uint8_t>(values.value.rawvals[0]), 0xffU);
            EXPECT_EQ(static_cast<uint8_t>(values.value.rawvals[4]), 0x1fU);
          }
        });
  }
}

TEST(VPI, ArrayValueQueryRejectsInvalidRequestsWithoutChangingSchedulerTier) {
  Fixture fixture;
  fixture.database = makeVPITypedArrayDatabase(
      vpiLogicTypespec, OBELISK_RT_DESIGN_SEMANTIC_LOGIC, 4,
      OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE);
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char name[] = "top.value";
  vpiHandle root = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(root, nullptr);
  PLI_INT32 start[]{0};
  s_vpi_arrayvalue values{};
  values.format = vpiStringVal;
  values.value.rawvals = reinterpret_cast<PLI_BYTE8 *>(uintptr_t{1});
  vpi_get_value_array(root, &values, start, 1);
  EXPECT_EQ(values.value.rawvals, nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);

  values = {};
  values.format = vpiVectorVal;
  values.flags = vpiUserAllocFlag;
  vpi_get_value_array(root, &values, start, 1);
  EXPECT_EQ(values.value.rawvals, nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  values = {};
  values.format = vpiVectorVal;
  vpi_get_value_array(root, &values, nullptr, 1);
  EXPECT_EQ(values.value.rawvals, nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  values = {};
  values.format = vpiVectorVal;
  vpi_get_value_array(root, &values, start, 0);
  EXPECT_EQ(values.value.rawvals, nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  values = {};
  values.format = vpiVectorVal;
  values.flags = 1;
  vpi_get_value_array(root, &values, start, 1);
  EXPECT_EQ(values.value.rawvals, nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  start[0] = 2;
  values = {};
  values.format = vpiVectorVal;
  vpi_get_value_array(root, &values, start, 2);
  EXPECT_EQ(values.value.rawvals, nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_FALSE(context->nativeScheduleDirtyRootsPresent);

  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, IndexedQueriesRejectNonIntegralFlattenedStorage) {
  constexpr uint32_t nonIntegralKinds[]{
      vpiClassVar,     vpiChandleVar, vpiVirtualInterfaceVar, vpiRealVar,
      vpiShortRealVar, vpiRealNet,    vpiShortRealNet};
  for (uint32_t exactType : nonIntegralKinds) {
    SCOPED_TRACE(exactType);
    Fixture fixture;
    fixture.database =
        makeVPIShapeDatabase(VPIShapeType::BasicVector, exactType);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char name[] = "top.value";
    vpiHandle value = vpi_handle_by_name(name, nullptr);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(vpi_handle_by_index(value, 0), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_release_handle(value), 1);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(VPI, IndexedQueriesRejectIndicesOnPartialOrdinaryPackedValues) {
  struct TestCase {
    uint32_t exactType;
    uint32_t recordKind;
  };
  constexpr TestCase testCases[]{
      {vpiReg, OBELISK_RT_DESIGN_RECORD_STORAGE},
      {vpiNet, OBELISK_RT_DESIGN_RECORD_NET},
  };
  for (const TestCase &testCase : testCases) {
    SCOPED_TRACE(testCase.exactType);
    Fixture fixture;
    fixture.database = makeVPIIndexedDatabase(
        0, 1, 7, 4, testCase.exactType, testCase.recordKind, true, false, true);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char name[] = "top.value";
    vpiHandle root = vpi_handle_by_name(name, nullptr);
    ASSERT_NE(root, nullptr);
    vpiHandle partial = vpi_handle_by_index(root, 0);
    ASSERT_NE(partial, nullptr);
    EXPECT_EQ(vpi_get(vpiType, partial), testCase.exactType);
    EXPECT_EQ(vpi_get(vpiArrayMember, partial), 0);
    EXPECT_EQ(vpi_handle(vpiIndex, partial), nullptr);
    EXPECT_EQ(vpi_iterate(vpiIndex, partial), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_release_handle(partial), 1);
    EXPECT_EQ(vpi_release_handle(root), 1);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(VPI, TraversesLazyTypespecRangesElementsMembersAndEnumBase) {
  Fixture fixture;
  fixture.database = makeSemanticTraversalDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  auto integerValue = [](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    return value.value.integer;
  };
  auto release = [](vpiHandle handle) {
    if (handle) {
      EXPECT_EQ(vpi_release_handle(handle), 1);
    }
  };

  char name[] = "top.value";
  vpiHandle object = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(object, nullptr);
  vpiHandle array = vpi_handle(vpiTypespec, object);
  ASSERT_NE(array, nullptr);
  EXPECT_EQ(vpi_get(vpiType, array), vpiArrayTypespec);
  EXPECT_EQ(vpi_get(vpiAllocScheme, array), vpiOtherScheme);
  EXPECT_EQ(vpi_get(vpiArrayType, array), vpiStaticArray);
  EXPECT_EQ(vpi_get_str(vpiName, array), nullptr);
  vpiHandle instance = vpi_handle(vpiInstance, array);
  ASSERT_NE(instance, nullptr);
  EXPECT_EQ(vpi_get(vpiType, instance), vpiModule);
  EXPECT_STREQ(vpi_get_str(vpiName, instance), "top");
  release(instance);

  vpiHandle ranges = vpi_iterate(vpiRange, array);
  ASSERT_NE(ranges, nullptr);
  EXPECT_EQ(vpi_get(vpiIteratorType, ranges), vpiRange);
  vpiHandle use = vpi_handle(vpiUse, ranges);
  ASSERT_NE(use, nullptr);
  EXPECT_EQ(vpi_compare_objects(use, array), 1);
  release(use);

  vpiHandle outerRange = vpi_scan(ranges);
  ASSERT_NE(outerRange, nullptr);
  EXPECT_EQ(vpi_get(vpiType, outerRange), vpiRange);
  EXPECT_EQ(vpi_get(vpiAllocScheme, outerRange), vpiOtherScheme);
  EXPECT_EQ(vpi_get(vpiSize, outerRange), 4);
  vpiHandle left = vpi_handle(vpiLeftRange, outerRange);
  vpiHandle right = vpi_handle(vpiRightRange, outerRange);
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(vpi_get(vpiAllocScheme, left), vpiOtherScheme);
  EXPECT_EQ(integerValue(left), 3);
  EXPECT_EQ(integerValue(right), 0);
  release(left);
  release(right);
  release(outerRange);

  for (unsigned dimension = 0; dimension != 3; ++dimension) {
    vpiHandle emptyRange = vpi_scan(ranges);
    ASSERT_NE(emptyRange, nullptr);
    EXPECT_EQ(vpi_get(vpiSize, emptyRange), 0);
    EXPECT_EQ(vpi_handle(vpiLeftRange, emptyRange), nullptr);
    EXPECT_EQ(vpi_handle(vpiRightRange, emptyRange), nullptr);
    release(emptyRange);
  }
  EXPECT_EQ(vpi_scan(ranges), nullptr);

  vpiHandle dynamic = vpi_handle(vpiElemTypespec, array);
  ASSERT_NE(dynamic, nullptr);
  EXPECT_EQ(vpi_get(vpiType, dynamic), vpiArrayTypespec);
  EXPECT_EQ(vpi_get(vpiArrayType, dynamic), vpiDynamicArray);
  vpiHandle associative = vpi_handle(vpiElemTypespec, dynamic);
  ASSERT_NE(associative, nullptr);
  EXPECT_EQ(vpi_get(vpiType, associative), vpiArrayTypespec);
  EXPECT_EQ(vpi_get(vpiArrayType, associative), vpiAssocArray);
  EXPECT_EQ(vpi_handle(vpiLeftRange, associative), nullptr);
  vpiHandle indexType = vpi_handle(vpiIndexTypespec, associative);
  ASSERT_NE(indexType, nullptr);
  EXPECT_EQ(vpi_get(vpiType, indexType), vpiStringTypespec);
  release(indexType);
  vpiHandle queue = vpi_handle(vpiElemTypespec, associative);
  ASSERT_NE(queue, nullptr);
  EXPECT_EQ(vpi_get(vpiType, queue), vpiArrayTypespec);
  EXPECT_EQ(vpi_get(vpiArrayType, queue), vpiQueueArray);
  vpiHandle packed = vpi_handle(vpiElemTypespec, queue);
  ASSERT_NE(packed, nullptr);
  EXPECT_EQ(vpi_get(vpiType, packed), vpiPackedArrayTypespec);
  EXPECT_EQ(vpi_get(vpiVector, packed), 1);
  left = vpi_handle(vpiLeftRange, packed);
  right = vpi_handle(vpiRightRange, packed);
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(integerValue(left), 7);
  EXPECT_EQ(integerValue(right), 4);
  release(left);
  release(right);

  vpiHandle structure = vpi_handle(vpiElemTypespec, packed);
  ASSERT_NE(structure, nullptr);
  EXPECT_EQ(vpi_get(vpiType, structure), vpiStructTypespec);
  EXPECT_EQ(vpi_get(vpiPacked, structure), 1);
  EXPECT_EQ(vpi_handle(vpiElemTypespec, structure), nullptr);
  vpiHandle members = vpi_iterate(vpiTypespecMember, structure);
  ASSERT_NE(members, nullptr);

  vpiHandle member = vpi_scan(members);
  ASSERT_NE(member, nullptr);
  EXPECT_EQ(vpi_get(vpiType, member), vpiTypespecMember);
  EXPECT_STREQ(vpi_get_str(vpiName, member), "logic");
  EXPECT_EQ(vpi_get(vpiRandType, member), vpiRand);
  vpiHandle memberType = vpi_handle(vpiTypespec, member);
  ASSERT_NE(memberType, nullptr);
  EXPECT_EQ(vpi_get(vpiType, memberType), vpiLogicTypespec);
  EXPECT_EQ(vpi_get(vpiVector, memberType), 0);
  EXPECT_EQ(vpi_iterate(vpiRange, memberType), nullptr);
  release(memberType);
  release(member);

  member = vpi_scan(members);
  ASSERT_NE(member, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiName, member), "value");
  EXPECT_EQ(vpi_get(vpiRandType, member), vpiRandC);
  memberType = vpi_handle(vpiTypespec, member);
  ASSERT_NE(memberType, nullptr);
  EXPECT_EQ(vpi_get(vpiType, memberType), vpiEnumTypespec);
  vpiHandle base = vpi_handle(vpiBaseTypespec, memberType);
  ASSERT_NE(base, nullptr);
  EXPECT_EQ(vpi_get(vpiType, base), vpiBitTypespec);
  EXPECT_EQ(vpi_handle(vpiElemTypespec, base), nullptr);
  release(base);
  release(memberType);
  release(member);
  EXPECT_EQ(vpi_scan(members), nullptr);

  release(structure);
  release(packed);
  release(queue);
  release(associative);
  release(dynamic);
  release(array);
  release(object);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, WildcardAssociativeTypespecHasNoIndexTypespec) {
  Fixture fixture;
  fixture.database = makeSemanticTraversalDatabase(true);
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char name[] = "top.value";
  vpiHandle object = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(object, nullptr);
  vpiHandle outer = vpi_handle(vpiTypespec, object);
  ASSERT_NE(outer, nullptr);
  vpiHandle dynamic = vpi_handle(vpiElemTypespec, outer);
  ASSERT_NE(dynamic, nullptr);
  vpiHandle associative = vpi_handle(vpiElemTypespec, dynamic);
  ASSERT_NE(associative, nullptr);
  EXPECT_EQ(vpi_get(vpiArrayType, associative), vpiAssocArray);
  EXPECT_EQ(vpi_handle(vpiIndexTypespec, associative), nullptr);
  vpiHandle element = vpi_handle(vpiElemTypespec, associative);
  ASSERT_NE(element, nullptr);
  EXPECT_EQ(vpi_get(vpiArrayType, element), vpiQueueArray);

  EXPECT_EQ(vpi_release_handle(element), 1);
  EXPECT_EQ(vpi_release_handle(associative), 1);
  EXPECT_EQ(vpi_release_handle(dynamic), 1);
  EXPECT_EQ(vpi_release_handle(outer), 1);
  EXPECT_EQ(vpi_release_handle(object), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, PrimaryNamedTypespecHasNoTypedefAlias) {
  Fixture fixture;
  fixture.database = makeNamedSemanticTypespecDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char name[] = "top.value";
  vpiHandle named = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(named, nullptr);
  EXPECT_EQ(vpi_get(vpiType, named), vpiStructTypespec);
  EXPECT_EQ(vpi_get(vpiPacked, named), 1);
  EXPECT_EQ(vpi_handle(vpiTypedefAlias, named), nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFile, named), "test.sv");
  EXPECT_EQ(vpi_get(vpiLineNo, named), 7);
  EXPECT_EQ(vpi_get_str(vpiFullName, named), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);

  EXPECT_EQ(vpi_release_handle(named), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, BuiltinTypedefAliasReturnsOneUnnamedUnderlyingTypespec) {
  Fixture fixture;
  fixture.database = makeBuiltinAliasTypespecDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char name[] = "top.value";
  vpiHandle named = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(named, nullptr);
  vpiHandle underlying = vpi_handle(vpiTypedefAlias, named);
  ASSERT_NE(underlying, nullptr);
  EXPECT_EQ(vpi_get(vpiType, underlying), vpiLogicTypespec);
  EXPECT_EQ(vpi_get_str(vpiName, underlying), nullptr);
  EXPECT_EQ(vpi_handle(vpiTypedefAlias, underlying), nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFile, underlying), "test.sv");
  EXPECT_EQ(vpi_get(vpiLineNo, underlying), 7);

  EXPECT_EQ(vpi_release_handle(underlying), 1);
  EXPECT_EQ(vpi_release_handle(named), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, DirectIntegralVectorTraversesOneLazyPackedDimension) {
  Fixture fixture;
  fixture.database = makeDirectIntegralVectorDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  auto integerValue = [](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    return value.value.integer;
  };
  char name[] = "top.value";
  vpiHandle object = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(object, nullptr);
  vpiHandle vector = vpi_handle(vpiTypespec, object);
  ASSERT_NE(vector, nullptr);
  EXPECT_EQ(vpi_get(vpiType, vector), vpiLogicTypespec);
  EXPECT_EQ(vpi_get(vpiVector, vector), 1);
  vpiHandle left = vpi_handle(vpiLeftRange, vector);
  vpiHandle right = vpi_handle(vpiRightRange, vector);
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(integerValue(left), -2);
  EXPECT_EQ(integerValue(right), 5);

  vpiHandle ranges = vpi_iterate(vpiRange, vector);
  ASSERT_NE(ranges, nullptr);
  vpiHandle range = vpi_scan(ranges);
  ASSERT_NE(range, nullptr);
  EXPECT_EQ(vpi_scan(ranges), nullptr);
  vpiHandle element = vpi_handle(vpiElemTypespec, vector);
  ASSERT_NE(element, nullptr);
  EXPECT_EQ(vpi_get(vpiType, element), vpiLogicTypespec);
  EXPECT_EQ(vpi_get(vpiVector, element), 0);
  EXPECT_EQ(vpi_iterate(vpiRange, element), nullptr);
  EXPECT_EQ(vpi_handle(vpiElemTypespec, element), nullptr);
  EXPECT_EQ(vpi_compare_objects(vector, element), 0);

  for (vpiHandle handle : {element, range, right, left, vector, object})
    EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, NonArraySemanticPayloadIsNotAnElementTypespec) {
  Fixture fixture;
  fixture.database = makeMailboxSemanticDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char name[] = "top.value";
  vpiHandle object = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(object, nullptr);
  vpiHandle mailbox = vpi_handle(vpiTypespec, object);
  ASSERT_NE(mailbox, nullptr);
  EXPECT_EQ(vpi_get(vpiType, mailbox), vpiClassTypespec);
  EXPECT_EQ(vpi_handle(vpiElemTypespec, mailbox), nullptr);

  EXPECT_EQ(vpi_release_handle(mailbox), 1);
  EXPECT_EQ(vpi_release_handle(object), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, RealAndShortRealUseRealValueFormatWithoutChangingBitStorage) {
  struct Case {
    VPIShapeType shape;
    uint32_t exactType;
    double first;
    double second;
  };
  for (const Case &testCase : {
           Case{VPIShapeType::ShortReal, vpiShortRealVar, 1.5, -2.25},
           Case{VPIShapeType::Real, vpiRealVar, 3.25, -7.5},
       }) {
    SCOPED_TRACE(testCase.exactType);
    Fixture fixture;
    fixture.database = makeVPIShapeDatabase(testCase.shape, testCase.exactType);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

    char valueName[] = "top.value";
    vpiHandle value = vpi_handle_by_name(valueName, nullptr);
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(vpi_get(vpiType, value), testCase.exactType);

    s_vpi_value written{};
    written.format = vpiRealVal;
    written.value.real = testCase.first;
    EXPECT_EQ(vpi_put_value(value, &written, nullptr, vpiNoDelay), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);

    s_vpi_value read{};
    read.format = vpiObjTypeVal;
    vpi_get_value(value, &read);
    EXPECT_EQ(read.format, vpiRealVal);
    EXPECT_DOUBLE_EQ(read.value.real, testCase.first);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);

    written.value.real = testCase.second;
    EXPECT_EQ(vpi_put_value(value, &written, nullptr, vpiNoDelay), nullptr);
    read = {};
    read.format = vpiRealVal;
    vpi_get_value(value, &read);
    EXPECT_DOUBLE_EQ(read.value.real, testCase.second);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);

    EXPECT_EQ(vpi_release_handle(value), 1);
    obelisk_rt_v1_context_destroy(context);
  }

  // Cross-kind reads use the signed semantic/physical type and the complete
  // arbitrary-width conversion path rather than interpreting integer bits as
  // IEEE floating-point storage.
  Fixture fixture;
  fixture.database = makeVPIShapeDatabase(VPIShapeType::BasicVector, vpiIntVar);
  put32(fixture.database, 336 + 4,
        OBELISK_RT_DESIGN_TYPE_SCALAR |
            ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE |
              OBELISK_RT_DESIGN_TYPE_SIGNED | OBELISK_RT_DESIGN_TYPE_PACKED)
             << 8));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char valueName[] = "top.value";
  vpiHandle value = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(value, nullptr);
  s_vpi_value real{};
  real.format = vpiRealVal;
  real.value.real = -1.0;
  vpi_get_value(value, &real);
  s_vpi_error_info error{};
  EXPECT_EQ(vpi_chk_error(&error), 0);
  EXPECT_DOUBLE_EQ(real.value.real, 0.0);
  // Cross-kind writes remain a separate mutation-path contract.
  real.value.real = -1.0;
  EXPECT_EQ(vpi_put_value(value, &real, nullptr, vpiNoDelay), nullptr);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message,
               "vpiRealVal conversion is not implemented for non-real "
               "objects");
  EXPECT_EQ(vpi_release_handle(value), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ReadsEveryScalarValueFormatAndResolvesObjectDefaults) {
  Fixture fixture;
  fixture.database = makeVPIShapeDatabase(VPIShapeType::BasicVector, vpiIntVar);
  put32(fixture.database, 336 + 4,
        OBELISK_RT_DESIGN_TYPE_SCALAR |
            ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE |
              OBELISK_RT_DESIGN_TYPE_SIGNED | OBELISK_RT_DESIGN_TYPE_PACKED)
             << 8));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char valueName[] = "top.value";
  vpiHandle handle = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(handle, nullptr);
  s_vpi_value written{};
  written.format = vpiBinStrVal;
  char fourState[] = "10xz";
  written.value.str = fourState;
  EXPECT_EQ(vpi_put_value(handle, &written, nullptr, vpiNoDelay), nullptr);
  ASSERT_EQ(vpi_chk_error(nullptr), 0);

  s_vpi_value read{};
  read.format = vpiObjTypeVal;
  vpi_get_value(handle, &read);
  EXPECT_EQ(read.format, vpiIntVal);
  EXPECT_EQ(read.value.integer, 8);

  read = {};
  read.format = vpiBinStrVal;
  vpi_get_value(handle, &read);
  ASSERT_NE(read.value.str, nullptr);
  EXPECT_STREQ(read.value.str, "000010xz");

  read = {};
  read.format = vpiOctStrVal;
  vpi_get_value(handle, &read);
  ASSERT_NE(read.value.str, nullptr);
  EXPECT_STREQ(read.value.str, "01X");

  read = {};
  read.format = vpiHexStrVal;
  vpi_get_value(handle, &read);
  ASSERT_NE(read.value.str, nullptr);
  EXPECT_STREQ(read.value.str, "0X");

  read = {};
  read.format = vpiDecStrVal;
  vpi_get_value(handle, &read);
  ASSERT_NE(read.value.str, nullptr);
  EXPECT_STREQ(read.value.str, "x");

  read = {};
  read.format = vpiScalarVal;
  vpi_get_value(handle, &read);
  EXPECT_EQ(read.value.scalar, vpiZ);

  read = {};
  read.format = vpiIntVal;
  vpi_get_value(handle, &read);
  EXPECT_EQ(read.value.integer, 8);

  read = {};
  read.format = vpiRealVal;
  vpi_get_value(handle, &read);
  EXPECT_DOUBLE_EQ(read.value.real, 8.0);

  read = {};
  read.format = vpiStringVal;
  vpi_get_value(handle, &read);
  ASSERT_NE(read.value.str, nullptr);
  EXPECT_EQ(static_cast<unsigned char>(read.value.str[0]), 8);

  read = {};
  read.format = vpiVectorVal;
  vpi_get_value(handle, &read);
  ASSERT_NE(read.value.vector, nullptr);
  EXPECT_EQ(read.value.vector[0].aval, 10u);
  EXPECT_EQ(read.value.vector[0].bval, 3u);

  read = {};
  read.format = vpiStrengthVal;
  vpi_get_value(handle, &read);
  ASSERT_NE(read.value.strength, nullptr);
  EXPECT_EQ(read.value.strength[0].logic, vpiZ);
  EXPECT_EQ(read.value.strength[0].s0, vpiStrongDrive);
  EXPECT_EQ(read.value.strength[0].s1, vpiStrongDrive);
  EXPECT_EQ(read.value.strength[3].logic, vpi1);

  read = {};
  read.format = vpiTimeVal;
  vpi_get_value(handle, &read);
  ASSERT_NE(read.value.time, nullptr);
  EXPECT_EQ(read.value.time->type, vpiSimTime);
  EXPECT_EQ(read.value.time->high, 0u);
  EXPECT_EQ(read.value.time->low, 8u);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  char minusOne[] = "11111111";
  written.value.str = minusOne;
  EXPECT_EQ(vpi_put_value(handle, &written, nullptr, vpiNoDelay), nullptr);
  read = {};
  read.format = vpiDecStrVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, "-1");
  read = {};
  read.format = vpiRealVal;
  vpi_get_value(handle, &read);
  EXPECT_DOUBLE_EQ(read.value.real, -1.0);
  read = {};
  read.format = vpiHexStrVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, "ff");

  char allX[] = "xxxxxxxx";
  written.value.str = allX;
  EXPECT_EQ(vpi_put_value(handle, &written, nullptr, vpiNoDelay), nullptr);
  read = {};
  read.format = vpiHexStrVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, "xx");
  read.format = vpiOctStrVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, "xxx");

  char allZ[] = "zzzzzzzz";
  written.value.str = allZ;
  EXPECT_EQ(vpi_put_value(handle, &written, nullptr, vpiNoDelay), nullptr);
  read = {};
  read.format = vpiHexStrVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, "zz");
  read.format = vpiOctStrVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, "zzz");
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(context);

  Fixture packedString;
  packedString.database =
      makeVPIShapeDatabase(VPIShapeType::BasicVector, vpiReg);
  put64(packedString.database, 240 + 56, 32);
  put64(packedString.database, 240 + 64, 31);
  put64(packedString.database, 336 + 8, 32);
  put64(packedString.database, 336 + 16, 31);
  put64(packedString.database, 32, imageChecksum(packedString.database));
  packedString.execution.design_database = packedString.database.data();
  packedString.execution.design_database_size = packedString.database.size();
  context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&packedString.execution,
                                                    &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  handle = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(handle, nullptr);
  char packedCharacters[] = "00000000010000010000000001000011";
  written.value.str = packedCharacters;
  EXPECT_EQ(vpi_put_value(handle, &written, nullptr, vpiNoDelay), nullptr);
  read = {};
  read.format = vpiStringVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, " A C");
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(context);

  struct DefaultCase {
    VPIShapeType shape;
    uint32_t type;
    PLI_INT32 format;
  };
  for (const DefaultCase &testCase : {
           DefaultCase{VPIShapeType::BasicScalar, vpiReg, vpiScalarVal},
           DefaultCase{VPIShapeType::BasicVector, vpiReg, vpiVectorVal},
           DefaultCase{VPIShapeType::BasicVector, vpiRegBit, vpiScalarVal},
           DefaultCase{VPIShapeType::PackedStructOneBit, vpiStructVar,
                       vpiVectorVal},
           DefaultCase{VPIShapeType::BasicVector, vpiTimeVar, vpiTimeVal},
           DefaultCase{VPIShapeType::BasicVector, vpiStringVar, vpiStringVal},
       }) {
    SCOPED_TRACE(testCase.type);
    Fixture local;
    local.database = makeVPIShapeDatabase(testCase.shape, testCase.type);
    local.execution.design_database = local.database.data();
    local.execution.design_database_size = local.database.size();
    context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&local.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    handle = vpi_handle_by_name(valueName, nullptr);
    ASSERT_NE(handle, nullptr);
    read = {};
    read.format = vpiObjTypeVal;
    vpi_get_value(handle, &read);
    EXPECT_EQ(read.format, testCase.format);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_release_handle(handle), 1);
    obelisk_rt_v1_context_destroy(context);
  }

  Fixture unsignedInteger;
  unsignedInteger.database =
      makeVPIShapeDatabase(VPIShapeType::BasicVector, vpiIntVar);
  unsignedInteger.execution.design_database = unsignedInteger.database.data();
  unsignedInteger.execution.design_database_size =
      unsignedInteger.database.size();
  context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&unsignedInteger.execution,
                                                    &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  handle = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(handle, nullptr);
  written.value.str = minusOne;
  EXPECT_EQ(vpi_put_value(handle, &written, nullptr, vpiNoDelay), nullptr);
  read = {};
  read.format = vpiDecStrVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, "255");
  read = {};
  read.format = vpiRealVal;
  vpi_get_value(handle, &read);
  EXPECT_DOUBLE_EQ(read.value.real, 255.0);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(context);

  // Decimal conversion is limb-based, so values wider than the host integer
  // types preserve their exact magnitude and two's-complement sign.
  Fixture wide;
  wide.database = makeDatabase();
  wide.execution.design_database = wide.database.data();
  wide.execution.design_database_size = wide.database.size();
  context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&wide.execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  handle = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(handle, nullptr);
  std::string twoTo64 = "1" + std::string(64, '0');
  written.value.str = twoTo64.data();
  EXPECT_EQ(vpi_put_value(handle, &written, nullptr, vpiNoDelay), nullptr);
  read = {};
  read.format = vpiDecStrVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, "18446744073709551616");
  EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(context);

  wide.database = makeDatabase();
  constexpr uint64_t wideTypeOffset = 336;
  put32(wide.database, wideTypeOffset + 4,
        OBELISK_RT_DESIGN_TYPE_SCALAR |
            ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE |
              OBELISK_RT_DESIGN_TYPE_SIGNED | OBELISK_RT_DESIGN_TYPE_PACKED)
             << 8));
  put64(wide.database, 32, imageChecksum(wide.database));
  wide.execution.design_database = wide.database.data();
  wide.execution.design_database_size = wide.database.size();
  context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&wide.execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  handle = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(handle, nullptr);
  std::string minusOneWide(65, '1');
  written.value.str = minusOneWide.data();
  EXPECT_EQ(vpi_put_value(handle, &written, nullptr, vpiNoDelay), nullptr);
  read = {};
  read.format = vpiDecStrVal;
  vpi_get_value(handle, &read);
  EXPECT_STREQ(read.value.str, "-1");
  read = {};
  read.format = vpiRealVal;
  vpi_get_value(handle, &read);
  EXPECT_DOUBLE_EQ(read.value.real, -1.0);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, NetStrengthReadsCanonicalStateOffsetWithoutChangingTiers) {
  constexpr uint64_t objectOffset = 240;
  Fixture fixture;
  fixture.bytecode = makeConnectedDriverBytecode();
  fixture.database = makeDatabase();
  put32(fixture.database, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_NET, vpiNet));
  // Source IDs and state coordinates are independent. A query that mistakes
  // this source identity for a stable state handle would inspect the wrong
  // connectivity component (or fail) instead of state bit 65.
  put64(fixture.database, objectOffset + 8, 999);
  put64(fixture.database, objectOffset + 80, 65);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.state_bit_count = 195;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  for (uint64_t bit = 130; bit != 195; ++bit) {
    const uint64_t mask = UINT64_C(1) << (bit % 64);
    context->stateValue[bit / 64] &= ~mask;
    context->stateUnknown[bit / 64] &= ~mask;
  }
  ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 130, 195),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char valueName[] = "top.value";
  vpiHandle net = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(net, nullptr);
  s_vpi_value read{};
  read.format = vpiStrengthVal;
  vpi_get_value(net, &read);
  ASSERT_NE(read.value.strength, nullptr);
  EXPECT_EQ(read.value.strength[0].logic, vpi0);
  EXPECT_EQ(read.value.strength[0].s0, vpiStrongDrive);
  EXPECT_EQ(read.value.strength[0].s1, vpiStrongDrive);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  vpiHandle selected = vpi_handle_by_index(net, 63);
  ASSERT_NE(selected, nullptr);
  read = {};
  read.format = vpiStrengthVal;
  vpi_get_value(selected, &read);
  ASSERT_NE(read.value.strength, nullptr);
  EXPECT_EQ(read.value.strength[0].logic, vpi0);
  EXPECT_EQ(read.value.strength[0].s0, vpiStrongDrive);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  EXPECT_EQ(vpi_release_handle(selected), 1);
  EXPECT_EQ(vpi_release_handle(net), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ScalarNetReadsPreserveAmbiguousLowAndHighStrengths) {
  constexpr uint64_t objectOffset = 240;
  Fixture fixture;
  fixture.bytecode = makeStrengthDriverBytecode();
  fixture.database = makeDatabase();
  put32(fixture.database, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_NET, vpiNet));
  put64(fixture.database, objectOffset + 8, 999);
  put64(fixture.database, objectOffset + 80, 0);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.state_bit_count = 260;
  fixture.execution.checksum = imageChecksum(fixture.bytecode);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  auto setState = [&](uint64_t offset, bool value, bool unknown) {
    const uint64_t mask = UINT64_C(1) << (offset % 64);
    if (value)
      context->stateValue[offset / 64] |= mask;
    else
      context->stateValue[offset / 64] &= ~mask;
    if (unknown)
      context->stateUnknown[offset / 64] |= mask;
    else
      context->stateUnknown[offset / 64] &= ~mask;
  };
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char valueName[] = "top.value";
  vpiHandle net = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(net, nullptr);
  s_vpi_value read{};
  read.format = vpiScalarVal;

  setState(65, false, true);
  setState(130, true, true);
  setState(195, true, true);
  ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 65, 196), OBELISK_RT_OK);
  vpi_get_value(net, &read);
  EXPECT_EQ(read.value.scalar, vpiL);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  setState(65, true, true);
  setState(130, false, true);
  ASSERT_EQ(obelisk_rt_resolve_design_drivers(context, 65, 196), OBELISK_RT_OK);
  vpi_get_value(net, &read);
  EXPECT_EQ(read.value.scalar, vpiH);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  EXPECT_EQ(vpi_release_handle(net), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, SemanticBackedValuesUseTheirBackingRepresentation) {
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t physicalTypeOffset = 336;
  constexpr uint64_t semanticTypesOffset = 496 + kSemanticDirectorySize;
  constexpr uint64_t semanticTypeOffset = semanticTypesOffset + 10 * 64;
  constexpr uint64_t semanticRootOffset =
      semanticTypesOffset + 11 * 64 + 10 * 24;
  struct Case {
    uint32_t objectType;
    uint32_t semanticKind;
    uint32_t publicTypespec;
    uint64_t width;
    PLI_INT32 defaultFormat;
  };
  for (const Case &testCase : {
           Case{vpiReg, OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL,
                vpiShortRealTypespec, 32, vpiRealVal},
           Case{vpiReg, OBELISK_RT_DESIGN_SEMANTIC_REAL, vpiRealTypespec, 64,
                vpiRealVal},
           Case{vpiReg, OBELISK_RT_DESIGN_SEMANTIC_STRING, vpiStringTypespec,
                64, vpiStringVal},
       }) {
    SCOPED_TRACE(testCase.objectType);
    Fixture fixture;
    fixture.database = makeSemanticTraversalDatabase();
    put32(fixture.database, objectOffset,
          designRecordKind(OBELISK_RT_DESIGN_RECORD_STORAGE,
                           testCase.objectType));
    put64(fixture.database, objectOffset + 56, testCase.width);
    put64(fixture.database, objectOffset + 64, testCase.width - 1);
    put64(fixture.database, objectOffset + 72, 0);
    put64(fixture.database, objectOffset + 80, 0);
    put32(fixture.database, physicalTypeOffset + 4,
          OBELISK_RT_DESIGN_TYPE_SCALAR);
    put64(fixture.database, physicalTypeOffset + 8, testCase.width);
    put64(fixture.database, physicalTypeOffset + 16, testCase.width - 1);
    put64(fixture.database, physicalTypeOffset + 24, 0);
    put32(fixture.database, semanticTypeOffset,
          testCase.semanticKind |
              (testCase.publicTypespec
               << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
    put32(fixture.database, semanticTypeOffset + 4, 0);
    put32(fixture.database, semanticTypeOffset + 8, 0);
    put64(fixture.database, semanticTypeOffset + 48, 0);
    put32(fixture.database, semanticRootOffset, 10);
    put64(fixture.database, 32, imageChecksum(fixture.database));
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    fixture.execution.state_bit_count = testCase.width;
    fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                              OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                              OBELISK_RT_EXECUTION_VPI_READ;

    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char valueName[] = "top.value";
    vpiHandle value = vpi_handle_by_name(valueName, nullptr);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(vpi_get(vpiType, value), testCase.objectType);
    s_vpi_value read{};
    read.format = testCase.defaultFormat;
    vpi_get_value(value, &read);
    if (testCase.defaultFormat == vpiRealVal)
      EXPECT_DOUBLE_EQ(read.value.real, 0.0);
    else {
      ASSERT_NE(read.value.str, nullptr);
      EXPECT_STREQ(read.value.str, "");
    }
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_release_handle(value), 1);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(VPI, GeneratedValuePoliciesRejectInvalidReadsBeforeStateAccess) {
  {
    Fixture fixture;
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

    char valueName[] = "top.value";
    vpiHandle valueHandle = vpi_handle_by_name(valueName, nullptr);
    ASSERT_NE(valueHandle, nullptr);

    s_vpi_value value{};
    value.format = vpiSuppressVal;
    value.value.integer = 73;
    vpi_get_value(valueHandle, &value);
    EXPECT_EQ(value.format, vpiSuppressVal);
    EXPECT_EQ(value.value.integer, 73);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
    EXPECT_STREQ(error.message,
                 "value format is not valid for this VPI object");

    vpi_get_value(valueHandle, nullptr);
    EXPECT_EQ(vpi_chk_error(&error), vpiError);
    EXPECT_STREQ(error.message, "VPI value destination is null");
    EXPECT_EQ(vpi_release_handle(valueHandle), 1);
    obelisk_rt_v1_context_destroy(context);
  }

  struct RejectedCase {
    VPIShapeType shape;
    uint32_t exactType;
    const char *message;
  };
  constexpr RejectedCase cases[] = {
      {VPIShapeType::UnpackedArrayOfScalar, vpiRegArray,
       "vpi_get_value is not defined for this VPI object"},
      // The compiler currently emits generic storage/net exact kinds. The
      // runtime must still reject the physical root shape before reading.
      {VPIShapeType::UnpackedArrayOfScalar, vpiReg,
       "vpi_get_value is not defined for a whole unpacked aggregate"},
      {VPIShapeType::UnpackedArrayOfVector, vpiNet,
       "vpi_get_value is not defined for a whole unpacked aggregate"},
      {VPIShapeType::UnpackedStruct, vpiStructVar,
       "vpi_get_value is not defined for a whole unpacked aggregate"},
      {VPIShapeType::UnpackedStruct, vpiReg,
       "vpi_get_value is not defined for a whole unpacked aggregate"},
      {VPIShapeType::UnpackedUnion, vpiNet,
       "vpi_get_value is not defined for a whole unpacked aggregate"},
  };
  for (const RejectedCase &testCase : cases) {
    SCOPED_TRACE(static_cast<unsigned>(testCase.shape));
    Fixture fixture;
    fixture.database = makeVPIShapeDatabase(testCase.shape, testCase.exactType);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char valueName[] = "top.value";
    vpiHandle valueHandle = vpi_handle_by_name(valueName, nullptr);
    ASSERT_NE(valueHandle, nullptr);
    s_vpi_value value{};
    value.format = vpiIntVal;
    value.value.integer = 91;
    vpi_get_value(valueHandle, &value);
    EXPECT_EQ(value.value.integer, 91);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
    EXPECT_STREQ(error.message, testCase.message);
    EXPECT_EQ(vpi_release_handle(valueHandle), 1);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST(VPI, ClassDefinitionValueRestrictionTracksHandleProvenance) {
  constexpr uint64_t scopeOffset = 176;
  Fixture fixture;
  fixture.database = makeDatabase();
  put32(fixture.database, scopeOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, vpiClassDefn));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  // Physical ownership is not the restriction in 37.29 detail 2: a static
  // member reached independently by hierarchical name remains readable.
  char absoluteMemberName[] = "top.value";
  vpiHandle direct = vpi_handle_by_name(absoluteMemberName, nullptr);
  ASSERT_NE(direct, nullptr);
  s_vpi_value directValue{};
  directValue.format = vpiIntVal;
  directValue.value.integer = 73;
  vpi_get_value(direct, &directValue);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(directValue.value.integer, 0);

  // The same object handle obtained relative to a class-definition handle is
  // provenance-restricted even though both paths resolve to one record.
  char className[] = "top";
  vpiHandle classDefinition = vpi_handle_by_name(className, nullptr);
  ASSERT_NE(classDefinition, nullptr);
  EXPECT_EQ(vpi_get(vpiType, classDefinition), vpiClassDefn);
  char relativeMemberName[] = "value";
  vpiHandle derived = vpi_handle_by_name(relativeMemberName, classDefinition);
  ASSERT_NE(derived, nullptr);
  s_vpi_value derivedValue{};
  derivedValue.format = vpiIntVal;
  derivedValue.value.integer = 91;
  vpi_get_value(derived, &derivedValue);
  EXPECT_EQ(derivedValue.value.integer, 91);
  s_vpi_error_info error{};
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message,
               "vpi_get_value is not defined for a variable or event handle "
               "obtained from a class definition");

  vpiHandle derivedBit = vpi_handle_by_index(derived, 64);
  ASSERT_NE(derivedBit, nullptr);
  s_vpi_value derivedBitValue{};
  derivedBitValue.format = vpiIntVal;
  derivedBitValue.value.integer = 92;
  vpi_get_value(derivedBit, &derivedBitValue);
  EXPECT_EQ(derivedBitValue.value.integer, 92);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message,
               "vpi_get_value is not defined for a variable or event handle "
               "obtained from a class definition");

  EXPECT_EQ(vpi_release_handle(derivedBit), 1);
  EXPECT_EQ(vpi_release_handle(derived), 1);
  EXPECT_EQ(vpi_release_handle(classDefinition), 1);
  EXPECT_EQ(vpi_release_handle(direct), 1);
  obelisk_rt_v1_context_destroy(context);
}

void installStatementDatabase(Fixture &fixture) {
  fixture.database = makeStatementDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;
}

TEST(VPI, TraversesProcessAndStatementRelationsWithExactTypes) {
  Fixture fixture;
  installStatementDatabase(fixture);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char childName[] = "top.child";
  char processName[] = "top.child.proc";
  vpiHandle child = vpi_handle_by_name(childName, nullptr);
  vpiHandle process = vpi_handle_by_name(processName, nullptr);
  ASSERT_NE(child, nullptr);
  ASSERT_NE(process, nullptr);
  EXPECT_EQ(vpi_get(vpiType, process), vpiInitial);

  // Generic hierarchy iteration streams the immutable sibling chain, while
  // exact process kinds are recovered from the relation source metadata.
  vpiHandle processes = vpi_iterate(vpiProcess, child);
  ASSERT_NE(processes, nullptr);
  vpiHandle scannedProcess = vpi_scan(processes);
  ASSERT_NE(scannedProcess, nullptr);
  EXPECT_EQ(vpi_compare_objects(process, scannedProcess), 1);
  EXPECT_EQ(vpi_scan(processes), nullptr);
  // vpiInitial is a concrete vpiType, not an iteration selector. Static
  // processes are selected through the generated vpiProcess relation.
  EXPECT_EQ(vpi_iterate(vpiInitial, child), nullptr);

  vpiHandle body = vpi_handle(vpiStmt, process);
  ASSERT_NE(body, nullptr);
  EXPECT_EQ(vpi_get(vpiType, body), vpiNamedBegin);
  EXPECT_EQ(vpi_get(vpiSize, body), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_STREQ(vpi_get_str(vpiName, body), "body");
  EXPECT_STREQ(vpi_get_str(vpiFullName, body), "top.child.body");
  EXPECT_STREQ(vpi_get_str(vpiFile, body), "test.sv");
  EXPECT_EQ(vpi_get(vpiLineNo, body), 8);
  vpiHandle statementScope = vpi_handle(vpiScope, body);
  ASSERT_NE(statementScope, nullptr);
  EXPECT_EQ(vpi_compare_objects(child, statementScope), 1);

  // The image records the LRM mode. A process has a singular vpiStmt, while
  // a begin block exposes its ordered children only through iteration.
  EXPECT_EQ(vpi_iterate(vpiStmt, process), nullptr);
  EXPECT_EQ(vpi_handle(vpiStmt, body), nullptr);
  vpiHandle statements = vpi_iterate(vpiStmt, body);
  ASSERT_NE(statements, nullptr);
  vpiHandle first = vpi_scan(statements);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(vpi_get(vpiType, first), vpiFor);
  EXPECT_EQ(vpi_get(vpiLineNo, first), 9);
  EXPECT_EQ(vpi_get_str(vpiName, first), nullptr);
  EXPECT_EQ(vpi_get_str(vpiFullName, first), nullptr);
  vpiHandle nestedScope = vpi_handle(vpiScope, first);
  ASSERT_NE(nestedScope, nullptr);
  EXPECT_EQ(vpi_compare_objects(body, nestedScope), 1);
  EXPECT_EQ(vpi_scan(statements), nullptr);

  vpiHandle second = vpi_handle(vpiStmt, first);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(vpi_get(vpiType, second), vpiNullStmt);
  EXPECT_EQ(vpi_get(vpiLineNo, second), 10);
  EXPECT_EQ(vpi_get_str(vpiName, second), nullptr);
  // A for statement is a scope only when its occurrence declares a loop
  // variable. This declaration-free loop is skipped in favor of its named
  // enclosing block.
  vpiHandle secondScope = vpi_handle(vpiScope, second);
  ASSERT_NE(secondScope, nullptr);
  EXPECT_EQ(vpi_compare_objects(body, secondScope), 1);
  EXPECT_EQ(vpi_scan(statements), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);

  vpiHandle released = vpi_iterate(vpiStmt, body);
  ASSERT_NE(released, nullptr);
  EXPECT_EQ(vpi_release_handle(released), 1);
  EXPECT_EQ(vpi_scan(released), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);

  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, UsesIntrinsicKindsWithoutOutgoingRelations) {
  struct ProcessCase {
    uint32_t encodedKind;
    PLI_INT32 expectedKind;
    bool staticProcess;
  };
  for (ProcessCase processCase : {
           ProcessCase{vpiInitial, vpiInitial, true},
           ProcessCase{vpiFinal, vpiFinal, true},
           ProcessCase{vpiAlways, vpiAlways, true},
           ProcessCase{vpiTask, vpiTask, false},
           ProcessCase{0, vpiUndefined, false},
       }) {
    SCOPED_TRACE(processCase.expectedKind);
    Fixture fixture;
    fixture.database = makeCodeUnitDatabase();
    put32(fixture.database, 240,
          designRecordKind(OBELISK_RT_DESIGN_RECORD_PROCESS,
                           processCase.encodedKind));
    put32(fixture.database, 244,
          processCase.encodedKind == 0
              ? static_cast<uint32_t>(OBELISK_RT_DESIGN_CAP_INTERNAL)
              : 0);
    put64(fixture.database, 32, imageChecksum(fixture.database));
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                              OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                              OBELISK_RT_EXECUTION_VPI_READ;
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char rootName[] = "top";
    char processName[] = "top.proc";
    vpiHandle root = vpi_handle_by_name(rootName, nullptr);
    vpiHandle process = vpi_handle_by_name(processName, nullptr);
    ASSERT_NE(root, nullptr);
    if (processCase.expectedKind == vpiUndefined) {
      EXPECT_EQ(process, nullptr);
      obelisk_rt_v1_context_destroy(context);
      continue;
    }
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(vpi_get(vpiType, process), processCase.expectedKind);
    vpiHandle processes = vpi_iterate(vpiProcess, root);
    if (processCase.staticProcess) {
      ASSERT_NE(processes, nullptr);
      EXPECT_EQ(vpi_compare_objects(process, vpi_scan(processes)), 1);
      EXPECT_EQ(vpi_scan(processes), nullptr);
    } else {
      EXPECT_EQ(processes, nullptr);
    }
    if (processCase.expectedKind == vpiTask) {
      vpiHandle taskFunctions = vpi_iterate(vpiTaskFunc, root);
      ASSERT_NE(taskFunctions, nullptr);
      EXPECT_EQ(vpi_compare_objects(process, vpi_scan(taskFunctions)), 1);
      EXPECT_EQ(vpi_get(vpiType, vpi_scan(taskFunctions)), vpiFunction);
      EXPECT_EQ(vpi_scan(taskFunctions), nullptr);
    }
    EXPECT_EQ(vpi_iterate(processCase.expectedKind, root), nullptr);
    obelisk_rt_v1_context_destroy(context);
  }

  for (uint32_t scopeKind :
       {uint32_t{vpiModule}, uint32_t{vpiInterface}, uint32_t{vpiProgram}}) {
    SCOPED_TRACE(scopeKind);
    Fixture fixture;
    fixture.database = makeCodeUnitDatabase();
    put32(fixture.database, 176,
          designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, scopeKind));
    put64(fixture.database, 32, imageChecksum(fixture.database));
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                              OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                              OBELISK_RT_EXECUTION_VPI_READ;
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char rootName[] = "top";
    vpiHandle root = vpi_handle_by_name(rootName, nullptr);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(vpi_get(vpiType, root), scopeKind);
    // No statement relation is present; source dispatch still uses the
    // intrinsic interface/program/module kind.
    vpiHandle processes = vpi_iterate(vpiProcess, root);
    ASSERT_NE(processes, nullptr);
    EXPECT_EQ(vpi_get(vpiType, vpi_scan(processes)), vpiInitial);
    EXPECT_EQ(vpi_scan(processes), nullptr);
    obelisk_rt_v1_context_destroy(context);
  }

  Fixture fixture;
  installStatementDatabase(fixture);
  put32(fixture.database, 240,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, vpiInterface));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char rootName[] = "top";
  char childName[] = "top.child";
  vpiHandle root = vpi_handle_by_name(rootName, nullptr);
  vpiHandle child = vpi_handle_by_name(childName, nullptr);
  ASSERT_NE(root, nullptr);
  ASSERT_NE(child, nullptr);
  EXPECT_EQ(vpi_get(vpiType, child), vpiInterface);
  EXPECT_EQ(vpi_iterate(vpiModule, root), nullptr);
  vpiHandle interfaces = vpi_iterate(vpiInterface, root);
  ASSERT_NE(interfaces, nullptr);
  EXPECT_EQ(vpi_compare_objects(child, vpi_scan(interfaces)), 1);
  EXPECT_EQ(vpi_scan(interfaces), nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, BuildsStatementNamesFromCanonicalScopeOwners) {
  auto check = [](uint32_t processKind, uint32_t scopeKind,
                  const char *expectedFullName, bool expectProcessScope) {
    Fixture fixture;
    installStatementDatabase(fixture);
    constexpr size_t childScope = 240;
    constexpr size_t process = 304;
    constexpr size_t relations = 568;
    put32(fixture.database, childScope,
          designRecordKind(OBELISK_RT_DESIGN_RECORD_SCOPE, scopeKind));
    put32(fixture.database, process,
          designRecordKind(OBELISK_RT_DESIGN_RECORD_PROCESS, processKind));
    put16(fixture.database, relations + 14,
          designRelationSource(1, static_cast<uint16_t>(processKind)));
    put64(fixture.database, 32, imageChecksum(fixture.database));

    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
    char processName[] = "top.child.proc";
    vpiHandle codeUnit = vpi_handle_by_name(processName, nullptr);
    ASSERT_NE(codeUnit, nullptr);
    vpiHandle body = vpi_handle(vpiStmt, codeUnit);
    ASSERT_NE(body, nullptr);
    EXPECT_STREQ(vpi_get_str(vpiFullName, body), expectedFullName);
    vpiHandle scope = vpi_handle(vpiScope, body);
    ASSERT_NE(scope, nullptr);
    if (expectProcessScope) {
      EXPECT_EQ(vpi_compare_objects(codeUnit, scope), 1);
      EXPECT_EQ(vpi_get(vpiType, scope), static_cast<PLI_INT32>(processKind));
      EXPECT_STREQ(vpi_get_str(vpiName, scope), "proc");
      EXPECT_STREQ(vpi_get_str(vpiFullName, scope), "top.child.proc");
      vpiHandle ownerScope = vpi_handle(vpiScope, scope);
      ASSERT_NE(ownerScope, nullptr);
      EXPECT_EQ(vpi_get(vpiType, ownerScope),
                static_cast<PLI_INT32>(scopeKind));
    } else {
      EXPECT_EQ(vpi_get(vpiType, scope), static_cast<PLI_INT32>(scopeKind));
    }
    obelisk_rt_v1_context_destroy(context);
  };

  check(vpiTask, vpiModule, "top.child.proc.body", true);
  check(vpiInitial, vpiPackage, "top.child::body", false);
}

TEST(VPI, HonorsPerOccurrenceStatementScopes) {
  Fixture fixture;
  installStatementDatabase(fixture);
  put16(fixture.database, 400 + 36, vpiForeachStmt);
  put16(fixture.database, 400 + 38, OBELISK_RT_DESIGN_STATEMENT_SCOPE);
  put32(fixture.database, 400 + 24, 0);
  put32(fixture.database, 520 + 8, 1);
  put16(fixture.database, 520 + 12, 1);
  put32(fixture.database, 520 + 16 + 8, 1);
  put16(fixture.database, 520 + 16 + 12, 2);
  put16(fixture.database, 568 + 16 + 14,
        designRelationSource(2, vpiForeachStmt));
  put64(fixture.database, 152, 2);
  put64(fixture.database, 32, imageChecksum(fixture.database));

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char processName[] = "top.child.proc";
  vpiHandle process = vpi_handle_by_name(processName, nullptr);
  ASSERT_NE(process, nullptr);
  vpiHandle body = vpi_handle(vpiStmt, process);
  ASSERT_NE(body, nullptr);
  vpiHandle loop = vpi_handle(vpiStmt, body);
  ASSERT_NE(loop, nullptr);
  vpiHandle nested = vpi_handle(vpiStmt, loop);
  ASSERT_NE(nested, nullptr);
  vpiHandle scope = vpi_handle(vpiScope, nested);
  ASSERT_NE(scope, nullptr);
  EXPECT_EQ(vpi_compare_objects(body, scope), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, RejectsScopeTraversalForStatementHelperKinds) {
  Fixture fixture;
  installStatementDatabase(fixture);
  constexpr size_t statements = 400;
  constexpr size_t sites = 520;
  constexpr size_t relations = 568;

  put16(fixture.database, statements + 40 + 36, vpiCase);
  put16(fixture.database, statements + 80 + 36, vpiCaseItem);
  put16(fixture.database, sites + 16 + 12, 0);
  put64(fixture.database, 152, 2);
  put16(fixture.database, relations + 32 + 12, vpiCaseItem);
  put16(fixture.database, relations + 32 + 14,
        designRelationSource(2, vpiCase, true));
  put64(fixture.database, 32, imageChecksum(fixture.database));

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char processName[] = "top.child.proc";
  vpiHandle process = vpi_handle_by_name(processName, nullptr);
  ASSERT_NE(process, nullptr);
  vpiHandle body = vpi_handle(vpiStmt, process);
  ASSERT_NE(body, nullptr);
  vpiHandle caseStatement = vpi_scan(vpi_iterate(vpiStmt, body));
  ASSERT_NE(caseStatement, nullptr);
  vpiHandle caseItem = vpi_scan(vpi_iterate(vpiCaseItem, caseStatement));
  ASSERT_NE(caseItem, nullptr);
  EXPECT_EQ(vpi_handle(vpiScope, caseItem), nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, PreservesDualHandleAndIterateStatementSemantics) {
  Fixture fixture;
  installStatementDatabase(fixture);
  constexpr size_t statements = 400;
  constexpr size_t sites = 520;
  constexpr size_t relations = 568;
  put16(fixture.database, statements + 36, vpiFor);
  put16(fixture.database, statements + 38, 0);
  put32(fixture.database, statements + 24, 0);
  put16(fixture.database, statements + 40 + 36, vpiNullStmt);
  put32(fixture.database, statements + 80 + 16, 0);
  put64(fixture.database, 152, 2);
  put32(fixture.database, sites + 8, 0);
  put16(fixture.database, sites + 12, 1);
  put32(fixture.database, sites + 16 + 8, 0);
  put16(fixture.database, sites + 16 + 12, 2);
  put16(fixture.database, relations + 16 + 12, vpiForInitStmt);
  put16(fixture.database, relations + 16 + 14, designRelationSource(2, vpiFor));
  put16(fixture.database, relations + 32 + 12, vpiForInitStmt);
  put16(fixture.database, relations + 32 + 14,
        designRelationSource(2, vpiFor, true));
  put32(fixture.database, relations + 32, 0);
  put32(fixture.database, relations + 32 + 4, (uint32_t{2} << 30) | 1);
  put32(fixture.database, relations + 32 + 8, 0);
  put64(fixture.database, 136, 2);
  put64(fixture.database, 32, imageChecksum(fixture.database));

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char processName[] = "top.child.proc";
  vpiHandle process = vpi_handle_by_name(processName, nullptr);
  ASSERT_NE(process, nullptr);
  vpiHandle loop = vpi_handle(vpiStmt, process);
  ASSERT_NE(loop, nullptr);
  EXPECT_EQ(vpi_get(vpiType, loop), vpiFor);

  vpiHandle singular = vpi_handle(vpiForInitStmt, loop);
  ASSERT_NE(singular, nullptr);
  vpiHandle iterator = vpi_iterate(vpiForInitStmt, loop);
  ASSERT_NE(iterator, nullptr);
  vpiHandle iteratorUse = vpi_handle(vpiUse, iterator);
  ASSERT_NE(iteratorUse, nullptr);
  EXPECT_EQ(vpi_compare_objects(iteratorUse, loop), 1);
  EXPECT_EQ(vpi_get(vpiType, iteratorUse), vpiFor);
  EXPECT_EQ(vpi_release_handle(iteratorUse), 1);
  vpiHandle first = vpi_scan(iterator);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(vpi_compare_objects(singular, first), 1);
  EXPECT_EQ(vpi_get(vpiType, first), vpiNullStmt);
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, TraversesScopeOwnedStatementRelations) {
  Fixture fixture;
  installStatementDatabase(fixture);
  constexpr size_t statements = 400;
  constexpr size_t relations = 568;
  for (size_t index = 0; index != 3; ++index) {
    size_t statement = statements + index * 40;
    put32(fixture.database, statement + 8, UINT32_MAX);
    put32(fixture.database, statement + 16, UINT32_MAX);
    put32(fixture.database, statement + 24, 0);
    put16(fixture.database, statement + 38, 0);
  }
  put16(fixture.database, statements + 36, vpiContAssign);
  put16(fixture.database, statements + 40 + 36, vpiContAssign);
  put16(fixture.database, statements + 80 + 36, vpiAliasStmt);
  put64(fixture.database, 152, 0);
  auto relation = [&](size_t index, uint32_t target, uint32_t ordinal,
                      uint16_t selector) {
    size_t offset = relations + index * 16;
    put32(fixture.database, offset, 1);
    put32(fixture.database, offset + 4, (uint32_t{2} << 30) | target);
    put32(fixture.database, offset + 8, ordinal);
    put16(fixture.database, offset + 12, selector);
    put16(fixture.database, offset + 14,
          designRelationSource(0, vpiModule, true));
  };
  relation(0, 0, 0, vpiContAssign);
  relation(1, 1, 1, vpiContAssign);
  relation(2, 2, 0, vpiAliasStmt);
  put64(fixture.database, 32, imageChecksum(fixture.database));

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char childName[] = "top.child";
  vpiHandle child = vpi_handle_by_name(childName, nullptr);
  ASSERT_NE(child, nullptr);
  EXPECT_EQ(vpi_handle(vpiContAssign, child), nullptr);
  vpiHandle assigns = vpi_iterate(vpiContAssign, child);
  ASSERT_NE(assigns, nullptr);
  EXPECT_EQ(vpi_get(vpiType, vpi_scan(assigns)), vpiContAssign);
  EXPECT_EQ(vpi_get(vpiType, vpi_scan(assigns)), vpiContAssign);
  EXPECT_EQ(vpi_scan(assigns), nullptr);
  vpiHandle aliases = vpi_iterate(vpiAliasStmt, child);
  ASSERT_NE(aliases, nullptr);
  EXPECT_EQ(vpi_get(vpiType, vpi_scan(aliases)), vpiAliasStmt);
  EXPECT_EQ(vpi_scan(aliases), nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, TraversesRelationsToScopeAndObjectRecords) {
  Fixture fixture;
  installStatementDatabase(fixture);
  constexpr size_t relations = 568;

  // IEEE 1800-2017 37.61: a process has a singular vpiModule relation.
  put64(fixture.database, 168, 1);
  put32(fixture.database, relations, 0);
  put32(fixture.database, relations + 4, 1); // Scope table, child module.
  put32(fixture.database, relations + 8, 0);
  put16(fixture.database, relations + 12, vpiModule);
  put16(fixture.database, relations + 14, designRelationSource(1, vpiInitial));
  put64(fixture.database, 32, imageChecksum(fixture.database));

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char processName[] = "top.child.proc";
  vpiHandle process = vpi_handle_by_name(processName, nullptr);
  ASSERT_NE(process, nullptr);
  vpiHandle module = vpi_handle(vpiModule, process);
  ASSERT_NE(module, nullptr);
  EXPECT_EQ(vpi_get(vpiType, module), vpiModule);
  EXPECT_STREQ(vpi_get_str(vpiFullName, module), "top.child");
  obelisk_rt_v1_context_destroy(context);

  // IEEE 1800-2017 37.5: a module iterates its static processes. Reuse the
  // same image storage with a target in the immutable object table.
  installStatementDatabase(fixture);
  put64(fixture.database, 168, 1);
  put32(fixture.database, relations, 1);
  put32(fixture.database, relations + 4, uint32_t{1} << 30);
  put32(fixture.database, relations + 8, 0);
  put16(fixture.database, relations + 12, vpiProcess);
  put16(fixture.database, relations + 14,
        designRelationSource(0, vpiModule, true));
  put64(fixture.database, 32, imageChecksum(fixture.database));

  context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char moduleName[] = "top.child";
  module = vpi_handle_by_name(moduleName, nullptr);
  ASSERT_NE(module, nullptr);
  vpiHandle processes = vpi_iterate(vpiProcess, module);
  ASSERT_NE(processes, nullptr);
  EXPECT_EQ(vpi_get(vpiIteratorType, processes), vpiProcess);
  process = vpi_scan(processes);
  ASSERT_NE(process, nullptr);
  EXPECT_EQ(vpi_get(vpiType, process), vpiInitial);
  EXPECT_STREQ(vpi_get_str(vpiFullName, process), "top.child.proc");
  EXPECT_EQ(vpi_scan(processes), nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, SeparatesHandleAndIterateModesForSameSelector) {
  Fixture fixture;
  fixture.database = makeNestedModuleRelationDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char moduleName[] = "top.child";
  vpiHandle module = vpi_handle_by_name(moduleName, nullptr);
  ASSERT_NE(module, nullptr);

  // IEEE 1800-2017 37.5 assigns different meanings to these same-selector
  // modes: the singular edge names the containing module while iteration
  // enumerates child modules.
  vpiHandle containing = vpi_handle(vpiModule, module);
  ASSERT_NE(containing, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, containing), "top");
  vpiHandle elements = vpi_iterate(vpiModule, module);
  ASSERT_NE(elements, nullptr);
  vpiHandle firstElement = vpi_scan(elements);
  ASSERT_NE(firstElement, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, firstElement), "top.child.leaf");
  EXPECT_EQ(vpi_compare_objects(containing, firstElement), 0);
  EXPECT_EQ(vpi_scan(elements), nullptr);
  obelisk_rt_v1_context_destroy(context);

  // An entirely absent automatic parent group uses the same immutable
  // hierarchy fallback as an absent automatic child group.
  fixture.database = makeNestedModuleRelationDatabase();
  constexpr size_t relationOffset = 368;
  std::memcpy(fixture.database.data() + relationOffset,
              fixture.database.data() + relationOffset + 16, 16);
  put64(fixture.database, 168, 1);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  module = vpi_handle_by_name(moduleName, nullptr);
  ASSERT_NE(module, nullptr);
  containing = vpi_handle(vpiModule, module);
  ASSERT_NE(containing, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, containing), "top");
  elements = vpi_iterate(vpiModule, module);
  ASSERT_NE(elements, nullptr);
  firstElement = vpi_scan(elements);
  ASSERT_NE(firstElement, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, firstElement), "top.child.leaf");
  EXPECT_EQ(vpi_scan(elements), nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, RejectsAutomaticRelationsThatDisagreeWithOwnership) {
  Fixture fixture;
  installStatementDatabase(fixture);
  constexpr size_t relations = 568;

  // A module cannot claim itself as a direct child.
  put64(fixture.database, 168, 1);
  put32(fixture.database, relations, 1);
  put32(fixture.database, relations + 4, 1);
  put32(fixture.database, relations + 8, 0);
  put16(fixture.database, relations + 12, vpiModule);
  put16(fixture.database, relations + 14,
        designRelationSource(0, vpiModule, true));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  obelisk_rt_context *context = nullptr;
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_INVALID_DESIGN);

  // A process must name its actual owning scope, not another same-kind scope.
  installStatementDatabase(fixture);
  put64(fixture.database, 168, 1);
  put32(fixture.database, relations, 0);
  put32(fixture.database, relations + 4, 0);
  put32(fixture.database, relations + 8, 0);
  put16(fixture.database, relations + 12, vpiModule);
  put16(fixture.database, relations + 14, designRelationSource(1, vpiInitial));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_INVALID_DESIGN);

  // A statement must name its effective lexical scope. A same-kind physical
  // scope cannot replace the nearest scope-bearing parent statement.
  installStatementDatabase(fixture);
  put64(fixture.database, 136, 2);
  put64(fixture.database, 168, 3);
  put32(fixture.database, relations, 0);
  put32(fixture.database, relations + 4, (uint32_t{2} << 30) | 0);
  put32(fixture.database, relations + 8, 0);
  put16(fixture.database, relations + 12, vpiStmt);
  put16(fixture.database, relations + 14, designRelationSource(1, vpiInitial));
  put32(fixture.database, relations + 16, 0);
  put32(fixture.database, relations + 16 + 4, (uint32_t{2} << 30) | 1);
  put32(fixture.database, relations + 16 + 8, 0);
  put16(fixture.database, relations + 16 + 12, vpiStmt);
  put16(fixture.database, relations + 16 + 14,
        designRelationSource(2, vpiNamedBegin, true));
  put32(fixture.database, relations + 32, 1);
  put32(fixture.database, relations + 32 + 4, 1); // Wrong: physical child.
  put32(fixture.database, relations + 32 + 8, 0);
  put16(fixture.database, relations + 32 + 12, vpiScope);
  put16(fixture.database, relations + 32 + 14, designRelationSource(2, vpiFor));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_INVALID_DESIGN);

  // The exact lexical target is accepted and returned by the relation-backed
  // query path.
  put32(fixture.database, relations + 32 + 4, (uint32_t{2} << 30) | 0);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char processName[] = "top.child.proc";
  vpiHandle process = vpi_handle_by_name(processName, nullptr);
  ASSERT_NE(process, nullptr);
  vpiHandle body = vpi_handle(vpiStmt, process);
  ASSERT_NE(body, nullptr);
  vpiHandle nested = vpi_scan(vpi_iterate(vpiStmt, body));
  ASSERT_NE(nested, nullptr);
  vpiHandle lexicalScope = vpi_handle(vpiScope, nested);
  ASSERT_NE(lexicalScope, nullptr);
  EXPECT_EQ(vpi_compare_objects(body, lexicalScope), 1);
  obelisk_rt_v1_context_destroy(context);

  // Dense ordinals cannot duplicate a valid child and hide the remainder.
  installStatementDatabase(fixture);
  put64(fixture.database, 168, 2);
  for (size_t index = 0; index != 2; ++index) {
    size_t relation = relations + index * 16;
    put32(fixture.database, relation, 1);
    put32(fixture.database, relation + 4, uint32_t{1} << 30);
    put32(fixture.database, relation + 8, static_cast<uint32_t>(index));
    put16(fixture.database, relation + 12, vpiProcess);
    put16(fixture.database, relation + 14,
          designRelationSource(0, vpiModule, true));
  }
  put64(fixture.database, 32, imageChecksum(fixture.database));
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_INVALID_DESIGN);
}

TEST(VPI, ConvertsValuesAndEnforcesMutationCapabilities) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char objectName[] = "top.value";
  vpiHandle object = vpi_handle_by_name(objectName, nullptr);
  ASSERT_NE(object, nullptr);

  s_vpi_value value{};
  value.format = vpiIntVal;
  value.value.integer = -1;
  EXPECT_EQ(vpi_put_value(object, &value, nullptr, vpiNoDelay), nullptr);
  value.value.integer = 0;
  vpi_get_value(object, &value);
  EXPECT_EQ(value.value.integer, -1);

  value = {};
  value.format = vpiVectorVal;
  vpi_get_value(object, &value);
  ASSERT_NE(value.value.vector, nullptr);
  EXPECT_EQ(value.value.vector[0].aval, UINT32_MAX);
  EXPECT_EQ(value.value.vector[0].bval, 0u);
  EXPECT_EQ(value.value.vector[2].aval, 1u);
  EXPECT_EQ(value.value.vector[2].bval, 0u);

  value = {};
  value.format = vpiScalarVal;
  value.value.scalar = vpiX;
  vpi_put_value(object, &value, nullptr, vpiNoDelay);
  value.value.scalar = vpi0;
  vpi_get_value(object, &value);
  EXPECT_EQ(value.value.scalar, vpiX);
  value = {};
  value.format = vpiIntVal;
  vpi_get_value(object, &value);
  EXPECT_EQ(value.value.integer, 0);

  char binary[] = "1_0xz?";
  value = {};
  value.format = vpiBinStrVal;
  value.value.str = binary;
  vpi_put_value(object, &value, nullptr, vpiForceFlag);
  value.value.str = nullptr;
  vpi_get_value(object, &value);
  ASSERT_NE(value.value.str, nullptr);
  const char *valueString = value.value.str;
  std::string formatted(value.value.str);
  ASSERT_GE(formatted.size(), 5u);
  EXPECT_EQ(formatted.substr(formatted.size() - 5), "10xzz");
  const char *propertyString = vpi_get_str(vpiFullName, object);
  ASSERT_NE(propertyString, nullptr);
  EXPECT_STREQ(propertyString, "top.value");
  EXPECT_STREQ(valueString, formatted.c_str());
  vpi_get_value(object, &value);
  EXPECT_STREQ(propertyString, "top.value");
  vpi_put_value(object, nullptr, nullptr, vpiReleaseFlag);

  char invalidBinary[] = "2";
  value.value.str = invalidBinary;
  vpi_put_value(object, &value, nullptr, vpiNoDelay);
  s_vpi_error_info error{};
  EXPECT_EQ(vpi_chk_error(&error), vpiError);
  EXPECT_STREQ(error.message, "invalid binary digit in VPI write");
  vpi_put_value(object, &value, nullptr, 2);
  EXPECT_EQ(vpi_chk_error(&error), vpiError);

  s_vpi_vlog_info info{};
  EXPECT_EQ(vpi_get_vlog_info(&info), 1);
  ASSERT_EQ(info.argc, 1);
  ASSERT_NE(info.argv, nullptr);
  EXPECT_STREQ(info.argv[0], "obelisk");
  EXPECT_STREQ(info.product, "Obelisk");
  EXPECT_STREQ(info.version, "prototype");
  EXPECT_EQ(vpi_get_vlog_info(nullptr), 0);
  EXPECT_EQ(vpi_release_handle(object), 1);
  obelisk_rt_v1_context_destroy(context);

  Fixture readOnly;
  readOnly.database = makeDatabase(false);
  readOnly.execution.design_database = readOnly.database.data();
  readOnly.execution.design_database_size = readOnly.database.size();
  readOnly.execution.flags &= ~OBELISK_RT_EXECUTION_VPI_WRITE;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&readOnly.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  object = vpi_handle_by_name(objectName, nullptr);
  ASSERT_NE(object, nullptr);
  value = {};
  value.format = vpiIntVal;
  value.value.integer = 7;
  vpi_put_value(object, &value, nullptr, vpiNoDelay);
  EXPECT_EQ(vpi_chk_error(&error), vpiError);
  EXPECT_STREQ(error.message, "VPI mutation requires --vpi=full");
  EXPECT_EQ(vpi_release_handle(object), 1);
  obelisk_rt_v1_context_destroy(context);

  Fixture noSource;
  noSource.database = makeDatabase(true, false);
  noSource.execution.design_database = noSource.database.data();
  noSource.execution.design_database_size = noSource.database.size();
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&noSource.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  object = vpi_handle_by_name(objectName, nullptr);
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(vpi_get_str(vpiFile, object), nullptr);
  EXPECT_EQ(vpi_get(vpiLineNo, object), 0);
  EXPECT_EQ(vpi_release_handle(object), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ReportsAnOwnedSnapshotOfInvocationArguments) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  std::array<std::string, 4> storage{"obelisk-sim", "+UVM_TESTNAME=smoke",
                                     "--seed=41", "design.cfg"};
  std::array<const char *, 4> arguments{};
  for (size_t index = 0; index != storage.size(); ++index)
    arguments[index] = storage[index].c_str();
  ASSERT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(arguments.size()), arguments.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  s_vpi_vlog_info info{};
  ASSERT_EQ(vpi_get_vlog_info(&info), 1);
  ASSERT_EQ(info.argc, static_cast<PLI_INT32>(arguments.size()));
  ASSERT_NE(info.argv, nullptr);
  for (size_t index = 0; index != storage.size(); ++index)
    EXPECT_STREQ(info.argv[index], storage[index].c_str());
  EXPECT_STREQ(info.product, "Obelisk");
  EXPECT_STREQ(info.version, "prototype");

  for (std::string &argument : storage)
    argument.assign("changed");
  EXPECT_STREQ(info.argv[0], "obelisk-sim");
  EXPECT_STREQ(info.argv[1], "+UVM_TESTNAME=smoke");
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ExportsEveryReadQueryRoutineDeclaredByThePublicHeader) {
  EXPECT_NE(&vpi_chk_error, nullptr);
  EXPECT_NE(&vpi_compare_objects, nullptr);
  EXPECT_NE(&vpi_get, nullptr);
  EXPECT_NE(&vpi_get64, nullptr);
  EXPECT_NE(&vpi_get_cb_info, nullptr);
  EXPECT_NE(&vpi_get_data, nullptr);
  EXPECT_NE(&vpi_get_delays, nullptr);
  EXPECT_NE(&vpi_get_str, nullptr);
  EXPECT_NE(&vpi_get_systf_info, nullptr);
  EXPECT_NE(&vpi_get_time, nullptr);
  EXPECT_NE(&vpi_get_userdata, nullptr);
  EXPECT_NE(&vpi_get_value, nullptr);
  EXPECT_NE(&vpi_get_value_array, nullptr);
  EXPECT_NE(&vpi_get_vlog_info, nullptr);
  EXPECT_NE(&vpi_handle, nullptr);
  EXPECT_NE(&vpi_handle_by_index, nullptr);
  EXPECT_NE(&vpi_handle_by_multi_index, nullptr);
  EXPECT_NE(&vpi_handle_by_name, nullptr);
  EXPECT_NE(&vpi_handle_multi, nullptr);
  EXPECT_NE(&vpi_iterate, nullptr);
  EXPECT_NE(&vpi_mcd_name, nullptr);
  EXPECT_NE(&vpi_release_handle, nullptr);
  EXPECT_NE(&vpi_scan, nullptr);
  EXPECT_NE(&vpi_free_object, nullptr);
}

TEST(VPI, UnbackedReadQueryRoutinesReportDeterministicErrors) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char name[] = "top.value";
  vpiHandle object = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(object, nullptr);
  s_vpi_error_info error{};

  s_vpi_systf_data systf{};
  vpi_get_systf_info(object, &systf);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message,
               "VPI system task/function metadata is unavailable");

  EXPECT_EQ(vpi_get_data(1, nullptr, 0), 0);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message, "VPI save/restart data is unavailable");

  s_vpi_delay delay{};
  vpi_get_delays(object, &delay);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message, "VPI delay metadata is unavailable");

  s_vpi_arrayvalue array{};
  array.value.rawvals = reinterpret_cast<PLI_BYTE8 *>(uintptr_t{1});
  vpi_get_value_array(object, &array, nullptr, 0);
  EXPECT_EQ(array.value.rawvals, nullptr);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message,
               "VPI array query requires an unpacked array object");

  EXPECT_EQ(vpi_handle_multi(vpiInterModPath, object, object), nullptr);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message, "VPI intermodule path metadata is unavailable");

  EXPECT_EQ(vpi_mcd_name(1), nullptr);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message,
               "VPI multichannel descriptor names are unavailable");
  EXPECT_EQ(vpi_release_handle(object), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ValueResultStorageOutlivesHandlesAndIsSharedAcrossQueries) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char objectName[] = "top.value";
  vpiHandle first = vpi_handle_by_name(objectName, nullptr);
  vpiHandle second = vpi_handle_by_name(objectName, nullptr);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(first, second);

  s_vpi_value firstValue{};
  firstValue.format = vpiVectorVal;
  vpi_get_value(first, &firstValue);
  ASSERT_NE(firstValue.value.vector, nullptr);
  std::array<s_vpi_vecval, 3> saved{};
  for (size_t index = 0; index != saved.size(); ++index)
    saved[index] = firstValue.value.vector[index];

  s_vpi_value secondValue{};
  secondValue.format = vpiVectorVal;
  vpi_get_value(second, &secondValue);
  ASSERT_NE(secondValue.value.vector, nullptr);
  // The two live handles must share the routine-family result storage.  A
  // per-handle buffer cannot satisfy this equality while both handles live.
  EXPECT_EQ(secondValue.value.vector, firstValue.value.vector);
  for (size_t index = 0; index != saved.size(); ++index) {
    EXPECT_EQ(secondValue.value.vector[index].aval, saved[index].aval);
    EXPECT_EQ(secondValue.value.vector[index].bval, saved[index].bval);
  }

  EXPECT_EQ(vpi_release_handle(first), 1);
  EXPECT_EQ(vpi_release_handle(second), 1);
  // Releasing every originating handle is not a vpi_get_value call and must
  // not invalidate the last result buffer.
  for (size_t index = 0; index != saved.size(); ++index) {
    EXPECT_EQ(secondValue.value.vector[index].aval, saved[index].aval);
    EXPECT_EQ(secondValue.value.vector[index].bval, saved[index].bval);
  }
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, QueriesSimulationAndObjectTimeWithoutSchedulerRegistration) {
  Fixture fixture;
  static constexpr char rootName[] = "$root";
  static constexpr char topName[] = "top";
  const obelisk_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, rootName, sizeof(rootName) - 1, -12, -12, 0},
      {1, 0, topName, sizeof(topName) - 1, -9, -12, 0},
  };
  fixture.execution.dpi_scopes = scopes;
  fixture.execution.dpi_scope_count = std::size(scopes);
  fixture.execution.dpi_time_precision = -12;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  context->schedulerTime = UINT64_C(0x123456789abcdef0);

  EXPECT_EQ(vpi_get(vpiTimeUnit, nullptr), -12);
  EXPECT_EQ(vpi_get(vpiTimePrecision, nullptr), -12);

  char top[] = "top";
  char valueName[] = "top.value";
  vpiHandle module = vpi_handle_by_name(top, nullptr);
  vpiHandle value = vpi_handle_by_name(valueName, nullptr);
  ASSERT_NE(module, nullptr);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(vpi_get(vpiTimeUnit, module), -9);
  EXPECT_EQ(vpi_get(vpiTimePrecision, module), -12);
  EXPECT_EQ(vpi_get(vpiTimeUnit, value), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);

  s_vpi_time time{};
  time.type = vpiSimTime;
  vpi_get_time(nullptr, &time);
  EXPECT_EQ(time.high, UINT32_C(0x12345678));
  EXPECT_EQ(time.low, UINT32_C(0x9abcdef0));
  EXPECT_EQ(time.real, 0.0);

  context->schedulerTime = 123456;
  time = {};
  time.type = vpiScaledRealTime;
  vpi_get_time(module, &time);
  EXPECT_DOUBLE_EQ(time.real, 123.456);
  time = {};
  time.type = vpiScaledRealTime;
  vpi_get_time(value, &time);
  EXPECT_DOUBLE_EQ(time.real, 123.456);

  time.high = 1;
  time.low = 2;
  time.real = 3.0;
  time.type = vpiSuppressTime;
  vpi_get_time(nullptr, &time);
  EXPECT_EQ(time.high, 0u);
  EXPECT_EQ(time.low, 0u);
  EXPECT_EQ(time.real, 0.0);

  time.type = 99;
  vpi_get_time(nullptr, &time);
  s_vpi_error_info error{};
  EXPECT_EQ(vpi_chk_error(&error), vpiError);
  EXPECT_STREQ(error.message, "unsupported VPI time format");
  vpi_get_time(nullptr, nullptr);
  EXPECT_EQ(vpi_chk_error(&error), vpiError);
  EXPECT_STREQ(error.message, "VPI time destination is null");

  EXPECT_FALSE(context->nativeScheduleDeoptimized);
  EXPECT_EQ(vpi_release_handle(value), 1);
  EXPECT_EQ(vpi_release_handle(module), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, IteratesCanonicalSchedulerTimeQueuesOnDemand) {
  Fixture fixture;
  static constexpr char rootName[] = "$root";
  static constexpr char topName[] = "top";
  const obelisk_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, rootName, sizeof(rootName) - 1, -12, -12, 0},
      {1, 0, topName, sizeof(topName) - 1, -9, -12, 0},
  };
  fixture.execution.dpi_scopes = scopes;
  fixture.execution.dpi_scope_count = std::size(scopes);
  fixture.execution.dpi_time_precision = -12;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  context->schedulerTime = 10;

  ScheduledProcess process;
  process.instance = reinterpret_cast<obelisk_rt_process_instance_v1 *>(1);
  process.started = true;
  process.suspendKind = OBELISK_RT_SUSPEND_DELAY;
  process.wakeTime = 40;
  context->scheduledProcesses.push_back(std::move(process));
  ScheduledProcess finalProcess;
  finalProcess.instance = reinterpret_cast<obelisk_rt_process_instance_v1 *>(1);
  finalProcess.phase = 1;
  finalProcess.started = true;
  finalProcess.suspendKind = OBELISK_RT_SUSPEND_DELAY;
  finalProcess.wakeTime = 11;
  context->scheduledProcesses.push_back(std::move(finalProcess));

  ScheduledDesignTask task;
  task.started = true;
  task.suspendKind = OBELISK_RT_SUSPEND_DELAY;
  task.wakeTime = 30;
  context->scheduledDesignTasks.push_back(std::move(task));
  ScheduledDesignTask terminatedTask;
  terminatedTask.started = true;
  terminatedTask.terminated = true;
  terminatedTask.suspendKind = OBELISK_RT_SUSPEND_DELAY;
  terminatedTask.wakeTime = 12;
  context->scheduledDesignTasks.push_back(std::move(terminatedTask));

  ScheduledNBA nba;
  nba.dueTime = 20;
  context->scheduledNBAs.push_back(nba);
  ScheduledNBA cancelledNBA;
  cancelledNBA.dueTime = 21;
  cancelledNBA.cancelled = true;
  context->scheduledNBAs.push_back(cancelledNBA);
  ScheduledNBA inertial;
  inertial.dueTime = 70;
  context->scheduledInertialPathNBAs.emplace(std::pair{70u, 1u}, inertial);
  ScheduledManagedNBA managed;
  managed.dueTime = 60;
  context->scheduledManagedNBAs.push_back(std::move(managed));
  ScheduledDesignNBA designNBA;
  designNBA.dueTime = 50;
  context->scheduledDesignNBAs.push_back(std::move(designNBA));
  context->scheduledDesignEvents.push_back({1, 80});
  context->clockOccurrences = std::make_unique<ClockOccurrenceFeatureState>();
  context->clockOccurrences->replaceableEvents =
      std::make_unique<ReplaceableEventFeatureState>();
  context->clockOccurrences->replaceableEvents->calendar.emplace(
      std::pair{90u, 2u}, ScheduledDesignEvent{2, 90});
  context->scheduledPassSwitchEvents.emplace(std::pair{UINT64_MAX, 3u},
                                             ScheduledPassSwitchEvent{});
  // This lazy heap entry is deliberately stale and must never be exposed.
  context->scheduledProcessDelayHeap.emplace_back(13, 999);

  vpiHandle iterator = vpi_iterate(vpiTimeQueue, nullptr);
  ASSERT_NE(iterator, nullptr);
  EXPECT_EQ(vpi_get(vpiIteratorType, iterator), vpiTimeQueue);
  EXPECT_EQ(vpi_handle(vpiUse, iterator), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  // Iterator contents are a snapshot, not a live view of scheduler storage.
  context->scheduledNBAs.front().dueTime = 25;

  const std::array<uint64_t, 9> expected{20, 30, 40, 50,        60,
                                         70, 80, 90, UINT64_MAX};
  for (uint64_t scheduledTime : expected) {
    vpiHandle queue = vpi_scan(iterator);
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(vpi_get(vpiAllocScheme, queue), vpiOtherScheme);
    EXPECT_EQ(vpi_get(vpiType, queue), vpiTimeQueue);
    EXPECT_STREQ(vpi_get_str(vpiType, queue), "vpiTimeQueue");
    EXPECT_EQ(vpi_get(vpiIsProtected, queue), 0);
    s_vpi_time time{};
    time.type = vpiSimTime;
    vpi_get_time(queue, &time);
    EXPECT_EQ((uint64_t{time.high} << 32) | time.low, scheduledTime);
    time = {};
    time.type = vpiScaledRealTime;
    vpi_get_time(queue, &time);
    EXPECT_EQ(time.real, static_cast<double>(scheduledTime));
    EXPECT_EQ(vpi_release_handle(queue), 1);
  }
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_FALSE(context->nativeScheduleDeoptimized);

  context->scheduledProcesses.clear();
  context->scheduledDesignTasks.clear();
  context->scheduledNBAs.clear();
  context->scheduledInertialPathNBAs.clear();
  context->scheduledManagedNBAs.clear();
  context->scheduledDesignNBAs.clear();
  context->scheduledDesignEvents.clear();
  context->scheduledPassSwitchEvents.clear();
  context->scheduledProcessDelayHeap.clear();
  context->clockOccurrences.reset();
  EXPECT_EQ(vpi_iterate(vpiTimeQueue, nullptr), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  // A running event is itself before read-only synchronization and therefore
  // keeps the current numeric time queue visible. Once that event boundary is
  // cleared, the same empty scheduler is at/after read-only synchronization.
  context->activeExecRegion = OBELISK_RT_REGION_ACTIVE;
  iterator = vpi_iterate(vpiTimeQueue, nullptr);
  ASSERT_NE(iterator, nullptr);
  vpiHandle current = vpi_scan(iterator);
  ASSERT_NE(current, nullptr);
  s_vpi_time currentTime{};
  currentTime.type = vpiSimTime;
  vpi_get_time(current, &currentTime);
  EXPECT_EQ(currentTime.high, 0u);
  EXPECT_EQ(currentTime.low, 10u);
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(vpi_release_handle(current), 1);
  context->activeExecRegion = OBELISK_RT_REGION_POSTPONED;
  EXPECT_EQ(vpi_iterate(vpiTimeQueue, nullptr), nullptr);

  context->activeExecRegion = UINT32_MAX;
  ScheduledProcess postponed;
  postponed.instance = reinterpret_cast<obelisk_rt_process_instance_v1 *>(1);
  postponed.queuedRegion = OBELISK_RT_REGION_POSTPONED;
  context->scheduledProcesses.push_back(std::move(postponed));
  ScheduledNBA postponedNBA;
  postponedNBA.dueTime = context->schedulerTime;
  postponedNBA.execRegion = OBELISK_RT_REGION_POSTPONED;
  context->scheduledNBAs.push_back(std::move(postponedNBA));
  EXPECT_EQ(vpi_iterate(vpiTimeQueue, nullptr), nullptr);
  context->scheduledProcesses.front().queuedRegion = OBELISK_RT_REGION_ACTIVE;
  iterator = vpi_iterate(vpiTimeQueue, nullptr);
  ASSERT_NE(iterator, nullptr);
  current = vpi_scan(iterator);
  ASSERT_NE(current, nullptr);
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(vpi_release_handle(current), 1);
  context->scheduledProcesses.clear();
  context->scheduledNBAs.clear();

  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, CurrentTimeQueueInspectionIsExactAndSideEffectFree) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  context->schedulerTime = 10;
  context->activeExecRegion = UINT32_MAX;
  context->signalDiagnosticsEnabled = true;

  auto currentQueueVisible = [&]() {
    vpiHandle iterator = vpi_iterate(vpiTimeQueue, nullptr);
    if (!iterator)
      return false;
    vpiHandle queue = vpi_scan(iterator);
    EXPECT_EQ(vpi_release_handle(iterator), 1);
    if (queue) {
      EXPECT_EQ(vpi_release_handle(queue), 1);
    }
    return queue != nullptr;
  };
  auto installDesignTask = [&](ScheduledDesignTask task) {
    size_t index = context->scheduledDesignTasks.size();
    context->scheduledDesignTaskIndices.emplace(task.id, index);
    context->designPollCandidates.insert(task.id);
    context->scheduledDesignTasks.push_back(std::move(task));
  };
  auto clearDesignTasks = [&]() {
    context->scheduledDesignTasks.clear();
    context->scheduledDesignTaskIndices.clear();
    context->designPollCandidates.clear();
  };

  struct WaitRecord {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  };

  // Event-generation readiness is shared with the bytecode scheduler rather
  // than approximated by the VPI query.
  WaitRecord eventRecord{
      {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EVENT, 0, 1, 0, 0},
      {77, OBELISK_RT_WAIT_EDGE_NONE, 0}};
  ScheduledDesignTask eventTask;
  eventTask.id = 101;
  eventTask.started = true;
  eventTask.suspendKind = OBELISK_RT_SUSPEND_EVENT;
  eventTask.queuedRegion = OBELISK_RT_REGION_ACTIVE;
  eventTask.waitSize = sizeof(eventRecord);
  eventTask.scratchOffset = sizeof(eventRecord);
  eventTask.frame.resize(sizeof(eventRecord));
  std::memcpy(eventTask.frame.data(), &eventRecord, sizeof(eventRecord));
  eventTask.waitGenerations.push_back(0);
  context->events[77].generation = 1;
  installDesignTask(std::move(eventTask));
  EXPECT_TRUE(currentQueueVisible());
  clearDesignTasks();

  // A slot-final timing coordinator sorts immediately before true Postponed
  // work and therefore still precedes the read-only synchronization point.
  WaitRecord slotFinalRecord{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE,
                              OBELISK_RT_WAIT_CLOCK_OCCURRENCE |
                                  OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL,
                              1, 0, 0},
                             {91, OBELISK_RT_WAIT_EDGE_POSEDGE, 1}};
  ScheduledDesignTask slotFinalTask;
  slotFinalTask.id = 102;
  slotFinalTask.started = true;
  slotFinalTask.suspendKind = OBELISK_RT_SUSPEND_EDGE;
  slotFinalTask.signalTriggered = true;
  slotFinalTask.queuedRegion = OBELISK_RT_REGION_OBSERVED;
  slotFinalTask.waitSize = sizeof(slotFinalRecord);
  slotFinalTask.scratchOffset = sizeof(slotFinalRecord);
  slotFinalTask.frame.resize(sizeof(slotFinalRecord));
  std::memcpy(slotFinalTask.frame.data(), &slotFinalRecord,
              sizeof(slotFinalRecord));
  installDesignTask(std::move(slotFinalTask));
  EXPECT_TRUE(currentQueueVisible());
  clearDesignTasks();

  // Urgent scheduler work is promoted to region zero even when its stored
  // queue region is Postponed.
  ScheduledDesignTask urgentTask;
  urgentTask.id = 103;
  urgentTask.urgent = true;
  urgentTask.queuedRegion = OBELISK_RT_REGION_POSTPONED;
  installDesignTask(std::move(urgentTask));
  EXPECT_TRUE(currentQueueVisible());
  clearDesignTasks();

  // Invalid managed waits are not runnable, but inspecting them must not
  // poison scheduler status or diagnostics.
  WaitRecord invalidMailbox{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_MAILBOX,
                             OBELISK_RT_WAIT_MAILBOX_NOT_EMPTY, 1, 0, 0},
                            {0, OBELISK_RT_WAIT_EDGE_NONE, 0}};
  ScheduledDesignTask mailboxTask;
  mailboxTask.id = 104;
  mailboxTask.started = true;
  mailboxTask.suspendKind = OBELISK_RT_SUSPEND_MAILBOX;
  mailboxTask.queuedRegion = OBELISK_RT_REGION_ACTIVE;
  mailboxTask.waitSize = sizeof(invalidMailbox);
  mailboxTask.scratchOffset = sizeof(invalidMailbox);
  mailboxTask.frame.resize(sizeof(invalidMailbox));
  std::memcpy(mailboxTask.frame.data(), &invalidMailbox,
              sizeof(invalidMailbox));
  installDesignTask(std::move(mailboxTask));
  const uint64_t readinessCalls = context->signalDiagnostics.readinessCalls;
  const obelisk_rt_status schedulerStatus = context->schedulerStatus;
  EXPECT_FALSE(currentQueueVisible());
  EXPECT_EQ(context->signalDiagnostics.readinessCalls, readinessCalls);
  EXPECT_EQ(context->schedulerStatus, schedulerStatus);
  clearDesignTasks();

  // Native readiness inspection has the same side-effect-free contract.
  obelisk_rt_frame_field_v1 waitField{OBELISK_RT_FRAME_WAIT,
                                      OBELISK_RT_FRAME_FIELD_FLAGS_NONE,
                                      0,
                                      sizeof(invalidMailbox),
                                      alignof(WaitRecord),
                                      0};
  obelisk_rt_frame_layout_v1 frameLayout{OBELISK_RT_VERSION,
                                         0,
                                         sizeof(invalidMailbox),
                                         alignof(WaitRecord),
                                         &waitField,
                                         1,
                                         0,
                                         nullptr,
                                         0};
  obelisk_rt_process_descriptor_v1 descriptor{};
  descriptor.frame_layout = &frameLayout;
  obelisk_rt_process_instance_v1 instance{};
  instance.descriptor = &descriptor;
  instance.frame = &invalidMailbox;
  instance.frame_size = sizeof(invalidMailbox);
  ScheduledProcess nativeMailbox;
  nativeMailbox.instance = &instance;
  nativeMailbox.started = true;
  nativeMailbox.suspendKind = OBELISK_RT_SUSPEND_MAILBOX;
  nativeMailbox.queuedRegion = OBELISK_RT_REGION_ACTIVE;
  nativeMailbox.waitSize = sizeof(invalidMailbox);
  context->scheduledProcesses.push_back(std::move(nativeMailbox));
  EXPECT_FALSE(currentQueueVisible());
  EXPECT_EQ(context->signalDiagnostics.readinessCalls, readinessCalls);
  EXPECT_EQ(context->schedulerStatus, schedulerStatus);
  context->scheduledProcesses.clear();

  // Cold inspection must not compact the scheduler's lazy unstarted set.
  context->unstartedActiveActors.insert(999);
  EXPECT_FALSE(currentQueueVisible());
  EXPECT_EQ(context->unstartedActiveActors.count(999), 1u);
  context->unstartedActiveActors.clear();

  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ResolvesTimescalesByExactReflectionScopeIdentity) {
  Fixture fixture;
  installStatementDatabase(fixture);
  static constexpr char rootName[] = "$root";
  static constexpr char topName[] = "top";
  static constexpr char childName[] = "top.child";
  const obelisk_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, rootName, sizeof(rootName) - 1, -12, -12, 0},
      {1, 0, topName, sizeof(topName) - 1, -9, -12, 0},
      {2, 1, childName, sizeof(childName) - 1, -6, -12, 0},
  };
  fixture.execution.dpi_scopes = scopes;
  fixture.execution.dpi_scope_count = std::size(scopes);
  fixture.execution.dpi_time_precision = -12;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  context->schedulerTime = 1000;

  char topPath[] = "top";
  char childPath[] = "top.child";
  char processPath[] = "top.child.proc";
  vpiHandle top = vpi_handle_by_name(topPath, nullptr);
  vpiHandle child = vpi_handle_by_name(childPath, nullptr);
  vpiHandle process = vpi_handle_by_name(processPath, nullptr);
  ASSERT_NE(top, nullptr);
  ASSERT_NE(child, nullptr);
  ASSERT_NE(process, nullptr);
  EXPECT_EQ(vpi_get(vpiTimeUnit, top), -9);
  EXPECT_EQ(vpi_get(vpiTimePrecision, top), -12);
  EXPECT_EQ(vpi_get(vpiTimeUnit, child), -6);
  EXPECT_EQ(vpi_get(vpiTimePrecision, child), -12);

  s_vpi_time time{};
  time.type = vpiScaledRealTime;
  vpi_get_time(process, &time);
  EXPECT_DOUBLE_EQ(time.real, 0.001);
  vpiHandle statement = vpi_handle(vpiStmt, process);
  ASSERT_NE(statement, nullptr);
  time = {};
  time.type = vpiScaledRealTime;
  vpi_get_time(statement, &time);
  EXPECT_DOUBLE_EQ(time.real, 0.001);
  obelisk_rt_v1_context_destroy(context);

  // If exact metadata for a physical scope is missing, do not silently use
  // an enclosing scope's different time unit.
  Fixture missing;
  installStatementDatabase(missing);
  missing.execution.dpi_scopes = scopes;
  missing.execution.dpi_scope_count = 2;
  missing.execution.dpi_time_precision = -12;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&missing.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  child = vpi_handle_by_name(childPath, nullptr);
  ASSERT_NE(child, nullptr);
  EXPECT_EQ(vpi_get(vpiTimeUnit, child), vpiUndefined);
  s_vpi_error_info error{};
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_STREQ(error.message, "VPI object timescale metadata is unavailable");
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, VPIIntrinsicsTraverseAndAccessLiveState) {
  Fixture fixture;
  fixture.bytecode = makeVPIBytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 40;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_CONTINUE);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  ASSERT_EQ(frameSize, 40u);
  std::array<uint64_t, 4> planes{};
  std::memcpy(planes.data(), frame, sizeof(planes));
  EXPECT_EQ(planes[0], UINT64_C(0x123456789abcdef0));
  EXPECT_EQ(planes[1], 1u);
  EXPECT_EQ(planes[2], UINT64_C(0x30));
  EXPECT_EQ(planes[3], 0u);
  uint64_t automatic = UINT64_MAX;
  std::memcpy(&automatic, static_cast<uint8_t *>(frame) + 32,
              sizeof(automatic));
  EXPECT_NE(automatic, UINT64_MAX);
  EXPECT_NE(automatic >> 63, 0u);
  std::array<uint8_t, 9> dummy{}, automaticValue{}, automaticUnknown{};
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, dummy.data(), 65,
                                                  automatic, 65, 0, 0,
                                                  automaticValue.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, dummy.data(), 65,
                                                  automatic, 65, 1, 0,
                                                  automaticUnknown.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(automaticValue[0], 0xf0);
  EXPECT_EQ(automaticValue[8], 1u);
  EXPECT_EQ(automaticUnknown[0], 0x30);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, AutomaticViewsPreservePartialDirectAndNBASelections) {
  auto run = [&](bool nba) {
    Fixture fixture;
    fixture.bytecode = makePartialAutomaticBytecode(nba);
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.checksum = imageChecksum(fixture.bytecode);
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_OK);
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
        OBELISK_RT_OK);
    obelisk_rt_fragment_action_v1 action{};
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  instance, context, OBELISK_RT_TIER_BYTECODE, &action),
              OBELISK_RT_OK);
    ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_CONTINUE);
    void *frame = nullptr;
    uint64_t frameSize = 0;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
        OBELISK_RT_OK);
    ASSERT_EQ(frameSize, 32u);
    std::array<uint64_t, 2> value{}, unknown{};
    if (nba) {
      uint64_t stable = 0;
      std::memcpy(&stable, frame, sizeof(stable));
      ASSERT_NE(stable, UINT64_MAX);
      ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
      std::array<uint8_t, 9> dummy{};
      ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                    context, dummy.data(), 65, stable, 65, 0, 0,
                    reinterpret_cast<uint8_t *>(value.data())),
                OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                    context, dummy.data(), 65, stable, 65, 1, 1,
                    reinterpret_cast<uint8_t *>(unknown.data())),
                OBELISK_RT_OK);
    } else {
      std::memcpy(value.data(), frame, 16);
      std::memcpy(unknown.data(), static_cast<uint8_t *>(frame) + 16, 16);
    }
    EXPECT_EQ(value[0], UINT64_C(0xfffffffffffffff8));
    EXPECT_EQ(value[1], 1u);
    EXPECT_EQ(unknown[0], UINT64_C(0x37));
    EXPECT_EQ(unknown[1], 0u);
    EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
    obelisk_rt_v1_context_destroy(context);
  };
  run(false);
  run(true);
}

TEST(DesignBytecode,
     AutomaticFrameReconstructionPreservesEmptyOutOfBoundsViews) {
  Fixture fixture;
  fixture.bytecode = makeAutomaticFrameLoadBytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  std::array<uint8_t, 9> initial{};
  uint64_t automatic = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, 65, initial.data(),
                                             initial.data(), &automatic),
            OBELISK_RT_OK);

  auto run = [&](int64_t offset) {
    uint64_t selected = obelisk_rt_v1_native_handle_offset(automatic, offset);
    ASSERT_NE(selected, UINT64_MAX);
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
        OBELISK_RT_OK);
    void *frame = nullptr;
    uint64_t frameSize = 0;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
        OBELISK_RT_OK);
    ASSERT_EQ(frameSize, 32u);
    std::memcpy(frame, &selected, sizeof(selected));
    obelisk_rt_fragment_action_v1 action{};
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  instance, context, OBELISK_RT_TIER_BYTECODE, &action),
              OBELISK_RT_OK);
    EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
    const auto *planes = static_cast<const uint64_t *>(frame);
    EXPECT_EQ(planes[0], 0u);
    EXPECT_EQ(planes[1], 0u);
    EXPECT_EQ(planes[2], UINT64_MAX);
    EXPECT_EQ(planes[3], 1u);
    EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  };
  run(-66);
  run(65);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, NativeStaticHandlesRemainBoundedAcrossBytecodeFrames) {
  Fixture fixture;
  fixture.bytecode = makeAutomaticFrameLoadBytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 65),
            OBELISK_RT_OK);
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  std::array<uint8_t, 9> globalValue{}, globalUnknown{}, zeros{}, ones{};
  ones.fill(0xff);
  ones.back() = 1;
  uint8_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(context, globalValue.data(),
                                                   65, root, 65, 0, ones.data(),
                                                   &changed),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context, globalUnknown.data(), 65, root, 65, 1, zeros.data(),
                &changed),
            OBELISK_RT_OK);

  auto run = [&](int64_t offset, uint64_t expectedValue0,
                 uint64_t expectedValue1, uint64_t expectedUnknown0,
                 uint64_t expectedUnknown1) {
    uint64_t selected = obelisk_rt_v1_native_handle_offset(root, offset);
    ASSERT_NE(selected, UINT64_MAX);
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
        OBELISK_RT_OK);
    void *frame = nullptr;
    uint64_t frameSize = 0;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
        OBELISK_RT_OK);
    ASSERT_EQ(frameSize, 32u);
    std::memcpy(frame, &selected, sizeof(selected));
    obelisk_rt_fragment_action_v1 action{};
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  instance, context, OBELISK_RT_TIER_BYTECODE, &action),
              OBELISK_RT_OK);
    EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
    const auto *planes = static_cast<const uint64_t *>(frame);
    EXPECT_EQ(planes[0], expectedValue0);
    EXPECT_EQ(planes[1], expectedValue1);
    EXPECT_EQ(planes[2], expectedUnknown0);
    EXPECT_EQ(planes[3], expectedUnknown1);
    EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  };
  run(-3, UINT64_MAX - 7, 1, 7, 0);
  run(65, 0, 0, UINT64_MAX, 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, StaticHandleOffsetIDStoreAndFrameRoundTrip) {
  Fixture fixture;
  fixture.bytecode = makeStaticHandleRoundTripBytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 65),
            OBELISK_RT_OK);
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(root, UINT64_MAX);
  std::array<uint8_t, 9> initial{
      {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x01}};
  std::array<uint8_t, 9> unknown{};
  uint8_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(context, initial.data(), 65,
                                                   root, 65, 0, initial.data(),
                                                   &changed),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(context, unknown.data(), 65,
                                                   root, 65, 1, unknown.data(),
                                                   &changed),
            OBELISK_RT_OK);

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  ASSERT_EQ(frameSize, 32u);
  std::memcpy(frame, &root, sizeof(root));
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  uint64_t selected = 0;
  uint64_t identity = 0;
  std::memcpy(&selected, frame, sizeof(selected));
  std::memcpy(&identity, static_cast<uint8_t *>(frame) + 8, sizeof(identity));
  EXPECT_EQ(selected, obelisk_rt_v1_native_handle_offset(root, 3));
  EXPECT_EQ(identity, root);
  std::array<uint8_t, 9> loaded{};
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, initial.data(), 65, root, 65, 0, 0, loaded.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded, initial);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, StaticHandleNBARemainsBoundedAndApplies) {
  Fixture fixture;
  fixture.bytecode = makeStaticNBABytecode();
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 65),
            OBELISK_RT_OK);
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  std::memcpy(frame, &root, sizeof(root));
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  std::array<uint8_t, 9> dummy{}, value{}, unknown{};
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, dummy.data(), 65,
                                                  root, 65, 0, 0, value.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, dummy.data(), 65, root, 65, 1, 0, unknown.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0], 0xf0);
  EXPECT_EQ(value[7], 0x12);
  EXPECT_EQ(value[8], 1u);
  EXPECT_EQ(unknown[0], 0x30);
  EXPECT_EQ(unknown[8], 0u);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, SpawnRetainsStableAutomaticHandlesAndReclaimsTaskState) {
  std::vector<uint8_t> bytecode = makeAutomaticSpawnBytecode();
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      OBELISK_RT_EXECUTION_HAS_BYTECODE,
      0,
      bytecode.data(),
      bytecode.size(),
      nullptr,
      0,
      65,
      imageChecksum(bytecode)};
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, 0, 0};
  std::array<uint32_t, 1> continuations{{0}};
  obelisk_rt_frame_layout_v1 layout{
      OBELISK_RT_VERSION, 0, 8, 8, nullptr, 0, 1, continuations.data(), 0};
  layout.checksum = frameChecksum(layout);
  obelisk_rt_process_descriptor_v1 descriptor{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 71},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_BYTECODE,
      0,
      &layout,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      &execution,
      &entry};

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  std::array<uint8_t, 9> initial{}, dummy{};
  uint64_t automatic = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, 65, initial.data(),
                                             initial.data(), &automatic),
            OBELISK_RT_OK);
  // The canonical process capture owns the extra reference that its native
  // spawn helper would normally establish.
  ASSERT_EQ(obelisk_rt_v1_native_state_retain(context, automatic),
            OBELISK_RT_OK);

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
            OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  ASSERT_EQ(frameSize, 8u);
  std::memcpy(frame, &automatic, sizeof(automatic));
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledProcesses.empty());
  EXPECT_TRUE(context->scheduledDesignTasks.empty());
  EXPECT_TRUE(context->logicalProcessParentsWithChildren.empty());
  EXPECT_EQ(context->terminatedDesignTasks.rangeCount(), 1u);
  EXPECT_EQ(context->designTaskFrames.size(), 1u);
  std::array<uint8_t, 9> value{}, unknown{};
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, dummy.data(), 65, automatic, 65, 0, 0, value.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, dummy.data(), 65, automatic, 65, 1, 0, unknown.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0], 0xf0);
  EXPECT_EQ(value[7], 0x12);
  EXPECT_EQ(value[8], 1u);
  EXPECT_EQ(unknown[0], 0x30);

  // Allocation IDs are monotonic. ID two was allocated by the child task and
  // must have lost its owner reference when that task terminated.
  uint64_t taskOwned = (UINT64_C(1) << 63) | (UINT64_C(2) << 32);
  EXPECT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, dummy.data(), 65, taskOwned, 65, 0, 0, value.data()),
            OBELISK_RT_INVALID_HANDLE);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, automatic, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, dummy.data(), 65, automatic, 65, 0, 0, value.data()),
            OBELISK_RT_INVALID_HANDLE);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, SpawnRoundTripsReservedPreponedEventHandle) {
  std::vector<uint8_t> bytecode = makePreponedEventSpawnBytecode();
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      OBELISK_RT_EXECUTION_HAS_BYTECODE,
      0,
      bytecode.data(),
      bytecode.size(),
      nullptr,
      0,
      0,
      imageChecksum(bytecode)};
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, 0, 0};
  std::array<uint32_t, 1> continuations{{0}};
  obelisk_rt_frame_layout_v1 layout{
      OBELISK_RT_VERSION, 0, 8, 8, nullptr, 0, 1, continuations.data(), 0};
  layout.checksum = frameChecksum(layout);
  obelisk_rt_process_descriptor_v1 descriptor{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 72},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_BYTECODE,
      0,
      &layout,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      &execution,
      &entry};

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
            OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  ASSERT_GE(frameSize, sizeof(uint64_t));
  uint64_t preponed = OBELISK_RT_STABLE_HANDLE_PREPONED_EVENT;
  std::memcpy(frame, &preponed, sizeof(preponed));

  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ScheduledSignalWaitUsesDirectSubscriptions) {
  std::vector<uint8_t> bytecode = makeSignalWaitSpawnBytecode();
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      OBELISK_RT_EXECUTION_HAS_BYTECODE,
      0,
      bytecode.data(),
      bytecode.size(),
      nullptr,
      0,
      65,
      imageChecksum(bytecode)};
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, 0, 0};
  std::array<uint32_t, 1> continuations{{0}};
  obelisk_rt_frame_layout_v1 layout{
      OBELISK_RT_VERSION, 0, 8, 8, nullptr, 0, 1, continuations.data(), 0};
  layout.checksum = frameChecksum(layout);
  obelisk_rt_process_descriptor_v1 descriptor{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 72},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_BYTECODE,
      0,
      &layout,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      &execution,
      &entry};

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  context->signalDiagnosticsEnabled = true;
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
            OBELISK_RT_OK);
  uint64_t capturedHandle = 16;
  std::memcpy(instance->frame, &capturedHandle, sizeof(capturedHandle));
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);

  ASSERT_EQ(context->scheduledDesignTasks.size(), 1u);
  ScheduledDesignTask &task = context->scheduledDesignTasks.front();
  ASSERT_LE(8 + sizeof(obelisk_rt_wait_record_v1) +
                sizeof(obelisk_rt_wait_entry_v1),
            task.scratchOffset);
  auto *wait =
      reinterpret_cast<obelisk_rt_wait_record_v1 *>(task.frame.data() + 8);
  auto *waitEntry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
  *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0};
  *waitEntry = {16, OBELISK_RT_WAIT_EDGE_NEGEDGE, 8};

  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledDesignTasks.size(), 1u);
  ASSERT_EQ(context->scheduledDesignTasks.front().signalSubscriptions.size(),
            1u);
  obelisk_rt_v1_scheduler_signal(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->scheduledDesignTasks.size(), 1u);
  EXPECT_EQ(context->scheduledDesignTasks.front().signalSubscriptions.size(),
            1u);
  obelisk_rt_v1_scheduler_signal(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->scheduledDesignTasks.size(), 1u);
  EXPECT_EQ(context->scheduledDesignTasks.front().signalSubscriptions.size(),
            1u);
  EXPECT_EQ(context->signalDiagnostics.subscriptionsHighWater, 1u);
  obelisk_rt_v1_scheduler_signal(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledDesignTasks.empty());
  EXPECT_TRUE(context->signalSubscriptionBuckets.empty());
  EXPECT_EQ(context->signalDiagnostics.subscriptionsHighWater, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode,
     ScheduledEventOnlyComputedWaitUnregistersAndRejectsPeriodicAOT) {
  std::vector<uint8_t> bytecode = makeEventOnlyComputedWaitBytecode();
  obelisk_rt_observer_descriptor_v1 observer{
      designEventOnlyObserverID, nullptr, 0, 1, 0, 0, nullptr, 0};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE;
  execution.bytecode = bytecode.data();
  execution.bytecode_size = bytecode.size();
  execution.checksum = imageChecksum(bytecode);
  execution.observers = &observer;
  execution.observer_count = 1;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);

  ScheduledDesignTask task;
  task.id = 0x7d03;
  task.function = 1;
  task.frame.resize(sizeof(DesignEventOnlyComputedWait) + 8);
  task.scratchOffset = sizeof(DesignEventOnlyComputedWait);
  task.scratchSize = 8;
  task.urgent = true;
  task.homeRegion = OBELISK_RT_REGION_ACTIVE;
  task.queuedRegion = OBELISK_RT_REGION_ACTIVE;
  task.insertionSequence = context->nextSchedulerSequence++;
  auto &record =
      *reinterpret_cast<DesignEventOnlyComputedWait *>(task.frame.data());
  record = {};
  record.wait = {OBELISK_RT_VERSION,
                 OBELISK_RT_SUSPEND_OBSERVER,
                 OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
                 1,
                 1,
                 0,
                 1,
                 1,
                 offsetof(DesignEventOnlyComputedWait, observer),
                 offsetof(DesignEventOnlyComputedWait, dependency),
                 offsetof(DesignEventOnlyComputedWait, dependency),
                 offsetof(DesignEventOnlyComputedWait, clause),
                 offsetof(DesignEventOnlyComputedWait, previousValue),
                 0,
                 sizeof(DesignEventOnlyComputedWait),
                 0};
  record.observer = {designEventOnlyObserverID,
                     0,
                     0,
                     0,
                     1,
                     static_cast<uint32_t>(
                         offsetof(DesignEventOnlyComputedWait, previousValue)),
                     0};
  // Generic computed waits may be driven only by an event, so lifecycle
  // accounting cannot rely on a signal subscription being present.
  record.dependency = {designEventOnlyDependencyID,
                       OBELISK_RT_OBSERVER_DEPENDENCY_EVENT, 1};
  record.clause = {0, OBELISK_RT_OBSERVER_CONDITION_NONE,
                   OBELISK_RT_WAIT_EDGE_POSEDGE,
                   OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY};
  context->scheduledDesignTaskIndices.emplace(task.id, 0);
  context->designPollCandidates.insert(task.id);
  context->scheduledDesignTasks.push_back(std::move(task));

  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledDesignTasks.size(), 1u);
  EXPECT_TRUE(
      context->scheduledDesignTasks.front().signalSubscriptions.empty());
  EXPECT_TRUE(
      context->scheduledDesignTasks.front().computedObserverWaitRegistered);
  EXPECT_EQ(context->scheduledDesignTasks.front().continuation, 1u);
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 1u);
  // This is the exact cold predicate used by periodic AOT preparation; unlike
  // the ordinary specialization predicate it deliberately permits the
  // scheduled design task itself and rejects its live computed waiter.
  EXPECT_FALSE(nativePeriodicAOTEnvironmentClean(context));

  obelisk_rt_v1_scheduler_event(context, designEventOnlyDependencyID, 0);
  EXPECT_TRUE(context->scheduledDesignTasks.front().signalTriggered);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledDesignTasks.empty());
  EXPECT_EQ(context->activeComputedObserverWaiterCount, 0u);
  EXPECT_TRUE(nativePeriodicAOTEnvironmentClean(context));
  obelisk_rt_v1_context_destroy(context);
}

void addBlockedForeverDesignTasks(obelisk_rt_context *context,
                                  uint64_t slowCount) {
  context->scheduledDesignTasks.reserve(context->scheduledDesignTasks.size() +
                                        slowCount);
  context->scheduledDesignTaskIndices.reserve(
      context->scheduledDesignTaskIndices.size() + slowCount);
  context->designPollCandidates.reserve(context->designPollCandidates.size() +
                                        slowCount);
  for (uint64_t index = 0; index != slowCount; ++index) {
    ScheduledDesignTask slow;
    do {
      slow.id = context->nextDesignTaskID++;
    } while (context->scheduledDesignTaskIndices.count(slow.id));
    slow.function = 0;
    slow.frame.resize(64);
    slow.scratchOffset = 32;
    slow.scratchSize = 32;
    slow.started = true;
    slow.suspendKind = OBELISK_RT_SUSPEND_FOREVER;
    context->scheduledDesignTaskIndices.emplace(
        slow.id, context->scheduledDesignTasks.size());
    context->designPollCandidates.insert(slow.id);
    context->scheduledDesignTasks.push_back(std::move(slow));
  }
}

void expectLargeDirectSignalCohortScansLinearly(uint64_t cohortSize,
                                                uint64_t slowCount = 0) {
  std::vector<uint8_t> bytecode = makeSignalWaitSpawnBytecode();
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      OBELISK_RT_EXECUTION_HAS_BYTECODE,
      0,
      bytecode.data(),
      bytecode.size(),
      nullptr,
      0,
      65,
      imageChecksum(bytecode)};
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, 0, 0};
  std::array<uint32_t, 1> continuations{{0}};
  obelisk_rt_frame_layout_v1 layout{
      OBELISK_RT_VERSION, 0, 8, 8, nullptr, 0, 1, continuations.data(), 0};
  layout.checksum = frameChecksum(layout);
  obelisk_rt_process_descriptor_v1 descriptor{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 72},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_BYTECODE,
      0,
      &layout,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      &execution,
      &entry};

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  for (uint64_t index = 0; index != cohortSize; ++index) {
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
              OBELISK_RT_OK);
    uint64_t capturedHandle = 16;
    std::memcpy(instance->frame, &capturedHandle, sizeof(capturedHandle));
    obelisk_rt_fragment_action_v1 action{};
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  instance, context, OBELISK_RT_TIER_BYTECODE, &action),
              OBELISK_RT_OK);
    ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
    ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  }

  ASSERT_EQ(context->scheduledDesignTasks.size(), cohortSize);
  for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
    ASSERT_LE(8 + sizeof(obelisk_rt_wait_record_v1) +
                  sizeof(obelisk_rt_wait_entry_v1),
              task.scratchOffset);
    auto *wait =
        reinterpret_cast<obelisk_rt_wait_record_v1 *>(task.frame.data() + 8);
    auto *waitEntry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
    *wait = {OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0};
    *waitEntry = {16, OBELISK_RT_WAIT_EDGE_NEGEDGE, 8};
  }
  // Start every child and establish its first direct signal subscription.
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledDesignTasks.size(), cohortSize);
  ASSERT_TRUE(context->designPollCandidates.empty());

  addBlockedForeverDesignTasks(context, slowCount);

  context->signalDiagnosticsEnabled = true;
  context->signalDiagnostics.candidateScans = 0;
  context->signalDiagnostics.readinessCalls = 0;
  // Wave one resumes every task, which then suspends on the same direct wait.
  obelisk_rt_v1_scheduler_signal(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(context->scheduledDesignTasks.size(), cohortSize + slowCount);
  ASSERT_EQ(context->designPollCandidates.size(), slowCount);
  EXPECT_EQ(context->signalDiagnostics.candidateScans,
            slowCount * (cohortSize + 1) + 2 * cohortSize - 1);

  // A second independent wave proves the resuspended tasks stay out of the
  // poll set until their next trigger. This continuation terminates them.
  obelisk_rt_v1_scheduler_signal(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_NEGEDGE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->scheduledDesignTasks.size(), slowCount);
  EXPECT_EQ(context->designPollCandidates.size(), slowCount);
  // Each wave performs one exact cohort build plus N-1 cached validations;
  // every cached validation still examines all slow candidates exactly.
  // The former behavior performed N + (N-1) + ... + 1 scans per wave.
  EXPECT_EQ(context->signalDiagnostics.candidateScans,
            2 * (slowCount * (cohortSize + 1) + 2 * cohortSize - 1));
  EXPECT_EQ(context->signalDiagnostics.readinessCalls,
            2 * (2 * cohortSize - 1));
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, DirectSignalCohort256ScansLinearly) {
  expectLargeDirectSignalCohortScansLinearly(256);
}

TEST(DesignBytecode, DirectSignalCohort1024ScansLinearly) {
  expectLargeDirectSignalCohortScansLinearly(1024);
}

TEST(DesignBytecode, DirectSignalCohortRescansOneSlowCandidate) {
  expectLargeDirectSignalCohortScansLinearly(256, 1);
}

TEST(DesignBytecode, DirectSignalCohortRescansEightSlowCandidates) {
  expectLargeDirectSignalCohortScansLinearly(256, 8);
}

TEST(DesignBytecode, DirectSignalCohortRescans64SlowCandidates) {
  expectLargeDirectSignalCohortScansLinearly(256, 64);
}

void addReadyTerminatingDesignTasks(obelisk_rt_context *context, uint64_t count,
                                    uint32_t region) {
  context->scheduledDesignTasks.reserve(context->scheduledDesignTasks.size() +
                                        count);
  context->scheduledDesignTaskIndices.reserve(
      context->scheduledDesignTaskIndices.size() + count);
  context->designPollCandidates.reserve(context->designPollCandidates.size() +
                                        count);
  for (uint64_t index = 0; index != count; ++index) {
    ScheduledDesignTask task;
    task.id = 100 + index;
    task.function = 0;
    task.frame.resize(64);
    task.scratchOffset = 32;
    task.scratchSize = 32;
    task.started = true;
    task.suspendKind = OBELISK_RT_SUSPEND_CHANGE;
    task.signalTriggered = true;
    task.queuedRegion = region;
    task.scheduleRank = 3;
    task.insertionSequence = count - index;
    context->scheduledDesignTaskIndices.emplace(
        task.id, context->scheduledDesignTasks.size());
    context->designPollCandidates.insert(task.id);
    context->scheduledDesignTasks.push_back(std::move(task));
  }
}

TEST(DesignBytecode, SmallDirectSignalSetDoesNotAllocateCohort) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 16, OBELISK_RT_REGION_ACTIVE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledDesignTasks.empty());
  EXPECT_EQ(context->designReadyCohort, nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ClockOccurrenceCacheSeparatesOrdinaryFromSlotFinalWaits) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 19, OBELISK_RT_REGION_OBSERVED);

  for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
    task.suspendKind = OBELISK_RT_SUSPEND_EDGE;
    task.waitOffset = 0;
    task.waitSize =
        sizeof(obelisk_rt_wait_record_v1) + sizeof(obelisk_rt_wait_entry_v1);
    auto *wait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(
        task.frame.data() + task.waitOffset);
    auto *entry = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
    *wait = {OBELISK_RT_VERSION,
             OBELISK_RT_SUSPEND_EDGE,
             OBELISK_RT_WAIT_CLOCK_OCCURRENCE,
             1,
             91,
             0};
    *entry = {16, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};
  }
  ScheduledDesignTask &slotFinal = context->scheduledDesignTasks.front();
  uint64_t slotFinalID = slotFinal.id;
  auto *slotFinalWait = reinterpret_cast<obelisk_rt_wait_record_v1 *>(
      slotFinal.frame.data() + slotFinal.waitOffset);
  slotFinalWait->flags |= OBELISK_RT_WAIT_CLOCK_OCCURRENCE_SLOT_FINAL;

  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_NE(context->designReadyCohort, nullptr);
  ASSERT_TRUE(context->designReadyCohort->valid);
  EXPECT_EQ(context->designReadyCohort->ready.size(), 17u);
  ASSERT_EQ(context->designReadyCohort->slowCandidates.size(), 1u);
  EXPECT_EQ(context->designReadyCohort->slowCandidates.front(), slotFinalID);
  for (const DesignReadyCohortEntry &ready :
       context->designReadyCohort->ready) {
    auto indexed = context->scheduledDesignTaskIndices.find(ready.id);
    ASSERT_NE(indexed, context->scheduledDesignTaskIndices.end());
    const ScheduledDesignTask &task =
        context->scheduledDesignTasks[indexed->second];
    const auto *wait = reinterpret_cast<const obelisk_rt_wait_record_v1 *>(
        task.frame.data() + task.waitOffset);
    EXPECT_EQ(wait->flags, OBELISK_RT_WAIT_CLOCK_OCCURRENCE);
  }
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, SlowDominantDirectSignalSetStaysOnExactScan) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  constexpr uint64_t readyCount = 17;
  constexpr uint64_t slowCount = 1024;
  addReadyTerminatingDesignTasks(context, readyCount, OBELISK_RT_REGION_ACTIVE);
  addBlockedForeverDesignTasks(context, slowCount);
  context->signalDiagnosticsEnabled = true;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->scheduledDesignTasks.size(), slowCount);
  EXPECT_EQ(context->signalDiagnostics.candidateScans,
            readyCount * (readyCount + 1) / 2 + slowCount * (readyCount + 1));
  EXPECT_EQ(context->signalDiagnostics.readinessCalls,
            readyCount * (readyCount + 1) / 2);
  ASSERT_NE(context->designReadyCohort, nullptr);
  EXPECT_FALSE(context->designReadyCohort->valid);
  EXPECT_TRUE(context->designReadyCohort->suppressed);
  EXPECT_TRUE(context->designReadyCohort->persistentSuppression);
  EXPECT_TRUE(context->designReadyCohortExactScan);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode,
     SlowDominantSuppressionSurvivesTransientSchedulerGenerations) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 17, OBELISK_RT_REGION_ACTIVE);
  addBlockedForeverDesignTasks(context, 1024);

  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_TRUE(context->designReadyCohort->suppressed);
  EXPECT_EQ(context->designReadyCohort->suppressedCandidateHighWater, 1041u);
  EXPECT_TRUE(context->designReadyCohortExactScan);
  uint64_t rejectedGeneration = context->designReadyCohort->selectionGeneration;

  // Signal publication, time advance, and phase changes affect readiness but
  // cannot affect exact-scan semantics. Keep the negative admission result
  // while the transient poll set only shrinks.
  ++context->schedulerSelectionGeneration;
  ++context->schedulerTime;
  context->schedulerRunningFinals = true;
  progress = true;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  EXPECT_FALSE(progress);
  EXPECT_TRUE(context->designReadyCohort->suppressed);
  EXPECT_EQ(context->designReadyCohort->selectionGeneration,
            rejectedGeneration);

  context->schedulerRunningFinals = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  EXPECT_TRUE(context->designReadyCohort->suppressed);
  EXPECT_EQ(context->designReadyCohort->selectionGeneration,
            rejectedGeneration);

  // Process creation is structural and makes the next call re-probe.
  addBlockedForeverDesignTasks(context, 1);
  obelisk_rt_invalidate_design_ready_cohort(context);
  uint64_t newNextTaskID = context->nextDesignTaskID;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  EXPECT_TRUE(context->designReadyCohort->suppressed);
  EXPECT_EQ(context->designReadyCohort->nextDesignTaskID, newNextTaskID);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, DesignTaskFilterTransitionsInvalidateCohortState) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 17, OBELISK_RT_REGION_ACTIVE);
  addBlockedForeverDesignTasks(context, 1024);

  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_TRUE(context->designReadyCohort->suppressed);
  ASSERT_TRUE(context->designReadyCohort->persistentSuppression);

  uint64_t forcedTask = context->scheduledDesignTasks.front().id;
  obelisk_rt_set_design_task_filter_unlocked(context, true, forcedTask);
  EXPECT_TRUE(context->nativeScheduleDesignTaskFilterActive);
  EXPECT_FALSE(context->designReadyCohort->valid);
  EXPECT_FALSE(context->designReadyCohort->suppressed);

  // Model state produced while a nested filtered handoff is active. Exiting
  // the filter must discard it rather than reviving the old negative result.
  context->designReadyCohort->suppressed = true;
  context->designReadyCohort->persistentSuppression = true;
  obelisk_rt_set_design_task_filter_unlocked(context, false, 0);
  EXPECT_FALSE(context->nativeScheduleDesignTaskFilterActive);
  EXPECT_FALSE(context->designReadyCohort->valid);
  EXPECT_FALSE(context->designReadyCohort->suppressed);
  EXPECT_FALSE(context->designReadyCohort->persistentSuppression);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, AllocatedCohortSuppressesAfterSlowDominantShrink) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 17, OBELISK_RT_REGION_ACTIVE);
  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_NE(context->designReadyCohort, nullptr);
  ASSERT_TRUE(context->designReadyCohort->valid);
  ASSERT_EQ(context->designReadyCohort->ready.size(), 16u);

  constexpr uint64_t readyCount = 16;
  constexpr uint64_t slowCount = 1024;
  addBlockedForeverDesignTasks(context, slowCount);
  context->signalDiagnosticsEnabled = true;
  context->signalDiagnostics.candidateScans = 0;
  context->signalDiagnostics.readinessCalls = 0;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(context->scheduledDesignTasks.size(), slowCount);
  EXPECT_EQ(context->signalDiagnostics.candidateScans,
            readyCount * (readyCount + 1) / 2 + slowCount * (readyCount + 1));
  EXPECT_EQ(context->signalDiagnostics.readinessCalls,
            readyCount * (readyCount + 1) / 2);
  EXPECT_FALSE(context->designReadyCohort->valid);
  EXPECT_TRUE(context->designReadyCohort->suppressed);
  EXPECT_TRUE(context->designReadyCohort->persistentSuppression);
  EXPECT_TRUE(context->designReadyCohortExactScan);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, CachedSlowMembershipFallbackCountsAllValidationWork) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 17, OBELISK_RT_REGION_ACTIVE);
  addBlockedForeverDesignTasks(context, 1);

  ScheduledDesignTask replacement;
  do {
    replacement.id = context->nextDesignTaskID++;
  } while (context->scheduledDesignTaskIndices.count(replacement.id));
  replacement.function = 0;
  replacement.frame.resize(64);
  replacement.scratchOffset = 32;
  replacement.scratchSize = 32;
  replacement.started = true;
  replacement.terminated = true;
  replacement.suspendKind = OBELISK_RT_SUSPEND_FOREVER;
  uint64_t replacementID = replacement.id;
  context->scheduledDesignTaskIndices.emplace(
      replacement.id, context->scheduledDesignTasks.size());
  context->scheduledDesignTasks.push_back(std::move(replacement));

  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_TRUE(context->designReadyCohort->valid);
  ASSERT_EQ(context->designReadyCohort->slowCandidates.size(), 1u);
  uint64_t staleSlow = context->designReadyCohort->slowCandidates.front();
  uint64_t expectedReady = context->designReadyCohort->ready.back().id;
  ASSERT_EQ(context->designPollCandidates.erase(staleSlow), 1u);
  ASSERT_TRUE(context->designPollCandidates.insert(replacementID).second);

  context->signalDiagnosticsEnabled = true;
  context->signalDiagnostics.candidateScans = 0;
  context->signalDiagnostics.readinessCalls = 0;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  EXPECT_EQ(context->terminatedDesignTasks.count(expectedReady), 1u);
  // One cached-head check, one stale slow-membership check, then all 17 live
  // poll members in the safe exact fallback. No validation work is hidden.
  EXPECT_EQ(context->signalDiagnostics.candidateScans, 19u);
  EXPECT_EQ(context->signalDiagnostics.readinessCalls, 17u);
  EXPECT_FALSE(context->designReadyCohort->valid);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, DirectSignalCohortPreservesBoundAndControlOrdering) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  constexpr uint64_t cohortSize = 17;
  addReadyTerminatingDesignTasks(context, cohortSize, OBELISK_RT_REGION_ACTIVE);

  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_NE(context->designReadyCohort, nullptr);
  ASSERT_TRUE(context->designReadyCohort->valid);
  // Insertion sequence one belongs to the last inserted task.
  EXPECT_EQ(context->terminatedDesignTasks.count(100 + cohortSize - 1), 1u);

  // The upper bound is strict. Rejecting the cached head must not consume it.
  progress = true;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, OBELISK_RT_REGION_ACTIVE, 3,
                                           2, &progress),
            OBELISK_RT_OK);
  EXPECT_FALSE(progress);
  ASSERT_TRUE(context->designReadyCohort->valid);
  ASSERT_FALSE(context->designReadyCohort->ready.empty());
  EXPECT_EQ(context->designReadyCohort->ready.back().insertionSequence, 2u);

  // A rare process-control mutation explicitly invalidates the cached order;
  // the following arbitration therefore returns to the exact general scan.
  obelisk_rt_process_control_disposition disposition{};
  ASSERT_EQ(obelisk_rt_v1_process_control(context, 100 + cohortSize - 2,
                                          OBELISK_RT_PROCESS_CONTROL_SUSPEND,
                                          &disposition),
            OBELISK_RT_OK);
  EXPECT_FALSE(context->designReadyCohort->valid);
  ASSERT_EQ(obelisk_rt_v1_process_control(context, 100 + cohortSize - 2,
                                          OBELISK_RT_PROCESS_CONTROL_RESUME,
                                          &disposition),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledDesignTasks.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, DirectSignalCohortRevalidatesTimePhaseAndFinals) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 20, OBELISK_RT_REGION_ACTIVE);

  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_TRUE(context->designReadyCohort->valid);

  // A new time key forces an exact rebuild. The restrictive upper bound keeps
  // the freshly validated head in place for the following phase mutation.
  ++context->schedulerTime;
  progress = true;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, OBELISK_RT_REGION_ACTIVE, 0,
                                           0, &progress),
            OBELISK_RT_OK);
  EXPECT_FALSE(progress);
  ASSERT_TRUE(context->designReadyCohort->valid);
  EXPECT_EQ(context->designReadyCohort->schedulerTime, context->schedulerTime);

  uint64_t finalPhaseID = context->designReadyCohort->ready.back().id;
  auto indexed = context->scheduledDesignTaskIndices.find(finalPhaseID);
  ASSERT_NE(indexed, context->scheduledDesignTaskIndices.end());
  context->scheduledDesignTasks[indexed->second].phase = 1;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  EXPECT_EQ(context->terminatedDesignTasks.count(finalPhaseID), 0u);
  ASSERT_TRUE(context->designReadyCohort->valid);

  // Entering Finals rejects the active-phase cache. The sole final-phase task
  // is selected by the exact scan, while the unprofitable shape is suppressed.
  context->schedulerRunningFinals = true;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  EXPECT_EQ(context->terminatedDesignTasks.count(finalPhaseID), 1u);
  EXPECT_FALSE(context->designReadyCohort->valid);
  EXPECT_TRUE(context->designReadyCohort->suppressed);
  EXPECT_TRUE(context->designReadyCohort->persistentSuppression);
  EXPECT_TRUE(context->designReadyCohortExactScan);
  EXPECT_TRUE(context->designReadyCohort->runningFinals);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, CachedDirectSignalCohortYieldsToSameSlotNBA) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 17, OBELISK_RT_REGION_REACTIVE);

  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_NE(context->designReadyCohort, nullptr);
  ASSERT_TRUE(context->designReadyCohort->valid);
  ASSERT_EQ(context->scheduledDesignTasks.size(), 16u);

  ScheduledNBA nba;
  nba.sequence = context->nextSchedulerSequence++;
  nba.dueTime = context->schedulerTime;
  nba.execRegion = OBELISK_RT_REGION_NBA;
  nba.valuePlane = reinterpret_cast<uint8_t *>(context->stateValue.data());
  nba.unknownPlane = reinterpret_cast<uint8_t *>(context->stateUnknown.data());
  nba.planeBitCount = fixture.execution.state_bit_count;
  nba.bitOffset =
      obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_GLOBAL, 0, 0);
  nba.bitWidth = 1;
  nba.inlinePacked = true;
  nba.inlineValue = 1;
  context->scheduledNBAs.push_back(std::move(nba));
  context->nativeScheduleSingleStep = true;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  // The NBA barrier precedes every cached Reactive task, so single-step must
  // commit it without consuming a cohort member.
  EXPECT_EQ(context->stateValue.front() & 1, 1u);
  EXPECT_EQ(context->scheduledDesignTasks.size(), 16u);
  EXPECT_TRUE(context->designReadyCohort->valid);
  context->nativeScheduleSingleStep = false;
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledDesignTasks.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, CachedDirectSignalCohortInvalidatesForPriorityWake) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 17, OBELISK_RT_REGION_REACTIVE);
  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_TRUE(context->designReadyCohort->valid);

  uint64_t priorityID = context->designReadyCohort->ready.front().id;
  auto indexed = context->scheduledDesignTaskIndices.find(priorityID);
  ASSERT_NE(indexed, context->scheduledDesignTaskIndices.end());
  context->scheduledDesignTasks[indexed->second].prioritySignal = true;
  if (++context->schedulerSelectionGeneration == 0)
    context->schedulerSelectionGeneration = 1;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  EXPECT_EQ(context->terminatedDesignTasks.count(priorityID), 1u);
  EXPECT_FALSE(context->designReadyCohort->valid);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode,
     SimultaneousPrioritySignalWakesRemainInExactCandidateOrder) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 19, OBELISK_RT_REGION_ACTIVE);

  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } record{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_CHANGE, 0, 1, 0, 0},
           {16, OBELISK_RT_WAIT_EDGE_CHANGE, 8}};
  context->designPollCandidates.clear();
  for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
    task.signalTriggered = false;
    ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
        context, &record.wait, task.signalSubscriptions, task.signalLatch,
        task.id, true));
  }
  std::array<uint64_t, 2> priorityIDs{context->scheduledDesignTasks[0].id,
                                      context->scheduledDesignTasks[1].id};
  context->scheduledDesignTasks[0].prioritySignal = true;
  context->scheduledDesignTasks[1].prioritySignal = true;

  // Exercise the production direct-wait publication path so both equal-key
  // priority candidates become runnable in the same scheduler generation.
  obelisk_rt_v1_scheduler_signal(
      context, 18, 1, OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE);
  ASSERT_EQ(context->designPollCandidates.size(), 19u);
  uint64_t exactFirst = 0;
  for (uint64_t candidateID : context->designPollCandidates) {
    auto indexed = context->scheduledDesignTaskIndices.find(candidateID);
    ASSERT_NE(indexed, context->scheduledDesignTaskIndices.end());
    if (context->scheduledDesignTasks[indexed->second].prioritySignal) {
      exactFirst = candidateID;
      break;
    }
  }
  ASSERT_NE(exactFirst, 0u);

  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  EXPECT_EQ(context->terminatedDesignTasks.count(exactFirst), 1u);
  ASSERT_NE(context->designReadyCohort, nullptr);
  for (uint64_t priorityID : priorityIDs) {
    EXPECT_EQ(std::count_if(context->designReadyCohort->ready.begin(),
                            context->designReadyCohort->ready.end(),
                            [&](const DesignReadyCohortEntry &entry) {
                              return entry.id == priorityID;
                            }),
              0);
    EXPECT_NE(std::find(context->designReadyCohort->slowCandidates.begin(),
                        context->designReadyCohort->slowCandidates.end(),
                        priorityID),
              context->designReadyCohort->slowCandidates.end());
  }
  // Selecting an exact slow candidate discards the ready-only cache.
  EXPECT_FALSE(context->designReadyCohort->valid);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, CachedDirectSignalCohortYieldsToUrgentTaskRequeue) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  addReadyTerminatingDesignTasks(context, 17, OBELISK_RT_REGION_REACTIVE);
  bool progress = false;
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  ASSERT_TRUE(context->designReadyCohort->valid);

  // A TASK_CALL continuation is requeued as an urgent, non-signal task. Its
  // extra poll-set member must fracture the cached direct-signal shape.
  ScheduledDesignTask urgent;
  urgent.id = 999;
  urgent.function = 0;
  urgent.frame.resize(64);
  urgent.scratchOffset = 32;
  urgent.scratchSize = 32;
  urgent.started = true;
  urgent.urgent = true;
  urgent.queuedRegion = OBELISK_RT_REGION_REACTIVE;
  urgent.insertionSequence = 1000;
  context->scheduledDesignTaskIndices.emplace(
      urgent.id, context->scheduledDesignTasks.size());
  context->designPollCandidates.insert(urgent.id);
  context->scheduledDesignTasks.push_back(std::move(urgent));
  ASSERT_EQ(obelisk_rt_run_one_design_task(context, UINT32_MAX, UINT32_MAX,
                                           UINT64_MAX, &progress),
            OBELISK_RT_OK);
  ASSERT_TRUE(progress);
  EXPECT_EQ(context->terminatedDesignTasks.count(999), 1u);
  EXPECT_FALSE(context->designReadyCohort->valid);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, ScheduledClockOccurrenceRejectsMalformedWaitRecords) {
  std::vector<uint8_t> bytecode = makeSignalWaitSpawnBytecode();
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      OBELISK_RT_EXECUTION_HAS_BYTECODE,
      0,
      bytecode.data(),
      bytecode.size(),
      nullptr,
      0,
      65,
      imageChecksum(bytecode)};
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, 0, 0};
  std::array<uint32_t, 1> continuations{{0}};
  obelisk_rt_frame_layout_v1 layout{
      OBELISK_RT_VERSION, 0, 8, 8, nullptr, 0, 1, continuations.data(), 0};
  layout.checksum = frameChecksum(layout);
  obelisk_rt_process_descriptor_v1 descriptor{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 72},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_BYTECODE,
      0,
      &layout,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      &execution,
      &entry};

  auto expectInvalid = [&](uint32_t count, uint64_t payload,
                           uint64_t conditionMask,
                           obelisk_rt_wait_edge_kind conditionEdge,
                           uint32_t conditionWidth) {
    obelisk_rt_context *context = nullptr;
    ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
              OBELISK_RT_OK);
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
              OBELISK_RT_OK);
    uint64_t capturedHandle = 16;
    std::memcpy(instance->frame, &capturedHandle, sizeof(capturedHandle));
    obelisk_rt_fragment_action_v1 action{};
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  instance, context, OBELISK_RT_TIER_BYTECODE, &action),
              OBELISK_RT_OK);
    ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
    ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
    ASSERT_EQ(context->scheduledDesignTasks.size(), 1u);
    ScheduledDesignTask &task = context->scheduledDesignTasks.front();
    size_t requiredWaitEnd = 8 + sizeof(obelisk_rt_wait_record_v1) +
                             2 * sizeof(obelisk_rt_wait_entry_v1);
    task.frame.resize(std::max(task.frame.size(), requiredWaitEnd));
    task.scratchOffset = std::max(task.scratchOffset, requiredWaitEnd);
    ASSERT_LE(8 + sizeof(obelisk_rt_wait_record_v1) +
                  2 * sizeof(obelisk_rt_wait_entry_v1),
              task.scratchOffset);
    auto *wait =
        reinterpret_cast<obelisk_rt_wait_record_v1 *>(task.frame.data() + 8);
    auto *entries = reinterpret_cast<obelisk_rt_wait_entry_v1 *>(wait + 1);
    *wait = {OBELISK_RT_VERSION,
             OBELISK_RT_SUSPEND_EDGE,
             OBELISK_RT_WAIT_CLOCK_OCCURRENCE,
             count,
             payload,
             conditionMask};
    entries[0] = {16, OBELISK_RT_WAIT_EDGE_POSEDGE, 1};
    entries[1] = {17, conditionEdge, conditionWidth};
    EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_INVALID_FRAME);
    obelisk_rt_v1_context_destroy(context);
  };

  expectInvalid(1, 0, 0, OBELISK_RT_WAIT_EDGE_NONE, 1);
  expectInvalid(0, 1, 0, OBELISK_RT_WAIT_EDGE_NONE, 1);
  expectInvalid(2, 1, 2, OBELISK_RT_WAIT_EDGE_NONE, 1);
  expectInvalid(2, 1, 1, OBELISK_RT_WAIT_EDGE_POSEDGE, 1);
  expectInvalid(2, 1, 1, OBELISK_RT_WAIT_EDGE_NONE, 0);
}

TEST(DesignBytecode, BlockingStorePreservesSparseTransitionCoordinates) {
  Fixture fixture;
  fixture.bytecode = makeSchedulerBytecode();
  size_t codeOffset = get64(fixture.bytecode, 72);
  size_t constantOffset = get64(fixture.bytecode, 104);
  instruction(fixture.bytecode, codeOffset, 3, OBELISK_RT_DB_STORE_STATE, 0, 0,
              1, 0);
  put64(fixture.bytecode, constantOffset, UINT64_C(0xa7));
  put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
  fixture.execution.bytecode = fixture.bytecode.data();
  fixture.execution.bytecode_size = fixture.bytecode.size();
  fixture.execution.checksum = imageChecksum(fixture.bytecode);
  fixture.entry = {&fixture.execution, 0, 0};
  fixture.layout.frame_size = 0;
  fixture.layout.checksum = frameChecksum(fixture.layout);
  fixture.descriptor.frame_layout = &fixture.layout;
  fixture.descriptor.execution = &fixture.execution;
  fixture.descriptor.design_bytecode = &fixture.entry;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  context->forceMask.resize(2);
  context->forceMask[0] = UINT64_C(1) << 1;

  struct {
    obelisk_rt_wait_record_v1 wait;
    obelisk_rt_wait_entry_v1 entry;
  } waitRecord{{OBELISK_RT_VERSION, OBELISK_RT_SUSPEND_EDGE, 0, 1, 0, 0},
               {7, OBELISK_RT_WAIT_EDGE_POSEDGE, 1}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;
  ASSERT_TRUE(obelisk_rt_register_signal_wait_unlocked(
      context, &waitRecord.wait, subscriptions, latch));

  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&fixture.descriptor, &instance),
      OBELISK_RT_OK);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  ASSERT_TRUE(latch);
  EXPECT_TRUE(latch->triggered);
  EXPECT_EQ(context->stateValue[0] & UINT64_C(0xff), UINT64_C(0xa5));
  EXPECT_EQ(context->stateValue[0] & (UINT64_C(1) << 1), 0u);

  obelisk_rt_unregister_signal_wait_unlocked(context, subscriptions);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, MixedTierSchedulerUsesRegionRankAndInsertionKey) {
  std::vector<uint8_t> bytecode = makeAutomaticSpawnBytecode(10);
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      OBELISK_RT_EXECUTION_HAS_BYTECODE,
      0,
      bytecode.data(),
      bytecode.size(),
      nullptr,
      0,
      65,
      imageChecksum(bytecode)};
  std::array<uint32_t, 1> continuations{{0}};
  obelisk_rt_frame_layout_v1 bytecodeLayout{
      OBELISK_RT_VERSION, 0, 8, 8, nullptr, 0, 1, continuations.data(), 0};
  bytecodeLayout.checksum = frameChecksum(bytecodeLayout);
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, 0, 0};
  obelisk_rt_process_descriptor_v1 bytecodeDescriptor{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 81},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_BYTECODE,
      0,
      &bytecodeLayout,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      &execution,
      &entry};

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  std::array<uint8_t, 9> initial{};
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, 65, initial.data(),
                                             initial.data(),
                                             &mixedTierObservedHandle),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_retain(context, mixedTierObservedHandle),
            OBELISK_RT_OK);

  // Execute the bytecode root directly. It enqueues a rank-10 bytecode child
  // before the rank-5 native process is registered.
  obelisk_rt_process_instance_v1 *root = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&bytecodeDescriptor, &root),
            OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(root, &frame, &frameSize),
            OBELISK_RT_OK);
  ASSERT_EQ(frameSize, 8u);
  std::memcpy(frame, &mixedTierObservedHandle, sizeof(mixedTierObservedHandle));
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                root, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(root), OBELISK_RT_OK);

  obelisk_rt_frame_layout_v1 nativeLayout{
      OBELISK_RT_VERSION, 0, 0, 1, nullptr, 0, 1, continuations.data(), 0};
  nativeLayout.checksum = frameChecksum(nativeLayout);
  obelisk_rt_process_descriptor_v1 nativeDescriptor{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 82},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_NATIVE,
      0,
      &nativeLayout,
      mixedTierRequirements,
      mixedTierExecute,
      mixedTierDestroy};
  obelisk_rt_process_instance_v1 *native = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&nativeDescriptor, &native),
            OBELISK_RT_OK);
  mixedTierObservedValue = UINT8_MAX;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add_ranked(context, native, 0, 5),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_EQ(mixedTierObservedValue, 0);

  std::array<uint8_t, 9> dummy{}, value{};
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(context, dummy.data(), 65,
                                                  mixedTierObservedHandle, 65,
                                                  0, 0, value.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0], 0xf0);
  ASSERT_EQ(
      obelisk_rt_v1_native_state_release(context, mixedTierObservedHandle, 0),
      OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
  mixedTierObservedHandle = UINT64_MAX;
}

TEST(DesignBytecode, RejectsNonCanonicalTablesAndUncallableFunctions) {
  auto rejected = [](Fixture &fixture) {
    put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.checksum = imageChecksum(fixture.bytecode);
    fixture.entry.execution = &fixture.execution;
    obelisk_rt_context *context = nullptr;
    EXPECT_EQ(
        obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
        OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(context, nullptr);
  };

  Fixture reservedHeader;
  put32(reservedHeader.bytecode, 20, 1);
  rejected(reservedHeader);

  Fixture redundantVersion;
  put32(redundantVersion.bytecode, 12, 1);
  rejected(redundantVersion);

  Fixture overlappingTable;
  put64(overlappingTable.bytecode, 56, 192);
  rejected(overlappingTable);

  Fixture noEntryContinuation;
  put64(noEntryContinuation.bytecode,
        OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE + 80, 0);
  rejected(noEntryContinuation);

  Fixture reservedLayout;
  put16(reservedLayout.bytecode,
        OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE + 96 + 2, 1);
  rejected(reservedLayout);

  Fixture oversizedScheduleRank;
  put64(oversizedScheduleRank.bytecode,
        OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE + 8, UINT64_C(1) << 32);
  rejected(oversizedScheduleRank);

  Fixture reservedContinuation;
  size_t continuation = get64(reservedContinuation.bytecode, 120);
  put32(reservedContinuation.bytecode, continuation + 20, 1);
  rejected(reservedContinuation);

  Fixture finalNonProcess;
  put64(finalNonProcess.bytecode, OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE + 88,
        OBELISK_RT_DESIGN_FUNCTION_FINAL);
  rejected(finalNonProcess);

  auto connected = [](Fixture &fixture) {
    fixture.bytecode = makeConnectedDriverBytecode();
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.state_bit_count = 195;
    fixture.execution.checksum = imageChecksum(fixture.bytecode);
    fixture.entry = {&fixture.execution, 0, 0};
  };
  Fixture invalidDriverStrength;
  connected(invalidDriverStrength);
  size_t strengthState = get64(invalidDriverStrength.bytecode, 168);
  put32(invalidDriverStrength.bytecode, strengthState + 64 + 4, 1u | (9u << 3));
  rejected(invalidDriverStrength);

  Fixture invalidNetResolution;
  connected(invalidNetResolution);
  size_t invalidNetState = get64(invalidNetResolution.bytecode, 168);
  put32(invalidNetResolution.bytecode, invalidNetState + 4,
        1u | resolutionFlags(10, false));
  rejected(invalidNetResolution);

  Fixture chargeStrengthOnWire;
  connected(chargeStrengthOnWire);
  size_t chargeStrengthState = get64(chargeStrengthOnWire.bytecode, 168);
  put32(chargeStrengthOnWire.bytecode, chargeStrengthState + 4, 1u | (1u << 7));
  rejected(chargeStrengthOnWire);

  Fixture twoStateTrireg;
  twoStateTrireg.bytecode = makeStrengthDriverBytecode(9);
  size_t twoStateTriregState = get64(twoStateTrireg.bytecode, 168);
  put32(twoStateTrireg.bytecode, twoStateTriregState + 4,
        resolutionFlags(9, false));
  twoStateTrireg.execution.state_bit_count = 260;
  rejected(twoStateTrireg);

  Fixture misalignedConnectivity;
  connected(misalignedConnectivity);
  put64(misalignedConnectivity.bytecode, 184,
        misalignedConnectivity.bytecode.size() - 31);
  rejected(misalignedConnectivity);

  Fixture unknownConnectivityNet;
  connected(unknownConnectivityNet);
  size_t connectivity = unknownConnectivityNet.bytecode.size() - 32;
  put64(unknownConnectivityNet.bytecode, connectivity + 8, 195);
  rejected(unknownConnectivityNet);

  Fixture invalidOrientation;
  connected(invalidOrientation);
  connectivity = invalidOrientation.bytecode.size() - 32;
  invalidOrientation.bytecode[connectivity + 26] = 8;
  rejected(invalidOrientation);

  Fixture incompatibleKinds;
  connected(incompatibleKinds);
  connectivity = incompatibleKinds.bytecode.size() - 32;
  incompatibleKinds.bytecode[connectivity + 24] = 2;
  rejected(incompatibleKinds);

  Fixture missingWiredDominance;
  missingWiredDominance.bytecode = makeMixedWiredDriverBytecode(false);
  missingWiredDominance.execution.bytecode =
      missingWiredDominance.bytecode.data();
  missingWiredDominance.execution.bytecode_size =
      missingWiredDominance.bytecode.size();
  missingWiredDominance.execution.state_bit_count = 260;
  missingWiredDominance.execution.checksum =
      imageChecksum(missingWiredDominance.bytecode);
  missingWiredDominance.entry = {&missingWiredDominance.execution, 0, 0};
  connectivity = missingWiredDominance.bytecode.size() - 32;
  missingWiredDominance.bytecode[connectivity + 26] = 0;
  rejected(missingWiredDominance);

  Fixture missingSameKindDominance;
  missingSameKindDominance.bytecode = makeMultiSinkWiredDriverBytecode();
  connectivity = get64(missingSameKindDominance.bytecode, 184);
  size_t sameKindConnectivityEnd = missingSameKindDominance.bytecode.size();
  missingSameKindDominance.bytecode.resize(sameKindConnectivityEnd + 32, 0);
  put64(missingSameKindDominance.bytecode, sameKindConnectivityEnd, 65);
  put64(missingSameKindDominance.bytecode, sameKindConnectivityEnd + 8, 130);
  put64(missingSameKindDominance.bytecode, sameKindConnectivityEnd + 16, 65);
  missingSameKindDominance.bytecode[sameKindConnectivityEnd + 24] = 3;
  missingSameKindDominance.bytecode[sameKindConnectivityEnd + 25] = 3;
  put64(missingSameKindDominance.bytecode, 24,
        missingSameKindDominance.bytecode.size());
  put64(missingSameKindDominance.bytecode, 192, 131);
  missingSameKindDominance.execution.state_bit_count = 260;
  rejected(missingSameKindDominance);

  Fixture cyclicWiredDominance;
  cyclicWiredDominance.bytecode = makeMultiSinkWiredDriverBytecode();
  size_t cyclicState = get64(cyclicWiredDominance.bytecode, 168);
  connectivity = get64(cyclicWiredDominance.bytecode, 184);
  put32(cyclicWiredDominance.bytecode, cyclicState + 4,
        1u | resolutionFlags(3, false));
  put32(cyclicWiredDominance.bytecode, cyclicState + 64 + 4,
        1u | resolutionFlags(4, false));
  for (uint64_t bit = 0; bit != 65; ++bit) {
    size_t first = connectivity + bit * 2 * 32;
    cyclicWiredDominance.bytecode[first + 24] = 3;
    cyclicWiredDominance.bytecode[first + 25] = 3;
    cyclicWiredDominance.bytecode[first + 26] = 6;
    cyclicWiredDominance.bytecode[first + 32 + 24] = 3;
    cyclicWiredDominance.bytecode[first + 32 + 25] = 4;
    cyclicWiredDominance.bytecode[first + 32 + 26] = 2;
  }
  size_t cyclicConnectivityEnd = cyclicWiredDominance.bytecode.size();
  cyclicWiredDominance.bytecode.resize(cyclicConnectivityEnd + 32, 0);
  put64(cyclicWiredDominance.bytecode, cyclicConnectivityEnd, 65);
  put64(cyclicWiredDominance.bytecode, cyclicConnectivityEnd + 8, 130);
  put64(cyclicWiredDominance.bytecode, cyclicConnectivityEnd + 16, 65);
  cyclicWiredDominance.bytecode[cyclicConnectivityEnd + 24] = 3;
  cyclicWiredDominance.bytecode[cyclicConnectivityEnd + 25] = 4;
  cyclicWiredDominance.bytecode[cyclicConnectivityEnd + 26] = 6;
  put64(cyclicWiredDominance.bytecode, 24,
        cyclicWiredDominance.bytecode.size());
  put64(cyclicWiredDominance.bytecode, 192, 131);
  cyclicWiredDominance.execution.state_bit_count = 260;
  rejected(cyclicWiredDominance);

  Fixture incompatibleStateDomains;
  connected(incompatibleStateDomains);
  size_t state = get64(incompatibleStateDomains.bytecode, 168);
  put32(incompatibleStateDomains.bytecode, state + 32 + 4, 0);
  rejected(incompatibleStateDomains);

  Fixture swappedConnectivityEndpoints;
  connected(swappedConnectivityEndpoints);
  connectivity = swappedConnectivityEndpoints.bytecode.size() - 32;
  put64(swappedConnectivityEndpoints.bytecode, connectivity, 65);
  put64(swappedConnectivityEndpoints.bytecode, connectivity + 8, 0);
  rejected(swappedConnectivityEndpoints);

  Fixture selfConnectivity;
  connected(selfConnectivity);
  connectivity = selfConnectivity.bytecode.size() - 32;
  put64(selfConnectivity.bytecode, connectivity + 8, 0);
  rejected(selfConnectivity);

  Fixture overlappingScalarConnectivity;
  connected(overlappingScalarConnectivity);
  connectivity = overlappingScalarConnectivity.bytecode.size() - 32;
  overlappingScalarConnectivity.bytecode.resize(
      overlappingScalarConnectivity.bytecode.size() + 32, 0);
  put64(overlappingScalarConnectivity.bytecode, connectivity + 16, 33);
  put64(overlappingScalarConnectivity.bytecode, connectivity + 32, 32);
  put64(overlappingScalarConnectivity.bytecode, connectivity + 40, 97);
  put64(overlappingScalarConnectivity.bytecode, connectivity + 48, 33);
  put64(overlappingScalarConnectivity.bytecode, 24,
        overlappingScalarConnectivity.bytecode.size());
  put64(overlappingScalarConnectivity.bytecode, 192, 2);
  rejected(overlappingScalarConnectivity);

  Fixture uncoalescedConnectivity;
  connected(uncoalescedConnectivity);
  connectivity = uncoalescedConnectivity.bytecode.size() - 32;
  uncoalescedConnectivity.bytecode.resize(
      uncoalescedConnectivity.bytecode.size() + 32, 0);
  put64(uncoalescedConnectivity.bytecode, connectivity + 16, 32);
  put64(uncoalescedConnectivity.bytecode, connectivity + 32, 32);
  put64(uncoalescedConnectivity.bytecode, connectivity + 40, 97);
  put64(uncoalescedConnectivity.bytecode, connectivity + 48, 33);
  put64(uncoalescedConnectivity.bytecode, 24,
        uncoalescedConnectivity.bytecode.size());
  put64(uncoalescedConnectivity.bytecode, 192, 2);
  rejected(uncoalescedConnectivity);

  Fixture abusiveConnectivityCount;
  connected(abusiveConnectivityCount);
  put64(abusiveConnectivityCount.bytecode, 192, UINT64_MAX);
  rejected(abusiveConnectivityCount);

  Fixture truncatedConnectivity;
  connected(truncatedConnectivity);
  truncatedConnectivity.bytecode.pop_back();
  put64(truncatedConnectivity.bytecode, 24,
        truncatedConnectivity.bytecode.size());
  rejected(truncatedConnectivity);

  Fixture overlappingUWireDrivers;
  overlappingUWireDrivers.bytecode = makeMixedUWireDriverBytecode(true);
  overlappingUWireDrivers.execution.bytecode =
      overlappingUWireDrivers.bytecode.data();
  overlappingUWireDrivers.execution.bytecode_size =
      overlappingUWireDrivers.bytecode.size();
  overlappingUWireDrivers.execution.state_bit_count = 195;
  overlappingUWireDrivers.execution.checksum =
      imageChecksum(overlappingUWireDrivers.bytecode);
  overlappingUWireDrivers.entry = {&overlappingUWireDrivers.execution, 0, 0};
  rejected(overlappingUWireDrivers);
}

TEST(DesignBytecode, InitializationBitsetsCoverWordBoundariesAndCFGJoins) {
  auto validate = [](std::vector<uint8_t> bytecode) {
    Fixture fixture;
    fixture.bytecode = std::move(bytecode);
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.checksum = imageChecksum(fixture.bytecode);
    fixture.entry = {&fixture.execution, 0, 0};
    uint64_t scratchSize = 0;
    uint64_t scratchAlignment = 0;
    return obelisk_rt_validate_design_bytecode(fixture.entry, nullptr,
                                               &scratchSize, &scratchAlignment);
  };

  EXPECT_EQ(validate(makeInitializationBoundaryBytecode(false)), OBELISK_RT_OK);
  EXPECT_EQ(validate(makeInitializationBoundaryBytecode(true)),
            OBELISK_RT_INVALID_BYTECODE);
}

TEST(DesignBytecode, ValidatesComparisonResultDomains) {
  auto validate = [](uint8_t resultKind, uint16_t comparisonKind) {
    Fixture fixture;
    fixture.bytecode = makeComparisonBytecode(resultKind, comparisonKind);
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.checksum = imageChecksum(fixture.bytecode);
    uint64_t scratchSize = 0;
    uint64_t scratchAlignment = 0;
    return obelisk_rt_validate_design_bytecode(fixture.entry, nullptr,
                                               &scratchSize, &scratchAlignment);
  };

  EXPECT_EQ(validate(OBELISK_RT_DBREG_LOGIC, OBELISK_RT_DB_CMP_WILD_EQ),
            OBELISK_RT_OK);
  EXPECT_EQ(validate(OBELISK_RT_DBREG_BITS, OBELISK_RT_DB_CMP_WILD_EQ),
            OBELISK_RT_OK);
  EXPECT_EQ(validate(OBELISK_RT_DBREG_BITS, OBELISK_RT_DB_CMP_CASEZ_EQ),
            OBELISK_RT_OK);
  EXPECT_EQ(validate(OBELISK_RT_DBREG_LOGIC, OBELISK_RT_DB_CMP_CASEZ_EQ),
            OBELISK_RT_INVALID_BYTECODE);
}

TEST(DesignBytecode, RejectsCorruptDynamicScanIntrinsicSignatures) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 3 * 40;
  constexpr size_t operandOffset = codeOffset + 2 * 32;
  constexpr size_t continuationOffset = operandOffset + 14 * 8;
  constexpr size_t intrinsicOffset = continuationOffset + 24;
  auto validate = [](std::vector<uint8_t> bytecode) {
    Fixture fixture;
    put64(bytecode, 32, imageChecksum(bytecode));
    fixture.bytecode = std::move(bytecode);
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.checksum = imageChecksum(fixture.bytecode);
    uint64_t scratchSize = 0;
    uint64_t scratchAlignment = 0;
    return obelisk_rt_validate_design_bytecode(fixture.entry, nullptr,
                                               &scratchSize, &scratchAlignment);
  };
  std::array<uint32_t, 3> ids{{
      OBELISK_RT_INTRINSIC_V1_STRING_SCAN_DYNAMIC,
      OBELISK_RT_INTRINSIC_V1_FILE_SCAN_DYNAMIC,
      OBELISK_RT_INTRINSIC_V1_SCAN_DYNAMIC_VALIDATE,
  }};
  for (uint32_t id : ids) {
    std::vector<uint8_t> valid = makeDynamicScanIntrinsicBytecode(id);
    EXPECT_EQ(validate(valid), OBELISK_RT_OK) << id;

    std::vector<uint8_t> arity = valid;
    put32(arity, intrinsicOffset + 4, 1);
    EXPECT_EQ(validate(std::move(arity)), OBELISK_RT_INVALID_BYTECODE) << id;

    std::vector<uint8_t> flags = valid;
    put32(flags, intrinsicOffset + 12, 1);
    EXPECT_EQ(validate(std::move(flags)), OBELISK_RT_INVALID_BYTECODE) << id;

    std::vector<uint8_t> stringKind = valid;
    stringKind[layoutOffset] = OBELISK_RT_DBREG_BITS;
    EXPECT_EQ(validate(std::move(stringKind)), OBELISK_RT_INVALID_BYTECODE)
        << id;

    std::vector<uint8_t> scalarKind = valid;
    scalarKind[layoutOffset + 40] = OBELISK_RT_DBREG_STRING;
    EXPECT_EQ(validate(std::move(scalarKind)), OBELISK_RT_INVALID_BYTECODE)
        << id;
  }
}

TEST(DesignBytecode, ValidatesContainerCreatePatternSignature) {
  constexpr uint64_t layoutOffset = 0;
  constexpr uint64_t intrinsicOffset = 12 * 40;
  constexpr uint64_t siteOffset = intrinsicOffset + 16;
  constexpr uint64_t operandOffset = siteOffset + 16;
  std::vector<uint8_t> bytes(operandOffset + 12 * 8, 0);
  auto layout = [&](uint32_t index, uint8_t kind, uint32_t width = 0) {
    uint64_t record = layoutOffset + uint64_t{index} * 40;
    bytes[record] = kind;
    put32(bytes, record + 4, width);
  };
  for (uint32_t index = 0; index != 7; ++index)
    layout(index, OBELISK_RT_DBREG_BITS, 64);
  layout(7, OBELISK_RT_DBREG_BYTES);
  layout(8, OBELISK_RT_DBREG_BYTES);
  layout(9, OBELISK_RT_DBREG_BITS, 64);
  layout(10, OBELISK_RT_DBREG_BITS, 64);
  layout(11, OBELISK_RT_DBREG_MANAGED);
  put32(bytes, intrinsicOffset, OBELISK_RT_INTRINSIC_V1_CONTAINER_CREATE);
  put32(bytes, intrinsicOffset + 4, 11);
  put32(bytes, intrinsicOffset + 8, 1);
  put32(bytes, siteOffset + 8, 11);
  put32(bytes, siteOffset + 12, 1);
  for (uint32_t index = 0; index != 11; ++index)
    put32(bytes, operandOffset + uint64_t{index} * 8 + 4, index);
  put32(bytes, operandOffset + 11 * 8, 11);

  obelisk::designbytecode::Image image{};
  image.data = bytes.data();
  image.size = bytes.size();
  image.layouts = layoutOffset;
  image.layoutCount = 12;
  image.intrinsics = intrinsicOffset;
  image.intrinsicCount = 1;
  image.sites = siteOffset;
  image.siteCount = 1;
  image.operands = operandOffset;
  image.operandCount = 12;
  obelisk::designbytecode::Function function{};
  function.layoutCount = 12;

  EXPECT_TRUE(obelisk::designbytecode::validIntrinsic(image, function, 0));
  bytes[8 * 40] = OBELISK_RT_DBREG_BITS;
  EXPECT_FALSE(obelisk::designbytecode::validIntrinsic(image, function, 0));
}

TEST(DesignBytecode, ValidatesManagedAggregateExtractionBounds) {
  auto validate = [](uint64_t bitOffset) {
    Fixture fixture;
    fixture.bytecode =
        makeComparisonBytecode(OBELISK_RT_DBREG_MANAGED, OBELISK_RT_DB_CMP_EQ);
    size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
    size_t layoutOffset = get64(fixture.bytecode, 56);
    size_t codeOffset = get64(fixture.bytecode, 72);
    fixture.bytecode[layoutOffset] = OBELISK_RT_DBREG_BITS;
    put32(fixture.bytecode, layoutOffset + 4, 128);
    put64(fixture.bytecode, layoutOffset + 16, 16);
    fixture.bytecode[layoutOffset + 80] = OBELISK_RT_DBREG_MANAGED;
    put32(fixture.bytecode, layoutOffset + 84, 64);
    put64(fixture.bytecode, layoutOffset + 96, 8);
    put32(fixture.bytecode, functionOffset + 48, 2);
    instruction(fixture.bytecode, codeOffset, 0, OBELISK_RT_DB_EXTRACT,
                OBELISK_RT_DB_AGGREGATE_MANAGED, 2, 0, UINT32_MAX, 0, 0,
                bitOffset);
    put64(fixture.bytecode, 32, imageChecksum(fixture.bytecode));
    fixture.execution.bytecode = fixture.bytecode.data();
    fixture.execution.bytecode_size = fixture.bytecode.size();
    fixture.execution.checksum = imageChecksum(fixture.bytecode);
    uint64_t scratchSize = 0;
    uint64_t scratchAlignment = 0;
    return obelisk_rt_validate_design_bytecode(fixture.entry, nullptr,
                                               &scratchSize, &scratchAlignment);
  };

  EXPECT_EQ(validate(64), OBELISK_RT_OK);
  EXPECT_EQ(validate(1), OBELISK_RT_INVALID_BYTECODE);
  EXPECT_EQ(validate(128), OBELISK_RT_INVALID_BYTECODE);
}

TEST(DesignBytecode, ContextTrustIsLimitedToItsValidatedExecutionImage) {
  Fixture trusted;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&trusted.execution, &context),
      OBELISK_RT_OK);
  std::array<uint8_t, 32> frame{};
  obelisk_rt_fragment_action_v1 action{};

  Fixture untrusted;
  untrusted.bytecode[0] ^= 1;
  EXPECT_EQ(obelisk_rt_execute_design_bytecode(untrusted.entry, context,
                                               frame.data(), frame.size(), 0,
                                               frame.size(), 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignBytecode, NativeAndBytecodeShareCanonicalDesignState) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_design_cursor_v1 object{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>("top.value"), 9, &object),
            OBELISK_RT_OK);

  std::array<uint8_t, 9> nativeValue{
      {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x01}};
  std::array<uint8_t, 9> nativeUnknown{{0x30}};
  std::array<uint8_t, 9> nativeGlobalValue{};
  std::array<uint8_t, 9> nativeGlobalUnknown{};
  uint8_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context, nativeGlobalValue.data(), 65, 0, 65, 0,
                nativeValue.data(), &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(changed, 1);
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context, nativeGlobalUnknown.data(), 65, 0, 65, 1,
                nativeUnknown.data(), &changed),
            OBELISK_RT_OK);

  std::array<uint64_t, 2> value{}, unknown{};
  ASSERT_EQ(obelisk_rt_v1_design_read(context, object, value.data(),
                                      unknown.data(), 65),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0], UINT64_C(0x123456789abcdef0));
  EXPECT_EQ(value[1], 1u);
  EXPECT_EQ(unknown[0], UINT64_C(0x30));

  value = {UINT64_C(0x0fedcba987654321), 0};
  unknown = {UINT64_C(0x0c), 1};
  ASSERT_EQ(obelisk_rt_v1_design_write(context, object, value.data(),
                                       unknown.data(), 65),
            OBELISK_RT_OK);
  std::array<uint8_t, 9> loadedValue{}, loadedUnknown{};
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, nativeGlobalValue.data(), 65, 0, 65, 0, 0,
                loadedValue.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_load_plane(
                context, nativeGlobalUnknown.data(), 65, 0, 65, 1, 0,
                loadedUnknown.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(loadedValue[0], 0x21);
  EXPECT_EQ(loadedValue[7], 0x0f);
  EXPECT_EQ(loadedValue[8], 0x00);
  EXPECT_EQ(loadedUnknown[0], 0x0c);
  EXPECT_EQ(loadedUnknown[8], 0x01);

  std::array<uint8_t, 1> nbaValue{{0x5a}}, nbaUnknown{{0xa0}};
  ASSERT_EQ(obelisk_rt_v1_scheduler_nba(context, nativeGlobalValue.data(),
                                        nativeGlobalUnknown.data(), 65, 0, 8, 0,
                                        nbaValue.data(), nbaUnknown.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_design_read(context, object, value.data(),
                                      unknown.data(), 65),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0] & UINT64_C(0xff), UINT64_C(0x5a));
  EXPECT_EQ(unknown[0] & UINT64_C(0xff), UINT64_C(0xa0));
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignDatabase, TraversesLooksUpAndAccessesLiveState) {
  Fixture fixture;
  ASSERT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);
  obelisk_rt_design_cursor_v1 root{}, object{}, found{};
  ASSERT_EQ(obelisk_rt_v1_design_root(&fixture.execution, &root),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_design_child(&fixture.execution, root, &object),
            OBELISK_RT_OK);
  constexpr std::string_view name = "top.value";
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>(name.data()), name.size(),
                &found),
            OBELISK_RT_OK);
  EXPECT_EQ(found.offset, object.offset);
  obelisk_rt_design_info_v1 info{};
  ASSERT_EQ(obelisk_rt_v1_design_info(&fixture.execution, found, &info),
            OBELISK_RT_OK);
  EXPECT_EQ(info.kind, OBELISK_RT_DESIGN_RECORD_STORAGE);
  EXPECT_EQ(info.handle.kind, OBELISK_RT_DESCRIPTOR_STORAGE);
  EXPECT_EQ(info.handle.id, 7u);
  EXPECT_EQ(info.bit_width, 65u);
  obelisk_rt_design_cursor_v1 indexed{};
  ASSERT_EQ(
      obelisk_rt_v1_design_child_at(&fixture.execution, root, 0, &indexed),
      OBELISK_RT_OK);
  EXPECT_EQ(indexed.offset, object.offset);
  EXPECT_EQ(
      obelisk_rt_v1_design_child_at(&fixture.execution, root, 1, &indexed),
      OBELISK_RT_EOF);
  obelisk_rt_design_type_info_v1 typeInfo{};
  ASSERT_EQ(obelisk_rt_v1_design_type_info(&fixture.execution,
                                           {info.type_offset}, &typeInfo),
            OBELISK_RT_OK);
  EXPECT_EQ(typeInfo.kind, OBELISK_RT_DESIGN_TYPE_SCALAR);
  EXPECT_EQ(typeInfo.flags,
            OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_PACKED);
  EXPECT_EQ(typeInfo.bit_width, 65u);
  EXPECT_EQ(typeInfo.range_left, 64);
  EXPECT_EQ(typeInfo.range_right, 0);
  EXPECT_EQ(obelisk_rt_v1_design_type_child(&fixture.execution,
                                            {info.type_offset}, 0, &indexed),
            OBELISK_RT_EOF);
  const uint8_t *typeName = nullptr;
  uint64_t typeNameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_design_name(&fixture.execution, {info.type_offset},
                                      &typeName, &typeNameSize),
            OBELISK_RT_OK);
  EXPECT_EQ(
      std::string_view(reinterpret_cast<const char *>(typeName), typeNameSize),
      "logic");

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  std::array<uint64_t, 2> value{{UINT64_C(0x123456789abcdef0), 1}};
  std::array<uint64_t, 2> unknown{{UINT64_C(0x30), 0}};
  ASSERT_EQ(obelisk_rt_v1_design_write(context, found, value.data(),
                                       unknown.data(), 65),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> readValue{}, readUnknown{};
  ASSERT_EQ(obelisk_rt_v1_design_read(context, found, readValue.data(),
                                      readUnknown.data(), 65),
            OBELISK_RT_OK);
  EXPECT_EQ(readValue, value);
  EXPECT_EQ(readUnknown, unknown);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignDatabase, ValidatedCacheTracksContextLifetime) {
  Fixture fixture;
  obelisk_rt_context *first = nullptr;
  obelisk_rt_context *second = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&fixture.execution, &first),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &second),
      OBELISK_RT_OK);

  constexpr std::string_view name = "top.value";
  obelisk_rt_design_cursor_v1 found{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>(name.data()), name.size(),
                &found),
            OBELISK_RT_OK);

  obelisk_rt_v1_context_destroy(first);
  ASSERT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(second);

  // The final context must remove the registered view. A subsequent checked
  // call must inspect the image rather than accepting stale validated state.
  fixture.database[224 + 16] ^= 1;
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);
}

TEST(DesignDatabase, TraversesStableProcessAndFunctionRecords) {
  Fixture fixture;
  fixture.database = makeCodeUnitDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;
  ASSERT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);

  obelisk_rt_design_cursor_v1 root{}, process{}, function{};
  ASSERT_EQ(obelisk_rt_v1_design_root(&fixture.execution, &root),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_design_child(&fixture.execution, root, &process),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_design_sibling(&fixture.execution, process, &function),
      OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_design_sibling(&fixture.execution, function, &root),
            OBELISK_RT_EOF);

  obelisk_rt_design_info_v1 info{};
  ASSERT_EQ(obelisk_rt_v1_design_info(&fixture.execution, process, &info),
            OBELISK_RT_OK);
  EXPECT_EQ(info.kind, OBELISK_RT_DESIGN_RECORD_PROCESS);
  EXPECT_EQ(info.handle.kind, OBELISK_RT_DESCRIPTOR_PROCESS);
  EXPECT_EQ(info.handle.id, 71u);
  EXPECT_EQ(info.type_offset, 0u);
  ASSERT_EQ(obelisk_rt_v1_design_info(&fixture.execution, function, &info),
            OBELISK_RT_OK);
  EXPECT_EQ(info.kind, OBELISK_RT_DESIGN_RECORD_FUNCTION);
  EXPECT_EQ(info.handle.kind, OBELISK_RT_DESCRIPTOR_FUNCTION);
  EXPECT_EQ(info.handle.id, 72u);

  constexpr std::string_view functionName = "top.fn";
  obelisk_rt_design_cursor_v1 found{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>(functionName.data()),
                functionName.size(), &found),
            OBELISK_RT_OK);
  EXPECT_EQ(found.offset, function.offset);

  // Version 1 code-unit records must remain pointer-free and state-free.
  std::vector<uint8_t> malformed = fixture.database;
  put64(malformed, 336 + 48, 432);
  put64(malformed, 32, imageChecksum(malformed));
  fixture.execution.design_database = malformed.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);
  put64(malformed, 336 + 48, 0);
  put32(malformed, 8, 2);
  put64(malformed, 32, imageChecksum(malformed));
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);
  put32(malformed, 8, OBELISK_RT_VERSION);
  put32(malformed, 12, 1);
  put64(malformed, 32, imageChecksum(malformed));
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);
}

TEST(DesignDatabase, SupportsImmutableSourceOnlyVPIObjects) {
  Fixture fixture;
  fixture.database = makeDatabase(false);
  constexpr uint64_t objectOffset = 240;
  put32(fixture.database, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                         vpiEnumTypespec));
  put32(fixture.database, objectOffset + 4, 0);
  // Static type metadata is independent of the 65-bit executable state image.
  put64(fixture.database, objectOffset + 56, 130);
  put64(fixture.database, objectOffset + 64, 129);
  constexpr uint64_t typeOffset = 336;
  put64(fixture.database, typeOffset + 8, 130);
  put64(fixture.database, typeOffset + 16, 129);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags &= ~OBELISK_RT_EXECUTION_VPI_WRITE;

  ASSERT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);
  obelisk_rt_design_cursor_v1 object{};
  constexpr std::string_view name = "top.value";
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>(name.data()), name.size(),
                &object),
            OBELISK_RT_OK);
  obelisk_rt_design_info_v1 info{};
  ASSERT_EQ(obelisk_rt_v1_design_info(&fixture.execution, object, &info),
            OBELISK_RT_OK);
  EXPECT_EQ(info.kind, OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT);
  EXPECT_EQ(info.handle.kind, OBELISK_RT_DESCRIPTOR_INVALID);
  EXPECT_EQ(info.bit_width, 130u);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  uint32_t exactType = 0;
  ASSERT_EQ(obelisk_rt_cached_vpi_type(context, object, &exactType),
            OBELISK_RT_OK);
  EXPECT_EQ(exactType, vpiEnumTypespec);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char mutableName[] = "top.value";
  vpiHandle handle = vpi_handle_by_name(mutableName, nullptr);
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(vpi_get(vpiType, handle), vpiEnumTypespec);
  EXPECT_EQ(vpi_get_str(vpiName, handle), nullptr);
  EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(context);

  put32(fixture.database, objectOffset + 4,
        OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  handle = vpi_handle_by_name(mutableName, nullptr);
  ASSERT_NE(handle, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiName, handle), "value");
  EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(context);

  for (uint32_t forbiddenKind : {vpiClassObj, vpiCallback, vpiIterator}) {
    std::vector<uint8_t> malformed = fixture.database;
    put32(malformed, objectOffset,
          designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                           forbiddenKind));
    put64(malformed, 32, imageChecksum(malformed));
    fixture.execution.design_database = malformed.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
              OBELISK_RT_INVALID_DESIGN);
  }
}

TEST(DesignDatabase, RejectsMalformedSemanticTraversalInventory) {
  Fixture fixture;
  fixture.database = makeSemanticTraversalDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;
  ASSERT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);

  constexpr uint64_t directoryOffset = 496;
  constexpr uint64_t semanticTypeOffset =
      directoryOffset + kSemanticDirectorySize;
  constexpr uint64_t semanticEdgeOffset = semanticTypeOffset + 11 * 64;
  auto reject = [&](std::vector<uint8_t> malformed) {
    put64(malformed, 32, imageChecksum(malformed));
    fixture.execution.design_database = malformed.data();
    fixture.execution.design_database_size = malformed.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
              OBELISK_RT_INVALID_DESIGN);
  };

  std::vector<uint8_t> malformed = fixture.database;
  put32(malformed, 12, static_cast<uint32_t>(malformed.size() - 4));
  reject(std::move(malformed));

  malformed = fixture.database;
  // The extension directory may not alias one of the sections it describes.
  put64(malformed, directoryOffset, directoryOffset);
  reject(std::move(malformed));

  malformed = fixture.database;
  put64(malformed, directoryOffset + 40, 2);
  reject(std::move(malformed));

  malformed = fixture.database;
  put64(malformed, directoryOffset + 40, 0);
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticTypeOffset,
        OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY);
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(
      malformed, semanticTypeOffset,
      OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY |
          OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE |
          (vpiBitTypespec << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticEdgeOffset, 99);
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticEdgeOffset + 2 * 24, 2);
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticEdgeOffset + 6 * 24 + 12, 0);
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticTypeOffset + 12, 0);
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticTypeOffset + 16, 0);
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticTypeOffset + 2 * 64,
        OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY |
            OBELISK_RT_DESIGN_SEMANTIC_WILDCARD_INDEX);
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticTypeOffset + 3 * 64,
        OBELISK_RT_DESIGN_SEMANTIC_UNTYPED);
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(
      malformed, semanticTypeOffset + 9 * 64,
      OBELISK_RT_DESIGN_SEMANTIC_BIT | OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE |
          (vpiBitTypespec << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticTypeOffset + 6 * 64,
        OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT |
            OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE |
            OBELISK_RT_DESIGN_SEMANTIC_TAGGED |
            (vpiStructTypespec
             << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
  reject(std::move(malformed));

  malformed = fixture.database;
  put32(malformed, semanticTypeOffset + 5 * 64,
        OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY |
            OBELISK_RT_DESIGN_SEMANTIC_SIGNED |
            OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE |
            OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE |
            (vpiPackedArrayTypespec
             << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT));
  reject(std::move(malformed));

  malformed = makeNamedSemanticTypespecDatabase();
  put32(
      malformed, 240,
      designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, vpiIntTypespec));
  reject(std::move(malformed));
}

TEST(VPI, StaticObjectsRequireExplicitTraversalRelations) {
  Fixture fixture;
  fixture.database = makeDatabase(false);
  constexpr uint64_t objectOffset = 240;
  constexpr uint64_t stringOffset = 416;
  constexpr uint64_t indexOffset = 448;
  put32(fixture.database, objectOffset,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, vpiClassDefn));
  put32(fixture.database, objectOffset + 4, 0);
  put64(fixture.database, objectOffset + 80, 0);
  std::memcpy(fixture.database.data() + stringOffset + 4, "pkg::Name", 9);
  struct Entry {
    uint64_t hash;
    uint64_t name;
    uint64_t record;
  };
  std::array<Entry, 2> index{{
      {nameHash("top"), stringOffset, 176},
      {nameHash("pkg::Name"), stringOffset + 4, objectOffset},
  }};
  std::sort(index.begin(), index.end(),
            [](const Entry &left, const Entry &right) {
              return std::tie(left.hash, left.name) <
                     std::tie(right.hash, right.name);
            });
  for (size_t entry = 0; entry != index.size(); ++entry) {
    put64(fixture.database, indexOffset + entry * 24, index[entry].hash);
    put64(fixture.database, indexOffset + entry * 24 + 8, index[entry].name);
    put64(fixture.database, indexOffset + entry * 24 + 16, index[entry].record);
  }
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags &= ~OBELISK_RT_EXECUTION_VPI_WRITE;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char moduleName[] = "top";
  char className[] = "pkg::Name";
  vpiHandle module = vpi_handle_by_name(moduleName, nullptr);
  vpiHandle classDefinition = vpi_handle_by_name(className, nullptr);
  ASSERT_NE(module, nullptr);
  ASSERT_NE(classDefinition, nullptr);
  EXPECT_EQ(vpi_get(vpiType, classDefinition), vpiClassDefn);
  EXPECT_STREQ(vpi_get_str(vpiName, classDefinition), "Name");
  EXPECT_STREQ(vpi_get_str(vpiFullName, classDefinition), "pkg::Name");
  // The static record is physically linked below top for image reachability,
  // but no semantic relation exposes it from this module.
  EXPECT_EQ(vpi_iterate(vpiClassDefn, module), nullptr);

  EXPECT_EQ(vpi_release_handle(classDefinition), 1);
  EXPECT_EQ(vpi_release_handle(module), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, TraversesExplicitStaticRelationsFromNullRoot) {
  Fixture fixture;
  fixture.database = makeRootStaticRelationDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags &= ~OBELISK_RT_EXECUTION_VPI_WRITE;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  vpiHandle iterator = vpi_iterate(vpiInstance, nullptr);
  ASSERT_NE(iterator, nullptr);
  vpiHandle package = vpi_scan(iterator);
  ASSERT_NE(package, nullptr);
  EXPECT_EQ(vpi_get(vpiType, package), vpiPackage);
  EXPECT_STREQ(vpi_get_str(vpiName, package), "pkg");
  EXPECT_STREQ(vpi_get_str(vpiFullName, package), "pkg::");
  EXPECT_EQ(vpi_scan(iterator), nullptr);

  char packageName[] = "pkg";
  vpiHandle byName = vpi_handle_by_name(packageName, nullptr);
  ASSERT_NE(byName, nullptr);
  EXPECT_EQ(vpi_get(vpiType, byName), vpiPackage);

  char className[] = "C";
  vpiHandle classDefinition = vpi_handle_by_name(className, package);
  ASSERT_NE(classDefinition, nullptr);
  EXPECT_EQ(vpi_get(vpiType, classDefinition), vpiClassDefn);
  EXPECT_STREQ(vpi_get_str(vpiFullName, classDefinition), "pkg::C");

  vpiHandle classes = vpi_iterate(vpiClassDefn, package);
  ASSERT_NE(classes, nullptr);
  vpiHandle traversedClass = vpi_scan(classes);
  ASSERT_NE(traversedClass, nullptr);
  EXPECT_EQ(vpi_compare_objects(traversedClass, classDefinition), 1);
  EXPECT_EQ(vpi_scan(classes), nullptr);
  vpiHandle classParent = vpi_handle(vpiScope, classDefinition);
  ASSERT_NE(classParent, nullptr);
  EXPECT_EQ(vpi_compare_objects(classParent, package), 1);

  char nestedName[] = "Nested";
  vpiHandle nestedClass = vpi_handle_by_name(nestedName, classDefinition);
  ASSERT_NE(nestedClass, nullptr);
  EXPECT_EQ(vpi_get(vpiType, nestedClass), vpiClassDefn);
  EXPECT_STREQ(vpi_get_str(vpiFullName, nestedClass), "pkg::C::Nested");

  vpiHandle nestedScopes = vpi_iterate(vpiInternalScope, classDefinition);
  ASSERT_NE(nestedScopes, nullptr);
  vpiHandle traversedNested = vpi_scan(nestedScopes);
  ASSERT_NE(traversedNested, nullptr);
  EXPECT_EQ(vpi_compare_objects(traversedNested, nestedClass), 1);
  vpiHandle method = vpi_scan(nestedScopes);
  ASSERT_NE(method, nullptr);
  EXPECT_EQ(vpi_get(vpiType, method), vpiFunction);
  EXPECT_STREQ(vpi_get_str(vpiName, method), "method");
  EXPECT_STREQ(vpi_get_str(vpiFullName, method), "pkg::C::method");
  EXPECT_EQ(vpi_scan(nestedScopes), nullptr);
  vpiHandle nestedParent = vpi_handle(vpiScope, nestedClass);
  ASSERT_NE(nestedParent, nullptr);
  EXPECT_EQ(vpi_compare_objects(nestedParent, classDefinition), 1);
  vpiHandle methodParent = vpi_handle(vpiScope, method);
  ASSERT_NE(methodParent, nullptr);
  EXPECT_EQ(vpi_compare_objects(methodParent, classDefinition), 1);

  EXPECT_EQ(vpi_release_handle(methodParent), 1);
  EXPECT_EQ(vpi_release_handle(method), 1);
  EXPECT_EQ(vpi_release_handle(nestedParent), 1);
  EXPECT_EQ(vpi_release_handle(traversedNested), 1);
  EXPECT_EQ(vpi_release_handle(nestedClass), 1);
  EXPECT_EQ(vpi_release_handle(classParent), 1);
  EXPECT_EQ(vpi_release_handle(traversedClass), 1);
  EXPECT_EQ(vpi_release_handle(classDefinition), 1);
  EXPECT_EQ(vpi_release_handle(byName), 1);
  EXPECT_EQ(vpi_release_handle(package), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, LexicalMethodDoesNotLeakThroughItsPhysicalModule) {
  Fixture fixture;
  fixture.database = makeClassMethodRelationDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags &= ~OBELISK_RT_EXECUTION_VPI_WRITE;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char moduleName[] = "top";
  char className[] = "pkg::C";
  vpiHandle module = vpi_handle_by_name(moduleName, nullptr);
  vpiHandle classDefinition = vpi_handle_by_name(className, nullptr);
  ASSERT_NE(module, nullptr);
  ASSERT_NE(classDefinition, nullptr);

  // The executable record is physically reachable below top, but only its
  // generated lexical relations expose it through VPI.
  EXPECT_EQ(vpi_iterate(vpiTaskFunc, module), nullptr);

  vpiHandle internalScopes = vpi_iterate(vpiInternalScope, classDefinition);
  ASSERT_NE(internalScopes, nullptr);
  vpiHandle internalMethod = vpi_scan(internalScopes);
  ASSERT_NE(internalMethod, nullptr);
  EXPECT_EQ(vpi_get(vpiType, internalMethod), vpiFunction);
  EXPECT_EQ(vpi_scan(internalScopes), nullptr);

  vpiHandle methods = vpi_iterate(vpiMethods, classDefinition);
  ASSERT_NE(methods, nullptr);
  vpiHandle method = vpi_scan(methods);
  ASSERT_NE(method, nullptr);
  EXPECT_EQ(vpi_compare_objects(method, internalMethod), 1);
  EXPECT_EQ(vpi_scan(methods), nullptr);

  vpiHandle owner = vpi_handle(vpiScope, method);
  ASSERT_NE(owner, nullptr);
  EXPECT_EQ(vpi_compare_objects(owner, classDefinition), 1);

  EXPECT_EQ(vpi_release_handle(owner), 1);
  EXPECT_EQ(vpi_release_handle(method), 1);
  EXPECT_EQ(vpi_release_handle(internalMethod), 1);
  EXPECT_EQ(vpi_release_handle(classDefinition), 1);
  EXPECT_EQ(vpi_release_handle(module), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(DesignDatabase, DoesNotTreatDerivedClassesAsLexicalContainment) {
  Fixture fixture;
  fixture.database = makeRootStaticRelationDatabase();
  constexpr uint64_t relations = 624;
  // Replace the nested-class containment pair with two legal semantic
  // vpiDerivedClasses edges. Neither direction implies lexical ownership.
  put32(fixture.database, relations + 48 + 4,
        (uint32_t{1} << 30) | uint32_t{3});
  put32(fixture.database, relations + 64 + 4,
        (uint32_t{1} << 30) | uint32_t{2});
  put32(fixture.database, relations + 64 + 8, 0);
  put16(fixture.database, relations + 64 + 12, vpiDerivedClasses);
  put16(fixture.database, relations + 80 + 12, vpiDerivedClasses);
  put16(fixture.database, relations + 80 + 14,
        designRelationSource(1, vpiClassDefn, true));
  put64(fixture.database, 32, imageChecksum(fixture.database));
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags &= ~OBELISK_RT_EXECUTION_VPI_WRITE;
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);
}

TEST(DesignDatabase, RejectsMalformedIntrinsicVPIKinds) {
  auto expectRejected = [](std::vector<uint8_t> database) {
    put64(database, 32, imageChecksum(database));
    Fixture fixture;
    fixture.database = std::move(database);
    fixture.execution.design_database = fixture.database.data();
    fixture.execution.design_database_size = fixture.database.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
              OBELISK_RT_INVALID_DESIGN);
  };

  std::vector<uint8_t> missingStorageKind = makeDatabase();
  put32(missingStorageKind, 240, OBELISK_RT_DESIGN_RECORD_STORAGE);
  expectRejected(std::move(missingStorageKind));

  std::vector<uint8_t> nonConcreteStorageKind = makeDatabase();
  put32(nonConcreteStorageKind, 240,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_STORAGE, vpiMemory));
  expectRejected(std::move(nonConcreteStorageKind));

  std::vector<uint8_t> reservedTypePayload = makeDatabase();
  put32(reservedTypePayload, 336,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_TYPE, vpiReg));
  expectRejected(std::move(reservedTypePayload));

  std::vector<uint8_t> missingNestedScopeKind = makeStatementDatabase();
  put32(missingNestedScopeKind, 240, OBELISK_RT_DESIGN_RECORD_SCOPE);
  expectRejected(std::move(missingNestedScopeKind));

  std::vector<uint8_t> missingRelationSourceKind = makeStatementDatabase();
  put16(missingRelationSourceKind, 568 + 14, designRelationSource(1, 0));
  expectRejected(std::move(missingRelationSourceKind));

  std::vector<uint8_t> rootKindOnObject = makeRootStaticRelationDatabase();
  put16(rootKindOnObject, 624 + 16 + 14, designRelationSource(1, 0, true));
  expectRejected(std::move(rootKindOnObject));

  std::vector<uint8_t> mismatchedStaticParent =
      makeRootStaticRelationDatabase();
  put32(mismatchedStaticParent, 624 + 32 + 4,
        (uint32_t{1} << 30) | uint32_t{2});
  expectRejected(std::move(mismatchedStaticParent));

  std::vector<uint8_t> rootKindOnNestedScope =
      makeNestedModuleRelationDatabase();
  put16(rootKindOnNestedScope, 368 + 16 + 14, designRelationSource(0, 0, true));
  expectRejected(std::move(rootKindOnNestedScope));

  std::vector<uint8_t> unmarkedInternalProcess = makeCodeUnitDatabase();
  put32(unmarkedInternalProcess, 240,
        designRecordKind(OBELISK_RT_DESIGN_RECORD_PROCESS, 0));
  expectRejected(std::move(unmarkedInternalProcess));
}

TEST(DesignDatabase, ValidatesCompactStatementAndSemanticSiteInventory) {
  Fixture fixture;
  fixture.database = makeStatementDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_VPI_READ;
  ASSERT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);
  constexpr size_t statements = 400;
  constexpr size_t sites = 520;
  constexpr size_t relations = 568;

  // The LRM permits both singular and iterative vpiForInitStmt traversal. A
  // merged MLIR declaration becomes two mode-tagged wire records at ordinal
  // zero.
  std::vector<uint8_t> dualMode = fixture.database;
  put16(dualMode, statements + 36, vpiFor);
  put16(dualMode, statements + 38, 0);
  put32(dualMode, statements + 24, 0);
  put16(dualMode, statements + 40 + 36, vpiNullStmt);
  put64(dualMode, 152, 2);
  put32(dualMode, sites + 8, 0);
  put16(dualMode, sites + 12, 1);
  put32(dualMode, sites + 16 + 8, 0);
  put16(dualMode, sites + 16 + 12, 2);
  put16(dualMode, relations + 16 + 12, vpiForInitStmt);
  put16(dualMode, relations + 16 + 14, designRelationSource(2, vpiFor));
  put16(dualMode, relations + 32 + 12, vpiForInitStmt);
  put16(dualMode, relations + 32 + 14, designRelationSource(2, vpiFor, true));
  put32(dualMode, relations + 32, 0);
  put32(dualMode, relations + 32 + 4, (uint32_t{2} << 30) | 1);
  put32(dualMode, relations + 32 + 8, 0);
  put64(dualMode, 136, 2);
  put64(dualMode, 32, imageChecksum(dualMode));
  fixture.execution.design_database = dualMode.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);

  // Scope-owned records use the exact module traversal selectors rather than
  // the behavioral vpiStmt relation. Cover a dense module continuous-assign
  // iterator followed by a distinct alias-statement iterator.
  std::vector<uint8_t> scopeRelations = fixture.database;
  for (size_t index = 0; index != 3; ++index) {
    size_t statement = statements + index * 40;
    put32(scopeRelations, statement + 8, UINT32_MAX);
    put32(scopeRelations, statement + 16, UINT32_MAX);
    put32(scopeRelations, statement + 24, 0);
    put16(scopeRelations, statement + 38, 0);
  }
  put16(scopeRelations, statements + 36, vpiContAssign);
  put16(scopeRelations, statements + 40 + 36, vpiContAssign);
  put16(scopeRelations, statements + 80 + 36, vpiAliasStmt);
  put64(scopeRelations, 152, 0);
  auto scopeRelation = [&](size_t index, uint32_t target, uint32_t ordinal,
                           uint16_t selector) {
    size_t relation = relations + index * 16;
    put32(scopeRelations, relation, 1);
    put32(scopeRelations, relation + 4, (uint32_t{2} << 30) | target);
    put32(scopeRelations, relation + 8, ordinal);
    put16(scopeRelations, relation + 12, selector);
    put16(scopeRelations, relation + 14,
          designRelationSource(0, vpiModule, true));
  };
  scopeRelation(0, 0, 0, vpiContAssign);
  scopeRelation(1, 1, 1, vpiContAssign);
  scopeRelation(2, 2, 0, vpiAliasStmt);
  put64(scopeRelations, 32, imageChecksum(scopeRelations));
  fixture.execution.design_database = scopeRelations.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);

  // Continuous assignments and alias statements are owned by their hierarchy
  // scope rather than by a process/function object.
  std::vector<uint8_t> scopeOwned = fixture.database;
  put32(scopeOwned, 400 + 40 + 8, UINT32_MAX);
  put32(scopeOwned, 400 + 40 + 16, UINT32_MAX);
  put32(scopeOwned, 400 + 80 + 16, 0);
  put64(scopeOwned, 152, 1);
  put64(scopeOwned, 168, 0);
  for (uint16_t kind : {vpiContAssign, vpiContAssignBit, vpiAliasStmt}) {
    put16(scopeOwned, 400 + 40 + 36, kind);
    put64(scopeOwned, 32, imageChecksum(scopeOwned));
    fixture.execution.design_database = scopeOwned.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK)
        << kind;
  }

  std::vector<uint8_t> scopeOwnedWithProcess = scopeOwned;
  put32(scopeOwnedWithProcess, 400 + 40 + 8, 0);
  put64(scopeOwnedWithProcess, 32, imageChecksum(scopeOwnedWithProcess));
  fixture.execution.design_database = scopeOwnedWithProcess.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);

  std::vector<uint8_t> crossScopeParent = fixture.database;
  put32(crossScopeParent, 400 + 8, UINT32_MAX);
  put32(crossScopeParent, 400 + 12, 0);
  put32(crossScopeParent, 400 + 24, 0);
  put16(crossScopeParent, 400 + 36, vpiContAssign);
  put32(crossScopeParent, 400 + 40 + 8, UINT32_MAX);
  put16(crossScopeParent, 400 + 40 + 36, vpiContAssignBit);
  put64(crossScopeParent, 152, 0);
  put64(crossScopeParent, 32, imageChecksum(crossScopeParent));
  fixture.execution.design_database = crossScopeParent.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);

  std::vector<uint8_t> behavioralWithoutProcess = fixture.database;
  put32(behavioralWithoutProcess, 400 + 40 + 8, UINT32_MAX);
  put64(behavioralWithoutProcess, 32, imageChecksum(behavioralWithoutProcess));
  fixture.execution.design_database = behavioralWithoutProcess.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);

  // Static traversal inventory includes statement kinds that are not eligible
  // for cbStmt, provided they have no semantic callback sites.
  std::vector<uint8_t> nonCallback = fixture.database;
  put16(nonCallback, 400 + 40 + 36, vpiNullStmt);
  put32(nonCallback, 400 + 80 + 16, 0);
  put32(nonCallback, relations + 32, 0);
  put32(nonCallback, relations + 32 + 8, 1);
  put16(nonCallback, relations + 32 + 14,
        designRelationSource(2, vpiNamedBegin, true));
  put64(nonCallback, 152, 1);
  put64(nonCallback, 32, imageChecksum(nonCallback));
  fixture.execution.design_database = nonCallback.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);

  std::vector<uint8_t> foreachWithoutScope = fixture.database;
  put16(foreachWithoutScope, statements + 40 + 36, vpiForeachStmt);
  put16(foreachWithoutScope, relations + 16 + 14,
        designRelationSource(2, vpiForeachStmt, true));
  put64(foreachWithoutScope, 32, imageChecksum(foreachWithoutScope));
  fixture.execution.design_database = foreachWithoutScope.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);

  auto rejected = [&](size_t offset, uint64_t value, unsigned width = 4) {
    std::vector<uint8_t> malformed = fixture.database;
    if (width == 8)
      put64(malformed, offset, value);
    else if (width == 2)
      put16(malformed, offset, static_cast<uint16_t>(value));
    else
      put32(malformed, offset, static_cast<uint32_t>(value));
    put64(malformed, 32, imageChecksum(malformed));
    fixture.execution.design_database = malformed.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
              OBELISK_RT_INVALID_DESIGN);
  };
  rejected(statements, 0, 8);                     // zero statement ID
  rejected(statements + 40, 50, 8);               // unsorted statement IDs
  rejected(statements + 40, 100, 8);              // duplicate ID
  rejected(statements + 40 + 36, vpiNullStmt, 2); // site on non-cbStmt
  rejected(statements + 16, 1);                   // parent cycle
  rejected(statements + 40 + 8, 1);               // missing owner
  rejected(statements + 12, 0);                   // scope differs from owner
  rejected(statements + 16, 2);                   // out-of-range parent
  rejected(statements + 20, 0);                   // source without line
  rejected(statements + 20, 42);                  // source string out of range
  rejected(statements + 24, 0);                   // named block without name
  rejected(statements + 40 + 24, 37);             // name on non-named statement
  rejected(statements + 38, 4, 2);                // reserved statement flags
  rejected(sites, 0, 8);                          // zero site ID
  rejected(sites + 16, 1000, 8);                  // duplicate site ID
  rejected(sites + 16, 900, 8);                   // unsorted site IDs
  rejected(sites + 8, 3);                         // missing statement target
  rejected(sites + 12, 2, 2);                     // illegal named-block phase
  rejected(sites + 16 + 12, 2, 2);                // duplicate for phase
  rejected(sites + 32 + 12, 0, 2);                // missing for increment
  rejected(sites + 14, 1, 2);                     // reserved flags
  rejected(128, 176, 8);                          // statement/scope overlap
  rejected(144, statements, 8);                   // site/statement overlap
  rejected(128, fixture.database.size() + 1, 8);  // statement past image
  rejected(144, fixture.database.size() + 1, 8);  // site past image
  rejected(136, UINT64_MAX, 8);                   // statement span overflow
  rejected(152, UINT64_MAX, 8);                   // site span overflow
  rejected(136, uint64_t{UINT32_MAX} + 1, 8);     // statement index cap
  rejected(152, uint64_t{UINT32_MAX} + 1, 8);     // site index cap
  rejected(relations, 1);                         // missing source object
  rejected(relations + 4, (uint32_t{2} << 30) | 3); // missing statement
  rejected(relations + 4, (uint32_t{1} << 30) | 1); // missing object
  rejected(relations + 4, uint32_t{3} << 30);       // reserved target table
  rejected(relations + 4, 1);     // scope kind is outside vpiStmt target set
  rejected(relations + 8, 1);     // non-dense handle ordinal
  rejected(relations + 12, 0, 2); // illegal selector
  rejected(relations + 14, designRelationSource(1, vpiInitial, true),
           2); // selector is not legal in iterate mode
  rejected(relations + 14, uint16_t{3u << 14} | vpiInitial, 2);
  rejected(relations + 16 + 14, designRelationSource(2, vpiFor, true), 2);
  rejected(relations + 16 + 4, uint32_t{2} << 30); // duplicate incoming edge
  rejected(relations + 32 + 8, 2);                 // iterate ordinal gap
  rejected(relations + 32 + 14, designRelationSource(2, vpiNamedFork),
           2);                                    // source kind inconsistency
  rejected(statements + 80 + 16, UINT32_MAX);     // target ownership mismatch
  rejected(statements + 80 + 36, vpiCaseItem, 2); // illegal traversal target
  {
    std::vector<uint8_t> malformed = fixture.database;
    std::swap_ranges(malformed.begin() + relations + 16,
                     malformed.begin() + relations + 32,
                     malformed.begin() + relations + 32);
    put64(malformed, 32, imageChecksum(malformed));
    fixture.execution.design_database = malformed.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
              OBELISK_RT_INVALID_DESIGN);
  }
  rejected(160, 176, 8);                         // relation/header overlap
  rejected(160, statements, 8);                  // relation/statement overlap
  rejected(160, fixture.database.size() + 1, 8); // relation past image
  rejected(168, UINT64_MAX, 8);                  // relation span overflow
  rejected(168, uint64_t{UINT32_MAX} + 1, 8);    // relation index cap

  fixture.execution.design_database = fixture.database.data();
  fixture.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                            OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                            OBELISK_RT_EXECUTION_WAVEFORM_METADATA;
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);
}

TEST(DesignDatabase, TraversesRecursiveAggregateTypesAndRejectsCycles) {
  Fixture fixture;
  fixture.database = makeAggregateDatabase();
  fixture.execution.design_database = fixture.database.data();
  fixture.execution.design_database_size = fixture.database.size();
  ASSERT_EQ(obelisk_rt_v1_design_validate(&fixture.execution), OBELISK_RT_OK);

  obelisk_rt_design_cursor_v1 object{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &fixture.execution,
                reinterpret_cast<const uint8_t *>("top.value"), 9, &object),
            OBELISK_RT_OK);
  obelisk_rt_design_info_v1 objectInfo{};
  ASSERT_EQ(obelisk_rt_v1_design_info(&fixture.execution, object, &objectInfo),
            OBELISK_RT_OK);

  obelisk_rt_design_type_info_v1 structInfo{};
  ASSERT_EQ(obelisk_rt_v1_design_type_info(
                &fixture.execution, {objectInfo.type_offset}, &structInfo),
            OBELISK_RT_OK);
  EXPECT_EQ(structInfo.kind, OBELISK_RT_DESIGN_TYPE_STRUCT);
  EXPECT_EQ(structInfo.flags,
            OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_PACKED);
  EXPECT_EQ(structInfo.child_count, 1u);

  obelisk_rt_design_cursor_v1 field{};
  ASSERT_EQ(obelisk_rt_v1_design_type_child(
                &fixture.execution, {objectInfo.type_offset}, 0, &field),
            OBELISK_RT_OK);
  obelisk_rt_design_type_info_v1 fieldInfo{};
  ASSERT_EQ(
      obelisk_rt_v1_design_type_info(&fixture.execution, field, &fieldInfo),
      OBELISK_RT_OK);
  EXPECT_EQ(fieldInfo.kind, OBELISK_RT_DESIGN_TYPE_FIELD);
  EXPECT_EQ(fieldInfo.ordinal, 0u);
  EXPECT_EQ(fieldInfo.packed_offset, 0u);

  const uint8_t *name = nullptr;
  uint64_t nameSize = 0;
  ASSERT_EQ(
      obelisk_rt_v1_design_name(&fixture.execution, field, &name, &nameSize),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string_view(reinterpret_cast<const char *>(name), nameSize),
            "value");
  obelisk_rt_design_type_info_v1 scalarInfo{};
  ASSERT_EQ(obelisk_rt_v1_design_type_info(&fixture.execution,
                                           fieldInfo.element_type, &scalarInfo),
            OBELISK_RT_OK);
  EXPECT_EQ(scalarInfo.kind, OBELISK_RT_DESIGN_TYPE_SCALAR);
  EXPECT_EQ(scalarInfo.bit_width, 65u);
  EXPECT_EQ(scalarInfo.range_left, 64);

  constexpr uint64_t fieldTypeOffset = 336 + 80;
  constexpr uint64_t rootTypeOffset = 336;
  put64(fixture.database, fieldTypeOffset + 32, rootTypeOffset);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);
}

TEST(DesignDatabase, RejectsCorruptionAndUnauthorizedWrites) {
  Fixture sourceMetadata;
  constexpr uint64_t scopeOffset = 176;
  constexpr uint64_t stringOffset = 416;
  put64(sourceMetadata.database, scopeOffset + 48, stringOffset);
  put64(sourceMetadata.database, scopeOffset + 56, (UINT64_C(12) << 32) | 7);
  put64(sourceMetadata.database, 32, imageChecksum(sourceMetadata.database));
  EXPECT_EQ(obelisk_rt_v1_design_validate(&sourceMetadata.execution),
            OBELISK_RT_OK);
  put64(sourceMetadata.database, scopeOffset + 56, UINT64_C(12) << 32);
  put64(sourceMetadata.database, 32, imageChecksum(sourceMetadata.database));
  EXPECT_EQ(obelisk_rt_v1_design_validate(&sourceMetadata.execution),
            OBELISK_RT_INVALID_DESIGN);

  Fixture inconsistentScalarRange;
  constexpr uint64_t typeOffset = 336;
  put64(inconsistentScalarRange.database, typeOffset + 16, 63);
  put64(inconsistentScalarRange.database, 32,
        imageChecksum(inconsistentScalarRange.database));
  EXPECT_EQ(obelisk_rt_v1_design_validate(&inconsistentScalarRange.execution),
            OBELISK_RT_INVALID_DESIGN);

  Fixture corrupt;
  corrupt.database[240 + 16] ^= 1;
  EXPECT_EQ(obelisk_rt_v1_design_validate(&corrupt.execution),
            OBELISK_RT_INVALID_DESIGN);
  obelisk_rt_context *context = nullptr;
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&corrupt.execution, &context),
      OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);

  Fixture outOfBounds;
  outOfBounds.execution.state_bit_count = 64;
  EXPECT_EQ(obelisk_rt_v1_design_validate(&outOfBounds.execution),
            OBELISK_RT_INVALID_DESIGN);

  Fixture twoState;
  put32(twoState.database, typeOffset + 4,
        OBELISK_RT_DESIGN_TYPE_SCALAR | (OBELISK_RT_DESIGN_TYPE_PACKED << 8));
  put64(twoState.database, 32, imageChecksum(twoState.database));
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&twoState.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_design_cursor_v1 twoStateObject{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &twoState.execution,
                reinterpret_cast<const uint8_t *>("top.value"), 9,
                &twoStateObject),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> twoStateValue{{UINT64_MAX, 1}};
  std::array<uint64_t, 2> ignoredUnknown{{UINT64_MAX, 1}};
  ASSERT_EQ(obelisk_rt_v1_design_write(context, twoStateObject,
                                       twoStateValue.data(),
                                       ignoredUnknown.data(), 65),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> readValue{}, readUnknown{{UINT64_MAX, UINT64_MAX}};
  ASSERT_EQ(obelisk_rt_v1_design_read(context, twoStateObject, readValue.data(),
                                      readUnknown.data(), 65),
            OBELISK_RT_OK);
  EXPECT_EQ(readValue, twoStateValue);
  EXPECT_EQ(readUnknown, (std::array<uint64_t, 2>{0, 0}));
  obelisk_rt_v1_context_destroy(context);
  context = nullptr;

  Fixture readOnly;
  readOnly.database = makeDatabase(false);
  readOnly.execution.design_database = readOnly.database.data();
  readOnly.execution.design_database_size = readOnly.database.size();
  readOnly.execution.flags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                             OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                             OBELISK_RT_EXECUTION_VPI_READ;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&readOnly.execution, &context),
      OBELISK_RT_OK);
  obelisk_rt_design_cursor_v1 object{};
  ASSERT_EQ(obelisk_rt_v1_design_lookup(
                &readOnly.execution,
                reinterpret_cast<const uint8_t *>("top.value"), 9, &object),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> value{};
  EXPECT_EQ(
      obelisk_rt_v1_design_write(context, object, value.data(), nullptr, 65),
      OBELISK_RT_PERMISSION_DENIED);
  obelisk_rt_v1_context_destroy(context);
}

} // namespace
