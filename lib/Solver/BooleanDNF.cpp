//===- BooleanDNF.cpp - Compile-time propositional minimization -----------===//

#include "obelisk/Solver/ConstraintSolver.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <limits>
#ifdef OBELISK_Z3_SINGLE_THREADED
#include <mutex>
#endif
#include <numeric>
#include <optional>
#include <string>
#include <utility>

#ifdef OBELISK_ENABLE_Z3
#include "Z3Support.h"
#include <z3++.h>
#endif

namespace obelisk::solver {
namespace {

static bool literalLess(const BooleanLiteral &lhs, const BooleanLiteral &rhs) {
  if (lhs.variable != rhs.variable)
    return lhs.variable < rhs.variable;
  return lhs.negated < rhs.negated;
}

static bool normalizeCube(BooleanCube &cube) {
  llvm::sort(cube, literalLess);
  BooleanCube normalized;
  normalized.reserve(cube.size());
  for (BooleanLiteral literal : cube) {
    if (!normalized.empty() && normalized.back().variable == literal.variable) {
      if (normalized.back().negated != literal.negated)
        return false;
      continue;
    }
    normalized.push_back(literal);
  }
  cube = std::move(normalized);
  return true;
}

static bool cubeSubsumes(const BooleanCube &lhs, const BooleanCube &rhs) {
  return std::includes(rhs.begin(), rhs.end(), lhs.begin(), lhs.end(),
                       literalLess);
}

static std::vector<BooleanCube> normalizeDNF(std::vector<BooleanCube> cubes) {
  llvm::erase_if(cubes, [](BooleanCube &cube) { return !normalizeCube(cube); });
  std::vector<uint8_t> redundant(cubes.size(), false);
  for (size_t candidate = 0; candidate < cubes.size(); ++candidate) {
    for (size_t covering = 0; covering < cubes.size(); ++covering) {
      if (candidate == covering || redundant[covering])
        continue;
      if (!cubeSubsumes(cubes[covering], cubes[candidate]))
        continue;
      if (cubes[covering] == cubes[candidate] && covering > candidate)
        continue;
      redundant[candidate] = true;
      break;
    }
  }
  std::vector<BooleanCube> result;
  result.reserve(cubes.size());
  for (auto [index, cube] : llvm::enumerate(cubes))
    if (!redundant[index])
      result.push_back(std::move(cube));
  return result;
}

} // namespace

BooleanDNFAnalysis minimizeBooleanDNF(std::vector<BooleanCube> cubes,
                                      uint64_t resourceLimit,
                                      uint64_t queryLimit) {
  BooleanDNFAnalysis analysis;
  analysis.cubes = normalizeDNF(std::move(cubes));

#ifdef OBELISK_ENABLE_Z3
  analysis.backend = "z3";
  if (analysis.cubes.empty() || resourceLimit == 0 || queryLimit == 0)
    return analysis;

#ifdef OBELISK_Z3_SINGLE_THREADED
  // The non-pthread wasm compiler uses Z3's single-threaded build.
  std::lock_guard<std::mutex> z3Lock(detail::getZ3Mutex());
#endif
  try {
#ifdef OBELISK_Z3_SINGLE_THREADED
    static z3::context context;
#else
    // Z3 contexts are isolated rather than shared across native MLIR workers.
    // Reuse one per worker thread because constructing a context is larger than
    // these tiny propositional queries.
    static thread_local z3::context context;
#endif
    uint32_t maximumVariable = 0;
    for (const BooleanCube &cube : analysis.cubes)
      for (BooleanLiteral literal : cube)
        maximumVariable = std::max(maximumVariable, literal.variable);
    std::vector<z3::expr> variables;
    variables.reserve(static_cast<size_t>(maximumVariable) + 1);
    for (uint64_t variable = 0; variable <= maximumVariable; ++variable)
      variables.push_back(
          context.bool_const(("sva_" + std::to_string(variable)).c_str()));

    auto cubeExpression = [&](const BooleanCube &cube) {
      z3::expr expression = context.bool_val(true);
      for (BooleanLiteral literal : cube)
        expression =
            expression && (literal.negated ? !variables[literal.variable]
                                           : variables[literal.variable]);
      return expression;
    };
    auto dnfExpression = [&](const std::vector<BooleanCube> &formula,
                             const std::vector<uint8_t> &active,
                             std::optional<size_t> excluded = std::nullopt) {
      z3::expr expression = context.bool_val(false);
      for (size_t index = 0; index < formula.size(); ++index)
        if (active[index] && excluded != index)
          expression = expression || cubeExpression(formula[index]);
      return expression;
    };

    z3::solver solver(context);
    z3::params parameters(context);
    parameters.set("rlimit",
                   static_cast<unsigned>(std::min<uint64_t>(
                       resourceLimit, std::numeric_limits<unsigned>::max())));
    solver.set(parameters);
    auto provesUnsatisfiable = [&](const z3::expr &expression) {
      if (analysis.solverQueries >= queryLimit)
        return false;
      ++analysis.solverQueries;
      solver.push();
      solver.add(expression);
      z3::check_result result = solver.check();
      solver.pop();
      return result == z3::unsat;
    };

    std::vector<BooleanCube> original = analysis.cubes;
    std::vector<uint8_t> originalActive(original.size(), true);
    z3::expr originalExpression = dnfExpression(original, originalActive);
    std::vector<uint8_t> active(analysis.cubes.size(), true);

    auto removeRedundantCubes = [&]() {
      for (size_t index = analysis.cubes.size(); index-- > 0;) {
        if (!active[index])
          continue;
        z3::expr uncovered = cubeExpression(analysis.cubes[index]) &&
                             !dnfExpression(analysis.cubes, active, index);
        if (provesUnsatisfiable(uncovered))
          active[index] = false;
      }
    };
    removeRedundantCubes();

    for (size_t cubeIndex = 0; cubeIndex < analysis.cubes.size(); ++cubeIndex) {
      if (!active[cubeIndex])
        continue;
      BooleanCube &cube = analysis.cubes[cubeIndex];
      for (size_t literalIndex = 0; literalIndex < cube.size();) {
        BooleanCube candidate = cube;
        candidate.erase(candidate.begin() + literalIndex);
        z3::expr broadened = context.bool_val(false);
        for (size_t index = 0; index < analysis.cubes.size(); ++index) {
          if (!active[index])
            continue;
          broadened = broadened || cubeExpression(index == cubeIndex
                                                      ? candidate
                                                      : analysis.cubes[index]);
        }
        if (provesUnsatisfiable(broadened && !originalExpression)) {
          cube = std::move(candidate);
          continue;
        }
        ++literalIndex;
      }
    }
    removeRedundantCubes();

    std::vector<BooleanCube> minimized;
    minimized.reserve(analysis.cubes.size());
    for (size_t index = 0; index < analysis.cubes.size(); ++index)
      if (active[index])
        minimized.push_back(std::move(analysis.cubes[index]));
    analysis.cubes = normalizeDNF(std::move(minimized));
  } catch (...) {
    // Every accepted edit above was guarded by an UNSAT proof. A solver or
    // allocation failure therefore leaves an exactly equivalent, possibly
    // less-minimal formula.
  }
#else
  (void)resourceLimit;
  (void)queryLimit;
#endif
  return analysis;
}

} // namespace obelisk::solver
