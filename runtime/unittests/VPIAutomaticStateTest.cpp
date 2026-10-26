//===- VPIAutomaticStateTest.cpp - Read-only automatic state slices -------===//

#include "../lib/RuntimeInternal.h"
#include "obelisk/Runtime/StableHandle.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>
#include <memory>

namespace {
using Context = std::unique_ptr<obelisk_rt_context,
                                decltype(&obelisk_rt_v1_context_destroy)>;

TEST(VPIAutomaticState, ReadsExactUnalignedFourStateSlicesWithoutFallback) {
  obelisk_rt_context *raw = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&raw), OBELISK_RT_OK);
  Context context(raw, obelisk_rt_v1_context_destroy);
  std::array<uint8_t, 23> value{}, unknown{};
  for (unsigned i = 0; i != value.size(); ++i) {
    value[i] = static_cast<uint8_t>(i * 19 + 7);
    unknown[i] = static_cast<uint8_t>(i * 13 + 5);
  }
  constexpr uint64_t width = 177;
  uint64_t reference = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context.get(), width, value.data(),
                                             unknown.data(), &reference),
            OBELISK_RT_OK);
  const uint64_t selectionGeneration = context->schedulerSelectionGeneration;
  const uint64_t epoch = context->schedulerEpoch;
  ASSERT_EQ(obelisk_rt_vpi_automatic_validate(context.get(), reference, width),
            OBELISK_RT_OK);
  for (const auto selection :
       {std::pair<uint64_t, uint64_t>{0, 177}, {3, 130}, {172, 5}}) {
    SCOPED_TRACE(::testing::Message()
                 << selection.first << ':' << selection.second);
    std::array<uint8_t, 23> actualValue{}, actualUnknown{};
    ASSERT_EQ(obelisk_rt_vpi_automatic_read(
                  context.get(), reference, selection.first, selection.second,
                  actualValue.data(), actualUnknown.data()),
              OBELISK_RT_OK);
    std::array<uint8_t, 23> expectedValue{}, expectedUnknown{};
    for (uint64_t bit = 0; bit != selection.second; ++bit) {
      const uint64_t source = selection.first + bit;
      expectedValue[bit / 8] |= ((value[source / 8] >> (source % 8)) & 1)
                                << (bit % 8);
      expectedUnknown[bit / 8] |= ((unknown[source / 8] >> (source % 8)) & 1)
                                  << (bit % 8);
    }
    EXPECT_EQ(actualValue, expectedValue);
    EXPECT_EQ(actualUnknown, expectedUnknown);
  }
  const auto selected = obelisk_rt_stable_handle_offset(reference, 5);
  ASSERT_EQ(obelisk_rt_vpi_automatic_validate(context.get(), selected, 172),
            OBELISK_RT_OK);
  uint8_t actual = 0, actualUnknown = 0;
  ASSERT_EQ(obelisk_rt_vpi_automatic_read(context.get(), selected, 3, 8,
                                          &actual, &actualUnknown),
            OBELISK_RT_OK);
  EXPECT_EQ(actual, value[1]);
  EXPECT_EQ(actualUnknown, unknown[1]);

  // Executable loads have fallback rules; reflection must reject the complete
  // out-of-range slice instead of returning a partly fabricated value.
  EXPECT_EQ(obelisk_rt_vpi_automatic_validate(context.get(), selected, 173),
            OBELISK_RT_INVALID_ARGUMENT);
  for (uint64_t offset : {uint64_t{175}, UINT64_MAX}) {
    actual = actualUnknown = 0xff;
    EXPECT_EQ(obelisk_rt_vpi_automatic_read(context.get(), reference, offset, 8,
                                            &actual, &actualUnknown),
              OBELISK_RT_INVALID_ARGUMENT);
    EXPECT_EQ(actual, 0);
    EXPECT_EQ(actualUnknown, 0);
  }
  EXPECT_EQ(
      obelisk_rt_vpi_automatic_validate(
          context.get(), obelisk_rt_stable_handle_offset(reference, -1), 1),
      OBELISK_RT_INVALID_ARGUMENT);
  for (auto invalid : {UINT64_MAX, uint64_t{0},
                       obelisk_rt_stable_handle_encode(
                           OBELISK_RT_STABLE_HANDLE_STATIC, 1, 0)}) {
    actual = actualUnknown = 0xff;
    EXPECT_EQ(obelisk_rt_vpi_automatic_read(context.get(), invalid, 0, 8,
                                            &actual, &actualUnknown),
              OBELISK_RT_INVALID_HANDLE);
    EXPECT_EQ(actual, 0);
    EXPECT_EQ(actualUnknown, 0);
  }
  EXPECT_EQ(obelisk_rt_vpi_automatic_validate(nullptr, reference, 1),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_vpi_automatic_validate(context.get(), reference, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_vpi_automatic_read(context.get(), reference, 0, 1,
                                          nullptr, &actualUnknown),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_vpi_automatic_read(context.get(), reference, 0,
                                          UINT64_MAX, &actual, &actualUnknown),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  EXPECT_EQ(context->schedulerSelectionGeneration, selectionGeneration);
  EXPECT_EQ(context->schedulerEpoch, epoch);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context.get(), reference, 0),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->nativeAutomaticStates.empty());
  actual = actualUnknown = 0xff;
  EXPECT_EQ(obelisk_rt_vpi_automatic_read(context.get(), selected, 0, 8,
                                          &actual, &actualUnknown),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(actual, 0);
  EXPECT_EQ(actualUnknown, 0);
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OK);

  // Missing unknown storage is an actual two-state plane, not a requested
  // executable fallback. A newly allocated state never reuses the old ID.
  uint64_t twoState = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context.get(), 8, value.data(),
                                             nullptr, &twoState),
            OBELISK_RT_OK);
  EXPECT_NE(twoState, reference);
  actual = actualUnknown = 0xff;
  ASSERT_EQ(obelisk_rt_vpi_automatic_read(context.get(), twoState, 0, 8,
                                          &actual, &actualUnknown),
            OBELISK_RT_OK);
  EXPECT_EQ(actual, value[0]);
  EXPECT_EQ(actualUnknown, 0);
  EXPECT_EQ(obelisk_rt_vpi_automatic_validate(context.get(), reference, 8),
            OBELISK_RT_INVALID_HANDLE);
}

