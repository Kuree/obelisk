//===- SolverZ3.cpp - Execute MLIR SMT formulas with Z3 -------*- C++ -*-===//

#include "obelisk/Solver/ConstraintSolver.h"

#ifdef OBELISK_ENABLE_Z3

#include "RandomProgramSMT.h"

#include "mlir/Dialect/SMT/IR/SMTOps.h"

#include "llvm/ADT/DenseMap.h"

#include "z3++.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace obelisk::solver {
namespace {

/// The MLIR SMT dialect intentionally does not select or link a solver. This
/// shim is the single backend boundary that evaluates its bit-vector DAG with
/// the pinned in-process Z3 API.
class Z3SMTShim {
public:
  explicit Z3SMTShim(z3::context &context) : context(context) {}

  std::optional<z3::expr> translate(mlir::Value value) {
    auto cached = expressions.find(value);
    if (cached != expressions.end())
      return values[cached->second];
    mlir::Operation *operation = value.getDefiningOp();
    if (!operation)
      return std::nullopt;

    std::optional<z3::expr> result;
    if (auto op = mlir::dyn_cast<mlir::smt::DeclareFunOp>(operation)) {
      std::string name = op.getNamePrefix().value_or("value").str() + "_" +
                         std::to_string(declarationIndex++);
      if (mlir::isa<mlir::smt::BoolType>(value.getType()))
        result = context.bool_const(name.c_str());
      else if (auto type =
                   mlir::dyn_cast<mlir::smt::BitVectorType>(value.getType()))
        result = context.bv_const(name.c_str(), type.getWidth());
    } else if (auto op = mlir::dyn_cast<mlir::smt::BoolConstantOp>(operation)) {
      result = context.bool_val(op.getValue());
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVConstantOp>(operation)) {
      llvm::APInt bits = op.getValue().getValue();
      result = context.bv_val(bits.getZExtValue(), bits.getBitWidth());
    } else if (auto op = mlir::dyn_cast<mlir::smt::ExtractOp>(operation)) {
      if (auto input = translate(op.getInput())) {
        unsigned width =
            mlir::cast<mlir::smt::BitVectorType>(value.getType()).getWidth();
        result = input->extract(op.getLowBit() + width - 1, op.getLowBit());
      }
    } else if (auto op = mlir::dyn_cast<mlir::smt::RepeatOp>(operation)) {
      if (auto input = translate(op.getInput()))
        result = input->repeat(op.getCount());
    } else if (auto op = mlir::dyn_cast<mlir::smt::ConcatOp>(operation)) {
      result = translateBinary(
          op.getLhs(), op.getRhs(),
          [](z3::expr lhs, z3::expr rhs) { return z3::concat(lhs, rhs); });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVNegOp>(operation)) {
      if (auto input = translate(op.getInput()))
        result = -*input;
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVNotOp>(operation)) {
      if (auto input = translate(op.getInput()))
        result = ~*input;
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVAddOp>(operation)) {
      result =
          translateBinary(op.getLhs(), op.getRhs(),
                          [](z3::expr lhs, z3::expr rhs) { return lhs + rhs; });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVMulOp>(operation)) {
      result =
          translateBinary(op.getLhs(), op.getRhs(),
                          [](z3::expr lhs, z3::expr rhs) { return lhs * rhs; });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVAndOp>(operation)) {
      result =
          translateBinary(op.getLhs(), op.getRhs(),
                          [](z3::expr lhs, z3::expr rhs) { return lhs & rhs; });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVOrOp>(operation)) {
      result =
          translateBinary(op.getLhs(), op.getRhs(),
                          [](z3::expr lhs, z3::expr rhs) { return lhs | rhs; });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVXOrOp>(operation)) {
      result =
          translateBinary(op.getLhs(), op.getRhs(),
                          [](z3::expr lhs, z3::expr rhs) { return lhs ^ rhs; });
    } else if (auto op = mlir::dyn_cast<mlir::smt::EqOp>(operation)) {
      result = translateComparison(op.getInputs(), true);
    } else if (auto op = mlir::dyn_cast<mlir::smt::DistinctOp>(operation)) {
      result = translateComparison(op.getInputs(), false);
    } else if (auto op = mlir::dyn_cast<mlir::smt::IteOp>(operation)) {
      auto condition = translate(op.getCond());
      auto thenValue = translate(op.getThenValue());
      auto elseValue = translate(op.getElseValue());
      if (condition && thenValue && elseValue)
        result = z3::ite(*condition, *thenValue, *elseValue);
    } else if (auto op = mlir::dyn_cast<mlir::smt::NotOp>(operation)) {
      if (auto input = translate(op.getInput()))
        result = !*input;
    } else if (auto op = mlir::dyn_cast<mlir::smt::AndOp>(operation)) {
      result = translateBooleanRange(op.getInputs(), true);
    } else if (auto op = mlir::dyn_cast<mlir::smt::OrOp>(operation)) {
      result = translateBooleanRange(op.getInputs(), false);
    } else if (auto op = mlir::dyn_cast<mlir::smt::XOrOp>(operation)) {
      result = translateBooleanXor(op.getInputs());
    } else if (auto op = mlir::dyn_cast<mlir::smt::ImpliesOp>(operation)) {
      result = translateBinary(
          op.getLhs(), op.getRhs(),
          [](z3::expr lhs, z3::expr rhs) { return z3::implies(lhs, rhs); });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVCmpOp>(operation)) {
      auto lhs = translate(op.getLhs());
      auto rhs = translate(op.getRhs());
      if (lhs && rhs) {
        switch (op.getPred()) {
        case mlir::smt::BVCmpPredicate::slt:
          result = z3::slt(*lhs, *rhs);
          break;
        case mlir::smt::BVCmpPredicate::sle:
          result = z3::sle(*lhs, *rhs);
          break;
        case mlir::smt::BVCmpPredicate::sgt:
          result = z3::sgt(*lhs, *rhs);
          break;
        case mlir::smt::BVCmpPredicate::sge:
          result = z3::sge(*lhs, *rhs);
          break;
        case mlir::smt::BVCmpPredicate::ult:
          result = z3::ult(*lhs, *rhs);
          break;
        case mlir::smt::BVCmpPredicate::ule:
          result = z3::ule(*lhs, *rhs);
          break;
        case mlir::smt::BVCmpPredicate::ugt:
          result = z3::ugt(*lhs, *rhs);
          break;
        case mlir::smt::BVCmpPredicate::uge:
          result = z3::uge(*lhs, *rhs);
          break;
        }
      }
    }
    if (!result)
      return std::nullopt;
    expressions[value] = values.size();
    values.push_back(*result);
    return result;
  }

private:
  template <typename Fn>
  std::optional<z3::expr> translateBinary(mlir::Value lhs, mlir::Value rhs,
                                          Fn &&combine) {
    auto left = translate(lhs);
    auto right = translate(rhs);
    if (!left || !right)
      return std::nullopt;
    return combine(*left, *right);
  }

