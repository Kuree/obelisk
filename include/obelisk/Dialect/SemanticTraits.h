//===- SemanticTraits.h - Shared semantic IR invariants ----------*- C++
//-*-===//

#ifndef OBELISK_DIALECT_SEMANTICTRAITS_H
#define OBELISK_DIALECT_SEMANTICTRAITS_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <functional>
#include <tuple>
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

template <typename ConcreteType>
class SemanticExpressionNode
    : public TraitBase<ConcreteType, SemanticExpressionNode> {};

template <typename ConcreteType>
class SemanticTimingNode : public TraitBase<ConcreteType, SemanticTimingNode> {
};

template <typename ConcreteType>
class SemanticReferenceRootNode
    : public TraitBase<ConcreteType, SemanticReferenceRootNode> {};

template <typename ConcreteType>
class PatternVariableNode
    : public TraitBase<ConcreteType, PatternVariableNode> {};

template <typename ConcreteType>
class TaggedPatternNode : public TraitBase<ConcreteType, TaggedPatternNode> {};

template <typename ConcreteType>
class PatternBindingNode : public TraitBase<ConcreteType, PatternBindingNode> {
};

template <typename ConcreteType>
class AggregateFieldNode : public TraitBase<ConcreteType, AggregateFieldNode> {
};

template <typename ConcreteType>
class FormalArgumentNode : public TraitBase<ConcreteType, FormalArgumentNode> {
};

template <typename ConcreteType>
class CoverageGroupNode : public TraitBase<ConcreteType, CoverageGroupNode> {};

template <typename ConcreteType>
class CoveragePointNode : public TraitBase<ConcreteType, CoveragePointNode> {};

template <typename ConcreteType>
class CoverageCrossNode : public TraitBase<ConcreteType, CoverageCrossNode> {};

template <typename ConcreteType>
class CoverageBinNode : public TraitBase<ConcreteType, CoverageBinNode> {};

template <typename ConcreteType>
class CoverageBlockEventNode
    : public TraitBase<ConcreteType, CoverageBlockEventNode> {};

template <typename ConcreteType>
class CoverageClockingEventNode
    : public TraitBase<ConcreteType, CoverageClockingEventNode> {};

template <typename ConcreteType>
class CoverageOptionNode : public TraitBase<ConcreteType, CoverageOptionNode> {
};

template <typename ConcreteType>
class CoverageSelectorNode
    : public TraitBase<ConcreteType, CoverageSelectorNode> {};

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

inline size_t getSemanticChildCount(Operation *op) {
  if (op->getNumRegions() == 0 || op->getRegion(0).empty())
    return 0;
  return op->getRegion(0).front().getOperations().size();
}

inline size_t countDirectCoverageOptions(Operation *op) {
  if (op->getNumRegions() == 0 || op->getRegion(0).empty())
    return 0;
  return llvm::count_if(op->getRegion(0).front(), [](Operation &child) {
    return child.hasTrait<CoverageOptionNode>();
  });
}

inline bool isSemanticExpression(Operation &op) {
  return op.hasTrait<SemanticExpressionNode>();
}

inline bool isCoverageSelector(Operation &op) {
  return op.hasTrait<CoverageSelectorNode>();
}

inline std::tuple<size_t, bool, bool> getDirectCoverageTiming(Operation *op) {
  size_t count = 0;
  bool isBlockEvent = false;
  bool isClockingEvent = false;
  if (op->getNumRegions() == 0 || op->getRegion(0).empty())
    return {count, isBlockEvent, isClockingEvent};
  for (Operation &child : op->getRegion(0).front()) {
    if (!child.hasTrait<SemanticTimingNode>())
      continue;
    ++count;
    isBlockEvent |= child.hasTrait<CoverageBlockEventNode>();
    isClockingEvent |= child.hasTrait<CoverageClockingEventNode>();
  }
  return {count, isBlockEvent, isClockingEvent};
}

inline LogicalResult verifyCoverageOptionCount(Operation *op,
                                               uint64_t expected) {
  if (countDirectCoverageOptions(op) != expected)
    return op->emitOpError(
        "option_count must match direct coverage.option children");
  return success();
}

