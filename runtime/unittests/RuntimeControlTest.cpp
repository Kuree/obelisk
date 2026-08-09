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

TEST(RuntimeDeferredAssertion, LatestPerProcessReportMatures) {
  EXPECT_EQ(obelisk_rt_v1_deferred_enqueue(nullptr, 1), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(nullptr, 1), 0u);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->activeLogicalProcessToken = 11;
  EXPECT_EQ(obelisk_rt_v1_deferred_enqueue(context, 0), 0u);
  uint64_t first = obelisk_rt_v1_deferred_enqueue(context, 71);
  uint64_t latest = obelisk_rt_v1_deferred_enqueue(context, 71);
  uint64_t otherSite = obelisk_rt_v1_deferred_enqueue(context, 72);
  ASSERT_NE(first, 0u);
  ASSERT_NE(latest, 0u);
  ASSERT_NE(otherSite, 0u);
  EXPECT_NE(first, latest);

  context->activeLogicalProcessToken = 12;
  uint64_t otherProcess = obelisk_rt_v1_deferred_enqueue(context, 71);
  ASSERT_NE(otherProcess, 0u);

  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, first), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, latest), 1u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, latest), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, otherSite), 1u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, otherProcess), 1u);

  context->activeLogicalProcessToken = 11;
  uint64_t expired = obelisk_rt_v1_deferred_enqueue(context, 73);
  ASSERT_NE(expired, 0u);
  ++context->schedulerTime;
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, expired), 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeDeferredAssertion, FlushCancelsOnlyTheSelectedProcess) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  context->activeLogicalProcessToken = 21;
  uint64_t firstSite = obelisk_rt_v1_deferred_enqueue(context, 81);
  uint64_t secondSite = obelisk_rt_v1_deferred_enqueue(context, 82);
  context->activeLogicalProcessToken = 22;
  uint64_t otherProcess = obelisk_rt_v1_deferred_enqueue(context, 81);
  ASSERT_NE(firstSite, 0u);
  ASSERT_NE(secondSite, 0u);
  ASSERT_NE(otherProcess, 0u);

  obelisk_rt_flush_deferred_immediate_reports_unlocked(context, 21);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, firstSite), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, secondSite), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, otherProcess), 1u);
  EXPECT_TRUE(context->deferredImmediateReports.empty());
  EXPECT_TRUE(context->latestDeferredImmediateReports.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeDeferredAssertion, LabeledDisableCancelsOnlyThatAssertion) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  context->activeLogicalProcessToken = 31;
  uint64_t first = obelisk_rt_v1_deferred_enqueue_for_assertion(
      context, 91, 701);
  uint64_t unrelated = obelisk_rt_v1_deferred_enqueue_for_assertion(
      context, 92, 702);
  context->activeLogicalProcessToken = 32;
  uint64_t otherProcess = obelisk_rt_v1_deferred_enqueue_for_assertion(
      context, 91, 701);
  ASSERT_NE(first, 0u);
  ASSERT_NE(unrelated, 0u);
  ASSERT_NE(otherProcess, 0u);

  context->activeLogicalProcessToken = 31;
  EXPECT_EQ(obelisk_rt_v1_control_disable(context, 701, 0, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, first), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, otherProcess), 1u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, unrelated), 1u);

  context->activeLogicalProcessToken = 31;
  uint64_t hierarchicalFirst =
      obelisk_rt_v1_deferred_enqueue_for_assertion(context, 93, 703);
  context->activeLogicalProcessToken = 32;
  uint64_t hierarchicalSecond =
      obelisk_rt_v1_deferred_enqueue_for_assertion(context, 93, 703);
  context->activeLogicalProcessToken = 33;
  EXPECT_EQ(obelisk_rt_v1_control_disable(context, 703, 0, 1),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, hierarchicalFirst), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, hierarchicalSecond), 0u);
  EXPECT_TRUE(context->deferredImmediateAssertionReports.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeDeferredAssertion, AssertionControlOffKillAndOn) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  context->activeLogicalProcessToken = 41;

  uint64_t survivesOff =
      obelisk_rt_v1_deferred_enqueue_for_assertion(context, 101, 801);
  ASSERT_NE(survivesOff, 0u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 4, 801), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_enabled(context, 801), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, survivesOff), 1u);

  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 3, 801), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_enabled(context, 801), 1u);
  uint64_t killed =
      obelisk_rt_v1_deferred_enqueue_for_assertion(context, 102, 801);
  uint64_t unrelated =
      obelisk_rt_v1_deferred_enqueue_for_assertion(context, 103, 802);
  ASSERT_NE(killed, 0u);
  ASSERT_NE(unrelated, 0u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 5, 801), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_enabled(context, 801), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, killed), 0u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, unrelated), 1u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 12, 801),
            OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeDeferredAssertion, AssertionActionControlsAndLocking) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);

  EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 901), 7u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 7, 901), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 901), 4u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 10, 901), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 901), 5u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 11, 901), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 901), 5u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 6, 901), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 901), 7u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 9, 901), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 901), 3u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 8, 901), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 901), 7u);

  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 1, 901), OBELISK_RT_OK);
  context->activeLogicalProcessToken = 91;
  uint64_t lockedTicket =
      obelisk_rt_v1_deferred_enqueue_for_assertion(context, 111, 901);
  ASSERT_NE(lockedTicket, 0u);
  for (uint32_t action = 3; action <= 11; ++action) {
    EXPECT_EQ(obelisk_rt_v1_assertion_control(context, action, 901),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_assertion_enabled(context, 901), 1u);
    EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 901), 7u);
  }
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, lockedTicket), 1u);
  EXPECT_EQ(obelisk_rt_v1_deferred_mature(context, lockedTicket), 0u);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 2, 901), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 4, 901), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_enabled(context, 901), 0u);

  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 4, 902), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 7, 902), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 9, 902), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 1, 902), OBELISK_RT_OK);
  for (uint32_t action = 3; action <= 11; ++action) {
    EXPECT_EQ(obelisk_rt_v1_assertion_control(context, action, 902),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_assertion_enabled(context, 902), 0u);
    EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 902), 0u);
  }
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 2, 902), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 3, 902), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 6, 902), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_control(context, 8, 902), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_assertion_enabled(context, 902), 1u);
  EXPECT_EQ(obelisk_rt_v1_assertion_action_state(context, 902), 7u);

  obelisk_rt_v1_context_destroy(context);
}

} // namespace
