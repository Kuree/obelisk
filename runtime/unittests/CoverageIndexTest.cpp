#include "../../lib/Coverage/CoverageIndex.h"
#include "../../lib/Coverage/CoverageDecisionIndex.h"
#include "../../lib/Coverage/CoverageTransitionIndex.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace obelisk::coverage::detail;

TEST(CoverageTransitionIndexTest,
     FixedLanguagesAgreeWithExhaustiveFiniteWordOracle) {
  using obelisk::coverage::TransitionRepetitionKind;
  std::mt19937 random(1947);
  for (uint32_t trial = 0; trial != 32; ++trial) {
    DecisionIndex values(2);
    TransitionIndex index(values, 2, 1);
    auto label = [&](uint8_t mask) {
      DecisionIndex::Root result = 0;
      for (uint64_t value = 0; value != 4; ++value)
        if (mask & (1 << value))
          result = values.unite(result, values.cube({value}, {0}));
      return result;
    };
    std::array<uint8_t, 3> ordinary;
    std::array<std::array<uint8_t, 2>, 3> exclusions;
    std::vector<TransitionIndex::Step> steps;
    for (auto &mask : ordinary) {
      mask = random() % 16;
      steps.push_back({label(mask), 1, 1, TransitionRepetitionKind::Once});
    }
    for (auto &word : exclusions) {
      word = {static_cast<uint8_t>(random() % 16),
              static_cast<uint8_t>(random() % 16)};
      index.addExclusion({label(word[0]), label(word[1])});
    }
    uint8_t expectedTerminals = 0;
    for (uint32_t encoded = 0; encoded != 64; ++encoded) {
      std::array<uint8_t, 3> word{static_cast<uint8_t>(encoded & 3),
                                  static_cast<uint8_t>((encoded >> 2) & 3),
                                  static_cast<uint8_t>(encoded >> 4)};
      bool matches = true, excluded = false;
      for (size_t ordinal = 0; ordinal != word.size(); ++ordinal)
        matches &= bool(ordinary[ordinal] & (1 << word[ordinal]));
      for (const auto &removed : exclusions)
        for (size_t offset = 0; offset != 2; ++offset)
          excluded |= (removed[0] & (1 << word[offset])) &&
                      (removed[1] & (1 << word[offset + 1]));
      if (matches && !excluded)
        expectedTerminals |= 1 << word.back();
    }
    EXPECT_EQ(index.hasAcceptedWord(steps), expectedTerminals != 0)
        << "trial=" << trial;
    auto terminals = index.terminalValues(steps);
    for (uint64_t value = 0; value != 4; ++value)
      EXPECT_EQ(values.intersect(terminals, values.cube({value}, {0})) != 0,
                bool(expectedTerminals & (1 << value)))
          << "trial=" << trial << ", terminal=" << value;
    EXPECT_FALSE(index.exhausted());
  }
}

TEST(CoverageTransitionIndexTest, NonconsecutiveGapsMayMatchTheFollowingStep) {
  using obelisk::coverage::TransitionRepetitionKind;
  for (auto repetition : {TransitionRepetitionKind::Goto,
                          TransitionRepetitionKind::Nonconsecutive}) {
    DecisionIndex values(2);
    TransitionIndex index(values, 2, 1);
    auto zero = values.cube({0}, {0});
    auto terminals = values.interval({1}, {3});
    index.addExclusion({zero, values.cube({1}, {0})});
    // 0,2,1 completes a legal nonconsecutive sequence ending in 1: its 2
    // may match either the suffix gap or the following step. Goto requires
    // that following step to be adjacent to 0, so it loses terminal 1.
    EXPECT_EQ(index.terminalValues(
                  {{zero, 1, 1, repetition},
                   {terminals, 1, 1, TransitionRepetitionKind::Once}}),
              repetition == TransitionRepetitionKind::Nonconsecutive
                  ? terminals
                  : values.interval({2}, {3}));
    EXPECT_FALSE(index.exhausted());
  }
}

TEST(CoverageTransitionIndexTest,
     ManyDisjointExclusionsRespectLinearWorkBudget) {
  using obelisk::coverage::TransitionRepetitionKind;
  constexpr uint32_t width = 8, count = 64;
  DecisionIndex values(width);
  TransitionIndex index(values, width, 1, count * width * 8);
  auto terminal = values.cube({127}, {0});
  std::vector<TransitionIndex::Step> ordinary;
  for (uint32_t value = 0; value != count; ++value) {
    auto start = values.cube({value}, {0});
    index.addExclusion({start, terminal});
    ordinary.push_back({start, 1, 1, TransitionRepetitionKind::Once});
  }
  // Each sample activates a different exclusion prefix. The ordinary word
  // avoids every terminal, and its proof must fit a budget linear in the
  // word count and width rather than rescan all exclusions for every sample.
  ordinary.push_back(
      {values.cube({count}, {0}), 1, 1, TransitionRepetitionKind::Once});
  EXPECT_TRUE(index.hasAcceptedWord(ordinary));
  EXPECT_FALSE(index.exhausted());
}