template <typename ConcreteType>
class VerifyCovergroupTypeMetadata
    : public TraitBase<ConcreteType, VerifyCovergroupTypeMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    if (concrete.getConstructorFormals().size() !=
        concrete.getConstructorArgumentCount())
      return op->emitOpError(
          "constructor_formals must match constructor_argument_count");
    if (concrete.getSampleFormals().size() != concrete.getSampleFormalCount())
      return op->emitOpError("sample_formals must match sample_formal_count");
    // Both dialect enums deliberately share this stable encoding. Inherited
    // groups keep their base reference authoritative and do not clone the
    // base's timing subtree.
    int eventKind = static_cast<int>(concrete.getCoverageEventKind());
    auto [timingCount, isBlockEvent, isClockingEvent] =
        getDirectCoverageTiming(op);
    bool hasTimingEvent = timingCount != 0;
    if (timingCount > 1)
      return op->emitOpError(
          "covergroup must contain at most one direct sampling timing child");
    if (eventKind != 4 && hasTimingEvent != concrete.getHasCoverageEvent())
      return op->emitOpError(
          "has_coverage_event must match the direct sampling timing child");
    if ((eventKind == 0 || eventKind == 2) && hasTimingEvent)
      return op->emitOpError(
          "none and custom-sample forms cannot contain a timing child");
    if (eventKind == 1 && (!hasTimingEvent || !isClockingEvent))
      return op->emitOpError(
          "clocking-event form requires one legal clocking-event timing child");
    if (eventKind == 3 && (!hasTimingEvent || !isBlockEvent))
      return op->emitOpError(
          "block-event form requires one block-event timing child");
    if (eventKind == 4 && hasTimingEvent)
      return op->emitOpError(
          "inherited sampling form cannot duplicate its base timing child");
    if (eventKind == 4 && !concrete.getBaseGroup())
      return op->emitOpError(
          "an inherited sampling form requires a base_group");
    if (eventKind != 4 && concrete.getBaseGroup())
      return op->emitOpError("base_group requires the inherited sampling form");
    return success();
  }
};

template <typename ConcreteType>
class VerifyCoverageOptionContainerMetadata
    : public TraitBase<ConcreteType, VerifyCoverageOptionContainerMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    return verifyCoverageOptionCount(op, concrete.getOptionCount());
  }
};

inline LogicalResult
verifyCoverageExpressionPrefix(Operation *op, ArrayAttr roles,
                               ArrayRef<int64_t> expected) {
  if (roles.size() != expected.size())
    return op->emitOpError("expression_roles has the wrong length");
  if (op->getRegion(0).empty() && !expected.empty())
    return op->emitOpError("missing coverage expression children");
  auto &children = op->getRegion(0).front().getOperations();
  auto child = children.begin();
  for (auto [role, expectedRole] : llvm::zip_equal(roles, expected)) {
    auto integer = dyn_cast<IntegerAttr>(role);
    if (!integer || integer.getInt() != expectedRole)
      return op->emitOpError("expression_roles has an invalid role order");
    if (child == children.end() || !isSemanticExpression(*child))
      return op->emitOpError("coverage expression roles must describe leading "
                             "expression children");
    ++child;
  }
  return success();
}

template <typename ConcreteType>
class VerifyCoverpointMetadata
    : public TraitBase<ConcreteType, VerifyCoverpointMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    SmallVector<int64_t, 2> roles{0};
    if (concrete.getHasIff())
      roles.push_back(1);
    if (failed(verifyCoverageExpressionPrefix(op, concrete.getExpressionRoles(),
                                              roles)))
      return failure();
    return verifyCoverageOptionCount(op, concrete.getOptionCount());
  }
};

template <typename ConcreteType>
class VerifyCoverCrossMetadata
    : public TraitBase<ConcreteType, VerifyCoverCrossMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    if (concrete.getTargetCount() < 2)
      return op->emitOpError("a cross requires at least two targets");
    if (concrete.getTargetSymbols().size() != concrete.getTargetCount())
      return op->emitOpError("target_symbols must match target_count");
    SmallVector<int64_t, 1> roles;
    if (concrete.getHasIff())
      roles.push_back(1);
    if (failed(verifyCoverageExpressionPrefix(op, concrete.getExpressionRoles(),
                                              roles)))
      return failure();
    return verifyCoverageOptionCount(op, concrete.getOptionCount());
  }
};