TEST(VPIAutomaticState, ManagedWordsStayWeakAfterAutomaticStorageRelease) {
  obelisk_rt_context *raw = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&raw), OBELISK_RT_OK);
  Context context(raw, obelisk_rt_v1_context_destroy);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context.get(), &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  obelisk_rt_object_v1 *semaphore = nullptr;
  ASSERT_EQ(obelisk_rt_v1_semaphore_create(lane, 0, &semaphore), OBELISK_RT_OK);
  uint64_t reference = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_native_state_alloc_managed(context.get(), semaphore,
                                                  &reference),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_vpi_automatic_validate(context.get(), reference, 64),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_vpi_automatic_validate(context.get(), reference, 32),
            OBELISK_RT_INVALID_ARGUMENT);
  std::array<uint8_t, 8> value{}, unknown{};
  ASSERT_EQ(obelisk_rt_vpi_automatic_read(context.get(), reference, 0, 64,
                                          value.data(), unknown.data()),
            OBELISK_RT_OK);
  obelisk_rt_managed_word_v1 copied = 0;
  std::memcpy(&copied, value.data(), sizeof(copied));
  EXPECT_EQ(copied, obelisk_rt_managed_word_from_object(semaphore));
  EXPECT_EQ(unknown, (std::array<uint8_t, 8>{}));
  EXPECT_EQ(obelisk_rt_vpi_automatic_read(context.get(), reference, 1, 8,
                                          value.data(), unknown.data()),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 stats{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context.get(), &stats), OBELISK_RT_OK);
  EXPECT_EQ(stats.live_objects, 1u);
  // Validation and copying must not retain executable storage or register a
  // new managed root. The host-side copied numeric word has no rooting effect.
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context.get(), reference, 0),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->nativeAutomaticStates.empty());
  semaphore = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context.get(), &stats), OBELISK_RT_OK);
  EXPECT_EQ(stats.live_objects, 0u);
  EXPECT_EQ(obelisk_rt_vpi_automatic_read(context.get(), reference, 0, 64,
                                          value.data(), unknown.data()),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(value, (std::array<uint8_t, 8>{}));
  EXPECT_EQ(unknown, (std::array<uint8_t, 8>{}));
  EXPECT_EQ(context->schedulerStatus, OBELISK_RT_OK);
  uint64_t nullReference = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_native_state_alloc_managed(context.get(), nullptr,
                                                  &nullReference),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_vpi_automatic_read(context.get(), nullReference, 0, 64,
                                          value.data(), unknown.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(value, (std::array<uint8_t, 8>{}));
  EXPECT_EQ(unknown, (std::array<uint8_t, 8>{}));
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context.get(), nullReference, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
}
} // namespace
