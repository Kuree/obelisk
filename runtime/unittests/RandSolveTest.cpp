#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

void append16(std::vector<uint8_t> &bytes, uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void append32(std::vector<uint8_t> &bytes, uint32_t value) {
  for (unsigned index = 0; index != 4; ++index)
    bytes.push_back(static_cast<uint8_t>(value >> (index * 8)));
}

void append64(std::vector<uint8_t> &bytes, uint64_t value) {
  for (unsigned index = 0; index != 8; ++index)
    bytes.push_back(static_cast<uint8_t>(value >> (index * 8)));
}

struct EncodedInstruction {
  uint8_t opcode;
  uint8_t width;
  uint8_t flags = 0;
  uint32_t operand = 0;
  uint64_t immediate = 0;
};

struct EncodedSolveBeforeEdge {
  uint64_t beforeMask;
  uint64_t afterMask;
  uint32_t constraintBlock = OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
};

struct EncodedDistRange {
  uint32_t group;
  uint32_t constraintBlock;
  uint32_t targetOffset;
  uint16_t width;
  uint64_t lower;
  uint64_t cardinality;
  uint64_t coefficient;
  uint32_t weightCapture;
  uint32_t flags = 0;
};

struct EncodedDomainPattern {
  uint32_t group;
  uint32_t targetOffset;
  uint16_t width;
  uint64_t mask;
  uint64_t value;
};

std::vector<uint8_t>
program(uint32_t width, uint32_t captures,
        std::initializer_list<EncodedInstruction> instructions,
        bool hasSoft = false,
        std::initializer_list<EncodedSolveBeforeEdge> solveEdges = {},
        std::initializer_list<EncodedDistRange> distRanges = {},
        std::initializer_list<EncodedDomainPattern> domainPatterns = {}) {
  std::vector<uint8_t> bytes;
  append32(bytes, OBELISK_RT_RANDOM_PROGRAM_MAGIC);
  append16(bytes, OBELISK_RT_RANDOM_PROGRAM_VERSION);
  append16(bytes, OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE);
  append32(bytes, width);
  append32(bytes, static_cast<uint32_t>(instructions.size()));
  append32(bytes, captures);
  uint32_t flags = hasSoft ? OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT : 0;
  if (solveEdges.size() != 0)
    flags |= OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE;
  if (distRanges.size() != 0)
    flags |= OBELISK_RT_RANDOM_PROGRAM_HAS_DIST;
  if (domainPatterns.size() != 0)
    flags |= OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS;
  append32(bytes, flags);
  for (const EncodedInstruction &instruction : instructions) {
    bytes.push_back(instruction.opcode);
    bytes.push_back(instruction.width);
    bytes.push_back(instruction.flags);
    bytes.push_back(0);
    append32(bytes, instruction.operand);
    append64(bytes, instruction.immediate);
  }
  if (solveEdges.size() != 0) {
    append32(bytes, static_cast<uint32_t>(solveEdges.size()));
    for (const EncodedSolveBeforeEdge &edge : solveEdges) {
      append64(bytes, edge.beforeMask);
      append64(bytes, edge.afterMask);
      append32(bytes, edge.constraintBlock);
      append32(bytes, 0);
    }
  }
  if (distRanges.size() != 0) {
    uint32_t groupCount = 0;
    for (const EncodedDistRange &range : distRanges)
      groupCount = std::max(groupCount, range.group + 1);
    append32(bytes, groupCount);
    append32(bytes, static_cast<uint32_t>(distRanges.size()));
    for (const EncodedDistRange &range : distRanges) {
      append32(bytes, range.group);
      append32(bytes, range.constraintBlock);
      append32(bytes, range.targetOffset);
      append16(bytes, range.width);
      append16(bytes, 0);
      append64(bytes, range.lower);
      append64(bytes, range.cardinality);
      append64(bytes, range.coefficient);
      append32(bytes, range.weightCapture);
      append32(bytes, range.flags);
    }
  }
  if (domainPatterns.size() != 0) {
    uint32_t groupCount = 0;
    for (const EncodedDomainPattern &pattern : domainPatterns)
      groupCount = std::max(groupCount, pattern.group + 1);
    append32(bytes, groupCount);
    append32(bytes, static_cast<uint32_t>(domainPatterns.size()));
    for (const EncodedDomainPattern &pattern : domainPatterns) {
      append32(bytes, pattern.group);
      append32(bytes, pattern.targetOffset);
      append16(bytes, pattern.width);
      append16(bytes, 0);
      append32(bytes, 0);
      append64(bytes, pattern.mask);
      append64(bytes, pattern.value);
    }
  }
  return bytes;
}

class RandSolveTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  }

  void TearDown() override {
    if (context)
      obelisk_rt_v1_context_destroy(context);
  }

  obelisk_rt_context *context = nullptr;
};