template <typename ConcreteType>
class VerifyCoverageOptionMetadata
    : public TraitBase<ConcreteType, VerifyCoverageOptionMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    if (getSemanticChildCount(op) != 1)
      return op->emitOpError(
          "must contain exactly one option value expression");
    if (!isSemanticExpression(op->getRegion(0).front().front()))
      return op->emitOpError("option value child must be an expression");
    ConcreteType concrete = cast<ConcreteType>(op);
    int owner = static_cast<int>(concrete.getOwnerKind());
    int scope = static_cast<int>(concrete.getScopeKind());
    int option = static_cast<int>(concrete.getOptionKind());
    bool allowed = false;
    if (owner == 0)
      allowed = scope == 0 ? option >= 0 && option <= 10
                           : option == 1 || option == 2 || option == 3 ||
                                 (option >= 11 && option <= 14);
    else if (owner == 1)
      allowed = scope == 0
                    ? (option >= 1 && option <= 5) || option == 8
                    : option == 1 || option == 2 || option == 3 || option == 14;
    else if (owner == 2)
      allowed = scope == 0
                    ? (option >= 1 && option <= 4) || option == 6 || option == 7
                    : option == 1 || option == 2 || option == 3;
    if (!allowed)
      return op->emitOpError(
          "option is not legal for this coverage owner and option scope");
    return success();
  }
};

template <typename ConcreteType>
class VerifyCoverageBinMetadata
    : public TraitBase<ConcreteType, VerifyCoverageBinMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    ArrayRef<int64_t> roles = concrete.getChildRoles();
    ArrayRef<int64_t> setRanges = concrete.getTransitionSetRangeCounts();
    ArrayRef<int64_t> rangeItems = concrete.getTransitionRangeItemCounts();
    ArrayRef<int64_t> repeatKinds = concrete.getTransitionRangeRepeatKinds();
    ArrayRef<int64_t> hasRepeatFrom =
        concrete.getTransitionRangeHasRepeatFrom();
    ArrayRef<int64_t> hasRepeatTo = concrete.getTransitionRangeHasRepeatTo();
    int64_t valueCount = op->getAttrOfType<IntegerAttr>("value_count").getInt();
    int64_t transitionSetCount =
        op->getAttrOfType<IntegerAttr>("transition_set_count").getInt();

    if (roles.size() != getSemanticChildCount(op))
      return op->emitOpError(
          "malformed coverage-bin child inventory: child_roles size does not "
          "match the region");
    if (valueCount < 0 || transitionSetCount < 0)
      return op->emitOpError(
          "malformed coverage-bin child inventory: value_count is out of "
          "bounds");
    if (setRanges.size() != static_cast<uint64_t>(transitionSetCount))
      return op->emitOpError(
          "malformed coverage-bin child inventory: transition-set count does "
          "not match its layout");

    size_t rangeCount = 0;
    for (int64_t count : setRanges) {
      if (count <= 0 || rangeCount > rangeItems.size() ||
          static_cast<uint64_t>(count) > rangeItems.size() - rangeCount)
        return op->emitOpError(
            "malformed coverage-bin child inventory: transition range count "
            "is out of bounds");
      rangeCount += static_cast<size_t>(count);
    }
    if (rangeCount != rangeItems.size() || repeatKinds.size() != rangeCount ||
        hasRepeatFrom.size() != rangeCount || hasRepeatTo.size() != rangeCount)
      return op->emitOpError(
          "malformed coverage-bin child inventory: transition-range arrays "
          "have inconsistent sizes");

    auto &children = op->getRegion(0).front().getOperations();
    size_t childIndex = 0;
    auto consume = [&](int64_t expected, bool selector = false) {
      if (childIndex >= roles.size() || roles[childIndex] != expected)
        return false;
      Operation &child = *std::next(children.begin(), childIndex++);
      return selector ? isCoverageSelector(child) : isSemanticExpression(child);
    };
    if (concrete.getHasIff() && !consume(0))
      return op->emitOpError("iff expression has no matching child role");
    if (concrete.getHasNumberOfBins() && !consume(1))
      return op->emitOpError("bin-count expression has no matching child role");
    if (concrete.getHasSetCoverage() && !consume(2))
      return op->emitOpError("set expression has no matching child role");
    if (concrete.getHasWith() && !consume(3))
      return op->emitOpError("with expression has no matching child role");
    if (childIndex < roles.size() && roles[childIndex] == 4 &&
        !consume(4, true))
      return op->emitOpError("cross selection has no matching selector child");
    for (int64_t index = 0; index < valueCount; ++index)
      if (!consume(5))
        return op->emitOpError("state value has no matching child role");

    size_t rangeIndex = 0;
    for (int64_t rangesInSet : setRanges) {
      for (int64_t ordinal = 0; ordinal < rangesInSet;
           ++ordinal, ++rangeIndex) {
        int64_t itemCount = rangeItems[rangeIndex];
        int64_t repeatKind = repeatKinds[rangeIndex];
        int64_t hasFrom = hasRepeatFrom[rangeIndex];
        int64_t hasTo = hasRepeatTo[rangeIndex];
        if (itemCount <= 0 ||
            static_cast<uint64_t>(itemCount) > roles.size() - childIndex)
          return op->emitOpError(
              "malformed coverage-bin child inventory: transition item count "
              "is out of bounds");
        if (repeatKind < 0 || repeatKind > 3 ||
            (hasFrom != 0 && hasFrom != 1) || (hasTo != 0 && hasTo != 1) ||
            hasTo > hasFrom || ((repeatKind == 0) != (hasFrom == 0)))
          return op->emitOpError(
              "malformed coverage-bin child inventory: transition repetition "
              "metadata is invalid");
        for (int64_t item = 0; item < itemCount; ++item)
          if (!consume(6))
            return op->emitOpError(
                "transition item has no matching child role");
        if (hasFrom && !consume(7))
          return op->emitOpError(
              "transition lower bound has no matching child role");
        if (hasTo && !consume(8))
          return op->emitOpError(
              "transition upper bound has no matching child role");
      }
    }
    if (childIndex != roles.size())
      return op->emitOpError("unexpected or out-of-order child role");
    return success();
  }
};

