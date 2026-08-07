#include "obelisk/Solver/ConstraintSolver.h"

#ifdef OBELISK_ENABLE_Z3

#include "obelisk/Runtime/Runtime.h"

#include "z3++.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace obelisk::solver {
namespace {

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

struct StackValue {
  StackValue(z3::expr bits, unsigned width)
      : bits(std::move(bits)), width(width) {}
  z3::expr bits;
  unsigned width;
};

z3::expr resize(StackValue value, unsigned width, bool signExtend) {
  if (value.width == width)
    return value.bits;
  if (value.width > width)
    return value.bits.extract(width - 1, 0);
  return signExtend ? z3::sext(value.bits, width - value.width)
                    : z3::zext(value.bits, width - value.width);
}

z3::expr truth(const StackValue &value) {
  return value.bits != value.bits.ctx().bv_val(0, value.width);
}

StackValue booleanValue(z3::expr predicate) {
  z3::context &context = predicate.ctx();
  return {z3::ite(predicate, context.bv_val(1, 1), context.bv_val(0, 1)), 1};
}

bool isUnary(uint8_t opcode) {
  return opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
         opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1;
}

bool isBinary(uint8_t opcode) {
  return opcode >= OBELISK_RT_RANDOM_ADD_V1 &&
         opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1;
}

} // namespace