TEST_F(RandSolveTest, SolvesHardConstraintWithDynamicCapture) {
  std::vector<uint8_t> bytes =
      program(4, 1,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 4, 0, 0},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1}});
  uint64_t capture = 7;
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 0,
                                       16, &capture, 1, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 7u);
}

TEST_F(RandSolveTest, TraversesSparseFiniteDomainInsteadOfUnderlyingBits) {
  std::vector<uint8_t> bytes = program(
      32, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 32},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 32, 0, 0, UINT32_C(0xdeadbeef)},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
      false, {}, {},
      {{0, 0, 32, UINT32_MAX, 7},
       {0, 0, 32, UINT32_MAX, UINT32_C(0xdeadbeef)},
       {0, 0, 32, UINT32_MAX, UINT32_C(0xf00d)}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 7,
                                       3, nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, UINT32_C(0xdeadbeef));
}

TEST_F(RandSolveTest, MaterializesTaggedUnionPatternDomain) {
  // Six packed bits contain a four-bit payload and a two-bit tag. Arm zero has
  // a two-bit payload, arm one a four-bit payload, and arm two is void.
  std::vector<uint8_t> bytes =
      program(6, 0,
              {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
              false, {}, {},
              {{0, 0, 6, UINT64_C(0x3c), 0},
               {0, 0, 6, UINT64_C(0x30), UINT64_C(0x10)},
               {0, 0, 6, UINT64_C(0x3f), UINT64_C(0x20)}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 63,
                                       1, nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  bool arm0 = (assignment & UINT64_C(0x3c)) == 0;
  bool arm1 = (assignment & UINT64_C(0x30)) == UINT64_C(0x10);
  bool arm2 = (assignment & UINT64_C(0x3f)) == UINT64_C(0x20);
  EXPECT_TRUE(arm0 || arm1 || arm2);
}

TEST_F(RandSolveTest, TraversesCartesianProductOfFiniteSubdomains) {
  // Two disjoint two-bit enum subfields and one ordinary bit form an
  // eight-element semantic domain. Exercise the complete mixed-radix walk,
  // including the final assignment in that domain.
  std::vector<uint8_t> bytes = program(
      5, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 5},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 5, 0, 0, 30},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
      false, {}, {},
      {{0, 0, 2, 3, 1}, {0, 0, 2, 3, 2},
       {1, 2, 2, 3, 0}, {1, 2, 2, 3, 3}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 0,
                                       8, nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 30u);
}

TEST_F(RandSolveTest, SamplesDistAcrossSparseFiniteDomain) {
  // The first dist range contains the two legal values 1 and 4, despite
  // spanning five underlying encodings. A per-range weight of six therefore
  // gives each value the same mass as the singleton value 9 with weight three.
  std::vector<uint8_t> bytes =
      program(4, 2,
              {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
              false, {},
              {{0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 4, 0, 5, 1, 0},
               {0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 4, 9, 1, 2, 1}},
              {{0, 0, 4, 15, 1}, {0, 0, 4, 15, 4}, {0, 0, 4, 15, 9}});
  uint64_t captures[] = {6, 3};
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 313, 41);
  unsigned counts[3] = {};
  for (unsigned iteration = 0; iteration != 6000; ++iteration) {
    uint64_t assignment = 0;
    uint64_t nextState = 0;
    uint32_t success = 0;
    ASSERT_EQ(obelisk_rt_v1_random_solve_modes_state(
                  context, bytes.data(), bytes.size(), 0, 15, 0, 3, state.state,
                  state.increment, captures, 2, &assignment, &success,
                  &nextState),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    if (assignment == 1)
      ++counts[0];
    else if (assignment == 4)
      ++counts[1];
    else if (assignment == 9)
      ++counts[2];
    else
      FAIL() << "out-of-domain assignment " << assignment;
    state.state = nextState;
  }
  for (unsigned count : counts) {
    EXPECT_GT(count, 1800u);
    EXPECT_LT(count, 2200u);
  }
}

TEST_F(RandSolveTest, RejectsInvalidFixedDomainValueAndPartialMutation) {
  std::vector<uint8_t> bytes =
      program(4, 0,
              {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
              false, {}, {}, {{0, 0, 4, 15, 1}, {0, 0, 4, 15, 9}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(context, bytes.data(),
                                             bytes.size(), 4, 0, 0, 2, nullptr,
                                             0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 0u);
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(context, bytes.data(),
                                             bytes.size(), 1, 3, 0, 2, nullptr,
                                             0, &assignment, &success),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RandSolveTest, RejectsMalformedFiniteDomainMetadata) {
  auto rejects = [&](std::vector<uint8_t> bytes) {
    uint64_t assignment = 0;
    uint32_t success = 0;
    EXPECT_EQ(obelisk_rt_v1_random_solve(
                  context, bytes.data(), bytes.size(), 0, 4, nullptr, 0,
                  &assignment, &success),
              OBELISK_RT_INVALID_ARGUMENT);
  };
  auto truth = std::initializer_list<EncodedInstruction>{
      {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
      {OBELISK_RT_RANDOM_END_HARD_V1, 1}};

  rejects(program(4, 0, truth, false, {}, {},
                  {{0, 0, 2, 3, 1}, {1, 1, 2, 3, 2}}));
  rejects(program(4, 0, truth, false, {}, {},
                  {{0, 0, 4, 8, 0}, {0, 0, 4, 4, 0}}));
  rejects(program(64, 0, truth, false, {}, {}, {{0, 0, 64, 0, 0}}));

  std::vector<uint8_t> truncated =
      program(4, 0, truth, false, {}, {}, {{0, 0, 4, 15, 1}});
  truncated.pop_back();
  rejects(std::move(truncated));
}

TEST_F(RandSolveTest, PreservesFixedWidthSignedArithmetic) {
  // In four signed bits, 7 + 1 wraps to -8. This checks that arithmetic is
  // truncated before the signed comparison rather than widened as host C++.
  std::vector<uint8_t> bytes = program(
      4, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 1},
       {OBELISK_RT_RANDOM_ADD_V1, 4, OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 0},
       {OBELISK_RT_RANDOM_LT_V1, 1, OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 0,
                                       16, nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 7u);
}

TEST_F(RandSolveTest, ResizesSignedOperandsBeforeEvaluation) {
  // Candidate one is true only when width-changing operators sign-extend their
  // inputs before evaluating, matching the compiler's SMT and MLIR paths.
  std::vector<uint8_t> bytes = program(
      1, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0},
       {OBELISK_RT_RANDOM_SELECT_V1, 4, OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 15},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1},
       {OBELISK_RT_RANDOM_POS_V1, 4, OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 15},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1},
       {OBELISK_RT_RANDOM_NEG_V1, 4, OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 1},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1},
       {OBELISK_RT_RANDOM_BIT_NOT_V1, 4, OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 0},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 3},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 2},
       {OBELISK_RT_RANDOM_MUL_V1, 4, OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 2},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 0,
                                       2, nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 1u);
}

TEST_F(RandSolveTest, EvaluatesDivisionShiftsAndPowerAtFixedWidth) {
  std::vector<uint8_t> bytes = program(
      1, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 13},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 3},
       {OBELISK_RT_RANDOM_DIV_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 4},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 13},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 3},
       {OBELISK_RT_RANDOM_MOD_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 1},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 9},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 2},
       {OBELISK_RT_RANDOM_DIV_V1, 4, OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 13},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 9},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 2},
       {OBELISK_RT_RANDOM_MOD_V1, 4, OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 15},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 3},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 8, 0, 0, 1},
       {OBELISK_RT_RANDOM_SHIFT_LEFT_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 6},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 12},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 8, 0, 0, 2},
       {OBELISK_RT_RANDOM_SHIFT_RIGHT_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 3},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 8},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 8, 0, 0, 2},
       {OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1, 4,
        OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 14},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 8},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 8, 0, 0, 7},
       {OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1, 4,
        OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 15},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 3},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 8, 0, 0, 3},
       {OBELISK_RT_RANDOM_POWER_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 11},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 8, 0, 0, 0},
       {OBELISK_RT_RANDOM_POWER_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 1},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 0,
                                       2, nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 0u);
}

