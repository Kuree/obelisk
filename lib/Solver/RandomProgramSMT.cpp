//===- RandomProgramSMT.cpp - Decode random constraints to SMT -*- C++ -*-===//

#include "obelisk/Solver/ConstraintSolver.h"

#ifdef OBELISK_ENABLE_Z3

#include "RandomProgramSMT.h"

#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Verifier.h"

#include "llvm/ADT/APInt.h"

#include <algorithm>
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
  mlir::Value bits;
  unsigned width;
  std::optional<SMTVariable> directVariable;
  std::optional<SMTVariableEquality> directEquality;
};

mlir::Value constant(mlir::OpBuilder &builder, mlir::Location location,
                     uint64_t value, unsigned width) {
  return mlir::smt::BVConstantOp::create(builder, location, value, width);
}

mlir::Value resize(mlir::OpBuilder &builder, mlir::Location location,
                   StackValue value, unsigned width, bool signExtend) {
  if (value.width == width)
    return value.bits;
  if (value.width > width)
    return mlir::smt::ExtractOp::create(
        builder, location,
        mlir::smt::BitVectorType::get(builder.getContext(), width), 0,
        value.bits);
  unsigned extension = width - value.width;
  mlir::Value prefix;
  if (signExtend) {
    mlir::Value sign = mlir::smt::ExtractOp::create(
        builder, location,
        mlir::smt::BitVectorType::get(builder.getContext(), 1), value.width - 1,
        value.bits);
    prefix = extension == 1 ? sign
                            : mlir::smt::RepeatOp::create(builder, location,
                                                          extension, sign)
                                  .getResult();
  } else {
    prefix = constant(builder, location, 0, extension);
  }
  return mlir::smt::ConcatOp::create(builder, location, prefix, value.bits);
}

mlir::Value truth(mlir::OpBuilder &builder, mlir::Location location,
                  const StackValue &value) {
  return mlir::smt::DistinctOp::create(
      builder, location, value.bits,
      constant(builder, location, 0, value.width));
}

