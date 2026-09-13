//===- GeneratedVPIStaticArraysTest.cpp - Static array VPI tests --------===//

#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/Runtime.h"

#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" const obelisk_rt_process_descriptor_v1 staticArraysDescriptor asm(
    "static_arrays_process.__obelisk_process_descriptor");

namespace {

uint16_t get16(const std::vector<uint8_t> &bytes, size_t offset) {
  return uint16_t{bytes[offset]} | (uint16_t{bytes[offset + 1]} << 8);
}

uint32_t get32(const std::vector<uint8_t> &bytes, size_t offset) {
  uint32_t value = 0;
  for (unsigned byte = 0; byte != 4; ++byte)
    value |= uint32_t{bytes[offset + byte]} << (byte * 8);
  return value;
}

uint64_t get64(const std::vector<uint8_t> &bytes, size_t offset) {
  uint64_t value = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    value |= uint64_t{bytes[offset + byte]} << (byte * 8);
  return value;
}

void put16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
  for (unsigned byte = 0; byte != 4; ++byte)
    bytes[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
}

void put64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value) {
  for (unsigned byte = 0; byte != 8; ++byte)
    bytes[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
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

struct MutableStaticArraysImage {
  std::vector<uint8_t> bytes;
  obelisk_rt_execution_descriptor_v1 execution;

  MutableStaticArraysImage()
      : bytes(staticArraysDescriptor.execution->design_database,
              staticArraysDescriptor.execution->design_database +
                  staticArraysDescriptor.execution->design_database_size),
        execution(*staticArraysDescriptor.execution) {
    execution.design_database = bytes.data();
  }

  void seal() { put64(bytes, 32, imageChecksum(bytes)); }
};

struct VPIContext {
  obelisk_rt_context *context = nullptr;

  void start() {
    ASSERT_NE(staticArraysDescriptor.execution, nullptr);
    ASSERT_EQ(
        staticArraysDescriptor.execution->flags &
            (OBELISK_RT_EXECUTION_VPI_READ | OBELISK_RT_EXECUTION_VPI_WRITE),
        OBELISK_RT_EXECUTION_VPI_READ);
    ASSERT_EQ(obelisk_rt_v1_design_validate(staticArraysDescriptor.execution),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_context_create_for_design(
                  staticArraysDescriptor.execution, &context),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  }

  ~VPIContext() {
    if (context)
      obelisk_rt_v1_context_destroy(context);
  }
};

int64_t integerValue(vpiHandle handle) {
  s_vpi_value value{};
  value.format = vpiIntVal;
  vpi_get_value(handle, &value);
  return value.value.integer;
}

std::vector<std::string> scanNames(vpiHandle iterator) {
  std::vector<std::string> names;
  for (vpiHandle item = vpi_scan(iterator); item; item = vpi_scan(iterator)) {
    const char *name =
        reinterpret_cast<const char *>(vpi_get_str(vpiFullName, item));
    EXPECT_NE(name, nullptr);
    if (name)
      names.emplace_back(name);
    EXPECT_EQ(vpi_release_handle(item), 1);
  }
  return names;
}

std::vector<std::array<int64_t, 2>> scanRanges(vpiHandle object) {
  std::vector<std::array<int64_t, 2>> ranges;
  vpiHandle iterator = vpi_iterate(vpiRange, object);
  if (!iterator)
    return ranges;
  for (vpiHandle range = vpi_scan(iterator); range;
       range = vpi_scan(iterator)) {
    vpiHandle left = vpi_handle(vpiLeftRange, range);
    vpiHandle right = vpi_handle(vpiRightRange, range);
    EXPECT_NE(left, nullptr);
    EXPECT_NE(right, nullptr);
    if (left && right)
      ranges.push_back({integerValue(left), integerValue(right)});
    if (left) {
      EXPECT_EQ(vpi_release_handle(left), 1);
    }
    if (right) {
      EXPECT_EQ(vpi_release_handle(right), 1);
    }
    EXPECT_EQ(vpi_release_handle(range), 1);
  }
  return ranges;
}

TEST(GeneratedVPIStaticArrays, TraversesSpecificAndGenericInstanceRelations) {
  VPIContext fixture;
  fixture.start();

  char name[] = "top.m";
  vpiHandle array = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(array, nullptr);
  EXPECT_EQ(vpi_get(vpiType, array), vpiModuleArray);
  EXPECT_EQ(vpi_get(vpiSize, array), 4);

  constexpr std::array<const char *, 4> expected{"top.m[1][-1]", "top.m[1][0]",
                                                 "top.m[0][-1]", "top.m[0][0]"};
  for (PLI_INT32 selector : {vpiModule, vpiInstance}) {
    vpiHandle iterator = vpi_iterate(selector, array);
    ASSERT_NE(iterator, nullptr) << selector;
    EXPECT_EQ(scanNames(iterator),
              (std::vector<std::string>(expected.begin(), expected.end())));
  }

  vpiHandle partial = vpi_handle_by_index(array, 1);
  ASSERT_NE(partial, nullptr);
  EXPECT_EQ(vpi_get(vpiType, partial), vpiModuleArray);
  EXPECT_EQ(vpi_get(vpiSize, partial), 2);
  EXPECT_STREQ(vpi_get_str(vpiFullName, partial), "top.m[1]");
  for (PLI_INT32 selector : {vpiModule, vpiInstance}) {
    vpiHandle iterator = vpi_iterate(selector, partial);
    ASSERT_NE(iterator, nullptr) << selector;
    EXPECT_EQ(scanNames(iterator),
              (std::vector<std::string>{"top.m[1][-1]", "top.m[1][0]"}));
  }

  EXPECT_EQ(vpi_release_handle(partial), 1);
  EXPECT_EQ(vpi_release_handle(array), 1);
}

TEST(GeneratedVPIStaticArrays,
     ResolvesPartialRelationArraySelectionsByHierarchicalName) {
  VPIContext fixture;
  fixture.start();

  struct Case {
    const char *arrayName;
    const char *partialName;
    PLI_INT32 index;
    PLI_INT32 partialType;
    PLI_INT32 childSelector;
    std::array<int64_t, 2> remainingRange;
    std::vector<std::string> children;
  };
  const Case cases[]{
      {"top.m",
       "top.m[1]",
       1,
       vpiModuleArray,
       vpiModule,
       {-1, 0},
       {"top.m[1][-1]", "top.m[1][0]"}},
      {"top.events",
       "top.events[0]",
       0,
       vpiNamedEventArray,
       vpiNamedEvent,
       {-1, 0},
       {"top.events[0][-1]", "top.events[0][0]"}},
  };
  for (const Case &item : cases) {
    std::vector<char> arrayName(
        item.arrayName, item.arrayName + std::strlen(item.arrayName) + 1);
    std::vector<char> partialName(
        item.partialName, item.partialName + std::strlen(item.partialName) + 1);
    vpiHandle array = vpi_handle_by_name(arrayName.data(), nullptr);
    ASSERT_NE(array, nullptr);
    vpiHandle indexed = vpi_handle_by_index(array, item.index);
    vpiHandle named = vpi_handle_by_name(partialName.data(), nullptr);
    ASSERT_NE(indexed, nullptr);
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(vpi_compare_objects(named, indexed), 1);
    EXPECT_EQ(vpi_get(vpiType, named), item.partialType);
    EXPECT_EQ(vpi_get(vpiSize, named), vpi_get(vpiSize, indexed));
    EXPECT_STREQ(vpi_get_str(vpiFullName, named), item.partialName);
    EXPECT_EQ(scanRanges(named),
              (std::vector<std::array<int64_t, 2>>{item.remainingRange}));
    vpiHandle children = vpi_iterate(item.childSelector, named);
    ASSERT_NE(children, nullptr);
    EXPECT_EQ(scanNames(children), item.children);
    EXPECT_EQ(vpi_release_handle(named), 1);
    EXPECT_EQ(vpi_release_handle(indexed), 1);
    EXPECT_EQ(vpi_release_handle(array), 1);
  }

  char topName[] = "top";
  char relativeName[] = "m[1]";
  char absoluteName[] = "$root.top.m[1]";
  char arrayName[] = "top.m";
  vpiHandle top = vpi_handle_by_name(topName, nullptr);
  vpiHandle array = vpi_handle_by_name(arrayName, nullptr);
  ASSERT_NE(top, nullptr);
  ASSERT_NE(array, nullptr);
  vpiHandle expected = vpi_handle_by_index(array, 1);
  vpiHandle relative = vpi_handle_by_name(relativeName, top);
  vpiHandle absolute = vpi_handle_by_name(absoluteName, top);
  ASSERT_NE(expected, nullptr);
  ASSERT_NE(relative, nullptr);
  ASSERT_NE(absolute, nullptr);
  EXPECT_EQ(vpi_compare_objects(relative, expected), 1);
  EXPECT_EQ(vpi_compare_objects(absolute, expected), 1);
  EXPECT_EQ(vpi_release_handle(absolute), 1);
  EXPECT_EQ(vpi_release_handle(relative), 1);
  EXPECT_EQ(vpi_release_handle(expected), 1);
  EXPECT_EQ(vpi_release_handle(array), 1);
  EXPECT_EQ(vpi_release_handle(top), 1);
}

TEST(GeneratedVPIStaticArrays, TraversesRangesAndPreservesIteratorUse) {
  VPIContext fixture;
  fixture.start();

  char name[] = "top.m";
  vpiHandle array = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(array, nullptr);
  vpiHandle ranges = vpi_iterate(vpiRange, array);
  ASSERT_NE(ranges, nullptr);
  EXPECT_EQ(vpi_release_handle(array), 1);

  vpiHandle use = vpi_handle(vpiUse, ranges);
  ASSERT_NE(use, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, use), "top.m");
  EXPECT_EQ(vpi_release_handle(use), 1);

  constexpr std::array<std::array<int64_t, 2>, 2> expected{
      {{{1, 0}}, {{-1, 0}}}};
  for (const auto &bounds : expected) {
    vpiHandle range = vpi_scan(ranges);
    ASSERT_NE(range, nullptr);
    vpiHandle left = vpi_handle(vpiLeftRange, range);
    vpiHandle right = vpi_handle(vpiRightRange, range);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(integerValue(left), bounds[0]);
    EXPECT_EQ(integerValue(right), bounds[1]);
    EXPECT_EQ(vpi_release_handle(left), 1);
    EXPECT_EQ(vpi_release_handle(right), 1);
    EXPECT_EQ(vpi_release_handle(range), 1);
  }
  EXPECT_EQ(vpi_scan(ranges), nullptr);

  array = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(array, nullptr);
  vpiHandle partial = vpi_handle_by_index(array, 0);
  ASSERT_NE(partial, nullptr);
  vpiHandle partialRanges = vpi_iterate(vpiRange, partial);
  ASSERT_NE(partialRanges, nullptr);
  vpiHandle elements = vpi_iterate(vpiInstance, partial);
  ASSERT_NE(elements, nullptr);
  EXPECT_EQ(vpi_release_handle(partial), 1);
  EXPECT_EQ(vpi_release_handle(array), 1);

  use = vpi_handle(vpiUse, partialRanges);
  ASSERT_NE(use, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, use), "top.m[0]");
  EXPECT_EQ(vpi_release_handle(use), 1);
  vpiHandle range = vpi_scan(partialRanges);
  ASSERT_NE(range, nullptr);
  vpiHandle left = vpi_handle(vpiLeftRange, range);
  vpiHandle right = vpi_handle(vpiRightRange, range);
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(integerValue(left), -1);
  EXPECT_EQ(integerValue(right), 0);
  EXPECT_EQ(vpi_release_handle(left), 1);
  EXPECT_EQ(vpi_release_handle(right), 1);
  EXPECT_EQ(vpi_release_handle(range), 1);
  EXPECT_EQ(vpi_scan(partialRanges), nullptr);

  use = vpi_handle(vpiUse, elements);
  ASSERT_NE(use, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, use), "top.m[0]");
  EXPECT_EQ(vpi_release_handle(use), 1);
  EXPECT_EQ(scanNames(elements),
            (std::vector<std::string>{"top.m[0][-1]", "top.m[0][0]"}));
}

