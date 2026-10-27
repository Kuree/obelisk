#include "../lib/RandModelCount.h"

#include "gtest/gtest.h"

#include <set>

using talos::random::BooleanCircuit;
using talos::random::PlanOpcode;
using talos::runtime::ModelCount;
using talos::runtime::ModelCounter;
using ID = BooleanCircuit::ID;

namespace {

class RandModelCountTest : public ::testing::Test {
protected:
  ID bit(unsigned index) {
    return context.input(PlanOpcode::RandomBit, index, index);
  }

  std::vector<ID> numeral(uint64_t value, unsigned width) {
    std::vector<ID> result(width);
    for (unsigned bit = 0; bit != width; ++bit)
      result[bit] = bit < 64 && ((value >> bit) & 1);
    return result;
  }

  const std::vector<ID> &packed(const std::vector<ID> &bits) { return bits; }

  ID equal(const std::vector<ID> &bits, const std::vector<ID> &value) {
    ID match = 1;
    for (unsigned bit = 0; bit != bits.size(); ++bit)
      match = context.conjunction(
          match, context.exclusiveOr(bits[bit], value[bit]) ^ 1);
    return match;
  }

  BooleanCircuit context;
};

TEST_F(RandModelCountTest, CountsAndRanksMatchExhaustiveIntegerArithmetic) {
  std::vector<ID> variables, xBits, yBits;
  for (unsigned index = 0; index != 4; ++index) {
    xBits.push_back(bit(index * 2));
    yBits.push_back(bit(index * 2 + 1));
    variables.push_back(xBits.back());
    variables.push_back(yBits.back());
  }
  // The fixture's small nonlinear relation is independently enumerated
  // into Boolean cubes, preserving the original exact rank/count comparison.
  ID predicate = 0;
  for (unsigned a = 0; a != 16; ++a)
    for (unsigned b = 0; b != 16; ++b)
      if (((a * b) & 15) % 7 == 3 && a >= 2 && b < 14 && a != b)
        predicate = context.disjunction(
            predicate, context.conjunction(equal(xBits, numeral(a, 4)),
                                           equal(yBits, numeral(b, 4))));
  std::set<unsigned> expected;
  for (unsigned a = 0; a != 16; ++a)
    for (unsigned b = 0; b != 16; ++b)
      if (((a * b) & 15) % 7 == 3 && a >= 2 && b < 14 && a != b)
        expected.insert(a | (b << 4));
  ModelCounter counter(context, variables);
  ASSERT_TRUE(counter.build(predicate));
  ASSERT_EQ(counter.count().compare(ModelCount(expected.size())), 0);
  std::set<unsigned> actual;
  for (unsigned rank = 0; rank != expected.size(); ++rank) {
    std::vector<bool> value = counter.assignment(ModelCount(rank));
    unsigned a = 0, b = 0;
    for (unsigned index = 0; index != 4; ++index) {
      a |= unsigned(value[index * 2]) << index;
      b |= unsigned(value[index * 2 + 1]) << index;
    }
    ASSERT_TRUE(actual.insert(a | (b << 4)).second);
  }
  EXPECT_EQ(actual, expected);
}

TEST_F(RandModelCountTest, CountsWideImplicationIncludingItsSingleton) {
  // The LRM 18.5.9 example widened beyond two words: 2^130 unconstrained
  // guard-false combinations and exactly one guard-true combination.
  std::vector<ID> variables;
  for (unsigned index = 0; index != 131; ++index)
    variables.push_back(bit(index));
  ID guard = variables.back();
  std::vector<ID> data(variables.begin(), variables.end() - 1);
  ID predicate =
      context.disjunction(guard ^ 1, equal(packed(data), numeral(0, 130)));
  ModelCounter counter(context, variables);
  ASSERT_TRUE(counter.build(predicate));
  ModelCount expected = ModelCount::powerOfTwo(130);
  expected.add(ModelCount(1));
  ASSERT_EQ(counter.count().compare(expected), 0);
  auto zero = counter.assignment(ModelCount());
  EXPECT_EQ(std::count(zero.begin(), zero.end(), true), 0);
  auto singleton = counter.assignment(ModelCount(1));
  EXPECT_TRUE(singleton.back());
  EXPECT_EQ(std::count(singleton.begin(), singleton.end() - 1, true), 0);
  auto last = counter.assignment(ModelCount::powerOfTwo(130));
  EXPECT_FALSE(last.back());
  EXPECT_EQ(std::count(last.begin(), last.end() - 1, true), 130);
}

TEST_F(RandModelCountTest, FullWideCubeAndRejectionSamplingPreservePrecision) {
  std::vector<ID> variables;
  for (unsigned index = 0; index != 4096; ++index)
    variables.push_back(bit(index));
  ModelCounter counter(context, variables);
  ASSERT_TRUE(counter.build(1));
  ModelCount count = ModelCount::powerOfTwo(4096);
  ASSERT_EQ(counter.count().compare(count), 0);
  ModelCount maximum = count;
  maximum.subtract(ModelCount(1));
  auto last = counter.assignment(maximum);
  EXPECT_EQ(std::count(last.begin(), last.end(), true), 4096);
  talos_rt_random_state_v1 state{71, 15};
  EXPECT_TRUE(ModelCount::randomBelow(ModelCount(1), state).zero());
  EXPECT_EQ(state.state, 71u);
  // A cardinality just above a power of two exercises repeated rejection,
  // carries, leading-word masks, and draws wider than the PCG state.
  ModelCount bound = ModelCount::powerOfTwo(129);
  bound.add(ModelCount(13));
  auto sameState = state;
  for (unsigned index = 0; index != 256; ++index) {
    ModelCount rank = ModelCount::randomBelow(bound, state);
    EXPECT_LT(rank.compare(bound), 0);
    EXPECT_EQ(rank.compare(ModelCount::randomBelow(bound, sameState)), 0);
  }
  EXPECT_EQ(state.state, sameState.state);
}

TEST_F(RandModelCountTest, ProjectionCountsChoicesIndependentlyOfCompletions) {
  std::vector<ID> variables;
  for (unsigned index = 0; index != 131; ++index)
    variables.push_back(bit(index));
  std::vector<ID> data(variables.begin(), variables.end() - 1);
  ID predicate = context.disjunction(variables.back() ^ 1,
                                     equal(packed(data), numeral(0, 130)));
  std::vector<bool> keep(variables.size(), false);
  keep.back() = true;
  for (unsigned selected = 0; selected != 2; ++selected) {
    ModelCounter counter(context, variables);
    ASSERT_TRUE(counter.build(predicate));
    counter.project(keep);
    ASSERT_EQ(counter.count().compare(ModelCount(2)), 0);
    auto value = counter.assignment(ModelCount(selected));
    ASSERT_EQ(value.back(), selected != 0);
    counter.restrict(value, keep);
    EXPECT_EQ(counter.count().compare(selected ? ModelCount(1)
                                               : ModelCount::powerOfTwo(130)),
              0);
    auto completion = counter.assignment(ModelCount());
    EXPECT_EQ(completion.back(), selected != 0);
    EXPECT_EQ(std::count(completion.begin(), completion.end() - 1, true), 0);
  }
}

// An exported decision diagram names both cofactors of each node in its
// gates, so importing it needs no cofactor walk.
TEST_F(RandModelCountTest, DecisionReadsExportedDiagramCofactors) {
  std::vector<ID> variables;
  for (unsigned index = 0; index != 6; ++index)
    variables.push_back(bit(index));
  ID predicate =
      context.disjunction(context.exclusiveOr(variables[0], variables[3]),
                          context.conjunction(variables[1], variables[4] ^ 1));
  predicate = context.conjunction(
      predicate, context.disjunction(variables[2], variables[5]));
  ModelCounter counter(context, variables);
  ASSERT_TRUE(counter.build(predicate));
  BooleanCircuit exported;
  std::vector<ID> inputs;
  for (unsigned index = 0; index != 6; ++index)
    inputs.push_back(exported.input(PlanOpcode::RandomBit, index, index));
  ID root = counter.exportDiagram(exported, inputs);
  std::vector<ID> pending{root};
  std::set<ID> seen;
  unsigned decisions = 0;
  while (!pending.empty()) {
    ID id = pending.back();
    pending.pop_back();
    if (id <= 1 || !seen.insert(id).second)
      continue;
    uint32_t variable = exported.firstVariable(id);
    ID low = 0, high = 0;
    ASSERT_TRUE(exported.decision(id, variable, low, high));
    EXPECT_EQ(low, exported.cofactor(id, variable, false));
    EXPECT_EQ(high, exported.cofactor(id, variable, true));
    ++decisions;
    pending.push_back(low);
    pending.push_back(high);
  }
  EXPECT_GT(decisions, 6u);
  // A gate over arithmetic, not a decision on its first input, is declined.
  ID mixed = exported.exclusiveOr(inputs[0], inputs[1]);
  ID low = 0, high = 0;
  EXPECT_FALSE(
      exported.decision(exported.conjunction(mixed, inputs[2]), 0, low, high));
}

// Counts keep two words inline and move wider values to the heap and back.
TEST_F(RandModelCountTest, CountsCrossInlineStorage) {
  ModelCount half = ModelCount::powerOfTwo(127);
  ModelCount wide = half;
  wide.add(half);
  EXPECT_EQ(wide.compare(ModelCount::powerOfTwo(128)), 0);
  EXPECT_EQ(wide.bitWidth(), 129u);
  EXPECT_EQ(wide.word(2), 1u);
  ModelCount product = wide.multiplied(wide);
  EXPECT_EQ(product.compare(ModelCount::powerOfTwo(256)), 0);
  EXPECT_EQ(product.divided(wide).compare(wide), 0);
  product.subtract(ModelCount::powerOfTwo(256));
  EXPECT_TRUE(product.zero());
  wide.subtract(half);
  EXPECT_EQ(wide.compare(half), 0);
  EXPECT_EQ(wide.word(1), uint64_t{1} << 63);
  EXPECT_EQ(wide.word(2), 0u);
  EXPECT_EQ(half.shifted(70).compare(ModelCount::powerOfTwo(197)), 0);
}

TEST(RandomFlatMapTest, FindsEveryEntryAcrossGrowth) {
  talos::random::FlatMap<uint64_t, uint32_t> map;
  for (uint32_t key = 0; key != 5000; ++key)
    ASSERT_TRUE(map.insert(uint64_t(key) * 7919, key).second);
  for (uint32_t key = 0; key != 5000; ++key) {
    const uint32_t *value = map.find(uint64_t(key) * 7919);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, key);
    auto [existing, inserted] = map.insert(uint64_t(key) * 7919, 0);
    EXPECT_FALSE(inserted);
    EXPECT_EQ(*existing, key);
  }
  EXPECT_FALSE(map.contains(1));
}

} // namespace
