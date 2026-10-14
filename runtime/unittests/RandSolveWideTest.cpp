//===- RandSolveWideTest.cpp - Wide residual solver tests ----------------===//

#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "RandomSamplerTestSupport.h"

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

struct SolveBeforeEdge {
  std::vector<uint64_t> before;
  std::vector<uint64_t> after;
  uint32_t block = OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
  bool functionPriority = false;
};

struct WeightedRecord {
  uint32_t group;
  uint32_t block;
  uint32_t offset;
  uint16_t width;
  uint64_t lower;
  uint64_t cardinality;
  uint64_t item;
  uint32_t capture;
  uint32_t flags;
};

std::vector<uint8_t> program(uint32_t aggregateWidth, uint32_t captureCount,
                             std::vector<Instruction> instructions,
                             uint32_t flags = 0,
                             std::vector<DomainRecord> domains = {},
                             std::vector<SolveBeforeEdge> edges = {},
                             std::vector<WeightedRecord> weighted = {}) {
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
  if ((flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE) != 0) {
    append32(bytes, static_cast<uint32_t>(edges.size()));
    for (const SolveBeforeEdge &edge : edges) {
      for (uint64_t word : edge.before)
        append64(bytes, word);
      for (uint64_t word : edge.after)
        append64(bytes, word);
      append32(bytes, edge.block);
      append32(bytes, edge.functionPriority
                          ? OBELISK_RT_RANDOM_SOLVE_EDGE_FUNCTION_PRIORITY
                          : 0u);
    }
  }
  if (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY)
    append32(bytes, 0);
  if (flags & OBELISK_RT_RANDOM_PROGRAM_EXACT_WEIGHTED) {
    uint32_t groups = 0;
    for (const auto &record : weighted)
      groups = std::max(groups, record.group + 1);
    append32(bytes, groups);
    append32(bytes, weighted.size());
    for (const auto &record : weighted) {
      append32(bytes, record.group);
      append32(bytes, record.block);
      append32(bytes, record.offset);
      append16(bytes, record.width);
      append16(bytes, 0);
      append64(bytes, record.lower);
      append64(bytes, record.cardinality);
      append64(bytes, record.item);
      append32(bytes, record.capture);
      append32(bytes, record.flags);
    }
  }
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
  appendTestSamplerPlan(bytes);
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
  std::vector<uint8_t> bytes =
      program(1, 1,
              {{OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 0},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {2, 0}},
               {OBELISK_RT_RANDOM_DIV_V1, 128, signedFlag},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1,
                128,
                0,
                0,
                0,
                {UINT64_MAX - 3, UINT64_MAX}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 0},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {2, 0}},
               {OBELISK_RT_RANDOM_MOD_V1, 128, signedFlag},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1,
                128,
                0,
                0,
                0,
                {UINT64_MAX, UINT64_MAX}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 0},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 128, 0, 0, 0, {1, 0}},
               {OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1, 128, signedFlag},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1,
                128,
                0,
                0,
                0,
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

TEST_F(RandSolveWideTest, RejectsMalformedWideSolveOrderMetadata) {
  auto rejects = [&](const std::vector<uint8_t> &bytes) {
    uint64_t start[] = {0, 0}, mutableMask[] = {UINT64_MAX, 1};
    uint64_t assignment[] = {0, 0}, nextState = 0;
    uint32_t success = 1;
    EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), start, mutableMask, 2,
                  UINT64_MAX, 1, 83, 21, nullptr, 0, nullptr, 0, assignment,
                  &success, &nextState),
              OBELISK_RT_INVALID_ARGUMENT);
    EXPECT_EQ(success, 0u);
    EXPECT_EQ(nextState, 83u);
  };
  std::vector<Instruction> truth = {
      {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0, {1}},
      {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
       OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}};
  auto encode = [&](std::vector<SolveBeforeEdge> edges) {
    return program(65, 0, truth,
                   OBELISK_RT_RANDOM_PROGRAM_EXACT_UNIFORM |
                       OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE,
                   {}, std::move(edges));
  };
  rejects(encode({}));
  rejects(encode({{{0, 0}, {0, 1}}}));
  rejects(encode({{{0, 2}, {1, 0}}}));
  rejects(encode({{{1, 0}, {1, 1}}}));
  rejects(encode({{{1, 0}, {0, 1}, 64}}));
  // Declaration cycles remain invalid even when their blocks are disabled.
  rejects(encode({{{1, 0}, {0, 1}, 0}, {{0, 1}, {1, 0}, 1}}));
  auto bytes = encode({{{1, 0}, {0, 1}}});
  bytes.back() = 1;
  rejects(bytes);
  bytes.back() = 0;
  bytes.pop_back();
  rejects(bytes);
  bytes = encode({{{1, 0}, {0, 1}}});
  bytes[20] &= ~OBELISK_RT_RANDOM_PROGRAM_EXACT_UNIFORM;
  rejects(bytes);
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

