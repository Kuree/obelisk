//===- PrepareValidation.cpp - Semantic preparation validation -----------===//
//
// Validates the elaborated semantic tree and freezes its global symbol
// namespace before isolated simulation units are created.
//
//===----------------------------------------------------------------------===//

#include "PrepareValidation.h"

#include "Detail.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"

#include <functional>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

/// Node kinds whose semantics are declarative but that derive from a shared
/// generic base, so they cannot carry the SemanticDeclarativeNode trait.
bool isDeclarativeLeafNode(Operation *op) {
  return isa<
      semantic::SVRandSequenceStatementOp, semantic::SVCoverCrossSymbolOp,
      semantic::SVCoverCrossBodySymbolOp, semantic::SVLocalAssertionVarSymbolOp,
      semantic::SVRandSeqProductionSymbolOp, semantic::SVDPIOpenArrayTypeOp>(
      op);
}

bool isCoverageNode(Operation *op) {
  if (isa<semantic::SVCovergroupTypeOp, semantic::SVCovergroupBodySymbolOp,
          semantic::SVCoverpointSymbolOp, semantic::SVCoverageBinSymbolOp,
          semantic::SVNewCovergroupExpressionOp>(op))
    return true;
  if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(op))
    return formal.getIsCoverageSampleFormal().value_or(false);
  return false;
}

bool isInsideCovergroup(Operation *op) {
  return op && op->getParentOfType<semantic::SVCovergroupTypeOp>();
}

bool isSupportedClassDeclaration(Operation *op) {
  return isa<semantic::SVClassTypeOp, semantic::SVGenericClassDefSymbolOp,
             semantic::SVMethodPrototypeSymbolOp,
             semantic::SVClassPropertySymbolOp,
             semantic::SVConstraintBlockSymbolOp>(op);
}

bool isSupportedConstraintNode(Operation *op) {
  return isa<
      semantic::SVConstraintListOp, semantic::SVExpressionConstraintOp,
      semantic::SVImplicationConstraintOp, semantic::SVConditionalConstraintOp,
      semantic::SVUniquenessConstraintOp, semantic::SVDisableSoftConstraintOp,
      semantic::SVSolveBeforeConstraintOp, semantic::SVForeachConstraintOp>(op);
}

bool isSupportedAssertionNode(Operation *op) {
  return isa<
      semantic::SVImmediateAssertionStatementOp,
      semantic::SVConcurrentAssertionStatementOp, semantic::SVPropertySymbolOp,
      semantic::SVSequenceSymbolOp, semantic::SVAssertionPortSymbolOp,
      semantic::SVAssertionInstanceExpressionOp,
      semantic::SVInvalidAssertionExprOp, semantic::SVSimpleAssertionExprOp,
      semantic::SVSequenceConcatExprOp, semantic::SVSequenceWithMatchExprOp,
      semantic::SVUnaryAssertionExprOp, semantic::SVBinaryAssertionExprOp,
      semantic::SVFirstMatchAssertionExprOp,
      semantic::SVClockingAssertionExprOp,
      semantic::SVStrongWeakAssertionExprOp, semantic::SVAbortAssertionExprOp,
      semantic::SVConditionalAssertionExprOp, semantic::SVCaseAssertionExprOp,
      semantic::SVDisableIffAssertionExprOp>(op);
}

} // namespace

