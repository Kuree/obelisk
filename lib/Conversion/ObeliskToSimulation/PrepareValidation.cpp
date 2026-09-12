//===- PrepareValidation.cpp - Semantic preparation validation -----------===//
//
// Validates the elaborated semantic tree and freezes its global symbol
// namespace before isolated simulation units are created.
//
//===----------------------------------------------------------------------===//

#include "PrepareValidation.h"

#include "obelisk/Coverage/CoverageDatabase.h"

#include "Detail.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <limits>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

/// Node kinds whose semantics are declarative but that derive from a shared
/// generic base, so they cannot carry the SemanticDeclarativeNode trait.
bool isDeclarativeLeafNode(Operation *op) {
  return isa<semantic::SVDPIOpenArrayTypeOp>(op);
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

std::optional<unsigned> getPackedCoverageEventWidth(Operation *expression) {
  FailureOr<Type> type = getNormalizedSemanticType(expression);
  if (failed(type))
    return std::nullopt;
  std::optional<unsigned> width = sim::getPackedWidth(*type);
  return width && *width ? width : std::nullopt;
}

bool isSupportedClockingCoverageEvent(
    semantic::SVCovergroupTypeOp covergroup,
    const llvm::StringMap<Operation *> &semanticSymbols) {
  (void)semanticSymbols;
  SmallVector<Operation *> direct = getChildren(covergroup);
  Operation *control = nullptr;
  for (Operation *child : direct)
    if (isa<semantic::SVSignalEventControlOp, semantic::SVEventListControlOp>(
            child)) {
      if (control)
        return false;
      control = child;
    }
  if (!control)
    return false;

  SmallVector<semantic::SVSignalEventControlOp> events;
  if (auto event = dyn_cast<semantic::SVSignalEventControlOp>(control))
    events.push_back(event);
  else
    for (Operation *child : getChildren(control)) {
      auto event = dyn_cast<semantic::SVSignalEventControlOp>(child);
      if (!event)
        return false;
      events.push_back(event);
    }
  if (events.empty() || events.size() > 64)
    return false;
  return llvm::all_of(events, [&](semantic::SVSignalEventControlOp event) {
    SmallVector<Operation *> children = getChildren(event);
    const size_t expected = event.getHasIff() ? 2 : 1;
    if (children.size() != expected)
      return false;
    std::optional<unsigned> primaryWidth =
        getPackedCoverageEventWidth(children.front());
    if (!primaryWidth)
      return false;
    return !event.getHasIff() ||
           getPackedCoverageEventWidth(children.back()).has_value();
  });
}

bool isSupportedBlockCoverageEvent(
    semantic::SVCovergroupTypeOp covergroup,
    const llvm::StringMap<Operation *> &semanticSymbols) {
  semantic::SVBlockEventListControlOp control;
  for (Operation *child : getChildren(covergroup)) {
    auto candidate = dyn_cast<semantic::SVBlockEventListControlOp>(child);
    if (!candidate)
      continue;
    if (control)
      return false;
    control = candidate;
  }
  if (!control)
    return false;
  SmallVector<Operation *> targets = getChildren(control);
  if (targets.empty() || targets.size() != control.getEventKinds().size() ||
      targets.size() > UINT32_MAX)
    return false;
  return llvm::all_of(targets, [&](Operation *target) {
    auto reference = target->getAttrOfType<SymbolRefAttr>("referenced_symbol");
    if (!reference)
      return false;
    auto found = semanticSymbols.find(reference.getLeafReference());
    return found != semanticSymbols.end() &&
           isa<semantic::SVSubroutineSymbolOp,
               semantic::SVStatementBlockSymbolOp>(found->second);
  });
}

/// Strip the one implicit cast that 19.5.7 applies from a bin endpoint to the
/// effective coverpoint type. The uncast value is needed to distinguish an
/// exactly representable endpoint from one that the language clips.
Operation *peelCoverageEffectiveTypeCast(Operation *expression,
                                         unsigned pointWidth) {
  auto conversion = dyn_cast<semantic::SVConversionExpressionOp>(expression);
  auto implicit = expression->getAttrOfType<BoolAttr>("is_implicit");
  if (!conversion || !implicit || !implicit.getValue())
    return expression;
  FailureOr<Type> normalized = getNormalizedSemanticType(expression);
  std::optional<unsigned> width =
      succeeded(normalized) ? sim::getPackedWidth(*normalized) : std::nullopt;
  SmallVector<Operation *> children = getChildren(conversion);
  return width && *width == pointWidth && children.size() == 1
             ? children.front()
             : expression;
}

bool isSignedCoverageExpression(Operation *expression) {
  if (auto isSigned = expression->getAttrOfType<BoolAttr>("is_signed"))
    return isSigned.getValue();
  if (auto type = expression->getAttrOfType<TypeAttr>("semantic_type"))
    return isSignedSemanticType(type.getValue());
  return false;
}

/// Parse one endpoint after the effective-type cast and prove that peeling
/// that cast does not expose a value rejected or clipped by 19.5.7.
FailureOr<llvm::APInt> parseCoverageWithEndpoint(Operation *endpoint,
                                                 unsigned pointWidth,
                                                 bool pointIsSigned) {
  std::optional<StringRef> effectiveSpelling = getConstantSpelling(endpoint);
  if (!effectiveSpelling) {
    endpoint->emitError(
        "coverage bin with range endpoints must be compiler-folded constants");
    return failure();
  }
  FailureOr<ParsedConstant> effective = parseSVInteger(
      *effectiveSpelling, pointWidth, getSemanticLocation(endpoint));
  if (failed(effective))
    return failure();
  if (!effective->unknown.isZero()) {
    endpoint->emitError(
        "coverage bin with range endpoints must not contain X or Z bits");
    return failure();
  }

  Operation *source = peelCoverageEffectiveTypeCast(endpoint, pointWidth);
  FailureOr<Type> sourceType = getNormalizedSemanticType(source);
  std::optional<unsigned> sourceWidth =
      succeeded(sourceType) ? sim::getPackedWidth(*sourceType) : std::nullopt;
  std::optional<StringRef> sourceSpelling = getConstantSpelling(source);
  if (!sourceWidth || !*sourceWidth || !sourceSpelling) {
    endpoint->emitError(
        "coverage bin with range endpoints must have a known integral value");
    return failure();
  }
  FailureOr<ParsedConstant> original = parseSVInteger(
      *sourceSpelling, *sourceWidth, getSemanticLocation(source));
  if (failed(original))
    return failure();
  if (!original->unknown.isZero()) {
    endpoint->emitError(
        "coverage bin with range endpoints must not contain X or Z bits");
    return failure();
  }

  const bool sourceIsSigned = isSignedCoverageExpression(source);
  const bool sourceIsNegative = sourceIsSigned && original->value.isNegative();
  const bool representable = [&] {
    if (pointIsSigned) {
      if (sourceIsSigned)
        return original->value.isSignedIntN(pointWidth);
      // A signed N-bit point can represent unsigned values using only its
      // lower N-1 magnitude bits. This also admits only zero for N == 1.
      return original->value.getActiveBits() <= pointWidth - 1;
    }
    return !sourceIsNegative && original->value.isIntN(pointWidth);
  }();
  if (!representable) {
    endpoint->emitError(
        "coverage bin with range endpoint is not representable by the "
        "effective coverpoint type");
    return failure();
  }

  llvm::APInt expected = sourceIsSigned
                             ? original->value.sextOrTrunc(pointWidth)
                             : original->value.zextOrTrunc(pointWidth);
  if (expected != effective->value) {
    endpoint->emitError(
        "coverage bin with range endpoint is not representable by the "
        "effective coverpoint type");
    return failure();
  }
  return effective->value;
}

struct CoverageWithCandidatePlan {
  SmallVector<llvm::APInt> values;
  std::string iteratorPath;
};

struct CrossWithCandidatePlan {
  /// Tuple-major values. The final target varies fastest.
  SmallVector<llvm::APInt> values;
  SmallVector<std::string> targetPaths;
};

/// Verify the implicit CrossQueueType shape required by 19.6.1.3 and
/// 19.6.1.4.  Slang preserves the automatically defined CrossValType as an
/// unpacked struct, including declaration-order field names.  Comparing the
/// normalized type here keeps the proof independent of any host ABI layout.
LogicalResult validateCrossSetExpressionType(
    Operation *expression, semantic::SVCoverCrossSymbolOp cross,
    const llvm::StringMap<Operation *> &semanticSymbols) {
  FailureOr<Type> normalized = getNormalizedSemanticType(expression);
  auto queue = succeeded(normalized) ? dyn_cast<sim::QueueType>(*normalized)
                                     : sim::QueueType{};
  auto tuple = queue ? dyn_cast<sim::UnpackedStructType>(queue.getElementType())
                     : sim::UnpackedStructType{};
  if (!queue || queue.getBound() != 0 || !tuple) {
    expression->emitError(
        "cross_set_expression must have the enclosing cross's unbounded "
        "CrossQueueType");
    return failure();
  }

  ArrayAttr fields = tuple.getFields();
  auto targets = cross.getTargetSymbols();
  if (fields.size() != targets.size()) {
    expression->emitError(
        "cross_set_expression CrossValType field count must match the "
        "enclosing cross target count");
    return failure();
  }
  for (auto [ordinal, targetAttr, fieldAttr] :
       llvm::enumerate(targets, fields)) {
    auto targetReference = dyn_cast<SymbolRefAttr>(targetAttr);
    auto found = targetReference
                     ? semanticSymbols.find(targetReference.getLeafReference())
                     : semanticSymbols.end();
    auto target = found == semanticSymbols.end()
                      ? semantic::SVCoverpointSymbolOp{}
                      : dyn_cast<semantic::SVCoverpointSymbolOp>(found->second);
    auto field = dyn_cast<sim::FieldAttr>(fieldAttr);
    FailureOr<Type> targetType =
        target ? getNormalizedSemanticType(target) : FailureOr<Type>(failure());
    StringRef targetName = target ? getDebugName(target) : StringRef{};
    if (!field || field.getOrdinal() != ordinal ||
        field.getPackedOffset() != 0 || targetName.empty() ||
        field.getName() != targetName || failed(targetType) ||
        field.getType() != *targetType) {
      expression->emitError()
          << "cross_set_expression CrossValType field " << ordinal
          << " must exactly match the corresponding cross target name and "
             "effective type";
      return failure();
    }
  }
  return success();
}

/// Verify the array-valued set_covergroup_expression contract from IEEE
/// 1800-2017 19.5.1.2. The frontend has already checked assignment
/// compatibility; this pass still validates the normalized shape so malformed
/// semantic IR cannot reach the managed-container ABI.
LogicalResult
validateCoverageSetExpressionType(Operation *expression,
                                  semantic::SVCoverageBinSymbolOp bin) {
  FailureOr<Type> normalized = getNormalizedSemanticType(expression);
  if (failed(normalized))
    return failure();

  Type element;
  if (auto packed = dyn_cast<sim::PackedArrayType>(*normalized))
    element = packed.getElementType();
  else if (auto fixed = dyn_cast<sim::UnpackedArrayType>(*normalized))
    element = fixed.getElementType();
  else if (auto dynamic = dyn_cast<sim::DynamicArrayType>(*normalized))
    element = dynamic.getElementType();
  else if (auto queue = dyn_cast<sim::QueueType>(*normalized))
    element = queue.getElementType();
  else {
    expression->emitError(
        "set_covergroup_expression must yield a packed array, fixed "
        "unpacked array, dynamic array, or queue; associative arrays are not "
        "permitted");
    return failure();
  }

  Type scalar = sim::getPackedScalarType(element);
  std::optional<unsigned> width =
      scalar ? sim::getPackedWidth(scalar) : std::nullopt;
  const bool integral = scalar && width && *width && *width <= UINT32_MAX &&
                        isa<IntegerType, sim::LogicType>(scalar);
  const bool real = element.isF32() || element.isF64();
  if (!integral && !real) {
    expression->emitError(
        "set_covergroup_expression elements must have an integral, "
        "four-state logic, or real scalar type");
    return failure();
  }

  auto point = bin->getParentOfType<semantic::SVCoverpointSymbolOp>();
  FailureOr<Type> pointType =
      point ? getNormalizedSemanticType(point) : FailureOr<Type>(failure());
  Type pointScalar =
      succeeded(pointType) ? sim::getPackedScalarType(*pointType) : Type{};
  if ((!pointScalar || !isa<IntegerType, sim::LogicType>(pointScalar)) &&
      !(succeeded(pointType) && (*pointType).isF64())) {
    expression->emitError(
        "set_covergroup_expression has no supported enclosing coverpoint "
        "type");
    return failure();
  }
  return success();
}

/// Materialize the finite target-value Cartesian product required by
/// 19.6.1.2. This deliberately enumerates values rather than coverpoint bins:
/// resolution later associates each value tuple with the candidate bin tuples
/// selected by the subordinate selector.
FailureOr<CrossWithCandidatePlan> buildCrossWithCandidatePlan(
    semantic::SVBinSelectWithFilterExprOp with,
    semantic::SVCoverCrossSymbolOp cross,
    const llvm::StringMap<Operation *> &semanticSymbols) {
  SmallVector<Operation *> children = getChildren(with);
  if (children.size() != size_t{2} + with.getHasMatches()) {
    with.emitError("cross selector with expression has malformed children");
    return failure();
  }

  struct TargetDomain {
    unsigned width = 0;
    bool isSigned = false;
    bool isFourState = false;
  };
  SmallVector<TargetDomain> domains;
  CrossWithCandidatePlan plan;
  uint64_t tupleCount = 1;
  StringRef crossHierarchy = getHierarchyName(cross);
  for (auto [ordinal, targetAttr] : llvm::enumerate(cross.getTargetSymbols())) {
    auto targetReference = dyn_cast<SymbolRefAttr>(targetAttr);
    auto found = targetReference
                     ? semanticSymbols.find(targetReference.getLeafReference())
                     : semanticSymbols.end();
    auto target = found == semanticSymbols.end()
                      ? semantic::SVCoverpointSymbolOp{}
                      : dyn_cast<semantic::SVCoverpointSymbolOp>(found->second);
    FailureOr<Type> normalized =
        target ? getNormalizedSemanticType(target) : FailureOr<Type>(failure());
    Type scalar =
        succeeded(normalized) ? sim::getPackedScalarType(*normalized) : Type{};
    std::optional<unsigned> width = sim::getPackedWidth(scalar);
    SmallVector<Operation *> targetChildren =
        target ? getChildren(target) : SmallVector<Operation *>{};
    Operation *sourceExpression =
        !targetChildren.empty() && width
            ? peelCoverageEffectiveTypeCast(targetChildren.front(), *width)
            : nullptr;
    auto semanticType =
        sourceExpression
            ? sourceExpression->getAttrOfType<TypeAttr>("semantic_type")
            : TypeAttr{};
    if (!target || !scalar || !width || !*width ||
        !isa<IntegerType, sim::LogicType>(scalar) || !semanticType) {
      with.emitError() << "cross selector with target " << ordinal
                       << " must be a finite integral coverpoint";
      return failure();
    }
    const bool isFourState = isFourStateSemanticType(semanticType.getValue());
    const uint64_t domainBits = uint64_t{*width} * (isFourState ? 2u : 1u);
    if (domainBits >= 63 ||
        tupleCount > coverage::MaxFunctionalCrossWithCandidates /
                         (uint64_t{1} << domainBits)) {
      with.emitError()
          << "cross selector with candidate count exceeds the v1 limit of "
          << coverage::MaxFunctionalCrossWithCandidates;
      return failure();
    }
    tupleCount *= uint64_t{1} << domainBits;
    domains.push_back(
        {*width, isSignedSemanticType(semanticType.getValue()), isFourState});
    StringRef targetName = getDebugName(target);
    if (crossHierarchy.empty() || targetName.empty()) {
      with.emitError("cross selector with target has no semantic iterator "
                     "identity");
      return failure();
    }
    plan.targetPaths.push_back(
        (Twine(crossHierarchy) + "." + targetName).str());
  }
  if (domains.size() < 2 || tupleCount == 0) {
    with.emitError("cross selector with requires at least two finite targets");
    return failure();
  }

  Operation *predicate = children[1];
  bool invalidIterator = false;
  predicate->walk([&](semantic::SVNamedValueExpressionOp reference) {
    auto symbol = reference.getReferencedSymbol();
    auto found = semanticSymbols.find(symbol.getLeafReference());
    if (found == semanticSymbols.end() ||
        !isa<semantic::SVIteratorSymbolOp>(found->second))
      return;
    // Cross-item iterators are direct members of a cover-cross body. Nested
    // array-method iterators are ordinary covergroup-expression locals and do
    // not name a cross item, so they remain valid inside the predicate.
    if (!isa<semantic::SVCoverCrossBodySymbolOp>(found->second->getParentOp()))
      return;
    if (!llvm::is_contained(plan.targetPaths,
                            reference.getReferencedPath().str()))
      invalidIterator = true;
  });
  if (invalidIterator) {
    predicate->emitError("cross selector with predicate may reference only "
                         "the enclosing cross item iterators");
    return failure();
  }

  plan.values.reserve(tupleCount * domains.size());
  SmallVector<uint64_t> indices(domains.size(), 0);
  for (uint64_t tupleOrdinal = 0; tupleOrdinal != tupleCount; ++tupleOrdinal) {
    for (auto [targetOrdinal, domain] : llvm::enumerate(domains)) {
      llvm::APInt value(domain.width * (domain.isFourState ? 2u : 1u),
                        indices[targetOrdinal]);
      // Biasing the sign bit maps unsigned ordinal order onto mathematical
      // signed order: min, ..., -1, 0, ..., max.
      if (domain.isSigned && !domain.isFourState)
        value.flipBit(domain.width - 1);
      plan.values.push_back(value);
    }
    for (size_t target = domains.size(); target != 0; --target) {
      const size_t index = target - 1;
      const uint64_t domainBits = uint64_t{domains[index].width} *
                                  (domains[index].isFourState ? 2u : 1u);
      const uint64_t cardinality = uint64_t{1} << domainBits;
      if (++indices[index] != cardinality)
        break;
      indices[index] = 0;
    }
  }
  return plan;
}

/// Materialize the exact 19.5.1.1 candidate occurrence sequence. A range is
/// expanded low-to-high, while range-list order, overlap, and duplicates are
/// deliberately retained.
FailureOr<CoverageWithCandidatePlan> buildCoverageWithCandidatePlan(
    semantic::SVCoverageBinSymbolOp bin, const CoverageBinChildren &children,
    const llvm::StringMap<Operation *> &semanticSymbols) {
  auto point = bin->getParentOfType<semantic::SVCoverpointSymbolOp>();
  FailureOr<Type> pointType =
      point ? getNormalizedSemanticType(point) : FailureOr<Type>(failure());
  Type scalar =
      succeeded(pointType) ? sim::getPackedScalarType(*pointType) : Type{};
  std::optional<unsigned> pointWidth = sim::getPackedWidth(scalar);
  const bool supportedShape =
      children.with && point && scalar && pointWidth && *pointWidth &&
      *pointWidth < std::numeric_limits<unsigned>::max() &&
      isa<IntegerType, sim::LogicType>(scalar) && !bin.getIsWildcard() &&
      !bin.getIsDefault() && !bin.getIsDefaultSequence() &&
      !bin.getHasSetCoverage() && bin.getTransitionSetCount() == 0 &&
      bin.getValueCount() > 0 && !children.values.empty();
  if (!supportedShape) {
    bin.emitError("coverage bin with expressions require a finite explicit "
                  "non-wildcard integral state range list");
    return failure();
  }

  const bool pointIsSigned =
      point->getAttrOfType<TypeAttr>("semantic_type") &&
      isSignedSemanticType(
          point->getAttrOfType<TypeAttr>("semantic_type").getValue());
  CoverageWithCandidatePlan plan;
  for (Operation *value : children.values) {
    Operation *low = value;
    Operation *high = nullptr;
    if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(value)) {
      SmallVector<Operation *> endpoints = getChildren(range);
      if (range.getRangeKind() != semantic::SVValueRangeKind::Simple ||
          endpoints.size() != 2) {
        range.emitError("coverage bin with expressions require simple ranges");
        return failure();
      }
      low = endpoints.front();
      high = endpoints.back();
    }

    const bool lowerUnbounded = isUnboundedEndpoint(low);
    const bool upperUnbounded = high && isUnboundedEndpoint(high);
    if ((lowerUnbounded || upperUnbounded) &&
        (!high || (lowerUnbounded && upperUnbounded))) {
      value->emitError("coverage bin open range must have exactly one bounded "
                       "endpoint");
      return failure();
    }
    auto endpointValue = [&](Operation *endpoint,
                             bool lower) -> FailureOr<llvm::APInt> {
      if (!isUnboundedEndpoint(endpoint))
        return parseCoverageWithEndpoint(endpoint, *pointWidth, pointIsSigned);
      if (pointIsSigned)
        return lower ? llvm::APInt::getSignedMinValue(*pointWidth)
                     : llvm::APInt::getSignedMaxValue(*pointWidth);
      return lower ? llvm::APInt(*pointWidth, 0)
                   : llvm::APInt::getAllOnes(*pointWidth);
    };

    FailureOr<llvm::APInt> lowValue = endpointValue(low, true);
    if (failed(lowValue))
      return failure();
    llvm::APInt highValue = *lowValue;
    if (high) {
      FailureOr<llvm::APInt> parsedHigh = endpointValue(high, false);
      if (failed(parsedHigh))
        return failure();
      highValue = *parsedHigh;
    }

    llvm::APInt orderedLow = *lowValue;
    llvm::APInt orderedHigh = highValue;
    if (pointIsSigned) {
      orderedLow.flipBit(*pointWidth - 1);
      orderedHigh.flipBit(*pointWidth - 1);
    }
    if (orderedLow.ugt(orderedHigh)) {
      value->emitError(
          "coverage bin with ranges must have a nondecreasing finite domain");
      return failure();
    }

    const unsigned countWidth = *pointWidth + 1;
    llvm::APInt occurrenceCount =
        orderedHigh.zext(countWidth) - orderedLow.zext(countWidth) + 1;
    const uint64_t remaining =
        coverage::MaxFunctionalWithCandidates - plan.values.size();
    const uint64_t count = occurrenceCount.getLimitedValue(remaining + 1);
    if (count > remaining) {
      bin.emitError() << "coverage bin with candidate count exceeds the v1 "
                         "limit of "
                      << coverage::MaxFunctionalWithCandidates;
      return failure();
    }
    llvm::APInt candidate = *lowValue;
    for (uint64_t ordinal = 0; ordinal != count; ++ordinal) {
      plan.values.push_back(candidate);
      ++candidate;
    }
  }

  Operation *iterator = nullptr;
  children.with->walk([&](semantic::SVNamedValueExpressionOp reference) {
    auto symbol = reference.getReferencedSymbol();
    auto found = semanticSymbols.find(symbol.getLeafReference());
    if (found == semanticSymbols.end() ||
        !isa<semantic::SVIteratorSymbolOp>(found->second))
      return;
    StringRef path = reference.getReferencedPath();
    if ((!iterator || iterator == found->second) &&
        (plan.iteratorPath.empty() || plan.iteratorPath == path)) {
      iterator = found->second;
      plan.iteratorPath = path.str();
      return;
    }
    iterator = children.with;
  });
  if (iterator == children.with || (iterator && plan.iteratorPath.empty()) ||
      (iterator && iterator->getParentOp() != point.getOperation())) {
    children.with->emitError("coverage bin with predicate must reference only "
                             "its own item iterator");
    return failure();
  }
  return plan;
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
  uint32_t coverageLanguageVersion = 2017;
  if (auto version = module->getAttrOfType<IntegerAttr>(
          sim::metadata::coverageLanguageVersion))
    coverageLanguageVersion =
        static_cast<uint32_t>(version.getValue().getZExtValue());
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
      if (covergroup.getHasCoverageEvent() &&
          covergroup.getCoverageEventKind() !=
              semantic::SVCoverageEventKind::Inherited &&
          !((covergroup.getCoverageEventKind() ==
                 semantic::SVCoverageEventKind::Clocking &&
             isSupportedClockingCoverageEvent(covergroup, result.symbols)) ||
            (covergroup.getCoverageEventKind() ==
                 semantic::SVCoverageEventKind::Block &&
             isSupportedBlockCoverageEvent(covergroup, result.symbols)))) {
        emitError(getSemanticLocation(op))
            << "this coverage event is not executable yet; the supported "
               "automatic-sampling forms are a clocking event over a packed "
               "expression with static dependencies or a typed begin/end "
               "block event over a named block, task, function, or method";
        invalid = true;
      }
      llvm::StringMap<Operation *> formalNames;
      llvm::DenseSet<Operation *> sampleFormals;
      llvm::DenseSet<Operation *> constructorStringFormals;
      for (Operation *child : getChildren(covergroup)) {
        auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
        if (!formal)
          continue;
        StringRef name = getDebugName(formal);
        auto [previous, inserted] =
            formalNames.try_emplace(name, formal.getOperation());
        if (!inserted) {
          emitError(getSemanticLocation(formal))
              << "covergroup constructor and sample formal names must be "
                 "unique; '"
              << name << "' is repeated";
          invalid = true;
        }
        const bool isSampleFormal =
            formal.getIsCoverageSampleFormal().value_or(false);
        if (isSampleFormal)
          sampleFormals.insert(formal);
        FailureOr<Type> type = getNormalizedSemanticType(formal);
        Type scalar =
            succeeded(type) ? sim::getPackedScalarType(*type) : Type{};
        const bool string = succeeded(type) && isa<sim::StringType>(*type);
        const bool supportedString =
            string &&
            (formal.getDirection() == semantic::SVArgumentDirection::In ||
             (isSampleFormal &&
              formal.getDirection() == semantic::SVArgumentDirection::Ref));
        if (supportedString && !isSampleFormal)
          constructorStringFormals.insert(formal);
        if ((formal.getDirection() != semantic::SVArgumentDirection::In &&
             formal.getDirection() != semantic::SVArgumentDirection::Ref) ||
            (!scalar && !(succeeded(type) && (*type).isF64()) &&
             !supportedString) ||
            (scalar && !isa<IntegerType, sim::LogicType>(scalar))) {
          emitError(getSemanticLocation(formal))
              << "coverage formals must be input or ref values with signless "
                 "integer, four-state logic, or real type, input string "
                 "constructor values, or input/ref string sample values";
          invalid = true;
        }
      }
      llvm::DenseSet<Operation *> sampleEvaluationUses;
      auto allowSubtree = [&](Operation *root) {
        root->walk(
            [&](Operation *nested) { sampleEvaluationUses.insert(nested); });
      };
      for (Operation *groupChild : getChildren(covergroup)) {
        auto body = dyn_cast<semantic::SVCovergroupBodySymbolOp>(groupChild);
        if (!body)
          continue;
        for (Operation *member : getChildren(body)) {
          if (auto cross = dyn_cast<semantic::SVCoverCrossSymbolOp>(member)) {
            SmallVector<Operation *> crossChildren = getChildren(cross);
            if (cross.getHasIff() && !crossChildren.empty())
              allowSubtree(crossChildren.front());
            continue;
          }
          auto point = dyn_cast<semantic::SVCoverpointSymbolOp>(member);
          if (!point)
            continue;
          SmallVector<Operation *> pointChildren = getChildren(point);
          unsigned directCount = point.getHasIff() ? 2 : 1;
          for (Operation *allowed : ArrayRef<Operation *>(pointChildren)
                                        .take_front(std::min<size_t>(
                                            directCount, pointChildren.size())))
            allowSubtree(allowed);
          for (Operation *candidate : pointChildren) {
            auto bin = dyn_cast<semantic::SVCoverageBinSymbolOp>(candidate);
            if (!bin)
              continue;
            FailureOr<CoverageBinChildren> decoded =
                decodeCoverageBinChildren(bin);
            if (succeeded(decoded) && decoded->iff)
              allowSubtree(decoded->iff);
          }
        }
      }
      covergroup->walk([&](Operation *nested) {
        auto reference =
            nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
        if (!reference)
          return;
        auto target = result.symbols.find(reference.getLeafReference());
        if (target == result.symbols.end())
          return;
        if (sampleFormals.contains(target->second) &&
            !sampleEvaluationUses.contains(nested)) {
          emitError(getSemanticLocation(nested))
              << "IEEE 1800-2017 19.8.1 permits a covergroup sample formal "
                 "only in a coverpoint expression or conditional guard";
          invalid = true;
        }
        if (constructorStringFormals.contains(target->second) &&
            sampleEvaluationUses.contains(nested)) {
          emitError(getSemanticLocation(nested))
              << "String covergroup constructor formals are supported only "
                 "by constructor-time expressions and coverage options";
          invalid = true;
        }
      });
    } else if (auto body = dyn_cast<semantic::SVCovergroupBodySymbolOp>(op)) {
      if (body.getOptionCount() != 0) {
        for (Operation *child : getChildren(body)) {
          auto option = dyn_cast<semantic::SVCoverageOptionOp>(child);
          if (!option)
            continue;
          const bool instanceOption =
              option.getScopeKind() ==
                  semantic::SVCoverageOptionScopeKind::Instance &&
              llvm::is_contained(
                  {semantic::SVCoverageOptionKind::Name,
                   semantic::SVCoverageOptionKind::Comment,
                   semantic::SVCoverageOptionKind::Weight,
                   semantic::SVCoverageOptionKind::Goal,
                   semantic::SVCoverageOptionKind::AtLeast,
                   semantic::SVCoverageOptionKind::AutoBinMax,
                   semantic::SVCoverageOptionKind::CrossNumPrintMissing,
                   semantic::SVCoverageOptionKind::DetectOverlap,
                   semantic::SVCoverageOptionKind::PerInstance,
                   semantic::SVCoverageOptionKind::GetInstCoverage,
                   semantic::SVCoverageOptionKind::CrossRetainAutoBins},
                  option.getOptionKind());
          const bool typeOption =
              option.getScopeKind() ==
                  semantic::SVCoverageOptionScopeKind::Type &&
              llvm::is_contained(
                  {semantic::SVCoverageOptionKind::Weight,
                   semantic::SVCoverageOptionKind::Goal,
                   semantic::SVCoverageOptionKind::Comment,
                   semantic::SVCoverageOptionKind::MergeInstances,
                   semantic::SVCoverageOptionKind::Strobe,
                   semantic::SVCoverageOptionKind::DistributeFirst,
                   semantic::SVCoverageOptionKind::RealInterval},
                  option.getOptionKind()) &&
              (option.getOptionKind() !=
                   semantic::SVCoverageOptionKind::RealInterval ||
               coverageLanguageVersion >= 2023);
          const bool supported =
              option.getOwnerKind() ==
                  semantic::SVCoverageOptionOwnerKind::Covergroup &&
              (instanceOption || typeOption);
          if (!supported) {
            emitError(getSemanticLocation(option))
                << "this covergroup coverage option is not supported";
            invalid = true;
          }
        }
      }
    } else if (auto coverpoint = dyn_cast<semantic::SVCoverpointSymbolOp>(op)) {
      if (coverpoint.getOptionCount() != 0) {
        for (Operation *child : getChildren(coverpoint)) {
          auto option = dyn_cast<semantic::SVCoverageOptionOp>(child);
          if (!option)
            continue;
          const bool instanceOption =
              option.getScopeKind() ==
                  semantic::SVCoverageOptionScopeKind::Instance &&
              llvm::is_contained(
                  {semantic::SVCoverageOptionKind::Weight,
                   semantic::SVCoverageOptionKind::Comment,
                   semantic::SVCoverageOptionKind::Goal,
                   semantic::SVCoverageOptionKind::AtLeast,
                   semantic::SVCoverageOptionKind::AutoBinMax,
                   semantic::SVCoverageOptionKind::DetectOverlap},
                  option.getOptionKind());
          const bool typeOption =
              option.getScopeKind() ==
                  semantic::SVCoverageOptionScopeKind::Type &&
              llvm::is_contained({semantic::SVCoverageOptionKind::Weight,
                                  semantic::SVCoverageOptionKind::Goal,
                                  semantic::SVCoverageOptionKind::Comment,
                                  semantic::SVCoverageOptionKind::RealInterval},
                                 option.getOptionKind()) &&
              (option.getOptionKind() !=
                   semantic::SVCoverageOptionKind::RealInterval ||
               coverageLanguageVersion >= 2023);
          const bool supported =
              option.getOwnerKind() ==
                  semantic::SVCoverageOptionOwnerKind::Coverpoint &&
              (instanceOption || typeOption);
          if (!supported) {
            emitError(getSemanticLocation(option))
                << "this coverpoint coverage option is not supported";
            invalid = true;
          }
        }
      }
      FailureOr<Type> type = getNormalizedSemanticType(coverpoint);
      Type scalar = succeeded(type) ? sim::getPackedScalarType(*type) : Type{};
      const bool real = succeeded(type) && (*type).isF64();
      if ((!scalar || !isa<IntegerType, sim::LogicType>(scalar)) &&
          !(real && coverageLanguageVersion >= 2023)) {
        emitError(getSemanticLocation(op))
            << "coverpoint expressions must have a two-state or four-state "
               "integral type, or a real type in IEEE 1800-2023";
        invalid = true;
      }
      if (real && coverageLanguageVersion >= 2023) {
        bool hasExplicitBin = false;
        for (Operation *child : getChildren(coverpoint)) {
          auto bin = dyn_cast<semantic::SVCoverageBinSymbolOp>(child);
          if (!bin)
            continue;
          hasExplicitBin = true;
          if (bin.getIsDefault() && bin.getIsArray()) {
            emitError(getSemanticLocation(bin))
                << "a default bin for a real coverpoint cannot be an array";
            invalid = true;
          }
        }
        if (!hasExplicitBin) {
          emitError(getSemanticLocation(coverpoint))
              << "real coverpoints must specify at least one explicit bin";
          invalid = true;
        }
      }
    } else if (auto bin = dyn_cast<semantic::SVCoverageBinSymbolOp>(op)) {
      if (auto cross = bin->getParentOfType<semantic::SVCoverCrossSymbolOp>()) {
        FailureOr<CoverageBinChildren> children =
            decodeCoverageBinChildren(bin);
        Operation *selector =
            succeeded(children) ? children->crossSelect : nullptr;
        auto isSupportedSelector = [&](auto &&self, Operation *node) -> bool {
          // VerifySemanticReferenceGraph already requires this leaf to name
          // its enclosing cross exactly.
          if (isa_and_nonnull<semantic::SVCrossIdBinsSelectExprOp>(node))
            return true;
          if (auto condition =
                  dyn_cast_or_null<semantic::SVConditionBinsSelectExprOp>(
                      node)) {
            auto found = result.symbols.find(
                condition.getTargetSymbol().getLeafReference());
            Operation *target =
                found == result.symbols.end() ? nullptr : found->second;
            if (isa_and_nonnull<semantic::SVCoverpointSymbolOp>(target))
              return true;
            auto targetBin =
                dyn_cast_or_null<semantic::SVCoverageBinSymbolOp>(target);
            return targetBin &&
                   targetBin->getParentOfType<semantic::SVCoverpointSymbolOp>();
          }
          if (auto negation =
                  dyn_cast_or_null<semantic::SVUnaryBinsSelectExprOp>(node)) {
            SmallVector<Operation *> operands = getChildren(negation);
            return negation.getOperatorKind() ==
                       semantic::SVCoverageSelectUnaryOperator::Negation &&
                   operands.size() == 1 &&
                   isa<semantic::SVConditionBinsSelectExprOp>(
                       operands.front()) &&
                   self(self, operands.front());
          }
          if (auto conjunction =
                  dyn_cast_or_null<semantic::SVBinaryBinsSelectExprOp>(node)) {
            SmallVector<Operation *> operands = getChildren(conjunction);
            return llvm::is_contained(
                       {semantic::SVCoverageSelectBinaryOperator::And,
                        semantic::SVCoverageSelectBinaryOperator::Or},
                       conjunction.getOperatorKind()) &&
                   operands.size() == 2 && self(self, operands[0]) &&
                   self(self, operands[1]);
          }
          if (auto with =
                  dyn_cast_or_null<semantic::SVBinSelectWithFilterExprOp>(
                      node)) {
            SmallVector<Operation *> operands = getChildren(with);
            if (operands.size() != size_t{2} + with.getHasMatches() ||
                !self(self, operands.front()))
              return false;
            FailureOr<Type> predicateType =
                getNormalizedSemanticType(operands[1]);
            Type predicateScalar =
                succeeded(predicateType)
                    ? sim::getPackedScalarType(*predicateType)
                    : Type{};
            if (!predicateScalar ||
                !isa<IntegerType, sim::LogicType>(predicateScalar)) {
              operands[1]->emitError("cross selector with predicate must have "
                                     "an integral Boolean-compatible type");
              return false;
            }

            if (with.getHasMatches()) {
              Operation *matches = operands.back();
              if (!isa<semantic::SVUnboundedLiteralOp>(matches)) {
                FailureOr<Type> matchesType =
                    getNormalizedSemanticType(matches);
                Type matchesScalar =
                    succeeded(matchesType)
                        ? sim::getPackedScalarType(*matchesType)
                        : Type{};
                if (!matchesScalar ||
                    !isa<IntegerType, sim::LogicType>(matchesScalar)) {
                  matches->emitError("cross selector matches expression must "
                                     "have an integral type");
                  return false;
                }
                if (std::optional<StringRef> spelling =
                        getConstantSpelling(matches)) {
                  std::optional<unsigned> width =
                      sim::getPackedWidth(matchesScalar);
                  FailureOr<ParsedConstant> parsed =
                      width && *width
                          ? parseSVInteger(*spelling, *width,
                                           getSemanticLocation(matches))
                          : FailureOr<ParsedConstant>(failure());
                  const bool isSigned = isSignedCoverageExpression(matches);
                  if (failed(parsed) || !parsed->unknown.isZero() ||
                      (isSigned && parsed->value.isNegative()) ||
                      parsed->value.isZero()) {
                    matches->emitError("cross selector matches constant must "
                                       "be a positive v1 integer");
                    return false;
                  }
                }
              }
            }

            FailureOr<CrossWithCandidatePlan> plan =
                buildCrossWithCandidatePlan(with, cross, result.symbols);
            if (failed(plan))
              return false;
            Builder builder(module.getContext());
            SmallVector<Attribute> values;
            values.reserve(plan->values.size());
            for (const llvm::APInt &candidate : plan->values)
              values.push_back(builder.getIntegerAttr(
                  IntegerType::get(module.getContext(),
                                   candidate.getBitWidth()),
                  candidate));
            SmallVector<Attribute> paths;
            paths.reserve(plan->targetPaths.size());
            for (StringRef path : plan->targetPaths)
              paths.push_back(builder.getStringAttr(path));
            operands[1]->setAttr(
                sim::metadata::coverageFunctionalCrossWithCandidateValues,
                builder.getArrayAttr(values));
            operands[1]->setAttr(
                sim::metadata::coverageFunctionalCrossWithTargetPaths,
                builder.getArrayAttr(paths));
            return true;
          }
          if (auto set =
                  dyn_cast_or_null<semantic::SVSetExprBinsSelectExprOp>(node)) {
            SmallVector<Operation *> operands = getChildren(set);
            if (operands.size() != size_t{1} + set.getHasMatches()) {
              set.emitError("cross_set_expression selector has malformed "
                            "children");
              return false;
            }
            if (failed(validateCrossSetExpressionType(operands.front(), cross,
                                                      result.symbols)))
              return false;
            if (!set.getHasMatches())
              return true;

            Operation *matches = operands.back();
            if (isa<semantic::SVUnboundedLiteralOp>(matches))
              return true;
            FailureOr<Type> matchesType = getNormalizedSemanticType(matches);
            Type matchesScalar = succeeded(matchesType)
                                     ? sim::getPackedScalarType(*matchesType)
                                     : Type{};
            if (!matchesScalar ||
                !isa<IntegerType, sim::LogicType>(matchesScalar)) {
              matches->emitError("cross_set_expression matches expression "
                                 "must have an integral type");
              return false;
            }
            if (std::optional<StringRef> spelling =
                    getConstantSpelling(matches)) {
              std::optional<unsigned> width =
                  sim::getPackedWidth(matchesScalar);
              FailureOr<ParsedConstant> parsed =
                  width && *width ? parseSVInteger(*spelling, *width,
                                                   getSemanticLocation(matches))
                                  : FailureOr<ParsedConstant>(failure());
              const bool isSigned = isSignedCoverageExpression(matches);
              if (failed(parsed) || !parsed->unknown.isZero() ||
                  (isSigned && parsed->value.isNegative()) ||
                  parsed->value.isZero()) {
                matches->emitError("cross_set_expression matches constant "
                                   "must be a positive v1 integer");
                return false;
              }
            }
            return true;
          }
          return false;
        };
        const bool supportedBinKind =
            bin.getBinsKind() == semantic::SVCoverageBinKind::Bins ||
            bin.getBinsKind() == semantic::SVCoverageBinKind::IgnoreBins ||
            bin.getBinsKind() == semantic::SVCoverageBinKind::IllegalBins;
        const bool supported =
            succeeded(children) && supportedBinKind && !bin.getIsArray() &&
            !bin.getIsDefault() && !bin.getIsDefaultSequence() &&
            !bin.getIsWildcard() && !bin.getHasNumberOfBins() &&
            !bin.getHasSetCoverage() && !bin.getHasWith() &&
            bin.getTransitionSetCount() == 0 &&
            isSupportedSelector(isSupportedSelector, selector);
        if (!supported) {
          emitError(getSemanticLocation(op))
              << "this explicit cross bin is not supported; the current "
                 "slice accepts a scalar bins, ignore_bins, or illegal_bins "
                 "declaration selected by an AND/OR tree of "
                 "the enclosing cross identifier, binsof(coverpoint), or "
                 "binsof(coverpoint.bin) leaves, with binsof leaves "
                 "optionally negated or restricted with intersect, and "
                 "finite integral cross-selector with clauses (including "
                 "bounded four-state domains), or "
                 "a typed cross_set_expression";
          invalid = true;
        }
      }
      if (bin.getBinsKind() == semantic::SVCoverageBinKind::IgnoreBins &&
          bin.getIsDefault()) {
        emitError(getSemanticLocation(op))
            << "ignore_bins cannot specify default";
        invalid = true;
      }
      if (bin.getIsDefault() && bin.getHasNumberOfBins()) {
        emitError(getSemanticLocation(op))
            << "a default coverage bin array must be unsized";
        invalid = true;
      }
      const bool transition = bin.getTransitionSetCount() != 0;
      if (transition || bin.getIsDefaultSequence()) {
        FailureOr<CoverageBinChildren> transitionChildren =
            decodeCoverageBinChildren(bin);
        auto point = bin->getParentOfType<semantic::SVCoverpointSymbolOp>();
        Type pointType;
        if (point) {
          FailureOr<Type> normalized = getNormalizedSemanticType(point);
          if (succeeded(normalized))
            pointType = *normalized;
        }
        const bool integralPoint =
            pointType && isa_and_nonnull<IntegerType, sim::LogicType>(
                             sim::getPackedScalarType(pointType));
        const bool defaultSequence =
            bin.getIsDefaultSequence() && !transition &&
            bin.getBinsKind() == semantic::SVCoverageBinKind::Bins &&
            !bin.getIsArray() && !bin.getIsWildcard() &&
            !bin.getHasSetCoverage() && integralPoint &&
            succeeded(transitionChildren);
        bool basicFinite =
            transition && !bin.getIsDefaultSequence() &&
            (bin.getBinsKind() == semantic::SVCoverageBinKind::Bins ||
             bin.getBinsKind() == semantic::SVCoverageBinKind::IgnoreBins ||
             bin.getBinsKind() == semantic::SVCoverageBinKind::IllegalBins) &&
            (!bin.getIsArray() || !bin.getHasNumberOfBins()) &&
            !bin.getHasSetCoverage() && integralPoint &&
            succeeded(transitionChildren);
        bool multipleBinsHasVariableLengthTransition = false;
        if (basicFinite)
          for (const CoverageTransitionSetChildren &set :
               transitionChildren->transitions) {
            bool hasSupportedRepetition = false;
            for (const CoverageTransitionRangeChildren &range : set.ranges) {
              if (range.repeatKind ==
                      semantic::SVCoverageTransitionRepeatKind::Consecutive ||
                  range.repeatKind ==
                      semantic::SVCoverageTransitionRepeatKind::GoTo ||
                  range.repeatKind ==
                      semantic::SVCoverageTransitionRepeatKind::Nonconsecutive)
                hasSupportedRepetition = true;
              else if (range.repeatKind !=
                       semantic::SVCoverageTransitionRepeatKind::None)
                basicFinite = false;
              if (bin.getIsArray() &&
                  range.repeatKind !=
                      semantic::SVCoverageTransitionRepeatKind::None &&
                  range.repeatKind !=
                      semantic::SVCoverageTransitionRepeatKind::Consecutive) {
                multipleBinsHasVariableLengthTransition = true;
                basicFinite = false;
              }
            }
            basicFinite &= set.ranges.size() >= 2 || hasSupportedRepetition;
          }
        bool pointHasTransitionExclusion = false;
        if (point)
          for (Operation *pointChild : getChildren(point))
            if (auto sibling =
                    dyn_cast<semantic::SVCoverageBinSymbolOp>(pointChild))
              pointHasTransitionExclusion |=
                  sibling.getTransitionSetCount() != 0 &&
                  (sibling.getBinsKind() ==
                       semantic::SVCoverageBinKind::IgnoreBins ||
                   sibling.getBinsKind() ==
                       semantic::SVCoverageBinKind::IllegalBins);
        if (multipleBinsHasVariableLengthTransition) {
          emitError(getSemanticLocation(op))
              << "multiple transition bins cannot contain goto or "
                 "nonconsecutive repetition because these produce unbounded "
                 "or varying-length sequences";
          invalid = true;
        }
        if (pointHasTransitionExclusion && !defaultSequence &&
            !multipleBinsHasVariableLengthTransition) {
          // Transition subtraction is performed after distribution. The
          // runtime accepts fixed-length symbolic integral words for both
          // ordinary and exclusion bins. This excludes repetitions, iff, and
          // set expressions whose subtraction would require a more general
          // temporal language operation.
          if (basicFinite) {
            basicFinite = !bin.getHasIff();
            for (const CoverageTransitionSetChildren &set :
                 transitionChildren->transitions)
              for (const CoverageTransitionRangeChildren &range : set.ranges) {
                basicFinite &=
                    range.repeatKind ==
                        semantic::SVCoverageTransitionRepeatKind::None &&
                    !range.items.empty();
              }
          }
          if (!basicFinite) {
            emitError(getSemanticLocation(op))
                << "transition ignore_bins and illegal_bins currently require "
                   "integral fixed-length transitions, scalar or unsized "
                   "arrays, with no repetition, iff, or set expression";
            invalid = true;
          }
        }
        if (!basicFinite && !defaultSequence &&
            !multipleBinsHasVariableLengthTransition) {
          if (!pointHasTransitionExclusion) {
            emitError(getSemanticLocation(op))
                << "this transition coverage bin is not supported; the current "
                   "slice accepts scalar integral bins, including wildcard "
                   "bins, with "
                   "sequences with optional consecutive, goto, or "
                   "nonconsecutive repetition, and unsized multiple bins for "
                   "bounded sequences with optional consecutive repetition";
            invalid = true;
          }
        }
      }
      FailureOr<CoverageBinChildren> children = decodeCoverageBinChildren(bin);
      if (failed(children)) {
        invalid = true;
      } else if (children->setCoverage &&
                 failed(validateCoverageSetExpressionType(children->setCoverage,
                                                          bin))) {
        invalid = true;
      } else if (bin.getHasWith() &&
                 !bin->getParentOfType<semantic::SVCoverCrossSymbolOp>()) {
        FailureOr<CoverageWithCandidatePlan> plan =
            buildCoverageWithCandidatePlan(bin, *children, result.symbols);
        if (failed(plan)) {
          invalid = true;
        } else {
          Builder builder(module.getContext());
          SmallVector<Attribute> candidates;
          candidates.reserve(plan->values.size());
          IntegerType candidateType = IntegerType::get(
              module.getContext(), plan->values.front().getBitWidth());
          for (const llvm::APInt &candidate : plan->values)
            candidates.push_back(
                builder.getIntegerAttr(candidateType, candidate));
          children->with->setAttr(
              sim::metadata::coverageFunctionalWithCandidateValues,
              builder.getArrayAttr(candidates));
          children->with->setAttr(
              sim::metadata::coverageFunctionalWithIteratorPath,
              builder.getStringAttr(plan->iteratorPath));
        }
      }
    } else if (auto cross = dyn_cast<semantic::SVCoverCrossSymbolOp>(op)) {
      for (Operation *child : getChildren(cross)) {
        auto option = dyn_cast<semantic::SVCoverageOptionOp>(child);
        if (!option)
          continue;
        const bool instanceOption =
            option.getScopeKind() ==
                semantic::SVCoverageOptionScopeKind::Instance &&
            llvm::is_contained(
                {semantic::SVCoverageOptionKind::Weight,
                 semantic::SVCoverageOptionKind::Comment,
                 semantic::SVCoverageOptionKind::Goal,
                 semantic::SVCoverageOptionKind::AtLeast,
                 semantic::SVCoverageOptionKind::CrossNumPrintMissing,
                 semantic::SVCoverageOptionKind::CrossRetainAutoBins},
                option.getOptionKind());
        const bool typeOption =
            option.getScopeKind() ==
                semantic::SVCoverageOptionScopeKind::Type &&
            llvm::is_contained({semantic::SVCoverageOptionKind::Weight,
                                semantic::SVCoverageOptionKind::Goal,
                                semantic::SVCoverageOptionKind::Comment},
                               option.getOptionKind());
        if (option.getOwnerKind() !=
                semantic::SVCoverageOptionOwnerKind::Cross ||
            (!instanceOption && !typeOption)) {
          emitError(getSemanticLocation(option))
              << "this cross coverage option is not supported";
          invalid = true;
        }
      }
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
