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
  return isa<semantic::SVCoverCrossSymbolOp, semantic::SVCoverCrossBodySymbolOp,
             semantic::SVDPIOpenArrayTypeOp>(op);
}

bool isSupportedRandSequenceNode(Operation *op) {
  return isa<semantic::SVRandSequenceStatementOp,
             semantic::SVRandSeqProductionSymbolOp, semantic::SVProdItemOp,
             semantic::SVCodeBlockProdOp, semantic::SVIfElseProdOp,
             semantic::SVRepeatProdOp, semantic::SVCaseProdOp>(op);
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
      semantic::SVLocalAssertionVarSymbolOp,
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

FailureOr<ValidatedSemanticDesign>
validateSemanticDesign(ModuleOp module, bool pruneUnusedCoverage) {
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

  // Embedded covergroup declarations contribute no behavior until a handle is
  // constructed or referenced. Inventory those uses once, before rejecting
  // unsupported live coverage semantics, so dead UVM metadata does not block
  // or inflate an otherwise unrelated class. This is two linear walks over
  // the semantic tree; every lookup below is O(1).
  llvm::DenseMap<Operation *, Type> embeddedCoverageOwnerByOperation;
  if (pruneUnusedCoverage) {
    llvm::DenseSet<Type> candidates;
    result.root->walk([&](semantic::SVCovergroupTypeOp covergroup) {
      if (isa<semantic::SVClassTypeOp>(covergroup->getParentOp())) {
        candidates.insert(covergroup.getSemanticType());
        covergroup->walk([&](Operation *nested) {
          embeddedCoverageOwnerByOperation.try_emplace(
              nested, covergroup.getSemanticType());
        });
      }
    });

    llvm::DenseSet<Type> live;
    bool preserveAll = false;
    result.root->walk([&](Operation *op) {
      if (!op->getName().getStringRef().starts_with("obelisk.sv.expression."))
        return;

      if (auto call = dyn_cast<semantic::SVCallExpressionOp>(op);
          call && call.getIsSystemCall() &&
          call.getCalleeName().contains("coverage"))
        preserveAll = true;

      if (auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type"))
        if (auto handle = dyn_cast<semantic::CovergroupHandleType>(
                semanticType.getValue()))
          if (candidates.contains(handle))
            live.insert(handle);

      auto reference = op->getAttrOfType<SymbolRefAttr>("referenced_symbol");
      if (!reference)
        return;
      auto target = result.symbols.find(reference.getLeafReference());
      if (target == result.symbols.end())
        return;
      auto owner = embeddedCoverageOwnerByOperation.find(target->second);
      if (owner != embeddedCoverageOwnerByOperation.end())
        live.insert(owner->second);
    });

    if (!preserveAll)
      for (Type candidate : candidates)
        if (!live.contains(candidate))
          result.unusedEmbeddedCovergroupTypes.insert(candidate);
  }

  std::function<bool(Operation *)> isCoverageConstant = [&](Operation
                                                                *expression) {
    if (isa<semantic::SVIntegerLiteralOp,
            semantic::SVUnbasedUnsizedIntegerLiteralOp>(expression))
      return true;
    if (isa<semantic::SVConversionExpressionOp, semantic::SVUnaryExpressionOp,
            semantic::SVBinaryExpressionOp>(expression)) {
      SmallVector<Operation *> children = getChildren(expression);
      return !children.empty() && llvm::all_of(children, isCoverageConstant);
    }
    SymbolRefAttr reference;
    if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression))
      reference = named.getReferencedSymbol();
    else if (auto hierarchical =
                 dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                     expression))
      reference = hierarchical.getReferencedSymbol();
    if (!reference)
      return false;
    auto symbol = result.symbols.find(reference.getLeafReference());
    return symbol != result.symbols.end() &&
           isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
               semantic::SVSpecparamSymbolOp>(symbol->second);
  };

  // Reject unsupported declarative families and dynamic object types before
  // producing target IR, so constructs never survive as silently dropped
  // semantics.
  module.walk([&](Operation *op) {
    if (!isSemanticOp(op))
      return;
    if (auto owner = embeddedCoverageOwnerByOperation.find(op);
        owner != embeddedCoverageOwnerByOperation.end() &&
        result.unusedEmbeddedCovergroupTypes.contains(owner->second))
      return;
    if (isa<semantic::SVCheckerInstanceSymbolOp>(op)) {
      emitError(getSemanticLocation(op))
          << "IEEE 1800-2017 Clause 17 checker instances are retained in "
             "semantic IR but are not executable yet";
      invalid = true;
      return;
    }
    if (isa<semantic::SVTimingPathSymbolOp>(op)) {
      if (!op->hasAttr("obelisk.simple_timing_path")) {
        emitError(getSemanticLocation(op))
            << "IEEE 1800-2017 Clause 30 specify timing paths are not "
               "executable yet for this form (supported subset: "
               "whole-terminal parallel or full paths, including if/ifnone, "
               "and edge-sensitive parallel or full paths, with unknown, "
               "positive, or negative polarity, static "
               "one/two/three/six/twelve transition delays, and statically "
               "disjoint destination driver spans; "
               "unconditional overlapping paths require one distinct whole "
               "source per path and exact driver dependencies)";
        invalid = true;
      }
      return;
    }
    if (isa<semantic::SVPulseStyleSymbolOp>(op)) {
      // IEEE 1800-2017 30.7.4 pulse-style declarations are consumed while
      // preparing the owning path rules. They deliberately produce no
      // standalone runtime actor or lookup table.
      return;
    }
    if (isa<semantic::SVSystemTimingCheckSymbolOp>(op)) {
      if (op->hasAttr("obelisk.basic_timing_check"))
        return;
      if (op->hasAttr("obelisk.invalid_negative_timing_window")) {
        emitError(getSemanticLocation(op))
            << "IEEE 1800-2017 31.9 requires the two negative timing-check "
               "limits to sum to more than one simulation precision unit";
        invalid = true;
        return;
      }
      if (op->hasAttr("obelisk.negative_timing_check")) {
        emitError(getSemanticLocation(op))
            << "IEEE 1800-2017 31.9.2 timestamp/timecheck conditions and "
               "explicit delayed_reference/delayed_data are not executable "
               "in the implicit delayed-signal tranche";
        invalid = true;
        return;
      }
      if (op->hasAttr("obelisk.unsupported_timing_condition")) {
        emitError(getSemanticLocation(op))
            << "IEEE 1800-2017 31.7 timing-check condition must be one "
               "direct packed signal with an optional ~ or ==/!=/===/!== "
               "comparison to 0 or 1; combine multiple conditioning "
               "signals outside the specify block";
        invalid = true;
        return;
      }
      emitError(getSemanticLocation(op))
          << "IEEE 1800-2017 Clause 31 system timing checks are retained in "
             "semantic IR but are not executable yet";
      invalid = true;
      return;
    }
    if ((op->hasTrait<OpTrait::SemanticDeclarativeNode>() &&
         !isSupportedClassDeclaration(op) && !isSupportedAssertionNode(op) &&
         !isSupportedConstraintNode(op) && !isCoverageNode(op) &&
         !isInsideCovergroup(op) && !isSupportedRandSequenceNode(op)) ||
        isDeclarativeLeafNode(op)) {
      emitError(getSemanticLocation(op))
          << "unsupported semantic construct in the first simulation slice: "
          << op->getName();
      invalid = true;
    }
    if (auto covergroup = dyn_cast<semantic::SVCovergroupTypeOp>(op)) {
      if (covergroup.getBaseGroupAttr()) {
        emitError(getSemanticLocation(op))
            << "covergroup type inheritance is not executable";
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
      size_t namedCoverageBins =
          llvm::count_if(getChildren(op), [](Operation *child) {
            auto bin = dyn_cast<semantic::SVCoverageBinSymbolOp>(child);
            return bin &&
                   bin.getBinsKind() == semantic::SVCoverageBinKind::Bins;
          });
      if (namedCoverageBins == 0) {
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
      if (bin.getBinsKind() == semantic::SVCoverageBinKind::IgnoreBins &&
          bin.getIsDefault()) {
        emitError(getSemanticLocation(op))
            << "ignore_bins cannot specify default";
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
