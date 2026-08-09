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

        Operation *resolvedSymbol = nullptr;
        auto candidates = symbolsByLeaf.find(components.back());
        if (candidates != symbolsByLeaf.end()) {
          // Importer-assigned names include a globally unique traversal ID.
          // Generic specializations can have semantic parents that differ from
          // their ownership nesting, so a unique leaf is authoritative. Keep
          // structural suffix matching for hand-written IR with duplicate
          // human-readable leaf names.
          if (candidates->second.size() == 1) {
            resolvedSymbol = candidates->second.front();
          } else {
            for (Operation *candidate : candidates->second) {
              SmallVector<StringAttr, 8> path = getSymbolPath(candidate);
              if (path.size() >= components.size() &&
                  llvm::equal(ArrayRef(path).take_back(components.size()),
                              components)) {
                if (resolvedSymbol) {
                  resolvedSymbol = nullptr;
                  break;
                }
                resolvedSymbol = candidate;
              }
            }
          }
        }
        if (!resolvedSymbol) {
          user->emitOpError()
              << "cannot resolve " << attributeName << ' ' << reference;
          result = failure();
          continue;
        }

        // Pattern references carry stronger invariants than a generic
        // SymbolRefAttr. Check them here, where the semantic graph has already
        // been indexed, instead of making every pattern verifier walk the
        // enclosing module independently.
        if (attributeName.getValue() != "referenced_symbol")
          continue;
        StringRef userName = user->getName().getStringRef();
        StringRef symbolName = resolvedSymbol->getName().getStringRef();
        if (userName == "slang.pattern.variable" ||
            userName == "obelisk.sv.pattern.variable") {
          StringRef expected =
              userName.starts_with("slang.") ? "slang.symbol.pattern_var"
                                              : "obelisk.sv.symbol.pattern_var";
          if (symbolName != expected) {
            user->emitOpError("referenced pattern variable does not resolve "
                              "to a pattern binding");
            result = failure();
          }
          continue;
        }
        if (userName != "slang.pattern.tagged" &&
            userName != "obelisk.sv.pattern.tagged")
          continue;

        StringRef expected =
            userName.starts_with("slang.") ? "slang.symbol.field"
                                            : "obelisk.sv.symbol.field";
        if (symbolName != expected) {
          user->emitOpError("referenced tagged member does not resolve to an "
                            "aggregate field");
          result = failure();
          continue;
        }
        auto ordinal = user->getAttrOfType<IntegerAttr>("field_ordinal");
        auto offset = user->getAttrOfType<IntegerAttr>("packed_offset");
        auto targetOrdinal =
            resolvedSymbol->getAttrOfType<IntegerAttr>("field_index");
        auto targetOffset =
            resolvedSymbol->getAttrOfType<IntegerAttr>("bit_offset");
        if (!ordinal || !offset || !targetOrdinal || !targetOffset ||
            ordinal.getValue() != targetOrdinal.getValue() ||
            offset.getValue() != targetOffset.getValue()) {
          user->emitOpError("tagged pattern field metadata does not match its "
                            "referenced member");
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

/// Checks the parallel arrays that describe a sequence or property signature.
template <typename ConcreteType>
class VerifyAssertionDeclarationMetadata
    : public TraitBase<ConcreteType, VerifyAssertionDeclarationMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    size_t count = concrete.getPortCount();
    if (concrete.getPortSymbols().size() != count ||
        concrete.getPortPaths().size() != count)
      return op->emitOpError()
             << "requires port metadata arrays to match port_count";
    return success();
  }
};