TEST(GeneratedVPIStaticArrays, ResolvesTerminalIdentityAndReverseIndices) {
  VPIContext fixture;
  fixture.start();

  char arrayName[] = "top.m";
  char memberName[] = "top.m[0][-1]";
  vpiHandle array = vpi_handle_by_name(arrayName, nullptr);
  vpiHandle named = vpi_handle_by_name(memberName, nullptr);
  ASSERT_NE(array, nullptr);
  ASSERT_NE(named, nullptr);
  PLI_INT32 indices[]{0, -1};
  vpiHandle selected = vpi_handle_by_multi_index(array, 2, indices);
  ASSERT_NE(selected, nullptr);
  EXPECT_EQ(vpi_get(vpiType, selected), vpiModule);
  EXPECT_EQ(vpi_compare_objects(named, selected), 1);
  EXPECT_EQ(vpi_get(vpiArrayMember, selected), 1);

  char topName[] = "top";
  vpiHandle top = vpi_handle_by_name(topName, nullptr);
  ASSERT_NE(top, nullptr);
  vpiHandle instance = vpi_handle(vpiInstance, selected);
  ASSERT_NE(instance, nullptr);
  EXPECT_EQ(vpi_compare_objects(top, instance), 1);
  EXPECT_EQ(vpi_release_handle(instance), 1);

  vpiHandle containing = vpi_handle(vpiModuleArray, selected);
  ASSERT_NE(containing, nullptr);
  EXPECT_EQ(vpi_compare_objects(array, containing), 1);
  EXPECT_EQ(vpi_release_handle(containing), 1);
  containing = vpi_handle(vpiInstanceArray, selected);
  ASSERT_NE(containing, nullptr);
  EXPECT_EQ(vpi_compare_objects(array, containing), 1);
  EXPECT_EQ(vpi_release_handle(containing), 1);
  EXPECT_EQ(vpi_handle(vpiTypespec, array), nullptr);

  vpiHandle index = vpi_handle(vpiIndex, selected);
  ASSERT_NE(index, nullptr);
  EXPECT_EQ(integerValue(index), -1);
  EXPECT_EQ(vpi_release_handle(index), 1);
  // Module instances expose their final source index through vpi_handle;
  // unlike named events, IEEE 1800 does not define vpi_iterate(vpiIndex).
  EXPECT_EQ(vpi_iterate(vpiIndex, selected), nullptr);

  vpiHandle outer = vpi_handle_by_index(array, 1);
  ASSERT_NE(outer, nullptr);
  vpiHandle chained = vpi_handle_by_index(outer, 0);
  ASSERT_NE(chained, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiFullName, chained), "top.m[1][0]");
  EXPECT_EQ(vpi_release_handle(chained), 1);
  EXPECT_EQ(vpi_release_handle(outer), 1);
  EXPECT_EQ(vpi_handle_by_index(array, 2), nullptr);
  EXPECT_EQ(vpi_handle_by_multi_index(array, 0, indices), nullptr);
  PLI_INT32 tooMany[]{1, -1, 0};
  EXPECT_EQ(vpi_handle_by_multi_index(array, 3, tooMany), nullptr);

  EXPECT_EQ(vpi_release_handle(top), 1);
  EXPECT_EQ(vpi_release_handle(selected), 1);
  EXPECT_EQ(vpi_release_handle(named), 1);
  EXPECT_EQ(vpi_release_handle(array), 1);
}

