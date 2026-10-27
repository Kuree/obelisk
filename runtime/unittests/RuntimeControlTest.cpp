//===- RuntimeControlTest.cpp - Dynamic control and once-site tests -------===//

#include "../lib/RuntimeInternal.h"
#include "talos/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <vector>

namespace {

TEST(RuntimeControl, NestedActivationsRequireStackOrderAndRetainMemberships) {
  uint64_t activation = UINT64_MAX;
  EXPECT_EQ(talos_rt_v1_control_enter(nullptr, 1, &activation),
            TALOS_RT_INVALID_ARGUMENT);

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_control_enter(context, 1, &activation),
            TALOS_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(activation, 0u);

  context->activeLogicalProcessToken = 7;
  uint64_t outer = 0;
  uint64_t inner = 0;
  ASSERT_EQ(talos_rt_v1_control_enter(context, 11, &outer), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_control_enter(context, 12, &inner), TALOS_RT_OK);
  ASSERT_NE(outer, inner);
  EXPECT_EQ(context->activeControls, (std::vector<uint64_t>{outer, inner}));
  EXPECT_EQ(context->controlActivations.at(outer).target, 11u);
  EXPECT_EQ(context->controlActivations.at(inner).target, 12u);

  EXPECT_EQ(talos_rt_v1_control_leave(context, outer),
            TALOS_RT_INVALID_LIFECYCLE);
  talos_rt_retain_controls_unlocked(context, context->activeControls);
  EXPECT_EQ(context->controlActivations.at(outer).memberships, 2u);
  EXPECT_EQ(context->controlActivations.at(inner).memberships, 2u);

  ASSERT_EQ(talos_rt_v1_control_leave(context, inner), TALOS_RT_OK);
  EXPECT_EQ(context->controlActivations.at(inner).memberships, 1u);
  ASSERT_EQ(talos_rt_v1_control_leave(context, outer), TALOS_RT_OK);
  EXPECT_EQ(context->controlActivations.at(outer).memberships, 1u);
  EXPECT_TRUE(context->activeControls.empty());

  talos_rt_release_controls_unlocked(context, {outer, inner});
  EXPECT_TRUE(context->controlActivations.empty());
  EXPECT_EQ(talos_rt_v1_control_leave(context, outer),
            TALOS_RT_INVALID_LIFECYCLE);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeControl, BoundaryRequiresTheCurrentTopOwnedActivation) {
  EXPECT_EQ(talos_rt_v1_control_escape_pending(nullptr), 0u);
  EXPECT_EQ(talos_rt_v1_control_boundary(nullptr, 1, 1),
            TALOS_RT_INVALID_ARGUMENT);

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->activeLogicalProcessToken = 7;
  context->controlEscapePending = true;
  EXPECT_EQ(talos_rt_v1_control_escape_pending(context), 1u);
  EXPECT_EQ(talos_rt_v1_control_escape_pending(context), 0u);
  uint64_t outer = 0;
  uint64_t inner = 0;
  ASSERT_EQ(talos_rt_v1_control_enter(context, 11, &outer), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_control_enter(context, 12, &inner), TALOS_RT_OK);

  EXPECT_EQ(talos_rt_v1_control_boundary(context, inner, 0),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_control_boundary(context, outer, 3),
            TALOS_RT_INVALID_LIFECYCLE);
  context->activeLogicalProcessToken = 8;
  EXPECT_EQ(talos_rt_v1_control_boundary(context, inner, 3),
            TALOS_RT_INVALID_LIFECYCLE);
  context->activeLogicalProcessToken = 7;
  ASSERT_EQ(talos_rt_v1_control_boundary(context, inner, 3), TALOS_RT_OK);
  EXPECT_EQ(context->controlActivations.at(inner).owner, 7u);
  EXPECT_EQ(context->controlActivations.at(inner).continuation, 3u);
  EXPECT_EQ(talos_rt_v1_control_boundary(context, inner, 4),
            TALOS_RT_INVALID_LIFECYCLE);

  ASSERT_EQ(talos_rt_v1_control_leave(context, inner), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_control_boundary(context, outer, 5), TALOS_RT_OK);
  EXPECT_EQ(context->controlActivations.at(outer).continuation, 5u);
  ASSERT_EQ(talos_rt_v1_control_leave(context, outer), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeOnce, StaticAndDeferredClaimsUseTheirDocumentedScope) {
  EXPECT_EQ(talos_rt_v1_static_once(nullptr, 1), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_once(nullptr, 1), 0u);

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_static_once(context, 0), 0u);
  EXPECT_EQ(talos_rt_v1_static_once(context, 41), 1u);
  EXPECT_EQ(talos_rt_v1_static_once(context, 41), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_once(context, 51), 0u);

  context->activeLogicalProcessToken = 1;
  EXPECT_EQ(talos_rt_v1_deferred_once(context, 0), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_once(context, 51), 1u);
  EXPECT_EQ(talos_rt_v1_deferred_once(context, 51), 0u);

  context->activeLogicalProcessToken = 2;
  EXPECT_EQ(talos_rt_v1_deferred_once(context, 51), 1u);
  context->activeLogicalProcessToken = 1;
  EXPECT_EQ(talos_rt_v1_deferred_once(context, 51), 0u);

  ++context->schedulerTime;
  EXPECT_EQ(talos_rt_v1_deferred_once(context, 51), 1u);
  EXPECT_EQ(talos_rt_v1_static_once(context, 41), 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeDeferredAssertion, EveryPendingProcessReportMatures) {
  EXPECT_EQ(talos_rt_v1_deferred_enqueue(nullptr, 1), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(nullptr, 1), 0u);

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->activeLogicalProcessToken = 11;
  EXPECT_EQ(talos_rt_v1_deferred_enqueue(context, 0), 0u);
  uint64_t first = talos_rt_v1_deferred_enqueue(context, 71);
  uint64_t second = talos_rt_v1_deferred_enqueue(context, 71);
  uint64_t otherSite = talos_rt_v1_deferred_enqueue(context, 72);
  ASSERT_NE(first, 0u);
  ASSERT_NE(second, 0u);
  ASSERT_NE(otherSite, 0u);
  EXPECT_NE(first, second);

  context->activeLogicalProcessToken = 12;
  uint64_t otherProcess = talos_rt_v1_deferred_enqueue(context, 71);
  ASSERT_NE(otherProcess, 0u);

  EXPECT_EQ(talos_rt_v1_deferred_mature(context, first), 1u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, second), 1u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, second), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, otherSite), 1u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, otherProcess), 1u);

  context->activeLogicalProcessToken = 11;
  uint64_t expired = talos_rt_v1_deferred_enqueue(context, 73);
  ASSERT_NE(expired, 0u);
  ++context->schedulerTime;
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, expired), 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeDeferredAssertion, FlushCancelsOnlyTheSelectedProcess) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  context->activeLogicalProcessToken = 21;
  uint64_t firstSite = talos_rt_v1_deferred_enqueue(context, 81);
  uint64_t secondSite = talos_rt_v1_deferred_enqueue(context, 82);
  context->activeLogicalProcessToken = 22;
  uint64_t otherProcess = talos_rt_v1_deferred_enqueue(context, 81);
  ASSERT_NE(firstSite, 0u);
  ASSERT_NE(secondSite, 0u);
  ASSERT_NE(otherProcess, 0u);

  talos_rt_flush_deferred_immediate_reports_unlocked(context, 21);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, firstSite), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, secondSite), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, otherProcess), 1u);
  EXPECT_TRUE(context->deferredImmediateReports.empty());
  EXPECT_TRUE(context->deferredImmediateProcessReports.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeDeferredAssertion, LabeledDisableCancelsOnlyThatAssertion) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  context->activeLogicalProcessToken = 31;
  uint64_t first =
      talos_rt_v1_deferred_enqueue_for_assertion(context, 91, 701);
  uint64_t unrelated =
      talos_rt_v1_deferred_enqueue_for_assertion(context, 92, 702);
  context->activeLogicalProcessToken = 32;
  uint64_t otherProcess =
      talos_rt_v1_deferred_enqueue_for_assertion(context, 91, 701);
  ASSERT_NE(first, 0u);
  ASSERT_NE(unrelated, 0u);
  ASSERT_NE(otherProcess, 0u);

  context->activeLogicalProcessToken = 31;
  EXPECT_EQ(talos_rt_v1_control_disable(context, 701, 0, 0), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, first), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, otherProcess), 1u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, unrelated), 1u);

  context->activeLogicalProcessToken = 31;
  uint64_t hierarchicalFirst =
      talos_rt_v1_deferred_enqueue_for_assertion(context, 93, 703);
  context->activeLogicalProcessToken = 32;
  uint64_t hierarchicalSecond =
      talos_rt_v1_deferred_enqueue_for_assertion(context, 93, 703);
  context->activeLogicalProcessToken = 33;
  EXPECT_EQ(talos_rt_v1_control_disable(context, 703, 0, 1), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, hierarchicalFirst), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, hierarchicalSecond), 0u);
  EXPECT_TRUE(context->deferredImmediateAssertionReports.empty());
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeDeferredAssertion, AssertionControlOffKillAndOn) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->activeLogicalProcessToken = 41;

  uint64_t survivesOff =
      talos_rt_v1_deferred_enqueue_for_assertion(context, 101, 801);
  ASSERT_NE(survivesOff, 0u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 4, 801), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_enabled(context, 801), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, survivesOff), 1u);

  EXPECT_EQ(talos_rt_v1_assertion_control(context, 3, 801), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_enabled(context, 801), 1u);
  uint64_t killed =
      talos_rt_v1_deferred_enqueue_for_assertion(context, 102, 801);
  uint64_t unrelated =
      talos_rt_v1_deferred_enqueue_for_assertion(context, 103, 802);
  ASSERT_NE(killed, 0u);
  ASSERT_NE(unrelated, 0u);
  EXPECT_EQ(talos_rt_v1_assertion_kill_epoch(context, 801), 0u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 5, 801), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_kill_epoch(context, 801), 1u);
  EXPECT_EQ(talos_rt_v1_assertion_kill_epoch(context, 802), 0u);
  EXPECT_EQ(talos_rt_v1_assertion_enabled(context, 801), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, killed), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, unrelated), 1u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 12, 801),
            TALOS_RT_INVALID_ARGUMENT);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeDeferredAssertion, AssertionActionControlsAndLocking) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);

  EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 901), 7u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 7, 901), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 901), 4u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 10, 901), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 901), 5u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 11, 901), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 901), 5u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 6, 901), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 901), 7u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 9, 901), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 901), 3u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 8, 901), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 901), 7u);

  EXPECT_EQ(talos_rt_v1_assertion_control(context, 1, 901), TALOS_RT_OK);
  context->activeLogicalProcessToken = 91;
  uint64_t lockedTicket =
      talos_rt_v1_deferred_enqueue_for_assertion(context, 111, 901);
  ASSERT_NE(lockedTicket, 0u);
  EXPECT_EQ(talos_rt_v1_assertion_kill_epoch(context, 901), 0u);
  for (uint32_t action = 3; action <= 11; ++action) {
    EXPECT_EQ(talos_rt_v1_assertion_control(context, action, 901),
              TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_assertion_enabled(context, 901), 1u);
    EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 901), 7u);
  }
  EXPECT_EQ(talos_rt_v1_assertion_kill_epoch(context, 901), 0u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, lockedTicket), 1u);
  EXPECT_EQ(talos_rt_v1_deferred_mature(context, lockedTicket), 0u);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 2, 901), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 4, 901), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_enabled(context, 901), 0u);

  EXPECT_EQ(talos_rt_v1_assertion_control(context, 4, 902), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 7, 902), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 9, 902), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 1, 902), TALOS_RT_OK);
  for (uint32_t action = 3; action <= 11; ++action) {
    EXPECT_EQ(talos_rt_v1_assertion_control(context, action, 902),
              TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_assertion_enabled(context, 902), 0u);
    EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 902), 0u);
  }
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 2, 902), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 3, 902), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 6, 902), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_control(context, 8, 902), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_assertion_enabled(context, 902), 1u);
  EXPECT_EQ(talos_rt_v1_assertion_action_state(context, 902), 7u);

  talos_rt_v1_context_destroy(context);
}

} // namespace

