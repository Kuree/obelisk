//===- CoverageBlockEventsTest.cpp - Block-event coverage tests ----------===//

#include "obelisk/Runtime/Runtime.h"

#include "../lib/RuntimeInternal.h"

#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

constexpr uint64_t kObserverID = 7001;
constexpr uint64_t kPrimaryTarget = 8001;
constexpr uint64_t kNestedTarget = 8002;

std::vector<uint64_t> sampledHandles;
bool fireNestedEvent = false;
bool samplerKnownTrue = true;

obelisk_rt_status blockEventSampler(obelisk_rt_context *context,
                                    const uint64_t *captures,
                                    uint32_t captureCount, uint64_t *value,
                                    uint64_t *unknown, uint32_t limbCount) {
  if (!context || !captures || captureCount != 1 || !value || !unknown ||
      limbCount != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  sampledHandles.push_back(captures[0]);
  if (fireNestedEvent) {
    fireNestedEvent = false;
    obelisk_rt_status status = obelisk_rt_v1_covergroup_block_event_fire(
        context, kNestedTarget, OBELISK_RT_COVERGROUP_BLOCK_EVENT_BEGIN,
        nullptr);
    if (status != OBELISK_RT_OK)
      return status;
  }
  value[0] = samplerKnownTrue ? 1 : 0;
  unknown[0] = 0;
  return OBELISK_RT_OK;
}

class CoverageBlockEventsTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
    context->coverage = std::make_unique<CoverageState>();
    context->coverage->instances.emplace(42, FunctionalCoverageInstanceState{});
    context->coverage->instances.emplace(43, FunctionalCoverageInstanceState{});

    captureABI = {OBELISK_RT_OBSERVER_CAPTURE_COVERGROUP, 64};
    observer = {kObserverID,
                &captureABI,
                1,
                1,
                0,
                OBELISK_RT_OBSERVER_NO_BYTECODE,
                blockEventSampler,
                0};
    execution.version = OBELISK_RT_VERSION;
    execution.observers = &observer;
    execution.observer_count = 1;
    processDescriptor.version = OBELISK_RT_VERSION;
    processDescriptor.execution = &execution;
    process.descriptor = &processDescriptor;
    process.tier = OBELISK_RT_TIER_NATIVE;
    context->execution = &execution;
    context->activeNativeProcess = &process;
    context->activeLogicalProcessToken =
        OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG | UINT64_C(1);
    sampledHandles.clear();
    fireNestedEvent = false;
    samplerKnownTrue = true;
  }

  void TearDown() override {
    context->activeNativeProcess = nullptr;
    context->activeLogicalProcessToken = 0;
    obelisk_rt_v1_context_destroy(context);
  }

  obelisk_rt_context *context = nullptr;
  obelisk_rt_observer_capture_abi_v1 captureABI{};
  obelisk_rt_observer_descriptor_v1 observer{};
  obelisk_rt_execution_descriptor_v1 execution{};
  obelisk_rt_process_descriptor_v1 processDescriptor{};
  obelisk_rt_process_instance_v1 process{};
};

TEST_F(CoverageBlockEventsTest,
       SamplesSynchronouslyDeduplicatesClausesAndSupportsReentrancy) {
  const obelisk_rt_computed_capture_v1 firstCapture{42, 0, 0, 0};
  const std::array<uint64_t, 3> firstTargets{kPrimaryTarget, kPrimaryTarget,
                                             kPrimaryTarget};
  const std::array<obelisk_rt_covergroup_block_event_kind_v1, 3> firstKinds{
      OBELISK_RT_COVERGROUP_BLOCK_EVENT_BEGIN,
      OBELISK_RT_COVERGROUP_BLOCK_EVENT_BEGIN,
      OBELISK_RT_COVERGROUP_BLOCK_EVENT_END};
  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_register(
                context, 42, nullptr, kObserverID, &firstCapture, 1,
                firstTargets.data(), firstKinds.data(), firstTargets.size()),
            OBELISK_RT_OK);

  const obelisk_rt_computed_capture_v1 secondCapture{43, 0, 0, 0};
  const uint64_t secondTarget = kNestedTarget;
  const obelisk_rt_covergroup_block_event_kind_v1 secondKind =
      OBELISK_RT_COVERGROUP_BLOCK_EVENT_BEGIN;
  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_register(
                context, 43, nullptr, kObserverID, &secondCapture, 1,
                &secondTarget, &secondKind, 1),
            OBELISK_RT_OK);

  ASSERT_NE(context->coverage->blockEvents, nullptr);
  EXPECT_EQ(context->coverage->blockEvents->registrations.size(), 2u);
  auto primaryBucket = context->coverage->blockEvents->buckets.find(
      {kPrimaryTarget, OBELISK_RT_COVERGROUP_BLOCK_EVENT_BEGIN});
  ASSERT_NE(primaryBucket, context->coverage->blockEvents->buckets.end());
  EXPECT_EQ(primaryBucket->second.size(), 1u);

  fireNestedEvent = true;
  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_fire(
                context, kPrimaryTarget,
                OBELISK_RT_COVERGROUP_BLOCK_EVENT_BEGIN, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(sampledHandles, std::vector<uint64_t>({42, 43}));

  sampledHandles.clear();
  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_fire(
                context, kPrimaryTarget, OBELISK_RT_COVERGROUP_BLOCK_EVENT_END,
                nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(sampledHandles, std::vector<uint64_t>({42}));

  context->coverage->instances.find(42)->second.enabled = false;
  sampledHandles.clear();
  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_fire(
                context, kPrimaryTarget,
                OBELISK_RT_COVERGROUP_BLOCK_EVENT_BEGIN, nullptr),
            OBELISK_RT_OK);
  EXPECT_TRUE(sampledHandles.empty());
}