TEST(GeneratedVPIStaticArrays, ContextTeardownOwnsOutstandingQueryHandles) {
  VPIContext fixture;
  fixture.start();
  char name[] = "top.m";
  vpiHandle array = vpi_handle_by_name(name, nullptr);
  ASSERT_NE(array, nullptr);
  ASSERT_NE(vpi_handle_by_index(array, 1), nullptr);
  ASSERT_NE(vpi_iterate(vpiInstance, array), nullptr);
  ASSERT_NE(vpi_iterate(vpiRange, array), nullptr);
  obelisk_rt_v1_context_destroy(fixture.context);
  fixture.context = nullptr;
}

TEST(GeneratedVPIStaticArrays, TraversesInterfaceAndProgramArrayAliases) {
  VPIContext fixture;
  fixture.start();

  struct Case {
    const char *arrayName;
    const char *memberName;
    PLI_INT32 index;
    PLI_INT32 arrayType;
    PLI_INT32 memberType;
    PLI_INT32 elementSelector;
  };
  constexpr Case cases[]{
      {"top.i", "top.i[2]", 2, vpiInterfaceArray, vpiInterface, vpiInterface},
      {"top.p", "top.p[-2]", -2, vpiProgramArray, vpiProgram, vpiProgram}};
  for (const Case &item : cases) {
    std::vector<char> arrayName(
        item.arrayName, item.arrayName + std::strlen(item.arrayName) + 1);
    vpiHandle array = vpi_handle_by_name(arrayName.data(), nullptr);
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(vpi_get(vpiType, array), item.arrayType);
    EXPECT_EQ(vpi_get(vpiSize, array), 1);
    for (PLI_INT32 selector : {item.elementSelector, vpiInstance}) {
      vpiHandle iterator = vpi_iterate(selector, array);
      ASSERT_NE(iterator, nullptr) << item.arrayName << ": " << selector;
      EXPECT_EQ(scanNames(iterator),
                (std::vector<std::string>{item.memberName}));
    }
    vpiHandle member = vpi_handle_by_index(array, item.index);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(vpi_get(vpiType, member), item.memberType);
    vpiHandle index = vpi_handle(vpiIndex, member);
    ASSERT_NE(index, nullptr);
    EXPECT_EQ(integerValue(index), item.index);
    EXPECT_EQ(vpi_release_handle(index), 1);
    EXPECT_EQ(vpi_handle(item.arrayType, member), nullptr);
    vpiHandle containing = vpi_handle(vpiInstanceArray, member);
    ASSERT_NE(containing, nullptr) << item.arrayName;
    EXPECT_EQ(vpi_compare_objects(array, containing), 1);
    EXPECT_EQ(vpi_release_handle(containing), 1);
    EXPECT_EQ(vpi_handle(vpiTypespec, array), nullptr);
    EXPECT_EQ(vpi_release_handle(member), 1);
    EXPECT_EQ(vpi_release_handle(array), 1);
  }
}