FailureOr<ValidatedSemanticDesign> validateSemanticDesign(ModuleOp module) {
  ValidatedSemanticDesign result;
  llvm::DenseMap<uint64_t, Operation *> nodeIds;
  bool invalid = false;
  module.walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (!isSemanticOp(op))
      return;
    if (auto root = dyn_cast<semantic::SVRootSymbolOp>(op)) {
      if (result.root) {
        op->emitError("multiple elaborated semantic roots");
        invalid = true;
      }
      result.root = root;
    }
    auto nodeId = op->getAttrOfType<IntegerAttr>("node_id");
    if (!nodeId) {
      op->emitError("semantic node is missing node_id");
      invalid = true;
      return;
    }
    uint64_t id = nodeId.getValue().getZExtValue();
    auto [it, inserted] = nodeIds.try_emplace(id, op);
    if (!inserted) {
      op->emitError() << "duplicate semantic node_id " << id;
      it->second->emitRemark("first node with this ID is here");
      invalid = true;
    }
  });
  if (!result.root) {
    module.emitError(
        "obelisk-sim-prepare requires an elaborated obelisk.sv root");
    return failure();
  }

  // Semantic symbols are isolated at every scope, so nearest-symbol lookup
  // cannot traverse elaboration paths. Node-prefixed names are globally
  // unique; validate every path component against that frozen namespace.
  module.walk([&](Operation *op) {
    if (auto name =
            op->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName()))
      result.symbols.try_emplace(name.getValue(), op);
  });
  result.root->walk([&](Operation *op) {
    for (NamedAttribute named : op->getAttrs()) {
      if (!named.getName().strref().ends_with("_symbol"))
        continue;
      named.getValue().walk([&](SymbolRefAttr reference) {
        bool resolved = result.symbols.count(reference.getRootReference());
        for (FlatSymbolRefAttr nested : reference.getNestedReferences())
          resolved &= result.symbols.count(nested.getValue());
        if (!resolved) {
          op->emitError() << "unresolved semantic reference " << reference;
          invalid = true;
        }
      });
    }
  });

  std::function<bool(Operation *)> isCoverageConstant =
      [&](Operation *expression) {
        if (isa<semantic::SVIntegerLiteralOp,
                semantic::SVUnbasedUnsizedIntegerLiteralOp>(expression))
          return true;
        if (isa<semantic::SVConversionExpressionOp,
                semantic::SVUnaryExpressionOp,
                semantic::SVBinaryExpressionOp>(expression)) {
          SmallVector<Operation *> children = getChildren(expression);
          return !children.empty() &&
                 llvm::all_of(children, isCoverageConstant);
        }
        SymbolRefAttr reference;
        if (auto named =
                dyn_cast<semantic::SVNamedValueExpressionOp>(expression))
          reference = named.getReferencedSymbol();
        else if (auto hierarchical =
                     dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                         expression))
          reference = hierarchical.getReferencedSymbol();
        if (!reference)
          return false;
        auto symbol = result.symbols.find(reference.getLeafReference());
        return symbol != result.symbols.end() &&
               isa<semantic::SVParameterSymbolOp,
                   semantic::SVEnumValueSymbolOp,
                   semantic::SVSpecparamSymbolOp>(symbol->second);
      };

  // Reject unsupported declarative families and dynamic object types before
  // producing target IR, so constructs never survive as silently dropped
  // semantics.
  module.walk([&](Operation *op) {
    if (!isSemanticOp(op))
      return;
    if ((op->hasTrait<OpTrait::SemanticDeclarativeNode>() &&
         !isSupportedClassDeclaration(op) && !isSupportedAssertionNode(op) &&
         !isSupportedConstraintNode(op) && !isCoverageNode(op) &&
         !isInsideCovergroup(op)) ||
        isDeclarativeLeafNode(op)) {
      emitError(getSemanticLocation(op))
          << "unsupported semantic construct in the first simulation slice: "
          << op->getName();
      invalid = true;
    }
    if (auto covergroup = dyn_cast<semantic::SVCovergroupTypeOp>(op)) {
      if (covergroup->getParentOfType<semantic::SVClassTypeOp>()) {
        emitError(getSemanticLocation(op))
            << "class-member and inherited covergroups are not executable";
        invalid = true;
      }
      if (covergroup.getBaseGroupAttr()) {
        emitError(getSemanticLocation(op))
            << "inherited covergroups are not executable";
        invalid = true;
      }
      if (covergroup.getConstructorArgumentCount() != 0) {
        emitError(getSemanticLocation(op))
            << "covergroup constructor formals are not supported; use "
               "zero-argument new";
        invalid = true;
      }
      if (covergroup.getHasCoverageEvent()) {
        emitError(getSemanticLocation(op))
            << "coverage events and automatic sampling are not supported";
        invalid = true;
      }
      for (Operation *child : getChildren(covergroup)) {
        auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
        if (!formal || !formal.getIsCoverageSampleFormal().value_or(false))
          continue;
        FailureOr<Type> type = getNormalizedSemanticType(formal);
        Type scalar =
            succeeded(type) ? sim::getPackedScalarType(*type) : Type{};
        if (formal.getDirection() != semantic::SVArgumentDirection::In ||
            !scalar || !isa<IntegerType, sim::LogicType>(scalar)) {
          emitError(getSemanticLocation(formal))
              << "coverage sample formals must be scalar integral inputs";
          invalid = true;
        }
      }
    } else if (auto body = dyn_cast<semantic::SVCovergroupBodySymbolOp>(op)) {
      if (body.getOptionCount() != 0) {
        emitError(getSemanticLocation(op))
            << "covergroup coverage options are not supported";
        invalid = true;
      }
    } else if (auto coverpoint = dyn_cast<semantic::SVCoverpointSymbolOp>(op)) {
      if (coverpoint.getOptionCount() != 0) {
        emitError(getSemanticLocation(op))
            << "coverpoint coverage options are not supported";
        invalid = true;
      }
      size_t namedBins = llvm::count_if(getChildren(op), [](Operation *child) {
        return isa<semantic::SVCoverageBinSymbolOp>(child);
      });
      if (namedBins == 0) {
        emitError(getSemanticLocation(op))
            << "coverpoints require explicit named bins; automatic bins are "
               "not supported";
        invalid = true;
      }
      FailureOr<Type> type = getNormalizedSemanticType(coverpoint);
      Type scalar = succeeded(type) ? sim::getPackedScalarType(*type) : Type{};
      if (!scalar || !isa<IntegerType, sim::LogicType>(scalar)) {
        emitError(getSemanticLocation(op))
            << "coverpoint expressions must have a two-state or four-state "
               "integral type";
        invalid = true;
      }
    } else if (auto bin = dyn_cast<semantic::SVCoverageBinSymbolOp>(op)) {
      if (bin.getBinsKind() != semantic::SVCoverageBinKind::Bins) {
        emitError(getSemanticLocation(op))
            << (bin.getBinsKind() ==
                        semantic::SVCoverageBinKind::IgnoreBins
                    ? "ignore_bins are not supported"
                    : "illegal_bins are not supported");
        invalid = true;
      }
      if (bin.getIsArray() || bin.getHasNumberOfBins()) {
        emitError(getSemanticLocation(op))
            << "coverage bin arrays and automatic bin counts are not "
               "supported";
        invalid = true;
      }
      if (bin.getIsWildcard()) {
        emitError(getSemanticLocation(op))
            << "wildcard coverage bins are not supported";
        invalid = true;
      }
      if (bin.getHasIff()) {
        emitError(getSemanticLocation(op)) << "bin-level iff is not supported";
        invalid = true;
      }
      if (bin.getTransitionSetCount() != 0 || bin.getIsDefaultSequence()) {
        emitError(getSemanticLocation(op))
            << "transition coverage bins are not supported";
        invalid = true;
      }
      if (bin.getHasSetCoverage() || bin.getHasWith()) {
        emitError(getSemanticLocation(op))
            << "coverage bin with/select expressions are not supported";
        invalid = true;
      }
      if (!bin.getIsDefault())
        for (Operation *value : getChildren(bin)) {
          if (auto range =
                  dyn_cast<semantic::SVValueRangeExpressionOp>(value)) {
            if (!llvm::all_of(getChildren(range), isCoverageConstant)) {
              emitError(getSemanticLocation(value))
                  << "coverage bin range bounds must be elaboration-time "
                     "constants";
              invalid = true;
            }
          } else if (!isCoverageConstant(value)) {
            emitError(getSemanticLocation(value))
                << "coverage bin values must be elaboration-time constants";
            invalid = true;
          }
        }
    } else if (isa<semantic::SVCoverCrossSymbolOp,
                   semantic::SVCoverCrossBodySymbolOp>(op)) {
      emitError(getSemanticLocation(op))
          << "coverage crosses are not supported";
      invalid = true;
    }
    for (NamedAttribute attr : op->getAttrs()) {
      attr.getValue().walk([&](Type type) {
        if (isa<semantic::ObjectType>(type)) {
          emitError(getSemanticLocation(op))
              << "unsupported dynamic or object type in the first simulation "
                 "slice: "
              << type;
          invalid = true;
        }
      });
    }
  });

  if (invalid)
    return failure();
  return result;
}

} // namespace obelisk::simlowering
