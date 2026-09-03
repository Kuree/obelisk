//===- GeneratedVPITraversalTest.cpp - Compiler/runtime VPI traversal -----===//

#include "obelisk/Runtime/Runtime.h"

#include "../lib/RuntimeInternal.h"

#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" const obelisk_rt_process_descriptor_v1
    dumpDescriptor asm("dump.__obelisk_process_descriptor");

namespace {

uint32_t read32(const std::vector<uint8_t> &bytes, size_t offset) {
  uint32_t value = 0;
  for (unsigned index = 0; index != 4; ++index)
    value |= uint32_t{bytes[offset + index]} << (index * 8);
  return value;
}

uint16_t read16(const std::vector<uint8_t> &bytes, size_t offset) {
  return uint16_t{bytes[offset]} | (uint16_t{bytes[offset + 1]} << 8);
}

uint64_t read64(const std::vector<uint8_t> &bytes, size_t offset) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= uint64_t{bytes[offset + index]} << (index * 8);
  return value;
}

void write32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
  for (unsigned index = 0; index != 4; ++index)
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

void write64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value) {
  for (unsigned index = 0; index != 8; ++index)
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
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

size_t findDirectPortAlias(const std::vector<uint8_t> &database) {
  constexpr size_t objectSize = 96;
  constexpr size_t indexSize = 24;
  uint64_t objects = read64(database, 64);
  uint64_t objectCount = read64(database, 72);
  uint64_t nameIndex = read64(database, 112);
  uint64_t indexCount = read64(database, 120);
  for (uint64_t objectIndex = 0; objectIndex != objectCount; ++objectIndex) {
    uint64_t object = objects + objectIndex * objectSize;
    if ((read32(database, object) & UINT32_C(0xffff)) !=
        OBELISK_RT_DESIGN_RECORD_PORT)
      continue;
    bool indexed = false;
    for (uint64_t index = 0; index != indexCount; ++index)
      indexed |= read64(database, nameIndex + index * indexSize + 16) == object;
    if (!indexed)
      return static_cast<size_t>(object);
  }
  return 0;
}

} // namespace

