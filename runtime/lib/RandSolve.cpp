//===- RandSolve.cpp - Constrained-random residual solver -----------------===//

#include "obelisk/Runtime/Runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace {

struct Instruction {
  uint8_t opcode;
  uint8_t width;
  uint8_t flags;
  uint32_t operand;
  uint64_t immediate;
};

struct Value {
  uint64_t bits;
  unsigned width;
};

uint16_t read16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t read32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t read64(const uint8_t *bytes) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
  return value;
}

uint64_t widthMask(unsigned width) {
  return width == 64 ? std::numeric_limits<uint64_t>::max()
                     : (uint64_t{1} << width) - 1;
}

uint64_t normalize(uint64_t value, unsigned width) {
  return value & widthMask(width);
}

uint64_t compressBits(uint64_t value, uint64_t mask) {
  uint64_t compressed = 0;
  unsigned output = 0;
  for (unsigned input = 0; input != 64; ++input) {
    uint64_t bit = uint64_t{1} << input;
    if ((mask & bit) == 0)
      continue;
    if ((value & bit) != 0)
      compressed |= uint64_t{1} << output;
    ++output;
  }
  return compressed;
}

uint64_t expandBits(uint64_t value, uint64_t mask) {
  uint64_t expanded = 0;
  unsigned input = 0;
  for (unsigned output = 0; output != 64; ++output) {
    uint64_t bit = uint64_t{1} << output;
    if ((mask & bit) == 0)
      continue;
    if ((value & (uint64_t{1} << input)) != 0)
      expanded |= bit;
    ++input;
  }
  return expanded;
}

unsigned countBits(uint64_t value) {
  unsigned count = 0;
  while (value != 0) {
    value &= value - 1;
    ++count;
  }
  return count;
}

uint64_t extendSigned(uint64_t value, unsigned sourceWidth,
                      unsigned targetWidth) {
  value = normalize(value, sourceWidth);
  if (sourceWidth < targetWidth && ((value >> (sourceWidth - 1)) & 1) != 0)
    value |= widthMask(targetWidth) & ~widthMask(sourceWidth);
  return normalize(value, targetWidth);
}

bool isUnary(uint8_t opcode) {
  return opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
         opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1;
}

bool isBinary(uint8_t opcode) {
  return (opcode >= OBELISK_RT_RANDOM_ADD_V1 &&
          opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1) ||
         (opcode >= OBELISK_RT_RANDOM_DIV_V1 &&
          opcode <= OBELISK_RT_RANDOM_POWER_V1);
}

uint64_t signedMagnitude(uint64_t value, unsigned width) {
  value = normalize(value, width);
  return ((value >> (width - 1)) & 1) != 0
             ? normalize(uint64_t{0} - value, width)
             : value;
}

uint64_t power(uint64_t base, uint64_t exponent, unsigned width) {
  uint64_t result = 1;
  base = normalize(base, width);
  while (exponent != 0) {
    if ((exponent & 1) != 0)
      result = normalize(result * base, width);
    exponent >>= 1;
    if (exponent != 0)
      base = normalize(base * base, width);
  }
  return normalize(result, width);
}