TEST(RuntimeControl, SelectedDisableIdentifiesRecursiveDynamicActivation) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->activeLogicalProcessToken = 41;
  uint64_t outer = 0, inner = 0, child = 0, selected = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_control_enter(context, 91, &outer), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_control_enter(context, 91, &inner), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_control_enter(context, 92, &child), TALOS_RT_OK);
  ASSERT_NE(outer, inner);
  ASSERT_EQ(
      talos_rt_v1_control_disable_selected(context, 91, 0, 0, &selected),
      TALOS_RT_OK);
  EXPECT_EQ(selected, inner);
  EXPECT_EQ(context->activeControls, std::vector<uint64_t>({outer}));
  EXPECT_EQ(context->controlActivations.count(inner), 0u);
  EXPECT_EQ(context->controlActivations.count(child), 0u);

  ASSERT_EQ(talos_rt_v1_control_enter(context, 91, &inner), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_control_enter(context, 92, &child), TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_control_disable_selected(context, 91, 0, 1, &selected),
      TALOS_RT_OK);
  EXPECT_EQ(selected, outer);
  EXPECT_TRUE(context->activeControls.empty());
  EXPECT_TRUE(context->controlActivations.empty());
  ASSERT_EQ(
      talos_rt_v1_control_disable_selected(context, 91, 0, 1, &selected),
      TALOS_RT_OK);
  EXPECT_EQ(selected, 0u);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeControl, SelectedDisableReturnsZeroWithoutCurrentEffect) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  context->activeLogicalProcessToken = 51;
  uint64_t activation = 0, selected = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_control_enter(context, 93, &activation),
            TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_control_disable_selected(context, 94, 0, 1, &selected),
      TALOS_RT_OK);
  EXPECT_EQ(selected, 0u);
  EXPECT_EQ(context->activeControls, std::vector<uint64_t>({activation}));
  selected = UINT64_MAX;
  EXPECT_EQ(talos_rt_v1_control_disable_selected(context, 94, activation, 0,
                                                   &selected),
            TALOS_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(selected, 0u);
  EXPECT_EQ(talos_rt_v1_control_disable_selected(context, 93, 0, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  ASSERT_EQ(talos_rt_v1_control_disable_selected(context, 93, activation, 0,
                                                   &selected),
            TALOS_RT_OK);
  EXPECT_EQ(selected, activation);
  selected = UINT64_MAX;
  EXPECT_EQ(
      talos_rt_v1_control_disable_selected(nullptr, 93, 0, 1, &selected),
      TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(selected, 0u);
  talos_rt_v1_context_destroy(context);
}
