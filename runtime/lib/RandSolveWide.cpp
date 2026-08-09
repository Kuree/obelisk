//===- RandSolveWide.cpp - Wide constrained-random fallback --------------===//

#include "obelisk/Runtime/Runtime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace {

struct WideValue {
  uint32_t width = 1;
  std::vector<uint64_t> words{0};

  WideValue() = default;
  explicit WideValue(uint32_t width)
      : width(width), words((static_cast<uint64_t>(width) + 63) / 64, 0) {}
};

struct WideInstruction {
  uint8_t opcode = 0;
  uint8_t flags = 0;
  uint32_t width = 0;
  uint32_t operand = 0;
  uint32_t auxiliary = 0;
  WideValue literal;
};

struct WideProgram {
  uint32_t aggregateWidth = 0;
  uint32_t maxDepth = 0;
  bool hasSoft = false;
  std::vector<uint32_t> captureWidths;
  std::vector<uint64_t> captureOffsets;
  std::vector<WideInstruction> instructions;
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
  uint64_t result = 0;
  for (unsigned index = 0; index != 8; ++index)
    result |= static_cast<uint64_t>(bytes[index]) << (index * 8);
  return result;
}

uint64_t highMask(uint32_t width) {
  unsigned used = width % 64;
  return used == 0 ? UINT64_MAX : (uint64_t{1} << used) - 1;
}

void normalize(WideValue &value) {
  value.words.back() &= highMask(value.width);
}

bool bit(const WideValue &value, uint32_t index) {
  return index < value.width &&
         ((value.words[index / 64] >> (index % 64)) & 1) != 0;
}

void setBit(WideValue &value, uint32_t index, bool selected) {
  uint64_t mask = uint64_t{1} << (index % 64);
  if (selected)
    value.words[index / 64] |= mask;
  else
    value.words[index / 64] &= ~mask;
}

bool truth(const WideValue &value) {
  return std::any_of(value.words.begin(), value.words.end(),
                     [](uint64_t word) { return word != 0; });
}

WideValue booleanValue(bool value, uint32_t width = 1) {
  WideValue result(width);
  result.words.front() = value;
  return result;
}

WideValue resize(const WideValue &input, uint32_t width, bool signExtend) {
  WideValue result(width);
  size_t common = std::min(result.words.size(), input.words.size());
  std::copy_n(input.words.begin(), common, result.words.begin());
  if (signExtend && width > input.width && bit(input, input.width - 1)) {
    for (uint32_t index = input.width; index != width; ++index)
      setBit(result, index, true);
  }
  normalize(result);
  return result;
}

WideValue extract(const uint64_t *words, uint64_t wordCount, uint32_t offset,
                  uint32_t width) {
  WideValue result(width);
  uint64_t firstWord = offset / 64;
  unsigned shift = offset % 64;
  for (size_t index = 0; index != result.words.size(); ++index) {
    uint64_t source = firstWord + index;
    if (source < wordCount)
      result.words[index] = words[source] >> shift;
    if (shift != 0 && source + 1 < wordCount)
      result.words[index] |= words[source + 1] << (64 - shift);
  }
  normalize(result);
  return result;
}

int compareUnsigned(const WideValue &lhs, const WideValue &rhs) {
  for (size_t index = lhs.words.size(); index != 0; --index) {
    if (lhs.words[index - 1] == rhs.words[index - 1])
      continue;
    return lhs.words[index - 1] < rhs.words[index - 1] ? -1 : 1;
  }
  return 0;
}

WideValue bitwise(const WideValue &lhs, const WideValue &rhs, uint32_t width,
                  uint8_t opcode) {
  WideValue result(width);
  for (size_t index = 0; index != result.words.size(); ++index) {
    switch (opcode) {
    case OBELISK_RT_RANDOM_BIT_AND_V1:
      result.words[index] = lhs.words[index] & rhs.words[index];
      break;
    case OBELISK_RT_RANDOM_BIT_OR_V1:
      result.words[index] = lhs.words[index] | rhs.words[index];
      break;
    case OBELISK_RT_RANDOM_BIT_XOR_V1:
      result.words[index] = lhs.words[index] ^ rhs.words[index];
      break;
    default:
      result.words[index] = ~(lhs.words[index] ^ rhs.words[index]);
      break;
    }
  }
  normalize(result);
  return result;
}

