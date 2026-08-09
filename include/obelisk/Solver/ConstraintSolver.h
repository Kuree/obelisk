//===- ConstraintSolver.h - Compiler-side constraint analysis -*- C++ -*-===//

#ifndef OBELISK_SOLVER_CONSTRAINTSOLVER_H
#define OBELISK_SOLVER_CONSTRAINTSOLVER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "llvm/ADT/APInt.h"

namespace obelisk::solver {

enum class Satisfiability { Unknown, Satisfiable, Unsatisfiable };

/// An inclusive unsigned interval containing every value of one serialized
/// random variable that can participate in a hard-constraint solution.
/// Variables are identified by their bit range in the aggregate assignment.
struct RandomVariableDomain {
  uint32_t offset = 0;
  uint32_t width = 0;
  llvm::APInt lower = llvm::APInt(1, 0);
  llvm::APInt upper = llvm::APInt(1, 0);
};

/// A Z3-verified bound whose value is read from a serialized runtime capture.
/// Generated proposals evaluate the capture once and sample the resulting
/// signed or unsigned interval without consulting a runtime solver. One lower
/// and one upper bound of the same signedness may form an intersected domain.
enum class RandomCaptureBoundKind {
  LowerInclusive,
  LowerExclusive,
  UpperInclusive,
  UpperExclusive,
};

struct RandomVariableCaptureBound {
  uint32_t offset = 0;
  uint32_t width = 0;
  uint32_t captureIndex = 0;
  RandomCaptureBoundKind kind = RandomCaptureBoundKind::UpperInclusive;
  bool isSigned = false;
};

/// A compile-time-proven equality between two serialized random variables.
/// Generated proposals sample `sourceOffset` and copy it to `targetOffset`.
struct RandomVariableAlias {
  uint32_t targetOffset = 0;
  uint32_t sourceOffset = 0;
  uint32_t width = 0;
};

/// A compile-time-proven assignment of one serialized random variable from a
/// contiguous RPN expression in the analyzed runtime program. Instruction
/// indices exclude the program header and `expressionEnd` is exclusive.
struct RandomVariableDefinition {
  uint32_t targetOffset = 0;
  uint32_t width = 0;
  uint32_t expressionBegin = 0;
  uint32_t expressionEnd = 0;
};

/// A complete table for one independent connected component of the hard
/// formula. Assignments use aggregate bit positions and have no bits outside
/// `mask`; variables outside the mask remain independently sampleable.
struct RandomAssignmentTable {
  llvm::APInt mask = llvm::APInt(1, 0);
  std::vector<llvm::APInt> assignments;
};

struct RandomProgramAnalysis {
  Satisfiability satisfiability = Satisfiability::Unknown;
  const char *backend = "heuristic";
  /// A complete, deterministically ordered table of aggregate assignments
  /// satisfying the hard formula. Generated code is responsible for choosing
  /// an unbiased index for arbitrary table cardinalities.
  std::vector<llvm::APInt> assignmentTable;
  /// Complete tables for independent hard-constraint components. Components
  /// are deterministically ordered and their masks never overlap.
  std::vector<RandomAssignmentTable> assignmentTables;
  /// Aggregate masks for the syntactic connected components of the analyzed
  /// formula. Unconstrained variables are omitted and therefore form implicit
  /// singleton components. The partition is conservative: a component may
  /// contain independent variables, but random variables mentioned together
  /// by one effective constraint never appear in separate masks.
  std::vector<llvm::APInt> constraintComponentMasks;
  /// True when `constraintComponentMasks` is a complete dependency partition.
  /// A false value requires clients to conservatively assume that any random
  /// variables may be coupled.
  bool hasConstraintComponentPartition = false;
  std::vector<RandomVariableDomain> domains;
  std::vector<RandomVariableCaptureBound> captureBounds;
  std::vector<RandomVariableAlias> aliases;
  std::vector<RandomVariableDefinition> definitions;
  /// True when every soft priority was resolved into the analyzed formula.
  /// This is currently possible only when runtime captures cannot change the
  /// preferred satisfiable priority set.
  bool softPreferencesResolved = false;
  /// True only when either the global/component assignment tables or the
  /// domain, capture-bound, alias, and definition proposal (with full domains
  /// for omitted variables) is equivalent to the analyzed effective formula.
  bool proposalExact = false;
};

/// Analyze a versioned runtime random-constraint program. Dynamic captures
/// remain free bit-vector variables, so an Unsatisfiable result proves that no
/// runtime capture values can make the hard constraints satisfiable. Reported
/// Capture-free soft constraints are resolved in priority order; when that is
/// possible, the effective formula below means the hard constraints plus the
/// selected satisfiable preferences. Reported variable domains conservatively
/// enclose the projection of all effective solutions, including every possible
/// runtime capture value. Capture bounds, aliases, and definitions are
/// relations implied by every effective solution. Assignment tables contain
/// every effective solution, either globally or independently per capture-free
/// connected component. Capture-dependent components remain on the
/// checker/runtime path unless a structural proposal covers them exactly. The
/// component-mask partition conservatively describes which random variables
/// can affect one another. Setting
/// `preferGlobalAssignmentTable` also attempts bounded global enumeration when
/// a structural proposal is already exact; clients that need the joint
/// conditional distribution, such as `solve before`, can request that stronger
/// result. The API deliberately exposes no Z3 types.
RandomProgramAnalysis
analyzeRandomProgram(const uint8_t *program, size_t programSize,
                     uint64_t resourceLimit = 100000,
                     bool preferGlobalAssignmentTable = false);

} // namespace obelisk::solver

#endif