TEST_F(RandSolveWideTest, DisableSoftUsesLiveConstraintMask) {
  std::vector<uint8_t> bytes =
      program(65, 0,
              {{OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, 0, 0, 0, {1}},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 65},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 65, 0, 0, 0, {1, 0}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1, 1, 0, 63},
               {OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1},
               {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1, 0}},
              OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT);
  for (uint64_t disabled : {UINT64_C(0), UINT64_C(1) << 63}) {
    uint64_t start[] = {2, 0};
    uint64_t mutableMask[] = {3, 0};
    uint64_t assignment[] = {0, 0};
    uint32_t success = 0;
    uint64_t nextState = 0;
    ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), start, mutableMask, 2,
                  disabled, 4, 67, 13, nullptr, 0, nullptr, 0, assignment,
                  &success, &nextState),
              OBELISK_RT_OK);
    EXPECT_EQ(success, 1u);
    EXPECT_EQ(assignment[0], disabled == 0 ? 2u : 1u);
    EXPECT_EQ(assignment[1], 0u);
    EXPECT_EQ(nextState, 67u);
  }
}

TEST_F(RandSolveWideTest, IntegralSystemFunctionsCountAcrossWordBoundaries) {
  struct Case {
    uint64_t low;
    uint64_t high;
    uint64_t ones;
    uint64_t logarithm;
  };
  for (Case test : {Case{0, 0, 0, 0}, Case{1, 0, 1, 0}, Case{0, 1, 1, 64},
                    Case{1, 1, 2, 65}, Case{0, UINT64_C(1) << 63, 1, 127},
                    Case{1, UINT64_C(1) << 63, 2, 128},
                    Case{UINT64_MAX, UINT64_MAX, 128, 128}}) {
    std::vector<uint8_t> bytes = program(
        128, 0,
        {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 128},
         {OBELISK_RT_RANDOM_COUNT_ONES_V1, 32},
         {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 32, 0, 0, 0, {test.ones}},
         {OBELISK_RT_RANDOM_EQ_V1, 1},
         {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 128},
         {OBELISK_RT_RANDOM_CLOG2_V1, 32},
         {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 32, 0, 0, 0, {test.logarithm}},
         {OBELISK_RT_RANDOM_EQ_V1, 1},
         {OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1},
         {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
          OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
    uint64_t start[] = {test.low, test.high};
    uint64_t mutableMask[] = {0, 0};
    uint64_t assignment[] = {0, 0};
    uint32_t success = 0;
    uint64_t nextState = 0;
    ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                  1, 67, 13, nullptr, 0, nullptr, 0, assignment, &success,
                  &nextState),
              OBELISK_RT_OK);
    EXPECT_EQ(success, 1u);
    EXPECT_EQ(assignment[0], test.low);
    EXPECT_EQ(assignment[1], test.high);
    EXPECT_EQ(nextState, 67u);
  }
}

TEST_F(RandSolveWideTest, RejectsMalformedConstraintEnabledInstructions) {
  for (Instruction instruction :
       {Instruction{OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1, 2},
        Instruction{OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1, 1,
                    OBELISK_RT_RANDOM_INSTRUCTION_SIGNED},
        Instruction{OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1, 1, 0, 64},
        Instruction{OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1, 1, 0, 0,
                    1}}) {
    std::vector<uint8_t> bytes =
        program(65, 0,
                {instruction,
                 {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                  OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}});
    uint64_t start[] = {0, 0}, mutableMask[] = {3, 0}, assignment[] = {0, 0};
    uint32_t success = 1;
    uint64_t nextState = 0;
    EXPECT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                  4, 67, 13, nullptr, 0, nullptr, 0, assignment, &success,
                  &nextState),
              OBELISK_RT_INVALID_ARGUMENT);
    EXPECT_EQ(success, 0u);
  }
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

