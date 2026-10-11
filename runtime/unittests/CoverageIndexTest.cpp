#include "../../lib/Coverage/CoverageIndex.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace obelisk::coverage::detail;

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
TEST(CoverageIndexTest, FailedInsertionPreservesExistingLeavesAndOrdering) {
  struct Value {
    int value;
    explicit Value(int value) : value(value) {
      if (value < 0)
        throw std::runtime_error("insertion failed");
    }
  };
  IndexMap<uint64_t, Value> index;
  EXPECT_THROW(index.emplace(8, -1), std::runtime_error);
  EXPECT_TRUE(index.empty());
  index.emplace(8, 8);
  index.emplace(16, 16);
  auto *first = &index.at(8);
  EXPECT_THROW(index.emplace(1, -1), std::runtime_error);
  EXPECT_THROW(index.emplace(12, -1), std::runtime_error);
  EXPECT_THROW(index.emplace(32, -1), std::runtime_error);
  EXPECT_EQ(index.size(), 2u);
  EXPECT_EQ(&index.at(8), first);
  EXPECT_EQ(index.find(12), index.end());
  EXPECT_NO_THROW(EXPECT_FALSE(index.emplace(8, -1).second));
  index.emplace(12, 12);
  std::vector<uint64_t> keys;
  for (const auto &entry : index)
    keys.push_back(entry.first);
  EXPECT_EQ(keys, (std::vector<uint64_t>{8, 12, 16}));
}
#endif

TEST(CoverageIndexTest, ArbitraryIDsHaveOrderedStableLeaves) {
  std::mt19937_64 random(12345);
  IndexMap<uint64_t, uint64_t> index;
  std::map<uint64_t, uint64_t> expected;
  for (unsigned i = 0; i != 256; ++i) {
    const auto key = random();
    auto inserted = index.emplace(key, i);
    ASSERT_TRUE(inserted.second);
    auto *address = &inserted.first->second;
    EXPECT_FALSE(index.emplace(key, i + 1).second);
    EXPECT_EQ(&index.find(key)->second, address);
    expected.emplace(key, i);
  }
  auto entry = index.begin();
  for (const auto &[key, value] : expected) {
    ASSERT_NE(entry, index.end());
    EXPECT_EQ(entry->first, key);
    EXPECT_EQ(entry->second, value);
    EXPECT_EQ(index.at(key), value);
    ++entry;
  }
  EXPECT_EQ(entry, index.end());
  EXPECT_EQ(index.find(0), index.end());
  EXPECT_EQ(index.find(UINT64_MAX), index.end());
  IndexMap<uint64_t, uint64_t> copied = index;
  IndexMap<uint64_t, uint64_t> moved = std::move(index);
  EXPECT_TRUE(index.empty());
  EXPECT_EQ(copied.size(), expected.size());
  EXPECT_EQ(moved.size(), expected.size());
  EXPECT_EQ(copied, moved);
  for (const auto &[key, value] : expected) {
    EXPECT_EQ(copied.at(key), value);
    EXPECT_EQ(moved.at(key), value);
  }
  ++copied.begin()->second;
  EXPECT_NE(copied, moved);
}

TEST(CoverageIndexTest, VariableKeysCanSplitAboveExistingLongPrefixes) {
  using Key = std::tuple<int64_t, std::string, std::vector<uint64_t>>;
  std::vector<Key> keys{{INT64_MIN, "", {}},
                        {INT64_MAX, "", {}},
                        {0, "abcdefg1", {1, 2}},
                        {0, "abcdefg2", {1, 2}},
                        {0, "abc", {1, 2}},
                        {0, "", {1, 2}},
                        {0, std::string("\0\xff", 2), {}},
                        {0, "abc", {}},
                        {0, "abc", {1}},
                        {0, "abc", {1, 2, 3}},
                        {-1, "abc", {0}}};
  IndexSet<Key> index;
  for (const auto &key : keys) {
    EXPECT_TRUE(index.insert(key).second);
    EXPECT_FALSE(index.insert(key).second);
  }
  std::sort(keys.begin(), keys.end());
  EXPECT_EQ(std::vector<Key>(index.begin(), index.end()), keys);
  std::reverse(keys.begin(), keys.end());
  EXPECT_EQ(std::vector<Key>(index.rbegin(), index.rend()), keys);
  EXPECT_EQ(index.count({0, "abcd", {1, 2}}), 0u);
  EXPECT_EQ(index.count({0, "abc", {1, 2}}), 1u);
}

TEST(CoverageIndexTest, StableSortOwnsTupleReferences) {
  struct Row {
    int64_t id;
    std::string name;
    unsigned ordinal;
  };
  std::vector<Row> rows{{1, "long", 0},
                        {-1, "", 1},
                        {1, "long", 2},
                        {1, "", 3},
                        {INT64_MIN, "prefix", 4}};
  sortByKey(rows.begin(), rows.end(),
            [](const Row &row) { return std::tie(row.id, row.name); });
  std::vector<unsigned> ordinals;
  for (const auto &row : rows)
    ordinals.push_back(row.ordinal);
  EXPECT_EQ(ordinals, (std::vector<unsigned>{4, 1, 3, 0, 2}));
  rows.clear();
  sortByKey(rows.begin(), rows.end(), [](const Row &row) { return row.id; });
}