TEST_F(RandSolveTest, PrefersSoftSolution) {
  std::vector<uint8_t> bytes =
      program(4, 2,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 4, 0, 0},
               {OBELISK_RT_RANDOM_LE_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1},
               {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 4, 0, 1},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_SOFT_V1, 1}},
              true);
  uint64_t captures[] = {3, 2};
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 3,
                                       16, captures, 2, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 2u);
}

TEST_F(RandSolveTest, DropsUnsatisfiableSoftOnlyAfterCompleteSearch) {
  std::vector<uint8_t> bytes =
      program(4, 2,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 4, 0, 0},
               {OBELISK_RT_RANDOM_LE_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1},
               {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 4, 0, 1},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_SOFT_V1, 1}},
              true);
  uint64_t captures[] = {3, 7};
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 4,
                                       16, captures, 2, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 0u);

  assignment = 99;
  success = 1;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 4,
                                       4, captures, 2, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 0u);
  EXPECT_EQ(assignment, 0u);
}

TEST_F(RandSolveTest, MaskedSolvePreservesFixedAssignmentBits) {
  std::vector<uint8_t> bytes =
      program(4, 0,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 15},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_masked(context, bytes.data(),
                                              bytes.size(), 10, 5, 4, nullptr,
                                              0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 15u);
  EXPECT_EQ(assignment & ~uint64_t{5}, uint64_t{10});
}