TEST(GeneratedVPIStaticArrays, TraversesPrimitiveArraysAndLexicalModules) {
  VPIContext fixture;
  fixture.start();

  char topName[] = "top";
  vpiHandle top = vpi_handle_by_name(topName, nullptr);
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(scanNames(vpi_iterate(vpiPrimitive, top)),
            (std::vector<std::string>{"top.gate", "top.switch", "top.udp"}));
  EXPECT_EQ(scanNames(vpi_iterate(vpiPrimitiveArray, top)),
            (std::vector<std::string>{"top.ga", "top.sa", "top.ua"}));
  for (const auto &[name, expectedSize] :
       std::array<std::pair<const char *, PLI_INT32>, 3>{
           {{"top.gate", 2}, {"top.switch", 3}, {"top.udp", 4}}}) {
    std::vector<char> mutableName(name, name + std::strlen(name) + 1);
    vpiHandle primitive = vpi_handle_by_name(mutableName.data(), nullptr);
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(vpi_get(vpiSize, primitive), expectedSize);
    EXPECT_EQ(vpi_get(vpiArrayMember, primitive), 0);
    EXPECT_EQ(vpi_handle(vpiIndex, primitive), nullptr);
    EXPECT_EQ(vpi_release_handle(primitive), 1);
  }

  struct Case {
    const char *arrayName;
    const char *memberName;
    PLI_INT32 index;
    PLI_INT32 arrayType;
    PLI_INT32 memberType;
    PLI_INT32 inputCount;
  };
  constexpr Case cases[]{
      {"top.ga", "top.ga[3]", 3, vpiGateArray, vpiGate, 1},
      {"top.sa", "top.sa[-1]", -1, vpiSwitchArray, vpiSwitch, 2},
      {"top.ua", "top.ua[9]", 9, vpiUdpArray, vpiUdp, 3}};
  for (const Case &item : cases) {
    std::vector<char> arrayName(
        item.arrayName, item.arrayName + std::strlen(item.arrayName) + 1);
    vpiHandle array = vpi_handle_by_name(arrayName.data(), nullptr);
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(vpi_get(vpiType, array), item.arrayType);
    EXPECT_EQ(vpi_get(vpiSize, array), 1);
    EXPECT_EQ(scanNames(vpi_iterate(vpiPrimitive, array)),
              (std::vector<std::string>{item.memberName}));
    vpiHandle member = vpi_handle_by_index(array, item.index);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(vpi_get(vpiType, member), item.memberType);
    EXPECT_EQ(vpi_get(vpiSize, member), item.inputCount);
    EXPECT_EQ(vpi_get(vpiArrayMember, member), 1);
    vpiHandle containing = vpi_handle(vpiPrimitiveArray, member);
    ASSERT_NE(containing, nullptr);
    EXPECT_EQ(vpi_compare_objects(array, containing), 1);
    EXPECT_EQ(vpi_release_handle(containing), 1);
    vpiHandle index = vpi_handle(vpiIndex, member);
    ASSERT_NE(index, nullptr);
    EXPECT_EQ(integerValue(index), item.index);
    EXPECT_EQ(vpi_release_handle(index), 1);
    vpiHandle module = vpi_handle(vpiModule, member);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(vpi_compare_objects(top, module), 1);
    EXPECT_EQ(vpi_release_handle(module), 1);
    EXPECT_EQ(vpi_handle(vpiTypespec, array), nullptr);
    EXPECT_EQ(vpi_release_handle(member), 1);
    EXPECT_EQ(vpi_release_handle(array), 1);
  }
  EXPECT_EQ(vpi_release_handle(top), 1);
}