bool validateInstruction(const Instruction &instruction,
                         uint32_t aggregateWidth, uint32_t captureCount,
                         size_t &depth, size_t &maxDepth, bool &sawHard,
                         bool &sawSoft) {
  if (instruction.width == 0 || instruction.width > 64 ||
      (instruction.flags & ~OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0)
    return false;
  switch (instruction.opcode) {
  case OBELISK_RT_RANDOM_PUSH_VARIABLE_V1:
    if (instruction.operand >= aggregateWidth ||
        instruction.width > aggregateWidth - instruction.operand)
      return false;
    ++depth;
    break;
  case OBELISK_RT_RANDOM_PUSH_CAPTURE_V1:
    if (instruction.operand >= captureCount)
      return false;
    ++depth;
    break;
  case OBELISK_RT_RANDOM_PUSH_LITERAL_V1:
    ++depth;
    break;
  case OBELISK_RT_RANDOM_END_HARD_V1:
  case OBELISK_RT_RANDOM_END_SOFT_V1:
    if (instruction.width != 1 || depth != 1)
      return false;
    depth = 0;
    sawHard |= instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1;
    sawSoft |= instruction.opcode == OBELISK_RT_RANDOM_END_SOFT_V1;
    break;
  case OBELISK_RT_RANDOM_SELECT_V1:
    if (depth < 3)
      return false;
    depth -= 2;
    break;
  default:
    if (isUnary(instruction.opcode)) {
      if (depth < 1)
        return false;
    } else if (isBinary(instruction.opcode)) {
      if (depth < 2)
        return false;
      --depth;
    } else {
      return false;
    }
    break;
  }
  maxDepth = std::max(maxDepth, depth);
  return true;
}

bool compare(Value lhs, Value rhs, bool isSigned, uint8_t opcode) {
  unsigned width = std::max(lhs.width, rhs.width);
  uint64_t left = isSigned ? extendSigned(lhs.bits, lhs.width, width)
                           : normalize(lhs.bits, width);
  uint64_t right = isSigned ? extendSigned(rhs.bits, rhs.width, width)
                            : normalize(rhs.bits, width);
  if (isSigned) {
    uint64_t sign = uint64_t{1} << (width - 1);
    left ^= sign;
    right ^= sign;
  }
  switch (opcode) {
  case OBELISK_RT_RANDOM_EQ_V1:
    return left == right;
  case OBELISK_RT_RANDOM_NE_V1:
    return left != right;
  case OBELISK_RT_RANDOM_GE_V1:
    return left >= right;
  case OBELISK_RT_RANDOM_GT_V1:
    return left > right;
  case OBELISK_RT_RANDOM_LE_V1:
    return left <= right;
  case OBELISK_RT_RANDOM_LT_V1:
    return left < right;
  default:
    return false;
  }
}

bool evaluate(const std::vector<Instruction> &instructions,
              std::vector<Value> &stack, uint64_t assignment,
              const uint64_t *captures, bool &hard, bool &soft) {
  stack.clear();
  hard = true;
  soft = true;
  for (const Instruction &instruction : instructions) {
    auto push = [&](uint64_t bits, unsigned width) {
      stack.push_back({normalize(bits, width), width});
    };
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
      push(assignment >> instruction.operand, instruction.width);
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
      push(captures[instruction.operand], instruction.width);
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
      push(instruction.immediate, instruction.width);
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
        instruction.opcode == OBELISK_RT_RANDOM_END_SOFT_V1) {
      bool truth = stack.back().bits != 0;
      stack.pop_back();
      if (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1)
        hard &= truth;
      else
        soft &= truth;
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_SELECT_V1) {
      Value falseValue = stack.back();
      stack.pop_back();
      Value trueValue = stack.back();
      stack.pop_back();
      Value condition = stack.back();
      stack.pop_back();
      Value selected = condition.bits != 0 ? trueValue : falseValue;
      bool isSigned =
          (instruction.flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
      push(isSigned
               ? extendSigned(selected.bits, selected.width, instruction.width)
               : normalize(selected.bits, instruction.width),
           instruction.width);
      continue;
    }
    if (isUnary(instruction.opcode)) {
      Value input = stack.back();
      stack.pop_back();
      uint64_t result = input.bits;
      switch (instruction.opcode) {
      case OBELISK_RT_RANDOM_CAST_V1:
      case OBELISK_RT_RANDOM_POS_V1:
        result = (instruction.flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED)
                     ? extendSigned(input.bits, input.width, instruction.width)
                     : normalize(input.bits, instruction.width);
        break;
      case OBELISK_RT_RANDOM_NEG_V1:
        result = uint64_t{0} -
                 ((instruction.flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED)
                      ? extendSigned(input.bits, input.width, instruction.width)
                      : normalize(input.bits, instruction.width));
        break;
      case OBELISK_RT_RANDOM_BIT_NOT_V1:
        result =
            ~((instruction.flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED)
                  ? extendSigned(input.bits, input.width, instruction.width)
                  : normalize(input.bits, instruction.width));
        break;
      case OBELISK_RT_RANDOM_REDUCE_AND_V1:
        result = normalize(input.bits, input.width) == widthMask(input.width);
        break;
      case OBELISK_RT_RANDOM_REDUCE_OR_V1:
        result = input.bits != 0;
        break;
      case OBELISK_RT_RANDOM_REDUCE_XOR_V1:
        result = static_cast<uint64_t>(__builtin_parityll(input.bits));
        break;
      case OBELISK_RT_RANDOM_REDUCE_NAND_V1:
        result = normalize(input.bits, input.width) != widthMask(input.width);
        break;
      case OBELISK_RT_RANDOM_REDUCE_NOR_V1:
        result = input.bits == 0;
        break;
      case OBELISK_RT_RANDOM_REDUCE_XNOR_V1:
        result = !__builtin_parityll(input.bits);
        break;
      case OBELISK_RT_RANDOM_LOGICAL_NOT_V1:
        result = input.bits == 0;
        break;
      default:
        return false;
      }
      push(result, instruction.width);
      continue;
    }

    Value rhs = stack.back();
    stack.pop_back();
    Value lhs = stack.back();
    stack.pop_back();
    uint64_t result = 0;
    bool isSigned =
        (instruction.flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
    uint64_t left = isSigned
                        ? extendSigned(lhs.bits, lhs.width, instruction.width)
                        : normalize(lhs.bits, instruction.width);
    uint64_t right = isSigned
                         ? extendSigned(rhs.bits, rhs.width, instruction.width)
                         : normalize(rhs.bits, instruction.width);
    switch (instruction.opcode) {
    case OBELISK_RT_RANDOM_ADD_V1:
      result = left + right;
      break;
    case OBELISK_RT_RANDOM_SUB_V1:
      result = left - right;
      break;
    case OBELISK_RT_RANDOM_MUL_V1:
      result = left * right;
      break;
    case OBELISK_RT_RANDOM_BIT_AND_V1:
      result = left & right;
      break;
    case OBELISK_RT_RANDOM_BIT_OR_V1:
      result = left | right;
      break;
    case OBELISK_RT_RANDOM_BIT_XOR_V1:
      result = left ^ right;
      break;
    case OBELISK_RT_RANDOM_BIT_XNOR_V1:
      result = ~(left ^ right);
      break;
    case OBELISK_RT_RANDOM_DIV_V1:
    case OBELISK_RT_RANDOM_MOD_V1:
      if (right == 0)
        return false;
      if (isSigned) {
        bool leftNegative = ((left >> (instruction.width - 1)) & 1) != 0;
        bool rightNegative = ((right >> (instruction.width - 1)) & 1) != 0;
        uint64_t leftMagnitude = signedMagnitude(left, instruction.width);
        uint64_t rightMagnitude = signedMagnitude(right, instruction.width);
        if (instruction.opcode == OBELISK_RT_RANDOM_DIV_V1) {
          result = leftMagnitude / rightMagnitude;
          if (leftNegative != rightNegative)
            result = uint64_t{0} - result;
        } else {
          result = leftMagnitude % rightMagnitude;
          if (leftNegative)
            result = uint64_t{0} - result;
        }
      } else if (instruction.opcode == OBELISK_RT_RANDOM_DIV_V1) {
        result = left / right;
      } else {
        result = left % right;
      }
      break;
    case OBELISK_RT_RANDOM_SHIFT_LEFT_V1:
      result = rhs.bits >= instruction.width ? 0 : left << rhs.bits;
      break;
    case OBELISK_RT_RANDOM_SHIFT_RIGHT_V1:
      result = rhs.bits >= instruction.width ? 0 : left >> rhs.bits;
      break;
    case OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1:
      if (rhs.bits >= instruction.width) {
        result = ((left >> (instruction.width - 1)) & 1) != 0
                     ? widthMask(instruction.width)
                     : 0;
      } else if (rhs.bits == 0) {
        result = left;
      } else {
        result = left >> rhs.bits;
        if (((left >> (instruction.width - 1)) & 1) != 0)
          result |= widthMask(instruction.width)
                    << (instruction.width - rhs.bits);
      }
      break;
    case OBELISK_RT_RANDOM_POWER_V1:
      result = power(left, rhs.bits, instruction.width);
      break;
    case OBELISK_RT_RANDOM_EQ_V1:
    case OBELISK_RT_RANDOM_NE_V1:
    case OBELISK_RT_RANDOM_GE_V1:
    case OBELISK_RT_RANDOM_GT_V1:
    case OBELISK_RT_RANDOM_LE_V1:
    case OBELISK_RT_RANDOM_LT_V1:
      result = compare(lhs, rhs, isSigned, instruction.opcode);
      break;
    case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
      result = lhs.bits != 0 && rhs.bits != 0;
      break;
    case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
      result = lhs.bits != 0 || rhs.bits != 0;
      break;
    case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
      result = lhs.bits == 0 || rhs.bits != 0;
      break;
    case OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1:
      result = (lhs.bits != 0) == (rhs.bits != 0);
      break;
    default:
      return false;
    }
    push(result, instruction.width);
  }
  return stack.empty();
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_random_solve_masked(
    obelisk_rt_context *context, const uint8_t *program, uint64_t programSize,
    uint64_t start, uint64_t mutableMask, uint64_t maxAttempts,
    const uint64_t *captures, uint64_t captureCount, uint64_t *outAssignment,
    uint32_t *outSuccess) {
  if (!context || !program || !outAssignment || !outSuccess ||
      (captureCount != 0 && !captures) ||
      programSize < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outAssignment = 0;
  *outSuccess = 0;
  if (read32(program) != OBELISK_RT_RANDOM_PROGRAM_MAGIC ||
      read16(program + 4) != OBELISK_RT_RANDOM_PROGRAM_VERSION ||
      read16(program + 6) != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint32_t aggregateWidth = read32(program + 8);
  uint32_t instructionCount = read32(program + 12);
  uint32_t encodedCaptures = read32(program + 16);
  uint32_t programFlags = read32(program + 20);
  if (aggregateWidth > 64 || encodedCaptures != captureCount ||
      (programFlags & ~OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0 ||
      programSize != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE +
                         static_cast<uint64_t>(instructionCount) *
                             OBELISK_RT_RANDOM_INSTRUCTION_SIZE)
    return OBELISK_RT_INVALID_ARGUMENT;

  try {
    std::vector<Instruction> instructions;
    instructions.reserve(instructionCount);
    size_t depth = 0, maxDepth = 0;
    bool sawHard = false, sawSoft = false;
    const uint8_t *cursor = program + OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE;
    for (uint32_t index = 0; index != instructionCount; ++index) {
      Instruction instruction{cursor[0], cursor[1], cursor[2],
                              read32(cursor + 4), read64(cursor + 8)};
      if (cursor[3] != 0 ||
          !validateInstruction(instruction, aggregateWidth, encodedCaptures,
                               depth, maxDepth, sawHard, sawSoft))
        return OBELISK_RT_INVALID_ARGUMENT;
      instructions.push_back(instruction);
      cursor += OBELISK_RT_RANDOM_INSTRUCTION_SIZE;
    }
    bool encodedSoft = (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0;
    if (depth != 0 || !sawHard || sawSoft != encodedSoft)
      return OBELISK_RT_INVALID_ARGUMENT;

    uint64_t mask = widthMask(aggregateWidth);
    mutableMask &= mask;
    unsigned mutableWidth = countBits(mutableMask);
    uint64_t domain = mutableWidth == 64 ? std::numeric_limits<uint64_t>::max()
                                         : uint64_t{1} << mutableWidth;
    uint64_t attempts =
        mutableWidth == 64 ? maxAttempts : std::min(maxAttempts, domain);
    bool complete = mutableWidth != 64 && attempts == domain;
    uint64_t fixed = (start & mask) & ~mutableMask;
    uint64_t candidateIndex = compressBits(start, mutableMask);
    uint64_t candidate = fixed | expandBits(candidateIndex, mutableMask);
    uint64_t hardFallback = 0;
    bool hasHardFallback = false;
    std::vector<Value> stack;
    stack.reserve(maxDepth);
    for (uint64_t attempt = 0; attempt != attempts; ++attempt) {
      bool hard = false, soft = false;
      if (!evaluate(instructions, stack, candidate, captures, hard, soft))
        return OBELISK_RT_INVALID_ARGUMENT;
      if (hard && (!encodedSoft || soft)) {
        *outAssignment = candidate;
        *outSuccess = 1;
        return OBELISK_RT_OK;
      }
      if (hard && !hasHardFallback) {
        hardFallback = candidate;
        hasHardFallback = true;
      }
      candidateIndex = (candidateIndex + 1) & widthMask(mutableWidth);
      candidate = fixed | expandBits(candidateIndex, mutableMask);
    }
    // A soft constraint may be dropped only after a complete finite-domain
    // traversal proves that no preferred solution exists.
    if (encodedSoft && complete && hasHardFallback) {
      *outAssignment = hardFallback;
      *outSuccess = 1;
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_random_solve(
    obelisk_rt_context *context, const uint8_t *program, uint64_t programSize,
    uint64_t start, uint64_t maxAttempts, const uint64_t *captures,
    uint64_t captureCount, uint64_t *outAssignment, uint32_t *outSuccess) {
  return obelisk_rt_v1_random_solve_masked(
      context, program, programSize, start, UINT64_MAX, maxAttempts, captures,
      captureCount, outAssignment, outSuccess);
}