TEST_F(RandSolveTest, MaskedSoftSearchUsesMutableDomainSize) {
  std::vector<uint8_t> bytes =
      program(4, 0,
              {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1},
               {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 0},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_SOFT_V1, 1}},
              true);
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_masked(context, bytes.data(),
                                              bytes.size(), 10, 1, 2, nullptr,
                                              0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 10u);
}

TEST_F(RandSolveTest, ConstraintModesDisableOnlySelectedBlocks) {
  std::vector<uint8_t> bytes = program(
      4, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 3},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 7},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, 1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 5},
       {OBELISK_RT_RANDOM_NE_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 5, UINT64_MAX, 0, 16,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 0u);

  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 5, UINT64_MAX, 1, 16,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 7u);

  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 5, UINT64_MAX, 2, 16,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 3u);

  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 5, UINT64_MAX, 3, 16,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 6u);
}

TEST_F(RandSolveTest, SamplesResidualCandidatesByDistWeight) {
  std::vector<uint8_t> bytes = program(
      1, 2,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
      false, {},
      {{0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 1, 0, 1, 1, 0},
       {0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 1, 1, 1, 1, 1}});
  uint64_t captures[] = {1, 3};
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 123, 17);
  unsigned ones = 0;
  for (unsigned iteration = 0; iteration != 4096; ++iteration) {
    uint64_t assignment = 0;
    uint64_t nextState = 0;
    uint32_t success = 0;
    ASSERT_EQ(obelisk_rt_v1_random_solve_modes_state(
                  context, bytes.data(), bytes.size(), 0, 1, 0, 2,
                  state.state, state.increment, captures, 2, &assignment,
                  &success, &nextState),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    ones += assignment == 1;
    state.state = nextState;
  }
  EXPECT_GT(ones, 2900u);
  EXPECT_LT(ones, 3250u);
}

TEST_F(RandSolveTest, DistTargetWeightIgnoresCompanionSolutionMultiplicity) {
  // x is bit 0 and y is bit 1. The hard constraint allows both y values when
  // x is zero, but only y=0 when x is one. Equal x weights must still produce
  // equal x probability rather than weighting each complete assignment.
  std::vector<uint8_t> bytes = program(
      2, 1,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 0, 0},
       {OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 1, 0},
       {OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1},
       {OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
      false, {},
      {{0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 1, 0, 2, 1, 0}});
  uint64_t capture = 1;
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 123, 17);
  unsigned ones = 0;
  for (unsigned iteration = 0; iteration != 4096; ++iteration) {
    uint64_t assignment = 0;
    uint64_t nextState = 0;
    uint32_t success = 0;
    ASSERT_EQ(obelisk_rt_v1_random_solve_modes_state(
                  context, bytes.data(), bytes.size(), 0, 3, 0, 4, state.state,
                  state.increment, &capture, 1, &assignment, &success,
                  &nextState),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    ones += assignment & 1;
    state.state = nextState;
  }
  EXPECT_GT(ones, 1850u);
  EXPECT_LT(ones, 2250u);
}

