//===- RuntimeControlTest.cpp - Dynamic control and once-site tests -------===//

#include "../lib/RuntimeInternal.h"
#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <vector>

namespace {

TEST(RuntimeControl, NestedActivationsRequireStackOrderAndRetainMemberships) {
  uint64_t activation = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_control_enter(nullptr, 1, &activation),
            OBELISK_RT_INVALID_ARGUMENT);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_control_enter(context, 1, &activation),
            OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(activation, 0u);

  context->activeLogicalProcessToken = 7;
  uint64_t outer = 0;
  uint64_t inner = 0;
  ASSERT_EQ(obelisk_rt_v1_control_enter(context, 11, &outer), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_control_enter(context, 12, &inner), OBELISK_RT_OK);
  ASSERT_NE(outer, inner);
  EXPECT_EQ(context->activeControls, (std::vector<uint64_t>{outer, inner}));
  EXPECT_EQ(context->controlActivations.at(outer).target, 11u);
  EXPECT_EQ(context->controlActivations.at(inner).target, 12u);

  EXPECT_EQ(obelisk_rt_v1_control_leave(context, outer),
            OBELISK_RT_INVALID_LIFECYCLE);
  obelisk_rt_retain_controls_unlocked(context, context->activeControls);
  EXPECT_EQ(context->controlActivations.at(outer).memberships, 2u);
  EXPECT_EQ(context->controlActivations.at(inner).memberships, 2u);

  ASSERT_EQ(obelisk_rt_v1_control_leave(context, inner), OBELISK_RT_OK);
  EXPECT_EQ(context->controlActivations.at(inner).memberships, 1u);
  ASSERT_EQ(obelisk_rt_v1_control_leave(context, outer), OBELISK_RT_OK);
  EXPECT_EQ(context->controlActivations.at(outer).memberships, 1u);
  EXPECT_TRUE(context->activeControls.empty());

  obelisk_rt_release_controls_unlocked(context, {outer, inner});
  EXPECT_TRUE(context->controlActivations.empty());
  EXPECT_EQ(obelisk_rt_v1_control_leave(context, outer),
            OBELISK_RT_INVALID_LIFECYCLE);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeOnce, StaticAndDeferredClaimsUseTheirDocumentedScope) {
  EXPECT_EQ(obelisk_rt_v1_static_once(nullptr, 1), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_once(nullptr, 1), 0u);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_static_once(context, 0), 0u);
  EXPECT_EQ(obelisk_rt_v1_static_once(context, 41), 1u);
  EXPECT_EQ(obelisk_rt_v1_static_once(context, 41), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_once(context, 51), 0u);

  context->activeLogicalProcessToken = 1;
  EXPECT_EQ(obelisk_rt_v1_deferred_once(context, 0), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_once(context, 51), 1u);
  EXPECT_EQ(obelisk_rt_v1_deferred_once(context, 51), 0u);

  context->activeLogicalProcessToken = 2;
  EXPECT_EQ(obelisk_rt_v1_deferred_once(context, 51), 1u);
  context->activeLogicalProcessToken = 1;
  EXPECT_EQ(obelisk_rt_v1_deferred_once(context, 51), 0u);

  ++context->schedulerTime;
  EXPECT_EQ(obelisk_rt_v1_deferred_once(context, 51), 1u);
  EXPECT_EQ(obelisk_rt_v1_static_once(context, 41), 0u);
  obelisk_rt_v1_context_destroy(context);
}

} // namespace
