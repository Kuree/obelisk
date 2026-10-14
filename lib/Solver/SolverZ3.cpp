//===- SolverZ3.cpp - Execute MLIR SMT formulas with Z3 -------*- C++ -*-===//

#include "obelisk/Solver/ConstraintSolver.h"

#ifdef OBELISK_ENABLE_Z3

#include "RandomProgramSMT.h"
#include "Z3Support.h"

#include "mlir/Dialect/SMT/IR/SMTOps.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"

#include "z3++.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <unordered_set>
#ifdef OBELISK_Z3_SINGLE_THREADED
#include <mutex>
#endif
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace obelisk::solver {
namespace {

uint64_t variableKey(uint32_t offset, unsigned width) {
  return (static_cast<uint64_t>(offset) << 32) | width;
}

/// Choose the lowest-offset representative of each undirected component in
/// one traversal, without repeated union-find or whole-variable scans.
std::vector<size_t>
connectedComponents(const std::vector<std::vector<size_t>> &neighbors,
                    llvm::ArrayRef<SMTVariable> variables) {
  std::vector<size_t> roots(variables.size(), SIZE_MAX);
  std::vector<size_t> pending;
  for (size_t seed = 0; seed != roots.size(); ++seed) {
    if (roots[seed] != SIZE_MAX)
      continue;
    size_t begin = pending.size();
    size_t representative = seed;
    roots[seed] = seed;
    pending.push_back(seed);
    for (size_t next = begin; next != pending.size(); ++next) {
      size_t index = pending[next];
      if (variables[index].offset < variables[representative].offset)
        representative = index;
      for (size_t neighbor : neighbors[index]) {
        if (roots[neighbor] != SIZE_MAX)
          continue;
        roots[neighbor] = seed;
        pending.push_back(neighbor);
      }
    }
    for (size_t next = begin; next != pending.size(); ++next)
      roots[pending[next]] = representative;
  }
  return roots;
}

z3::expr bitVectorValue(z3::context &context, const llvm::APInt &value) {
  // Passing fixed-size words avoids quadratic conversion of a wide APInt
  // into a decimal numeral before entering Z3.
  unsigned count = value.getNumWords();
  unsigned highWidth = value.getBitWidth() - (count - 1) * 64;
  const uint64_t *words = value.getRawData();
  z3::expr result = context.bv_val(words[count - 1], highWidth);
  while (--count)
    result = z3::concat(result, context.bv_val(words[count - 1], 64));
  return result;
}

std::optional<llvm::APInt> bitVectorNumeral(const z3::expr &value,
                                            unsigned width) {
  std::string binary;
  if (!value.as_binary(binary))
    return std::nullopt;
  return llvm::APInt(width, binary, 2);
}

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
    struct Frame {
      mlir::Value value;
      unsigned nextOperand = 0;
    };
    std::vector<Frame> pending{{value}};
    while (!pending.empty()) {
      Frame &frame = pending.back();
      if (expressions.contains(frame.value)) {
        pending.pop_back();
        continue;
      }
      mlir::Operation *operation = frame.value.getDefiningOp();
      if (!operation)
        return std::nullopt;
      if (frame.nextOperand != operation->getNumOperands()) {
        mlir::Value operand = operation->getOperand(frame.nextOperand++);
        if (!expressions.contains(operand))
          pending.push_back({operand});
        continue;
      }
      if (!translateReady(frame.value))
        return std::nullopt;
      pending.pop_back();
    }
    return values[expressions.lookup(value)];
  }

private:
  std::optional<z3::expr> translateReady(mlir::Value value) {
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
      result = bitVectorValue(context, bits);
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
    z3::expr_vector distinctInputs(context);
    for (mlir::Value input : inputs) {
      auto current = translate(input);
      if (!current)
        return std::nullopt;
      distinctInputs.push_back(*current);
    }
    return z3::distinct(distinctInputs);
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

#ifdef OBELISK_Z3_SINGLE_THREADED
std::mutex &detail::getZ3Mutex() {
  static std::mutex mutex;
  return mutex;
}
#endif

Satisfiability
detail::checkRandomDomainOverlapZ3(llvm::ArrayRef<RandomDomainPattern> patterns,
                                   uint64_t resourceLimit) {
#ifdef OBELISK_Z3_SINGLE_THREADED
  std::lock_guard<std::mutex> lock(detail::getZ3Mutex());
#endif
  try {
    z3::context context;
    z3::solver solver(context);
    z3::params parameters(context);
    parameters.set("random_seed", 0u);
    parameters.set("rlimit", static_cast<unsigned>(
                                 std::min<uint64_t>(resourceLimit, UINT_MAX)));
    solver.set(parameters);
    unsigned width = patterns.front().mask.getBitWidth();
    z3::expr field = context.bv_const("domain_field", width);
    z3::expr member = context.bool_val(false);
    z3::expr overlap = member;
    for (const RandomDomainPattern &pattern : patterns) {
      z3::expr matches = (field & bitVectorValue(context, pattern.mask)) ==
                         bitVectorValue(context, pattern.value);
      overlap = overlap || (member && matches);
      member = member || matches;
    }
    solver.add(overlap);
    z3::check_result result = solver.check();
    if (result == z3::unsat)
      return Satisfiability::Unsatisfiable;
    if (result == z3::sat)
      return Satisfiability::Satisfiable;
  } catch (const z3::exception &) {
  }
  return Satisfiability::Unknown;
}

/// Nonlinear products the optional program analysis accepts.
static constexpr unsigned maxNonlinearProducts = 16;

/// Products of two non-constant bit-vectors in an expression DAG.
static unsigned countNonlinearProducts(const z3::expr &root) {
  std::vector<z3::expr> pending{root};
  std::unordered_set<unsigned> visited;
  unsigned count = 0;
  while (!pending.empty()) {
    z3::expr node = pending.back();
    pending.pop_back();
    if (!node.is_app() || !visited.insert(node.id()).second)
      continue;
    if (node.decl().decl_kind() == Z3_OP_BMUL) {
      unsigned symbolic = 0;
      for (unsigned index = 0; index != node.num_args(); ++index)
        symbolic += !node.arg(index).is_numeral();
      count += symbolic > 1;
    }
    for (unsigned index = 0; index != node.num_args(); ++index)
      pending.push_back(node.arg(index));
  }
  return count;
}

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

#ifdef OBELISK_Z3_SINGLE_THREADED
  // The non-pthread wasm build removes Z3's internal synchronization. Native
  // compilers instead use the thread-safe build and enter here concurrently
  // with one context per worker invocation.
  std::lock_guard<std::mutex> lock(detail::getZ3Mutex());
#endif

  // Z3 uses exceptions internally even though the rest of Obelisk follows
  // LLVM's no-exceptions convention. They are contained within this
  // translation unit and converted into conservative Unknown results.
  try {
    z3::context context;
    Z3SMTShim shim(context);
    z3::solver solver(context);
    z3::params parameters(context);
    parameters.set("random_seed", 0u);
    parameters.set("rlimit", static_cast<unsigned>(
                                 std::min<uint64_t>(resourceLimit, UINT_MAX)));
    solver.set(parameters);
    // Unequal domain masks describe disjoint cubes only when no value matches
    // two records. Prove that with a linear prefix formula, independently of
    // the user's hard constraints; those may otherwise hide malformed records.
    for (mlir::Value overlap : smt->domainOverlaps) {
      std::optional<z3::expr> predicate = shim.translate(overlap);
      if (!predicate)
        return analysis;
      solver.push();
      solver.add(*predicate);
      z3::check_result overlapResult = solver.check();
      solver.pop();
      if (overlapResult != z3::unsat)
        return analysis;
    }
    std::optional<z3::expr> hard = shim.translate(smt->hard);
    if (!hard)
      return analysis;
    // Asserting a formula runs Z3's polynomial normalization before the
    // resource limit applies, and products of many symbolic factors (such as
    // a ** with a random exponent) make it unbounded. This analysis is
    // optional; leave such relations to the exact sampling plan.
    if (countNonlinearProducts(*hard) > maxNonlinearProducts)
      return analysis;
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

    // Soft constraints are preferences ordered from low to high priority.
    // Select the lexicographically best satisfiable set by retaining each
    // preference when it is compatible with every already-selected higher
    // priority. Discarded preferences add no negated constraint: they remain
    // irrelevant exactly as required by IEEE 1800-2017 18.5.14.1.
    std::vector<std::optional<z3::expr>> softExpressions(
        smt->softConstraints.size());
    std::vector<bool> selectedSoft(softExpressions.size());
    z3::expr effective = *hard;
    // A runtime capture can change which priority set is satisfiable. Leave
    // those calls on the residual path instead of freezing a preference chosen
    // for one existential capture value.
    if (smt->captures.empty()) {
      for (const SMTSoftConstraint &constraint : smt->softConstraints) {
        if (constraint.priority >= softExpressions.size() ||
            softExpressions[constraint.priority])
          return analysis;
        softExpressions[constraint.priority] =
            shim.translate(constraint.expression);
        if (!softExpressions[constraint.priority])
          return analysis;
      }
      for (size_t priority = softExpressions.size(); priority != 0;
           --priority) {
        solver.push();
        solver.add(*softExpressions[priority - 1]);
        z3::check_result preferred = solver.check();
        solver.pop();
        if (preferred == z3::unknown)
          return analysis;
        if (preferred == z3::unsat)
          continue;
        solver.add(*softExpressions[priority - 1]);
        effective = effective && *softExpressions[priority - 1];
        selectedSoft[priority - 1] = true;
      }
      for (const SMTSoftConstraint &constraint : smt->softConstraints) {
        if (!selectedSoft[constraint.priority])
          continue;
        smt->hardConstraints.push_back({constraint.expression,
                                        constraint.dependencies,
                                        constraint.hasCapture});
        if (constraint.directEquality)
          smt->directEqualities.push_back(*constraint.directEquality);
        if (constraint.directDefinition)
          smt->directDefinitions.push_back(*constraint.directDefinition);
        if (constraint.directCaptureBound)
          smt->directCaptureBounds.push_back(*constraint.directCaptureBound);
      }
      analysis.softPreferencesResolved = !smt->softConstraints.empty();
    }

    // A preference probe followed by pop/add invalidates Z3's current model,
    // including when the last priority was discarded as unsatisfiable. Check
    // the selected formula once so range extraction below always starts from
    // a valid witness for the effective preference set.
    if (analysis.softPreferencesResolved && solver.check() != z3::sat)
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
    llvm::DenseMap<uint64_t, size_t> variableIndices;
    for (auto [index, variable] : llvm::enumerate(smt->variables))
      variableIndices.try_emplace(variableKey(variable.offset, variable.width),
                                  index);
    auto variableIndex = [&](const SMTVariable &candidate) {
      auto found =
          variableIndices.find(variableKey(candidate.offset, candidate.width));
      return found == variableIndices.end() ? smt->variables.size()
                                            : found->second;
    };
    auto findVariable = [&](uint32_t offset,
                            unsigned width) -> const SMTVariable * {
      auto found = variableIndices.find(variableKey(offset, width));
      return found == variableIndices.end() ? nullptr
                                            : &smt->variables[found->second];
    };
    std::vector<std::vector<size_t>> aliasNeighbors(smt->variables.size());
    for (const SMTVariableEquality &equality : smt->directEqualities) {
      size_t lhs = variableIndex(equality.lhs);
      size_t rhs = variableIndex(equality.rhs);
      if (lhs == smt->variables.size() || rhs == smt->variables.size())
        continue;
      if (lhs == rhs)
        continue;
      aliasNeighbors[lhs].push_back(rhs);
      aliasNeighbors[rhs].push_back(lhs);
    }
    std::vector<size_t> parents =
        connectedComponents(aliasNeighbors, smt->variables);
    auto findRoot = [&](size_t index) { return parents[index]; };

    // Select one deterministic, separately proven definition per target.
    // Alias classes that contain exactly one such target use it as their
    // representative, allowing a definition to feed every equal field.
    std::vector<const SMTVariableDefinition *> definitions;
    llvm::DenseSet<uint64_t> definedTargets;
    for (const SMTVariableDefinition &definition : smt->directDefinitions) {
      uint64_t key =
          variableKey(definition.target.offset, definition.target.width);
      if (definedTargets.contains(key))
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
      definedTargets.insert(key);
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
    std::vector<size_t> lastDependent(definitions.size(), SIZE_MAX);
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
            lastDependent[*dependencyDefinition] == index)
          continue;
        lastDependent[*dependencyDefinition] = index;
        dependents[*dependencyDefinition].push_back(index);
        ++indegrees[index];
      }
    }

    std::vector<size_t> readyDefinitions;
    for (size_t index = 0; index != definitions.size(); ++index)
      if (eligible[index] && !selfDependent[index] && indegrees[index] == 0)
        readyDefinitions.push_back(index);
    std::vector<const SMTVariableDefinition *> emittedDefinitions;
    for (size_t next = 0; next != readyDefinitions.size(); ++next) {
      size_t index = readyDefinitions[next];
      const SMTVariableDefinition &definition = *definitions[index];
      analysis.definitions.push_back(
          {definition.target.offset, definition.target.width,
           definition.expressionBegin, definition.expressionEnd});
      emittedDefinitions.push_back(&definition);
      for (size_t dependent : dependents[index])
        if (--indegrees[dependent] == 0 && eligible[dependent] &&
            !selfDependent[dependent])
          readyDefinitions.push_back(dependent);
    }

    unsigned aggregateWidth =
        mlir::cast<mlir::smt::BitVectorType>(smt->assignment.getType())
            .getWidth();
    std::vector<uint32_t> finiteCoverage(static_cast<size_t>(aggregateWidth) +
                                         1);
    for (const SMTFiniteDomain &domain : smt->finiteDomains) {
      uint64_t end =
          static_cast<uint64_t>(domain.target.offset) + domain.target.width;
      for (uint64_t bit = domain.target.offset; bit != end; ++bit)
        finiteCoverage[bit + 1] = 1;
    }
    for (size_t bit = 1; bit != finiteCoverage.size(); ++bit)
      finiteCoverage[bit] += finiteCoverage[bit - 1];
    auto overlapsFiniteDomain = [&](const SMTVariable &variable) {
      uint64_t variableEnd =
          static_cast<uint64_t>(variable.offset) + variable.width;
      return finiteCoverage[variableEnd] != finiteCoverage[variable.offset];
    };
    llvm::DenseSet<uint64_t> signedBoundTargets;
    for (const SMTVariableCaptureBound &bound : smt->directCaptureBounds)
      if (bound.isSigned)
        signedBoundTargets.insert(
            variableKey(bound.target.offset, bound.target.width));
    llvm::DenseMap<uint64_t, bool> domainSignedness;
    for (const SMTVariable &variable : smt->variables) {
      // The generated finite-domain sampler reproduces these predicates
      // exactly. An enclosing min/max interval is redundant for a pure domain
      // and can make a sparse domain look inexact; constraints that further
      // restrict an overlapping field remain protected by the final reverse
      // implication proof below.
      if (overlapsFiniteDomain(variable))
        continue;
      // The generated interval sampler is 64-bit shaped. Searching for exact
      // min/max bounds above that width can require two solver queries per bit
      // yet still cannot produce a directly materializable proposal. Leave
      // wide variables to the bounded table enumeration below; sparse formulas
      // can still be solved completely at compile time without width-linear
      // bound discovery.
      if (variable.width > 64)
        continue;
      std::optional<z3::expr> bits = shim.translate(variable.bits);
      if (!bits)
        continue;
      uint64_t key = variableKey(variable.offset, variable.width);
      bool isSigned = signedBoundTargets.contains(key);
      z3::expr projectedBits =
          isSigned ? *bits ^ bitVectorValue(context, llvm::APInt::getOneBitSet(
                                                         variable.width,
                                                         variable.width - 1))
                   : *bits;
      z3::expr evaluated = witness.eval(projectedBits, true);
      std::optional<llvm::APInt> modelValue =
          bitVectorNumeral(evaluated, variable.width);
      if (!modelValue)
        continue;

      llvm::APInt lower = llvm::APInt::getZero(variable.width);
      llvm::APInt upper = *modelValue;
      bool complete = true;
      while (lower.ult(upper)) {
        llvm::APInt midpoint = lower + (upper - lower).lshr(1);
        z3::check_result query = checkWith(
            z3::ule(projectedBits, bitVectorValue(context, midpoint)));
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
      llvm::APInt minimum = lower;

      lower = *modelValue;
      upper = llvm::APInt::getAllOnes(variable.width);
      while (lower.ult(upper)) {
        llvm::APInt distance = upper - lower;
        llvm::APInt midpoint =
            lower + distance.lshr(1) + static_cast<uint64_t>(distance[0]);
        z3::check_result query = checkWith(
            z3::uge(projectedBits, bitVectorValue(context, midpoint)));
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
      llvm::APInt maximum = lower;
      llvm::APInt fullMaximum = llvm::APInt::getAllOnes(variable.width);
      if (!minimum.isZero() || maximum != fullMaximum) {
        analysis.domains.push_back(
            {variable.offset, variable.width, minimum, maximum, isSigned});
        domainSignedness.try_emplace(key, isSigned);
      }
    }

    // Preserve at most one direct capture bound in each direction per
    // otherwise independently sampled field. The decoder only records exact
    // comparisons, and this implication proof keeps malformed or
    // unexpectedly transformed SMT shapes from crossing the solver API
    // boundary.
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
    llvm::DenseSet<uint64_t> aliasedTargets;
    for (const RandomVariableAlias &alias : analysis.aliases) {
      aliasedTargets.insert(variableKey(alias.targetOffset, alias.width));
      aliasedTargets.insert(variableKey(alias.sourceOffset, alias.width));
    }
    llvm::DenseSet<uint64_t> emittedTargets;
    for (const RandomVariableDefinition &definition : analysis.definitions)
      emittedTargets.insert(
          variableKey(definition.targetOffset, definition.width));
    struct SelectedCaptureBounds {
      bool isSigned;
      bool hasLower;
      bool hasUpper;
    };
    llvm::DenseMap<uint64_t, SelectedCaptureBounds> selectedCaptureBounds;
    for (const SMTVariableCaptureBound &bound : smt->directCaptureBounds) {
      RandomCaptureBoundKind kind = convertCaptureBoundKind(bound.kind);
      uint64_t key = variableKey(bound.target.offset, bound.target.width);
      auto domain = domainSignedness.find(key);
      auto selected = selectedCaptureBounds.find(key);
      bool isLower = kind == RandomCaptureBoundKind::LowerInclusive ||
                     kind == RandomCaptureBoundKind::LowerExclusive;
      bool conflicts = overlapsFiniteDomain(bound.target) ||
                       (domain != domainSignedness.end() &&
                        domain->second != bound.isSigned) ||
                       (selected != selectedCaptureBounds.end() &&
                        (selected->second.isSigned != bound.isSigned ||
                         (isLower ? selected->second.hasLower
                                  : selected->second.hasUpper))) ||
                       aliasedTargets.contains(key) ||
                       emittedTargets.contains(key);
      if (conflicts || bound.captureIndex >= smt->captures.size())
        continue;
      std::optional<z3::expr> predicate = shim.translate(bound.predicate);
      if (!predicate || checkWith(!*predicate) != z3::unsat)
        continue;
      analysis.captureBounds.push_back({bound.target.offset, bound.target.width,
                                        bound.captureIndex, kind,
                                        bound.isSigned});
      auto entry = selectedCaptureBounds.try_emplace(
          key, SelectedCaptureBounds{bound.isSigned, false, false});
      if (isLower)
        entry.first->second.hasLower = true;
      else
        entry.first->second.hasUpper = true;
    }

    // The bounds and aliases above already prove hard => proposal. Prove the
    // reverse implication as one final query; exact proposals can bypass both
    // the generated checker and the runtime fallback.
    z3::expr proposal = context.bool_val(true);
    bool translatedProposal = true;
    for (const SMTFiniteDomain &domain : smt->finiteDomains) {
      std::optional<z3::expr> predicate = shim.translate(domain.predicate);
      if (!predicate) {
        translatedProposal = false;
        break;
      }
      proposal = proposal && *predicate;
    }
    for (const RandomVariableDomain &domain : analysis.domains) {
      const SMTVariable *found = findVariable(domain.offset, domain.width);
      if (!found) {
        translatedProposal = false;
        break;
      }
      std::optional<z3::expr> bits = shim.translate(found->bits);
      if (!bits) {
        translatedProposal = false;
        break;
      }
      z3::expr projectedBits =
          domain.isSigned
              ? *bits ^
                    bitVectorValue(context, llvm::APInt::getOneBitSet(
                                                domain.width, domain.width - 1))
              : *bits;
      proposal =
          proposal &&
          z3::uge(projectedBits, bitVectorValue(context, domain.lower)) &&
          z3::ule(projectedBits, bitVectorValue(context, domain.upper));
    }
    for (const RandomVariableCaptureBound &bound : analysis.captureBounds) {
      const SMTVariable *found = findVariable(bound.offset, bound.width);
      if (!found || bound.captureIndex >= smt->captures.size()) {
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
      const SMTVariable *target = findVariable(alias.targetOffset, alias.width);
      const SMTVariable *source = findVariable(alias.sourceOffset, alias.width);
      if (!target || !source) {
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
    for (const SMTVariableDefinition *found : emittedDefinitions) {
      std::optional<z3::expr> targetBits = shim.translate(found->target.bits);
      std::optional<z3::expr> expression = shim.translate(found->expression);
      if (!targetBits || !expression) {
        translatedProposal = false;
        break;
      }
      proposal = proposal && (*targetBits == *expression);
    }
    // All preceding implication queries intentionally run under the effective
    // hard-plus-selected-soft formula.
    // Remove that assertion for the reverse implication: retaining it would
    // make `proposal && !hard` vacuously unsatisfiable.
    solver.pop();
    if (translatedProposal && checkWith(proposal && !effective) == z3::unsat)
      analysis.proposalExact = true;

    // Fully enumerate small capture-free formulas when structural planning was
    // not exact or the client needs the joint conditional distribution. A
    // complete solution table lets generated code sample correlated assignments
    // directly and needs neither a checker nor runtime solving. Keep the table
    // deliberately small to bound both compile time and IR size.
    // Enumeration is capped by both the row limit below and the solver rlimit,
    // so bit-vector width does not need to approximate solution cardinality.
    // Keeping a generous representation cap lets sparse 128/256-bit formulas
    // become tiny exact tables instead of forcing runtime search merely because
    // their legal values are far apart.
    constexpr unsigned maxEnumeratedAssignmentWidth = 4096;
    constexpr size_t maxAssignmentTableSize = 16;
    auto assignmentType =
        mlir::dyn_cast<mlir::smt::BitVectorType>(smt->assignment.getType());
    if ((!analysis.proposalExact || preferGlobalAssignmentTable) &&
        smt->captures.empty() && assignmentType &&
        assignmentType.getWidth() <= maxEnumeratedAssignmentWidth) {
      z3::context enumerationContext;
      Z3SMTShim enumerationShim(enumerationContext);
      std::optional<z3::expr> enumerationHard =
          enumerationShim.translate(smt->hard);
      for (const SMTSoftConstraint &constraint : smt->softConstraints) {
        if (!enumerationHard || !selectedSoft[constraint.priority])
          continue;
        std::optional<z3::expr> preferred =
            enumerationShim.translate(constraint.expression);
        if (!preferred) {
          enumerationHard.reset();
          break;
        }
        *enumerationHard = *enumerationHard && *preferred;
      }
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
        std::vector<llvm::APInt> assignments;
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
          z3::expr evaluated = enumerator.get_model().eval(*assignment, true);
          std::optional<llvm::APInt> value =
              bitVectorNumeral(evaluated, assignmentType.getWidth());
          if (!value)
            break;
          assignments.push_back(*value);
          enumerator.add(*assignment !=
                         bitVectorValue(enumerationContext, *value));
        }
        if (complete && !assignments.empty()) {
          llvm::sort(assignments,
                     [](const llvm::APInt &lhs, const llvm::APInt &rhs) {
                       return lhs.ult(rhs);
                     });
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
      std::vector<std::vector<size_t>> componentNeighbors(
          smt->variables.size());
      for (const SMTHardConstraint &constraint : smt->hardConstraints) {
        if (constraint.dependencies.empty())
          continue;
        size_t first = variableIndex(constraint.dependencies.front());
        if (first == smt->variables.size())
          continue;
        for (const SMTVariable &dependency :
             llvm::ArrayRef(constraint.dependencies).drop_front()) {
          size_t other = variableIndex(dependency);
          if (other != smt->variables.size() && other != first) {
            componentNeighbors[first].push_back(other);
            componentNeighbors[other].push_back(first);
          }
        }
      }

      std::vector<size_t> componentParents =
          connectedComponents(componentNeighbors, smt->variables);
      auto findComponentRoot = [&](size_t index) {
        return componentParents[index];
      };

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

      // Full-width component masks and up to sixteen table rows can expand
      // quadratically for many tiny components. Limit that optional output by
      // the same linear budget as dependency discovery. The complete Boolean
      // sampler still represents every component when tables are skipped.
      uint64_t componentCount =
          llvm::count_if(componentConstraints,
                         [](const auto &items) { return !items.empty(); });
      uint64_t assignmentWords =
          (static_cast<uint64_t>(aggregateWidth) + 63) / 64;
      uint64_t wordsPerComponent =
          assignmentWords * (maxAssignmentTableSize + 2);
      if (componentCount > smt->proposalWorkLimit / wordsPerComponent) {
        analysis.hasConstraintComponentPartition = false;
        return analysis;
      }

      for (size_t root = 0; root != componentVariables.size(); ++root) {
        if (componentConstraints[root].empty())
          continue;
        unsigned assignmentWidth =
            mlir::cast<mlir::smt::BitVectorType>(smt->assignment.getType())
                .getWidth();
        llvm::APInt componentMask = llvm::APInt::getZero(assignmentWidth);
        for (size_t variableNumber : componentVariables[root]) {
          const SMTVariable &variable = smt->variables[variableNumber];
          componentMask.setBits(variable.offset,
                                variable.offset + variable.width);
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
        uint64_t componentWidth = 0;
        unsigned assignmentWidth =
            mlir::cast<mlir::smt::BitVectorType>(smt->assignment.getType())
                .getWidth();
        llvm::APInt componentMask = llvm::APInt::getZero(assignmentWidth);
        for (size_t variableNumber : componentVariables[root]) {
          const SMTVariable &variable = smt->variables[variableNumber];
          componentWidth += variable.width;
          componentMask.setBits(variable.offset,
                                variable.offset + variable.width);
        }
        if (componentWidth > maxEnumeratedAssignmentWidth) {
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
          llvm::APInt assignment = llvm::APInt::getZero(assignmentWidth);
          z3::expr different = enumerationContext.bool_val(false);
          for (auto [variableNumber, bits] :
               llvm::zip_equal(componentVariables[root], componentBits)) {
            const SMTVariable &variable = smt->variables[variableNumber];
            z3::expr evaluated = model.eval(bits, true);
            std::optional<llvm::APInt> value =
                bitVectorNumeral(evaluated, variable.width);
            if (!value) {
              translatedComponent = false;
              break;
            }
            assignment.insertBits(*value, variable.offset);
            different = different ||
                        (bits != bitVectorValue(enumerationContext, *value));
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
        llvm::sort(table.assignments,
                   [](const llvm::APInt &lhs, const llvm::APInt &rhs) {
                     return lhs.ult(rhs);
                   });

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
        for (const llvm::APInt &assignment : table.assignments) {
          z3::expr row = context.bool_val(true);
          for (size_t variableNumber : componentVariables[root]) {
            const SMTVariable &variable = smt->variables[variableNumber];
            std::optional<z3::expr> bits = shim.translate(variable.bits);
            if (!bits) {
              translatedTable = false;
              break;
            }
            llvm::APInt value =
                assignment.extractBits(variable.width, variable.offset);
            row = row && (*bits == bitVectorValue(context, value));
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
        unsigned assignmentWidth =
            mlir::cast<mlir::smt::BitVectorType>(smt->assignment.getType())
                .getWidth();
        llvm::APInt tableMask = llvm::APInt::getZero(assignmentWidth);
        for (const RandomAssignmentTable &table : analysis.assignmentTables)
          tableMask |= table.mask;
        std::vector<uint32_t> tableCoverage(
            static_cast<size_t>(assignmentWidth) + 1);
        for (size_t bit = 0; bit != assignmentWidth; ++bit)
          tableCoverage[bit + 1] = tableCoverage[bit] + tableMask[bit];
        auto overlapsTable = [&](uint32_t offset, uint32_t width) {
          uint64_t end = static_cast<uint64_t>(offset) + width;
          return tableCoverage[end] != tableCoverage[offset];
        };
        llvm::erase_if(analysis.domains, [&](const RandomVariableDomain &item) {
          return overlapsTable(item.offset, item.width);
        });
        llvm::erase_if(analysis.aliases, [&](const RandomVariableAlias &item) {
          return overlapsTable(item.targetOffset, item.width) ||
                 overlapsTable(item.sourceOffset, item.width);
        });
        llvm::erase_if(analysis.definitions,
                       [&](const RandomVariableDefinition &item) {
                         return overlapsTable(item.targetOffset, item.width);
                       });

        bool equivalentCompleteProposal =
            sawConstrainedComponent && allConstrainedComponentsComplete &&
            checkWith(effective && !composedProposal) == z3::unsat &&
            checkWith(composedProposal && !effective) == z3::unsat;
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
