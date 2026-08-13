//===- RandSolveWideTest.cpp - Wide residual solver tests ----------------===//

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

struct Instruction {
  uint8_t opcode;
  uint32_t width;
  uint8_t flags = 0;
  uint32_t operand = 0;
  uint32_t auxiliary = 0;
  std::vector<uint64_t> literal;
};

struct DomainRecord {
  uint32_t group;
  uint32_t targetOffset;
  uint16_t width;
  uint64_t mask;
  uint64_t value;
};

std::vector<uint8_t> program(uint32_t aggregateWidth, uint32_t captureCount,
                             std::vector<Instruction> instructions,
                             uint32_t flags = 0,
                             std::vector<DomainRecord> domains = {}) {
  std::vector<uint64_t> literals;
  for (Instruction &instruction : instructions) {
    if (instruction.opcode != OBELISK_RT_RANDOM_PUSH_LITERAL_V1)
      continue;
    instruction.auxiliary = static_cast<uint32_t>(literals.size());
    literals.insert(literals.end(), instruction.literal.begin(),
                    instruction.literal.end());
  }
  std::vector<uint8_t> bytes;
  append32(bytes, OBELISK_RT_RANDOM_PROGRAM_MAGIC);
  append16(bytes, OBELISK_RT_RANDOM_PROGRAM_VERSION_V2);
  append16(bytes, OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2);
  append32(bytes, aggregateWidth);
  append32(bytes, static_cast<uint32_t>(instructions.size()));
  append32(bytes, captureCount);
  append32(bytes, flags);
  append32(bytes, static_cast<uint32_t>(literals.size()));
  append32(bytes, 0);
  for (const Instruction &instruction : instructions) {
    bytes.push_back(instruction.opcode);
    bytes.push_back(instruction.flags);
    append16(bytes, 0);
    append32(bytes, instruction.width);
    append32(bytes, instruction.operand);
    append32(bytes, instruction.auxiliary);
  }
  for (uint64_t literal : literals)
    append64(bytes, literal);
  if ((flags & OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS) != 0) {
    uint32_t groupCount = 0;
    for (const DomainRecord &domain : domains)
      groupCount = std::max(groupCount, domain.group + 1);
    append32(bytes, groupCount);
    append32(bytes, static_cast<uint32_t>(domains.size()));
    for (const DomainRecord &domain : domains) {
      append32(bytes, domain.group);
      append32(bytes, domain.targetOffset);
      append16(bytes, domain.width);
      append16(bytes, 0);
      append32(bytes, 0);
      append64(bytes, domain.mask);
      append64(bytes, domain.value);
    }
  }
  return bytes;
}

class RandSolveWideTest : public testing::Test {
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

TEST_F(RandSolveWideTest, SolvesHardConstraintWithWideCapture) {
  std::vector<uint8_t> bytes =
      program(128, 1,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 128},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 0},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t start[] = {0, 1};
  uint64_t mutableMask[] = {7, 0};
  uint64_t capture[] = {5, 1};
  uint32_t captureWidths[] = {128};
  uint64_t assignment[] = {UINT64_MAX, UINT64_MAX};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                8, 17, 3, capture, 2, captureWidths, 1, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 5u);
  EXPECT_EQ(assignment[1], 1u);
  EXPECT_EQ(nextState, 17u);
}

TEST_F(RandSolveWideTest, SupportsWideExpressionsOverNarrowAssignments) {
  std::vector<uint8_t> bytes =
      program(8, 1,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 8},
               {OBELISK_RT_RANDOM_CAST_V1, 128},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 0},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t start[] = {0};
  uint64_t mutableMask[] = {7};
  uint64_t capture[] = {5, 0};
  uint32_t captureWidths[] = {128};
  uint64_t assignment[] = {0};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 1, 0,
                8, 23, 3, capture, 2, captureWidths, 1, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 5u);
}

