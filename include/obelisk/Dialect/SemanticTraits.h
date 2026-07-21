//===- SemanticTraits.h - Shared semantic IR invariants ----------*- C++
//-*-===//

#ifndef OBELISK_DIALECT_SEMANTICTRAITS_H
#define OBELISK_DIALECT_SEMANTICTRAITS_H

#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <functional>
#include <utility>

namespace mlir::OpTrait {

/// Marks an elaborated SystemVerilog AST node: an attribute-and-region tree
/// node imported from the frontend. Both semantic dialects also own lowered
/// value operations, so dialect membership alone does not identify the AST.
template <typename ConcreteType>
class SemanticASTNode : public TraitBase<ConcreteType, SemanticASTNode> {};

/// Marks a declarative semantic node family: constraints, assertion and
/// sequence expressions, coverage bins and covergroups, randsequence
/// productions, and class members. These describe properties to be solved,
/// checked, or sampled rather than a procedural statement and expression tree,
/// so a consumer that only executes procedural code must reject them
/// explicitly instead of silently ignoring them. The trait rides on the
/// generated category base classes, so nodes added by a later slang release
/// inherit it without updating any consumer.
template <typename ConcreteType>
class SemanticDeclarativeNode
    : public TraitBase<ConcreteType, SemanticDeclarativeNode> {};

/// Marks an upstream error-recovery sentinel that is inventoried for exhaustive
/// dispatch coverage but can never be persisted as valid semantic IR.
template <typename ConcreteType>
class RejectInvalidSemanticNode
    : public TraitBase<ConcreteType, RejectInvalidSemanticNode> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return op->emitOpError(
        "represents an invalid semantic sentinel and cannot appear in IR");
  }
};

/// Verifies the complete semantic reference graph once at its root. Importer
/// IDs make leaf names globally unique, while accepting a suffix permits normal
/// relative SymbolRefAttr spelling in hand-written tests. This avoids
/// rebuilding symbol tables or walking the full UVM hierarchy for every
/// individual use.
template <typename ConcreteType>
class VerifySemanticReferenceGraph
    : public TraitBase<ConcreteType, VerifySemanticReferenceGraph> {
public:
  static LogicalResult verifyTrait(Operation *root) {
    Operation *container = root;
    while (container->getParentOp())
      container = container->getParentOp();

    llvm::DenseMap<StringAttr, SmallVector<Operation *, 1>> symbolsByLeaf;
    container->walk([&](Operation *candidate) {
      if (auto symbol = dyn_cast<::mlir::SymbolOpInterface>(candidate))
        if (StringAttr name = symbol.getNameAttr())
          symbolsByLeaf[name].push_back(candidate);
    });

    auto getSymbolPath = [](Operation *symbol) {
      SmallVector<StringAttr, 8> path;
      for (Operation *current = symbol; current;
           current = current->getParentOp()) {
        if (auto interface = dyn_cast<::mlir::SymbolOpInterface>(current))
          if (StringAttr name = interface.getNameAttr())
            path.push_back(name);
      }
      std::reverse(path.begin(), path.end());
      return path;
    };

    LogicalResult result = success();
    container->walk([&](Operation *user) {
      SmallVector<std::pair<StringAttr, SymbolRefAttr>, 4> references;
      std::function<void(StringAttr, Attribute)> collectAttributes;
      std::function<void(StringAttr, Type)> collectTypes;
      collectTypes = [&](StringAttr name, Type type) {
        type.walkImmediateSubElements(
            [&](Attribute nested) { collectAttributes(name, nested); },
            [&](Type nested) { collectTypes(name, nested); });
      };
      collectAttributes = [&](StringAttr name, Attribute attribute) {
        if (auto reference = dyn_cast<SymbolRefAttr>(attribute)) {
          references.emplace_back(name, reference);
          return;
        }
        attribute.walkImmediateSubElements(
            [&](Attribute nested) { collectAttributes(name, nested); },
            [&](Type nested) { collectTypes(name, nested); });
      };
      for (NamedAttribute attribute : user->getAttrs())
        collectAttributes(attribute.getName(), attribute.getValue());

      for (const auto &[attributeName, reference] : references) {
        SmallVector<StringAttr, 8> components{reference.getRootReference()};
        for (FlatSymbolRefAttr nested : reference.getNestedReferences())
          components.push_back(nested.getAttr());

        bool resolved = false;
        auto candidates = symbolsByLeaf.find(components.back());
        if (candidates != symbolsByLeaf.end()) {
          // Importer-assigned names include a globally unique traversal ID.
          // Generic specializations can have semantic parents that differ from
          // their ownership nesting, so a unique leaf is authoritative. Keep
          // structural suffix matching for hand-written IR with duplicate
          // human-readable leaf names.
          resolved = candidates->second.size() == 1;
          if (!resolved) {
            for (Operation *candidate : candidates->second) {
              SmallVector<StringAttr, 8> path = getSymbolPath(candidate);
              if (path.size() >= components.size() &&
                  llvm::equal(ArrayRef(path).take_back(components.size()),
                              components)) {
                resolved = true;
                break;
              }
            }
          }
        }
        if (!resolved) {
          user->emitOpError()
              << "cannot resolve " << attributeName << ' ' << reference;
          result = failure();
        }
      }
    });
    return result;
  }
};

inline LogicalResult verifySequenceRange(Operation *op, StringRef label,
                                         bool present, bool unbounded,
                                         bool hasMinimum, bool hasMaximum,
                                         bool hasRequiredMetadata) {
  if (!present) {
    if (hasRequiredMetadata || hasMinimum || hasMaximum || unbounded)
      return op->emitOpError()
             << "has " << label << " metadata even though the range is absent";
    return success();
  }
  if (!hasMinimum || !hasRequiredMetadata)
    return op->emitOpError() << "requires complete metadata for " << label;
  if (unbounded == hasMaximum)
    return op->emitOpError()
           << "requires exactly one of an upper bound or unbounded " << label;
  return success();
}

/// Checks the correlated optional fields used for assertion repetitions.
template <typename ConcreteType>
class VerifyRepetitionMetadata
    : public TraitBase<ConcreteType, VerifyRepetitionMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    return verifySequenceRange(op, "repetition", concrete.getHasRepetition(),
                               concrete.getRepetitionIsUnbounded(),
                               concrete.getRepetitionMin().has_value(),
                               concrete.getRepetitionMax().has_value(),
                               concrete.getRepetitionKind().has_value());
  }
};

/// Checks the correlated optional fields used for assertion sequence ranges.
template <typename ConcreteType>
class VerifySequenceRangeMetadata
    : public TraitBase<ConcreteType, VerifySequenceRangeMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    return verifySequenceRange(
        op, "range", concrete.getHasRange(), concrete.getRangeIsUnbounded(),
        concrete.getRangeMin().has_value(), concrete.getRangeMax().has_value(),
        /*hasRequiredMetadata=*/true);
  }
};

} // namespace mlir::OpTrait

#endif // OBELISK_DIALECT_SEMANTICTRAITS_H