TEST(GeneratedVPITraversal, PreservesPortIdentityAndCanonicalNameLookup) {
  ASSERT_NE(dumpDescriptor.execution, nullptr);
  EXPECT_EQ(dumpDescriptor.execution->flags & (OBELISK_RT_EXECUTION_VPI_READ |
                                               OBELISK_RT_EXECUTION_VPI_WRITE),
            OBELISK_RT_EXECUTION_VPI_READ);
  ASSERT_EQ(obelisk_rt_v1_design_validate(dumpDescriptor.execution),
            OBELISK_RT_OK);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(dumpDescriptor.execution,
                                                    &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char moduleName[] = "top.d";
  char inputName[] = "top.d.a";
  char inoutName[] = "top.d.io";
  char anonymousBackingName[] = "zzBacking";
  vpiHandle module = vpi_handle_by_name(moduleName, nullptr);
  vpiHandle input = vpi_handle_by_name(inputName, nullptr);
  vpiHandle inout = vpi_handle_by_name(inoutName, nullptr);
  vpiHandle anonymousBacking =
      vpi_handle_by_name(anonymousBackingName, nullptr);
  ASSERT_NE(module, nullptr);
  ASSERT_NE(input, nullptr);
  ASSERT_NE(inout, nullptr);
  ASSERT_NE(anonymousBacking, nullptr);
  EXPECT_EQ(vpi_get(vpiType, input), vpiReg);
  EXPECT_EQ(vpi_get(vpiType, inout), vpiNet);

  vpiHandle iterator = vpi_iterate(vpiPort, module);
  ASSERT_NE(iterator, nullptr);
  constexpr std::array<const char *, 6> expectedNames{
      "top.d.a", "top.d.io",    "top.d.p",
      "top.d.q", "top.d.slice", "top.d.zouter"};
  constexpr std::array<PLI_INT32, 6> expectedOrdinals{0, 1, 3, 4, 2, 5};
  constexpr std::array<PLI_INT32, 6> expectedDirections{
      vpiInput, vpiInout, vpiInput, vpiInput, vpiOutput, vpiInput};
  constexpr std::array<PLI_INT32, 6> expectedWidths{8, 1, 8, 8, 4, 16};
  std::array<vpiHandle, 6> ports{};
  for (size_t index = 0; index != ports.size(); ++index) {
    ports[index] = vpi_scan(iterator);
    ASSERT_NE(ports[index], nullptr);
    EXPECT_EQ(vpi_get(vpiType, ports[index]), vpiPort);
    EXPECT_EQ(std::string(vpi_get_str(vpiFullName, ports[index])),
              expectedNames[index]);
    vpiHandle instance = vpi_handle(vpiInstance, ports[index]);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(vpi_compare_objects(module, instance), 1);
    EXPECT_EQ(vpi_get(vpiPortIndex, ports[index]), expectedOrdinals[index]);
    EXPECT_EQ(vpi_get64(vpiPortIndex, ports[index]), vpiUndefined);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    EXPECT_EQ(vpi_get(vpiPortType, ports[index]), vpiPort);
    EXPECT_EQ(vpi_get(vpiDirection, ports[index]), expectedDirections[index]);
    EXPECT_EQ(vpi_get(vpiSize, ports[index]), expectedWidths[index]);
    EXPECT_EQ(vpi_get(vpiScalar, ports[index]), expectedWidths[index] == 1);
    EXPECT_EQ(vpi_get(vpiVector, ports[index]), expectedWidths[index] > 1);
  }
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(vpi_compare_objects(input, ports[0]), 0);
  EXPECT_EQ(vpi_compare_objects(inout, ports[1]), 0);
  vpiHandle inputLowConnection = vpi_handle(vpiLowConn, ports[0]);
  vpiHandle inoutLowConnection = vpi_handle(vpiLowConn, ports[1]);
  vpiHandle renamedInputLowConnection = vpi_handle(vpiLowConn, ports[2]);
  vpiHandle anonymousLowConnection = vpi_handle(vpiLowConn, ports[3]);
  ASSERT_NE(inputLowConnection, nullptr);
  ASSERT_NE(inoutLowConnection, nullptr);
  ASSERT_NE(renamedInputLowConnection, nullptr);
  ASSERT_NE(anonymousLowConnection, nullptr);
  EXPECT_EQ(vpi_compare_objects(input, inputLowConnection), 1);
  EXPECT_EQ(vpi_compare_objects(inout, inoutLowConnection), 1);
  EXPECT_EQ(vpi_compare_objects(input, renamedInputLowConnection), 1);
  EXPECT_EQ(vpi_compare_objects(anonymousBacking, anonymousLowConnection), 1);
  // A selected port needs a select/ref-object identity before it can expose a
  // low connection; it must never be redirected to the whole backing object.
  EXPECT_EQ(vpi_handle(vpiLowConn, ports[4]), nullptr);
  // A full-width source in another scope is a higher-side connection, not the
  // formal's same-scope lower connection.
  EXPECT_EQ(vpi_handle(vpiLowConn, ports[5]), nullptr);

  vpiHandle registers = vpi_iterate(vpiReg, module);
  ASSERT_NE(registers, nullptr);
  vpiHandle canonicalInput = vpi_scan(registers);
  ASSERT_NE(canonicalInput, nullptr);
  EXPECT_EQ(vpi_compare_objects(input, canonicalInput), 1);
  vpiHandle anonymousRegister = vpi_scan(registers);
  ASSERT_NE(anonymousRegister, nullptr);
  EXPECT_EQ(vpi_compare_objects(anonymousBacking, anonymousRegister), 1);
  EXPECT_EQ(vpi_scan(registers), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  // Port-only properties are undefined on the distinct backing object.
  s_vpi_error_info error{};
  EXPECT_EQ(vpi_get(vpiDirection, input), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_EQ(vpi_get(vpiPortIndex, input), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  EXPECT_EQ(vpi_get(vpiPortType, input), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(&error), vpiNotice);

  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedVPITraversal, RejectsMalformedUnindexedPortAliases) {
  const auto *execution = dumpDescriptor.execution;
  ASSERT_NE(execution, nullptr);
  std::vector<uint8_t> original(execution->design_database,
                                execution->design_database +
                                    execution->design_database_size);
  size_t port = findDirectPortAlias(original);
  ASSERT_NE(port, 0u);

  auto rejects = [&](std::vector<uint8_t> database) {
    write64(database, 32, imageChecksum(database));
    obelisk_rt_execution_descriptor_v1 mutated = *execution;
    mutated.design_database = database.data();
    mutated.design_database_size = database.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&mutated),
              OBELISK_RT_INVALID_DESIGN);
  };

  std::vector<uint8_t> wrongRange = original;
  write64(wrongRange, port + 64, read64(wrongRange, port + 64) ^ 1);
  rejects(std::move(wrongRange));

  std::vector<uint8_t> wrongOrdinal = original;
  write32(wrongOrdinal, port + 4,
          read32(wrongOrdinal, port + 4) ^
              (UINT32_C(1) << OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_SHIFT));
  rejects(std::move(wrongOrdinal));

  constexpr size_t objectSize = 96;
  constexpr size_t relationSize = 16;
  uint64_t objects = read64(original, 64);
  uint64_t objectCount = read64(original, 72);
  uint64_t relations = read64(original, 160);
  uint64_t relationCount = read64(original, 168);
  std::vector<size_t> lowConnections;
  for (uint64_t index = 0; index != relationCount; ++index) {
    size_t relation = static_cast<size_t>(relations + index * relationSize);
    if (read16(original, relation + 12) == vpiLowConn)
      lowConnections.push_back(relation);
  }
  ASSERT_EQ(lowConnections.size(), 4u);

  std::vector<uint8_t> redirected = original;
  size_t lowConnection = lowConnections.front();
  uint32_t canonicalTarget = read32(original, lowConnection + 4) & 0x3fffffff;
  size_t canonicalObject =
      static_cast<size_t>(objects + uint64_t{canonicalTarget} * objectSize);
  uint32_t wrongTarget = UINT32_MAX;
  for (uint32_t index = 0; index != objectCount; ++index) {
    size_t candidate = static_cast<size_t>(objects + index * objectSize);
    uint32_t kind = read32(original, candidate) & 0xffff;
    if (index != canonicalTarget &&
        (kind == OBELISK_RT_DESIGN_RECORD_STORAGE ||
         kind == OBELISK_RT_DESIGN_RECORD_NET) &&
        read64(original, candidate + 16) ==
            read64(original, canonicalObject + 16) &&
        read64(original, candidate + 48) ==
            read64(original, canonicalObject + 48) &&
        read64(original, candidate + 56) ==
            read64(original, canonicalObject + 56) &&
        read64(original, candidate + 64) ==
            read64(original, canonicalObject + 64) &&
        read64(original, candidate + 72) ==
            read64(original, canonicalObject + 72)) {
      wrongTarget = index;
      break;
    }
  }
  ASSERT_NE(wrongTarget, UINT32_MAX);
  size_t wrongObject =
      static_cast<size_t>(objects + uint64_t{wrongTarget} * objectSize);
  write64(redirected, wrongObject + 80, read64(original, canonicalObject + 80));
  write32(redirected, lowConnection + 4, (UINT32_C(1) << 30) | wrongTarget);
  rejects(std::move(redirected));

  std::vector<uint8_t> misplacedWholeSource = original;
  write32(misplacedWholeSource, canonicalObject + 4,
          read32(misplacedWholeSource, canonicalObject + 4) |
              OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE);
  rejects(std::move(misplacedWholeSource));

  // Truncate immediately before the last low-connection record. Missing
  // hierarchy compatibility relations after it remain legal, so rejection
  // specifically proves that every direct port connection is mandatory.
  std::vector<uint8_t> omitted = original;
  size_t lastLowConnection = lowConnections.back();
  write64(omitted, 168, (lastLowConnection - relations) / relationSize);
  rejects(std::move(omitted));
}

TEST(GeneratedVPITraversal, RejectsRelationFreeDirectPortAliases) {
  const auto *execution = dumpDescriptor.execution;
  ASSERT_NE(execution, nullptr);
  std::vector<uint8_t> database(execution->design_database,
                                execution->design_database +
                                    execution->design_database_size);
  ASSERT_GE(database.size(), OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  write64(database, 168, 0);
  write64(database, 32, imageChecksum(database));
  obelisk_rt_execution_descriptor_v1 relationFree = *execution;
  relationFree.design_database = database.data();
  relationFree.design_database_size = database.size();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&relationFree),
            OBELISK_RT_INVALID_DESIGN);
}