#ifdef OBELISK_TEST_COMPILED_SAMPLER
TEST_F(RandSolveWideTest, FunctionArgumentStagesPreserveWideFailureSemantics) {
  auto bytes = program(131, 0,
                       {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 130},
                        {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 130},
                        {OBELISK_RT_RANDOM_CAST_V1, 130},
                        {OBELISK_RT_RANDOM_EQ_V1, 1},
                        {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, 0},
                        {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 130},
                        {OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1},
                        {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, 1}},
                       OBELISK_RT_RANDOM_PROGRAM_EXACT_UNIFORM |
                           OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE |
                           OBELISK_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY,
                       {}, {{{0, 0, 4}, {UINT64_MAX, UINT64_MAX, 3}, 0, true}});
  unsigned accepted = 0;
  uint64_t start[] = {0, 0, 0}, mask[] = {UINT64_MAX, UINT64_MAX, 7};
  for (unsigned seed = 0; seed != 128; ++seed) {
    obelisk_rt_random_state_v1 state;
    obelisk_rt_v1_random_state_seed(&state, seed, 17);
    uint64_t original = state.state, assignment[3] = {};
    uint32_t success = 0;
    ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), start, mask, 3, 0, 1,
                  state.state, state.increment, nullptr, 0, nullptr, 0,
                  assignment, &success, &state.state),
              OBELISK_RT_OK);
    EXPECT_EQ(assignment[0] | assignment[1] | assignment[2], 0u);
    if (success)
      ++accepted;
    else
      EXPECT_EQ(state.state, original);
  }
  EXPECT_GT(accepted, 40u);
  EXPECT_LT(accepted, 88u);
}

