#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

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

std::vector<uint8_t>
program(uint32_t width, uint32_t captures,
        std::initializer_list<EncodedInstruction> instructions,
        bool hasSoft = false) {
  std::vector<uint8_t> bytes;
  append32(bytes, OBELISK_RT_RANDOM_PROGRAM_MAGIC);
  append16(bytes, OBELISK_RT_RANDOM_PROGRAM_VERSION);
  append16(bytes, OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE);
  append32(bytes, width);
  append32(bytes, static_cast<uint32_t>(instructions.size()));
  append32(bytes, captures);
  append32(bytes, hasSoft ? OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT : 0);
  for (const EncodedInstruction &instruction : instructions) {
    bytes.push_back(instruction.opcode);
    bytes.push_back(instruction.width);
    bytes.push_back(instruction.flags);
    bytes.push_back(0);
    append32(bytes, instruction.operand);
    append64(bytes, instruction.immediate);
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
