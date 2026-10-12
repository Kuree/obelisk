//===- StateDomainAnalysis.h - Whole-value X/Z analysis --------*- C++ -*-===//
//
// A read-only, whole-design analysis of whether normalized simulation values
// can contain SystemVerilog X or Z bits.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_STATEDOMAINANALYSIS_H
#define OBELISK_ANALYSIS_STATEDOMAINANALYSIS_H

#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Simulation/SimulationEnums.h"

#include "mlir/IR/Types.h"
#include "mlir/IR/Value.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <optional>
#include <utility>

namespace obelisk::sim {
class SimDesignOp;
}

namespace obelisk {

struct ValueFactProgram;

/// The lattice used while solving the design. Bottom is kept internal to the
/// fixed point; unresolved facts are exposed conservatively as MayFourState.
enum class StateDomain {
  Bottom,
  TwoState,
  MayFourState,
};

/// The transfer or boundary rule that most directly established a fact.
enum class StateDomainReason {
  Unresolved,
  NonLogic,
  FunctionEntry,
  CallActual,
  SpawnActual,
  CFGJoin,
  InfeasibleCFG,
  Continuation,
  LogicConstant,
  UnknownConstant,
  LogicFromBits,
  LogicResize,
  LogicUnary,
  LogicReduction,
  LogicBinary,
  LogicMux,
  Select,
  LogicLogical,
  LogicShift,
  LogicCompare,
  LogicConcat,
  LogicReplicate,
  LogicExtract,
  LogicInsert,
  PackedView,
  Aggregate,
  DynamicExtract,
  DynamicExtractIndex,
  DivisionDivisor,
  CaseComparison,
  AbsorbingConstant,
  PartialKnownBits,
  InductiveRootRead,
  RefLoad,
  NetRead,
  CallResult,
  UnknownCall,
  ExternalDeclaration,
  UnsupportedProducer,
};

struct StateDomainFact {
  StateDomain domain = StateDomain::Bottom;
  StateDomainReason reason = StateDomainReason::Unresolved;
};

/// One canonical design-state root whose known-state invariant is inductive.
/// The root may begin as X/Z; clients must guard its unknown plane before
/// specializing a kernel, and must invalidate that specialization across
/// external mutation.
struct InductiveStateRoot {
  schedule::ComputeResourceKind resource =
      schedule::ComputeResourceKind::Unknown;
  uint64_t descriptor = 0;

  bool operator==(const InductiveStateRoot &other) const {
    return resource == other.resource && descriptor == other.descriptor;
  }
};

llvm::StringRef stringifyStateDomain(StateDomain domain);

/// The IEEE 1800-2023 value-domain transfer of one non-call operation, shared
/// by the whole-design solve and by function-local clients. `operandFact`
/// supplies the current fact of a logic operand. Loads and net reads are
/// four-state unless `isKnownRead` certifies the value they observe. Dynamic
/// selections (`logic.dyn_extract`, `array.extract_dynamic`) are four-state
/// unless `isInRangeSelection` proves the whole selection valid. The result
/// applies to every tracked result of `op`.
StateDomainFact transferStateDomain(
    mlir::Operation *op,
    llvm::function_ref<StateDomainFact(mlir::Value)> operandFact,
    llvm::function_ref<bool(mlir::Operation *)> isKnownRead,
    llvm::function_ref<bool(mlir::Operation *)> isInRangeSelection);

/// Whether a value of `type` carries an unknown plane: a four-state scalar or
/// an aggregate with a four-state member. Handles name storage and carry none.
bool hasUnknownPlane(mlir::Type type);

/// Whether a dynamic selection has a constant index selecting only bits or
/// elements inside its input.
bool isConstantInRangeSelection(mlir::Operation *op);

/// The inclusive [low, high] index range of a fixed packed or unpacked array.
std::optional<std::pair<int64_t, int64_t>>
getArraySelectionBounds(mlir::Type arrayType);

/// The most defined functions a design may have for native compilation to
/// run optional whole-design state-domain solves on it: 4096 at O3, unbounded
/// below, or `obelisk.native.max_state_domain_functions` on `module`. Fails
/// with an error for a negative limit.
mlir::FailureOr<uint64_t>
getNativeStateDomainFunctionLimit(mlir::Operation *module);
llvm::StringRef stringifyStateDomainReason(StateDomainReason reason);

/// A read-only snapshot of state-domain facts for one simulation design.
///
/// The analyzed design and every queried value must remain alive and unchanged
/// for the lifetime of this object. Recompute the analysis after mutating IR.
class StateDomainAnalysis {
public:
  /// Compute unconditional SSA facts and, when requested, the more expensive
  /// guarded fixed point over canonical storage roots. Value-only lowering
  /// clients should pass false; kernel-versioning and diagnostics retain the
  /// default complete proof.
  static mlir::FailureOr<StateDomainAnalysis>
  compute(sim::SimDesignOp design, bool proveInductiveRoots = true);

  /// Compute only the guarded fixed point and inductive-root inventory. This
  /// avoids the separate unconditional solve for clients which exclusively
  /// query getWithInductiveRoots() and isInductivelyTwoState(). Calls to get()
  /// on the returned snapshot conservatively report unresolved logic values.
  static mlir::FailureOr<StateDomainAnalysis>
  computeInductiveOnly(sim::SimDesignOp design);

  /// Compute conditional facts under the boundary assumption that every
  /// canonical logic storage/net root is currently known.  Unlike compute(),
  /// this does not claim that those roots are globally inductive: clients must
  /// separately prove that the selected kernel writes only known values and
  /// must invalidate the specialization across asynchronous mutation.
  static mlir::FailureOr<StateDomainAnalysis>
  computeAssumingKnownState(sim::SimDesignOp design);

  /// Share immutable CFG/call/provenance indexes and the unconditional solve
  /// between kernel-versioning proofs. The second snapshot exposes only
  /// guarded all-known-state facts; get() remains conservative on that one.
  static mlir::FailureOr<std::pair<StateDomainAnalysis, StateDomainAnalysis>>
  computeForSpecialization(sim::SimDesignOp design,
                           bool proveInductiveRoots = true);

  StateDomainFact get(mlir::Value value) const;
  bool isTwoState(mlir::Value value) const;
  /// Query the guarded fact used by a versioned native kernel after it has
  /// established that every root in its inductive closure is known.
  StateDomainFact getWithInductiveRoots(mlir::Value value) const;
  bool isTwoStateWithInductiveRoots(mlir::Value value) const;
  bool isInductivelyTwoState(schedule::ComputeResourceKind resource,
                             uint64_t descriptor) const;
  mlir::ArrayRef<InductiveStateRoot> getInductiveRoots() const {
    return inductiveRoots;
  }

private:
  static mlir::FailureOr<StateDomainAnalysis>
  computeInductiveOnly(const ValueFactProgram &program);
  explicit StateDomainAnalysis(
      llvm::DenseMap<mlir::Value, StateDomainFact> facts,
      llvm::DenseMap<mlir::Value, StateDomainFact> guardedFacts,
      llvm::SmallVector<InductiveStateRoot> inductiveRoots)
      : facts(std::move(facts)), guardedFacts(std::move(guardedFacts)),
        inductiveRoots(std::move(inductiveRoots)) {}

  llvm::DenseMap<mlir::Value, StateDomainFact> facts;
  llvm::DenseMap<mlir::Value, StateDomainFact> guardedFacts;
  llvm::SmallVector<InductiveStateRoot> inductiveRoots;
};

} // namespace obelisk

#endif // OBELISK_ANALYSIS_STATEDOMAINANALYSIS_H
