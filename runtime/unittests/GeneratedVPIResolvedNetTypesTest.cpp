//===- GeneratedVPIResolvedNetTypesTest.cpp - resolved-net VPI tests -----===//

#include "../lib/RuntimeInternal.h"

#include "talos/Runtime/Runtime.h"

#include "GeneratedSymbol.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"

#include <array>
#include <cstdint>

extern "C" const talos_rt_process_descriptor_v1 initialDescriptor asm(
    TALOS_TEST_SYMBOL("initial.__talos_process_descriptor"));

namespace {

class GeneratedResolvedNetContext {
public:
  GeneratedResolvedNetContext() {
    EXPECT_NE(initialDescriptor.execution, nullptr);
    if (!initialDescriptor.execution)
      return;
    EXPECT_EQ(talos_rt_v1_design_validate(initialDescriptor.execution),
              TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_context_create_for_design(
                  initialDescriptor.execution, &context),
              TALOS_RT_OK);
    if (!context)
      return;
    EXPECT_EQ(talos_rt_v1_vpi_startup(context, nullptr, 0), TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_vpi_end_compile(context), TALOS_RT_OK);
  }

  ~GeneratedResolvedNetContext() {
    if (context)
      talos_rt_v1_context_destroy(context);
  }

  vpiHandle byName(const char *name) {
    return vpi_handle_by_name(const_cast<char *>(name), nullptr);
  }

  talos_rt_context *context = nullptr;
};

void expectResolved(vpiHandle handle, PLI_INT32 type, const char *name) {
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(vpi_get(vpiResolvedNetType, handle), type);
  EXPECT_STREQ(vpi_get_str(vpiResolvedNetType, handle), name);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
}

void expectUnavailable(vpiHandle handle) {
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(vpi_get(vpiResolvedNetType, handle), vpiUndefined);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(vpi_get_str(vpiResolvedNetType, handle), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
}

TEST(GeneratedVPIResolvedNetTypes,
     PreservesExactDominatingSubtypeInBothConnectionDirections) {
  GeneratedResolvedNetContext fixture;
  ASSERT_NE(fixture.context, nullptr);
  constexpr std::array<PLI_INT32, 4> types{vpiWand, vpiTriAnd, vpiWor,
                                           vpiTriOr};
  constexpr std::array<const char *, 4> names{"vpiWand", "vpiTriAnd",
                                               "vpiWor", "vpiTriOr"};
  for (const char *netName : {"top.sink", "top.sink_reverse"}) {
    vpiHandle net = fixture.byName(netName);
    ASSERT_NE(net, nullptr);
    expectUnavailable(net);
    for (PLI_INT32 index = 0; index != 4; ++index) {
      vpiHandle bit = vpi_handle_by_index(net, index);
      ASSERT_NE(bit, nullptr) << netName << '[' << index << ']';
      expectResolved(bit, types[index], names[index]);
      EXPECT_EQ(vpi_release_handle(bit), 1);
    }
    EXPECT_EQ(vpi_release_handle(net), 1);
  }
}

TEST(GeneratedVPIResolvedNetTypes,
     ResolvesUnanimousCollapseAndCoalescesAdjacentBits) {
  GeneratedResolvedNetContext fixture;
  ASSERT_NE(fixture.context, nullptr);
  for (const char *name : {"top.unanimous", "top.coalesced"}) {
    vpiHandle net = fixture.byName(name);
    expectResolved(net, vpiWand, "vpiWand");
    if (vpi_get(vpiSize, net) == 2) {
      for (PLI_INT32 index = 0; index != 2; ++index) {
        vpiHandle bit = vpi_handle_by_index(net, index);
        expectResolved(bit, vpiWand, "vpiWand");
        EXPECT_EQ(vpi_release_handle(bit), 1);
      }
    }
    EXPECT_EQ(vpi_release_handle(net), 1);
  }
}

TEST(GeneratedVPIResolvedNetTypes,
     KeepsMixedAndCyclicDominanceUnavailableButInheritsInterconnect) {
  GeneratedResolvedNetContext fixture;
  ASSERT_NE(fixture.context, nullptr);
  vpiHandle interconnect = fixture.byName("top.interconnect");
  expectResolved(interconnect, vpiWand, "vpiWand");
  EXPECT_EQ(vpi_release_handle(interconnect), 1);

  for (const char *name : {"top.mixed", "top.cycle_a", "top.cycle_b",
                           "top.cycle_c"}) {
    vpiHandle net = fixture.byName(name);
    expectUnavailable(net);
    EXPECT_EQ(vpi_release_handle(net), 1);
  }
  EXPECT_FALSE(fixture.context->vpiObservationDemand);
  EXPECT_FALSE(fixture.context->nativeScheduleDeoptimized);
}

} // namespace