TEST_F(CoverageBlockEventsTest, RejectsMalformedRegistrationAndSamplerResult) {
  const obelisk_rt_computed_capture_v1 capture{42, 0, 0, 0};
  const uint64_t target = kPrimaryTarget;
  const obelisk_rt_covergroup_block_event_kind_v1 kind =
      OBELISK_RT_COVERGROUP_BLOCK_EVENT_BEGIN;

  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = 0;
  EXPECT_EQ(obelisk_rt_v1_covergroup_block_event_register(context, 42, nullptr,
                                                          kObserverID, &capture,
                                                          1, &target, &kind, 1),
            OBELISK_RT_INVALID_LIFECYCLE);
  context->activeNativeProcess = &process;
  context->activeLogicalProcessToken =
      OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG | UINT64_C(1);

  obelisk_rt_computed_capture_v1 wrongHandle{43, 0, 0, 0};
  EXPECT_EQ(obelisk_rt_v1_covergroup_block_event_register(
                context, 42, nullptr, kObserverID, &wrongHandle, 1, &target,
                &kind, 1),
            OBELISK_RT_INVALID_DESIGN);
  auto invalidKind = static_cast<obelisk_rt_covergroup_block_event_kind_v1>(2);
  EXPECT_EQ(obelisk_rt_v1_covergroup_block_event_register(
                context, 42, nullptr, kObserverID, &capture, 1, &target,
                &invalidKind, 1),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(obelisk_rt_v1_covergroup_block_event_fire(context, target,
                                                      invalidKind, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);

  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_register(context, 42, nullptr,
                                                          kObserverID, &capture,
                                                          1, &target, &kind, 1),
            OBELISK_RT_OK);
  samplerKnownTrue = false;
  EXPECT_EQ(
      obelisk_rt_v1_covergroup_block_event_fire(context, target, kind, nullptr),
      OBELISK_RT_INVALID_DESIGN);
}

TEST_F(CoverageBlockEventsTest,
       RootsReceiversAndFiltersOnlyReceiverScopedFires) {
  constexpr char className[] = "block_event_owner";
  const obelisk_rt_class_descriptor_v1 classDescriptor{OBELISK_RT_VERSION,
                                                       0,
                                                       9001,
                                                       sizeof(void *),
                                                       alignof(void *),
                                                       nullptr,
                                                       nullptr,
                                                       0,
                                                       nullptr,
                                                       nullptr,
                                                       0,
                                                       className,
                                                       sizeof(className) - 1,
                                                       nullptr};
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  obelisk_rt_object_v1 *firstReceiver = nullptr;
  obelisk_rt_object_v1 *secondReceiver = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_object_allocate(lane, &classDescriptor, &firstReceiver),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_object_allocate(lane, &classDescriptor, &secondReceiver),
      OBELISK_RT_OK);

  const uint64_t target = kPrimaryTarget;
  const obelisk_rt_covergroup_block_event_kind_v1 kind =
      OBELISK_RT_COVERGROUP_BLOCK_EVENT_BEGIN;
  const obelisk_rt_computed_capture_v1 firstCapture{42, 0, 0, 0};
  const obelisk_rt_computed_capture_v1 secondCapture{43, 0, 0, 0};
  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_register(
                context, 42, firstReceiver, kObserverID, &firstCapture, 1,
                &target, &kind, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_register(
                context, 43, secondReceiver, kObserverID, &secondCapture, 1,
                &target, &kind, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_register(
                context, 42, firstReceiver, kObserverID, &firstCapture, 1,
                &target, &kind, 1),
            OBELISK_RT_OK);

  // No stack scanning is involved. Both objects survive solely through the
  // context-owned block-event registrations.
  firstReceiver = nullptr;
  secondReceiver = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 2u);

  auto &registrations = context->coverage->blockEvents->registrations;
  obelisk_rt_object_v1 *retainedFirst = registrations.find(1)->second->receiver;
  sampledHandles.clear();
  ASSERT_EQ(obelisk_rt_v1_covergroup_block_event_fire(context, target, kind,
                                                      retainedFirst),
            OBELISK_RT_OK);
  // Multiple OR clauses (including clauses reached through aliased object
  // variables) denote one triggering event and sample an instance only once.
  EXPECT_EQ(sampledHandles, std::vector<uint64_t>({42}));

  // A null firing receiver denotes a static task/function/block and therefore
  // samples every matching registration, even for a class-owned covergroup.
  sampledHandles.clear();
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_block_event_fire(context, target, kind, nullptr),
      OBELISK_RT_OK);
  EXPECT_EQ(sampledHandles, std::vector<uint64_t>({42, 43}));

  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
}

} // namespace
