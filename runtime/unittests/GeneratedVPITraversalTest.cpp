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

void write16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
  for (unsigned index = 0; index != 2; ++index)
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

PLI_INT32 integerValue(vpiHandle handle) {
  s_vpi_value value{};
  value.format = vpiIntVal;
  vpi_get_value(handle, &value);
  return value.value.integer;
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
  vpiHandle inputBit = vpi_handle_by_index(ports[0], 7);
  ASSERT_NE(inputBit, nullptr);
  EXPECT_EQ(vpi_get(vpiType, inputBit), vpiPortBit);
  EXPECT_EQ(vpi_get(vpiSize, inputBit), 1);
  EXPECT_EQ(vpi_get_str(vpiName, inputBit), nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, inputBit), "top.d.a[7]");
  EXPECT_EQ(vpi_handle(vpiIndex, inputBit), nullptr);
  vpiHandle inputBitParent = vpi_handle(vpiParent, inputBit);
  ASSERT_NE(inputBitParent, nullptr);
  EXPECT_EQ(vpi_compare_objects(inputBitParent, ports[0]), 1);
  EXPECT_EQ(vpi_release_handle(inputBitParent), 1);
  EXPECT_EQ(vpi_release_handle(inputBit), 1);
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

TEST(GeneratedVPITraversal, TraversesDefinitionSharedIODeclarations) {
  ASSERT_NE(dumpDescriptor.execution, nullptr);
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
  char secondModuleName[] = "top.e";
  vpiHandle module = vpi_handle_by_name(moduleName, nullptr);
  vpiHandle secondModule = vpi_handle_by_name(secondModuleName, nullptr);
  ASSERT_NE(module, nullptr);
  ASSERT_NE(secondModule, nullptr);
  vpiHandle iterator = vpi_iterate(vpiIODecl, module);
  ASSERT_NE(iterator, nullptr);

  constexpr std::array<const char *, 5> expectedNames{"a", "io", "slice", "r",
                                                      "iface"};
  constexpr std::array<PLI_INT32, 5> expectedDirections{
      vpiInput, vpiInout, vpiOutput, vpiRef, vpiUndefined};
  constexpr std::array<PLI_INT32, 5> expectedWidths{8, 1, 4, 32, 1};
  constexpr std::array<PLI_INT32, 5> expectedSigned{0, 0, 1, 1, 0};
  constexpr std::array<PLI_INT32, 5> expectedTypespecs{
      vpiBitTypespec, vpiLogicTypespec, vpiBitTypespec, vpiIntTypespec,
      vpiBitTypespec};
  constexpr std::array<PLI_INT32, 5> expectedLeft{7, -1, 3, -1, -1};
  constexpr std::array<PLI_INT32, 5> expectedRight{0, -1, 0, 0, -1};
  std::array<vpiHandle, 5> declarations{};
  for (size_t index = 0; index != declarations.size(); ++index) {
    declarations[index] = vpi_scan(iterator);
    ASSERT_NE(declarations[index], nullptr);
    EXPECT_EQ(vpi_get(vpiType, declarations[index]), vpiIODecl);
    EXPECT_STREQ(vpi_get_str(vpiName, declarations[index]),
                 expectedNames[index]);
    EXPECT_EQ(vpi_get_str(vpiFullName, declarations[index]), nullptr);
    EXPECT_EQ(vpi_get(vpiDirection, declarations[index]),
              expectedDirections[index]);
    EXPECT_EQ(vpi_get(vpiSize, declarations[index]), expectedWidths[index]);
    EXPECT_EQ(vpi_get(vpiScalar, declarations[index]),
              expectedWidths[index] == 1);
    EXPECT_EQ(vpi_get(vpiVector, declarations[index]),
              expectedWidths[index] != 1);
    EXPECT_EQ(vpi_get(vpiSigned, declarations[index]), expectedSigned[index]);
    EXPECT_STREQ(vpi_get_str(vpiFile, declarations[index]), "ports.sv");
    EXPECT_EQ(vpi_get(vpiLineNo, declarations[index]), 2);

    vpiHandle instance = vpi_handle(vpiInstance, declarations[index]);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(vpi_compare_objects(instance, module), 1);
    EXPECT_EQ(vpi_release_handle(instance), 1);

    vpiHandle typespec = vpi_handle(vpiTypespec, declarations[index]);
    ASSERT_NE(typespec, nullptr);
    EXPECT_EQ(vpi_get(vpiType, typespec), expectedTypespecs[index]);
    EXPECT_EQ(vpi_release_handle(typespec), 1);

    vpiHandle left = vpi_handle(vpiLeftRange, declarations[index]);
    vpiHandle right = vpi_handle(vpiRightRange, declarations[index]);
    vpiHandle ranges = vpi_iterate(vpiRange, declarations[index]);
    if (expectedLeft[index] < 0) {
      EXPECT_EQ(left, nullptr);
      EXPECT_EQ(right, nullptr);
      EXPECT_EQ(ranges, nullptr);
    } else {
      ASSERT_NE(left, nullptr);
      ASSERT_NE(right, nullptr);
      ASSERT_NE(ranges, nullptr);
      EXPECT_EQ(integerValue(left), expectedLeft[index]);
      EXPECT_EQ(integerValue(right), expectedRight[index]);
      vpiHandle range = vpi_scan(ranges);
      ASSERT_NE(range, nullptr);
      EXPECT_EQ(vpi_scan(ranges), nullptr);
      EXPECT_EQ(vpi_release_handle(range), 1);
      EXPECT_EQ(vpi_release_handle(left), 1);
      EXPECT_EQ(vpi_release_handle(right), 1);
    }
  }
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(vpi_compare_objects(declarations[0], declarations[1]), 0);

  // The synthetic member identity includes the elaborated scope. The member
  // metadata and effective type vector remain physically shared in the image.
  vpiHandle secondIterator = vpi_iterate(vpiIODecl, secondModule);
  ASSERT_NE(secondIterator, nullptr);
  for (size_t index = 0; index != declarations.size(); ++index) {
    vpiHandle declaration = vpi_scan(secondIterator);
    ASSERT_NE(declaration, nullptr);
    EXPECT_STREQ(vpi_get_str(vpiName, declaration), expectedNames[index]);
    EXPECT_EQ(vpi_get(vpiSize, declaration), expectedWidths[index]);
    EXPECT_EQ(vpi_compare_objects(declaration, declarations[index]), 0);
    vpiHandle instance = vpi_handle(vpiInstance, declaration);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(vpi_compare_objects(instance, secondModule), 1);
    EXPECT_EQ(vpi_release_handle(instance), 1);
    EXPECT_EQ(vpi_release_handle(declaration), 1);
  }
  EXPECT_EQ(vpi_scan(secondIterator), nullptr);

  std::vector<uint8_t> database(
      dumpDescriptor.execution->design_database,
      dumpDescriptor.execution->design_database +
          dumpDescriptor.execution->design_database_size);
  uint32_t directory = read32(database, 12);
  EXPECT_EQ(read64(database, directory + 184), 1u); // definitions
  EXPECT_EQ(read64(database, directory + 200), 2u); // instance bindings
  EXPECT_EQ(read64(database, directory + 216), 5u); // member templates
  EXPECT_EQ(read64(database, directory + 232), 1u); // relation ranges
  EXPECT_EQ(read64(database, directory + 248), 5u); // relation targets
  EXPECT_EQ(read64(database, directory + 264), 1u); // specializations
  EXPECT_EQ(read64(database, directory + 280), 5u); // type bindings

  for (vpiHandle declaration : declarations)
    EXPECT_EQ(vpi_release_handle(declaration), 1);
  EXPECT_EQ(vpi_release_handle(secondModule), 1);
  EXPECT_EQ(vpi_release_handle(module), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedVPITraversal, RejectsMalformedDefinitionMemberImages) {
  const auto *execution = dumpDescriptor.execution;
  ASSERT_NE(execution, nullptr);
  std::vector<uint8_t> original(execution->design_database,
                                execution->design_database +
                                    execution->design_database_size);
  uint32_t directory = read32(original, 12);
  uint64_t definition = read64(original, directory + 176);
  uint64_t instanceBinding = read64(original, directory + 192);
  uint64_t member = read64(original, directory + 208);
  uint64_t relation = read64(original, directory + 224);
  uint64_t relationTarget = read64(original, directory + 240);
  uint64_t specialization = read64(original, directory + 256);
  uint64_t typeBinding = read64(original, directory + 272);
  ASSERT_EQ(read64(original, directory + 184), 1u);
  ASSERT_EQ(read64(original, directory + 200), 2u);
  ASSERT_EQ(read64(original, directory + 216), 5u);
  ASSERT_EQ(read64(original, directory + 232), 1u);
  ASSERT_EQ(read64(original, directory + 248), 5u);
  ASSERT_EQ(read64(original, directory + 264), 1u);
  ASSERT_EQ(read64(original, directory + 280), 5u);

  auto rejects = [&](std::vector<uint8_t> database) {
    write64(database, 32, imageChecksum(database));
    obelisk_rt_execution_descriptor_v1 mutated = *execution;
    mutated.design_database = database.data();
    mutated.design_database_size = database.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&mutated),
              OBELISK_RT_INVALID_DESIGN);
  };

  std::vector<uint8_t> malformed = original;
  write32(malformed, definition + 20, 6); // Member range exceeds section.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, member, 0); // Members require a nonempty name.
  rejects(std::move(malformed));

  malformed = original;
  write16(malformed, member + 18, 7); // Invalid IO direction domain.
  rejects(std::move(malformed));

  malformed = original;
  write16(malformed, relation, vpiPort); // Wrong automatic relation.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, relationTarget, 5); // Target outside the definition.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, specialization, 1); // Unknown definition.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, specialization + 8, 4); // Incomplete member types.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, typeBinding, 5); // Member outside the definition.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, typeBinding + 4, UINT32_MAX); // Unknown semantic type.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceBinding + 8, 1); // Unknown specialization.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceBinding + 8,
          UINT32_MAX); // Member-bearing definitions require one.
  rejects(std::move(malformed));
}

