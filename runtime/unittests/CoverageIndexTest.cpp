#include "../../lib/Coverage/CoverageIndex.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace obelisk::coverage::detail;

TEST(CoverageIndexTest, TypeHierarchyUsesDirectEdgesAndHandlesUnorderedChains) {
  struct Type {
    uint64_t id, base;
  };
  std::vector<Type> rows;
  for (uint64_t id = 64; id != 0; --id)
    rows.push_back({id, id - 1});
  rows.push_back({100, 0});
  rows.push_back({101, 1});
  TypeHierarchyIndex index;
  ASSERT_TRUE(index.build(rows));
  EXPECT_TRUE(index.isInstanceOf(64, 1));
  EXPECT_TRUE(index.isInstanceOf(64, 64));
  EXPECT_FALSE(index.isInstanceOf(1, 64));
  EXPECT_TRUE(index.isInstanceOf(101, 1));
  EXPECT_FALSE(index.isInstanceOf(64, 101));
  EXPECT_FALSE(index.isInstanceOf(100, 1));
  EXPECT_FALSE(index.isInstanceOf(0, 1));
  EXPECT_FALSE(index.isInstanceOf(1, 999));
  rows[63].base = 64;
  EXPECT_FALSE(index.build(rows));
  EXPECT_FALSE(index.contains(100));
  rows[63].base = 999;
  EXPECT_FALSE(index.build(rows));
  rows[63].base = 0;
  rows.push_back({1, 0});
  EXPECT_FALSE(index.build(rows));
}

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

TEST(CoverageIndexTest, BoundsAgreeWithOrderedReferenceForPresentAndAbsentIDs) {
  std::mt19937_64 random(67890);
  IndexMap<uint64_t, uint64_t> index;
  std::map<uint64_t, uint64_t> expected;
  EXPECT_EQ(index.lower_bound(0), index.end());
  EXPECT_EQ(index.upper_bound(UINT64_MAX), index.end());
  for (unsigned i = 0; i != 128; ++i) {
    uint64_t key = random();
    index.emplace(key, i);
    expected.emplace(key, i);
  }
  const auto &view = index;
  auto check = [&](uint64_t key) {
    auto lower = expected.lower_bound(key), upper = expected.upper_bound(key);
    for (bool after : {false, true}) {
      auto actual = after ? view.upper_bound(key) : view.lower_bound(key);
      auto wanted = after ? upper : lower;
      EXPECT_EQ(actual == view.end(), wanted == expected.end());
      if (actual != view.end() && wanted != expected.end())
        EXPECT_EQ(actual->first, wanted->first);
      auto mutableResult =
          after ? index.upper_bound(key) : index.lower_bound(key);
      IndexMap<uint64_t, uint64_t>::const_iterator constResult = mutableResult;
      EXPECT_EQ(constResult, actual);
    }
  };
  check(0);
  check(UINT64_MAX);
  for (const auto &[key, value] : expected) {
    check(key);
    check(key - 1);
    check(key + 1);
  }
}

TEST(CoverageIndexTest, BoundsHandleSkippedPrefixesAndVariableLengthKeys) {
  std::string prefix(1024, 'm');
  IndexMap<std::string, unsigned> index;
  std::map<std::string, unsigned> expected;
  for (const std::string &key :
       {prefix + "zz", std::string("z"), prefix, prefix + "ab", std::string(),
        std::string("a"), prefix + "abc"}) {
    unsigned ordinal = expected.size();
    index.emplace(key, ordinal);
    expected.emplace(key, ordinal);
  }
  for (const std::string &key :
       {std::string(), std::string("b"), std::string("l"), prefix, prefix + "a",
        prefix + "ab", prefix + "abb", prefix + "abcd", prefix + "zy",
        prefix + "zzz", std::string("n"), std::string("zz")}) {
    for (bool after : {false, true}) {
      auto actual = after ? index.upper_bound(key) : index.lower_bound(key);
      auto wanted =
          after ? expected.upper_bound(key) : expected.lower_bound(key);
      EXPECT_EQ(actual == index.end(), wanted == expected.end());
      if (actual != index.end() && wanted != expected.end())
        EXPECT_EQ(actual->first, wanted->first);
    }
  }
}

