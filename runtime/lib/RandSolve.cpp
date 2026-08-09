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

struct SolveBeforeEdge {
  uint64_t beforeMask;
  uint64_t afterMask;
  uint32_t constraintBlock;
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

bool constraintEnabled(uint32_t constraintBlock, uint64_t constraintMask) {
  return constraintBlock == OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 ||
         ((constraintMask >> constraintBlock) & 1) == 0;
}

bool buildSolveBeforeLayers(const std::vector<SolveBeforeEdge> &edges,
                            uint64_t mutableMask, uint64_t constraintMask,
                            std::vector<uint64_t> &layers) {
  std::vector<uint64_t> nodes;
  for (const SolveBeforeEdge &edge : edges) {
    if (!constraintEnabled(edge.constraintBlock, constraintMask))
      continue;
    uint64_t before = edge.beforeMask & mutableMask;
    uint64_t after = edge.afterMask & mutableMask;
    if (before == 0 || after == 0)
      continue;
    if (std::find(nodes.begin(), nodes.end(), before) == nodes.end())
      nodes.push_back(before);
    if (std::find(nodes.begin(), nodes.end(), after) == nodes.end())
      nodes.push_back(after);
  }

  layers.clear();
  while (!nodes.empty()) {
    uint64_t layer = 0;
    for (uint64_t node : nodes) {
      bool hasPredecessor = false;
      for (const SolveBeforeEdge &edge : edges) {
        if (!constraintEnabled(edge.constraintBlock, constraintMask) ||
            (edge.afterMask & mutableMask) != node)
          continue;
        uint64_t before = edge.beforeMask & mutableMask;
        if (before != 0 &&
            std::find(nodes.begin(), nodes.end(), before) != nodes.end()) {
          hasPredecessor = true;
          break;
        }
      }
      if (!hasPredecessor)
        layer |= node;
    }
    if (layer == 0)
      return false;
    layers.push_back(layer);
    nodes.erase(
        std::remove_if(nodes.begin(), nodes.end(),
                       [&](uint64_t node) { return (node & layer) != 0; }),
        nodes.end());
  }
  return true;
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
                         bool &sawSoft, uint64_t &softPriorities) {
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
    if (instruction.width != 1 || depth != 1 ||
        (instruction.operand != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
         instruction.operand >= 64) ||
        (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1 &&
         instruction.immediate != 0) ||
        (instruction.opcode == OBELISK_RT_RANDOM_END_SOFT_V1 &&
         instruction.immediate >= 64))
      return false;
    if (instruction.opcode == OBELISK_RT_RANDOM_END_SOFT_V1)
      softPriorities |= uint64_t{1} << instruction.immediate;
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
              uint64_t constraintMask, const uint64_t *captures, bool &hard,
              std::vector<uint8_t> &soft) {
  stack.clear();
  hard = true;
  std::fill(soft.begin(), soft.end(), uint8_t{1});
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
      bool enabled =
          instruction.operand == OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 ||
          ((constraintMask >> instruction.operand) & 1) == 0;
      truth |= !enabled;
      if (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1)
        hard &= truth;
      else
        soft[instruction.immediate] &= truth;
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

static obelisk_rt_status
randomSolveModesImpl(obelisk_rt_context *context, const uint8_t *program,
                     uint64_t programSize, uint64_t start, uint64_t mutableMask,
                     uint64_t constraintMask, uint64_t maxAttempts,
                     const uint64_t *captures, uint64_t captureCount,
                     uint64_t *outAssignment, uint32_t *outSuccess,
                     obelisk_rt_random_state_v1 *randomState) {
  if (!context || !program || !outAssignment || !outSuccess ||
      (captureCount != 0 && !captures) ||
      programSize < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE ||
      programSize > std::numeric_limits<size_t>::max())
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
      (programFlags & ~(OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT |
                        OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE)) != 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t instructionEnd = OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE +
                            static_cast<uint64_t>(instructionCount) *
                                OBELISK_RT_RANDOM_INSTRUCTION_SIZE;
  bool encodedSolveBefore =
      (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE) != 0;
  uint32_t solveEdgeCount = 0;
  if (encodedSolveBefore) {
    if (programSize < instructionEnd + OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE)
      return OBELISK_RT_INVALID_ARGUMENT;
    solveEdgeCount = read32(program + instructionEnd);
    instructionEnd += OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE +
                      static_cast<uint64_t>(solveEdgeCount) *
                          OBELISK_RT_RANDOM_SOLVE_EDGE_SIZE;
  }
  if (programSize != instructionEnd)
    return OBELISK_RT_INVALID_ARGUMENT;

  try {
    std::vector<Instruction> instructions;
    instructions.reserve(instructionCount);
    size_t depth = 0, maxDepth = 0;
    bool sawHard = false, sawSoft = false;
    uint64_t softPriorities = 0;
    const uint8_t *cursor = program + OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE;
    for (uint32_t index = 0; index != instructionCount; ++index) {
      Instruction instruction{cursor[0], cursor[1], cursor[2],
                              read32(cursor + 4), read64(cursor + 8)};
      if (cursor[3] != 0 ||
          !validateInstruction(instruction, aggregateWidth, encodedCaptures,
                               depth, maxDepth, sawHard, sawSoft,
                               softPriorities))
        return OBELISK_RT_INVALID_ARGUMENT;
      instructions.push_back(instruction);
      cursor += OBELISK_RT_RANDOM_INSTRUCTION_SIZE;
    }
    bool encodedSoft = (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0;
    unsigned softCount = 0;
    if (softPriorities != 0) {
      softCount = 64;
      while (((softPriorities >> (softCount - 1)) & 1) == 0)
        --softCount;
    }
    uint64_t expectedSoftPriorities =
        softCount == 64 ? UINT64_MAX : (uint64_t{1} << softCount) - 1;
    if (depth != 0 || !sawHard || sawSoft != encodedSoft ||
        softPriorities != expectedSoftPriorities)
      return OBELISK_RT_INVALID_ARGUMENT;

    std::vector<SolveBeforeEdge> solveEdges;
    solveEdges.reserve(solveEdgeCount);
    if (encodedSolveBefore) {
      cursor = program + OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE +
               static_cast<uint64_t>(instructionCount) *
                   OBELISK_RT_RANDOM_INSTRUCTION_SIZE +
               OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE;
      uint64_t aggregateMask = widthMask(aggregateWidth);
      std::vector<uint64_t> propertyMasks;
      for (uint32_t index = 0; index != solveEdgeCount; ++index) {
        SolveBeforeEdge edge{read64(cursor), read64(cursor + 8),
                             read32(cursor + 16)};
        uint32_t reserved = read32(cursor + 20);
        cursor += OBELISK_RT_RANDOM_SOLVE_EDGE_SIZE;
        if (edge.beforeMask == 0 || edge.afterMask == 0 ||
            (edge.beforeMask & ~aggregateMask) != 0 ||
            (edge.afterMask & ~aggregateMask) != 0 ||
            (edge.beforeMask & edge.afterMask) != 0 || reserved != 0 ||
            (edge.constraintBlock != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
             edge.constraintBlock >= 64))
          return OBELISK_RT_INVALID_ARGUMENT;
        for (uint64_t propertyMask : {edge.beforeMask, edge.afterMask}) {
          for (uint64_t existing : propertyMasks)
            if (existing != propertyMask && (existing & propertyMask) != 0)
              return OBELISK_RT_INVALID_ARGUMENT;
          if (std::find(propertyMasks.begin(), propertyMasks.end(),
                        propertyMask) == propertyMasks.end())
            propertyMasks.push_back(propertyMask);
        }
        solveEdges.push_back(edge);
      }
      std::vector<uint64_t> validationLayers;
      if (solveEdges.empty() ||
          !buildSolveBeforeLayers(solveEdges, aggregateMask, 0,
                                  validationLayers))
        return OBELISK_RT_INVALID_ARGUMENT;
    }

    uint64_t mask = widthMask(aggregateWidth);
    mutableMask &= mask;
    std::vector<uint64_t> solveLayers;
    bool activeSolveBefore = false;
    if (encodedSolveBefore) {
      if (!buildSolveBeforeLayers(solveEdges, mutableMask, constraintMask,
                                  solveLayers))
        return OBELISK_RT_INVALID_ARGUMENT;
      activeSolveBefore = !solveLayers.empty();
      // A stateless entry point cannot make exact non-power-of-two choices
      // for the conditional solve-order distribution. Compiler-generated
      // object randomization always uses the stateful entry point.
      if (activeSolveBefore && !randomState)
        return OBELISK_RT_INVALID_ARGUMENT;
    }
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
    std::vector<uint8_t> bestSoft(softCount, 0);
    std::vector<uint8_t> soft(softCount, 1);
    std::vector<uint64_t> solveCandidates;
    std::vector<Value> stack;
    stack.reserve(maxDepth);
    auto compareSoft = [&](const std::vector<uint8_t> &lhs,
                           const std::vector<uint8_t> &rhs) {
      for (unsigned priority = softCount; priority != 0; --priority) {
        if (lhs[priority - 1] == rhs[priority - 1])
          continue;
        return lhs[priority - 1] > rhs[priority - 1] ? 1 : -1;
      }
      return 0;
    };
    for (uint64_t attempt = 0; attempt != attempts; ++attempt) {
      bool hard = false;
      if (!evaluate(instructions, stack, candidate, constraintMask, captures,
                    hard, soft))
        return OBELISK_RT_INVALID_ARGUMENT;
      bool satisfiesAllSoft = std::all_of(
          soft.begin(), soft.end(), [](uint8_t value) { return value != 0; });
      if (!activeSolveBefore && hard && (!encodedSoft || satisfiesAllSoft)) {
        *outAssignment = candidate;
        *outSuccess = 1;
        return OBELISK_RT_OK;
      }
      if (hard && activeSolveBefore) {
        int comparison =
            solveCandidates.empty() ? 1 : compareSoft(soft, bestSoft);
        if (!encodedSoft || comparison >= 0) {
          if (encodedSoft && comparison > 0) {
            solveCandidates.clear();
            bestSoft = soft;
          }
          solveCandidates.push_back(candidate);
        }
      } else if (hard) {
        bool better = !hasHardFallback || compareSoft(soft, bestSoft) > 0;
        if (better) {
          hardFallback = candidate;
          bestSoft = soft;
          hasHardFallback = true;
        }
      }
      candidateIndex = (candidateIndex + 1) & widthMask(mutableWidth);
      candidate = fixed | expandBits(candidateIndex, mutableMask);
    }
    if (activeSolveBefore) {
      // Conditional solve-order probabilities require the complete enabled
      // finite domain. Returning exhaustion for a larger bounded search is
      // preferable to silently changing the source-level distribution.
      if (!complete || solveCandidates.empty())
        return OBELISK_RT_OK;

      auto chooseIndex = [&](uint64_t bound,
                             uint64_t &index) -> obelisk_rt_status {
        if (bound == 1) {
          index = 0;
          return OBELISK_RT_OK;
        }
        return obelisk_rt_v1_random_state_bounded(randomState, bound, &index);
      };
      for (uint64_t layerMask : solveLayers) {
        std::vector<uint64_t> values;
        values.reserve(solveCandidates.size());
        for (uint64_t assignment : solveCandidates)
          values.push_back(assignment & layerMask);
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        uint64_t selectedIndex = 0;
        obelisk_rt_status status = chooseIndex(values.size(), selectedIndex);
        if (status != OBELISK_RT_OK)
          return status;
        uint64_t selected = values[selectedIndex];
        solveCandidates.erase(
            std::remove_if(solveCandidates.begin(), solveCandidates.end(),
                           [&](uint64_t assignment) {
                             return (assignment & layerMask) != selected;
                           }),
            solveCandidates.end());
      }

      std::sort(solveCandidates.begin(), solveCandidates.end());
      uint64_t selectedIndex = 0;
      obelisk_rt_status status =
          chooseIndex(solveCandidates.size(), selectedIndex);
      if (status != OBELISK_RT_OK)
        return status;
      *outAssignment = solveCandidates[selectedIndex];
      *outSuccess = 1;
      return OBELISK_RT_OK;
    }
    // Soft constraints are dropped only after a complete finite-domain
    // traversal proves the lexicographically best enabled priority set.
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

extern "C" obelisk_rt_status obelisk_rt_v1_random_solve_modes(
    obelisk_rt_context *context, const uint8_t *program, uint64_t programSize,
    uint64_t start, uint64_t mutableMask, uint64_t constraintMask,
    uint64_t maxAttempts, const uint64_t *captures, uint64_t captureCount,
    uint64_t *outAssignment, uint32_t *outSuccess) {
  return randomSolveModesImpl(context, program, programSize, start, mutableMask,
                              constraintMask, maxAttempts, captures,
                              captureCount, outAssignment, outSuccess, nullptr);
}

extern "C" obelisk_rt_status obelisk_rt_v1_random_solve_modes_state(
    obelisk_rt_context *context, const uint8_t *program, uint64_t programSize,
    uint64_t start, uint64_t mutableMask, uint64_t constraintMask,
    uint64_t maxAttempts, uint64_t rngState, uint64_t rngIncrement,
    const uint64_t *captures, uint64_t captureCount, uint64_t *outAssignment,
    uint32_t *outSuccess, uint64_t *outRngState) {
  if (!outRngState)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outRngState = rngState;
  if ((rngIncrement & 1) == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_random_state_v1 randomState{rngState, rngIncrement};
  obelisk_rt_status status =
      randomSolveModesImpl(context, program, programSize, start, mutableMask,
                           constraintMask, maxAttempts, captures, captureCount,
                           outAssignment, outSuccess, &randomState);
  if (status == OBELISK_RT_OK)
    *outRngState = randomState.state;
  return status;
}

extern "C" obelisk_rt_status obelisk_rt_v1_random_solve_masked(
    obelisk_rt_context *context, const uint8_t *program, uint64_t programSize,
    uint64_t start, uint64_t mutableMask, uint64_t maxAttempts,
    const uint64_t *captures, uint64_t captureCount, uint64_t *outAssignment,
    uint32_t *outSuccess) {
  return obelisk_rt_v1_random_solve_modes(
      context, program, programSize, start, mutableMask, 0, maxAttempts,
      captures, captureCount, outAssignment, outSuccess);
}

extern "C" obelisk_rt_status obelisk_rt_v1_random_solve(
    obelisk_rt_context *context, const uint8_t *program, uint64_t programSize,
    uint64_t start, uint64_t maxAttempts, const uint64_t *captures,
    uint64_t captureCount, uint64_t *outAssignment, uint32_t *outSuccess) {
  return obelisk_rt_v1_random_solve_masked(
      context, program, programSize, start, UINT64_MAX, maxAttempts, captures,
      captureCount, outAssignment, outSuccess);
}
