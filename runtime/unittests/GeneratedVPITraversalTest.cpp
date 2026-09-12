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
  EXPECT_EQ(vpi_get(vpiType, input), vpiBitVar);
  EXPECT_EQ(vpi_get(vpiType, inout), vpiNet);

  vpiHandle iterator = vpi_iterate(vpiPort, module);
  ASSERT_NE(iterator, nullptr);
  constexpr std::array<const char *, 4> expectedNames{"top.d.a", "top.d.io",
                                                      "top.d.p", "top.d.slice"};
  constexpr std::array<PLI_INT32, 4> expectedOrdinals{0, 1, 3, 2};
  constexpr std::array<PLI_INT32, 4> expectedDirections{vpiInput, vpiInout,
                                                        vpiRef, vpiOutput};
  constexpr std::array<PLI_INT32, 4> expectedWidths{8, 1, 8, 4};
  std::array<vpiHandle, 4> ports{};
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
  EXPECT_EQ(vpi_get(vpiPortIndex, inputBit), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
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
  vpiHandle refLowConnection = vpi_handle(vpiLowConn, ports[2]);
  ASSERT_NE(inputLowConnection, nullptr);
  ASSERT_NE(inoutLowConnection, nullptr);
  ASSERT_NE(refLowConnection, nullptr);
  EXPECT_EQ(vpi_compare_objects(input, inputLowConnection), 1);
  EXPECT_EQ(vpi_compare_objects(inout, inoutLowConnection), 1);
  EXPECT_EQ(vpi_get(vpiType, refLowConnection), vpiRefObj);
  EXPECT_STREQ(vpi_get_str(vpiFullName, refLowConnection), "top.d.r");
  // A selected port needs a select/ref-object identity before it can expose a
  // low connection; it must never be redirected to the whole backing object.
  EXPECT_EQ(vpi_handle(vpiLowConn, ports[3]), nullptr);

  vpiHandle variables = vpi_iterate(vpiVariables, module);
  ASSERT_NE(variables, nullptr);
  vpiHandle canonicalInput = vpi_scan(variables);
  ASSERT_NE(canonicalInput, nullptr);
  EXPECT_EQ(vpi_compare_objects(input, canonicalInput), 1);
  vpiHandle anonymousRegister = vpi_scan(variables);
  ASSERT_NE(anonymousRegister, nullptr);
  EXPECT_EQ(vpi_compare_objects(anonymousBacking, anonymousRegister), 1);
  EXPECT_EQ(vpi_scan(variables), nullptr);
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
  char inputName[] = "top.d.a";
  char inoutName[] = "top.d.io";
  char secondInputName[] = "top.e.a";
  char secondInoutName[] = "top.e.io";
  vpiHandle directInput = vpi_handle_by_name(inputName, nullptr);
  vpiHandle directInout = vpi_handle_by_name(inoutName, nullptr);
  vpiHandle secondDirectInput = vpi_handle_by_name(secondInputName, nullptr);
  vpiHandle secondDirectInout = vpi_handle_by_name(secondInoutName, nullptr);
  ASSERT_NE(directInput, nullptr);
  ASSERT_NE(directInout, nullptr);
  ASSERT_NE(secondDirectInput, nullptr);
  ASSERT_NE(secondDirectInout, nullptr);

  constexpr std::array<const char *, 5> expectedNames{"a", "io", "slice", "r",
                                                      "iface"};
  constexpr std::array<PLI_INT32, 5> expectedDirections{
      vpiInput, vpiInout, vpiOutput, vpiRef, vpiUndefined};
  constexpr std::array<PLI_INT32, 5> expectedWidths{8, 1, 4, 8, 1};
  constexpr std::array<PLI_INT32, 5> expectedSigned{0, 0, 1, 0, 0};
  constexpr std::array<PLI_INT32, 5> expectedTypespecs{
      vpiBitTypespec, vpiLogicTypespec, vpiBitTypespec, vpiBitTypespec,
      vpiBitTypespec};
  constexpr std::array<PLI_INT32, 5> expectedLeft{7, -1, 3, 7, -1};
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

    vpiHandle expression = vpi_handle(vpiExpr, declarations[index]);
    if (index < 2) {
      ASSERT_NE(expression, nullptr);
      EXPECT_EQ(vpi_compare_objects(expression,
                                    index == 0 ? directInput : directInout),
                1);
      EXPECT_EQ(vpi_release_handle(expression), 1);
    } else if (index == 3) {
      ASSERT_NE(expression, nullptr);
      EXPECT_EQ(vpi_get(vpiType, expression), vpiRefObj);
      EXPECT_STREQ(vpi_get_str(vpiName, expression), "r");
      EXPECT_STREQ(vpi_get_str(vpiFullName, expression), "top.d.r");
      EXPECT_EQ(vpi_get(vpiSize, expression), 8);
      EXPECT_EQ(vpi_get(vpiGeneric, expression), vpiUndefined);
      EXPECT_EQ(vpi_get_str(vpiDefName, expression), nullptr);
      EXPECT_EQ(vpi_handle(vpiParent, expression), nullptr);
      vpiHandle refTypespec = vpi_handle(vpiTypespec, expression);
      ASSERT_NE(refTypespec, nullptr);
      EXPECT_EQ(vpi_get(vpiType, refTypespec), vpiBitTypespec);
      EXPECT_EQ(vpi_release_handle(refTypespec), 1);
      vpiHandle refInstance = vpi_handle(vpiInstance, expression);
      ASSERT_NE(refInstance, nullptr);
      EXPECT_EQ(vpi_compare_objects(refInstance, module), 1);
      EXPECT_EQ(vpi_release_handle(refInstance), 1);
      vpiHandle actual = vpi_handle(vpiActual, expression);
      ASSERT_NE(actual, nullptr);
      EXPECT_EQ(vpi_compare_objects(actual, secondDirectInput), 1);
      EXPECT_EQ(vpi_compare_objects(actual, expression), 0);
      EXPECT_EQ(integerValue(expression), 0);
      s_vpi_error_info valueError{};
      EXPECT_EQ(vpi_chk_error(&valueError), 0)
          << (valueError.message ? valueError.message : "unknown VPI error");
      s_vpi_value defaultValue{};
      defaultValue.format = vpiObjTypeVal;
      vpi_get_value(expression, &defaultValue);
      EXPECT_EQ(defaultValue.format, vpiVectorVal);
      ASSERT_NE(defaultValue.value.vector, nullptr);
      EXPECT_EQ(defaultValue.value.vector[0].aval, 0u);
      EXPECT_EQ(defaultValue.value.vector[0].bval, 0u);
      EXPECT_EQ(vpi_chk_error(&valueError), 0)
          << (valueError.message ? valueError.message : "unknown VPI error");
      EXPECT_EQ(vpi_release_handle(actual), 1);

      vpiHandle ownPorts = vpi_iterate(vpiPort, expression);
      ASSERT_NE(ownPorts, nullptr);
      vpiHandle ownPort = vpi_scan(ownPorts);
      ASSERT_NE(ownPort, nullptr);
      EXPECT_EQ(vpi_get(vpiType, ownPort), vpiPort);
      EXPECT_STREQ(vpi_get_str(vpiFullName, ownPort), "top.d.p");
      EXPECT_EQ(vpi_get(vpiPortIndex, ownPort), 3);
      vpiHandle ownPortInstance = vpi_handle(vpiInstance, ownPort);
      ASSERT_NE(ownPortInstance, nullptr);
      EXPECT_EQ(vpi_compare_objects(ownPortInstance, module), 1);
      EXPECT_EQ(vpi_release_handle(ownPortInstance), 1);
      vpiHandle lowConnection = vpi_handle(vpiLowConn, ownPort);
      ASSERT_NE(lowConnection, nullptr);
      EXPECT_EQ(vpi_compare_objects(lowConnection, expression), 1);
      EXPECT_EQ(vpi_release_handle(lowConnection), 1);
      EXPECT_EQ(vpi_scan(ownPorts), nullptr);
      EXPECT_EQ(vpi_release_handle(ownPort), 1);

      vpiHandle downstream = vpi_iterate(vpiPortInst, expression);
      ASSERT_NE(downstream, nullptr);
      vpiHandle firstDownstream = vpi_scan(downstream);
      vpiHandle secondDownstream = vpi_scan(downstream);
      ASSERT_NE(firstDownstream, nullptr);
      ASSERT_NE(secondDownstream, nullptr);
      EXPECT_STREQ(vpi_get_str(vpiFullName, firstDownstream), "top.d.child.q");
      EXPECT_STREQ(vpi_get_str(vpiFullName, secondDownstream),
                   "top.d.child.zouter");
      EXPECT_EQ(vpi_get(vpiPortIndex, firstDownstream), 4);
      EXPECT_EQ(vpi_get(vpiPortIndex, secondDownstream), 5);
      vpiHandle firstDownstreamInstance =
          vpi_handle(vpiInstance, firstDownstream);
      vpiHandle secondDownstreamInstance =
          vpi_handle(vpiInstance, secondDownstream);
      ASSERT_NE(firstDownstreamInstance, nullptr);
      ASSERT_NE(secondDownstreamInstance, nullptr);
      EXPECT_STREQ(vpi_get_str(vpiFullName, firstDownstreamInstance),
                   "top.d.child");
      EXPECT_STREQ(vpi_get_str(vpiFullName, secondDownstreamInstance),
                   "top.d.child");
      EXPECT_EQ(vpi_release_handle(firstDownstreamInstance), 1);
      EXPECT_EQ(vpi_release_handle(secondDownstreamInstance), 1);
      vpiHandle firstHighConnection = vpi_handle(vpiHighConn, firstDownstream);
      vpiHandle secondHighConnection =
          vpi_handle(vpiHighConn, secondDownstream);
      ASSERT_NE(firstHighConnection, nullptr);
      ASSERT_NE(secondHighConnection, nullptr);
      EXPECT_EQ(vpi_compare_objects(firstHighConnection, expression), 1);
      EXPECT_EQ(vpi_compare_objects(secondHighConnection, expression), 1);
      EXPECT_EQ(vpi_release_handle(firstHighConnection), 1);
      EXPECT_EQ(vpi_release_handle(secondHighConnection), 1);
      EXPECT_EQ(vpi_scan(downstream), nullptr);
      EXPECT_EQ(vpi_release_handle(firstDownstream), 1);
      EXPECT_EQ(vpi_release_handle(secondDownstream), 1);
      EXPECT_EQ(vpi_release_handle(expression), 1);
    } else {
      EXPECT_EQ(expression, nullptr);
    }

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
    vpiHandle expression = vpi_handle(vpiExpr, declaration);
    if (index < 2) {
      ASSERT_NE(expression, nullptr);
      vpiHandle expected = index == 0 ? secondDirectInput : secondDirectInout;
      EXPECT_EQ(vpi_compare_objects(expression, expected), 1);
      EXPECT_EQ(vpi_compare_objects(expression,
                                    index == 0 ? directInput : directInout),
                0);
      EXPECT_EQ(vpi_release_handle(expression), 1);
    } else if (index == 3) {
      ASSERT_NE(expression, nullptr);
      EXPECT_EQ(vpi_get(vpiType, expression), vpiRefObj);
      EXPECT_STREQ(vpi_get_str(vpiFullName, expression), "top.e.r");
      vpiHandle firstExpression = vpi_handle(vpiExpr, declarations[3]);
      ASSERT_NE(firstExpression, nullptr);
      EXPECT_EQ(vpi_compare_objects(expression, firstExpression), 0);
      EXPECT_EQ(vpi_release_handle(firstExpression), 1);
      vpiHandle actual = vpi_handle(vpiActual, expression);
      ASSERT_NE(actual, nullptr);
      EXPECT_EQ(vpi_compare_objects(actual, directInput), 1);
      EXPECT_EQ(vpi_compare_objects(actual, secondDirectInput), 0);
      EXPECT_EQ(vpi_release_handle(actual), 1);

      vpiHandle ownPorts = vpi_iterate(vpiPort, expression);
      ASSERT_NE(ownPorts, nullptr);
      vpiHandle ownPort = vpi_scan(ownPorts);
      ASSERT_NE(ownPort, nullptr);
      EXPECT_STREQ(vpi_get_str(vpiFullName, ownPort), "top.e.r");
      EXPECT_EQ(vpi_get(vpiPortIndex, ownPort), 3);
      vpiHandle ownPortInstance = vpi_handle(vpiInstance, ownPort);
      ASSERT_NE(ownPortInstance, nullptr);
      EXPECT_EQ(vpi_compare_objects(ownPortInstance, secondModule), 1);
      EXPECT_EQ(vpi_release_handle(ownPortInstance), 1);
      vpiHandle lowConnection = vpi_handle(vpiLowConn, ownPort);
      ASSERT_NE(lowConnection, nullptr);
      EXPECT_EQ(vpi_compare_objects(lowConnection, expression), 1);
      EXPECT_EQ(vpi_release_handle(lowConnection), 1);
      EXPECT_EQ(vpi_scan(ownPorts), nullptr);
      EXPECT_EQ(vpi_release_handle(ownPort), 1);

      vpiHandle downstream = vpi_iterate(vpiPortInst, expression);
      ASSERT_NE(downstream, nullptr);
      vpiHandle downstreamPort = vpi_scan(downstream);
      ASSERT_NE(downstreamPort, nullptr);
      EXPECT_STREQ(vpi_get_str(vpiFullName, downstreamPort), "top.e.child.r");
      EXPECT_EQ(vpi_get(vpiPortIndex, downstreamPort), 0);
      vpiHandle downstreamInstance = vpi_handle(vpiInstance, downstreamPort);
      ASSERT_NE(downstreamInstance, nullptr);
      EXPECT_STREQ(vpi_get_str(vpiFullName, downstreamInstance), "top.e.child");
      EXPECT_EQ(vpi_release_handle(downstreamInstance), 1);
      vpiHandle highConnection = vpi_handle(vpiHighConn, downstreamPort);
      vpiHandle childLowConnection = vpi_handle(vpiLowConn, downstreamPort);
      ASSERT_NE(highConnection, nullptr);
      ASSERT_NE(childLowConnection, nullptr);
      EXPECT_EQ(vpi_compare_objects(highConnection, expression), 1);
      EXPECT_EQ(vpi_get(vpiType, childLowConnection), vpiRefObj);
      EXPECT_STREQ(vpi_get_str(vpiFullName, childLowConnection),
                   "top.e.child.r");
      EXPECT_EQ(vpi_compare_objects(childLowConnection, expression), 0);
      EXPECT_EQ(vpi_get(vpiDirection, downstreamPort), vpiRef);
      EXPECT_EQ(vpi_release_handle(highConnection), 1);
      EXPECT_EQ(vpi_release_handle(childLowConnection), 1);
      EXPECT_EQ(vpi_scan(downstream), nullptr);
      EXPECT_EQ(vpi_release_handle(downstreamPort), 1);
      EXPECT_EQ(vpi_release_handle(expression), 1);
    } else {
      EXPECT_EQ(expression, nullptr);
    }
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
  EXPECT_EQ(read64(database, directory + 184), 2u);  // definitions
  EXPECT_EQ(read64(database, directory + 200), 4u);  // instance bindings
  EXPECT_EQ(read64(database, directory + 216), 10u); // member templates
  EXPECT_EQ(read64(database, directory + 232), 2u);  // relation ranges
  EXPECT_EQ(read64(database, directory + 248), 10u); // relation targets
  EXPECT_EQ(read64(database, directory + 264), 2u);  // specializations
  EXPECT_EQ(read64(database, directory + 280), 10u); // type bindings
  EXPECT_EQ(read64(database, directory + 296), 20u); // instance endpoints
  EXPECT_EQ(read64(database, directory + 312), 5u);  // sparse relation ranges
  EXPECT_EQ(read64(database, directory + 328), 6u);  // relation targets
  EXPECT_EQ(read64(database, directory + 344), 6u);  // inverse targets
  constexpr uint64_t objectSize = 96;
  uint64_t objects = read64(database, 64);
  uint64_t objectCount = read64(database, 72);
  for (uint64_t index = 0; index != objectCount; ++index)
    EXPECT_NE(read32(database, objects + index * objectSize) >> 16,
              static_cast<uint32_t>(vpiRefObj));

  for (vpiHandle declaration : declarations)
    EXPECT_EQ(vpi_release_handle(declaration), 1);
  EXPECT_EQ(vpi_release_handle(secondDirectInout), 1);
  EXPECT_EQ(vpi_release_handle(secondDirectInput), 1);
  EXPECT_EQ(vpi_release_handle(directInout), 1);
  EXPECT_EQ(vpi_release_handle(directInput), 1);
  EXPECT_EQ(vpi_release_handle(secondModule), 1);
  EXPECT_EQ(vpi_release_handle(module), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedVPITraversal, RefObjectValuesUseActualSemanticKindsAndStorage) {
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

  auto store = [&](const char *name, uint64_t value, uint64_t unknown,
                   uint64_t width) {
    obelisk_rt_design_cursor_v1 cursor{};
    ASSERT_EQ(
        obelisk_rt_v1_design_lookup(dumpDescriptor.execution,
                                    reinterpret_cast<const uint8_t *>(name),
                                    std::strlen(name), &cursor),
        OBELISK_RT_OK);
    uint64_t stateOffset = 0;
    ASSERT_EQ(obelisk_rt_design_state_offset(context, cursor, 0, &stateOffset),
              OBELISK_RT_OK);
    ASSERT_LE(stateOffset + width,
              static_cast<uint64_t>(context->stateValue.size()) * 64);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    for (uint64_t bit = 0; bit != width; ++bit) {
      const uint64_t absolute = stateOffset + bit;
      const uint64_t mask = uint64_t{1} << (absolute % 64);
      uint64_t &valueWord = context->stateValue[absolute / 64];
      uint64_t &unknownWord = context->stateUnknown[absolute / 64];
      valueWord = ((value >> bit) & 1) ? valueWord | mask : valueWord & ~mask;
      unknownWord =
          ((unknown >> bit) & 1) ? unknownWord | mask : unknownWord & ~mask;
    }
  };

  store("top.values.scalar", 1, 1, 1);
  store("top.values.int", static_cast<uint32_t>(-42), 0, 32);
  constexpr uint64_t timeBits = UINT64_C(0x1234567887654321);
  store("top.values.time", timeBits, 0, 64);
  double real = 3.25;
  uint64_t realBits = 0;
  static_assert(sizeof(real) == sizeof(realBits));
  std::memcpy(&realBits, &real, sizeof(realBits));
  store("top.values.real", realBits, 0, 64);

  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  constexpr char stringText[] = "heap-backed-ref-value";
  obelisk_rt_string_v1 string = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, stringText,
                                        sizeof(stringText) - 1, &string),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 stringRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &stringRoot, &string),
            OBELISK_RT_OK);
  store("top.values.string", string, 0, 64);

  char moduleName[] = "top.values";
  vpiHandle module = vpi_handle_by_name(moduleName, nullptr);
  ASSERT_NE(module, nullptr);
  vpiHandle iterator = vpi_iterate(vpiIODecl, module);
  ASSERT_NE(iterator, nullptr);
  constexpr std::array<const char *, 5> memberNames{
      "scalar_ref", "int_ref", "time_ref", "real_ref", "string_ref"};
  constexpr std::array<const char *, 5> actualNames{
      "top.values.scalar", "top.values.int", "top.values.time",
      "top.values.real", "top.values.string"};
  constexpr std::array<PLI_INT32, 5> actualTypes{
      vpiLogicVar, vpiIntVar, vpiTimeVar, vpiRealVar, vpiStringVar};
  constexpr std::array<PLI_INT32, 5> defaultFormats{
      vpiScalarVal, vpiIntVal, vpiTimeVal, vpiRealVal, vpiStringVal};

  const char *detachedString = nullptr;
  for (size_t index = 0; index != memberNames.size(); ++index) {
    vpiHandle declaration = vpi_scan(iterator);
    ASSERT_NE(declaration, nullptr);
    EXPECT_STREQ(vpi_get_str(vpiName, declaration), memberNames[index]);
    vpiHandle reference = vpi_handle(vpiExpr, declaration);
    ASSERT_NE(reference, nullptr);
    EXPECT_EQ(vpi_get(vpiType, reference), vpiRefObj);
    EXPECT_EQ(vpi_get(vpiSigned, reference), index == 1 ? 1 : 0);
    vpiHandle actual = vpi_handle(vpiActual, reference);
    ASSERT_NE(actual, nullptr);
    EXPECT_EQ(vpi_get(vpiType, actual), actualTypes[index]);
    EXPECT_STREQ(vpi_get_str(vpiFullName, actual), actualNames[index]);
    EXPECT_EQ(vpi_compare_objects(reference, actual), 0);

    s_vpi_value value{};
    value.format = vpiObjTypeVal;
    vpi_get_value(reference, &value);
    s_vpi_error_info error{};
    const PLI_INT32 errorLevel = vpi_chk_error(&error);
    EXPECT_EQ(errorLevel, 0)
        << (error.message ? error.message : "unknown VPI error");
    if (errorLevel != 0) {
      EXPECT_EQ(vpi_release_handle(actual), 1);
      EXPECT_EQ(vpi_release_handle(reference), 1);
      EXPECT_EQ(vpi_release_handle(declaration), 1);
      continue;
    }
    EXPECT_EQ(value.format, defaultFormats[index]);
    if (index == 0)
      EXPECT_EQ(value.value.scalar, vpiZ);
    else if (index == 1)
      EXPECT_EQ(value.value.integer, -42);
    else if (index == 2) {
      ASSERT_NE(value.value.time, nullptr);
      EXPECT_EQ(value.value.time->type, vpiSimTime);
      EXPECT_EQ(value.value.time->high,
                static_cast<PLI_UINT32>(timeBits >> 32));
      EXPECT_EQ(value.value.time->low, static_cast<PLI_UINT32>(timeBits));
    } else if (index == 3) {
      EXPECT_DOUBLE_EQ(value.value.real, real);
    } else {
      ASSERT_NE(value.value.str, nullptr);
      EXPECT_STREQ(value.value.str, stringText);
      detachedString = value.value.str;
      EXPECT_EQ(vpi_get(vpiSize, actual), sizeof(stringText) - 1);
      EXPECT_EQ(vpi_get(vpiSize, reference), sizeof(stringText) - 1);
    }
    EXPECT_EQ(vpi_release_handle(actual), 1);
    EXPECT_EQ(vpi_release_handle(reference), 1);
    EXPECT_EQ(vpi_release_handle(declaration), 1);
  }
  EXPECT_EQ(vpi_scan(iterator), nullptr);

  // The VPI scratch result owns its bytes. Reclaiming the actual heap string
  // after the query must neither leak it nor invalidate the returned copy.
  store("top.values.string", 0, 0, 64);
  vpiHandle emptyIterator = vpi_iterate(vpiIODecl, module);
  ASSERT_NE(emptyIterator, nullptr);
  for (size_t index = 0; index != memberNames.size(); ++index) {
    vpiHandle declaration = vpi_scan(emptyIterator);
    ASSERT_NE(declaration, nullptr);
    if (index + 1 == memberNames.size()) {
      vpiHandle reference = vpi_handle(vpiExpr, declaration);
      ASSERT_NE(reference, nullptr);
      vpiHandle actual = vpi_handle(vpiActual, reference);
      ASSERT_NE(actual, nullptr);
      EXPECT_EQ(vpi_get(vpiSize, actual), 0);
      EXPECT_EQ(vpi_get(vpiSize, reference), 0);
      EXPECT_EQ(vpi_release_handle(actual), 1);
      EXPECT_EQ(vpi_release_handle(reference), 1);
    }
    EXPECT_EQ(vpi_release_handle(declaration), 1);
  }
  EXPECT_EQ(vpi_scan(emptyIterator), nullptr);
  obelisk_rt_object_v1 *stringObject =
      obelisk_rt_object_from_managed_word(string);
  ASSERT_NE(stringObject, nullptr);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &stringRoot),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_validate_string(context, string),
            OBELISK_RT_INVALID_HANDLE);
  ASSERT_NE(detachedString, nullptr);
  EXPECT_STREQ(detachedString, stringText);

  EXPECT_EQ(vpi_release_handle(module), 1);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
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
  uint64_t endpoint = read64(original, directory + 288);
  uint64_t instanceRelation = read64(original, directory + 304);
  uint64_t instanceRelationTarget = read64(original, directory + 320);
  uint64_t instanceRelationInverse = read64(original, directory + 336);
  ASSERT_EQ(read64(original, directory + 184), 2u);
  ASSERT_EQ(read64(original, directory + 200), 4u);
  ASSERT_EQ(read64(original, directory + 216), 10u);
  ASSERT_EQ(read64(original, directory + 232), 2u);
  ASSERT_EQ(read64(original, directory + 248), 10u);
  ASSERT_EQ(read64(original, directory + 264), 2u);
  ASSERT_EQ(read64(original, directory + 280), 10u);
  ASSERT_EQ(read64(original, directory + 296), 20u);
  ASSERT_EQ(read64(original, directory + 312), 5u);
  ASSERT_EQ(read64(original, directory + 328), 6u);
  ASSERT_EQ(read64(original, directory + 344), 6u);

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
  write32(malformed, endpoint, 0x3fffffff); // Endpoint is not an object.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, endpoint,
          read32(original, endpoint + 5 * 4)); // Other instance's storage.
  rejects(std::move(malformed));

  malformed = original;
  write16(malformed, member + 3 * 20 + 18,
          vpiInput); // Cross-scope actual requires ref direction.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, endpoint + 3 * 4,
          read32(original, endpoint + 1 * 4)); // Ref actual cannot be a net.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, endpoint + 3 * 4,
          UINT32_MAX); // Relation source endpoint is absent.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, typeBinding + 3 * 8 + 4,
          read32(original,
                 typeBinding + 1 * 8 + 4)); // Ref actual type mismatch.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, endpoint + 4 * 4,
          read32(original, endpoint)); // Interface IODecl is not direct.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceBinding + 8, 1); // Unknown specialization.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceBinding + 8,
          UINT32_MAX); // Member-bearing definitions require one.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceRelation, 2); // Unknown definition binding.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceRelation + 4, 3); // Missing RefObj role flag.
  rejects(std::move(malformed));

  malformed = original;
  write16(malformed, instanceRelation + 8, vpiActual); // Wrong auto edge.
  rejects(std::move(malformed));

  malformed = original;
  write16(malformed, instanceRelation + 10, 0); // Handle instead of iterate.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceRelation + 20 + 12,
          0); // Noncontiguous target range.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceRelationTarget,
          uint32_t{3} << 30); // Invalid target table/index.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceRelationTarget + 2 * 4,
          read32(malformed,
                 instanceRelationTarget + 4)); // Duplicate target in group.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceRelationInverse,
          UINT32_MAX); // Inverse target index is out of range.
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, instanceRelationInverse + 4,
          read32(original,
                 instanceRelationInverse)); // Duplicate/unsorted inverse.
  rejects(std::move(malformed));

  constexpr uint64_t objectSize = 96;
  uint64_t objects = read64(original, 64);
  uint32_t ownPortIndex =
      read32(original, instanceRelationTarget) & UINT32_C(0x3fffffff);
  uint64_t ownPort = objects + uint64_t{ownPortIndex} * objectSize;
  malformed = original;
  write32(malformed, ownPort + 4,
          read32(original, ownPort + 4) &
              ~uint32_t{OBELISK_RT_DESIGN_CAP_PORT_REF});
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, ownPort + 4,
          read32(original, ownPort + 4) |
              OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE);
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, ownPort + 4,
          read32(original, ownPort + 4) &
              ~uint32_t{OBELISK_RT_DESIGN_CAP_PORT_INPUT});
  rejects(std::move(malformed));

  malformed = original;
  write32(malformed, ownPort + 4,
          read32(original, ownPort + 4) &
              ~uint32_t{OBELISK_RT_DESIGN_CAP_PORT_OUTPUT});
  rejects(std::move(malformed));

  uint32_t downstreamPortIndex =
      read32(original, instanceRelationTarget + 4) & UINT32_C(0x3fffffff);
  uint64_t downstreamPort =
      objects + uint64_t{downstreamPortIndex} * objectSize;
  malformed = original;
  write32(malformed, downstreamPort + 4,
          read32(original, downstreamPort + 4) |
              OBELISK_RT_DESIGN_CAP_PORT_REF);
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
