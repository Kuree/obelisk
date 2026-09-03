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
  EXPECT_EQ(dumpDescriptor.execution->flags &
                (OBELISK_RT_EXECUTION_VPI_READ |
                 OBELISK_RT_EXECUTION_VPI_WRITE),
            OBELISK_RT_EXECUTION_VPI_READ);
  ASSERT_EQ(obelisk_rt_v1_design_validate(dumpDescriptor.execution),
            OBELISK_RT_OK);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(
                dumpDescriptor.execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char moduleName[] = "top.d";
  char inputName[] = "top.d.a";
  char inoutName[] = "top.d.io";
  vpiHandle module = vpi_handle_by_name(moduleName, nullptr);
  vpiHandle input = vpi_handle_by_name(inputName, nullptr);
  vpiHandle inout = vpi_handle_by_name(inoutName, nullptr);
  ASSERT_NE(module, nullptr);
  ASSERT_NE(input, nullptr);
  ASSERT_NE(inout, nullptr);
  EXPECT_EQ(vpi_get(vpiType, input), vpiReg);
  EXPECT_EQ(vpi_get(vpiType, inout), vpiNet);

  vpiHandle iterator = vpi_iterate(vpiPort, module);
  ASSERT_NE(iterator, nullptr);
  constexpr std::array<const char *, 3> expectedNames{
      "top.d.a", "top.d.io", "top.d.slice"};
  std::array<vpiHandle, 3> ports{};
  for (size_t index = 0; index != ports.size(); ++index) {
    ports[index] = vpi_scan(iterator);
    ASSERT_NE(ports[index], nullptr);
    EXPECT_EQ(vpi_get(vpiType, ports[index]), vpiPort);
    EXPECT_EQ(std::string(vpi_get_str(vpiFullName, ports[index])),
              expectedNames[index]);
    vpiHandle instance = vpi_handle(vpiInstance, ports[index]);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(vpi_compare_objects(module, instance), 1);
  }
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(vpi_compare_objects(input, ports[0]), 0);
  EXPECT_EQ(vpi_compare_objects(inout, ports[1]), 0);

  vpiHandle registers = vpi_iterate(vpiReg, module);
  ASSERT_NE(registers, nullptr);
  vpiHandle canonicalInput = vpi_scan(registers);
  ASSERT_NE(canonicalInput, nullptr);
  EXPECT_EQ(vpi_compare_objects(input, canonicalInput), 1);
  EXPECT_EQ(vpi_scan(registers), nullptr);

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
}

TEST(GeneratedVPITraversal, FallsBackWhenAutomaticRelationsAreAbsent) {
  const auto *execution = dumpDescriptor.execution;
  ASSERT_NE(execution, nullptr);
  std::vector<uint8_t> database(execution->design_database,
                                execution->design_database +
                                    execution->design_database_size);
  ASSERT_GE(database.size(), OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  write64(database, 168, 0);
  write64(database, 32, imageChecksum(database));
  obelisk_rt_execution_descriptor_v1 relationFree{};
  relationFree.version = OBELISK_RT_VERSION;
  relationFree.flags = OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                       OBELISK_RT_EXECUTION_VPI_READ;
  relationFree.design_database = database.data();
  relationFree.design_database_size = database.size();
  relationFree.state_bit_count = execution->state_bit_count;
  ASSERT_EQ(obelisk_rt_v1_design_validate(&relationFree), OBELISK_RT_OK);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&relationFree, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char moduleName[] = "top.d";
  vpiHandle module = vpi_handle_by_name(moduleName, nullptr);
  ASSERT_NE(module, nullptr);
  vpiHandle iterator = vpi_iterate(vpiPort, module);
  ASSERT_NE(iterator, nullptr);
  vpiHandle port = vpi_scan(iterator);
  ASSERT_NE(port, nullptr);
  EXPECT_EQ(vpi_get(vpiType, port), vpiPort);
  vpiHandle instance = vpi_handle(vpiInstance, port);
  ASSERT_NE(instance, nullptr);
  EXPECT_EQ(vpi_compare_objects(module, instance), 1);

  obelisk_rt_v1_context_destroy(context);
}
