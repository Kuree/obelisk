//===- SemanticTypeIndex.h - Frozen source type identities ------*- C++ -*-===//

#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_SEMANTICTYPEINDEX_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_SEMANTICTYPEINDEX_H

#include "mlir/IR/Location.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Types.h"
#include "llvm/ADT/DenseMap.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace talos::simlowering {

// Build once per module before workers run. Entries contain immutable MLIR
// values, so lowering never reads another worker's mutable operation
// attributes.
class SemanticTypeIndex {
public:
  struct Mailbox {
    mlir::Type element;
    mlir::Type normalized;
    mlir::Location location;
    uint64_t specialization;
  };
  explicit SemanticTypeIndex(mlir::Operation *module);
  const Mailbox *lookupMailbox(mlir::Type type) const;
  std::optional<mlir::Type> lookupNormalizedType(mlir::Type type,
                                                 bool allowRealScalar) const;

private:
  llvm::DenseMap<mlir::Type, Mailbox> mailboxes;
  llvm::DenseMap<std::pair<mlir::Type, unsigned>, mlir::Type> normalizedTypes;
};

class ScopedSemanticTypeIndex {
public:
  explicit ScopedSemanticTypeIndex(const SemanticTypeIndex &index);
  ~ScopedSemanticTypeIndex();
  ScopedSemanticTypeIndex(const ScopedSemanticTypeIndex &) = delete;
  ScopedSemanticTypeIndex &operator=(const ScopedSemanticTypeIndex &) = delete;
  static const SemanticTypeIndex::Mailbox *lookupMailbox(mlir::Type type);
  static std::optional<mlir::Type> lookupNormalizedType(mlir::Type type,
                                                        bool allowRealScalar);

private:
  const SemanticTypeIndex *previous;
  static thread_local const SemanticTypeIndex *current;
};

} // namespace talos::simlowering
#endif