TEST_F(RandSolveWideTest, AcceptsWordPaddedNarrowCaptureStorage) {
  std::vector<uint8_t> bytes =
      program(1, 1,
              {{OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 8, 0, 0},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 8, 0, 0, 0, {5}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t start[] = {0};
  uint64_t mutableMask[] = {0};
  uint64_t capture[] = {5};
  uint32_t captureWidths[] = {64};
  uint64_t assignment[] = {UINT64_MAX};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 1, 0,
                1, 21, 3, capture, 1, captureWidths, 1, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 0u);
}

TEST_F(RandSolveWideTest, UsesMaximumWidthForRepeatedCaptureReferences) {
  std::vector<uint8_t> bytes =
      program(1, 1,
              {{OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 64, 0, 0},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 64, 0, 0, 0, {5}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 0},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {5, 1}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t start[] = {0};
  uint64_t mutableMask[] = {0};
  uint64_t capture[] = {5, 1};
  uint32_t captureWidths[] = {128};
  uint64_t assignment[] = {UINT64_MAX};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 1, 0,
                1, 27, 3, capture, 2, captureWidths, 1, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 0u);
}

TEST_F(RandSolveWideTest, RejectsMismatchedCaptureBoundaries) {
  std::vector<uint8_t> bytes =
      program(1, 2,
              {{OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 64, 0, 0},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 64, 0, 0, 0, {5}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 1},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {6, 1}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t start[] = {0};
  uint64_t mutableMask[] = {0};
  uint64_t captures[] = {5, 6, 1};
  uint32_t swappedWidths[] = {128, 64};
  uint64_t assignment[] = {UINT64_MAX};
  uint32_t success = 1;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 1, 0,
                1, 25, 3, captures, 3, swappedWidths, 2, assignment, &success,
                &nextState),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(success, 0u);
  EXPECT_EQ(nextState, 25u);
}

TEST_F(RandSolveWideTest, EvaluatesWideLiteralArithmetic) {
  std::vector<uint8_t> bytes =
      program(128, 0,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 128},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {1, 1}},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {3, 0}},
               {OBELISK_RT_RANDOM_MUL_V1, 128},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t start[] = {0, 3};
  uint64_t mutableMask[] = {7, 0};
  uint64_t assignment[] = {0, 0};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                8, 29, 5, nullptr, 0, nullptr, 0, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 3u);
  EXPECT_EQ(assignment[1], 3u);
  EXPECT_EQ(nextState, 29u);
}

TEST_F(RandSolveWideTest, EvaluatesWideSignedDivisionRemainderAndShift) {
  constexpr uint8_t signedFlag = OBELISK_RT_RANDOM_INSTRUCTION_SIGNED;
  std::vector<uint8_t> bytes = program(
      1, 1,
      {{OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {2, 0}},
       {OBELISK_RT_RANDOM_DIV_V1, 128, signedFlag},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0,
        {UINT64_MAX - 3, UINT64_MAX}},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
       {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {2, 0}},
       {OBELISK_RT_RANDOM_MOD_V1, 128, signedFlag},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0,
        {UINT64_MAX, UINT64_MAX}},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
       {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {1, 0}},
       {OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1, 128, signedFlag},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0,
        {UINT64_MAX - 4, UINT64_MAX}},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
        OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t start[] = {0};
  uint64_t mutableMask[] = {0};
  uint64_t capture[] = {UINT64_MAX - 8, UINT64_MAX};
  uint32_t captureWidths[] = {128};
  uint64_t assignment[] = {UINT64_MAX};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 1, 0,
                1, 31, 5, capture, 2, captureWidths, 1, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 0u);
  EXPECT_EQ(nextState, 31u);
}

TEST_F(RandSolveWideTest, CarriesEnumerationAcrossWordBoundary) {
  std::vector<uint8_t> bytes =
      program(65, 0,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 65},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 65, 0, 0, 0, {0, 1}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t start[] = {UINT64_MAX, 0};
  uint64_t mutableMask[] = {UINT64_MAX, 1};
  uint64_t assignment[] = {0, 0};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                2, 41, 7, nullptr, 0, nullptr, 0, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 0u);
  EXPECT_EQ(assignment[1], 1u);
}

TEST_F(RandSolveWideTest, TraversesFiniteDomainAcrossWordBoundary) {
  std::vector<uint8_t> bytes =
      program(65, 0,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 2, 0, 63},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 2, 0, 0, 0, {2}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
              OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS,
              {{0, 63, 2, 3, 1}, {0, 63, 2, 3, 2}});
  uint64_t start[] = {0, 0};
  uint64_t mutableMask[] = {uint64_t{1} << 63, 1};
  uint64_t assignment[] = {UINT64_MAX, UINT64_MAX};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                2, 73, 17, nullptr, 0, nullptr, 0, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 0u);
  EXPECT_EQ(assignment[1], 1u);
  EXPECT_NE(nextState, 73u);
}

