#include "../lib/RandModelCount.h"

#include "gtest/gtest.h"

#include <set>

using obelisk::random::BooleanCircuit;
using obelisk::random::PlanOpcode;
using obelisk::runtime::ModelCount;
using obelisk::runtime::ModelCounter;
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
  obelisk_rt_random_state_v1 state{71, 15};
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

} // namespace
