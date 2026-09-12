//===- GeneratedVPINettypesTest.cpp - nettype VPI query tests ------------===//

#include "../lib/RuntimeInternal.h"

#include "obelisk/Runtime/Runtime.h"

#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"

#include <array>

extern "C" const obelisk_rt_process_descriptor_v1
    initialDescriptor asm("initial.__obelisk_process_descriptor");

namespace {

class GeneratedNettypeContext {
public:
  GeneratedNettypeContext() {
    EXPECT_NE(initialDescriptor.execution, nullptr);
    if (!initialDescriptor.execution)
      return;
    EXPECT_EQ(obelisk_rt_v1_design_validate(initialDescriptor.execution),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_context_create_for_design(
                  initialDescriptor.execution, &context),
              OBELISK_RT_OK);
    if (!context)
      return;
    EXPECT_EQ(obelisk_rt_v1_vpi_startup(context, nullptr, 0), OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_vpi_end_compile(context), OBELISK_RT_OK);
  }

  ~GeneratedNettypeContext() {
    if (context)
      obelisk_rt_v1_context_destroy(context);
  }

  vpiHandle byName(const char *name) {
    return vpi_handle_by_name(const_cast<char *>(name), nullptr);
  }

  obelisk_rt_context *context = nullptr;
};

TEST(GeneratedVPINettypes, TraversesDeclarationsAliasesTypesAndResolvers) {
  GeneratedNettypeContext fixture;
  ASSERT_NE(fixture.context, nullptr);

  vpiHandle module = fixture.byName("top.d");
  ASSERT_NE(module, nullptr);
  vpiHandle iterator = vpi_iterate(vpiNetTypedef, module);
  ASSERT_NE(iterator, nullptr);
  vpiHandle base = vpi_scan(iterator);
  vpiHandle alias = vpi_scan(iterator);
  ASSERT_NE(base, nullptr);
  ASSERT_NE(alias, nullptr);
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(vpi_get(vpiType, base), vpiNettypeDecl);
  EXPECT_EQ(vpi_get(vpiType, alias), vpiNettypeDecl);
  EXPECT_STREQ(vpi_get_str(vpiName, base), "base_nt");
  EXPECT_STREQ(vpi_get_str(vpiName, alias), "alias_nt");

  EXPECT_EQ(vpi_handle(vpiNetTypedefAlias, base), nullptr);
  vpiHandle directAlias = vpi_handle(vpiNetTypedefAlias, alias);
  ASSERT_NE(directAlias, nullptr);
  EXPECT_EQ(vpi_compare_objects(directAlias, base), 1);

  for (vpiHandle nettype : {base, alias}) {
    vpiHandle typespec = vpi_handle(vpiTypespec, nettype);
    ASSERT_NE(typespec, nullptr);
    EXPECT_EQ(vpi_get(vpiType, typespec), vpiLogicTypespec);
    EXPECT_EQ(vpi_release_handle(typespec), 1);

    vpiHandle resolver = vpi_handle(vpiWith, nettype);
    ASSERT_NE(resolver, nullptr);
    EXPECT_EQ(vpi_get(vpiType, resolver), vpiFunction);
    EXPECT_STREQ(vpi_get_str(vpiName, resolver), "resolve");
    EXPECT_EQ(vpi_release_handle(resolver), 1);
  }

  vpiHandle net = fixture.byName("top.d.n");
  ASSERT_NE(net, nullptr);
  EXPECT_EQ(vpi_get(vpiNetType, net), vpiNettypeNet);
  EXPECT_STREQ(vpi_get_str(vpiNetType, net), "vpiNettypeNet");
  vpiHandle netBit = vpi_handle_by_index(net, 0);
  ASSERT_NE(netBit, nullptr);
  EXPECT_EQ(vpi_get(vpiNetType, netBit), vpiNettypeNetSelect);
  EXPECT_STREQ(vpi_get_str(vpiNetType, netBit), "vpiNettypeNetSelect");
  vpiHandle declaredNettype = vpi_handle(vpiTypespec, net);
  ASSERT_NE(declaredNettype, nullptr);
  EXPECT_EQ(vpi_compare_objects(declaredNettype, alias), 1);

  vpiHandle collapsedNet = fixture.byName("top.d.alias_n");
  ASSERT_NE(collapsedNet, nullptr);
  EXPECT_EQ(vpi_get(vpiType, collapsedNet), vpiNet);
  EXPECT_EQ(vpi_get(vpiNetType, collapsedNet), vpiNettypeNet);
  vpiHandle collapsedNettype = vpi_handle(vpiTypespec, collapsedNet);
  ASSERT_NE(collapsedNettype, nullptr);
  EXPECT_EQ(vpi_compare_objects(collapsedNettype, alias), 1);
  vpiHandle simulatedNet = vpi_handle(vpiSimNet, collapsedNet);
  ASSERT_NE(simulatedNet, nullptr);
  EXPECT_EQ(vpi_compare_objects(simulatedNet, net), 1);
  vpiHandle collapsedBit = vpi_handle_by_index(collapsedNet, 1);
  ASSERT_NE(collapsedBit, nullptr);
  EXPECT_EQ(vpi_get(vpiNetType, collapsedBit), vpiNettypeNetSelect);

  vpiHandle arrayNet = fixture.byName("top.d.array_n");
  ASSERT_NE(arrayNet, nullptr);
  vpiHandle arrayNettype = vpi_handle(vpiTypespec, arrayNet);
  ASSERT_NE(arrayNettype, nullptr);
  EXPECT_EQ(vpi_compare_objects(arrayNettype, alias), 1);
  vpiHandle arrayElement = vpi_handle_by_index(arrayNet, 3);
  ASSERT_NE(arrayElement, nullptr);
  EXPECT_EQ(vpi_get(vpiNetType, arrayElement), vpiNettypeNetSelect);

  vpiHandle collapsedArray = fixture.byName("top.d.alias_array_n");
  ASSERT_NE(collapsedArray, nullptr);
  vpiHandle collapsedArrayNettype = vpi_handle(vpiTypespec, collapsedArray);
  ASSERT_NE(collapsedArrayNettype, nullptr);
  EXPECT_EQ(vpi_compare_objects(collapsedArrayNettype, alias), 1);
  vpiHandle collapsedArrayElement = vpi_handle_by_index(collapsedArray, 0);
  ASSERT_NE(collapsedArrayElement, nullptr);
  EXPECT_EQ(vpi_get(vpiNetType, collapsedArrayElement), vpiNettypeNetSelect);

  EXPECT_EQ(vpi_release_handle(collapsedArrayElement), 1);
  EXPECT_EQ(vpi_release_handle(collapsedArrayNettype), 1);
  EXPECT_EQ(vpi_release_handle(collapsedArray), 1);
  EXPECT_EQ(vpi_release_handle(arrayElement), 1);
  EXPECT_EQ(vpi_release_handle(arrayNettype), 1);
  EXPECT_EQ(vpi_release_handle(arrayNet), 1);
  EXPECT_EQ(vpi_release_handle(collapsedBit), 1);
  EXPECT_EQ(vpi_release_handle(simulatedNet), 1);
  EXPECT_EQ(vpi_release_handle(collapsedNettype), 1);
  EXPECT_EQ(vpi_release_handle(collapsedNet), 1);
  EXPECT_EQ(vpi_release_handle(declaredNettype), 1);
  EXPECT_EQ(vpi_release_handle(netBit), 1);
  EXPECT_EQ(vpi_release_handle(net), 1);
  EXPECT_EQ(vpi_release_handle(directAlias), 1);
  EXPECT_EQ(vpi_release_handle(alias), 1);
  EXPECT_EQ(vpi_release_handle(base), 1);
  EXPECT_EQ(vpi_release_handle(module), 1);
  EXPECT_FALSE(fixture.context->vpiObservationDemand);
  EXPECT_FALSE(fixture.context->nativeScheduleDeoptimized);
}

} // namespace
