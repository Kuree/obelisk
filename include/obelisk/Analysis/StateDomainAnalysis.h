//===- StateDomainAnalysis.h - Whole-value X/Z analysis --------*- C++ -*-===//
//
// A read-only, whole-design analysis of whether normalized simulation values
// can contain SystemVerilog X or Z bits.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_ANALYSIS_STATEDOMAINANALYSIS_H
#define OBELISK_ANALYSIS_STATEDOMAINANALYSIS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/Value.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <utility>

namespace obelisk {

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
  LogicLogical,
  LogicShift,
  LogicCompare,
  LogicConcat,
  LogicReplicate,
  LogicExtract,
  LogicInsert,
  PackedView,
  DynamicExtract,
  DynamicExtractIndex,
  DivisionDivisor,
  CaseComparison,
  AbsorbingConstant,
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
  sim::ComputeResourceKind resource = sim::ComputeResourceKind::Unknown;
  uint64_t descriptor = 0;

  bool operator==(const InductiveStateRoot &other) const {
    return resource == other.resource && descriptor == other.descriptor;
  }
};

llvm::StringRef stringifyStateDomain(StateDomain domain);
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

  StateDomainFact get(mlir::Value value) const;
  bool isTwoState(mlir::Value value) const;
  /// Query the guarded fact used by a versioned native kernel after it has
  /// established that every root in its inductive closure is known.
  StateDomainFact getWithInductiveRoots(mlir::Value value) const;
  bool isTwoStateWithInductiveRoots(mlir::Value value) const;
  bool isInductivelyTwoState(sim::ComputeResourceKind resource,
                             uint64_t descriptor) const;
  mlir::ArrayRef<InductiveStateRoot> getInductiveRoots() const {
    return inductiveRoots;
  }

private:
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