TEST(GeneratedVPITraversal, VirtualInterfaceTypedefKeepsRawCanonicalIdentity) {
  ASSERT_NE(dumpDescriptor.execution, nullptr);
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
  char aliasName[] = "top.d.iface_alias_t";
  vpiHandle module = vpi_handle_by_name(moduleName, nullptr);
  vpiHandle alias = vpi_handle_by_name(aliasName, nullptr);
  ASSERT_NE(module, nullptr);
  ASSERT_NE(alias, nullptr);
  vpiHandle interfaces = vpi_iterate(vpiTypespec, module);
  ASSERT_NE(interfaces, nullptr);
  vpiHandle raw = vpi_scan(interfaces);
  ASSERT_NE(raw, nullptr);
  EXPECT_EQ(vpi_scan(interfaces), nullptr);
  EXPECT_EQ(vpi_get(vpiType, raw), vpiInterfaceTypespec);
  EXPECT_EQ(vpi_get(vpiType, alias), vpiInterfaceTypespec);
  EXPECT_EQ(vpi_handle(vpiTypedefAlias, raw), nullptr);
  vpiHandle target = vpi_handle(vpiTypedefAlias, alias);
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(vpi_compare_objects(target, raw), 1);

  EXPECT_EQ(vpi_release_handle(target), 1);
  EXPECT_EQ(vpi_release_handle(alias), 1);
  EXPECT_EQ(vpi_release_handle(raw), 1);
  EXPECT_EQ(vpi_release_handle(module), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedVPITraversal, TypedefAliasChainEndsAtUnnamedBuiltinType) {
  ASSERT_NE(dumpDescriptor.execution, nullptr);
  ASSERT_EQ(obelisk_rt_v1_design_validate(dumpDescriptor.execution),
            OBELISK_RT_OK);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(dumpDescriptor.execution,
                                                    &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);

  char baseName[] = "top.d.base_t";
  char aliasName[] = "top.d.alias_t";
  vpiHandle base = vpi_handle_by_name(baseName, nullptr);
  vpiHandle alias = vpi_handle_by_name(aliasName, nullptr);
  ASSERT_NE(base, nullptr);
  ASSERT_NE(alias, nullptr);
  vpiHandle aliasTarget = vpi_handle(vpiTypedefAlias, alias);
  ASSERT_NE(aliasTarget, nullptr);
  EXPECT_EQ(vpi_compare_objects(aliasTarget, base), 1);
  vpiHandle builtin = vpi_handle(vpiTypedefAlias, base);
  ASSERT_NE(builtin, nullptr);
  EXPECT_EQ(vpi_get(vpiType, builtin), vpiBitTypespec);
  EXPECT_EQ(vpi_get_str(vpiName, builtin), nullptr);
  EXPECT_EQ(vpi_handle(vpiTypedefAlias, builtin), nullptr);

  EXPECT_EQ(vpi_release_handle(builtin), 1);
  EXPECT_EQ(vpi_release_handle(aliasTarget), 1);
  EXPECT_EQ(vpi_release_handle(alias), 1);
  EXPECT_EQ(vpi_release_handle(base), 1);
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