TEST_F(RandSolveWideTest, OrderedProjectionSpansWordsAndHonorsModes) {
  auto bytes = program(131, 1,
                       {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 130},
                        {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 130},
                        {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 130},
                        {OBELISK_RT_RANDOM_EQ_V1, 1},
                        {OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1, 1},
                        {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                         OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
                       OBELISK_RT_RANDOM_PROGRAM_EXACT_UNIFORM |
                           OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE,
                       {}, {{{0, 0, 4}, {UINT64_MAX, UINT64_MAX, 3}, 0}});
  uint64_t start[] = {0, 0, 0}, mutableMask[] = {UINT64_MAX, UINT64_MAX, 7};
  uint64_t captures[] = {7, UINT64_C(0x123456789abcdef0), 2},
           assignment[3] = {};
  uint32_t widths[] = {130}, success = 0;
  uint64_t state = 71;
  auto solve = [&](uint64_t disabled) {
    return obelisk_rt_v1_random_solve_wide_modes_state(
        context, bytes.data(), bytes.size(), start, mutableMask, 3, disabled, 1,
        state, 15, captures, 3, widths, 1, assignment, &success, &state);
  };
  unsigned guardTrue = 0;
  uint64_t ones[3] = {}, zeros[3] = {};
  for (unsigned iteration = 0; iteration != 128; ++iteration) {
    captures[0] = iteration & 1 ? 11 : 7;
    ASSERT_EQ(solve(0), OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    if (assignment[2] & 4) {
      ++guardTrue;
      EXPECT_EQ(assignment[0], captures[0]);
      EXPECT_EQ(assignment[1], captures[1]);
      EXPECT_EQ(assignment[2] & 3, captures[2]);
    } else {
      for (unsigned word = 0; word != 3; ++word) {
        ones[word] |= assignment[word];
        zeros[word] |= ~assignment[word];
      }
    }
    EXPECT_EQ(assignment[2] & ~uint64_t{7}, 0u);
  }
  EXPECT_GT(guardTrue, 32u);
  EXPECT_LT(guardTrue, 96u);
  EXPECT_EQ(ones[0], UINT64_MAX);
  EXPECT_EQ(ones[1], UINT64_MAX);
  EXPECT_EQ(ones[2] & 3, 3u);
  EXPECT_EQ(zeros[0], UINT64_MAX);
  EXPECT_EQ(zeros[1], UINT64_MAX);
  EXPECT_EQ(zeros[2] & 3, 3u);
  // Disabling the ordering block restores the ordinary joint distribution.
  for (unsigned iteration = 0; iteration != 16; ++iteration) {
    ASSERT_EQ(solve(1), OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    EXPECT_EQ(assignment[2] & 4, 0u);
  }
  // Freezing the guard leaves exactly one completion and consumes no draw.
  start[2] = 4;
  mutableMask[2] = 3;
  uint64_t unchanged = state;
  ASSERT_EQ(solve(0), OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], captures[0]);
  EXPECT_EQ(assignment[1], captures[1]);
  EXPECT_EQ(assignment[2], 4 | captures[2]);
  EXPECT_EQ(state, unchanged);
}

TEST_F(RandSolveWideTest, OrderedDomainsFollowVariableLengthEdges) {
  auto bytes = program(65, 1,
                       {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 64},
                        {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 32},
                        {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 32},
                        {OBELISK_RT_RANDOM_EQ_V1, 1},
                        {OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1, 1},
                        {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                         OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
                       OBELISK_RT_RANDOM_PROGRAM_EXACT_UNIFORM |
                           OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE |
                           OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS,
                       {{0, 0, 32, UINT32_MAX, 7}, {0, 0, 32, UINT32_MAX, 11}},
                       {{{0, 1}, {UINT64_MAX, 0}}});
  uint64_t start[] = {0, 0}, mutableMask[] = {UINT64_MAX, 1};
  uint64_t capture = 7, assignment[2] = {}, state = 71;
  uint32_t width = 32, success = 0;
  unsigned guardTrue = 0;
  for (unsigned iteration = 0; iteration != 64; ++iteration) {
    ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), start, mutableMask, 2, 0,
                  1, state, 15, &capture, 1, &width, 1, assignment, &success,
                  &state),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    EXPECT_TRUE(uint32_t(assignment[0]) == 7 || uint32_t(assignment[0]) == 11);
    if (assignment[1]) {
      ++guardTrue;
      EXPECT_EQ(uint32_t(assignment[0]), 7u);
    }
  }
  EXPECT_GT(guardTrue, 16u);
  EXPECT_LT(guardTrue, 48u);
}

TEST_F(RandSolveWideTest, UniformSparseComponentWithWideFreeAndFrozenFields) {
  auto bytes = program(192, 1,
                       {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 32},
                        {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 32},
                        {OBELISK_RT_RANDOM_MUL_V1, 32},
                        {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 32, 0, 0},
                        {OBELISK_RT_RANDOM_EQ_V1, 1},
                        {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                         OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
                       OBELISK_RT_RANDOM_PROGRAM_EXACT_UNIFORM);
  uint64_t start[] = {0, 0, UINT64_C(0x1234567800000000)};
  uint64_t mutableMask[] = {UINT64_MAX, UINT64_MAX, UINT32_MAX};
  uint64_t captures[] = {49}, assignment[3] = {};
  uint32_t widths[] = {32}, success = 0;
  uint64_t state = 71;
  uint32_t seen = 0;
  uint64_t freeOnes = 0, freeZeros = 0;
  for (unsigned iteration = 0; iteration != 64; ++iteration) {
    ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), start, mutableMask, 3, 0,
                  1, state, 15, captures, 1, widths, 1, assignment, &success,
                  &state),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    uint32_t root = assignment[0];
    EXPECT_EQ(uint32_t(uint64_t(root) * root), 49u);
    EXPECT_EQ(assignment[2] >> 32, UINT64_C(0x12345678));
    if (root == 7)
      seen |= 1;
    else if (root == UINT32_C(0x7ffffff9))
      seen |= 2;
    else if (root == UINT32_C(0x80000007))
      seen |= 4;
    else if (root == UINT32_C(0xfffffff9))
      seen |= 8;
    freeOnes |= assignment[1];
    freeZeros |= ~assignment[1];
  }
  EXPECT_EQ(seen, 15u);
  EXPECT_EQ(freeOnes, UINT64_MAX);
  EXPECT_EQ(freeZeros, UINT64_MAX);
  captures[0] = 2;
  uint64_t unchanged = state;
  ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mutableMask, 3, 0,
                1, state, 15, captures, 1, widths, 1, assignment, &success,
                &state),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 0u);
  EXPECT_EQ(state, unchanged);
  EXPECT_EQ(assignment[0] | assignment[1] | assignment[2], 0u);
}

