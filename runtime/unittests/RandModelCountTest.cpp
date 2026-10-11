#ifdef OBELISK_RUNTIME_Z3
#include "../lib/RandModelCount.h"

#include "gtest/gtest.h"

#include <set>

using obelisk::runtime::ModelCount;
using obelisk::runtime::ModelCounter;

namespace {

class RandModelCountTest : public ::testing::Test {
protected:
  void SetUp() override {
    Z3_config config = Z3_mk_config();
    context = Z3_mk_context(config);
    Z3_del_config(config);
  }

  void TearDown() override { Z3_del_context(context); }

  Z3_ast bit(unsigned index) {
    return Z3_mk_const(context, Z3_mk_int_symbol(context, index),
                       Z3_mk_bool_sort(context));
  }

  Z3_ast numeral(uint64_t value, unsigned width) {
    return Z3_mk_unsigned_int64(context, value, Z3_mk_bv_sort(context, width));
  }

  Z3_ast packed(const std::vector<Z3_ast> &bits) {
    Z3_ast result = nullptr;
    for (size_t index = bits.size(); index-- != 0;) {
      Z3_ast value =
          Z3_mk_ite(context, bits[index], numeral(1, 1), numeral(0, 1));
      result = result ? Z3_mk_concat(context, result, value) : value;
    }
    return result;
  }

  Z3_context context = nullptr;
};

TEST_F(RandModelCountTest, CountsAndRanksMatchExhaustiveIntegerArithmetic) {
  std::vector<Z3_ast> variables, xBits, yBits;
  for (unsigned index = 0; index != 4; ++index) {
    xBits.push_back(bit(index * 2));
    yBits.push_back(bit(index * 2 + 1));
    variables.push_back(xBits.back());
    variables.push_back(yBits.back());
  }
  Z3_ast x = packed(xBits), y = packed(yBits);
  Z3_ast product = Z3_mk_bvmul(context, x, y);
  Z3_ast predicates[] = {Z3_mk_eq(context,
                                  Z3_mk_bvurem(context, product, numeral(7, 4)),
                                  numeral(3, 4)),
                         Z3_mk_bvuge(context, x, numeral(2, 4)),
                         Z3_mk_bvult(context, y, numeral(14, 4)),
                         Z3_mk_not(context, Z3_mk_eq(context, x, y))};
  std::set<unsigned> expected;
  for (unsigned a = 0; a != 16; ++a)
    for (unsigned b = 0; b != 16; ++b)
      if (((a * b) & 15) % 7 == 3 && a >= 2 && b < 14 && a != b)
        expected.insert(a | (b << 4));
  ModelCounter counter(context, variables);
  ASSERT_TRUE(counter.build(Z3_mk_and(context, 4, predicates)));
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
  std::vector<Z3_ast> variables;
  for (unsigned index = 0; index != 131; ++index)
    variables.push_back(bit(index));
  Z3_ast guard = variables.back();
  std::vector<Z3_ast> data(variables.begin(), variables.end() - 1);
  Z3_ast predicate = Z3_mk_implies(
      context, guard, Z3_mk_eq(context, packed(data), numeral(0, 130)));
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
  std::vector<Z3_ast> variables;
  for (unsigned index = 0; index != 4096; ++index)
    variables.push_back(bit(index));
  ModelCounter counter(context, variables);
  ASSERT_TRUE(counter.build(Z3_mk_true(context)));
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
  std::vector<Z3_ast> variables;
  for (unsigned index = 0; index != 131; ++index)
    variables.push_back(bit(index));
  std::vector<Z3_ast> data(variables.begin(), variables.end() - 1);
  Z3_ast predicate =
      Z3_mk_implies(context, variables.back(),
                    Z3_mk_eq(context, packed(data), numeral(0, 130)));
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
#endif
