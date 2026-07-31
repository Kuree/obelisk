//===- DesignBytecodeTest.cpp - Design bytecode/reflection tests ----------===//

#include "../lib/RuntimeInternal.h"
#include "obelisk/Runtime/Runtime.h"

#include "vpi_user.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

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

std::vector<uint8_t> makeSchedulerBytecode(uint64_t stateHandle = 0) {
  constexpr size_t functionOffset = OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE;
  constexpr size_t layoutOffset = functionOffset + 96;
  constexpr size_t codeOffset = layoutOffset + 4 * 40;
  constexpr size_t operandOffset = codeOffset + 7 * 32;
  constexpr size_t constantOffset = operandOffset + 4 * 8;
  constexpr size_t continuationOffset = constantOffset + 24;
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
  put64(bytes, 112, 24);
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
  instruction(bytes, codeOffset, 4, OBELISK_RT_DB_MAKE_HANDLE, 0, 3,
              OBELISK_RT_DESCRIPTOR_EVENT, 0, 0, 0, 7);
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

std::vector<uint8_t> makePartialUWireDriverBytecode(bool overlap) {
  std::vector<uint8_t> bytes = makeConnectedDriverBytecode();
  size_t stateOffset = get64(bytes, 168);
  size_t connectivityOffset = get64(bytes, 184);
  bytes.insert(bytes.begin() + connectivityOffset, 32, 0);

  // Both logical nets and both driver slices are unresolved. The first
  // driver targets aliased component zero; the second either targets distinct
  // component one or intentionally overlaps component zero.
  put32(bytes, stateOffset + 4, 5);
  put32(bytes, stateOffset + 32 + 4, 5);
  put32(bytes, stateOffset + 64 + 4, 5);
  put64(bytes, stateOffset + 64 + 24, 1);
  put32(bytes, connectivityOffset, UINT32_MAX);
  put32(bytes, connectivityOffset + 4, 5);
  put64(bytes, connectivityOffset + 8, 131);
  put64(bytes, connectivityOffset + 16, overlap ? 65 : 66);
  put64(bytes, connectivityOffset + 24, 1);

  size_t movedConnectivity = connectivityOffset + 32;
  bytes[movedConnectivity + 24] = 2;
  bytes[movedConnectivity + 25] = 2;
  put64(bytes, 24, bytes.size());
  put64(bytes, 176, 4);
  put64(bytes, 184, movedConnectivity);
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

std::vector<uint8_t> makeDatabase(bool writable = true) {
  constexpr uint64_t scopeOffset = 128;
  constexpr uint64_t objectOffset = 192;
  constexpr uint64_t typeOffset = 288;
  constexpr uint64_t stringOffset = 368;
  constexpr uint64_t stringSize = 20;
  constexpr uint64_t indexOffset = 392;
  std::vector<uint8_t> bytes(indexOffset + 48, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16,
        OBELISK_RT_DESIGN_PROFILE_READ |
            (writable ? OBELISK_RT_DESIGN_PROFILE_WRITE : 0));
  put32(bytes, 20, 128);
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

  put32(bytes, scopeOffset, OBELISK_RT_DESIGN_RECORD_SCOPE);
  put32(bytes, scopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, scopeOffset + 8, 1);
  put64(bytes, scopeOffset + 24, objectOffset);
  put64(bytes, scopeOffset + 40, stringOffset);

  put32(bytes, objectOffset, OBELISK_RT_DESIGN_RECORD_STORAGE);
  put32(bytes, objectOffset + 4,
        OBELISK_RT_DESIGN_CAP_READ |
            (writable ? OBELISK_RT_DESIGN_CAP_WRITE : 0));
  put64(bytes, objectOffset + 8, 7);
  put64(bytes, objectOffset + 16, scopeOffset);
  put64(bytes, objectOffset + 40, stringOffset + 4);
  put64(bytes, objectOffset + 48, typeOffset);
  put64(bytes, objectOffset + 56, 65);
  put64(bytes, objectOffset + 64, 64);
  put64(bytes, objectOffset + 72, 0);
  put64(bytes, objectOffset + 80, 0);

  put32(bytes, typeOffset, OBELISK_RT_DESIGN_RECORD_TYPE);
  put32(bytes, typeOffset + 4,
        OBELISK_RT_DESIGN_TYPE_SCALAR |
            ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_PACKED)
             << 8));
  put64(bytes, typeOffset + 8, 65);
  put64(bytes, typeOffset + 16, 64);
  put64(bytes, typeOffset + 72, stringOffset + 14);
  std::memcpy(bytes.data() + stringOffset, "top\0top.value\0logic\0",
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

std::vector<uint8_t> makeCodeUnitDatabase() {
  constexpr uint64_t scopeOffset = 128;
  constexpr uint64_t processOffset = 192;
  constexpr uint64_t functionOffset = 288;
  constexpr uint64_t stringOffset = 384;
  constexpr uint64_t stringSize = 20;
  constexpr uint64_t indexOffset = 408;
  std::vector<uint8_t> bytes(indexOffset + 72, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16, OBELISK_RT_DESIGN_PROFILE_READ);
  put32(bytes, 20, 128);
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

  put32(bytes, scopeOffset, OBELISK_RT_DESIGN_RECORD_SCOPE);
  put32(bytes, scopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, scopeOffset + 8, 1);
  put64(bytes, scopeOffset + 24, processOffset);
  put64(bytes, scopeOffset + 40, stringOffset);

  put32(bytes, processOffset, OBELISK_RT_DESIGN_RECORD_PROCESS);
  put64(bytes, processOffset + 8, 71);
  put64(bytes, processOffset + 16, scopeOffset);
  put64(bytes, processOffset + 24, functionOffset);
  put64(bytes, processOffset + 40, stringOffset + 4);

  put32(bytes, functionOffset, OBELISK_RT_DESIGN_RECORD_FUNCTION);
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

std::vector<uint8_t> makeAggregateDatabase() {
  constexpr uint64_t scopeOffset = 128;
  constexpr uint64_t objectOffset = 192;
  constexpr uint64_t typeOffset = 288;
  constexpr uint64_t fieldTypeOffset = typeOffset + 80;
  constexpr uint64_t scalarTypeOffset = fieldTypeOffset + 80;
  constexpr uint64_t stringOffset = scalarTypeOffset + 80;
  constexpr uint64_t stringSize = 33;
  constexpr uint64_t indexOffset = 568;
  std::vector<uint8_t> bytes(indexOffset + 48, 0);
  std::memcpy(bytes.data(), "OBDSGN1\0", 8);
  put32(bytes, 8, OBELISK_RT_VERSION);
  put32(bytes, 12, 0);
  put32(bytes, 16,
        OBELISK_RT_DESIGN_PROFILE_READ | OBELISK_RT_DESIGN_PROFILE_WRITE);
  put32(bytes, 20, 128);
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

  put32(bytes, scopeOffset, OBELISK_RT_DESIGN_RECORD_SCOPE);
  put32(bytes, scopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
  put64(bytes, scopeOffset + 8, 1);
  put64(bytes, scopeOffset + 24, objectOffset);
  put64(bytes, scopeOffset + 40, stringOffset);

  put32(bytes, objectOffset, OBELISK_RT_DESIGN_RECORD_STORAGE);
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
  put32(fixture.database, 192, OBELISK_RT_DESIGN_RECORD_NET);
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

TEST(DesignBytecode, ResolvesDriversAcrossLogicalNetAliases) {
  Fixture fixture;
  fixture.bytecode = makeConnectedDriverBytecode();
  fixture.database = makeDatabase();
  put32(fixture.database, 192, OBELISK_RT_DESIGN_RECORD_NET);
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

TEST(DesignBytecode, AcceptsDisjointUWireDriverComponents) {
  Fixture fixture;
  fixture.bytecode = makePartialUWireDriverBytecode(false);
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
  obelisk_rt_v1_vpi_shutdown(context);

  char rootName[] = "$root";
  EXPECT_EQ(vpi_handle_by_name(rootName, nullptr), nullptr);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, TraversesReflectionAndTracksHandleState) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);

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
  EXPECT_EQ(vpi_get(vpiSize, root), 0);
  EXPECT_STREQ(vpi_get_str(vpiName, root), "top");
  EXPECT_EQ(vpi_get64(vpiType, value), vpiReg);
  EXPECT_EQ(vpi_get(vpiSize, value), 65);
  EXPECT_STREQ(vpi_get_str(vpiName, value), "value");
  EXPECT_STREQ(vpi_get_str(vpiFullName, value), "top.value");
  EXPECT_EQ(vpi_compare_objects(value, absolute), 1);
  EXPECT_EQ(vpi_compare_objects(root, scope), 1);

  vpiHandle iterator = vpi_iterate(vpiReg, root);
  ASSERT_NE(iterator, nullptr);
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
  EXPECT_EQ(vpi_chk_error(&error), 0);

  EXPECT_EQ(vpi_release_handle(scanned), 1);
  EXPECT_EQ(vpi_release_handle(scanned), 0);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_free_object(absolute), 1);
  EXPECT_EQ(vpi_release_handle(scope), 1);
  EXPECT_EQ(vpi_release_handle(value), 1);
  EXPECT_EQ(vpi_release_handle(root), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(VPI, ConvertsValuesAndEnforcesMutationCapabilities) {
  Fixture fixture;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&fixture.execution, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);

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

  value = {};
  value.format = vpiScalarVal;
  value.value.scalar = vpiX;
  vpi_put_value(object, &value, nullptr, vpiNoDelay);
  value.value.scalar = vpi0;
  vpi_get_value(object, &value);
  EXPECT_EQ(value.value.scalar, vpiX);

  char binary[] = "1_0xz?";
  value = {};
  value.format = vpiBinStrVal;
  value.value.str = binary;
  vpi_put_value(object, &value, nullptr, vpiForceFlag);
  value.value.str = nullptr;
  vpi_get_value(object, &value);
  ASSERT_NE(value.value.str, nullptr);
  std::string formatted(value.value.str);
  ASSERT_GE(formatted.size(), 5u);
  EXPECT_EQ(formatted.substr(formatted.size() - 5), "10xzz");
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
  EXPECT_STREQ(info.product, "Obelisk");
  EXPECT_STREQ(info.version, "0.1");
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
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                instance, context, OBELISK_RT_TIER_BYTECODE, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  EXPECT_TRUE(context->scheduledDesignTasks.empty());
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
  invalidOrientation.bytecode[connectivity + 26] = 2;
  rejected(invalidOrientation);

  Fixture incompatibleKinds;
  connected(incompatibleKinds);
  connectivity = incompatibleKinds.bytecode.size() - 32;
  incompatibleKinds.bytecode[connectivity + 24] = 2;
  rejected(incompatibleKinds);

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
  overlappingUWireDrivers.bytecode = makePartialUWireDriverBytecode(true);
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
  fixture.database[192 + 16] ^= 1;
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
  put64(malformed, 288 + 48, 384);
  put64(malformed, 32, imageChecksum(malformed));
  fixture.execution.design_database = malformed.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);
  put64(malformed, 288 + 48, 0);
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

  constexpr uint64_t fieldTypeOffset = 288 + 80;
  constexpr uint64_t rootTypeOffset = 288;
  put64(fixture.database, fieldTypeOffset + 32, rootTypeOffset);
  put64(fixture.database, 32, imageChecksum(fixture.database));
  EXPECT_EQ(obelisk_rt_v1_design_validate(&fixture.execution),
            OBELISK_RT_INVALID_DESIGN);
}

TEST(DesignDatabase, RejectsCorruptionAndUnauthorizedWrites) {
  Fixture sourceMetadata;
  constexpr uint64_t scopeOffset = 128;
  constexpr uint64_t stringOffset = 368;
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
  constexpr uint64_t typeOffset = 288;
  put64(inconsistentScalarRange.database, typeOffset + 16, 63);
  put64(inconsistentScalarRange.database, 32,
        imageChecksum(inconsistentScalarRange.database));
  EXPECT_EQ(obelisk_rt_v1_design_validate(&inconsistentScalarRange.execution),
            OBELISK_RT_INVALID_DESIGN);

  Fixture corrupt;
  corrupt.database[192 + 16] ^= 1;
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
