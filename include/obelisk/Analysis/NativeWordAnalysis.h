//===- NativeWordAnalysis.h - Independent packed lane dataflow ----*- C++
//-*-===//
#ifndef OBELISK_ANALYSIS_NATIVEWORDANALYSIS_H
#define OBELISK_ANALYSIS_NATIVEWORDANALYSIS_H
#include "obelisk/Analysis/BoundedLoopAnalysis.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>
namespace obelisk::sim {
class SimFuncOp;
}
namespace obelisk::analysis {
/// Dependence on one independently bounded unit-stride induction variable.
/// Index/ReverseIndex mean base +/- induction without integer wrap; Reference
/// selects consecutive storage slots in either direction. Logic is a
/// lane-wise two- or four-state computation, including packed wrappers. Uniform
/// is invariant across this lane dimension, not necessarily constant.
enum class WordLaneKind {
  Unknown,
  Uniform,
  Index,
  ReverseIndex,
  Reference,
  ReverseReference,
  Logic
};

/// Exact scalar truth tables, indexed by each operand's value bit followed by
/// its unknown bit (the latter exists only for logic<1>). Constant operands
/// are specialized before enumeration. No row may have undefined semantics.
struct WordTruthTable {
  llvm::SmallVector<unsigned> operands;
  llvm::APInt value = llvm::APInt(1, 0);
  llvm::APInt unknown = llvm::APInt(1, 0);
};
bool isWordLaneType(mlir::Type type);
bool isWordLaneComputation(mlir::Operation *op);
bool isWordBitwiseComputation(mlir::Operation *op);
bool isWordReferenceIdentity(mlir::Operation *op);
/// Physical bits between adjacent selected lanes, including array padding.
uint64_t getWordLaneStorageWidth(mlir::Value value);
std::optional<WordTruthTable> computeWordTruthTable(mlir::Operation *op);
class NativeWordAnalysis {
public:
  NativeWordAnalysis(sim::SimFuncOp function, const BoundedLoop &loop,
                     unsigned elementWidth = 1);
  WordLaneKind get(mlir::Value value) const {
    auto found = kinds.find(value);
    return found == kinds.end() ? WordLaneKind::Unknown : found->second;
  }
  /// Different outer iterations cannot address the same element, even when
  /// inner induction variables change the reference between repeated stores.
  bool hasDisjointIterations(mlir::Value reference) const {
    return disjointIterations.contains(reference);
  }

private:
  llvm::DenseMap<mlir::Value, WordLaneKind> kinds;
  llvm::DenseSet<mlir::Value> disjointIterations;
};
} // namespace obelisk::analysis
#endif
