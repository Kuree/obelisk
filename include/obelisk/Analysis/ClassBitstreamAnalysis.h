//===- ClassBitstreamAnalysis.h - Object bit-stream schemas -*- C++ -*-===//

#ifndef OBELISK_ANALYSIS_CLASSBITSTREAMANALYSIS_H
#define OBELISK_ANALYSIS_CLASSBITSTREAMANALYSIS_H

#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Analysis/ManagedClassLayoutAnalysis.h"

#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"

#include <array>
#include <memory>

namespace llvm {
class DataLayout;
} // namespace llvm

namespace obelisk::analysis {

/// Closed-world, target-dependent schemas used by explicit class bit-stream
/// casts. A schema describes one exact concrete runtime class. Cast sites keep
/// a separate root inventory because the 6.24.3 `this` exception applies only
/// to that root object and never to class handles reached through its fields.
class ClassBitstreamAnalysis {
public:
  struct Schema {
    const ManagedClassLayoutAnalysis::Class *layout = nullptr;
    mlir::SmallVector<const ManagedClassLayoutAnalysis::Field *> fields;
    mlir::SmallVector<sim::ClassHandleType> nestedStaticTypes;
    sim::SimClassFieldDeclOp invalidField;
    bool hasHiddenField = false;
  };

  struct CastClosure {
    /// Exact dynamic schemas permitted for the source handle at the cast site.
    mlir::SmallVector<const Schema *> roots;
    /// Roots plus every public-only schema reachable through their fields.
    /// Entries are unique and ordered by class ID.
    mlir::SmallVector<const Schema *> schemas;
  };

  static mlir::FailureOr<ClassBitstreamAnalysis>
  compute(sim::SimDesignOp design, const llvm::DataLayout &dataLayout);

  /// Build the finite schema closure for one explicit bit-stream cast.
  /// `allowHiddenRoot` is true only when the source expression is exactly the
  /// current-instance `this`. Nested object handles are always public-only.
  mlir::FailureOr<const CastClosure *>
  getCastClosure(sim::ClassHandleType source, bool allowHiddenRoot) const;

  const Schema *lookup(uint64_t classID) const;

private:
  ClassBitstreamAnalysis(ClassDispatchAnalysis dispatch,
                         std::unique_ptr<ManagedClassLayoutAnalysis> layouts)
      : dispatch(std::move(dispatch)), layouts(std::move(layouts)) {}

  ClassDispatchAnalysis dispatch;
  // Schema field pointers remain stable when the analysis result is moved.
  std::unique_ptr<ManagedClassLayoutAnalysis> layouts;
  // Keep schemas heap-stable as well: successful cached closures contain
  // schema pointers and the analysis itself remains movable.
  mlir::SmallVector<std::unique_ptr<Schema>> schemas;
  llvm::DenseMap<uint64_t, unsigned> schemaIndices;
  // Each distinct static source and root-visibility mode owns one dispatch
  // group. Heap ownership keeps returned group pointers stable across map
  // growth and analysis moves.
  mutable llvm::DenseMap<mlir::Type,
                         std::array<std::unique_ptr<CastClosure>, 2>>
      closureCache;
};

} // namespace obelisk::analysis

#endif // OBELISK_ANALYSIS_CLASSBITSTREAMANALYSIS_H
