#include "talos/Runtime/PromotionRangeIndex.h"
#include "gtest/gtest.h"

#include <random>
#include <set>

using talos::runtime::buildPromotionRangeIndex;
using talos::runtime::visitPromotionRangeDependencies;

namespace {

TEST(PromotionRangeIndex, MergesOnlyTheSameProofAndRejectsInvalidRanges) {
  std::vector<talos_rt_native_promotion_dependency> entries{{9, 20, 0, 0},
                                                              {1, 10, 0, 0},
                                                              {20, 25, 0, 0},
                                                              {2, 12, 0, 1},
                                                              {30, 31, 0, 0}};
  ASSERT_TRUE(buildPromotionRangeIndex(entries, 32, 2));
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].begin, 1u);
  EXPECT_EQ(entries[0].end, 25u);
  EXPECT_EQ(entries[1].certificate, 1u);
  EXPECT_EQ(entries[0].subtree_end, 25u);
  EXPECT_EQ(entries[1].subtree_end, 31u);
  EXPECT_EQ(entries[2].subtree_end, 31u);
  for (auto invalid : std::vector<talos_rt_native_promotion_dependency>{
           {1, 1, 0, 0}, {2, 1, 0, 0}, {0, 33, 0, 0}, {0, 1, 0, 2}}) {
    std::vector<talos_rt_native_promotion_dependency> bad{invalid};
    EXPECT_FALSE(buildPromotionRangeIndex(bad, 32, 2));
  }
}

TEST(PromotionRangeIndex, RandomQueriesMatchIndependentScalarOverlap) {
  std::mt19937_64 random(0x48b3c9);
  for (unsigned trial = 0; trial != 24; ++trial) {
    // Include queries and intervals near the limit without wrapping endpoints.
    uint64_t base = trial % 2 ? UINT64_MAX - 4096 : 0;
    std::vector<talos_rt_native_promotion_dependency> original;
    for (unsigned index = 0; index != 400; ++index) {
      uint64_t begin = base + random() % 2048;
      original.push_back(
          {begin, begin + 1 + random() % 512, 0, random() % 129});
    }
    auto entries = original;
    ASSERT_TRUE(buildPromotionRangeIndex(entries, UINT64_MAX, 129));
    for (unsigned query = 0; query != 256; ++query) {
      uint64_t begin = base + random() % 4096;
      uint64_t width = random() % 600;
      uint64_t end = width > UINT64_MAX - begin ? UINT64_MAX : begin + width;
      std::set<uint64_t> expected, actual;
      if (width)
        for (auto entry : original)
          if (entry.begin < end && begin < entry.end)
            expected.insert(entry.certificate);
      visitPromotionRangeDependencies(entries.data(), entries.size(), begin,
                                      width,
                                      [&](uint64_t id) { actual.insert(id); });
      EXPECT_EQ(actual, expected);
    }
  }
}

unsigned aggregateInvalidations;
void invalidateAggregate() { ++aggregateInvalidations; }
void promotedRoute() {}
void fourStateRoute() {}

TEST(PromotionRangeIndex, RuntimeUpdatesOnlyDependentProofState) {
  uint8_t first = 1, second = 1;
  uint64_t pending = 0;
  auto route = promotedRoute;
  talos_rt_native_promotion_certificate certificates[] = {
      {&first, &pending, 1, nullptr, nullptr},
      {&second, &pending, uint64_t{1} << 63, nullptr, nullptr},
      {nullptr, nullptr, 0, &route, fourStateRoute}};
  std::vector<talos_rt_native_promotion_dependency> entries{
      {0, 4, 0, 0}, {8, 12, 0, 1}, {8, 10, 0, 2}, {16, 20, 0, 2}};
  ASSERT_TRUE(buildPromotionRangeIndex(entries, 32, 3));
  aggregateInvalidations = 0;
  auto invalidate = [&](uint64_t offset, uint64_t width) {
    talos_rt_v1_native_promotion_invalidate_ranges(
        entries.data(), entries.size(), certificates, invalidateAggregate,
        offset, width);
  };
  invalidate(4, 4);
  EXPECT_EQ(aggregateInvalidations, 0u);
  invalidate(1, 1);
  EXPECT_EQ(first, 0u);
  EXPECT_EQ(second, 1u);
  EXPECT_EQ(route, promotedRoute);
  EXPECT_EQ(pending, 1u);
  EXPECT_EQ(aggregateInvalidations, 1u);
  invalidate(8, 12);
  EXPECT_EQ(second, 0u);
  EXPECT_EQ(route, fourStateRoute);
  EXPECT_EQ(pending, uint64_t{1} | (uint64_t{1} << 63));
  // One aggregate invalidation even if several proofs (or several disjoint
  // ranges of the same proof) were affected by this single mutation.
  EXPECT_EQ(aggregateInvalidations, 2u);
  invalidate(0, 0);
  EXPECT_EQ(aggregateInvalidations, 2u);
}

