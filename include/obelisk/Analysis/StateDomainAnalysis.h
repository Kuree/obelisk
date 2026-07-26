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

llvm::StringRef stringifyStateDomain(StateDomain domain);
llvm::StringRef stringifyStateDomainReason(StateDomainReason reason);

/// A read-only snapshot of state-domain facts for one simulation design.
///
/// The analyzed design and every queried value must remain alive and unchanged
/// for the lifetime of this object. Recompute the analysis after mutating IR.
class StateDomainAnalysis {
public:
  static mlir::FailureOr<StateDomainAnalysis> compute(sim::SimDesignOp design);

  StateDomainFact get(mlir::Value value) const;
  bool isTwoState(mlir::Value value) const;

private:
  explicit StateDomainAnalysis(
      llvm::DenseMap<mlir::Value, StateDomainFact> facts)
      : facts(std::move(facts)) {}

  llvm::DenseMap<mlir::Value, StateDomainFact> facts;
};

} // namespace obelisk

#endif // OBELISK_ANALYSIS_STATEDOMAINANALYSIS_H