TEST_F(RandSolveWideTest, SolvesWideSystemFunctionsWithCaptureBoundaries) {
  std::vector<uint8_t> bytes =
      program(128, 2,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 128},
               {OBELISK_RT_RANDOM_COUNT_ONES_V1, 32},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 32, 0, 0, 0, {1}},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 128},
               {OBELISK_RT_RANDOM_CLOG2_V1, 32},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 32, 0, 0},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1},
               {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 128},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 128, 0, 1},
               {OBELISK_RT_RANDOM_EQ_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1}},
              OBELISK_RT_RANDOM_PROGRAM_EXACT_UNIFORM);
  uint64_t start[] = {0, 0}, mutableMask[] = {UINT64_MAX, UINT64_MAX};
  uint64_t captures[] = {96, 0, uint64_t{1} << 32};
  uint32_t captureWidths[] = {32, 128};
  uint64_t assignment[] = {0, 0}, nextState = 0;
  uint32_t success = 0;
  auto solve = [&] {
    return obelisk_rt_v1_random_solve_wide_modes_state(
        context, bytes.data(), bytes.size(), start, mutableMask, 2, 0, 1, 71,
        15, captures, 3, captureWidths, 2, assignment, &success, &nextState);
  };
  ASSERT_EQ(solve(), OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], 0u);
  EXPECT_EQ(assignment[1], uint64_t{1} << 32);
  EXPECT_EQ(nextState, 71u);
  // Freezing the required bit at zero makes the relation unsatisfiable.
  mutableMask[1] ^= uint64_t{1} << 32;
  ASSERT_EQ(solve(), OBELISK_RT_OK);
  EXPECT_EQ(success, 0u);
  EXPECT_EQ(assignment[0], 0u);
  EXPECT_EQ(assignment[1], 0u);
  EXPECT_EQ(nextState, 71u);
}

TEST_F(RandSolveWideTest, WeightedWideMassIgnoresCompanionMultiplicity) {
  // A 129-bit companion has 2^129 completions for x=0, one for x=1.
  // The wide unsigned weights 2^64 and 3*2^64 still require x's ratio 1:3.
  auto bytes = program(
      130, 2,
      {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1},
       {OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 129, 0, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 129, 0, 0, 0, {0, 0, 0}},
       {OBELISK_RT_RANDOM_EQ_V1, 1},
       {OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, UINT32_MAX},
       {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 129, 0, 0},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 129, 0, 0, 0, {0, 0, 0}},
       {OBELISK_RT_RANDOM_GT_V1, 1},
       {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 129, 0, 1},
       {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 129, 0, 0, 0, {0, 0, 0}},
       {OBELISK_RT_RANDOM_GT_V1, 1},
       {OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1},
       {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, UINT32_MAX}},
      OBELISK_RT_RANDOM_PROGRAM_HAS_DIST |
          OBELISK_RT_RANDOM_PROGRAM_EXACT_WEIGHTED,
      {}, {},
      {{0, UINT32_MAX, 0, 1, 0, 1, 0, 0, OBELISK_RT_RANDOM_DIST_RAW_WEIGHT},
       {0, UINT32_MAX, 0, 1, 1, 1, 1, 1, OBELISK_RT_RANDOM_DIST_RAW_WEIGHT}});
  uint64_t start[] = {0, 0, 0}, mask[] = {UINT64_MAX, UINT64_MAX, 3};
  uint64_t captures[] = {0, 1, 0, 0, 3, 0}, assignment[3] = {};
  uint32_t widths[] = {129, 129}, success = 0;
  uint64_t state = 713, companionSeen = 0;
  unsigned ones = 0;
  for (unsigned draw = 0; draw != 192; ++draw) {
    ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), start, mask, 3, 0, 1,
                  state, 15, captures, 6, widths, 2, assignment, &success,
                  &state),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    if (assignment[0] & 1) {
      ++ones;
      EXPECT_EQ(assignment[0], 1u);
      EXPECT_EQ(assignment[1], 0u);
      EXPECT_EQ(assignment[2], 0u);
    } else {
      companionSeen |= assignment[1];
    }
  }
  EXPECT_GT(ones, 118u);
  EXPECT_LT(ones, 173u);
  EXPECT_EQ(companionSeen, UINT64_MAX);
}