TEST(CoverageIndexTest,
     InsertionBatchesKeepImmovableHistoryAndRestoreOrdering) {
  struct Value {
    int number;
    explicit Value(int number) : number(number) {}
    Value(const Value &) = delete;
    Value(Value &&) = delete;
  };
  IndexMap<uint64_t, Value> index;
  index.emplace(8, 8);
  index.emplace(16, 16);
  Value *first = &index.at(8);
  for (unsigned repeat = 0; repeat != 4; ++repeat) {
    decltype(index)::InsertionBatch batch(index);
    for (uint64_t key : {32, 1, 12, 24, 0, 17, 48, 9, 64})
      EXPECT_TRUE(batch.emplace(key, key).second);
    EXPECT_FALSE(batch.emplace(8, -1).second);
    EXPECT_EQ(&index.at(8), first);
    EXPECT_EQ(first->number, 8);
  }
  EXPECT_EQ(index.size(), 2u);
  std::vector<uint64_t> keys;
  for (const auto &entry : index)
    keys.push_back(entry.first);
  EXPECT_EQ(keys, (std::vector<uint64_t>{8, 16}));
  EXPECT_EQ(index.find(12), index.end());
  {
    decltype(index)::InsertionBatch batch(index);
    EXPECT_TRUE(batch.emplace(12, 12).second);
    EXPECT_TRUE(batch.emplace(1, 1).second);
    batch.commit();
  }
  EXPECT_EQ(index.size(), 4u);
  EXPECT_EQ(&index.at(8), first);
  EXPECT_EQ(index.at(12).number, 12);
  IndexMap<uint64_t, uint64_t> empty;
  {
    decltype(empty)::InsertionBatch batch(empty);
    batch.emplace(1, 1);
    batch.emplace(0, 0);
  }
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.begin(), empty.end());
  EXPECT_EQ(empty.find(1), empty.end());
  EXPECT_TRUE(empty.emplace(2, 2).second);
  EXPECT_EQ(empty.begin()->first, 2u);
}

TEST(CoverageIndexTest, VariableWidthBatchesRestorePrefixSplitsAndNestedMaps) {
  using Tuples = IndexMap<std::vector<uint64_t>, uint64_t>;
  IndexMap<uint64_t, Tuples> crosses;
  const std::vector<uint64_t> original{10, 1};
  crosses[8].emplace(original, 11);
  auto *counter = &crosses.at(8).at(original);
  {
    decltype(crosses)::InsertionBatch outer(crosses);
    auto existing = outer.emplace(8).first;
    auto inserted = outer.emplace(16).first;
    std::vector<Tuples::InsertionBatch> children;
    // Reallocation moves a live batch; it must retain its rollback journal.
    children.emplace_back(existing->second);
    children.back().emplace(std::vector<uint64_t>{}, 1);
    children.back().emplace(std::vector<uint64_t>{10}, 2);
    children.back().emplace(std::vector<uint64_t>{10, 1, 1}, 3);
    children.back().emplace(std::vector<uint64_t>{1, UINT64_MAX}, 4);
    children.emplace_back(inserted->second);
    children.back().emplace(std::vector<uint64_t>{0, 0}, 5);
  }
  ASSERT_EQ(crosses.size(), 1u);
  ASSERT_EQ(crosses.at(8).size(), 1u);
  EXPECT_EQ(&crosses.at(8).at(original), counter);
  EXPECT_EQ(*counter, 11u);
  auto iterator = crosses.at(8).end();
  --iterator;
  EXPECT_EQ(iterator->first, original);
  EXPECT_EQ(crosses.find(16), crosses.end());
}

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
TEST(CoverageIndexTest, FailedBatchConstructionCanContinueAndThenRollBack) {
  struct Value {
    int number;
    explicit Value(int number) : number(number) {
      if (number < 0)
        throw std::runtime_error("batch insertion failed");
    }
  };
  IndexMap<uint64_t, Value> index;
  index.emplace(8, 8);
  auto *original = &index.at(8);
  {
    decltype(index)::InsertionBatch batch(index);
    batch.emplace(16, 16);
    EXPECT_THROW(batch.emplace(12, -1), std::runtime_error);
    EXPECT_THROW(batch.emplace(0, -1), std::runtime_error);
    EXPECT_NO_THROW(EXPECT_FALSE(batch.emplace(8, -1).second));
    batch.emplace(1, 1);
  }
  ASSERT_EQ(index.size(), 1u);
  EXPECT_EQ(&index.at(8), original);
  EXPECT_EQ(original->number, 8);
  EXPECT_EQ(index.begin()->first, 8u);
  EXPECT_EQ(index.find(12), index.end());
  EXPECT_TRUE(index.emplace(12, 12).second);
  IndexMap<uint64_t, Value> empty;
  {
    decltype(empty)::InsertionBatch batch(empty);
    EXPECT_THROW(batch.emplace(1, -1), std::runtime_error);
    batch.emplace(2, 2);
  }
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.begin(), empty.end());
}
#endif

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