TEST(GeneratedVPIStaticArrays, TraversesNamedEventArraysAndEventTypespecs) {
  VPIContext fixture;
  fixture.start();

  char arrayName[] = "top.events";
  char memberName[] = "top.events[1][-1]";
  vpiHandle array = vpi_handle_by_name(arrayName, nullptr);
  ASSERT_NE(array, nullptr);
  char topName[] = "top";
  vpiHandle top = vpi_handle_by_name(topName, nullptr);
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(scanNames(vpi_iterate(vpiNamedEvent, top)),
            (std::vector<std::string>{"top.event"}));
  EXPECT_EQ(scanNames(vpi_iterate(vpiNamedEventArray, top)),
            (std::vector<std::string>{"top.events"}));
  char scalarName[] = "top.event";
  vpiHandle scalar = vpi_handle_by_name(scalarName, nullptr);
  ASSERT_NE(scalar, nullptr);
  EXPECT_EQ(vpi_get(vpiAutomatic, scalar), 0);
  EXPECT_EQ(vpi_get(vpiAutomatic, array), 0);
  EXPECT_EQ(vpi_get(vpiArrayMember, scalar), 0);
  EXPECT_EQ(vpi_iterate(vpiIndex, scalar), nullptr);
  EXPECT_EQ(vpi_get(vpiSize, array), vpiUndefined);
  EXPECT_EQ(
      scanNames(vpi_iterate(vpiNamedEvent, array)),
      (std::vector<std::string>{"top.events[1][-1]", "top.events[1][0]",
                                "top.events[0][-1]", "top.events[0][0]"}));
  PLI_INT32 indices[]{1, -1};
  vpiHandle selected = vpi_handle_by_multi_index(array, 2, indices);
  vpiHandle named = vpi_handle_by_name(memberName, nullptr);
  ASSERT_NE(selected, nullptr);
  ASSERT_NE(named, nullptr);
  EXPECT_EQ(vpi_get(vpiAutomatic, selected), 0);
  EXPECT_EQ(vpi_get(vpiAutomatic, named), 0);
  EXPECT_EQ(vpi_compare_objects(selected, named), 1);
  EXPECT_EQ(vpi_get(vpiArrayMember, selected), 1);
  vpiHandle parent = vpi_handle(vpiParent, selected);
  ASSERT_NE(parent, nullptr);
  EXPECT_EQ(vpi_compare_objects(parent, array), 1);
  EXPECT_EQ(vpi_release_handle(parent), 1);
  for (PLI_INT32 selector : {vpiModule, vpiInstance}) {
    vpiHandle enclosing = vpi_handle(selector, selected);
    ASSERT_NE(enclosing, nullptr) << selector;
    EXPECT_EQ(vpi_compare_objects(enclosing, top), 1);
    EXPECT_EQ(vpi_release_handle(enclosing), 1);
  }

  vpiHandle partial = vpi_handle_by_index(array, 0);
  ASSERT_NE(partial, nullptr);
  EXPECT_EQ(vpi_get(vpiType, partial), vpiNamedEventArray);
  EXPECT_EQ(vpi_get(vpiAutomatic, partial), 0);
  EXPECT_STREQ(vpi_get_str(vpiFullName, partial), "top.events[0]");
  vpiHandle partialTypespec = vpi_handle(vpiTypespec, partial);
  ASSERT_NE(partialTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, partialTypespec), vpiArrayTypespec);
  EXPECT_EQ(vpi_release_handle(partialTypespec), 1);
  EXPECT_EQ(vpi_release_handle(partial), 1);

  vpiHandle arrayTypespec = vpi_handle(vpiTypespec, array);
  ASSERT_NE(arrayTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, arrayTypespec), vpiArrayTypespec);
  vpiHandle elementTypespec = vpi_handle(vpiElemTypespec, arrayTypespec);
  ASSERT_NE(elementTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, elementTypespec), vpiArrayTypespec);
  vpiHandle eventTypespec = vpi_handle(vpiElemTypespec, elementTypespec);
  ASSERT_NE(eventTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, eventTypespec), vpiEventTypespec);
  vpiHandle selectedTypespec = vpi_handle(vpiTypespec, selected);
  ASSERT_NE(selectedTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, selectedTypespec), vpiEventTypespec);

  vpiHandle indexIterator = vpi_iterate(vpiIndex, selected);
  ASSERT_NE(indexIterator, nullptr);
  vpiHandle inner = vpi_scan(indexIterator);
  vpiHandle outer = vpi_scan(indexIterator);
  ASSERT_NE(inner, nullptr);
  ASSERT_NE(outer, nullptr);
  EXPECT_EQ(integerValue(inner), -1);
  EXPECT_EQ(integerValue(outer), 1);
  EXPECT_EQ(vpi_scan(indexIterator), nullptr);
  EXPECT_EQ(vpi_release_handle(inner), 1);
  EXPECT_EQ(vpi_release_handle(outer), 1);

  EXPECT_EQ(vpi_release_handle(selectedTypespec), 1);
  EXPECT_EQ(vpi_release_handle(eventTypespec), 1);
  EXPECT_EQ(vpi_release_handle(elementTypespec), 1);
  EXPECT_EQ(vpi_release_handle(arrayTypespec), 1);
  EXPECT_EQ(vpi_release_handle(named), 1);
  EXPECT_EQ(vpi_release_handle(selected), 1);
  EXPECT_EQ(vpi_release_handle(array), 1);
  EXPECT_EQ(vpi_release_handle(scalar), 1);
  EXPECT_EQ(vpi_release_handle(top), 1);
}