  std::optional<z3::expr> translateComparison(mlir::ValueRange inputs,
                                              bool equal) {
    if (inputs.size() < 2)
      return std::nullopt;
    z3::expr result = context.bool_val(true);
    if (equal) {
      auto previous = translate(inputs.front());
      if (!previous)
        return std::nullopt;
      for (mlir::Value input : inputs.drop_front()) {
        auto current = translate(input);
        if (!current)
          return std::nullopt;
        result = result && (*previous == *current);
        previous = std::move(current);
      }
      return result;
    }
    for (size_t left = 0; left != inputs.size(); ++left)
      for (size_t right = left + 1; right != inputs.size(); ++right) {
        auto lhs = translate(inputs[left]);
        auto rhs = translate(inputs[right]);
        if (!lhs || !rhs)
          return std::nullopt;
        result = result && (*lhs != *rhs);
      }
    return result;
  }

  std::optional<z3::expr> translateBooleanRange(mlir::ValueRange inputs,
                                                bool conjunction) {
    z3::expr result = context.bool_val(conjunction);
    for (mlir::Value input : inputs) {
      auto current = translate(input);
      if (!current)
        return std::nullopt;
      result = conjunction ? result && *current : result || *current;
    }
    return result;
  }

  std::optional<z3::expr> translateBooleanXor(mlir::ValueRange inputs) {
    if (inputs.size() < 2)
      return std::nullopt;
    z3::expr result = context.bool_val(false);
    for (mlir::Value input : inputs) {
      auto current = translate(input);
      if (!current)
        return std::nullopt;
      result = result != *current;
    }
    return result;
  }

