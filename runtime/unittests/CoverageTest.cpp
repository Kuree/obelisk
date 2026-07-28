//===- CoverageTest.cpp - Functional coverage runtime tests --------------===//

#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <array>

namespace {

class CoverageRuntimeTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  }
  void TearDown() override { obelisk_rt_v1_context_destroy(context); }

  obelisk_rt_context *context = nullptr;
};

TEST_F(CoverageRuntimeTest, InstanceAndTypeQueriesUseEqualWeightAverages) {
  constexpr std::array<uint64_t, 2> bins{2, 4};
  double percentage = -1.0;
  int32_t covered = -1;
  int32_t total = -1;
  ASSERT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 17, bins.data(), bins.size(), &percentage, &covered,
                &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 0.0);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 0);

  obelisk_rt_covergroup_v1 first = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(
                context, 17, bins.data(), bins.size(), &first),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, first, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 0.0);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 6);

  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, first, 0, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, first, 1, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, first, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 37.5);
  EXPECT_EQ(covered, 2);
  EXPECT_EQ(total, 6);

  obelisk_rt_covergroup_v1 second = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(
                context, 17, bins.data(), bins.size(), &second),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, second, 0, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, second, 0, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, second, 1, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, second, 1, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, second, 1, 2),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, second, 1, 3),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 17, bins.data(), bins.size(), &percentage, &covered,
                &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 68.75);
  EXPECT_EQ(covered, 8);
  EXPECT_EQ(total, 12);
}

TEST_F(CoverageRuntimeTest, StopStartAndInvalidHandlesAreDeterministic) {
  constexpr std::array<uint64_t, 1> bins{1};
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(
                context, 23, bins.data(), bins.size(), &handle),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_enabled(context, handle, 0),
            OBELISK_RT_OK);
  uint32_t enabled = 1;
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_sample_enabled(context, handle, &enabled),
      OBELISK_RT_OK);
  EXPECT_EQ(enabled, 0u);
  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, handle, 0, 0),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_covergroup_set_enabled(context, handle, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, handle, 0, 0),
            OBELISK_RT_OK);
  double percentage = 0.0;
  int32_t covered = 0;
  int32_t total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  EXPECT_EQ(covered, 1);
  EXPECT_EQ(total, 1);

  EXPECT_EQ(obelisk_rt_v1_covergroup_set_enabled(context, 0, 1),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_covergroup_bin_hit(context, UINT64_MAX, 0, 0),
            OBELISK_RT_INVALID_HANDLE);
}

TEST_F(CoverageRuntimeTest, CompleteSamplesCommitAtomically) {
  constexpr std::array<uint64_t, 2> bins{2, 2};
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(
                context, 29, bins.data(), bins.size(), &handle),
            OBELISK_RT_OK);

  constexpr std::array<uint8_t, 4> invalidHits{1, 2, 1, 1};
  EXPECT_EQ(obelisk_rt_v1_covergroup_sample(
                context, handle, invalidHits.data(), invalidHits.size()),
            OBELISK_RT_INVALID_ARGUMENT);
  constexpr std::array<uint8_t, 3> shortHits{1, 1, 1};
  EXPECT_EQ(obelisk_rt_v1_covergroup_sample(
                context, handle, shortHits.data(), shortHits.size()),
            OBELISK_RT_INVALID_ARGUMENT);

  double percentage = -1.0;
  int32_t covered = -1;
  int32_t total = -1;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 0.0);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 4);

  constexpr std::array<uint8_t, 4> firstSample{1, 1, 0, 1};
  ASSERT_EQ(obelisk_rt_v1_covergroup_sample(
                context, handle, firstSample.data(), firstSample.size()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 75.0);
  EXPECT_EQ(covered, 3);

  ASSERT_EQ(obelisk_rt_v1_covergroup_set_enabled(context, handle, 0),
            OBELISK_RT_OK);
  constexpr std::array<uint8_t, 4> secondSample{0, 0, 1, 0};
  ASSERT_EQ(obelisk_rt_v1_covergroup_sample(
                context, handle, secondSample.data(), secondSample.size()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 75.0);
  EXPECT_EQ(covered, 3);

  ASSERT_EQ(obelisk_rt_v1_covergroup_set_enabled(context, handle, 1),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_sample(
                context, handle, secondSample.data(), secondSample.size()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  EXPECT_EQ(covered, 4);
}

} // namespace