TEST(PromotionRangeIndex, NBACertificatesAndRecoveryAreIndependent) {
  uint8_t failedKernel = 0, validKernel = 1;
  uint64_t pending = 0, routePending = 0;
  uint64_t knownRoots[] = {6, uint64_t{1} << 63};
  auto route = promotedRoute;
  talos_rt_native_promotion_certificate certificates[] = {
      {&failedKernel, &pending, 1, nullptr, nullptr},
      {&validKernel, &pending, 2, nullptr, nullptr},
      {nullptr, &routePending, 4, &route, fourStateRoute},
      {nullptr, &knownRoots[0], 2, nullptr, nullptr},
      {nullptr, &knownRoots[1], uint64_t{1} << 63, nullptr, nullptr}};
  std::vector<talos_rt_native_promotion_dependency> entries{
      {0, 4, 0, 0}, {4, 8, 0, 1}, {4, 8, 0, 2}, {0, 4, 0, 3}, {128, 132, 0, 4}};
  ASSERT_TRUE(buildPromotionRangeIndex(entries, 160, 5));
  aggregateInvalidations = 0;
  // The kernel was already unpromoted, but this NBA root was independently
  // known. It must lose its certificate before a value-only commit is used.
  talos_rt_v1_native_promotion_invalidate_ranges(
      entries.data(), entries.size(), certificates, invalidateAggregate, 1, 1);
  EXPECT_EQ(failedKernel, 0);
  EXPECT_EQ(validKernel, 1);
  EXPECT_EQ(pending, 1u);
  EXPECT_EQ(knownRoots[0], 4u);
  EXPECT_EQ(knownRoots[1], uint64_t{1} << 63);
  EXPECT_EQ(route, promotedRoute);
  EXPECT_EQ(aggregateInvalidations, 1u);

  // A gain requests rechecking; it cannot prove an entire root known, nor
  // invalidate an already valid route/kernel/root touched by a wider range.
  talos_rt_v1_native_promotion_recheck_ranges(
      entries.data(), entries.size(), certificates, invalidateAggregate, 0, 8);
  EXPECT_EQ(failedKernel, 0);
  EXPECT_EQ(validKernel, 1);
  EXPECT_EQ(pending, 1u);
  EXPECT_EQ(knownRoots[0], 4u);
  EXPECT_EQ(route, promotedRoute);
  EXPECT_EQ(aggregateInvalidations, 2u);
  talos_rt_v1_native_promotion_recheck_ranges(entries.data(), entries.size(),
                                                certificates,
                                                invalidateAggregate, 8, 120);
  EXPECT_EQ(aggregateInvalidations, 2u);
  talos_rt_v1_native_promotion_invalidate_ranges(entries.data(),
                                                   entries.size(), certificates,
                                                   invalidateAggregate, 129, 1);
  EXPECT_EQ(knownRoots[0], 4u);
  EXPECT_EQ(knownRoots[1], 0u);
  EXPECT_EQ(routePending, 0u);
  talos_rt_v1_native_promotion_invalidate_ranges(
      entries.data(), entries.size(), certificates, invalidateAggregate, 5, 1);
  EXPECT_EQ(route, fourStateRoute);
  EXPECT_EQ(routePending, 4u);
  routePending = 0; // A failed scan consumes its pending bit.
  talos_rt_v1_native_promotion_recheck_ranges(
      entries.data(), entries.size(), certificates, invalidateAggregate, 5, 1);
  EXPECT_EQ(routePending, 4u);
  route = promotedRoute;
  routePending = 0;
  talos_rt_v1_native_promotion_recheck_ranges(
      entries.data(), entries.size(), certificates, invalidateAggregate, 5, 1);
  EXPECT_EQ(routePending, 0u);
}

TEST(PromotionBucketIndex, BudgetAndEncodingRejectUnboundedExpansion) {
  talos::runtime::PromotionBucketIndex buckets;
  auto build = [&](std::vector<talos_rt_native_promotion_dependency> entries,
                   uint64_t budget) {
    return talos::runtime::buildPromotionBucketIndex(entries, budget,
                                                       buckets);
  };
  EXPECT_FALSE(build({}, 1024));
  EXPECT_FALSE(build({{1, 1, 0, 0}}, 1024));
  EXPECT_FALSE(build({{0, 1, 0, uint64_t{1} << 48}}, 1024));
  EXPECT_FALSE(build({{UINT64_MAX - 1, UINT64_MAX, 0, 0}}, UINT64_MAX));
  EXPECT_FALSE(build({{0, 1024 * 1024, 0, 0}}, 1024));
  EXPECT_FALSE(build({{1024 * 1024, 1024 * 1024 + 1, 0, 0}}, 1024));
  EXPECT_TRUE(buckets.starts.empty());
  EXPECT_TRUE(buckets.records.empty());
  EXPECT_TRUE(build({{63, 65, 0, (uint64_t{1} << 48) - 1}}, 28));
  EXPECT_EQ(buckets.starts, (std::vector<uint32_t>{0, 1, 2}));
  EXPECT_EQ(buckets.records[0] & 65535, (63u << 8) | 64);
  EXPECT_EQ(buckets.records[1] & 65535, 1u);
  EXPECT_EQ(buckets.records[0] >> 16, (uint64_t{1} << 48) - 1);
  EXPECT_FALSE(build({{63, 65, 0, 0}}, 27));
  EXPECT_TRUE(buckets.starts.empty());
}