WideValue add(const WideValue &lhs, const WideValue &rhs, uint32_t width) {
  WideValue result(width);
  uint64_t carry = 0;
  for (size_t index = 0; index != result.words.size(); ++index) {
    uint64_t partial = lhs.words[index] + carry;
    bool firstCarry = partial < lhs.words[index];
    uint64_t sum = partial + rhs.words[index];
    bool secondCarry = sum < partial;
    result.words[index] = sum;
    carry = firstCarry || secondCarry;
  }
  normalize(result);
  return result;
}

WideValue negate(const WideValue &input) {
  WideValue result(input.width);
  uint64_t carry = 1;
  for (size_t index = 0; index != result.words.size(); ++index) {
    uint64_t inverted = ~input.words[index];
    result.words[index] = inverted + carry;
    carry = carry && result.words[index] == 0;
  }
  normalize(result);
  return result;
}

WideValue subtract(const WideValue &lhs, const WideValue &rhs, uint32_t width) {
  return add(lhs, negate(rhs), width);
}

WideValue multiply(const WideValue &lhs, const WideValue &rhs, uint32_t width) {
  WideValue result(width);
  for (size_t left = 0; left != lhs.words.size(); ++left) {
    uint64_t carry = 0;
    for (size_t right = 0; right + left < result.words.size(); ++right) {
      size_t output = left + right;
      __uint128_t product =
          static_cast<__uint128_t>(lhs.words[left]) * rhs.words[right] +
          result.words[output] + carry;
      result.words[output] = static_cast<uint64_t>(product);
      carry = static_cast<uint64_t>(product >> 64);
    }
  }
  normalize(result);
  return result;
}

bool shiftAmount(const WideValue &input, uint32_t width, uint32_t &amount) {
  if (input.words.size() > 1 &&
      std::any_of(input.words.begin() + 1, input.words.end(),
                  [](uint64_t word) { return word != 0; }))
    return false;
  if (input.words.front() >= width)
    return false;
  amount = static_cast<uint32_t>(input.words.front());
  return true;
}

WideValue shiftLeft(const WideValue &input, uint32_t width, uint32_t amount) {
  WideValue result(width);
  size_t wordShift = amount / 64;
  unsigned bitShift = amount % 64;
  for (size_t output = result.words.size(); output != 0; --output) {
    size_t destination = output - 1;
    if (destination < wordShift)
      continue;
    size_t source = destination - wordShift;
    result.words[destination] |= input.words[source] << bitShift;
    if (bitShift != 0 && source != 0)
      result.words[destination] |= input.words[source - 1] >> (64 - bitShift);
  }
  normalize(result);
  return result;
}

WideValue shiftRight(const WideValue &input, uint32_t width, uint32_t amount,
                     bool arithmetic) {
  WideValue result(width);
  size_t wordShift = amount / 64;
  unsigned bitShift = amount % 64;
  for (size_t destination = 0; destination != result.words.size();
       ++destination) {
    size_t source = destination + wordShift;
    if (source >= input.words.size())
      continue;
    result.words[destination] |= input.words[source] >> bitShift;
    if (bitShift != 0 && source + 1 < input.words.size())
      result.words[destination] |= input.words[source + 1] << (64 - bitShift);
  }
  if (arithmetic && bit(input, width - 1))
    for (uint32_t index = width - amount; index != width; ++index)
      setBit(result, index, true);
  normalize(result);
  return result;
}

void unsignedDivMod(const WideValue &dividend, const WideValue &divisor,
                    WideValue &quotient, WideValue &remainder) {
  quotient = WideValue(dividend.width);
  remainder = WideValue(dividend.width);
  for (uint32_t index = dividend.width; index != 0; --index) {
    remainder = shiftLeft(remainder, remainder.width, 1);
    setBit(remainder, 0, bit(dividend, index - 1));
    if (compareUnsigned(remainder, divisor) >= 0) {
      remainder = subtract(remainder, divisor, remainder.width);
      setBit(quotient, index - 1, true);
    }
  }
}

