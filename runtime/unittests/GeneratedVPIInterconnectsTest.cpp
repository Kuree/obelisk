//===- GeneratedVPIInterconnectsTest.cpp - interconnect VPI tests --------===//

#include "../lib/RuntimeInternal.h"

#include "obelisk/Runtime/Runtime.h"

#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"

#include <array>
#include <string>
#include <vector>

extern "C" const obelisk_rt_process_descriptor_v1
    interconnectDescriptor asm("initial.__obelisk_process_descriptor");

namespace {

class GeneratedInterconnectContext {
public:
  GeneratedInterconnectContext() {
    EXPECT_NE(interconnectDescriptor.execution, nullptr);
    if (!interconnectDescriptor.execution)
      return;
    EXPECT_EQ(obelisk_rt_v1_design_validate(interconnectDescriptor.execution),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_context_create_for_design(
                  interconnectDescriptor.execution, &context),
              OBELISK_RT_OK);
    if (!context)
      return;
    EXPECT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  }

  ~GeneratedInterconnectContext() {
    if (context)
      obelisk_rt_v1_context_destroy(context);
  }

  vpiHandle byName(const char *name) {
    return vpi_handle_by_name(const_cast<char *>(name), nullptr);
  }

  obelisk_rt_context *context = nullptr;
};

std::vector<std::string> scanNames(vpiHandle iterator,
                                   PLI_INT32 expectedType) {
  std::vector<std::string> names;
  for (vpiHandle item = vpi_scan(iterator); item; item = vpi_scan(iterator)) {
    EXPECT_EQ(vpi_get(vpiType, item), expectedType);
    const char *name = vpi_get_str(vpiFullName, item);
    EXPECT_NE(name, nullptr);
    if (name)
      names.emplace_back(name);
    EXPECT_EQ(vpi_release_handle(item), 1);
  }
  return names;
}

int64_t integerValue(vpiHandle handle) {
  s_vpi_value value{};
  value.format = vpiIntVal;
  vpi_get_value(handle, &value);
  return value.value.integer;
}

TEST(GeneratedVPIInterconnects,
     TraversesOneDimensionAtATimeAndKeepsTypedLeaves) {
  GeneratedInterconnectContext fixture;
  ASSERT_NE(fixture.context, nullptr);

  vpiHandle module = fixture.byName("top.d");
  vpiHandle bus = fixture.byName("top.d.bus");
  ASSERT_NE(module, nullptr);
  ASSERT_NE(bus, nullptr);
  EXPECT_EQ(vpi_get(vpiType, bus), vpiInterconnectArray);
  EXPECT_EQ(vpi_get(vpiNetType, bus), vpiInterconnect);
  EXPECT_EQ(vpi_get(vpiSize, bus), 4);
  EXPECT_EQ(vpi_get(vpiPacked, bus), 0);
  EXPECT_EQ(vpi_handle(vpiTypespec, bus), nullptr);

  vpiHandle moduleNets = vpi_iterate(vpiNet, module);
  ASSERT_NE(moduleNets, nullptr);
  vpiHandle moduleBus = vpi_scan(moduleNets);
  vpiHandle moduleScalar = vpi_scan(moduleNets);
  ASSERT_NE(moduleBus, nullptr);
  ASSERT_NE(moduleScalar, nullptr);
  EXPECT_EQ(vpi_scan(moduleNets), nullptr);
  EXPECT_EQ(vpi_get(vpiType, moduleBus), vpiInterconnectArray);
  EXPECT_EQ(vpi_get(vpiType, moduleScalar), vpiInterconnectNet);
  EXPECT_STREQ(vpi_get_str(vpiFullName, moduleBus), "top.d.bus");
  EXPECT_STREQ(vpi_get_str(vpiFullName, moduleScalar), "top.d.scalar");

  vpiHandle scalar = fixture.byName("top.d.scalar");
  ASSERT_NE(scalar, nullptr);
  EXPECT_EQ(vpi_compare_objects(scalar, moduleScalar), 1);
  EXPECT_EQ(vpi_get(vpiType, scalar), vpiInterconnectNet);
  EXPECT_EQ(vpi_get(vpiNetType, scalar), vpiInterconnect);
  vpiHandle scalarTypespec = vpi_handle(vpiTypespec, scalar);
  ASSERT_NE(scalarTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, scalarTypespec), vpiLogicTypespec);
  EXPECT_EQ(vpi_handle(vpiParent, scalar), nullptr);