TEST_F(RandSolveWideTest, WeightedFull64BitRangeAndLargeMassNeverOverflow) {
  // :/ can describe a full 2^64 range, and := mass can exceed 128 bits.
  // Other constraints leave only the value 3; weights cannot make it fail.
  for (bool perRange : {false, true}) {
    uint32_t flags = OBELISK_RT_RANDOM_DIST_RAW_WEIGHT |
                     (perRange ? OBELISK_RT_RANDOM_DIST_PER_RANGE : 0u);
    auto bytes =
        program(64, 1,
                {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 64},
                 {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 64, 0, 0, 0, {3}},
                 {OBELISK_RT_RANDOM_EQ_V1, 1},
                 {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, UINT32_MAX},
                 {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 129},
                 {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 129, 0, 0, 0, {0, 0, 0}},
                 {OBELISK_RT_RANDOM_GT_V1, 1},
                 {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, UINT32_MAX}},
                OBELISK_RT_RANDOM_PROGRAM_HAS_DIST |
                    OBELISK_RT_RANDOM_PROGRAM_EXACT_WEIGHTED,
                {}, {}, {{0, UINT32_MAX, 0, 64, 0, 0, 0, 0, flags}});
    uint64_t start = 0, mask = UINT64_MAX,
             captures[] = {UINT64_MAX, UINT64_MAX, 1};
    uint64_t assignment = 0, state = 713;
    uint32_t width = 129, success = 0;
    ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), &start, &mask, 1, 0, 1,
                  state, 15, captures, 3, &width, 1, &assignment, &success,
                  &state),
              OBELISK_RT_OK);
    EXPECT_EQ(success, 1u);
    EXPECT_EQ(assignment, 3u);
  }
}

TEST_F(RandSolveWideTest, RetainsMoreThanSixtyFourSoftPriorities) {
  // IEEE 1800-2023 18.5.13.1 places no bound on the number of soft
  // constraints: 70 preferences that each set one bit are all retained except
  // the one a hard constraint contradicts.
  std::vector<Instruction> instructions = {
      {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, 5},
      {OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1},
      {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, UINT32_MAX}};
  for (uint32_t priority = 0; priority != 70; ++priority) {
    instructions.push_back({OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 1, 0, priority});
    instructions.push_back(
        {OBELISK_RT_RANDOM_END_SOFT_V1, 1, 0, UINT32_MAX, priority});
  }
  auto bytes = program(70, 0, instructions,
                       OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT |
                           OBELISK_RT_RANDOM_PROGRAM_EXACT_UNIFORM);
  uint64_t start[] = {0, 0}, mask[] = {UINT64_MAX, 63}, assignment[2] = {};
  uint64_t state = 713;
  uint32_t success = 0;
  ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                context, bytes.data(), bytes.size(), start, mask, 2, 0, 1,
                state, 15, nullptr, 0, nullptr, 0, assignment, &success,
                &state),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(assignment[0], ~(uint64_t{1} << 5));
  EXPECT_EQ(assignment[1], 63u);
}

