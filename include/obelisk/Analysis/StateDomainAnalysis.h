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

  StateDomainFact get(mlir::Value value) const;
  bool isTwoState(mlir::Value value) const;
  bool isInductivelyTwoState(sim::ComputeResourceKind resource,
                             uint64_t descriptor) const;
  mlir::ArrayRef<InductiveStateRoot> getInductiveRoots() const {
    return inductiveRoots;
  }

private:
  explicit StateDomainAnalysis(
      llvm::DenseMap<mlir::Value, StateDomainFact> facts,
      llvm::SmallVector<InductiveStateRoot> inductiveRoots)
      : facts(std::move(facts)), inductiveRoots(std::move(inductiveRoots)) {}

  llvm::DenseMap<mlir::Value, StateDomainFact> facts;
  llvm::SmallVector<InductiveStateRoot> inductiveRoots;
};

} // namespace obelisk

#endif // OBELISK_ANALYSIS_STATEDOMAINANALYSIS_H