WideValue power(WideValue base, const WideValue &exponent, uint32_t width) {
  WideValue result(width);
  result.words.front() = 1;
  for (uint32_t index = 0; index != exponent.width; ++index) {
    if (bit(exponent, index))
      result = multiply(result, base, width);
    if (index + 1 != exponent.width)
      base = multiply(base, base, width);
  }
  return result;
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

bool parseWideProgram(const uint8_t *bytes, size_t size, WideProgram &result,
                      const uint32_t *captureWidths, uint64_t captureCount,
                      uint64_t captureWordCount) {
  if (size < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2 ||
      read32(bytes) != OBELISK_RT_RANDOM_PROGRAM_MAGIC ||
      read16(bytes + 4) != OBELISK_RT_RANDOM_PROGRAM_VERSION_V2 ||
      read16(bytes + 6) != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2)
    return false;
  result.aggregateWidth = read32(bytes + 8);
  uint32_t instructionCount = read32(bytes + 12);
  uint32_t encodedCaptureCount = read32(bytes + 16);
  uint32_t flags = read32(bytes + 20);
  uint32_t literalWordCount = read32(bytes + 24);
  if (result.aggregateWidth == 0 || encodedCaptureCount != captureCount ||
      read32(bytes + 28) != 0 ||
      (flags & ~OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0)
    return false;
  result.hasSoft = (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0;
  uint64_t instructionBytes = static_cast<uint64_t>(instructionCount) *
                              OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
  uint64_t literalBytes = static_cast<uint64_t>(literalWordCount) * 8;
  uint64_t expectedSize = OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2;
  if (instructionBytes > UINT64_MAX - expectedSize)
    return false;
  expectedSize += instructionBytes;
  if (literalBytes > UINT64_MAX - expectedSize)
    return false;
  uint64_t literalOffset = expectedSize;
  expectedSize += literalBytes;
  if (expectedSize != size)
    return false;

  result.captureWidths.assign(encodedCaptureCount, 0);
  result.captureOffsets.assign(encodedCaptureCount, 0);
  result.instructions.reserve(instructionCount);
  std::vector<uint8_t> literalUse(literalWordCount, 0);
  size_t depth = 0;
  size_t maxDepth = 0;
  bool sawHard = false;
  bool sawSoft = false;
  uint64_t softPriorities = 0;
  for (uint32_t index = 0; index != instructionCount; ++index) {
    const uint8_t *cursor =
        bytes + OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2 +
        static_cast<uint64_t>(index) * OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
    WideInstruction instruction;
    instruction.opcode = cursor[0];
    instruction.flags = cursor[1];
    instruction.width = read32(cursor + 4);
    instruction.operand = read32(cursor + 8);
    instruction.auxiliary = read32(cursor + 12);
    if (read16(cursor + 2) != 0 || instruction.width == 0 ||
        (instruction.flags & ~OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0)
      return false;
    switch (instruction.opcode) {
    case OBELISK_RT_RANDOM_PUSH_VARIABLE_V1:
      if (instruction.operand >= result.aggregateWidth ||
          instruction.width > result.aggregateWidth - instruction.operand ||
          instruction.auxiliary != 0)
        return false;
      ++depth;
      break;
    case OBELISK_RT_RANDOM_PUSH_CAPTURE_V1:
      if (instruction.operand >= encodedCaptureCount ||
          instruction.auxiliary != 0)
        return false;
      result.captureWidths[instruction.operand] =
          std::max(result.captureWidths[instruction.operand],
                   instruction.width);
      ++depth;
      break;
    case OBELISK_RT_RANDOM_PUSH_LITERAL_V1: {
      uint64_t wordCount = (static_cast<uint64_t>(instruction.width) + 63) / 64;
      if (instruction.auxiliary > literalWordCount ||
          wordCount > literalWordCount - instruction.auxiliary)
        return false;
      instruction.literal = WideValue(instruction.width);
      for (uint64_t word = 0; word != wordCount; ++word) {
        uint64_t poolIndex = instruction.auxiliary + word;
        if (literalUse[poolIndex] != 0)
          return false;
        literalUse[poolIndex] = 1;
        instruction.literal.words[word] =
            read64(bytes + literalOffset + poolIndex * 8);
      }
      uint64_t final = instruction.literal.words.back();
      normalize(instruction.literal);
      if (final != instruction.literal.words.back())
        return false;
      ++depth;
      break;
    }
    case OBELISK_RT_RANDOM_END_HARD_V1:
    case OBELISK_RT_RANDOM_END_SOFT_V1:
      if (instruction.width != 1 || depth != 1 ||
          (instruction.operand != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
           instruction.operand >= 64))
        return false;
      if (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1) {
        if (instruction.auxiliary != 0)
          return false;
        sawHard = true;
      } else {
        if (instruction.auxiliary >= 64)
          return false;
        softPriorities |= uint64_t{1} << instruction.auxiliary;
        sawSoft = true;
      }
      depth = 0;
      break;
    case OBELISK_RT_RANDOM_SELECT_V1:
      if (instruction.auxiliary != 0 || depth < 3)
        return false;
      depth -= 2;
      break;
    default:
      if (instruction.auxiliary != 0)
        return false;
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
    result.instructions.push_back(std::move(instruction));
  }
  unsigned softCount = 0;
  if (softPriorities != 0) {
    softCount = 64;
    while (((softPriorities >> (softCount - 1)) & 1) == 0)
      --softCount;
  }
  uint64_t expectedSoftPriorities =
      softCount == 64 ? UINT64_MAX : (uint64_t{1} << softCount) - 1;
  if (depth != 0 || !sawHard || sawSoft != result.hasSoft ||
      softPriorities != expectedSoftPriorities ||
      std::any_of(literalUse.begin(), literalUse.end(),
                  [](uint8_t used) { return used != 1; }) ||
      std::any_of(result.captureWidths.begin(), result.captureWidths.end(),
                  [](uint32_t width) { return width == 0; }))
    return false;
  result.maxDepth = static_cast<uint32_t>(maxDepth);
  uint64_t requiredCaptureWords = 0;
  for (size_t index = 0; index != result.captureWidths.size(); ++index) {
    uint32_t logicalWidth = result.captureWidths[index];
    uint32_t storageWidth = captureWidths[index];
    uint64_t logicalWords =
        (static_cast<uint64_t>(logicalWidth) + 63) / 64;
    uint64_t storageWords =
        (static_cast<uint64_t>(storageWidth) + 63) / 64;
    if (storageWidth < logicalWidth || storageWords != logicalWords)
      return false;
    result.captureOffsets[index] = requiredCaptureWords;
    if (storageWords > UINT64_MAX - requiredCaptureWords)
      return false;
    requiredCaptureWords += storageWords;
  }
  return requiredCaptureWords == captureWordCount;
}

bool compare(const WideValue &lhs, const WideValue &rhs, bool isSigned,
             uint8_t opcode) {
  uint32_t width = std::max(lhs.width, rhs.width);
  WideValue left = resize(lhs, width, isSigned);
  WideValue right = resize(rhs, width, isSigned);
  int relation = 0;
  if (isSigned && bit(left, width - 1) != bit(right, width - 1))
    relation = bit(left, width - 1) ? -1 : 1;
  else
    relation = compareUnsigned(left, right);
  switch (opcode) {
  case OBELISK_RT_RANDOM_EQ_V1:
    return relation == 0;
  case OBELISK_RT_RANDOM_NE_V1:
    return relation != 0;
  case OBELISK_RT_RANDOM_GE_V1:
    return relation >= 0;
  case OBELISK_RT_RANDOM_GT_V1:
    return relation > 0;
  case OBELISK_RT_RANDOM_LE_V1:
    return relation <= 0;
  default:
    return relation < 0;
  }
}

bool evaluate(const WideProgram &program, const WideValue &assignment,
              const uint64_t *captureWords, uint64_t constraintMask,
              std::vector<WideValue> &stack, bool &hard,
              std::array<uint8_t, 64> &soft) {
  stack.clear();
  hard = true;
  soft.fill(1);
  for (const WideInstruction &instruction : program.instructions) {
    bool signedOperation =
        (instruction.flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
      stack.push_back(extract(assignment.words.data(), assignment.words.size(),
                              instruction.operand, instruction.width));
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
      stack.push_back(
          extract(captureWords + program.captureOffsets[instruction.operand],
                  (static_cast<uint64_t>(instruction.width) + 63) / 64, 0,
                  instruction.width));
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
      stack.push_back(instruction.literal);
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
        instruction.opcode == OBELISK_RT_RANDOM_END_SOFT_V1) {
      bool satisfied = truth(stack.back());
      stack.pop_back();
      bool enabled =
          instruction.operand == OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 ||
          ((constraintMask >> instruction.operand) & 1) == 0;
      satisfied |= !enabled;
      if (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1)
        hard &= satisfied;
      else
        soft[instruction.auxiliary] &= satisfied;
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_SELECT_V1) {
      WideValue falseValue = std::move(stack.back());
      stack.pop_back();
      WideValue trueValue = std::move(stack.back());
      stack.pop_back();
      WideValue condition = std::move(stack.back());
      stack.pop_back();
      stack.push_back(resize(truth(condition) ? trueValue : falseValue,
                             instruction.width, signedOperation));
      continue;
    }
    if (isUnary(instruction.opcode)) {
      WideValue input = std::move(stack.back());
      stack.pop_back();
      WideValue result;
      switch (instruction.opcode) {
      case OBELISK_RT_RANDOM_CAST_V1:
      case OBELISK_RT_RANDOM_POS_V1:
        result = resize(input, instruction.width, signedOperation);
        break;
      case OBELISK_RT_RANDOM_NEG_V1:
        result = negate(resize(input, instruction.width, signedOperation));
        break;
      case OBELISK_RT_RANDOM_BIT_NOT_V1:
        result = resize(input, instruction.width, signedOperation);
        for (uint64_t &word : result.words)
          word = ~word;
        normalize(result);
        break;
      case OBELISK_RT_RANDOM_REDUCE_AND_V1: {
        bool all = input.words.back() == highMask(input.width);
        for (size_t index = 0; all && index + 1 < input.words.size(); ++index)
          all = input.words[index] == UINT64_MAX;
        result = booleanValue(all, instruction.width);
        break;
      }
      case OBELISK_RT_RANDOM_REDUCE_OR_V1:
        result = booleanValue(truth(input), instruction.width);
        break;
      case OBELISK_RT_RANDOM_REDUCE_XOR_V1:
      case OBELISK_RT_RANDOM_REDUCE_XNOR_V1: {
        unsigned parity = 0;
        for (uint64_t word : input.words)
          parity ^= static_cast<unsigned>(__builtin_parityll(word));
        if (instruction.opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1)
          parity ^= 1;
        result = booleanValue(parity != 0, instruction.width);
        break;
      }
      case OBELISK_RT_RANDOM_REDUCE_NAND_V1: {
        bool all = input.words.back() == highMask(input.width);
        for (size_t index = 0; all && index + 1 < input.words.size(); ++index)
          all = input.words[index] == UINT64_MAX;
        result = booleanValue(!all, instruction.width);
        break;
      }
      default:
        result = booleanValue(!truth(input), instruction.width);
        break;
      }
      stack.push_back(std::move(result));
      continue;
    }

    WideValue rhs = std::move(stack.back());
    stack.pop_back();
    WideValue lhs = std::move(stack.back());
    stack.pop_back();
    WideValue result;
    if (instruction.opcode >= OBELISK_RT_RANDOM_EQ_V1 &&
        instruction.opcode <= OBELISK_RT_RANDOM_LT_V1) {
      result =
          booleanValue(compare(lhs, rhs, signedOperation, instruction.opcode),
                       instruction.width);
    } else if (instruction.opcode >= OBELISK_RT_RANDOM_LOGICAL_AND_V1 &&
               instruction.opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1) {
      bool left = truth(lhs), right = truth(rhs), value = false;
      switch (instruction.opcode) {
      case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
        value = left && right;
        break;
      case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
        value = left || right;
        break;
      case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
        value = !left || right;
        break;
      default:
        value = left == right;
        break;
      }
      result = booleanValue(value, instruction.width);
    } else {
      WideValue left = resize(lhs, instruction.width, signedOperation);
      WideValue right = resize(rhs, instruction.width, signedOperation);
      switch (instruction.opcode) {
      case OBELISK_RT_RANDOM_ADD_V1:
        result = add(left, right, instruction.width);
        break;
      case OBELISK_RT_RANDOM_SUB_V1:
        result = subtract(left, right, instruction.width);
        break;
      case OBELISK_RT_RANDOM_MUL_V1:
        result = multiply(left, right, instruction.width);
        break;
      case OBELISK_RT_RANDOM_BIT_AND_V1:
      case OBELISK_RT_RANDOM_BIT_OR_V1:
      case OBELISK_RT_RANDOM_BIT_XOR_V1:
      case OBELISK_RT_RANDOM_BIT_XNOR_V1:
        result = bitwise(left, right, instruction.width, instruction.opcode);
        break;
      case OBELISK_RT_RANDOM_DIV_V1:
      case OBELISK_RT_RANDOM_MOD_V1: {
        if (!truth(right))
          return false;
        bool leftNegative = signedOperation && bit(left, left.width - 1);
        bool rightNegative = signedOperation && bit(right, right.width - 1);
        WideValue leftMagnitude = leftNegative ? negate(left) : left;
        WideValue rightMagnitude = rightNegative ? negate(right) : right;
        WideValue quotient, remainder;
        unsignedDivMod(leftMagnitude, rightMagnitude, quotient, remainder);
        if (instruction.opcode == OBELISK_RT_RANDOM_DIV_V1) {
          result = leftNegative != rightNegative ? negate(quotient) : quotient;
        } else {
          result = leftNegative ? negate(remainder) : remainder;
        }
        break;
      }
      case OBELISK_RT_RANDOM_SHIFT_LEFT_V1:
      case OBELISK_RT_RANDOM_SHIFT_RIGHT_V1:
      case OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1: {
        uint32_t amount = 0;
        if (!shiftAmount(rhs, instruction.width, amount)) {
          bool fill =
              instruction.opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1 &&
              bit(left, instruction.width - 1);
          result = WideValue(instruction.width);
          if (fill) {
            std::fill(result.words.begin(), result.words.end(), UINT64_MAX);
            normalize(result);
          }
        } else if (instruction.opcode == OBELISK_RT_RANDOM_SHIFT_LEFT_V1) {
          result = shiftLeft(left, instruction.width, amount);
        } else {
          result = shiftRight(left, instruction.width, amount,
                              instruction.opcode ==
                                  OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1);
        }
        break;
      }
      case OBELISK_RT_RANDOM_POWER_V1:
        result = power(left, rhs, instruction.width);
        break;
      default:
        return false;
      }
    }
    stack.push_back(std::move(result));
  }
  return stack.empty();
}

bool allSoft(const std::array<uint8_t, 64> &soft) {
  return std::all_of(soft.begin(), soft.end(),
                     [](uint8_t value) { return value != 0; });
}

int compareSoft(const std::array<uint8_t, 64> &lhs,
                const std::array<uint8_t, 64> &rhs) {
  for (size_t index = lhs.size(); index != 0; --index) {
    if (lhs[index - 1] == rhs[index - 1])
      continue;
    return lhs[index - 1] > rhs[index - 1] ? 1 : -1;
  }
  return 0;
}

void advanceMutable(WideValue &candidate, const WideValue &mutableMask) {
  for (uint32_t index = 0; index != candidate.width; ++index) {
    if (!bit(mutableMask, index))
      continue;
    bool current = bit(candidate, index);
    setBit(candidate, index, !current);
    if (!current)
      return;
  }
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_random_solve_wide_modes_state(
    obelisk_rt_context *context, const uint8_t *programBytes,
    uint64_t programSize, const uint64_t *startWords,
    const uint64_t *mutableMaskWords, uint64_t assignmentWordCount,
    uint64_t constraintMask, uint64_t maxAttempts, uint64_t rngState,
    uint64_t rngIncrement, const uint64_t *captureWords,
    uint64_t captureWordCount, const uint32_t *captureWidths,
    uint64_t captureCount, uint64_t *outAssignmentWords, uint32_t *outSuccess,
    uint64_t *outRngState) {
  constexpr uint64_t maxAddressableWords =
      std::numeric_limits<size_t>::max() / sizeof(uint64_t);
  if (!context || !programBytes || !startWords || !mutableMaskWords ||
      !outAssignmentWords || !outSuccess || !outRngState ||
      (captureWordCount != 0 && !captureWords) ||
      (captureCount != 0 && !captureWidths) ||
      programSize > std::numeric_limits<size_t>::max() ||
      assignmentWordCount > maxAddressableWords ||
      captureWordCount > maxAddressableWords ||
      (rngIncrement & 1) == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outSuccess = 0;
  *outRngState = rngState;
  try {
    WideProgram program;
    if (!parseWideProgram(programBytes, static_cast<size_t>(programSize),
                          program, captureWidths, captureCount,
                          captureWordCount) ||
        assignmentWordCount !=
            (static_cast<uint64_t>(program.aggregateWidth) + 63) / 64)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t aggregateHighMask = highMask(program.aggregateWidth);
    if ((startWords[assignmentWordCount - 1] & ~aggregateHighMask) != 0 ||
        (mutableMaskWords[assignmentWordCount - 1] & ~aggregateHighMask) != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    for (size_t index = 0; index != program.captureWidths.size(); ++index) {
      uint32_t width = program.captureWidths[index];
      uint64_t words = (static_cast<uint64_t>(width) + 63) / 64;
      if ((captureWords[program.captureOffsets[index] + words - 1] &
           ~highMask(width)) != 0)
        return OBELISK_RT_INVALID_ARGUMENT;
    }
    std::fill_n(outAssignmentWords, static_cast<size_t>(assignmentWordCount),
                0);
    WideValue start(program.aggregateWidth);
    WideValue mutableMask(program.aggregateWidth);
    std::copy_n(startWords, start.words.size(), start.words.begin());
    std::copy_n(mutableMaskWords, mutableMask.words.size(),
                mutableMask.words.begin());
    normalize(start);
    normalize(mutableMask);

    unsigned mutableBits = 0;
    for (uint64_t word : mutableMask.words)
      mutableBits += static_cast<unsigned>(__builtin_popcountll(word));
    bool finiteDomain = mutableBits < 64;
    uint64_t domainSize = finiteDomain ? uint64_t{1} << mutableBits : 0;
    uint64_t attempts =
        finiteDomain ? std::min(maxAttempts, domainSize) : maxAttempts;
    bool complete = finiteDomain && attempts == domainSize;

    WideValue candidate = start;
    WideValue best(program.aggregateWidth);
    bool hasBest = false;
    std::array<uint8_t, 64> bestSoft{};
    std::array<uint8_t, 64> soft{};
    std::vector<WideValue> stack;
    stack.reserve(program.maxDepth);
    for (uint64_t attempt = 0; attempt != attempts; ++attempt) {
      bool hard = false;
      if (!evaluate(program, candidate, captureWords, constraintMask, stack,
                    hard, soft))
        return OBELISK_RT_INVALID_ARGUMENT;
      if (hard && (!program.hasSoft || allSoft(soft))) {
        std::copy(candidate.words.begin(), candidate.words.end(),
                  outAssignmentWords);
        *outSuccess = 1;
        return OBELISK_RT_OK;
      }
      if (hard && program.hasSoft &&
          (!hasBest || compareSoft(soft, bestSoft) > 0)) {
        best = candidate;
        bestSoft = soft;
        hasBest = true;
      }
      advanceMutable(candidate, mutableMask);
    }
    if (program.hasSoft && complete && hasBest) {
      std::copy(best.words.begin(), best.words.end(), outAssignmentWords);
      *outSuccess = 1;
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}