RandomProgramAnalysis analyzeRandomProgram(const uint8_t *program,
                                           size_t programSize,
                                           uint64_t resourceLimit) {
  RandomProgramAnalysis analysis{Satisfiability::Unknown, "z3-4.13.4"};
  if (!program || programSize < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE ||
      read32(program) != OBELISK_RT_RANDOM_PROGRAM_MAGIC ||
      read16(program + 4) != OBELISK_RT_RANDOM_PROGRAM_VERSION ||
      read16(program + 6) != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE)
    return analysis;
  uint32_t aggregateWidth = read32(program + 8);
  uint32_t instructionCount = read32(program + 12);
  uint32_t captureCount = read32(program + 16);
  uint32_t programFlags = read32(program + 20);
  if (aggregateWidth > 64 ||
      (programFlags & ~OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0 ||
      instructionCount > (std::numeric_limits<size_t>::max() -
                          OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE) /
                             OBELISK_RT_RANDOM_INSTRUCTION_SIZE ||
      programSize != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE +
                         static_cast<size_t>(instructionCount) *
                             OBELISK_RT_RANDOM_INSTRUCTION_SIZE)
    return analysis;

  // Z3 uses exceptions internally even though the rest of Obelisk follows
  // LLVM's no-exceptions convention. They are contained within this
  // translation unit and converted into conservative Unknown results.
  try {
    z3::context context;
    z3::expr assignment = context.bv_const(
        "assignment", aggregateWidth == 0 ? 1 : aggregateWidth);
    std::vector<z3::expr> captures;
    captures.reserve(captureCount);
    for (uint32_t index = 0; index != captureCount; ++index)
      captures.push_back(
          context.bv_const(("capture_" + std::to_string(index)).c_str(), 64));
    std::vector<StackValue> stack;
    stack.reserve(instructionCount);
    z3::expr hard = context.bool_val(true);
    bool sawHard = false;
    bool sawSoft = false;
    const uint8_t *cursor = program + OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE;
    for (uint32_t index = 0; index != instructionCount; ++index) {
      uint8_t opcode = cursor[0];
      unsigned width = cursor[1];
      uint8_t flags = cursor[2];
      uint8_t reserved = cursor[3];
      uint32_t operand = read32(cursor + 4);
      uint64_t immediate = read64(cursor + 8);
      cursor += OBELISK_RT_RANDOM_INSTRUCTION_SIZE;
      if (width == 0 || width > 64 || reserved != 0 ||
          (flags & ~OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0)
        return analysis;
      bool signedOperation =
          (flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
      if (opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
        if (aggregateWidth == 0 || operand >= aggregateWidth ||
            width > aggregateWidth - operand)
          return analysis;
        stack.emplace_back(assignment.extract(operand + width - 1, operand),
                           width);
        continue;
      }
      if (opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
        if (operand >= captures.size())
          return analysis;
        stack.emplace_back(captures[operand].extract(width - 1, 0), width);
        continue;
      }
      if (opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
        stack.emplace_back(context.bv_val(immediate, width), width);
        continue;
      }
      if (opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
          opcode == OBELISK_RT_RANDOM_END_SOFT_V1) {
        if (width != 1 || stack.size() != 1)
          return analysis;
        if (opcode == OBELISK_RT_RANDOM_END_HARD_V1)
          hard = hard && truth(stack.back());
        sawHard |= opcode == OBELISK_RT_RANDOM_END_HARD_V1;
        sawSoft |= opcode == OBELISK_RT_RANDOM_END_SOFT_V1;
        stack.clear();
        continue;
      }
      if (opcode == OBELISK_RT_RANDOM_SELECT_V1) {
        if (stack.size() < 3)
          return analysis;
        StackValue falseValue = std::move(stack.back());
        stack.pop_back();
        StackValue trueValue = std::move(stack.back());
        stack.pop_back();
        StackValue condition = std::move(stack.back());
        stack.pop_back();
        stack.emplace_back(
            z3::ite(truth(condition),
                    resize(std::move(trueValue), width, signedOperation),
                    resize(std::move(falseValue), width, signedOperation)),
            width);
        continue;
      }
      if (isUnary(opcode)) {
        if (stack.empty())
          return analysis;
        StackValue input = std::move(stack.back());
        stack.pop_back();
        switch (opcode) {
        case OBELISK_RT_RANDOM_CAST_V1:
        case OBELISK_RT_RANDOM_POS_V1:
          stack.emplace_back(resize(std::move(input), width, signedOperation),
                             width);
          break;
        case OBELISK_RT_RANDOM_NEG_V1:
          stack.emplace_back(-resize(std::move(input), width, signedOperation),
                             width);
          break;
        case OBELISK_RT_RANDOM_BIT_NOT_V1:
          stack.emplace_back(~resize(std::move(input), width, signedOperation),
                             width);
          break;
        case OBELISK_RT_RANDOM_REDUCE_AND_V1:
          stack.push_back(booleanValue(
              input.bits == context.bv_val(UINT64_MAX, input.width)));
          break;
        case OBELISK_RT_RANDOM_REDUCE_OR_V1:
          stack.push_back(booleanValue(truth(input)));
          break;
        case OBELISK_RT_RANDOM_REDUCE_XOR_V1:
        case OBELISK_RT_RANDOM_REDUCE_XNOR_V1: {
          z3::expr parity = context.bool_val(false);
          for (unsigned bit = 0; bit != input.width; ++bit)
            parity = parity !=
                     (input.bits.extract(bit, bit) == context.bv_val(1, 1));
          if (opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1)
            parity = !parity;
          stack.push_back(booleanValue(parity));
          break;
        }
        case OBELISK_RT_RANDOM_REDUCE_NAND_V1:
          stack.push_back(booleanValue(
              input.bits != context.bv_val(UINT64_MAX, input.width)));
          break;
        case OBELISK_RT_RANDOM_REDUCE_NOR_V1:
        case OBELISK_RT_RANDOM_LOGICAL_NOT_V1:
          stack.push_back(booleanValue(!truth(input)));
          break;
        default:
          return analysis;
        }
        continue;
      }
      if (!isBinary(opcode) || stack.size() < 2)
        return analysis;
      StackValue rhs = std::move(stack.back());
      stack.pop_back();
      StackValue lhs = std::move(stack.back());
      stack.pop_back();
      if (opcode >= OBELISK_RT_RANDOM_EQ_V1 &&
          opcode <= OBELISK_RT_RANDOM_LT_V1) {
        unsigned compareWidth = std::max(lhs.width, rhs.width);
        z3::expr left = resize(std::move(lhs), compareWidth, signedOperation);
        z3::expr right = resize(std::move(rhs), compareWidth, signedOperation);
        z3::expr predicate = context.bool_val(false);
        switch (opcode) {
        case OBELISK_RT_RANDOM_EQ_V1:
          predicate = left == right;
          break;
        case OBELISK_RT_RANDOM_NE_V1:
          predicate = left != right;
          break;
        case OBELISK_RT_RANDOM_GE_V1:
          predicate =
              signedOperation ? z3::sge(left, right) : z3::uge(left, right);
          break;
        case OBELISK_RT_RANDOM_GT_V1:
          predicate =
              signedOperation ? z3::sgt(left, right) : z3::ugt(left, right);
          break;
        case OBELISK_RT_RANDOM_LE_V1:
          predicate =
              signedOperation ? z3::sle(left, right) : z3::ule(left, right);
          break;
        case OBELISK_RT_RANDOM_LT_V1:
          predicate =
              signedOperation ? z3::slt(left, right) : z3::ult(left, right);
          break;
        default:
          break;
        }
        stack.push_back(booleanValue(predicate));
        continue;
      }
      if (opcode >= OBELISK_RT_RANDOM_LOGICAL_AND_V1) {
        z3::expr left = truth(lhs), right = truth(rhs);
        z3::expr predicate = context.bool_val(false);
        switch (opcode) {
        case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
          predicate = left && right;
          break;
        case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
          predicate = left || right;
          break;
        case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
          predicate = z3::implies(left, right);
          break;
        case OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1:
          predicate = left == right;
          break;
        default:
          return analysis;
        }
        stack.push_back(booleanValue(predicate));
        continue;
      }
      z3::expr left = resize(std::move(lhs), width, signedOperation);
      z3::expr right = resize(std::move(rhs), width, signedOperation);
      switch (opcode) {
      case OBELISK_RT_RANDOM_ADD_V1:
        stack.emplace_back(left + right, width);
        break;
      case OBELISK_RT_RANDOM_SUB_V1:
        stack.emplace_back(left - right, width);
        break;
      case OBELISK_RT_RANDOM_MUL_V1:
        stack.emplace_back(left * right, width);
        break;
      case OBELISK_RT_RANDOM_BIT_AND_V1:
        stack.emplace_back(left & right, width);
        break;
      case OBELISK_RT_RANDOM_BIT_OR_V1:
        stack.emplace_back(left | right, width);
        break;
      case OBELISK_RT_RANDOM_BIT_XOR_V1:
        stack.emplace_back(left ^ right, width);
        break;
      case OBELISK_RT_RANDOM_BIT_XNOR_V1:
        stack.emplace_back(~(left ^ right), width);
        break;
      default:
        return analysis;
      }
    }
    bool encodedSoft = (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0;
    if (!stack.empty() || !sawHard || sawSoft != encodedSoft)
      return analysis;
    z3::solver solver(context);
    z3::params parameters(context);
    parameters.set("random_seed", 0u);
    parameters.set("rlimit", static_cast<unsigned>(
                                 std::min<uint64_t>(resourceLimit, UINT_MAX)));
    solver.set(parameters);
    solver.add(hard);
    switch (solver.check()) {
    case z3::sat:
      analysis.satisfiability = Satisfiability::Satisfiable;
      break;
    case z3::unsat:
      analysis.satisfiability = Satisfiability::Unsatisfiable;
      break;
    case z3::unknown:
      break;
    }
  } catch (...) {
  }
  return analysis;
}

} // namespace obelisk::solver

#endif