TEST(CoverageTransitionIndexTest,
     GapRepetitionsRespectExclusionsAndKnownSampleDomain) {
  using obelisk::coverage::TransitionRepetitionKind;
  for (bool fourState : {false, true}) {
    for (auto repetition : {TransitionRepetitionKind::Goto,
                            TransitionRepetitionKind::Nonconsecutive}) {
      DecisionIndex values(3);
      auto known = values.interval({0}, {3});
      TransitionIndex index(values, 3, fourState ? 1 : known);
      index.addExclusion({known, known});
      // A two-state full-domain repetition has no possible separating gap.
      // A four-state X/Z sample can separate its two known occurrences.
      EXPECT_EQ(index.hasAcceptedWord({{known, 2, 2, repetition}}), fourState);
      EXPECT_FALSE(index.exhausted());
    }
  }
  for (bool binaryDomain : {false, true}) {
    for (auto repetition : {TransitionRepetitionKind::Goto,
                            TransitionRepetitionKind::Nonconsecutive}) {
      DecisionIndex values(2);
      auto zero = values.cube({0}, {0}), one = values.cube({1}, {0});
      TransitionIndex index(values, 2,
                            binaryDomain ? values.interval({0}, {1}) : 1);
      index.addExclusion({zero, one});
      // Goto requires adjacency after its final occurrence. Nonconsecutive
      // can avoid that pair with a value outside both step sets, when one
      // exists in the sample domain.
      EXPECT_EQ(
          index.hasAcceptedWord({{zero, 1, 1, repetition},
                                 {one, 1, 1, TransitionRepetitionKind::Once}}),
          !binaryDomain &&
              repetition == TransitionRepetitionKind::Nonconsecutive);
      EXPECT_FALSE(index.exhausted());
    }
  }
}

TEST(CoverageDecisionIndexTest, SharedSetOperationsAgreeWithSmallValueOracle) {
  DecisionIndex index(4);
  auto range = index.interval({1}, {10});
  auto odd = index.cube({1}, {14});
  auto removed = index.cube({2}, {5});
  auto selected = index.subtract(index.intersect(range, odd), removed);
  auto united = index.unite(range, odd);
  uint64_t selectedCount = 0, unionCount = 0;
  for (uint64_t value = 0; value != 16; ++value) {
    bool inRange = value >= 1 && value <= 10;
    bool inOdd = value & 1;
    bool inRemoved = (value & ~uint64_t{5}) == 2;
    auto singleton = index.cube({value}, {0});
    EXPECT_EQ(index.intersect(selected, singleton) != 0,
              inRange && inOdd && !inRemoved);
    EXPECT_EQ(index.intersect(united, singleton) != 0, inRange || inOdd);
    selectedCount += inRange && inOdd && !inRemoved;
    unionCount += inRange || inOdd;
  }
  for (uint64_t cap : {uint64_t{1}, uint64_t{3}, uint64_t{16}}) {
    EXPECT_EQ(index.cardinality(selected, cap), std::min(cap, selectedCount));
    EXPECT_EQ(index.cardinality(united, cap), std::min(cap, unionCount));
  }
  EXPECT_EQ(index.subtract(united, united), 0u);
  EXPECT_FALSE(index.exhausted());
}

TEST(CoverageDecisionIndexTest, WideCardinalitySaturatesAndExpansionIsBounded) {
  DecisionIndex index(65);
  auto all = index.interval({0, 0}, {1, UINT64_MAX});
  auto one = index.interval({1, 0}, {1, 0});
  EXPECT_EQ(index.cardinality(all, UINT64_MAX), UINT64_MAX);
  EXPECT_EQ(index.cardinality(one, UINT64_MAX), 1u);
  EXPECT_EQ(index.cardinality(index.subtract(all, one), 7), 7u);
  EXPECT_EQ(index.cardinality(index.intersect(all, one), 7), 1u);
  EXPECT_FALSE(index.exhausted());
  DecisionIndex limited(65, 8);
  limited.cube({0, 0}, {0, 0});
  EXPECT_TRUE(limited.exhausted());
}

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