TEST_F(RandSolveTest, OverlappingDistRangesAccumulatePerValueWeight) {
  std::vector<uint8_t> bytes = program(
      2, 1,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
      false, {},
      {{0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 2, 0, 3, 1, 0},
       {0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 2, 1, 3, 1, 0}});
  uint64_t capture = 1;
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 71, 29);
  unsigned middle = 0;
  for (unsigned iteration = 0; iteration != 4096; ++iteration) {
    uint64_t assignment = 0;
    uint64_t nextState = 0;
    uint32_t success = 0;
    ASSERT_EQ(obelisk_rt_v1_random_solve_modes_state(
                  context, bytes.data(), bytes.size(), 0, 3, 0, 4, state.state,
                  state.increment, &capture, 1, &assignment, &success,
                  &nextState),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    middle += assignment == 1 || assignment == 2;
    state.state = nextState;
  }
  EXPECT_GT(middle, 2550u);
  EXPECT_LT(middle, 2900u);
}

TEST_F(RandSolveTest, SignedDistRangesUseBiasedCoordinates) {
  std::vector<uint8_t> bytes = program(
      2, 2,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
      false, {},
      {{0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 2, 0, 2, 1, 0,
        OBELISK_RT_RANDOM_DIST_TARGET_SIGNED},
       {0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 2, 2, 2, 1, 1,
        OBELISK_RT_RANDOM_DIST_TARGET_SIGNED}});
  uint64_t captures[] = {3, 1};
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 91, 7);
  unsigned negative = 0;
  for (unsigned iteration = 0; iteration != 4096; ++iteration) {
    uint64_t assignment = 0;
    uint64_t nextState = 0;
    uint32_t success = 0;
    ASSERT_EQ(obelisk_rt_v1_random_solve_modes_state(
                  context, bytes.data(), bytes.size(), 0, 3, 0, 4, state.state,
                  state.increment, captures, 2, &assignment, &success,
                  &nextState),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    negative += (assignment & 2) != 0;
    state.state = nextState;
  }
  EXPECT_GT(negative, 2900u);
  EXPECT_LT(negative, 3250u);
}

TEST_F(RandSolveTest, DisabledConstraintDisablesItsDistGroup) {
  std::vector<uint8_t> bytes = program(
      1, 1,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
      false, {}, {{0, 0, 0, 1, 1, 1, 1, 0}});
  uint64_t capture = 1;
  uint64_t assignment = 1;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 0, 1, 1, 2, &capture, 1,
                &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 0u);
}

TEST_F(RandSolveTest, RejectsNegativeDynamicDistWeight) {
  std::vector<uint8_t> bytes = program(
      1, 1,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
      false, {},
      {{0, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 1, 0, 2, 1, 0,
        OBELISK_RT_RANDOM_DIST_WEIGHT_SIGNED}});
  uint64_t capture = UINT64_MAX;
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 9, 5);
  uint64_t assignment = 0;
  uint64_t nextState = 0;
  uint32_t success = 1;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes_state(
                context, bytes.data(), bytes.size(), 0, 1, 0, 2, state.state,
                state.increment, &capture, 1, &assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 0u);
  EXPECT_EQ(nextState, state.state);
}

TEST_F(RandSolveTest, RejectsMalformedDistGroupInventory) {
  std::vector<uint8_t> bytes = program(
      1, 1,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1}},
      false, {},
      {{1, OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0, 1, 0, 1, 1, 0}});
  uint64_t capture = 1;
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 9, 5);
  uint64_t assignment = 0;
  uint64_t nextState = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes_state(
                context, bytes.data(), bytes.size(), 0, 1, 0, 2, state.state,
                state.increment, &capture, 1, &assignment, &success,
                &nextState),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RandSolveTest, DisabledSoftConstraintAcceptsFirstHardSolution) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 1},
       {OBELISK_RT_RANDOM_GE_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 2},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0, 0}},
      true);
  uint64_t assignment = 0;
  uint32_t success = 0;

  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 1, 3, 0, 4,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 2u);

  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 1, 3, 1, 4,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 1u);
}