template <typename ConcreteType>
class VerifyCoverageBlockEventMetadata
    : public TraitBase<ConcreteType, VerifyCoverageBlockEventMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    if (concrete.getEventKinds().size() != getSemanticChildCount(op))
      return op->emitOpError(
          "event_kinds must have one entry per block event target");
    return success();
  }
};

inline LogicalResult verifySelectorChild(Operation *op, size_t index,
                                         bool selector) {
  auto &children = op->getRegion(0).front().getOperations();
  if (index >= children.size())
    return op->emitOpError("missing coverage selector child");
  Operation &child = *std::next(children.begin(), index);
  if (selector ? !isCoverageSelector(child) : !isSemanticExpression(child))
    return op->emitOpError()
           << "child " << index << " must be a "
           << (selector ? "coverage selector" : "semantic expression");
  return success();
}

template <typename ConcreteType>
class VerifyCoverageConditionSelectorMetadata
    : public TraitBase<ConcreteType, VerifyCoverageConditionSelectorMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    if (getSemanticChildCount(op) != concrete.getIntersectCount())
      return op->emitOpError(
          "intersect_count must match intersect expression children");
    for (size_t i = 0; i < getSemanticChildCount(op); ++i)
      if (failed(verifySelectorChild(op, i, false)))
        return failure();
    return success();
  }
};

template <typename ConcreteType>
class VerifyCoverageUnarySelectorMetadata
    : public TraitBase<ConcreteType, VerifyCoverageUnarySelectorMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    if (getSemanticChildCount(op) != 1)
      return op->emitOpError("unary selector requires one selector child");
    return verifySelectorChild(op, 0, true);
  }
};

template <typename ConcreteType>
class VerifyCoverageBinarySelectorMetadata
    : public TraitBase<ConcreteType, VerifyCoverageBinarySelectorMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    if (getSemanticChildCount(op) != 2)
      return op->emitOpError("binary selector requires two selector children");
    return success(succeeded(verifySelectorChild(op, 0, true)) &&
                   succeeded(verifySelectorChild(op, 1, true)));
  }
};

