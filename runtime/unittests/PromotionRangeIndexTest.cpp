#include "obelisk/Runtime/PromotionRangeIndex.h"
#include "gtest/gtest.h"

#include <random>
#include <set>

using obelisk::runtime::buildPromotionRangeIndex;
using obelisk::runtime::visitPromotionRangeDependencies;

namespace {

TEST(PromotionRangeIndex, MergesOnlyTheSameProofAndRejectsInvalidRanges) {
  std::vector<obelisk_rt_native_promotion_dependency> entries{{9, 20, 0, 0},
                                                              {1, 10, 0, 0},
                                                              {20, 25, 0, 0},
                                                              {2, 12, 0, 1},
                                                              {30, 31, 0, 0}};
  ASSERT_TRUE(buildPromotionRangeIndex(entries, 32, 2));
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].begin, 1u);
  EXPECT_EQ(entries[0].end, 25u);
  EXPECT_EQ(entries[1].certificate, 1u);
  EXPECT_EQ(entries[1].prefix_end, 25u);
  EXPECT_EQ(entries[2].prefix_end, 31u);
  for (auto invalid : std::vector<obelisk_rt_native_promotion_dependency>{
           {1, 1, 0, 0}, {2, 1, 0, 0}, {0, 33, 0, 0}, {0, 1, 0, 2}}) {
    std::vector<obelisk_rt_native_promotion_dependency> bad{invalid};
    EXPECT_FALSE(buildPromotionRangeIndex(bad, 32, 2));
  }
}

TEST(PromotionRangeIndex, RandomQueriesMatchIndependentScalarOverlap) {
  std::mt19937_64 random(0x48b3c9);
  for (unsigned trial = 0; trial != 24; ++trial) {
    // Include queries and intervals near the limit without wrapping endpoints.
    uint64_t base = trial % 2 ? UINT64_MAX - 4096 : 0;
    std::vector<obelisk_rt_native_promotion_dependency> original;
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
  obelisk_rt_native_promotion_certificate certificates[] = {
      {&first, &pending, 1, nullptr, nullptr},
      {&second, &pending, uint64_t{1} << 63, nullptr, nullptr},
      {nullptr, nullptr, 0, &route, fourStateRoute}};
  std::vector<obelisk_rt_native_promotion_dependency> entries{
      {0, 4, 0, 0}, {8, 12, 0, 1}, {8, 10, 0, 2}, {16, 20, 0, 2}};
  ASSERT_TRUE(buildPromotionRangeIndex(entries, 32, 3));
  aggregateInvalidations = 0;
  auto invalidate = [&](uint64_t offset, uint64_t width) {
    obelisk_rt_v1_native_promotion_invalidate_ranges(
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

} // namespace
