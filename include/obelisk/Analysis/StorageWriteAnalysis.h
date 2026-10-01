//===- StorageWriteAnalysis.h - Storage writes --------------------*- C++ -*-===//
#ifndef OBELISK_ANALYSIS_STORAGEWRITEANALYSIS_H
#define OBELISK_ANALYSIS_STORAGEWRITEANALYSIS_H

#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>

namespace obelisk::analysis {

/// Storage-view projection of the shared SSA handle lattice. A known root can
/// have a contiguous or strided lane, or an unknown selection within that root.
/// Root identity alone does not establish that different writes cannot overlap.
struct StorageViewFact {
  enum class Kind { Unknown, Root, Lane } kind = Kind::Unknown;
  uint64_t descriptor = 0, rootWidth = 0;
  uint64_t low = 0, width = 0, stride = 0;
  bool dynamic = false, clipped = false;

  bool hasRoot() const { return kind == Kind::Root || kind == Kind::Lane; }
  bool hasLane() const { return kind == Kind::Lane; }
  void print(llvm::raw_ostream &os) const;
};

/// May-execute reachability before a cold checkpoint returns to the runtime.
/// Dense CFG/region joins retain any hot incoming path. A checkpoint block
/// kills that path; its successors become hot only through another hot edge.
class NativeHotPathReachability {
public:
  explicit NativeHotPathReachability(sim::SimFuncOp function);
  bool canExecute(mlir::Operation *operation) const;

private:
  llvm::DenseSet<mlir::Operation *> hot;
  bool valid = false;
};

/// Dense execution bounds for explicitly tracked statements. Intraprocedural
/// counts are independent of a statement's opcode, so source planning and
/// post-conversion verification consume the same lattice implementation.
class WriteExecutionBounds {
public:
  WriteExecutionBounds(mlir::Operation *scope,
                       mlir::ArrayRef<mlir::Operation *> tracked,
                       bool unknownCallEffects = true,
                       bool resetAtPositiveDelay = true);
  bool executesAtMostOnce(mlir::Operation *write) const;
  bool mutuallyExclusive(mlir::Operation *lhs, mlir::Operation *rhs) const;

private:
  llvm::DenseMap<mlir::Operation *, unsigned> indices;
  llvm::SmallVector<uint8_t> bounds;
  llvm::SmallVector<llvm::BitVector> preceding;
  bool valid = false;
};

/// Statements that must have executed since the most recent barrier on every
/// incoming path. Joins intersect definitions; barriers clear them. This is
/// distinct from NBA-window counts: even a #0 suspension ends an activation.
class MustDefinitionAnalysis {
public:
  MustDefinitionAnalysis(sim::SimFuncOp function,
                         mlir::ArrayRef<mlir::Operation *> definitions,
                         llvm::function_ref<bool(mlir::Operation *)> isBarrier);
  bool containsBefore(mlir::Operation *definition, mlir::Operation *use) const;

private:
  llvm::DenseMap<mlir::Operation *, unsigned> indices;
  llvm::DenseMap<mlir::Operation *, llvm::BitVector> before;
};

/// Must remain before the first barrier on every incoming execution path.
class NoBarrierAnalysis {
public:
  NoBarrierAnalysis(sim::SimFuncOp function,
                    llvm::function_ref<bool(mlir::Operation *)> isBarrier);
  bool isSafeBefore(mlir::Operation *op) const { return safe.contains(op); }

private:
  llvm::DenseSet<mlir::Operation *> safe;
};

/// Combined SSA storage-view and dense forward write-execution analysis.
/// The dense product tracks per-statement {0, 1, many} counts to a fixed point
/// through CFG and structured regions. Joins use max; a strictly positive
/// delay starts a fresh NBA window. #0 and event waits do not (LRM 4.4.2.4).
/// Missing states and unsupported transfers never establish a certificate.
/// This analysis describes execution and aliasing, not observer merge safety
/// or which widths a particular lowering implements.
class StorageWriteAnalysis {
public:
  StorageWriteAnalysis(sim::SimFuncOp function,
                       const HandleDataflowAnalysis &analysis);
  StorageWriteAnalysis(sim::SimFuncOp function, HandleDataflowResult handles,
                       bool trackExecution = true);
  const HandleDataflowResult &getHandles() const { return handles; }
  StorageViewFact lookup(mlir::Value reference) const;
  bool executesAtMostOnce(mlir::Operation *write) const;
  bool mutuallyExclusive(mlir::Operation *lhs, mlir::Operation *rhs) const;
  /// The direct packed lowering currently implements one dynamic selection
  /// from a whole root. Root identity comes from dataflow, including CFG args.
  bool hasDirectDynamicSelection(mlir::Value reference) const;

private:
  HandleDataflowResult handles;
  std::unique_ptr<WriteExecutionBounds> execution;
};
} // namespace obelisk::analysis
#endif