template <typename ConcreteType>
class VerifyCoverageSetSelectorMetadata
    : public TraitBase<ConcreteType, VerifyCoverageSetSelectorMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    size_t expected = 1 + concrete.getHasMatches();
    if (getSemanticChildCount(op) != expected)
      return op->emitOpError(
          "set selector requires a set expression and optional matches child");
    for (size_t i = 0; i < expected; ++i)
      if (failed(verifySelectorChild(op, i, false)))
        return failure();
    return success();
  }
};

template <typename ConcreteType>
class VerifyCoverageWithSelectorMetadata
    : public TraitBase<ConcreteType, VerifyCoverageWithSelectorMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    ConcreteType concrete = cast<ConcreteType>(op);
    size_t expected = 2 + concrete.getHasMatches();
    if (getSemanticChildCount(op) != expected)
      return op->emitOpError(
          "with selector requires selector and filter children plus optional "
          "matches child");
    if (failed(verifySelectorChild(op, 0, true)))
      return failure();
    for (size_t i = 1; i < expected; ++i)
      if (failed(verifySelectorChild(op, i, false)))
        return failure();
    return success();
  }
};

template <typename ConcreteType>
class VerifyCoverageCrossIdSelectorMetadata
    : public TraitBase<ConcreteType, VerifyCoverageCrossIdSelectorMetadata> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    if (getSemanticChildCount(op) != 0)
      return op->emitOpError("cross identifier selector has no children");
    return success();
  }
};