TEST_F(RandSolveTest, HonorsMultipleSoftConstraintPriorities) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 1},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 2},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 1}},
      true);
  uint64_t assignment = 0;
  uint32_t success = 0;

  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 0, 3, 0, 4,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 2u);
}

TEST_F(RandSolveTest, GuardedSoftPredicatesAreVacuouslySatisfied) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 1},
       {OBELISK_RT_RANDOM_GE_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 2},
       {OBELISK_RT_RANDOM_LE_V1, 1},
       {OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 2},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 1},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1, 1},
       {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 1},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_SELECT_V1, 1},
       {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 2}},
      true);
  uint64_t assignment = 0;
  uint32_t success = 0;

  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 0, 3, 0, 4,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 2u);
}

TEST_F(RandSolveTest, RejectsNoncontiguousSoftPriorities) {
  std::vector<uint8_t> bytes = program(
      1, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 1}},
      true);
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 0, 1, 0, 2,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RandSolveTest, PreservesSolveBeforeConditionalDistribution) {
  // x <= y has solutions 00, 10, and 11 in aggregate bit order. Solving x
  // first selects x uniformly and then y uniformly from compatible values.
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 1},
       {OBELISK_RT_RANDOM_LE_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{1, 2}});
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 23, 11);
  obelisk_rt_random_state_v1 expectedState = state;
  uint64_t expectedX = 0;
  ASSERT_EQ(obelisk_rt_v1_random_state_bounded(&expectedState, 2, &expectedX),
            OBELISK_RT_OK);
  uint64_t expectedY = 1;
  if (expectedX == 0) {
    ASSERT_EQ(obelisk_rt_v1_random_state_bounded(&expectedState, 2, &expectedY),
              OBELISK_RT_OK);
  }

  uint64_t assignment = 0;
  uint64_t nextState = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes_state(
                context, bytes.data(), bytes.size(), 1, 3, 0, 4, state.state,
                state.increment, nullptr, 0, &assignment, &success, &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, expectedX | (expectedY << 1));
  EXPECT_EQ(nextState, expectedState.state);
}

TEST_F(RandSolveTest,
       ExecutesExpandedConstraintFunctionProgramWithStateCapture) {
  // This is the residual form of x == map(y), where map returns y plus the
  // pre-solve value of rand field bias. The function argument orders y before
  // x, while the function-body read of bias is an immutable capture even
  // though the bias property itself is randomized to a different value.
  std::vector<uint8_t> bytes = program(
      6, 1,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2, 0, 2},
       {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 2, 0, 0},
       {OBELISK_RT_RANDOM_ADD_V1, 2},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2, 0, 4},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 3},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{UINT64_C(0x0c), UINT64_C(0x03)}});
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 41, 17);
  uint64_t oldBias = 1;
  uint64_t assignment = 0;
  uint64_t nextState = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes_state(
                context, bytes.data(), bytes.size(), 0, UINT64_C(0x3f), 0, 64,
                state.state, state.increment, &oldBias, 1, &assignment,
                &success, &nextState),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  EXPECT_EQ((assignment >> 4) & 3, 3u);
  EXPECT_EQ(assignment & 3, (((assignment >> 2) & 3) + oldBias) & 3);
  EXPECT_NE(nextState, state.state);
}

TEST_F(RandSolveTest, StatelessEntryRejectsActiveSolveBefore) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{1, 2}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 0, 3, 0, 4,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RandSolveTest, ConstraintModeDisablesSolveBeforeEdge) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 1},
       {OBELISK_RT_RANDOM_LE_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{1, 2, 0}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 1, 3, 1, 4,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 2u);
}

TEST_F(RandSolveTest, StatefulSolveBeforeUsesUnbiasedBoundedDraw) {
  // x has exactly three legal values and is solved before y, which is fixed
  // by x. The stateful entry must use the PCG rejection-based bounded draw.
  std::vector<uint8_t> bytes = program(
      4, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 3},
       {OBELISK_RT_RANDOM_LT_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2, 0, 2},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2, 0, 0},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{3, 12}});
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 17, 9);
  obelisk_rt_random_state_v1 expectedState = state;
  uint64_t expectedX = 0;
  ASSERT_EQ(obelisk_rt_v1_random_state_bounded(&expectedState, 3, &expectedX),
            OBELISK_RT_OK);

  uint64_t assignment = 0;
  uint64_t nextState = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes_state(
                context, bytes.data(), bytes.size(), 0, 15, 0, 16, state.state,
                state.increment, nullptr, 0, &assignment, &success, &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment & 3, expectedX);
  EXPECT_EQ((assignment >> 2) & 3, expectedX);
  EXPECT_EQ(nextState, expectedState.state);
}

