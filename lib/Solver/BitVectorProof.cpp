#include "obelisk/Solver/BitVectorProof.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"
#include <functional>
#include <string>
#include <vector>
#ifdef OBELISK_ENABLE_Z3
#include "Z3Support.h"
#include <z3++.h>
#endif

namespace obelisk::solver {
bool proveBitVectorBounds(llvm::Value *value, const llvm::APInt &lower,
                          const llvm::APInt &upper,
                          llvm::ArrayRef<BitVectorRange> ranges,
                          unsigned resourceLimit) {
#ifndef OBELISK_ENABLE_Z3
  return false;
#else
  if (!resourceLimit || !value || !value->getType()->isIntegerTy())
    return false;
  unsigned targetWidth = value->getType()->getIntegerBitWidth();
  if (lower.getBitWidth() != targetWidth ||
      upper.getBitWidth() != targetWidth || lower.sgt(upper))
    return false;
  for (const auto &range : ranges)
    if (!range.value || !range.value->getType()->isIntegerTy() ||
        range.umin.getBitWidth() !=
            range.value->getType()->getIntegerBitWidth() ||
        range.umax.getBitWidth() != range.umin.getBitWidth() ||
        range.smin.getBitWidth() != range.umin.getBitWidth() ||
        range.smax.getBitWidth() != range.umin.getBitWidth() ||
        range.umin.ugt(range.umax) || range.smin.sgt(range.smax))
      return false;
#ifdef OBELISK_Z3_SINGLE_THREADED
  std::lock_guard<std::mutex> lock(detail::getZ3Mutex());
#endif
  try {
#ifdef OBELISK_Z3_SINGLE_THREADED
    static z3::context context;
#else
    static thread_local z3::context context;
#endif
    auto numeral = [&](const llvm::APInt &bits) {
      llvm::SmallString<40> digits;
      bits.toString(digits, 10, false);
      return context.bv_val(digits.c_str(), bits.getBitWidth());
    };
    // A fresh solver gives each query an independent deterministic work budget.
    // Context reuse avoids allocating a context for each small selector slice.
    llvm::DenseMap<llvm::Value *, unsigned> indices;
    std::vector<z3::expr> expressions;
    unsigned nodes = 0;
    bool exhausted = false;
    std::function<z3::expr(llvm::Value *, unsigned)> translate =
        [&](llvm::Value *input, unsigned depth) -> z3::expr {
      if (auto found = indices.find(input); found != indices.end())
        return expressions[found->second];
      if (++nodes > 128 || depth > 32) {
        exhausted = true;
        return context.bv_val(0, input->getType()->getIntegerBitWidth());
      }
      unsigned width = input->getType()->getIntegerBitWidth();
      z3::expr result = context.bv_const(
          ("selector_" + std::to_string(nodes)).c_str(), width);
      if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(input)) {
        result = numeral(constant->getValue());
      } else if (auto *binary = llvm::dyn_cast<llvm::BinaryOperator>(input)) {
        auto lhs = translate(binary->getOperand(0), depth + 1);
        auto rhs = translate(binary->getOperand(1), depth + 1);
        switch (binary->getOpcode()) {
        case llvm::Instruction::Add:
          result = lhs + rhs;
          break;
        case llvm::Instruction::Sub:
          result = lhs - rhs;
          break;
        case llvm::Instruction::Mul:
          result = lhs * rhs;
          break;
        case llvm::Instruction::And:
          result = lhs & rhs;
          break;
        case llvm::Instruction::Or:
          result = lhs | rhs;
          break;
        case llvm::Instruction::Xor:
          result = lhs ^ rhs;
          break;
        default:
          break;
        }
      } else if (auto *cast = llvm::dyn_cast<llvm::CastInst>(input);
                 cast && (cast->getOpcode() == llvm::Instruction::Trunc ||
                          cast->getOpcode() == llvm::Instruction::ZExt ||
                          cast->getOpcode() == llvm::Instruction::SExt)) {
        auto source = translate(cast->getOperand(0), depth + 1);
        unsigned sourceWidth =
            cast->getOperand(0)->getType()->getIntegerBitWidth();
        switch (cast->getOpcode()) {
        case llvm::Instruction::Trunc:
          result = source.extract(width - 1, 0);
          break;
        case llvm::Instruction::ZExt:
          result = z3::zext(source, width - sourceWidth);
          break;
        case llvm::Instruction::SExt:
          result = z3::sext(source, width - sourceWidth);
          break;
        default:
          break;
        }
      } else if (auto *cmp = llvm::dyn_cast<llvm::ICmpInst>(input);
                 cmp && cmp->getOperand(0)->getType()->isIntegerTy()) {
        auto lhs = translate(cmp->getOperand(0), depth + 1);
        auto rhs = translate(cmp->getOperand(1), depth + 1);
        z3::expr condition = context.bool_val(false);
        switch (cmp->getPredicate()) {
        case llvm::CmpInst::ICMP_EQ:
          condition = lhs == rhs;
          break;
        case llvm::CmpInst::ICMP_NE:
          condition = lhs != rhs;
          break;
        case llvm::CmpInst::ICMP_ULT:
          condition = z3::ult(lhs, rhs);
          break;
        case llvm::CmpInst::ICMP_ULE:
          condition = z3::ule(lhs, rhs);
          break;
        case llvm::CmpInst::ICMP_UGT:
          condition = z3::ugt(lhs, rhs);
          break;
        case llvm::CmpInst::ICMP_UGE:
          condition = z3::uge(lhs, rhs);
          break;
        case llvm::CmpInst::ICMP_SLT:
          condition = z3::slt(lhs, rhs);
          break;
        case llvm::CmpInst::ICMP_SLE:
          condition = z3::sle(lhs, rhs);
          break;
        case llvm::CmpInst::ICMP_SGT:
          condition = z3::sgt(lhs, rhs);
          break;
        case llvm::CmpInst::ICMP_SGE:
          condition = z3::sge(lhs, rhs);
          break;
        default:
          llvm_unreachable("invalid integer comparison");
        }
        result = z3::ite(condition, context.bv_val(1, 1), context.bv_val(0, 1));
      } else if (auto *select = llvm::dyn_cast<llvm::SelectInst>(input)) {
        auto condition = translate(select->getCondition(), depth + 1);
        result = z3::ite(condition == context.bv_val(1, 1),
                         translate(select->getTrueValue(), depth + 1),
                         translate(select->getFalseValue(), depth + 1));
      }
      indices.try_emplace(input, expressions.size());
      expressions.push_back(result);
      return result;
    };
    z3::expr target = translate(value, 0);
    if (exhausted)
      return false;
    z3::solver solver(context);
    z3::params params(context);
    params.set("rlimit", resourceLimit);
    solver.set(params);
    for (const auto &range : ranges) {
      auto found = indices.find(range.value);
      if (found == indices.end())
        continue;
      auto bits = expressions[found->second];
      solver.add(z3::uge(bits, numeral(range.umin)) &&
                 z3::ule(bits, numeral(range.umax)) &&
                 z3::sge(bits, numeral(range.smin)) &&
                 z3::sle(bits, numeral(range.smax)));
    }
    solver.add(z3::slt(target, numeral(lower)) ||
               z3::sgt(target, numeral(upper)));
    return solver.check() == z3::unsat;
  } catch (const z3::exception &) {
    return false;
  }
#endif
}
} // namespace obelisk::solver