  vpiHandle rows = vpi_iterate(vpiElement, bus);
  ASSERT_NE(rows, nullptr);
  EXPECT_EQ(scanNames(rows, vpiInterconnectArray),
            (std::vector<std::string>{"top.d.bus[1]", "top.d.bus[0]"}));

  vpiHandle row = vpi_handle_by_index(bus, 1);
  vpiHandle namedRow = fixture.byName("top.d.bus[1]");
  ASSERT_NE(row, nullptr);
  ASSERT_NE(namedRow, nullptr);
  EXPECT_EQ(vpi_compare_objects(row, namedRow), 1);
  EXPECT_EQ(vpi_get(vpiType, row), vpiInterconnectArray);
  EXPECT_EQ(vpi_get(vpiNetType, row), vpiInterconnect);
  EXPECT_EQ(vpi_get(vpiSize, row), 2);
  EXPECT_EQ(vpi_get(vpiPacked, row), 1);
  EXPECT_EQ(vpi_handle(vpiTypespec, row), nullptr);

  vpiHandle leaves = vpi_iterate(vpiElement, row);
  ASSERT_NE(leaves, nullptr);
  EXPECT_EQ(scanNames(leaves, vpiInterconnectNet),
            (std::vector<std::string>{"top.d.bus[1][-1]",
                                      "top.d.bus[1][0]"}));