TEST(PromotionBucketIndex, RandomQueriesMatchIndependentScalarOverlap) {
  std::mt19937_64 random(0xbac4e7);
  for (unsigned trial = 0; trial != 24; ++trial) {
    std::vector<talos_rt_native_promotion_dependency> original;
    for (unsigned index = 0; index != 400; ++index) {
      uint64_t begin = random() % 8192;
      original.push_back(
          {begin, begin + 1 + random() % 512, 0, random() % 129});
    }
    auto entries = original;
    ASSERT_TRUE(buildPromotionRangeIndex(entries, 9000, 129));
    talos::runtime::PromotionBucketIndex buckets;
    ASSERT_TRUE(
        talos::runtime::buildPromotionBucketIndex(entries, 65536, buckets));
    for (unsigned query = 0; query != 256; ++query) {
      uint64_t begin = random() % 16384;
      uint64_t width = query % 31 ? random() % 600 : UINT64_MAX;
      uint64_t end = width > UINT64_MAX - begin ? UINT64_MAX : begin + width;
      std::set<uint64_t> expected, actual;
      if (width)
        for (auto entry : original)
          if (entry.begin < end && begin < entry.end)
            expected.insert(entry.certificate);
      talos::runtime::visitPromotionBucketDependencies(
          buckets.starts.data(), buckets.records.data(),
          buckets.starts.size() - 1, begin, width,
          [&](uint64_t id) { actual.insert(id); });
      EXPECT_EQ(actual, expected);
    }
  }
}

TEST(PromotionBucketIndex, RuntimeActionsMatchIntervalIndex) {
  // Include duplicate certificate visits across buckets, holes, the high word
  // bit, a failed and a valid latch, an NBA bit, and a function-pointer route.
  std::vector<talos_rt_native_promotion_dependency> entries{{0, 130, 0, 0},
                                                              {63, 68, 0, 1},
                                                              {60, 66, 0, 2},
                                                              {192, 193, 0, 0},
                                                              {257, 300, 0, 3}};
  ASSERT_TRUE(buildPromotionRangeIndex(entries, 320, 4));
  talos::runtime::PromotionBucketIndex buckets;
  ASSERT_TRUE(
      talos::runtime::buildPromotionBucketIndex(entries, 1024, buckets));
  for (uint64_t offset :
       {0u, 60u, 63u, 64u, 65u, 129u, 130u, 192u, 257u, 319u, 320u})
    for (uint64_t width : {0u, 1u, 4u, 64u, 65u, 256u})
      for (bool recover : {false, true}) {
        uint8_t latch[2][2] = {{0, 1}, {0, 1}};
        uint64_t words[2][2] = {{0, UINT64_MAX}, {0, UINT64_MAX}};
        auto firstRoute = recover ? fourStateRoute : promotedRoute;
        talos_rt_native_promotion_invalidate routes[] = {firstRoute,
                                                           firstRoute};
        unsigned aggregate[2];
        for (unsigned variant = 0; variant != 2; ++variant) {
          talos_rt_native_promotion_certificate certificates[] = {
              {&latch[variant][0], &words[variant][0], 1, nullptr, nullptr},
              {&latch[variant][1], &words[variant][0], uint64_t{1} << 63,
               nullptr, nullptr},
              {nullptr, &words[variant][1], 2, nullptr, nullptr},
              {nullptr, &words[variant][0], 4, &routes[variant],
               fourStateRoute}};
          aggregateInvalidations = 0;
          if (variant) {
            auto action =
                recover ? talos_rt_v1_native_promotion_recheck_buckets
                        : talos_rt_v1_native_promotion_invalidate_buckets;
            action(buckets.starts.data(), buckets.records.data(),
                   buckets.starts.size() - 1, certificates, invalidateAggregate,
                   offset, width);
          } else {
            auto action =
                recover ? talos_rt_v1_native_promotion_recheck_ranges
                        : talos_rt_v1_native_promotion_invalidate_ranges;
            action(entries.data(), entries.size(), certificates,
                   invalidateAggregate, offset, width);
          }
          aggregate[variant] = aggregateInvalidations;
        }
        EXPECT_EQ(latch[0][0], latch[1][0]);
        EXPECT_EQ(latch[0][1], latch[1][1]);
        EXPECT_EQ(words[0][0], words[1][0]);
        EXPECT_EQ(words[0][1], words[1][1]);
        EXPECT_EQ(routes[0], routes[1]);
        EXPECT_EQ(aggregate[0], aggregate[1]);
      }
}

} // namespace
