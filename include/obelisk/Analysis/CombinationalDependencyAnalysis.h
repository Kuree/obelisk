//===- CombinationalDependencyAnalysis.h - Statement dependencies ===//
#ifndef OBELISK_ANALYSIS_COMBINATIONALDEPENDENCYANALYSIS_H
#define OBELISK_ANALYSIS_COMBINATIONALDEPENDENCYANALYSIS_H

#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/SmallVector.h"

namespace obelisk::analysis {

/// Revision-local, whole-program memory and statement-order dependencies.
/// Direct-call effects are solved by MLIR dataflow with actual/formal range
/// substitution. Recursive/unresolved effects remain opaque. Suspension edges
/// are phase boundaries, not combinational feedback. Every immediate memory
/// access is a separate node: a process that produces an address and consumes
/// the resulting data need not be a cyclic scheduling node.
///
/// This is a may-dependence analysis, NOT an execute-once certificate. In
/// particular, acyclicity does not prove activation eligibility, complete
/// assignment, quiescent inputs, or absence of intermediate observers. A
/// materializer must establish those properties and retain source-order edges.
/// Configuration-dependent edges must not be removed using sampled values;
/// they require a separately verified guard and invalidation protocol.
class CombinationalDependencyAnalysis {
public:
  enum class AccessKind { Read, Write, NBA, Watch };
  struct Access {
    AccessKind kind;
    HandleFact target;
    bool operator==(const Access &other) const {
      return kind == other.kind && target == other.target;
    }
  };
  struct Summary {
    llvm::SmallVector<Access> accesses;
    bool opaque = false;
  };
  struct Node {
    mlir::Operation *operation;
    sim::SimFuncOp owner;
    llvm::SmallVector<Access> accesses;
    bool opaque = false;
  };
  enum class EdgeKind { SourceOrder, Memory };
  struct Edge {
    unsigned source, target;
    EdgeKind kind;
    bool operator==(const Edge &other) const {
      return source == other.source && target == other.target &&
             kind == other.kind;
    }
  };
  struct Component {
    llvm::SmallVector<unsigned> nodes;
    bool cyclic = false;
  };

  explicit CombinationalDependencyAnalysis(sim::SimDesignOp design);
  const Summary *lookup(sim::SimFuncOp function) const;
  llvm::ArrayRef<Node> getNodes() const { return nodes; }
  llvm::ArrayRef<Edge> getEdges() const { return edges; }
  /// SCC condensation in producer-before-consumer order, with stable IDs.
  llvm::ArrayRef<Component> getComponents() const { return components; }
  /// A source-order-preserving interleaving of straight-line activations.
  /// Returns none for cycles, opaque effects, or non-straight-line bodies.
  /// This only supplies an order; activation/observation proofs remain with
  /// the caller. No operation is cloned or moved by this query.
  std::optional<llvm::SmallVector<mlir::Operation *>>
  getInterleaving(llvm::ArrayRef<sim::SimFuncOp> functions) const;
  /// Reads/watches by non-combinational processes, including clocked ordinary
  /// reads whose event controls watch a different root. NBA targets are kept
  /// in summaries but never become same-phase producers.
  llvm::ArrayRef<Node> getObservers() const { return observers; }
  bool hasUnknownEffects() const { return unknownEffects; }

private:
  llvm::DenseMap<mlir::Operation *, Summary> summaries;
  llvm::SmallVector<Node> nodes, observers;
  llvm::SmallVector<Edge> edges;
  llvm::SmallVector<Component> components;
  bool unknownEffects = true;
};

} // namespace obelisk::analysis
#endif