  PLI_INT32 indices[] = {1, -1};
  vpiHandle leaf = vpi_handle_by_index(row, -1);
  vpiHandle multi = vpi_handle_by_multi_index(bus, 2, indices);
  vpiHandle namedLeaf = fixture.byName("top.d.bus[1][-1]");
  ASSERT_NE(leaf, nullptr);
  ASSERT_NE(multi, nullptr);
  ASSERT_NE(namedLeaf, nullptr);
  EXPECT_EQ(vpi_compare_objects(leaf, multi), 1);
  EXPECT_EQ(vpi_compare_objects(leaf, namedLeaf), 1);
  EXPECT_EQ(vpi_get(vpiType, leaf), vpiInterconnectNet);
  EXPECT_EQ(vpi_get(vpiNetType, leaf), vpiInterconnect);
  vpiHandle leafTypespec = vpi_handle(vpiTypespec, leaf);
  ASSERT_NE(leafTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, leafTypespec), vpiLogicTypespec);
  vpiHandle simulated = vpi_handle(vpiSimNet, leaf);
  ASSERT_NE(simulated, nullptr);
  EXPECT_EQ(vpi_compare_objects(simulated, leaf), 1);
  vpiHandle parent = vpi_handle(vpiParent, leaf);
  ASSERT_NE(parent, nullptr);
  EXPECT_EQ(vpi_compare_objects(parent, row), 1);
  vpiHandle indicesIterator = vpi_iterate(vpiIndex, leaf);
  ASSERT_NE(indicesIterator, nullptr);
  vpiHandle innerIndex = vpi_scan(indicesIterator);
  vpiHandle outerIndex = vpi_scan(indicesIterator);
  ASSERT_NE(innerIndex, nullptr);
  ASSERT_NE(outerIndex, nullptr);
  EXPECT_EQ(integerValue(innerIndex), -1);
  EXPECT_EQ(integerValue(outerIndex), 1);
  EXPECT_EQ(vpi_scan(indicesIterator), nullptr);

  vpiHandle realLeaf = fixture.byName("top.d.bus[1][0]");
  ASSERT_NE(realLeaf, nullptr);
  vpiHandle realTypespec = vpi_handle(vpiTypespec, realLeaf);
  ASSERT_NE(realTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, realTypespec), vpiRealTypespec);

  vpiHandle vectorLeaf = fixture.byName("top.d.bus[1][-1]");
  ASSERT_NE(vectorLeaf, nullptr);
  vpiHandle bits = vpi_iterate(vpiElement, vectorLeaf);
  ASSERT_NE(bits, nullptr);
  vpiHandle bit1 = vpi_scan(bits);
  vpiHandle bit0 = vpi_scan(bits);
  ASSERT_NE(bit1, nullptr);
  ASSERT_NE(bit0, nullptr);
  EXPECT_EQ(vpi_scan(bits), nullptr);
  EXPECT_EQ(vpi_get(vpiType, bit1), vpiNetBit);
  EXPECT_EQ(vpi_get(vpiType, bit0), vpiNetBit);
  EXPECT_STREQ(vpi_get_str(vpiName, bit1), "bus[1][-1][1]");
  EXPECT_STREQ(vpi_get_str(vpiFullName, bit0), "top.d.bus[1][-1][0]");
  vpiHandle vectorParent = vpi_handle(vpiParent, bit1);
  ASSERT_NE(vectorParent, nullptr);
  EXPECT_EQ(vpi_compare_objects(vectorParent, vectorLeaf), 1);
  vpiHandle vectorIndex = vpi_handle(vpiIndex, bit0);
  ASSERT_NE(vectorIndex, nullptr);
  EXPECT_EQ(integerValue(vectorIndex), 0);

  vpiHandle structLeaf = fixture.byName("top.d.bus[0][0]");
  ASSERT_NE(structLeaf, nullptr);
  vpiHandle members = vpi_iterate(vpiMember, structLeaf);
  ASSERT_NE(members, nullptr);
  vpiHandle memberA = vpi_scan(members);
  vpiHandle memberB = vpi_scan(members);
  ASSERT_NE(memberA, nullptr);
  ASSERT_NE(memberB, nullptr);
  EXPECT_EQ(vpi_scan(members), nullptr);
  EXPECT_EQ(vpi_get(vpiType, memberA), vpiNet);
  EXPECT_EQ(vpi_get(vpiType, memberB), vpiNet);
  EXPECT_EQ(vpi_get(vpiStructUnionMember, memberA), 1);
  EXPECT_EQ(vpi_get(vpiStructUnionMember, memberB), 1);
  EXPECT_STREQ(vpi_get_str(vpiName, memberA), "a");
  EXPECT_STREQ(vpi_get_str(vpiFullName, memberA),
               "top.d.bus[0][0].a");
  EXPECT_STREQ(vpi_get_str(vpiDecompile, memberA), "bus[0][0].a");
  EXPECT_STREQ(vpi_get_str(vpiName, memberB), "b");
  vpiHandle namedMember = fixture.byName("top.d.bus[0][0].a");
  ASSERT_NE(namedMember, nullptr);
  EXPECT_EQ(vpi_compare_objects(namedMember, memberA), 1);
  vpiHandle memberParent = vpi_handle(vpiParent, memberA);
  ASSERT_NE(memberParent, nullptr);
  EXPECT_EQ(vpi_compare_objects(memberParent, structLeaf), 1);
  vpiHandle memberTypespec = vpi_handle(vpiTypespec, memberA);
  ASSERT_NE(memberTypespec, nullptr);
  EXPECT_EQ(vpi_get(vpiType, memberTypespec), vpiLogicTypespec);
  EXPECT_EQ(vpi_get(vpiSize, memberA), 3);
  EXPECT_EQ(vpi_handle(vpiIndex, memberA), nullptr);
  EXPECT_EQ(vpi_iterate(vpiIndex, memberA), nullptr);
  vpiHandle memberBits = vpi_iterate(vpiBit, memberA);
  ASSERT_NE(memberBits, nullptr);
  vpiHandle memberBit2 = vpi_scan(memberBits);
  vpiHandle memberBit1 = vpi_scan(memberBits);
  vpiHandle memberBit0 = vpi_scan(memberBits);
  ASSERT_NE(memberBit2, nullptr);
  ASSERT_NE(memberBit1, nullptr);
  ASSERT_NE(memberBit0, nullptr);
  EXPECT_EQ(vpi_scan(memberBits), nullptr);
  EXPECT_EQ(vpi_get(vpiType, memberBit2), vpiNetBit);
  EXPECT_STREQ(vpi_get_str(vpiFullName, memberBit2),
               "top.d.bus[0][0].a[2]");
  vpiHandle memberBitParent = vpi_handle(vpiParent, memberBit2);
  ASSERT_NE(memberBitParent, nullptr);
  EXPECT_EQ(vpi_compare_objects(memberBitParent, memberA), 1);
  vpiHandle memberBitIndex = vpi_handle(vpiIndex, memberBit0);
  ASSERT_NE(memberBitIndex, nullptr);
  EXPECT_EQ(integerValue(memberBitIndex), 0);
  EXPECT_EQ(vpi_iterate(vpiMember, vectorLeaf), nullptr);
  EXPECT_EQ(vpi_iterate(vpiElement, structLeaf), nullptr);

  EXPECT_EQ(vpi_handle_by_index(bus, 2), nullptr);
  PLI_INT32 tooMany[] = {1, -1, 0};
  EXPECT_EQ(vpi_handle_by_multi_index(bus, 3, tooMany), nullptr);

  EXPECT_EQ(vpi_release_handle(memberBitIndex), 1);
  EXPECT_EQ(vpi_release_handle(memberBitParent), 1);
  EXPECT_EQ(vpi_release_handle(memberBit0), 1);
  EXPECT_EQ(vpi_release_handle(memberBit1), 1);
  EXPECT_EQ(vpi_release_handle(memberBit2), 1);
  EXPECT_EQ(vpi_release_handle(memberTypespec), 1);
  EXPECT_EQ(vpi_release_handle(memberParent), 1);
  EXPECT_EQ(vpi_release_handle(namedMember), 1);
  EXPECT_EQ(vpi_release_handle(memberB), 1);
  EXPECT_EQ(vpi_release_handle(memberA), 1);
  EXPECT_EQ(vpi_release_handle(structLeaf), 1);
  EXPECT_EQ(vpi_release_handle(vectorIndex), 1);
  EXPECT_EQ(vpi_release_handle(vectorParent), 1);
  EXPECT_EQ(vpi_release_handle(bit0), 1);
  EXPECT_EQ(vpi_release_handle(bit1), 1);
  EXPECT_EQ(vpi_release_handle(vectorLeaf), 1);
  EXPECT_EQ(vpi_release_handle(scalarTypespec), 1);
  EXPECT_EQ(vpi_release_handle(scalar), 1);
  EXPECT_EQ(vpi_release_handle(moduleScalar), 1);
  EXPECT_EQ(vpi_release_handle(moduleBus), 1);
  EXPECT_EQ(vpi_release_handle(realTypespec), 1);
  EXPECT_EQ(vpi_release_handle(realLeaf), 1);
  EXPECT_EQ(vpi_release_handle(outerIndex), 1);
  EXPECT_EQ(vpi_release_handle(innerIndex), 1);
  EXPECT_EQ(vpi_release_handle(parent), 1);
  EXPECT_EQ(vpi_release_handle(simulated), 1);
  EXPECT_EQ(vpi_release_handle(leafTypespec), 1);
  EXPECT_EQ(vpi_release_handle(namedLeaf), 1);
  EXPECT_EQ(vpi_release_handle(multi), 1);
  EXPECT_EQ(vpi_release_handle(leaf), 1);
  EXPECT_EQ(vpi_release_handle(namedRow), 1);
  EXPECT_EQ(vpi_release_handle(row), 1);
  EXPECT_EQ(vpi_release_handle(bus), 1);
  EXPECT_EQ(vpi_release_handle(module), 1);
  EXPECT_FALSE(fixture.context->vpiObservationDemand);
  EXPECT_FALSE(fixture.context->nativeScheduleDeoptimized);
}

} // namespace