TEST(GeneratedVPIStaticArrays, DirectIndexedNameInheritsProtectedArraySource) {
  MutableStaticArraysImage image;
  const size_t directory = get32(image.bytes, 12);
  const size_t objects = get64(image.bytes, 64);
  const size_t objectCount = get64(image.bytes, 72);
  uint32_t eventArray = UINT32_MAX;
  for (uint32_t index = 0; index != objectCount; ++index)
    if (get32(image.bytes, objects + size_t{index} * 96) >> 16 ==
        vpiNamedEventArray) {
      eventArray = index;
      break;
    }
  ASSERT_NE(eventArray, UINT32_MAX);
  const size_t property = image.bytes.size();
  image.bytes.resize(property + 16, 0);
  image.execution.design_database = image.bytes.data();
  image.execution.design_database_size = image.bytes.size();
  put64(image.bytes, 24, image.bytes.size());
  put64(image.bytes, directory + 112, property);
  put64(image.bytes, directory + 120, 1);
  put32(image.bytes, property, (uint32_t{1} << 30) | eventArray);
  put16(image.bytes, property + 4, vpiIsProtected);
  put16(image.bytes, property + 6, 0);
  put64(image.bytes, property + 8, 1);
  image.seal();
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE | OBELISK_RT_EXECUTION_VPI_READ,
      0,
      nullptr,
      0,
      image.bytes.data(),
      image.bytes.size(),
      image.execution.state_bit_count,
      0};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(context), OBELISK_RT_OK);
  char arrayName[] = "top.events";
  char partialName[] = "top.events[0]";
  vpiHandle array = vpi_handle_by_name(arrayName, nullptr);
  ASSERT_NE(array, nullptr);
  EXPECT_EQ(vpi_get(vpiIsProtected, array), 1);
  EXPECT_EQ(vpi_handle_by_index(array, 0), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  vpiHandle partial = vpi_handle_by_name(partialName, nullptr);
  ASSERT_NE(partial, nullptr);
  EXPECT_EQ(vpi_get(vpiIsProtected, partial), 1);
  EXPECT_EQ(vpi_get_str(vpiName, partial), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  EXPECT_EQ(vpi_release_handle(partial), 1);
  EXPECT_EQ(vpi_release_handle(array), 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedVPIStaticArrays, TraversesSparseAndEmptyGenerateArrays) {
  VPIContext fixture;
  fixture.start();

  char sparseName[] = "top.generated";
  vpiHandle sparse = vpi_handle_by_name(sparseName, nullptr);
  ASSERT_NE(sparse, nullptr);
  EXPECT_EQ(vpi_get(vpiSize, sparse), 2);
  EXPECT_EQ(
      scanNames(vpi_iterate(vpiGenScope, sparse)),
      (std::vector<std::string>{"top.generated[-3]", "top.generated[5]"}));
  vpiHandle genScopes = vpi_iterate(vpiGenScope, sparse);
  ASSERT_NE(genScopes, nullptr);
  vpiHandle use = vpi_handle(vpiUse, genScopes);
  ASSERT_NE(use, nullptr);
  EXPECT_EQ(vpi_compare_objects(use, sparse), 1);
  EXPECT_EQ(vpi_release_handle(use), 1);
  EXPECT_EQ(
      scanNames(genScopes),
      (std::vector<std::string>{"top.generated[-3]", "top.generated[5]"}));
  for (PLI_INT32 index : {-3, 5}) {
    vpiHandle member = vpi_handle_by_index(sparse, index);
    ASSERT_NE(member, nullptr) << index;
    EXPECT_EQ(vpi_get(vpiType, member), vpiGenScope);
    EXPECT_EQ(vpi_get(vpiArrayMember, member), 1);
    vpiHandle memberIndex = vpi_handle(vpiIndex, member);
    ASSERT_NE(memberIndex, nullptr);
    EXPECT_EQ(integerValue(memberIndex), index);
    EXPECT_EQ(vpi_release_handle(memberIndex), 1);
    char topName[] = "top";
    vpiHandle top = vpi_handle_by_name(topName, nullptr);
    ASSERT_NE(top, nullptr);
    vpiHandle instance = vpi_handle(vpiInstance, member);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(vpi_compare_objects(top, instance), 1);
    EXPECT_EQ(vpi_release_handle(instance), 1);
    EXPECT_EQ(vpi_release_handle(top), 1);
    EXPECT_EQ(vpi_release_handle(member), 1);
  }
  EXPECT_EQ(vpi_handle_by_index(sparse, 4), nullptr);
  EXPECT_EQ(vpi_release_handle(sparse), 1);

  char emptyName[] = "top.empty_generated";
  vpiHandle empty = vpi_handle_by_name(emptyName, nullptr);
  ASSERT_NE(empty, nullptr);
  EXPECT_EQ(vpi_get(vpiSize, empty), 0);
  EXPECT_EQ(vpi_iterate(vpiGenScope, empty), nullptr);
  EXPECT_EQ(vpi_handle_by_index(empty, 0), nullptr);
  EXPECT_EQ(vpi_release_handle(empty), 1);
}

TEST(GeneratedVPIStaticArrays, RejectsCorruptRelationIndexImage) {
  using namespace obelisk::reflection;
  constexpr uint32_t objectTableTag = uint32_t{1} << 30;

  // Sparse generate lookup has separate source-ordinal and sorted-key slices.
  // Both are required to describe the same bijection.
  {
    MutableStaticArraysImage image;
    ASSERT_EQ(obelisk_rt_v1_design_validate(&image.execution), OBELISK_RT_OK);
    size_t directory = get32(image.bytes, 12);
    size_t indices = get64(image.bytes, directory + 48);
    size_t indexCount = get64(image.bytes, directory + 56);
    size_t keys = get64(image.bytes, directory + 80);
    bool corrupted = false;
    for (size_t index = 0; index != indexCount; ++index) {
      size_t record = indices + index * RelationIndexLayout.size;
      if (get16(image.bytes, record + 10) != 1)
        continue;
      uint32_t firstOrdinalKey = get32(image.bytes, record + 16);
      put64(image.bytes,
            keys + size_t{firstOrdinalKey} * RelationIndexKeyLayout.size,
            UINT64_C(0x5a5a5a5a));
      corrupted = true;
      break;
    }
    ASSERT_TRUE(corrupted);
    image.seal();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&image.execution),
              OBELISK_RT_INVALID_DESIGN);
  }

  // A reverse vpiPrimitiveArray edge may target another legal primitive-array
  // kind. It must nevertheless point to the exact canonical indexed container.
  {
    MutableStaticArraysImage image;
    size_t directory = get32(image.bytes, 12);
    size_t indices = get64(image.bytes, directory + 48);
    size_t indexCount = get64(image.bytes, directory + 56);
    size_t members = get64(image.bytes, directory + 96);
    size_t memberCount = get64(image.bytes, directory + 104);
    size_t objects = get64(image.bytes, 64);
    size_t objectCount = get64(image.bytes, 72);
    size_t relations = get64(image.bytes, 160);
    size_t relationCount = get64(image.bytes, 168);
    uint32_t switchArray = UINT32_MAX;
    for (uint32_t index = 0; index != objectCount; ++index)
      if (get32(image.bytes, objects + size_t{index} * 96) >> 16 ==
          vpiSwitchArray) {
        switchArray = index;
        break;
      }
    ASSERT_NE(switchArray, UINT32_MAX);

    bool corrupted = false;
    for (size_t memberIndex = 0; memberIndex != memberCount; ++memberIndex) {
      size_t member = members + memberIndex * RelationIndexMemberLayout.size;
      uint32_t packedMember = get32(image.bytes, member);
      uint32_t memberTable = packedMember >> 30;
      uint32_t memberObject = packedMember & UINT32_C(0x3fffffff);
      if (memberTable != 1 ||
          get32(image.bytes, objects + size_t{memberObject} * 96) >> 16 !=
              vpiGate)
        continue;
      uint32_t relationIndex = get32(image.bytes, member + 4);
      ASSERT_LT(relationIndex, indexCount);
      uint32_t arrayObject =
          get32(image.bytes,
                indices + size_t{relationIndex} * RelationIndexLayout.size);
      ASSERT_EQ(get32(image.bytes, objects + size_t{arrayObject} * 96) >> 16,
                vpiGateArray);
      for (size_t relationIndex = 0; relationIndex != relationCount;
           ++relationIndex) {
        size_t relation = relations + relationIndex * RelationLayout.size;
        uint16_t source = get16(image.bytes, relation + 14);
        if (get32(image.bytes, relation) == memberObject &&
            (source >> 14) == memberTable &&
            (source & (uint16_t{1} << 13)) == 0 &&
            get32(image.bytes, relation + 4) ==
                (objectTableTag | arrayObject)) {
          put32(image.bytes, relation + 4, objectTableTag | switchArray);
          corrupted = true;
          break;
        }
      }
      if (corrupted)
        break;
    }
    ASSERT_TRUE(corrupted);
    image.seal();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&image.execution),
              OBELISK_RT_INVALID_DESIGN);
  }

  // Primitive input count and its derived scalar range are one checked unit.
  {
    MutableStaticArraysImage image;
    size_t objects = get64(image.bytes, 64);
    size_t objectCount = get64(image.bytes, 72);
    bool corrupted = false;
    for (size_t index = 0; index != objectCount; ++index) {
      size_t object = objects + index * 96;
      if (get32(image.bytes, object) >> 16 != vpiUdp ||
          get64(image.bytes, object + 56) == 0)
        continue;
      put64(image.bytes, object + 64, get64(image.bytes, object + 64) + 1);
      corrupted = true;
      break;
    }
    ASSERT_TRUE(corrupted);
    image.seal();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&image.execution),
              OBELISK_RT_INVALID_DESIGN);
  }
}

} // namespace