/// Checks the parallel arrays that describe an expanded assertion invocation.
template <typename ConcreteType>
class VerifyAssertionInvocationMetadata
    : public TraitBase<ConcreteType, VerifyAssertionInvocationMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    size_t argumentCount = concrete.getArgumentCount();
    if (concrete.getArgumentFormalSymbols().size() != argumentCount ||
        concrete.getArgumentFormalPaths().size() != argumentCount ||
        concrete.getArgumentKinds().size() != argumentCount)
      return op->emitOpError()
             << "requires argument metadata arrays to match argument_count";

    size_t localCount = concrete.getLocalVariableCount();
    if (concrete.getLocalVariableSymbols().size() != localCount ||
        concrete.getLocalVariablePaths().size() != localCount ||
        concrete.getLocalVariableHasInitializer().size() != localCount)
      return op->emitOpError()
             << "requires local variable metadata arrays to match "
                "local_variable_count";

    for (int64_t kind : concrete.getArgumentKinds())
      if (kind < 0 || kind > 2)
        return op->emitOpError()
               << "has invalid assertion actual argument kind " << kind;
    size_t initializedLocals = 0;
    for (int64_t hasInitializer : concrete.getLocalVariableHasInitializer()) {
      if (hasInitializer != 0 && hasInitializer != 1)
        return op->emitOpError()
               << "has invalid local variable initializer flag "
               << hasInitializer;
      initializedLocals += hasInitializer != 0;
    }
    if (concrete.getIsRecursiveProperty() && concrete.getHasExpandedBody())
      return op->emitOpError()
             << "cannot expand the body of a recursive property placeholder";

    // Slang emits a substituted assertion body first, followed by one child
    // for every actual (including a selected default), then the initializers
    // of local assertion variables. Keep that contract explicit: bounded AOT
    // consumers execute only the already-substituted first child and must not
    // accidentally evaluate the metadata copies of actuals a second time.
    size_t expectedChildren = argumentCount + initializedLocals +
                              static_cast<size_t>(
                                  concrete.getHasExpandedBody());
    size_t actualChildren = 0;
    if (op->getNumRegions() != 0 && !op->getRegion(0).empty())
      actualChildren = op->getRegion(0).front().getOperations().size();
    if (actualChildren != expectedChildren)
      return op->emitOpError()
             << "assertion invocation inventory describes "
             << expectedChildren << " children but body contains "
             << actualChildren;
    return success();
  }
};

/// Checks the resolved formal / actual metadata on an elaborated checker
/// instance. Actual kind uses the same expression / assertion / timing
/// encoding as sequence and property invocations.
template <typename ConcreteType>
class VerifyCheckerInstanceMetadata
    : public TraitBase<ConcreteType, VerifyCheckerInstanceMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    size_t count = concrete.getConnectionCount();
    if (concrete.getConnectionFormalSymbols().size() != count ||
        concrete.getConnectionFormalPaths().size() != count ||
        concrete.getConnectionActualKinds().size() != count ||
        concrete.getConnectionHasActual().size() != count ||
        concrete.getConnectionHasOutputInitial().size() != count ||
        concrete.getConnectionAttributeCounts().size() != count)
      return op->emitOpError()
             << "requires connection metadata arrays to match "
                "connection_count";

    for (int64_t kind : concrete.getConnectionActualKinds())
      if (kind < 0 || kind > 2)
        return op->emitOpError()
               << "has invalid checker actual argument kind " << kind;
    for (int64_t present : concrete.getConnectionHasActual())
      if (present < 0 || present > 1)
        return op->emitOpError()
               << "requires connection_has_actual entries to be boolean";
    for (int64_t present : concrete.getConnectionHasOutputInitial())
      if (present < 0 || present > 1)
        return op->emitOpError()
               << "requires connection_has_output_initial entries to be "
                  "boolean";
    for (int64_t attributeCount : concrete.getConnectionAttributeCounts())
      if (attributeCount < 0)
        return op->emitOpError()
               << "requires nonnegative connection attribute counts";
    return success();
  }
};

/// Checks the identities attached to a procedural checker instantiation
/// statement. The elaborated checker symbols themselves remain ordinary
/// symbol-table children of the surrounding procedural scope.
template <typename ConcreteType>
class VerifyProceduralCheckerMetadata
    : public TraitBase<ConcreteType, VerifyProceduralCheckerMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    size_t count = concrete.getInstanceCount();
    if (concrete.getInstanceSymbols().size() != count ||
        concrete.getInstancePaths().size() != count)
      return op->emitOpError()
             << "requires instance metadata arrays to match instance_count";
    return success();
  }
};

} // namespace mlir::OpTrait

#endif // OBELISK_DIALECT_SEMANTICTRAITS_H
