//===- SolverZ3.cpp - Execute MLIR SMT formulas with Z3 -------*- C++ -*-===//

#include "obelisk/Solver/ConstraintSolver.h"

#ifdef OBELISK_ENABLE_Z3

#include "RandomProgramSMT.h"

#include "mlir/Dialect/SMT/IR/SMTOps.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"

#include "z3++.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace obelisk::solver {
namespace {

/// Obelisk deliberately builds Z3 with Z3_SINGLE_THREADED so the compiler and
/// its WebAssembly build do not acquire a pthread runtime dependency. That Z3
/// configuration is explicitly non-thread-safe, while MLIR may lower isolated
/// simulation units concurrently. Serialize every entry through the in-process
/// shim; generated simulation code and the runtime remain fully parallel.
static std::mutex z3Mutex;

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
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVSDivOp>(operation)) {
      result =
          translateBinary(op.getLhs(), op.getRhs(),
                          [](z3::expr lhs, z3::expr rhs) { return lhs / rhs; });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVUDivOp>(operation)) {
      result = translateBinary(
          op.getLhs(), op.getRhs(),
          [](z3::expr lhs, z3::expr rhs) { return z3::udiv(lhs, rhs); });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVSRemOp>(operation)) {
      result = translateBinary(
          op.getLhs(), op.getRhs(),
          [](z3::expr lhs, z3::expr rhs) { return z3::srem(lhs, rhs); });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVURemOp>(operation)) {
      result = translateBinary(
          op.getLhs(), op.getRhs(),
          [](z3::expr lhs, z3::expr rhs) { return z3::urem(lhs, rhs); });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVShlOp>(operation)) {
      result = translateBinary(
          op.getLhs(), op.getRhs(),
          [](z3::expr lhs, z3::expr rhs) { return z3::shl(lhs, rhs); });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVLShrOp>(operation)) {
      result = translateBinary(
          op.getLhs(), op.getRhs(),
          [](z3::expr lhs, z3::expr rhs) { return z3::lshr(lhs, rhs); });
    } else if (auto op = mlir::dyn_cast<mlir::smt::BVAShrOp>(operation)) {
      result = translateBinary(
          op.getLhs(), op.getRhs(),
          [](z3::expr lhs, z3::expr rhs) { return z3::ashr(lhs, rhs); });
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
                                           uint64_t resourceLimit,
                                           bool preferGlobalAssignmentTable) {
  RandomProgramAnalysis analysis;
  analysis.backend = "z3-4.13.4";
  std::optional<RandomProgramSMT> smt =
      buildRandomProgramSMT(program, programSize);
  if (!smt)
    return analysis;

  // Decoding and SMT-dialect construction are independent compiler work. Only
  // serialize the region that enters the non-thread-safe Z3 library.
  std::lock_guard<std::mutex> lock(z3Mutex);

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
    solver.push();
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
    // Each non-canonical field can then be copied from one representative.
    // Prove the complete extracted relation once more against the SMT formula
    // before exposing it to lowering; unknown remains conservative.
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

    // Select one deterministic, separately proven definition per target.
    // Alias classes that contain exactly one such target use it as their
    // representative, allowing a definition to feed every equal field.
    std::vector<const SMTVariableDefinition *> definitions;
    for (const SMTVariableDefinition &definition : smt->directDefinitions) {
      if (std::any_of(definitions.begin(), definitions.end(),
                      [&](const SMTVariableDefinition *selected) {
                        return selected->target.offset ==
                                   definition.target.offset &&
                               selected->target.width ==
                                   definition.target.width;
                      }))
        continue;
      std::optional<z3::expr> target = shim.translate(definition.target.bits);
      std::optional<z3::expr> expression =
          shim.translate(definition.expression);
      if (!target || !expression ||
          definition.expressionBegin >= definition.expressionEnd)
        continue;
      if (checkWith(*target != *expression) != z3::unsat)
        continue;
      definitions.push_back(&definition);
    }

    std::vector<unsigned> classDefinitionCounts(parents.size());
    std::vector<size_t> definitionTargetIndices(definitions.size());
    for (size_t index = 0; index != definitions.size(); ++index) {
      size_t target = variableIndex(definitions[index]->target);
      definitionTargetIndices[index] = target;
      if (target != smt->variables.size())
        ++classDefinitionCounts[findRoot(target)];
    }
    std::vector<std::optional<size_t>> classDefinitions(parents.size());
    std::vector<size_t> representatives(parents.size());
    for (size_t index = 0; index != parents.size(); ++index)
      representatives[index] = findRoot(index);
    for (size_t index = 0; index != definitions.size(); ++index) {
      size_t target = definitionTargetIndices[index];
      if (target == smt->variables.size())
        continue;
      size_t root = findRoot(target);
      if (classDefinitionCounts[root] == 1) {
        classDefinitions[root] = index;
        representatives[root] = target;
      }
    }

    z3::expr aliasViolation = context.bool_val(false);
    bool translatedAliases = true;
    for (size_t index = 0; index != parents.size(); ++index) {
      size_t source = representatives[findRoot(index)];
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
    bool validAliases =
        translatedAliases &&
        (analysis.aliases.empty() || checkWith(aliasViolation) == z3::unsat);
    if (!validAliases) {
      analysis.aliases.clear();
      // Do not let an unexposed alias relation influence the definition plan.
      // Restore singleton classes so independent definitions can still be
      // scheduled exactly as they would be without any direct equalities.
      std::fill(classDefinitionCounts.begin(), classDefinitionCounts.end(), 0);
      std::fill(classDefinitions.begin(), classDefinitions.end(), std::nullopt);
      for (size_t index = 0; index != parents.size(); ++index) {
        parents[index] = index;
        representatives[index] = index;
      }
      for (size_t index = 0; index != definitions.size(); ++index) {
        size_t target = definitionTargetIndices[index];
        if (target == smt->variables.size())
          continue;
        ++classDefinitionCounts[target];
        classDefinitions[target] = index;
      }
    }

    // Definitions depend on the representative of every alias class they
    // read. Topologically order the acyclic portion; a definition that reads
    // another member of its own class is a semantic self-cycle and remains on
    // the checker/runtime path.
    std::vector<std::vector<size_t>> dependents(definitions.size());
    std::vector<unsigned> indegrees(definitions.size());
    std::vector<bool> eligible(definitions.size());
    std::vector<bool> selfDependent(definitions.size());
    for (size_t index = 0; index != definitions.size(); ++index) {
      size_t target = definitionTargetIndices[index];
      if (target == smt->variables.size())
        continue;
      size_t targetRoot = findRoot(target);
      eligible[index] = classDefinitionCounts[targetRoot] == 1;
      if (!eligible[index])
        continue;
      for (const SMTVariable &dependency : definitions[index]->dependencies) {
        size_t dependencyVariable = variableIndex(dependency);
        if (dependencyVariable == smt->variables.size())
          continue;
        size_t dependencyRoot = findRoot(dependencyVariable);
        if (dependencyRoot == targetRoot) {
          selfDependent[index] = true;
          continue;
        }
        std::optional<size_t> dependencyDefinition =
            classDefinitions[dependencyRoot];
        if (!dependencyDefinition || *dependencyDefinition == index ||
            std::find(dependents[*dependencyDefinition].begin(),
                      dependents[*dependencyDefinition].end(),
                      index) != dependents[*dependencyDefinition].end())
          continue;
        dependents[*dependencyDefinition].push_back(index);
        ++indegrees[index];
      }
    }

    std::vector<bool> emitted(definitions.size());
    for (size_t count = 0; count != definitions.size(); ++count) {
      size_t index = 0;
      while (index != definitions.size() &&
             (emitted[index] || !eligible[index] || selfDependent[index] ||
              indegrees[index] != 0))
        ++index;
      if (index == definitions.size())
        break;
      emitted[index] = true;
      const SMTVariableDefinition &definition = *definitions[index];
      analysis.definitions.push_back(
          {definition.target.offset, definition.target.width,
           definition.expressionBegin, definition.expressionEnd});
      for (size_t dependent : dependents[index])
        --indegrees[dependent];
    }

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

    // Preserve at most one direct capture bound in each direction per
    // otherwise independently sampled field. The decoder only records exact
    // comparisons, and this implication proof keeps malformed or
    // unexpectedly transformed SMT shapes from crossing the solver API
    // boundary.
    auto isLowerCaptureBound = [](RandomCaptureBoundKind kind) {
      return kind == RandomCaptureBoundKind::LowerInclusive ||
             kind == RandomCaptureBoundKind::LowerExclusive;
    };
    auto convertCaptureBoundKind = [](SMTCaptureBoundKind kind) {
      switch (kind) {
      case SMTCaptureBoundKind::LowerInclusive:
        return RandomCaptureBoundKind::LowerInclusive;
      case SMTCaptureBoundKind::LowerExclusive:
        return RandomCaptureBoundKind::LowerExclusive;
      case SMTCaptureBoundKind::UpperInclusive:
        return RandomCaptureBoundKind::UpperInclusive;
      case SMTCaptureBoundKind::UpperExclusive:
        return RandomCaptureBoundKind::UpperExclusive;
      }
      llvm_unreachable("unknown SMT capture bound kind");
    };
    for (const SMTVariableCaptureBound &bound : smt->directCaptureBounds) {
      RandomCaptureBoundKind kind = convertCaptureBoundKind(bound.kind);
      bool conflicts =
          (bound.isSigned &&
           llvm::any_of(analysis.domains,
                        [&](const RandomVariableDomain &domain) {
                          return domain.offset == bound.target.offset &&
                                 domain.width == bound.target.width;
                        })) ||
          llvm::any_of(analysis.captureBounds,
                       [&](const RandomVariableCaptureBound &selected) {
                         return selected.offset == bound.target.offset &&
                                selected.width == bound.target.width &&
                                (selected.isSigned != bound.isSigned ||
                                 isLowerCaptureBound(selected.kind) ==
                                     isLowerCaptureBound(kind));
                       }) ||
          llvm::any_of(analysis.aliases,
                       [&](const RandomVariableAlias &alias) {
                         return (alias.targetOffset == bound.target.offset ||
                                 alias.sourceOffset == bound.target.offset) &&
                                alias.width == bound.target.width;
                       }) ||
          llvm::any_of(analysis.definitions,
                       [&](const RandomVariableDefinition &definition) {
                         return definition.targetOffset ==
                                    bound.target.offset &&
                                definition.width == bound.target.width;
                       });
      if (conflicts || bound.captureIndex >= smt->captures.size())
        continue;
      std::optional<z3::expr> predicate = shim.translate(bound.predicate);
      if (!predicate || checkWith(!*predicate) != z3::unsat)
        continue;
      analysis.captureBounds.push_back({bound.target.offset, bound.target.width,
                                        bound.captureIndex, kind,
                                        bound.isSigned});
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
    for (const RandomVariableCaptureBound &bound : analysis.captureBounds) {
      auto found = std::find_if(smt->variables.begin(), smt->variables.end(),
                                [&](const SMTVariable &variable) {
                                  return variable.offset == bound.offset &&
                                         variable.width == bound.width;
                                });
      if (found == smt->variables.end() ||
          bound.captureIndex >= smt->captures.size()) {
        translatedProposal = false;
        break;
      }
      std::optional<z3::expr> bits = shim.translate(found->bits);
      std::optional<z3::expr> capture =
          shim.translate(smt->captures[bound.captureIndex]);
      if (!bits || !capture) {
        translatedProposal = false;
        break;
      }
      z3::expr captureBits = capture->extract(bound.width - 1, 0);
      switch (bound.kind) {
      case RandomCaptureBoundKind::LowerInclusive:
        proposal = proposal && (bound.isSigned ? z3::sge(*bits, captureBits)
                                               : z3::uge(*bits, captureBits));
        break;
      case RandomCaptureBoundKind::LowerExclusive:
        proposal = proposal && (bound.isSigned ? z3::sgt(*bits, captureBits)
                                               : z3::ugt(*bits, captureBits));
        break;
      case RandomCaptureBoundKind::UpperInclusive:
        proposal = proposal && (bound.isSigned ? z3::sle(*bits, captureBits)
                                               : z3::ule(*bits, captureBits));
        break;
      case RandomCaptureBoundKind::UpperExclusive:
        proposal = proposal && (bound.isSigned ? z3::slt(*bits, captureBits)
                                               : z3::ult(*bits, captureBits));
        break;
      }
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
    for (const RandomVariableDefinition &definition : analysis.definitions) {
      auto found = std::find_if(
          smt->directDefinitions.begin(), smt->directDefinitions.end(),
          [&](const SMTVariableDefinition &candidate) {
            return candidate.target.offset == definition.targetOffset &&
                   candidate.target.width == definition.width &&
                   candidate.expressionBegin == definition.expressionBegin &&
                   candidate.expressionEnd == definition.expressionEnd;
          });
      if (found == smt->directDefinitions.end()) {
        translatedProposal = false;
        break;
      }
      std::optional<z3::expr> targetBits = shim.translate(found->target.bits);
      std::optional<z3::expr> expression = shim.translate(found->expression);
      if (!targetBits || !expression) {
        translatedProposal = false;
        break;
      }
      proposal = proposal && (*targetBits == *expression);
    }
    // All preceding implication queries intentionally run under `hard`.
    // Remove that assertion for the reverse implication: retaining it would
    // make `proposal && !hard` vacuously unsatisfiable.
    solver.pop();
    if (translatedProposal && checkWith(proposal && !*hard) == z3::unsat)
      analysis.proposalExact = true;

    // Fully enumerate small capture-free formulas when structural planning was
    // not exact or the client needs the joint conditional distribution. A
    // complete solution table lets generated code sample correlated assignments
    // directly and needs neither a checker nor runtime solving. Keep the table
    // deliberately small to bound both compile time and IR size.
    constexpr unsigned maxAssignmentWidth = 12;
    constexpr size_t maxAssignmentTableSize = 16;
    auto assignmentType =
        mlir::dyn_cast<mlir::smt::BitVectorType>(smt->assignment.getType());
    if ((!analysis.proposalExact || preferGlobalAssignmentTable) &&
        smt->captures.empty() && assignmentType &&
        assignmentType.getWidth() <= maxAssignmentWidth) {
      z3::context enumerationContext;
      Z3SMTShim enumerationShim(enumerationContext);
      std::optional<z3::expr> enumerationHard =
          enumerationShim.translate(smt->hard);
      std::optional<z3::expr> assignment =
          enumerationShim.translate(smt->assignment);
      if (enumerationHard && assignment) {
        z3::solver enumerator(enumerationContext);
        z3::params enumerationParameters(enumerationContext);
        enumerationParameters.set("random_seed", 0u);
        enumerationParameters.set(
            "rlimit",
            static_cast<unsigned>(std::min<uint64_t>(resourceLimit, UINT_MAX)));
        enumerator.set(enumerationParameters);
        enumerator.add(*enumerationHard);
        std::vector<uint64_t> assignments;
        bool complete = false;
        while (assignments.size() <= maxAssignmentTableSize) {
          z3::check_result enumerationResult = enumerator.check();
          if (enumerationResult == z3::unsat) {
            complete = true;
            break;
          }
          if (enumerationResult != z3::sat ||
              assignments.size() == maxAssignmentTableSize)
            break;
          uint64_t value = 0;
          z3::expr evaluated = enumerator.get_model().eval(*assignment, true);
          if (!evaluated.is_numeral_u64(value))
            break;
          assignments.push_back(value);
          enumerator.add(*assignment != enumerationContext.bv_val(
                                            value, assignmentType.getWidth()));
        }
        if (complete && !assignments.empty()) {
          std::sort(assignments.begin(), assignments.end());
          analysis.assignmentTable = std::move(assignments);
          analysis.domains.clear();
          analysis.aliases.clear();
          analysis.definitions.clear();
          analysis.proposalExact = true;
        }
      }
    }

    // A global table can exceed the width or row limit even when the hard
    // formula is a conjunction of tiny independent components. Build the
    // variable connectivity graph from each top-level hard constraint, then
    // enumerate every constrained component separately. Unconstrained
    // variables remain free in the generated proposal.
    bool analyzeComponents =
        analysis.assignmentTable.empty() &&
        (!analysis.proposalExact || preferGlobalAssignmentTable);
    if (analyzeComponents)
      analysis.hasConstraintComponentPartition = true;
    if (analyzeComponents && !smt->variables.empty()) {
      std::vector<size_t> componentParents(smt->variables.size());
      for (size_t index = 0; index != componentParents.size(); ++index)
        componentParents[index] = index;
      auto findComponentRoot = [&](size_t index) {
        while (componentParents[index] != index) {
          componentParents[index] = componentParents[componentParents[index]];
          index = componentParents[index];
        }
        return index;
      };
      auto mergeComponents = [&](size_t lhs, size_t rhs) {
        lhs = findComponentRoot(lhs);
        rhs = findComponentRoot(rhs);
        if (lhs == rhs)
          return;
        if (smt->variables[rhs].offset < smt->variables[lhs].offset)
          std::swap(lhs, rhs);
        componentParents[rhs] = lhs;
      };
      for (const SMTHardConstraint &constraint : smt->hardConstraints) {
        if (constraint.dependencies.empty())
          continue;
        size_t first = variableIndex(constraint.dependencies.front());
        if (first == smt->variables.size())
          continue;
        for (const SMTVariable &dependency :
             llvm::ArrayRef(constraint.dependencies).drop_front()) {
          size_t other = variableIndex(dependency);
          if (other != smt->variables.size())
            mergeComponents(first, other);
        }
      }

      std::vector<std::vector<size_t>> componentVariables(
          smt->variables.size());
      for (size_t index = 0; index != smt->variables.size(); ++index)
        componentVariables[findComponentRoot(index)].push_back(index);
      std::vector<std::vector<const SMTHardConstraint *>> componentConstraints(
          smt->variables.size());
      for (const SMTHardConstraint &constraint : smt->hardConstraints) {
        if (constraint.dependencies.empty())
          continue;
        size_t variable = variableIndex(constraint.dependencies.front());
        if (variable != smt->variables.size())
          componentConstraints[findComponentRoot(variable)].push_back(
              &constraint);
      }

      for (size_t root = 0; root != componentVariables.size(); ++root) {
        if (componentConstraints[root].empty())
          continue;
        uint64_t componentMask = 0;
        for (size_t variableNumber : componentVariables[root]) {
          const SMTVariable &variable = smt->variables[variableNumber];
          uint64_t valueMask = variable.width == 64
                                   ? UINT64_MAX
                                   : (uint64_t{1} << variable.width) - 1;
          componentMask |= valueMask << variable.offset;
        }
        analysis.constraintComponentMasks.push_back(componentMask);
      }

      std::vector<RandomAssignmentTable> componentTables;
      // Compose component tables with the structural proposal already proven
      // to contain every solution. This lets a capture-dependent interval or
      // definition coexist with independent capture-free tables; the final
      // whole-formula equivalence query remains the authority for exactness.
      z3::expr composedProposal = proposal;
      bool sawConstrainedComponent = false;
      bool allConstrainedComponentsComplete = true;
      for (size_t root = 0; root != componentVariables.size(); ++root) {
        if (componentConstraints[root].empty())
          continue;
        sawConstrainedComponent = true;
        if (llvm::any_of(componentConstraints[root],
                         [](const SMTHardConstraint *constraint) {
                           return constraint->hasCapture;
                         }))
          continue;
        unsigned componentWidth = 0;
        uint64_t componentMask = 0;
        for (size_t variableNumber : componentVariables[root]) {
          const SMTVariable &variable = smt->variables[variableNumber];
          componentWidth += variable.width;
          uint64_t valueMask = variable.width == 64
                                   ? UINT64_MAX
                                   : (uint64_t{1} << variable.width) - 1;
          componentMask |= valueMask << variable.offset;
        }
        if (componentWidth > maxAssignmentWidth) {
          allConstrainedComponentsComplete = false;
          continue;
        }

        z3::context enumerationContext;
        Z3SMTShim enumerationShim(enumerationContext);
        z3::expr componentHard = enumerationContext.bool_val(true);
        bool translatedComponent = true;
        for (const SMTHardConstraint *constraint : componentConstraints[root]) {
          std::optional<z3::expr> translated =
              enumerationShim.translate(constraint->expression);
          if (!translated) {
            translatedComponent = false;
            break;
          }
          componentHard = componentHard && *translated;
        }
        if (!translatedComponent) {
          allConstrainedComponentsComplete = false;
          continue;
        }

        std::vector<z3::expr> componentBits;
        for (size_t variableNumber : componentVariables[root]) {
          std::optional<z3::expr> translated =
              enumerationShim.translate(smt->variables[variableNumber].bits);
          if (!translated) {
            translatedComponent = false;
            break;
          }
          componentBits.push_back(*translated);
        }
        if (!translatedComponent) {
          allConstrainedComponentsComplete = false;
          continue;
        }

        z3::solver enumerator(enumerationContext);
        z3::params enumerationParameters(enumerationContext);
        enumerationParameters.set("random_seed", 0u);
        enumerationParameters.set(
            "rlimit",
            static_cast<unsigned>(std::min<uint64_t>(resourceLimit, UINT_MAX)));
        enumerator.set(enumerationParameters);
        enumerator.add(componentHard);
        RandomAssignmentTable table;
        table.mask = componentMask;
        bool complete = false;
        while (table.assignments.size() <= maxAssignmentTableSize) {
          z3::check_result enumerationResult = enumerator.check();
          if (enumerationResult == z3::unsat) {
            complete = true;
            break;
          }
          if (enumerationResult != z3::sat ||
              table.assignments.size() == maxAssignmentTableSize)
            break;
          z3::model model = enumerator.get_model();
          uint64_t assignment = 0;
          z3::expr different = enumerationContext.bool_val(false);
          for (auto [variableNumber, bits] :
               llvm::zip_equal(componentVariables[root], componentBits)) {
            const SMTVariable &variable = smt->variables[variableNumber];
            uint64_t value = 0;
            z3::expr evaluated = model.eval(bits, true);
            if (!evaluated.is_numeral_u64(value)) {
              translatedComponent = false;
              break;
            }
            assignment |= value << variable.offset;
            different =
                different ||
                (bits != enumerationContext.bv_val(value, variable.width));
          }
          if (!translatedComponent)
            break;
          table.assignments.push_back(assignment);
          enumerator.add(different);
        }
        if (!translatedComponent || !complete || table.assignments.empty()) {
          allConstrainedComponentsComplete = false;
          continue;
        }
        std::sort(table.assignments.begin(), table.assignments.end());

        // Prove each table against its own component formula before retaining
        // it. If dependency discovery missed a cross-component value, the
        // free value remains in the original formula and this equivalence
        // fails.
        z3::expr originalComponentHard = context.bool_val(true);
        bool translatedTable = true;
        for (const SMTHardConstraint *constraint : componentConstraints[root]) {
          std::optional<z3::expr> translated =
              shim.translate(constraint->expression);
          if (!translated) {
            translatedTable = false;
            break;
          }
          originalComponentHard = originalComponentHard && *translated;
        }
        z3::expr tableProposal = context.bool_val(false);
        for (uint64_t assignment : table.assignments) {
          z3::expr row = context.bool_val(true);
          for (size_t variableNumber : componentVariables[root]) {
            const SMTVariable &variable = smt->variables[variableNumber];
            uint64_t valueMask = variable.width == 64
                                     ? UINT64_MAX
                                     : (uint64_t{1} << variable.width) - 1;
            std::optional<z3::expr> bits = shim.translate(variable.bits);
            if (!bits) {
              translatedTable = false;
              break;
            }
            uint64_t value = (assignment >> variable.offset) & valueMask;
            row = row && (*bits == context.bv_val(value, variable.width));
          }
          if (!translatedTable)
            break;
          tableProposal = tableProposal || row;
        }
        bool equivalentTable =
            translatedTable &&
            checkWith(originalComponentHard && !tableProposal) == z3::unsat &&
            checkWith(tableProposal && !originalComponentHard) == z3::unsat;
        if (!equivalentTable) {
          allConstrainedComponentsComplete = false;
          continue;
        }
        composedProposal = composedProposal && tableProposal;
        componentTables.push_back(std::move(table));
      }

      if (!componentTables.empty()) {
        analysis.assignmentTables = std::move(componentTables);
        uint64_t tableMask = 0;
        for (const RandomAssignmentTable &table : analysis.assignmentTables)
          tableMask |= table.mask;
        auto variableMask = [](uint32_t offset, uint32_t width) {
          uint64_t mask = width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
          return mask << offset;
        };
        llvm::erase_if(analysis.domains, [&](const RandomVariableDomain &item) {
          return (variableMask(item.offset, item.width) & tableMask) != 0;
        });
        llvm::erase_if(analysis.aliases, [&](const RandomVariableAlias &item) {
          return ((variableMask(item.targetOffset, item.width) |
                   variableMask(item.sourceOffset, item.width)) &
                  tableMask) != 0;
        });
        llvm::erase_if(analysis.definitions,
                       [&](const RandomVariableDefinition &item) {
                         return (variableMask(item.targetOffset, item.width) &
                                 tableMask) != 0;
                       });

        bool equivalentCompleteProposal =
            sawConstrainedComponent && allConstrainedComponentsComplete &&
            checkWith(*hard && !composedProposal) == z3::unsat &&
            checkWith(composedProposal && !*hard) == z3::unsat;
        if (equivalentCompleteProposal)
          analysis.proposalExact = true;
      }
    }
  } catch (...) {
  }
  return analysis;
}

} // namespace obelisk::solver

#endif