TEST_F(RandSolveTest, StatefulSolveBeforeRequiresOddIncrement) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{1, 2}});
  uint64_t assignment = 0;
  uint64_t nextState = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes_state(
                context, bytes.data(), bytes.size(), 0, 3, 0, 4, 17, 2,
                nullptr, 0, &assignment, &success, &nextState),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(nextState, 17u);
}

TEST_F(RandSolveTest, HandlesManyDistinctSolveLayerValues) {
  std::vector<uint8_t> bytes = program(
      16, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{(uint64_t{1} << 15) - 1, uint64_t{1} << 15}});
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 29, 13);
  uint64_t assignment = 0;
  uint64_t nextState = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes_state(
                context, bytes.data(), bytes.size(), 0, UINT16_MAX, 0,
                uint64_t{1} << 16, state.state, state.increment, nullptr, 0,
                &assignment, &success, &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
}

TEST_F(RandSolveTest, SolveBeforeTreatsDisabledPropertyAsFixed) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 1},
       {OBELISK_RT_RANDOM_LE_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{1, 2}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 1, 2, 0, 2,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment, 3u);
}

TEST_F(RandSolveTest, SolveBeforeHonorsSoftPriority) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 1},
       {OBELISK_RT_RANDOM_LE_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0}},
      true, {{1, 2}});
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 31, 15);
  uint64_t assignment = 0;
  uint64_t nextState = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes_state(
                context, bytes.data(), bytes.size(), 3, 3, 0, 4, state.state,
                state.increment, nullptr, 0, &assignment, &success, &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment & 1, 0u);
}

TEST_F(RandSolveTest, SolveBeforeRefusesIncompleteDomainTraversal) {
  std::vector<uint8_t> bytes = program(
      21, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{1, 2}});
  obelisk_rt_random_state_v1 state;
  obelisk_rt_v1_random_state_seed(&state, 37, 17);
  uint64_t assignment = 0;
  uint64_t nextState = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes_state(
                context, bytes.data(), bytes.size(), 0,
                (uint64_t{1} << 21) - 1, 0, 1, state.state, state.increment,
                nullptr, 0, &assignment, &success, &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 0u);
}

TEST_F(RandSolveTest, RejectsCyclicSolveBeforeMetadata) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{1, 2}, {2, 1}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 0, 3, 0, 4,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RandSolveTest, RejectsOverlappingPathSolveBeforeCycle) {
  // Whole and selected packed paths can overlap across distinct ordering
  // edges. Bit 1 -> bit 2 comes from the first edge, while bit 2 -> bit 1
  // comes from the second, so this is a cycle even though the serialized mask
  // nodes are not pairwise identical.
  std::vector<uint8_t> bytes = program(
      4, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{UINT64_C(0x3), UINT64_C(0x4)},
              {UINT64_C(0x4), UINT64_C(0x2)}});
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 0, UINT64_C(0xf), 0, 16,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RandSolveTest, RejectsTruncatedSolveBeforeMetadata) {
  std::vector<uint8_t> bytes = program(
      2, 0,
      {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
      false, {{1, 2}});
  bytes.pop_back();
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_modes(
                context, bytes.data(), bytes.size(), 0, 3, 0, 4,
                nullptr, 0, &assignment, &success),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RandSolveTest, RejectsMalformedPrograms) {
  std::vector<uint8_t> bytes = program(4, 0,
                                       {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
                                        {OBELISK_RT_RANDOM_END_HARD_V1, 1}});
  bytes[0] ^= 0xff;
  uint64_t assignment = 0;
  uint32_t success = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve(context, bytes.data(), bytes.size(), 0,
                                       16, nullptr, 0, &assignment, &success),
            OBELISK_RT_INVALID_ARGUMENT);
}

} // namespace