  z3::context &context;
  llvm::DenseMap<mlir::Value, size_t> expressions;
  std::vector<z3::expr> values;
  unsigned declarationIndex = 0;
};

} // namespace

RandomProgramAnalysis analyzeRandomProgram(const uint8_t *program,
                                           size_t programSize,
                                           uint64_t resourceLimit) {
  RandomProgramAnalysis analysis;
  analysis.backend = "z3-4.13.4";
  std::optional<RandomProgramSMT> smt =
      buildRandomProgramSMT(program, programSize);
  if (!smt)
    return analysis;

  // Z3 uses exceptions internally even though the rest of Obelisk follows
  // LLVM's no-exceptions convention. They are contained within this
  // translation unit and converted into conservative Unknown results.
  try {
    z3::context context;
    Z3SMTShim shim(context);
    std::optional<z3::expr> hard = shim.translate(smt->hard);
    if (!hard)
      return analysis;
    z3::solver solver(context);
    z3::params parameters(context);
    parameters.set("random_seed", 0u);
    parameters.set("rlimit", static_cast<unsigned>(
                                 std::min<uint64_t>(resourceLimit, UINT_MAX)));
    solver.set(parameters);
    solver.add(*hard);
    z3::check_result result = solver.check();
    switch (result) {
    case z3::sat:
      analysis.satisfiability = Satisfiability::Satisfiable;
      break;
    case z3::unsat:
      analysis.satisfiability = Satisfiability::Unsatisfiable;
      break;
    case z3::unknown:
      break;
    }
    if (result != z3::sat)
      return analysis;

    // Bound each serialized variable independently. Captures remain free in
    // every query, so these are conservative bounds on the projection of all
    // solutions for all possible runtime capture values. An unknown query
    // abandons only that variable's optional narrowing plan.
    z3::model witness = solver.get_model();
    auto checkWith = [&](const z3::expr &predicate) {
      solver.push();
      solver.add(predicate);
      z3::check_result queryResult = solver.check();
      solver.pop();
      return queryResult;
    };

    // Collapse top-level direct variable equalities into equivalence classes.
    // Each non-canonical field can then be copied from the lowest-offset field
    // in its class. Prove the complete extracted relation once more against the
    // SMT formula before exposing it to lowering; unknown remains conservative.
    std::vector<size_t> parents(smt->variables.size());
    for (size_t index = 0; index != parents.size(); ++index)
      parents[index] = index;
    auto findRoot = [&](size_t index) {
      while (parents[index] != index) {
        parents[index] = parents[parents[index]];
        index = parents[index];
      }
      return index;
    };
    auto variableIndex = [&](const SMTVariable &candidate) {
      auto found = std::find_if(smt->variables.begin(), smt->variables.end(),
                                [&](const SMTVariable &variable) {
                                  return variable.offset == candidate.offset &&
                                         variable.width == candidate.width;
                                });
      return static_cast<size_t>(found - smt->variables.begin());
    };
    for (const SMTVariableEquality &equality : smt->directEqualities) {
      size_t lhs = variableIndex(equality.lhs);
      size_t rhs = variableIndex(equality.rhs);
      if (lhs == smt->variables.size() || rhs == smt->variables.size())
        continue;
      lhs = findRoot(lhs);
      rhs = findRoot(rhs);
      if (lhs == rhs)
        continue;
      if (smt->variables[rhs].offset < smt->variables[lhs].offset)
        std::swap(lhs, rhs);
      parents[rhs] = lhs;
    }

    z3::expr aliasViolation = context.bool_val(false);
    bool translatedAliases = true;
    for (size_t index = 0; index != parents.size(); ++index) {
      size_t source = findRoot(index);
      if (source == index)
        continue;
      const SMTVariable &targetVariable = smt->variables[index];
      const SMTVariable &sourceVariable = smt->variables[source];
      std::optional<z3::expr> target = shim.translate(targetVariable.bits);
      std::optional<z3::expr> sourceBits = shim.translate(sourceVariable.bits);
      if (!target || !sourceBits) {
        translatedAliases = false;
        break;
      }
      aliasViolation = aliasViolation || (*target != *sourceBits);
      analysis.aliases.push_back(
          {targetVariable.offset, sourceVariable.offset, targetVariable.width});
    }
    if (!translatedAliases ||
        (!analysis.aliases.empty() && checkWith(aliasViolation) != z3::unsat))
      analysis.aliases.clear();

    for (const SMTVariable &variable : smt->variables) {
      std::optional<z3::expr> bits = shim.translate(variable.bits);
      if (!bits)
        continue;
      uint64_t modelValue = 0;
      z3::expr evaluated = witness.eval(*bits, true);
      if (!evaluated.is_numeral_u64(modelValue))
        continue;

      uint64_t lower = 0;
      uint64_t upper = modelValue;
      bool complete = true;
      while (lower < upper) {
        uint64_t midpoint = lower + ((upper - lower) >> 1);
        z3::check_result query =
            checkWith(z3::ule(*bits, context.bv_val(midpoint, variable.width)));
        if (query == z3::sat)
          upper = midpoint;
        else if (query == z3::unsat)
          lower = midpoint + 1;
        else {
          complete = false;
          break;
        }
      }
      if (!complete)
        continue;
      uint64_t minimum = lower;

      lower = modelValue;
      upper = variable.width == 64 ? UINT64_MAX
                                   : (uint64_t{1} << variable.width) - 1;
      while (lower < upper) {
        uint64_t distance = upper - lower;
        uint64_t midpoint = lower + (distance >> 1) + (distance & 1);
        z3::check_result query =
            checkWith(z3::uge(*bits, context.bv_val(midpoint, variable.width)));
        if (query == z3::sat)
          lower = midpoint;
        else if (query == z3::unsat)
          upper = midpoint - 1;
        else {
          complete = false;
          break;
        }
      }
      if (!complete)
        continue;
      uint64_t maximum = lower;
      uint64_t fullMaximum = variable.width == 64
                                 ? UINT64_MAX
                                 : (uint64_t{1} << variable.width) - 1;
      if (minimum != 0 || maximum != fullMaximum)
        analysis.domains.push_back(
            {variable.offset, variable.width, minimum, maximum});
    }

    // The bounds and aliases above already prove hard => proposal. Prove the
    // reverse implication as one final query; exact proposals can bypass both
    // the generated checker and the runtime fallback.
    z3::expr proposal = context.bool_val(true);
    bool translatedProposal = true;
    for (const RandomVariableDomain &domain : analysis.domains) {
      auto found = std::find_if(smt->variables.begin(), smt->variables.end(),
                                [&](const SMTVariable &variable) {
                                  return variable.offset == domain.offset &&
                                         variable.width == domain.width;
                                });
      if (found == smt->variables.end()) {
        translatedProposal = false;
        break;
      }
      std::optional<z3::expr> bits = shim.translate(found->bits);
      if (!bits) {
        translatedProposal = false;
        break;
      }
      proposal = proposal &&
                 z3::uge(*bits, context.bv_val(domain.lower, domain.width)) &&
                 z3::ule(*bits, context.bv_val(domain.upper, domain.width));
    }
    for (const RandomVariableAlias &alias : analysis.aliases) {
      auto target =
          std::find_if(smt->variables.begin(), smt->variables.end(),
                       [&](const SMTVariable &variable) {
                         return variable.offset == alias.targetOffset &&
                                variable.width == alias.width;
                       });
      auto source =
          std::find_if(smt->variables.begin(), smt->variables.end(),
                       [&](const SMTVariable &variable) {
                         return variable.offset == alias.sourceOffset &&
                                variable.width == alias.width;
                       });
      if (target == smt->variables.end() || source == smt->variables.end()) {
        translatedProposal = false;
        break;
      }
      std::optional<z3::expr> targetBits = shim.translate(target->bits);
      std::optional<z3::expr> sourceBits = shim.translate(source->bits);
      if (!targetBits || !sourceBits) {
        translatedProposal = false;
        break;
      }
      proposal = proposal && (*targetBits == *sourceBits);
    }
    if (translatedProposal && checkWith(proposal && !*hard) == z3::unsat)
      analysis.proposalExact = true;
  } catch (...) {
  }
  return analysis;
}

} // namespace obelisk::solver

#endif