TEST_F(RandSolveWideTest, WeightedExpressionTargetWeighsEachValue) {
  // IEEE 1800-2023 18.5.3: (a + b) dist {0 := 1, [1:6] :/ 1} over two 2-bit
  // fields weighs values of the expression, not (a, b) pairs: the value 3
  // has four witnesses and 6 has one, yet both get 1/12 of the draws.
  std::vector<Instruction> sum = {
      {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
      {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 0, {3}},
      {OBELISK_RT_RANDOM_BIT_AND_V1, 4},
      {OBELISK_RT_RANDOM_CAST_V1, 3},
      {OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 4},
      {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 4, 0, 0, 0, {2}},
      {OBELISK_RT_RANDOM_SHIFT_RIGHT_V1, 4},
      {OBELISK_RT_RANDOM_CAST_V1, 3},
      {OBELISK_RT_RANDOM_ADD_V1, 3}};
  std::vector<Instruction> instructions = sum;
  instructions.push_back({OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 3, 0, 0, 0, {6}});
  instructions.push_back({OBELISK_RT_RANDOM_LE_V1, 1});
  instructions.push_back({OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, UINT32_MAX});
  instructions.push_back({OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 64});
  instructions.push_back({OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 64, 0, 0, 0, {0}});
  instructions.push_back({OBELISK_RT_RANDOM_GT_V1, 1});
  instructions.push_back({OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, UINT32_MAX});
  instructions.insert(instructions.end(), sum.begin(), sum.end());
  for (uint32_t bit = 0; bit != 3; ++bit)
    instructions.push_back(
        {OBELISK_RT_RANDOM_END_DIST_TARGET_BIT_V1, 3, 0, 0, bit});
  uint32_t target = OBELISK_RT_RANDOM_DIST_RAW_WEIGHT |
                    OBELISK_RT_RANDOM_DIST_EXPRESSION_TARGET;
  auto bytes = program(
      4, 1, instructions,
      OBELISK_RT_RANDOM_PROGRAM_HAS_DIST |
          OBELISK_RT_RANDOM_PROGRAM_EXACT_WEIGHTED,
      {}, {},
      {{0, UINT32_MAX, 0, 3, 0, 1, 0, 0, target},
       {0, UINT32_MAX, 0, 3, 1, 6, 1, 0,
        target | OBELISK_RT_RANDOM_DIST_PER_RANGE}});
  uint64_t start = 0, mask = 15, weight = 1, assignment = 0, state = 713;
  uint32_t width = 64, success = 0;
  unsigned values[8] = {};
  for (unsigned draw = 0; draw != 1200; ++draw) {
    ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), &start, &mask, 1, 0, 1,
                  state, 15, &weight, 1, &width, 1, &assignment, &success,
                  &state),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    ASSERT_LT(assignment, 16u);
    ++values[(assignment & 3) + (assignment >> 2)];
  }
  EXPECT_EQ(values[7], 0u);
  EXPECT_GT(values[0], 530u);
  EXPECT_LT(values[0], 670u);
  for (unsigned value : {3u, 6u}) {
    EXPECT_GT(values[value], 65u);
    EXPECT_LT(values[value], 140u);
  }
}

TEST_F(RandSolveWideTest, WeightedProjectionNormalizesHugeLegalCardinalities) {
  // The full-range item has 2^64-1 legal values after excluding zero, yet
  // retains the same :/ mass as singleton 1. This exercises a large exact
  // denominator and overlapping contributions without traversing the domain.
  uint32_t raw =
      OBELISK_RT_RANDOM_DIST_RAW_WEIGHT | OBELISK_RT_RANDOM_DIST_PER_RANGE;
  auto bytes =
      program(64, 1,
              {{OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, 64},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 64, 0, 0, 0, {0}},
               {OBELISK_RT_RANDOM_NE_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, UINT32_MAX},
               {OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 129},
               {OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 129, 0, 0, 0, {0, 0, 0}},
               {OBELISK_RT_RANDOM_GT_V1, 1},
               {OBELISK_RT_RANDOM_END_HARD_V1, 1, 0, UINT32_MAX}},
              OBELISK_RT_RANDOM_PROGRAM_HAS_DIST |
                  OBELISK_RT_RANDOM_PROGRAM_EXACT_WEIGHTED,
              {}, {},
              {{0, UINT32_MAX, 0, 64, 0, 0, 0, 0, raw},
               {0, UINT32_MAX, 0, 64, 1, 1, 1, 0, raw}});
  uint64_t start = 0, mask = UINT64_MAX,
           captures[] = {UINT64_MAX, UINT64_MAX, 1};
  uint64_t assignment = 0, state = 713, otherBits = 0;
  uint32_t width = 129, success = 0;
  unsigned ones = 0;
  for (unsigned draw = 0; draw != 64; ++draw) {
    ASSERT_EQ(obelisk_rt_v1_random_solve_wide_modes_state(
                  context, bytes.data(), bytes.size(), &start, &mask, 1, 0, 1,
                  state, 15, captures, 3, &width, 1, &assignment, &success,
                  &state),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    ASSERT_NE(assignment, 0u);
    ones += assignment == 1;
    otherBits |= assignment;
  }
  EXPECT_GT(ones, 14u);
  EXPECT_LT(ones, 50u);
  EXPECT_EQ(otherBits, UINT64_MAX);
}

#endif

} // namespace