/// Verifies the complete semantic reference graph once at its root. Coverage
/// relationships are deliberately resolved within that root, while generic
/// semantic references may fall back to the enclosing module. This keeps
/// independent semantic roots isolated without rejecting references to
/// module-sibling declarations.
template <typename ConcreteType>
class VerifySemanticReferenceGraph
    : public TraitBase<ConcreteType, VerifySemanticReferenceGraph> {
public:
  static LogicalResult verifyTrait(Operation *root) {
    ModuleOp module = root->getParentOfType<ModuleOp>();
    IntegerAttr languageVersion;
    if (module)
      languageVersion = dyn_cast_or_null<IntegerAttr>(
          module->getDiscardableAttr("obelisk.coverage.language_version"));

    using SymbolIndex = llvm::DenseMap<StringAttr, SmallVector<Operation *, 1>>;
    SymbolIndex localSymbols;
    SymbolIndex externalSymbols;
    auto indexSymbols = [](Operation *scope, SymbolIndex &index) {
      scope->walk([&](Operation *candidate) {
        if (auto symbol = dyn_cast<::mlir::SymbolOpInterface>(candidate))
          if (StringAttr name = symbol.getNameAttr())
            index[name].push_back(candidate);
      });
    };
    indexSymbols(root, localSymbols);
    if (module) {
      // Generic semantic references may name module-sibling definitions, but
      // must never bind into a different semantic root. Prune every root
      // subtree while constructing this fallback index.
      module->walk<WalkOrder::PreOrder>([&](Operation *candidate) {
        if (candidate->hasTrait<SemanticReferenceRootNode>())
          return WalkResult::skip();
        if (auto symbol = dyn_cast<::mlir::SymbolOpInterface>(candidate))
          if (StringAttr name = symbol.getNameAttr())
            externalSymbols[name].push_back(candidate);
        return WalkResult::advance();
      });
    }

    auto getSymbolPath = [](Operation *symbol) {
      SmallVector<StringAttr, 8> path;
      for (Operation *current = symbol; current;
           current = current->getParentOp()) {
        // Nested SymbolRefAttr components correspond to symbol-table scopes,
        // not every symbol-bearing operation in the lexical ancestry.
        if ((current == symbol ||
             current->hasTrait<::mlir::OpTrait::SymbolTable>()) &&
            isa<::mlir::SymbolOpInterface>(current)) {
          auto interface = cast<::mlir::SymbolOpInterface>(current);
          if (StringAttr name = interface.getNameAttr())
            path.push_back(name);
        }
      }
      std::reverse(path.begin(), path.end());
      return path;
    };

    auto resolveInIndex = [&](SymbolRefAttr reference, const SymbolIndex &index,
                              bool requireExactPath) -> Operation * {
      SmallVector<StringAttr, 8> components{reference.getRootReference()};
      for (FlatSymbolRefAttr nested : reference.getNestedReferences())
        components.push_back(nested.getAttr());
      auto candidates = index.find(components.back());
      if (candidates == index.end())
        return nullptr;
      // A qualified reference must always validate every written component.
      // The unique-leaf shortcut is valid only for a flat reference.
      if (!requireExactPath && components.size() == 1 &&
          candidates->second.size() == 1)
        return candidates->second.front();
      Operation *resolved = nullptr;
      for (Operation *candidate : candidates->second) {
        SmallVector<StringAttr, 8> path = getSymbolPath(candidate);
        if (path.size() < components.size() ||
            !llvm::equal(ArrayRef(path).take_back(components.size()),
                         components))
          continue;
        if (resolved)
          return nullptr;
        resolved = candidate;
      }
      return resolved;
    };
    auto resolveLocalReference = [&](SymbolRefAttr reference) -> Operation * {
      return resolveInIndex(reference, localSymbols,
                            /*requireExactPath=*/true);
    };
    auto resolveGenericReference = [&](SymbolRefAttr reference) -> Operation * {
      // Preserve ordinary lexical lookup first. Only references not found in
      // this semantic root use the module-wide fallback.
      if (Operation *local = resolveInIndex(reference, localSymbols,
                                            /*requireExactPath=*/false))
        return local;
      return resolveInIndex(reference, externalSymbols,
                            /*requireExactPath=*/false);
    };

    auto findCoverageGroup = [](Operation *op) -> Operation * {
      for (Operation *current = op; current; current = current->getParentOp())
        if (current->hasTrait<CoverageGroupNode>())
          return current;
      return nullptr;
    };
    auto findCoveragePoint = [](Operation *op) -> Operation * {
      for (Operation *current = op; current; current = current->getParentOp())
        if (current->hasTrait<CoveragePointNode>())
          return current;
      return nullptr;
    };
    auto findCoverageCross = [](Operation *op) -> Operation * {
      for (Operation *current = op; current; current = current->getParentOp())
        if (current->hasTrait<CoverageCrossNode>())
          return current;
      return nullptr;
    };
    auto findFirstReference = [](Attribute attribute) -> SymbolRefAttr {
      SymbolRefAttr result;
      std::function<void(Attribute)> visitAttribute;
      std::function<void(Type)> visitType;
      visitType = [&](Type type) {
        type.walkImmediateSubElements(
            [&](Attribute nested) { visitAttribute(nested); },
            [&](Type nested) { visitType(nested); });
      };
      visitAttribute = [&](Attribute nested) {
        if (result)
          return;
        if (auto reference = dyn_cast<SymbolRefAttr>(nested)) {
          result = reference;
          return;
        }
        nested.walkImmediateSubElements(
            [&](Attribute child) { visitAttribute(child); },
            [&](Type child) { visitType(child); });
      };
      visitAttribute(attribute);
      return result;
    };
    auto getBaseGroup = [&](Operation *group) -> Operation * {
      Attribute base = group->getAttr("base_group");
      if (!base)
        base = group->getAttr("obelisk.coverage.base_group");
      if (!base)
        return nullptr;
      SymbolRefAttr reference = findFirstReference(base);
      return reference ? resolveLocalReference(reference) : nullptr;
    };
    auto isGroupInBaseChain = [&](Operation *target, Operation *group) -> bool {
      SmallVector<Operation *, 8> visited;
      for (Operation *current = group; current;
           current = getBaseGroup(current)) {
        if (current == target)
          return true;
        if (llvm::is_contained(visited, current))
          return false;
        visited.push_back(current);
      }
      return false;
    };
    auto resolvedCrossTargets = [&](Operation *cross) {
      SmallVector<Operation *, 4> targets;
      if (auto references = cross->getAttrOfType<ArrayAttr>("target_symbols"))
        for (Attribute attribute : references)
          if (auto reference = dyn_cast<SymbolRefAttr>(attribute))
            if (Operation *target = resolveLocalReference(reference))
              targets.push_back(target);
      return targets;
    };

    LogicalResult result = success();
    root->walk([&](Operation *user) {
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
        StringRef attribute = attributeName.getValue();
        bool requireExactPath =
            ((attribute == "base_group" || attribute == "constructor_formals" ||
              attribute == "sample_formals") &&
             user->hasTrait<CoverageGroupNode>()) ||
            (attribute == "target_symbols" &&
             user->hasTrait<CoverageCrossNode>()) ||
            ((attribute == "enclosing_cross_symbol" ||
              attribute == "target_symbol") &&
             user->hasTrait<CoverageSelectorNode>()) ||
            (attribute == "owner_symbol" &&
             user->hasTrait<CoverageOptionNode>());
        bool isPatternReference = attribute == "referenced_symbol" &&
                                  (user->hasTrait<PatternVariableNode>() ||
                                   user->hasTrait<TaggedPatternNode>());
        // This root trait owns coverage relationships and the relational
        // metadata of pattern references. Other SymbolRefs (including refs
        // nested in semantic types) have their own dialect consumers and may
        // legally use elaboration-specific paths that are not symbol-table
        // paths.
        if (!requireExactPath && !isPatternReference)
          continue;
        Operation *resolvedSymbol = requireExactPath
                                        ? resolveLocalReference(reference)
                                        : resolveGenericReference(reference);
        if (!resolvedSymbol) {
          user->emitOpError()
              << "cannot resolve " << attributeName << ' ' << reference;
          result = failure();
          continue;
        }

        if (attribute == "base_group" && user->hasTrait<CoverageGroupNode>()) {
          if (!resolvedSymbol->hasTrait<CoverageGroupNode>()) {
            user->emitOpError("base_group must resolve to a covergroup type");
            result = failure();
            continue;
          }
          auto eventKind =
              user->getAttrOfType<IntegerAttr>("coverage_event_kind");
          if (!eventKind || eventKind.getInt() != 4) {
            user->emitOpError(
                "base_group is only valid for an inherited sampling form");
            result = failure();
            continue;
          }
          if (languageVersion && languageVersion.getInt() < 2023) {
            user->emitOpError(
                "covergroup inheritance requires IEEE 1800-2023 or later");
            result = failure();
            continue;
          }
          SmallVector<Operation *, 8> baseChain{user};
          Operation *base = resolvedSymbol;
          bool hasCycle = false;
          while (base) {
            if (llvm::is_contained(baseChain, base)) {
              hasCycle = true;
              break;
            }
            baseChain.push_back(base);
            base = getBaseGroup(base);
          }
          if (hasCycle) {
            user->emitOpError("base_group chain must be acyclic");
            result = failure();
            continue;
          }
          auto inheritedHasEvent =
              user->getAttrOfType<BoolAttr>("has_coverage_event");
          auto baseHasEvent =
              resolvedSymbol->getAttrOfType<BoolAttr>("has_coverage_event");
          if (!inheritedHasEvent || !baseHasEvent ||
              inheritedHasEvent.getValue() != baseHasEvent.getValue()) {
            user->emitOpError(
                "inherited sampling-event presence must match base_group");
            result = failure();
          }
          continue;
        }
        if (attribute == "constructor_formals" ||
            attribute == "sample_formals") {
          if (!user->hasTrait<CoverageGroupNode>() ||
              !resolvedSymbol->hasTrait<FormalArgumentNode>()) {
            user->emitOpError()
                << attribute << " must resolve to formal arguments";
            result = failure();
          } else if (attribute == "constructor_formals" &&
                     resolvedSymbol->hasAttr("is_coverage_sample_formal")) {
            user->emitOpError(
                "constructor_formals cannot name coverage sample formals");
            result = failure();
          } else if (attribute == "sample_formals" &&
                     !resolvedSymbol->hasAttr("is_coverage_sample_formal")) {
            user->emitOpError(
                "sample_formals must resolve to coverage sample formals");
            result = failure();
          } else if (!resolvedSymbol->getParentOp() ||
                     !resolvedSymbol->getParentOp()
                          ->hasTrait<CoverageGroupNode>() ||
                     !isGroupInBaseChain(resolvedSymbol->getParentOp(), user)) {
            user->emitOpError()
                << attribute
                << " must be direct formals of this covergroup or its "
                   "base-group chain";
            result = failure();
          }
          continue;
        }
        if (attribute == "target_symbols" &&
            user->hasTrait<CoverageCrossNode>()) {
          if (!resolvedSymbol->hasTrait<CoveragePointNode>()) {
            user->emitOpError(
                "cross target_symbols must resolve to coverpoints");
            result = failure();
          } else if (!isGroupInBaseChain(findCoverageGroup(resolvedSymbol),
                                         findCoverageGroup(user))) {
            user->emitOpError(
                "cross target must belong to its enclosing covergroup or "
                "base-group chain");
            result = failure();
          }
          continue;
        }
        if (attribute == "enclosing_cross_symbol" &&
            user->hasTrait<CoverageSelectorNode>()) {
          if (!resolvedSymbol->hasTrait<CoverageCrossNode>()) {
            user->emitOpError(
                "enclosing_cross_symbol must resolve to a cover cross");
            result = failure();
          } else if (findCoverageCross(user) != resolvedSymbol) {
            user->emitOpError(
                "enclosing_cross_symbol must name the selector's enclosing "
                "cross");
            result = failure();
          }
          continue;
        }
        if (attribute == "target_symbol" &&
            user->hasTrait<CoverageSelectorNode>()) {
          Operation *targetPoint = nullptr;
          if (resolvedSymbol->hasTrait<CoveragePointNode>())
            targetPoint = resolvedSymbol;
          else if (resolvedSymbol->hasTrait<CoverageBinNode>())
            targetPoint = findCoveragePoint(resolvedSymbol);
          if (!targetPoint) {
            user->emitOpError(
                "binsof target_symbol must resolve to a coverpoint or bin");
            result = failure();
          } else {
            Operation *cross = findCoverageCross(user);
            SmallVector<Operation *, 4> targets =
                cross ? resolvedCrossTargets(cross)
                      : SmallVector<Operation *, 4>{};
            if (!llvm::is_contained(targets, targetPoint)) {
              user->emitOpError(
                  "binsof target must be one of the enclosing cross targets");
              result = failure();
            }
          }
          continue;
        }
        if (attribute == "owner_symbol" &&
            user->hasTrait<CoverageOptionNode>()) {
          auto owner = user->getAttrOfType<IntegerAttr>("owner_kind");
          Operation *actualOwner = nullptr;
          int64_t actualOwnerKind = -1;
          for (Operation *current = user->getParentOp(); current;
               current = current->getParentOp()) {
            if (current->hasTrait<CoveragePointNode>()) {
              actualOwner = current;
              actualOwnerKind = 1;
              break;
            }
            if (current->hasTrait<CoverageCrossNode>()) {
              actualOwner = current;
              actualOwnerKind = 2;
              break;
            }
            if (current->hasTrait<CoverageGroupNode>()) {
              actualOwner = current;
              actualOwnerKind = 0;
              break;
            }
          }
          bool correct = owner && owner.getInt() == actualOwnerKind &&
                         actualOwner == resolvedSymbol;
          if (!correct) {
            user->emitOpError(
                "owner_symbol must name the option's actual enclosing owner");
            result = failure();
          }
          auto optionKind = user->getAttrOfType<IntegerAttr>("option_kind");
          if (languageVersion && optionKind &&
              (optionKind.getInt() == 7 || optionKind.getInt() == 14) &&
              languageVersion.getInt() < 2023) {
            user->emitOpError(
                "coverage option requires IEEE 1800-2023 or later");
            result = failure();
          }
          continue;
        }

        // Pattern references carry stronger invariants than a generic
        // SymbolRefAttr. Check them here, where the semantic graph has already
        // been indexed, instead of making every pattern verifier walk the
        // enclosing module independently.
        if (user->hasTrait<PatternVariableNode>()) {
          if (!resolvedSymbol->hasTrait<PatternBindingNode>()) {
            user->emitOpError("referenced pattern variable does not resolve "
                              "to a pattern binding");
            result = failure();
          }
          continue;
        }
        if (!user->hasTrait<TaggedPatternNode>())
          continue;

        if (!resolvedSymbol->hasTrait<AggregateFieldNode>()) {
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
        /*hasRequiredMetadata=*/concrete.getHasRange());
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
    size_t expectedChildren =
        argumentCount + initializedLocals +
        static_cast<size_t>(concrete.getHasExpandedBody());
    size_t actualChildren = 0;
    if (op->getNumRegions() != 0 && !op->getRegion(0).empty())
      actualChildren = op->getRegion(0).front().getOperations().size();
    if (actualChildren != expectedChildren)
      return op->emitOpError()
             << "assertion invocation inventory describes " << expectedChildren
             << " children but body contains " << actualChildren;
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