TEST_F(RandSolveWideTest, AllowsUnnamedValueInInactiveFiniteDomain) {
  std::vector<uint8_t> bytes =
      program(65, 0,
              {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0, {1}},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
              OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS,
              {{0, 63, 2, 3, 1}, {0, 63, 2, 3, 2}});
  uint64_t start[] = {0, 0};
  uint64_t mutableMask[] = {0, 0};
  uint64_t assignment[] = {UINT64_MAX, UINT64_MAX};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                1, 79, 19, nullptr, 0, nullptr, 0, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 0u);
  EXPECT_EQ(assignment[1], 0u);
  EXPECT_EQ(nextState, 79u);

  mutableMask[0] = uint64_t{1} << 63;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                1, 79, 19, nullptr, 0, nullptr, 0, assignment, &success,
                &nextState),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(success, 0u);
}

TEST_F(RandSolveWideTest, RejectsMalformedFiniteDomainMetadata) {
  auto rejects = [&](std::vector<uint8_t> bytes) {
    uint64_t start[] = {0, 0};
    uint64_t mutableMask[] = {UINT64_MAX, 1};
    uint64_t assignment[] = {0, 0};
    uint32_t success = 1;
    uint64_t nextState = 0;
    EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                  1, 83, 21, nullptr, 0, nullptr, 0, assignment, &success,
                  &nextState),
              OBELISK_RT_INVALID_ARGUMENT);
    EXPECT_EQ(success, 0u);
  };
  std::vector<Instruction> truth = {
      {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0, {1}},
      {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
       OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}};

  rejects(program(65, 0, truth, OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS,
                  {{1, 63, 2, 3, 1}}));
  rejects(program(65, 0, truth, OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS,
                  {{0, 63, 2, 3, 1}, {1, 64, 1, 1, 0}}));
  std::vector<uint8_t> truncated = program(
      65, 0, truth, OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS, {{0, 63, 2, 3, 1}});
  truncated.pop_back();
  rejects(std::move(truncated));
}

TEST_F(RandSolveWideTest, HonorsConstraintModeMask) {
  std::vector<uint8_t> bytes =
      program(65, 0,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 65},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 65, 0, 0, 0, {5, 0}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 65},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 65, 0, 0, 0, {6, 0}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, 0}});
  uint64_t start[] = {0, 0};
  uint64_t mutableMask[] = {7, 0};
  uint64_t assignment[] = {0, 0};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 2, 1,
                8, 61, 11, nullptr, 0, nullptr, 0, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 5u);
  EXPECT_EQ(assignment[1], 0u);
}

TEST_F(RandSolveWideTest, SelectsBestSoftPriorityAfterCompleteTraversal) {
  std::vector<uint8_t> bytes =
      program(65, 0,
              {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0, {1}},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 65},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 65, 0, 0, 0, {3, 0}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0},
               {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 65},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 65, 0, 0, 0, {5, 0}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 1}},
              OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT);
  uint64_t start[] = {0, 0};
  uint64_t mutableMask[] = {7, 0};
  uint64_t assignment[] = {0, 0};
  uint32_t success = 0;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                8, 67, 13, nullptr, 0, nullptr, 0, assignment, &success,
                &nextState),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 5u);
  EXPECT_EQ(assignment[1], 0u);
}

TEST_F(RandSolveWideTest, RejectsNonCanonicalLiteralPadding) {
  std::vector<uint8_t> bytes =
      program(65, 0,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 65},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 65, 0, 0, 0, {0, 2}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
  uint64_t start[] = {0, 0};
  uint64_t mutableMask[] = {UINT64_MAX, 1};
  uint64_t assignment[] = {UINT64_MAX, UINT64_MAX};
  uint32_t success = 1;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                2, 53, 9, nullptr, 0, nullptr, 0, assignment, &success,
                &nextState),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(success, 0u);
  EXPECT_EQ(nextState, 53u);
}

TEST_F(RandSolveWideTest, RejectsNonContiguousSoftPriorities) {
  std::vector<uint8_t> bytes =
      program(1, 0,
              {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0, {1}},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0, {1}},
               {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 1}},
              OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT);
  uint64_t start[] = {0};
  uint64_t mutableMask[] = {0};
  uint64_t assignment[] = {UINT64_MAX};
  uint32_t success = 1;
  uint64_t nextState = 0;
  EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 1, 0,
                1, 71, 15, nullptr, 0, nullptr, 0, assignment, &success,
                &nextState),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(success, 0u);
  EXPECT_EQ(nextState, 71u);
}

} // namespace