StackValue
booleanValue(mlir::OpBuilder &builder, mlir::Location location,
             mlir::Value predicate,
             std::optional<SMTVariableEquality> directEquality = std::nullopt) {
  return {mlir::smt::IteOp::create(builder, location, predicate,
                                   constant(builder, location, 1, 1),
                                   constant(builder, location, 0, 1)),
          1, std::nullopt, std::move(directEquality)};
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

std::optional<RandomProgramSMT> buildRandomProgramSMT(const uint8_t *program,
                                                      size_t programSize) {
  if (!program || programSize < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE ||
      read32(program) != OBELISK_RT_RANDOM_PROGRAM_MAGIC ||
      read16(program + 4) != OBELISK_RT_RANDOM_PROGRAM_VERSION ||
      read16(program + 6) != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE)
    return std::nullopt;
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
    return std::nullopt;

  RandomProgramSMT result;
  result.context = std::make_unique<mlir::MLIRContext>();
  result.context->loadDialect<mlir::smt::SMTDialect>();
  mlir::OpBuilder builder(result.context.get());
  mlir::Location location = builder.getUnknownLoc();
  result.module = mlir::ModuleOp::create(location);
  builder.setInsertionPointToStart(result.module->getBody());
  result.solver = mlir::smt::SolverOp::create(
      builder, location, mlir::TypeRange{}, mlir::ValueRange{},
      llvm::ArrayRef<mlir::NamedAttribute>{});
  mlir::Block &body = result.solver.getBodyRegion().emplaceBlock();
  builder.setInsertionPointToStart(&body);

  auto bitVectorType = [&](unsigned width) {
    return mlir::smt::BitVectorType::get(result.context.get(), width);
  };
  mlir::Value assignment = mlir::smt::DeclareFunOp::create(
      builder, location,
      bitVectorType(aggregateWidth == 0 ? 1 : aggregateWidth),
      builder.getStringAttr("assignment"));
  std::vector<mlir::Value> captures;
  captures.reserve(captureCount);
  for (uint32_t index = 0; index != captureCount; ++index)
    captures.push_back(mlir::smt::DeclareFunOp::create(
        builder, location, bitVectorType(64),
        builder.getStringAttr("capture_" + std::to_string(index))));

  std::vector<StackValue> stack;
  stack.reserve(instructionCount);
  mlir::Value hard = mlir::smt::BoolConstantOp::create(builder, location, true);
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
      return std::nullopt;
    bool signedOperation = (flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
    if (opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
      if (aggregateWidth == 0 || operand >= aggregateWidth ||
          width > aggregateWidth - operand)
        return std::nullopt;
      mlir::Value bits = mlir::smt::ExtractOp::create(
          builder, location, bitVectorType(width), operand, assignment);
      stack.push_back(
          {bits, width, SMTVariable{operand, width, bits}, std::nullopt});
      auto found = std::find_if(
          result.variables.begin(), result.variables.end(),
          [&](const SMTVariable &variable) {
            return variable.offset == operand && variable.width == width;
          });
      if (found == result.variables.end())
        result.variables.push_back({operand, width, bits});
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
      if (operand >= captures.size())
        return std::nullopt;
      stack.push_back(
          {mlir::smt::ExtractOp::create(builder, location, bitVectorType(width),
                                        0, captures[operand]),
           width});
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
      stack.push_back({constant(builder, location, immediate, width), width});
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
        opcode == OBELISK_RT_RANDOM_END_SOFT_V1) {
      if (width != 1 || stack.size() != 1)
        return std::nullopt;
      if (opcode == OBELISK_RT_RANDOM_END_HARD_V1) {
        hard = mlir::smt::AndOp::create(builder, location, hard,
                                        truth(builder, location, stack.back()));
        if (stack.back().directEquality)
          result.directEqualities.push_back(*stack.back().directEquality);
      }
      sawHard |= opcode == OBELISK_RT_RANDOM_END_HARD_V1;
      sawSoft |= opcode == OBELISK_RT_RANDOM_END_SOFT_V1;
      stack.clear();
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_SELECT_V1) {
      if (stack.size() < 3)
        return std::nullopt;
      StackValue falseValue = stack.back();
      stack.pop_back();
      StackValue trueValue = stack.back();
      stack.pop_back();
      StackValue condition = stack.back();
      stack.pop_back();
      stack.push_back(
          {mlir::smt::IteOp::create(
               builder, location, truth(builder, location, condition),
               resize(builder, location, trueValue, width, signedOperation),
               resize(builder, location, falseValue, width, signedOperation)),
           width});
      continue;
    }
    if (isUnary(opcode)) {
      if (stack.empty())
        return std::nullopt;
      StackValue input = stack.back();
      stack.pop_back();
      switch (opcode) {
      case OBELISK_RT_RANDOM_CAST_V1:
      case OBELISK_RT_RANDOM_POS_V1:
        stack.push_back(
            {resize(builder, location, input, width, signedOperation), width});
        break;
      case OBELISK_RT_RANDOM_NEG_V1:
        stack.push_back(
            {mlir::smt::BVNegOp::create(
                 builder, location,
                 resize(builder, location, input, width, signedOperation)),
             width});
        break;
      case OBELISK_RT_RANDOM_BIT_NOT_V1:
        stack.push_back(
            {mlir::smt::BVNotOp::create(
                 builder, location,
                 resize(builder, location, input, width, signedOperation)),
             width});
        break;
      case OBELISK_RT_RANDOM_REDUCE_AND_V1:
        stack.push_back(booleanValue(
            builder, location,
            mlir::smt::EqOp::create(
                builder, location, input.bits,
                mlir::smt::BVConstantOp::create(
                    builder, location, llvm::APInt::getAllOnes(input.width)))));
        break;
      case OBELISK_RT_RANDOM_REDUCE_OR_V1:
        stack.push_back(
            booleanValue(builder, location, truth(builder, location, input)));
        break;
      case OBELISK_RT_RANDOM_REDUCE_XOR_V1:
      case OBELISK_RT_RANDOM_REDUCE_XNOR_V1: {
        mlir::Value parity =
            mlir::smt::BoolConstantOp::create(builder, location, false);
        for (unsigned bit = 0; bit != input.width; ++bit) {
          mlir::Value current = mlir::smt::EqOp::create(
              builder, location,
              mlir::smt::ExtractOp::create(builder, location, bitVectorType(1),
                                           bit, input.bits),
              constant(builder, location, 1, 1));
          parity = mlir::smt::XOrOp::create(builder, location, parity, current);
        }
        if (opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1)
          parity = mlir::smt::NotOp::create(builder, location, parity);
        stack.push_back(booleanValue(builder, location, parity));
        break;
      }
      case OBELISK_RT_RANDOM_REDUCE_NAND_V1:
        stack.push_back(booleanValue(
            builder, location,
            mlir::smt::DistinctOp::create(
                builder, location, input.bits,
                mlir::smt::BVConstantOp::create(
                    builder, location, llvm::APInt::getAllOnes(input.width)))));
        break;
      case OBELISK_RT_RANDOM_REDUCE_NOR_V1:
      case OBELISK_RT_RANDOM_LOGICAL_NOT_V1:
        stack.push_back(booleanValue(
            builder, location,
            mlir::smt::NotOp::create(builder, location,
                                     truth(builder, location, input))));
        break;
      default:
        return std::nullopt;
      }
      continue;
    }
    if (!isBinary(opcode) || stack.size() < 2)
      return std::nullopt;
    StackValue rhs = stack.back();
    stack.pop_back();
    StackValue lhs = stack.back();
    stack.pop_back();
    if (opcode >= OBELISK_RT_RANDOM_EQ_V1 &&
        opcode <= OBELISK_RT_RANDOM_LT_V1) {
      unsigned compareWidth = std::max(lhs.width, rhs.width);
      mlir::Value left =
          resize(builder, location, lhs, compareWidth, signedOperation);
      mlir::Value right =
          resize(builder, location, rhs, compareWidth, signedOperation);
      mlir::Value predicate;
      switch (opcode) {
      case OBELISK_RT_RANDOM_EQ_V1:
        predicate = mlir::smt::EqOp::create(builder, location, left, right);
        break;
      case OBELISK_RT_RANDOM_NE_V1:
        predicate =
            mlir::smt::DistinctOp::create(builder, location, left, right);
        break;
      default: {
        mlir::smt::BVCmpPredicate comparison;
        if (signedOperation) {
          comparison = opcode == OBELISK_RT_RANDOM_GE_V1
                           ? mlir::smt::BVCmpPredicate::sge
                       : opcode == OBELISK_RT_RANDOM_GT_V1
                           ? mlir::smt::BVCmpPredicate::sgt
                       : opcode == OBELISK_RT_RANDOM_LE_V1
                           ? mlir::smt::BVCmpPredicate::sle
                           : mlir::smt::BVCmpPredicate::slt;
        } else {
          comparison = opcode == OBELISK_RT_RANDOM_GE_V1
                           ? mlir::smt::BVCmpPredicate::uge
                       : opcode == OBELISK_RT_RANDOM_GT_V1
                           ? mlir::smt::BVCmpPredicate::ugt
                       : opcode == OBELISK_RT_RANDOM_LE_V1
                           ? mlir::smt::BVCmpPredicate::ule
                           : mlir::smt::BVCmpPredicate::ult;
        }
        predicate = mlir::smt::BVCmpOp::create(builder, location, comparison,
                                               left, right);
        break;
      }
      }
      std::optional<SMTVariableEquality> directEquality;
      if (opcode == OBELISK_RT_RANDOM_EQ_V1 && lhs.width == rhs.width &&
          lhs.directVariable && rhs.directVariable &&
          lhs.directVariable->offset != rhs.directVariable->offset)
        directEquality =
            SMTVariableEquality{*lhs.directVariable, *rhs.directVariable};
      stack.push_back(booleanValue(builder, location, predicate,
                                   std::move(directEquality)));
      continue;
    }
    if (opcode >= OBELISK_RT_RANDOM_LOGICAL_AND_V1) {
      mlir::Value left = truth(builder, location, lhs);
      mlir::Value right = truth(builder, location, rhs);
      mlir::Value predicate;
      switch (opcode) {
      case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
        predicate = mlir::smt::AndOp::create(builder, location, left, right);
        break;
      case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
        predicate = mlir::smt::OrOp::create(builder, location, left, right);
        break;
      case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
        predicate =
            mlir::smt::ImpliesOp::create(builder, location, left, right);
        break;
      case OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1:
        predicate = mlir::smt::EqOp::create(builder, location, left, right);
        break;
      default:
        return std::nullopt;
      }
      stack.push_back(booleanValue(builder, location, predicate));
      continue;
    }
    mlir::Value left = resize(builder, location, lhs, width, signedOperation);
    mlir::Value right = resize(builder, location, rhs, width, signedOperation);
    mlir::Value bits;
    switch (opcode) {
    case OBELISK_RT_RANDOM_ADD_V1:
      bits = mlir::smt::BVAddOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_SUB_V1:
      bits = mlir::smt::BVAddOp::create(
          builder, location, left,
          mlir::smt::BVNegOp::create(builder, location, right));
      break;
    case OBELISK_RT_RANDOM_MUL_V1:
      bits = mlir::smt::BVMulOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_BIT_AND_V1:
      bits = mlir::smt::BVAndOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_BIT_OR_V1:
      bits = mlir::smt::BVOrOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_BIT_XOR_V1:
      bits = mlir::smt::BVXOrOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_BIT_XNOR_V1:
      bits = mlir::smt::BVNotOp::create(
          builder, location,
          mlir::smt::BVXOrOp::create(builder, location, left, right));
      break;
    default:
      return std::nullopt;
    }
    stack.push_back({bits, width});
  }

  bool encodedSoft = (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0;
  if (!stack.empty() || !sawHard || sawSoft != encodedSoft)
    return std::nullopt;
  result.hard = hard;
  mlir::smt::AssertOp::create(builder, location, hard);
  mlir::smt::YieldOp::create(builder, location);
  if (mlir::failed(mlir::verify(*result.module)))
    return std::nullopt;
  return result;
}

} // namespace obelisk::solver

#endif
