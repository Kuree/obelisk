//===- PrepareRandomFunctions.cpp - Frozen constraint functions -----------===//

#include "PrepareRandomFunctions.h"
#include "Detail.h"

#include "llvm/ADT/STLExtras.h"

#include <functional>
#include <limits>
#include <memory>

using namespace mlir;

namespace obelisk::simlowering {

std::optional<unsigned> getIntegralRandomWidth(Type type) {
  if (auto width = sim::getPackedWidth(type))
    return width;
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
    auto elementWidth = getIntegralRandomWidth(array.getElementType());
    if (!elementWidth)
      return std::nullopt;
    uint64_t width =
        uint64_t{*elementWidth} * sim::getAggregateNumElements(array);
    if (width <= std::numeric_limits<unsigned>::max())
      return static_cast<unsigned>(width);
  }
  return std::nullopt;
}

std::optional<FixedRandomArrayLayout> getFixedRandomArrayLayout(Type type) {
  if (!isa<sim::UnpackedArrayType>(type))
    return std::nullopt;
  auto width = getIntegralRandomWidth(type);
  while (auto array = dyn_cast<sim::UnpackedArrayType>(type))
    type = array.getElementType();
  auto elementWidth = sim::getPackedWidth(type);
  if (!width || !elementWidth || *elementWidth == 0 || *width == 0 ||
      *width % *elementWidth != 0)
    return std::nullopt;
  return FixedRandomArrayLayout{*elementWidth, *width / *elementWidth};
}

FailureOr<SmallVector<QualifiedRandomMember>>
collectQualifiedRandomStructMembers(Type type, Location location,
                                    Operation *scope) {
  SmallVector<QualifiedRandomMember> members;
  SmallVector<int64_t> path;
  std::function<LogicalResult(Type)> visit = [&](Type source) -> LogicalResult {
    auto structure = dyn_cast<semantic::SourceAggregateType>(source);
    if (!structure || structure.getIsPacked() || structure.getIsUnion())
      return failure();
    for (auto [ordinal, attribute] : llvm::enumerate(structure.getFields())) {
      auto field = dyn_cast<DictionaryAttr>(attribute);
      auto fieldType = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
      auto mode = field ? field.getAs<IntegerAttr>("rand_mode") : IntegerAttr{};
      if (!fieldType || (field.contains("rand_mode") && !mode) ||
          (mode &&
           (mode.getValue().isNegative() ||
            mode.getValue().getActiveBits() > 2 || mode.getInt() > 2))) {
        emitError(location) << "unpacked random structure has malformed member "
                               "qualifier metadata";
        return failure();
      }
      if (!mode || mode.getInt() == 0)
        continue;
      bool randc = mode.getInt() == 2;
      path.push_back(ordinal);
      auto nested =
          dyn_cast<semantic::SourceAggregateType>(fieldType.getValue());
      if (nested && !nested.getIsPacked()) {
        if (nested.getIsUnion() || randc) {
          emitError(location)
              << (nested.getIsUnion()
                      ? "unpacked unions shall not be random"
                      : "unpacked structures shall not be randc");
          return failure();
        }
        if (failed(visit(fieldType.getValue())))
          return failure();
      } else {
        FailureOr<Type> normalized =
            normalizeSemanticType(fieldType.getValue(), location, scope);
        if (failed(normalized))
          return failure();
        members.push_back({path, fieldType.getValue(), *normalized, randc});
      }
      path.pop_back();
    }
    return success();
  };
  if (failed(visit(type)))
    return failure();
  return members;
}

FailureOr<std::optional<RandomConstraintPath>>
getRandomConstraintPath(Operation *expression,
                        ArrayRef<uint64_t> propertyWidths,
                        llvm::function_ref<std::optional<unsigned>(Operation *)>
                            propertyForExpression) {
  if (auto index = propertyForExpression(expression))
    return std::optional<RandomConstraintPath>(RandomConstraintPath{
        *index, 0, static_cast<unsigned>(propertyWidths[*index]), true,
        expression->hasAttr(randomFunctionStateAttrName)});
  SmallVector<Operation *> children = getChildren(expression);
  if (children.empty())
    return std::optional<RandomConstraintPath>{};
  bool member = isa<semantic::SVMemberAccessExpressionOp>(expression);
  bool element = isa<semantic::SVElementSelectExpressionOp>(expression);
  auto range = dyn_cast<semantic::SVRangeSelectExpressionOp>(expression);
  if (!member && !element && !range)
    return std::optional<RandomConstraintPath>{};
  FailureOr<std::optional<RandomConstraintPath>> base = getRandomConstraintPath(
      children.front(), propertyWidths, propertyForExpression);
  if (failed(base) || !*base)
    return base;
  auto evaluationError = [&]() {
    expression->setAttr(randomEvaluationErrorAttrName,
                        UnitAttr::get(expression->getContext()));
    (**base).precise = false;
    return base;
  };
  if (children.front()->hasAttr(randomEvaluationErrorAttrName))
    return evaluationError();
  if (!(**base).precise)
    return base;
  FailureOr<Type> resultType = getNormalizedSemanticType(expression);
  std::optional<unsigned> resultWidth =
      succeeded(resultType) ? getIntegralRandomWidth(*resultType)
                            : std::nullopt;
  if (!resultWidth || *resultWidth == 0) {
    emitError(getSemanticLocation(expression))
        << "constraint function ordering path has no fixed integral width";
    return failure();
  }

  uint64_t relativeOffset = 0;
  if (member) {
    auto packedOffset = expression->getAttrOfType<IntegerAttr>("packed_offset");
    if (!packedOffset || packedOffset.getValue().isNegative() ||
        packedOffset.getValue().getActiveBits() > 64) {
      emitError(getSemanticLocation(expression))
          << "constraint function ordering path has malformed packed "
             "member metadata";
      return failure();
    }
    relativeOffset = packedOffset.getValue().getZExtValue();
  } else {
    if (children.size() != (element ? 2u : 3u)) {
      emitError(getSemanticLocation(expression))
          << "constraint function ordering path has malformed selection "
             "metadata";
      return failure();
    }
    auto parseKnownIndex =
        [&](Operation *index) -> FailureOr<std::optional<int64_t>> {
      std::optional<StringRef> spelling = getConstantSpelling(index);
      if (!spelling)
        return std::optional<int64_t>{};
      auto sourceType = index->getAttrOfType<TypeAttr>("semantic_type");
      auto sourceWidth = sourceType
                             ? getSemanticPackedWidth(sourceType.getValue())
                             : std::nullopt;
      unsigned parseWidth =
          sourceWidth ? static_cast<unsigned>(*sourceWidth) : 64;
      FailureOr<ParsedConstant> parsed =
          parseSVInteger(*spelling, parseWidth, getSemanticLocation(index));
      if (failed(parsed))
        return failure();
      bool signedIndex =
          sourceType && isSignedSemanticType(sourceType.getValue());
      if (!parsed->unknown.isZero() ||
          (signedIndex ? !parsed->value.isSignedIntN(64)
                       : parsed->value.getActiveBits() > 63)) {
        expression->setAttr(randomEvaluationErrorAttrName,
                            UnitAttr::get(expression->getContext()));
        return std::optional<int64_t>{};
      }
      return std::optional<int64_t>(
          signedIndex ? parsed->value.getSExtValue()
                      : static_cast<int64_t>(parsed->value.getZExtValue()));
    };
    FailureOr<std::optional<int64_t>> first = parseKnownIndex(children[1]);
    if (failed(first))
      return failure();
    if (!*first) {
      (**base).precise = false;
      return base;
    }

    auto sourceTypeAttr =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!sourceTypeAttr) {
      emitError(getSemanticLocation(expression))
          << "constraint function ordering selection has no source type";
      return failure();
    }
    Type sourceType = sourceTypeAttr.getValue();
    if (auto enumeration = dyn_cast<semantic::EnumType>(sourceType))
      sourceType = enumeration.getBaseType();
    int64_t left = static_cast<int64_t>((*base)->width) - 1;
    int64_t right = 0;
    unsigned elementWidth = 1;
    if (auto integral = dyn_cast<semantic::IntegralType>(sourceType)) {
      left = integral.getLeft();
      right = integral.getRight();
    } else if (isa<semantic::RangedPackedArrayType,
                   semantic::RangedUnpackedArrayType,
                   semantic::UnpackedArrayType>(sourceType)) {
      if (auto packed = dyn_cast<semantic::RangedPackedArrayType>(sourceType)) {
        left = packed.getLeft();
        right = packed.getRight();
      } else if (auto unpacked =
                     dyn_cast<semantic::RangedUnpackedArrayType>(sourceType)) {
        left = unpacked.getLeft();
        right = unpacked.getRight();
      } else {
        left = 0;
        right = static_cast<int64_t>(
                    cast<semantic::UnpackedArrayType>(sourceType).getSize()) -
                1;
      }
      APInt leftBound(65, static_cast<uint64_t>(left), true);
      APInt rightBound(65, static_cast<uint64_t>(right), true);
      APInt elementCount = leftBound - rightBound;
      if (elementCount.isNegative())
        elementCount = -elementCount;
      ++elementCount;
      if (elementCount.getActiveBits() > 64 ||
          elementCount.getZExtValue() == 0 ||
          elementCount.getZExtValue() > (**base).width ||
          (**base).width % elementCount.getZExtValue() != 0) {
        emitError(getSemanticLocation(expression))
            << "constraint function ordering selection has malformed "
               "element width";
        return failure();
      }
      elementWidth = (**base).width / elementCount.getZExtValue();
    }
    bool descending = left >= right;
    auto physicalOffset = [&](int64_t index) -> std::optional<uint64_t> {
      APInt selected(65, static_cast<uint64_t>(index), true);
      APInt boundary(65, static_cast<uint64_t>(right), true);
      APInt ordinal = descending ? selected - boundary : boundary - selected;
      if (ordinal.isNegative() || ordinal.getActiveBits() > 64)
        return std::nullopt;
      APInt scaled = ordinal * APInt(65, elementWidth);
      if (scaled.getActiveBits() > 64)
        return std::nullopt;
      return scaled.getZExtValue();
    };
    std::optional<uint64_t> low = physicalOffset(**first);
    if (!low)
      return evaluationError();
    if (element) {
      if (*resultWidth != elementWidth) {
        emitError(getSemanticLocation(expression))
            << "constraint function ordering element selection width is "
               "inconsistent";
        return failure();
      }
    } else if (range) {
      if (range.getSelectionKind() == semantic::SVRangeSelectionKind::Simple) {
        FailureOr<std::optional<int64_t>> second = parseKnownIndex(children[2]);
        if (failed(second))
          return failure();
        if (!*second) {
          (**base).precise = false;
          return base;
        }
        std::optional<uint64_t> other = physicalOffset(**second);
        if (!other)
          return evaluationError();
        uint64_t high = std::max(*low, *other);
        *low = std::min(*low, *other);
        uint64_t selectedWidth = high - *low + elementWidth;
        if (*resultWidth != selectedWidth) {
          emitError(getSemanticLocation(expression))
              << "constraint function ordering range selection width is "
                 "inconsistent";
          return failure();
        }
      } else {
        FailureOr<std::optional<int64_t>> selectedElements =
            parseKnownIndex(children[2]);
        if (failed(selectedElements))
          return failure();
        if (!*selectedElements || **selectedElements <= 0 ||
            static_cast<uint64_t>(**selectedElements) >
                UINT64_MAX / elementWidth ||
            static_cast<uint64_t>(**selectedElements) * elementWidth !=
                *resultWidth) {
          emitError(getSemanticLocation(expression))
              << "constraint function ordering indexed selection width "
                 "is inconsistent";
          return failure();
        }
        bool baseNamesHighBit =
            (descending && range.getSelectionKind() ==
                               semantic::SVRangeSelectionKind::IndexedDown) ||
            (!descending && range.getSelectionKind() ==
                                semantic::SVRangeSelectionKind::IndexedUp);
        if (baseNamesHighBit && *resultWidth > elementWidth) {
          uint64_t adjustment = *resultWidth - elementWidth;
          if (*low < adjustment) {
            return evaluationError();
          }
          *low -= adjustment;
        }
      }
    }
    relativeOffset = *low;
  }
  if (relativeOffset > (*base)->width ||
      *resultWidth > (*base)->width - relativeOffset)
    return evaluationError();
  (*base)->offset += relativeOffset;
  (*base)->width = *resultWidth;
  return base;
}

LogicalResult freezeRandomConstraintPriorityInputs(
    Operation *root, ArrayRef<uint64_t> randomPropertyOffsets,
    uint64_t randomPropertyOffset, Builder &builder,
    llvm::function_ref<
        FailureOr<std::optional<RandomConstraintPath>>(Operation *)>
        getRandomConstraintPath) {
  unsigned orderingWidth =
      static_cast<unsigned>(std::max<uint64_t>(64, randomPropertyOffset));
  bool invalid = false;
  // Function expansion can replace a call by a constant or eliminate an
  // unused argument. Guards still classify the source call as RANDOM when
  // one of its input arguments names an active random variable (18.5.12).
  std::function<FailureOr<APInt>(Operation *)> guardInputs =
      [&](Operation *expression) -> FailureOr<APInt> {
    APInt inputs = APInt::getZero(orderingWidth);
    auto function = dyn_cast<semantic::SVCallExpressionOp>(expression);
    SmallVector<Operation *> children = getChildren(expression);
    if (function && !function.getIsSystemCall()) {
      if (function.getArgumentCount() > children.size())
        return failure();
      children.erase(children.begin(),
                     children.end() - function.getArgumentCount());
    } else {
      FailureOr<std::optional<RandomConstraintPath>> path =
          getRandomConstraintPath(expression);
      if (failed(path))
        return failure();
      if (*path) {
        inputs |= APInt::getLowBitsSet(orderingWidth, (*path)->width)
                      .shl(randomPropertyOffsets[(*path)->property] +
                           (*path)->offset);
        if (isa<semantic::SVElementSelectExpressionOp,
                semantic::SVRangeSelectExpressionOp>(expression))
          children.erase(children.begin());
        else
          children.clear();
      }
    }
    for (Operation *child : children) {
      FailureOr<APInt> childInputs = guardInputs(child);
      if (failed(childInputs))
        return failure();
      inputs |= *childInputs;
    }
    if (function && !function.getIsSystemCall())
      expression->setAttr(randomGuardInputsAttrName,
                          builder.getIntegerAttr(
                              builder.getIntegerType(orderingWidth), inputs));
    return inputs;
  };
  if (failed(guardInputs(root)))
    return failure();
  SmallVector<std::pair<APInt, APInt>> functionOrder;
  bool functionArgumentsPresent = false;
  root->walk([&](Operation *expression) {
    if (!isa<semantic::SVExpressionConstraintOp,
             semantic::SVUniquenessConstraintOp,
             semantic::SVDisableSoftConstraintOp>(expression))
      return;
    SmallVector<APInt> arguments;
    SmallVector<APInt> nonArguments;
    APInt referencedInputs = APInt::getZero(orderingWidth);
    bool impreciseArguments = false;
    bool impreciseNonArguments = false;
    Operation *imprecisePath = nullptr;
    std::function<void(Operation *, bool)> collectReferences =
        [&](Operation *nested, bool inFunctionArgument) {
          if (auto function = dyn_cast<semantic::SVCallExpressionOp>(nested);
              function && !function.getIsSystemCall()) {
            SmallVector<Operation *> children = getChildren(function);
            uint64_t argumentCount = function.getArgumentCount();
            if (argumentCount > children.size()) {
              emitError(getSemanticLocation(function))
                  << "constraint function has malformed argument metadata";
              invalid = true;
              return;
            }
            for (Operation *argument :
                 ArrayRef(children).take_back(argumentCount))
              collectReferences(argument, true);
            return;
          }
          FailureOr<std::optional<RandomConstraintPath>> path =
              getRandomConstraintPath(nested);
          if (failed(path)) {
            invalid = true;
            return;
          }
          if (*path) {
            const RandomConstraintPath &valuePath = **path;
            // Preserve source references even if expression simplification
            // later removes their Boolean dependence. Function priority
            // classifies constraints by the variables they include.
            referencedInputs |=
                APInt::getLowBitsSet(orderingWidth, valuePath.width)
                    .shl(randomPropertyOffsets[valuePath.property] +
                         valuePath.offset);
            if (!valuePath.precise) {
              (inFunctionArgument ? impreciseArguments
                                  : impreciseNonArguments) = true;
              if (!imprecisePath)
                imprecisePath = nested;
            } else {
              uint64_t globalOffset =
                  randomPropertyOffsets[valuePath.property] + valuePath.offset;
              if (globalOffset >= randomPropertyOffset ||
                  valuePath.width > randomPropertyOffset - globalOffset) {
                (inFunctionArgument ? impreciseArguments
                                    : impreciseNonArguments) = true;
                if (!imprecisePath)
                  imprecisePath = nested;
                return;
              }
              APInt mask = APInt::getLowBitsSet(orderingWidth, valuePath.width)
                               .shl(globalOffset);
              SmallVector<APInt> &target =
                  inFunctionArgument ? arguments : nonArguments;
              if (!llvm::is_contained(target, mask))
                target.push_back(mask);
            }
            // A selected path consumes its base. Selection indices remain
            // independent expressions and can themselves name rand state.
            SmallVector<Operation *> children = getChildren(nested);
            if (isa<semantic::SVElementSelectExpressionOp,
                    semantic::SVRangeSelectExpressionOp>(nested))
              for (Operation *index : ArrayRef(children).drop_front())
                collectReferences(index, inFunctionArgument);
            return;
          }
          for (Operation *child : getChildren(nested))
            collectReferences(child, inFunctionArgument);
        };
    if (!isa<semantic::SVDisableSoftConstraintOp>(expression))
      for (Operation *child : getChildren(expression))
        collectReferences(child, false);
    for (Operation *parent = expression->getParentOp();
         parent && parent != root->getParentOp();
         parent = parent->getParentOp()) {
      if (isa<semantic::SVImplicationConstraintOp,
              semantic::SVConditionalConstraintOp>(parent)) {
        SmallVector<Operation *> parts = getChildren(parent);
        if (!parts.empty())
          collectReferences(parts.front(), false);
      }
    }
    expression->setAttr(
        randomPriorityInputsAttrName,
        builder.getIntegerAttr(builder.getIntegerType(orderingWidth),
                               referencedInputs));
    APInt argumentInputs = APInt::getZero(orderingWidth);
    for (const APInt &argument : arguments)
      argumentInputs |= argument;
    expression->setAttr(
        randomPriorityArgumentsAttrName,
        builder.getIntegerAttr(builder.getIntegerType(orderingWidth),
                               argumentInputs));
    functionArgumentsPresent |= !argumentInputs.isZero();
    bool hasArgumentPath = impreciseArguments || !arguments.empty();
    bool hasNonArgumentPath = impreciseNonArguments || !nonArguments.empty();
    if (hasArgumentPath && hasNonArgumentPath &&
        (impreciseArguments || impreciseNonArguments)) {
      emitError(getSemanticLocation(imprecisePath))
          << "constraint function implicit ordering requires statically "
             "selected rand paths";
      invalid = true;
      return;
    }
    for (const APInt &before : arguments)
      for (const APInt &after : nonArguments)
        if ((before & after).isZero() &&
            !llvm::is_contained(functionOrder, std::make_pair(before, after)))
          functionOrder.emplace_back(before, after);
  });
  if (!functionOrder.empty() || functionArgumentsPresent) {
    SmallVector<Attribute> encoded;
    encoded.reserve(functionOrder.size() * 2);
    for (auto [before, after] : functionOrder) {
      encoded.push_back(builder.getIntegerAttr(
          builder.getIntegerType(orderingWidth), before));
      encoded.push_back(
          builder.getIntegerAttr(builder.getIntegerType(orderingWidth), after));
    }
    root->setAttr(randomFunctionOrderAttrName, builder.getArrayAttr(encoded));
  }
  return failure(invalid);
}

static FailureOr<Operation *>
getConstraintFunctionResult(semantic::SVSubroutineSymbolOp function,
                            semantic::SVCallExpressionOp call) {
  MLIRContext *context = function.getContext();
  SmallVector<semantic::SVReturnStatementOp> returns;
  SmallVector<semantic::SVExpressionStatementOp> expressionStatements;
  bool unsupportedStatement = false;
  bool unsupportedLocal = false;
  function->walk([&](Operation *nested) {
    if (nested == function.getOperation())
      return;
    if (auto local = dyn_cast<semantic::SVVariableSymbolOp>(nested)) {
      auto reference = FlatSymbolRefAttr::get(context, local.getSymName());
      bool compilerState =
          local.getIsCompilerGenerated().value_or(false) &&
          ((function.getReturnVariableSymbol() &&
            function.getReturnVariableSymbol()->getLeafReference() ==
                reference.getValue()) ||
           (function.getThisVariableSymbol() &&
            function.getThisVariableSymbol()->getLeafReference() ==
                reference.getValue()));
      unsupportedLocal |= !compilerState;
      return;
    }
    if (auto ret = dyn_cast<semantic::SVReturnStatementOp>(nested)) {
      returns.push_back(ret);
      return;
    }
    if (auto statement = dyn_cast<semantic::SVExpressionStatementOp>(nested)) {
      expressionStatements.push_back(statement);
      return;
    }
    StringRef name = nested->getName().getStringRef();
    if (name.starts_with("obelisk.sv.statement.") &&
        !isa<semantic::SVStatementListOp, semantic::SVBlockStatementOp>(nested))
      unsupportedStatement = true;
  });
  if (unsupportedLocal || unsupportedStatement) {
    emitError(getSemanticLocation(call))
        << "constraint function " << function.getName().value_or("")
        << " must be a side-effect-free expression function";
    return failure();
  }
  if (returns.size() == 1 && expressionStatements.empty()) {
    SmallVector<Operation *> values = getChildren(returns.front());
    if (values.size() == 1)
      return values.front();
  }
  if (returns.empty() && expressionStatements.size() == 1) {
    SmallVector<Operation *> statement =
        getChildren(expressionStatements.front());
    auto assignment =
        statement.size() == 1
            ? dyn_cast<semantic::SVAssignmentExpressionOp>(statement.front())
            : semantic::SVAssignmentExpressionOp{};
    SmallVector<Operation *> operands =
        assignment ? getChildren(assignment) : SmallVector<Operation *>{};
    auto lhs = operands.size() == 2
                   ? operands.front()->getAttrOfType<SymbolRefAttr>(
                         "referenced_symbol")
                   : SymbolRefAttr{};
    if (assignment &&
        assignment.getAssignmentKind() ==
            semantic::SVAssignmentKind::Blocking &&
        function.getReturnVariableSymbol() && lhs &&
        lhs.getLeafReference() ==
            function.getReturnVariableSymbol()->getLeafReference())
      return operands.back();
  }
  emitError(getSemanticLocation(call))
      << "constraint function " << function.getName().value_or("")
      << " must define its result with one return expression or one "
         "blocking assignment";
  return failure();
}

LogicalResult expandRandomConstraintFunctions(
    MutableArrayRef<Operation *> constraintRoots,
    const llvm::StringMap<Operation *> &semanticSymbols, Builder &builder,
    const llvm::DenseMap<Operation *, unsigned> &,
    llvm::function_ref<bool(semantic::SVCallExpressionOp)> isAdmittedCall,
    llvm::function_ref<
        FailureOr<semantic::SVSubroutineSymbolOp>(semantic::SVCallExpressionOp)>
        resolveConstraintFunction) {
  std::function<FailureOr<Operation *>(
      Operation *, const llvm::DenseMap<Operation *, Operation *> &, bool,
      SmallVectorImpl<Operation *> &)>
      cloneConstraintExpression;
  cloneConstraintExpression =
      [&](Operation *source,
          const llvm::DenseMap<Operation *, Operation *> &substitutions,
          bool functionBody,
          SmallVectorImpl<Operation *> &callStack) -> FailureOr<Operation *> {
    if (auto reference =
            source->getAttrOfType<SymbolRefAttr>("referenced_symbol")) {
      auto symbol = semanticSymbols.find(reference.getLeafReference());
      if (symbol != semanticSymbols.end())
        if (auto substitution = substitutions.find(symbol->second);
            substitution != substitutions.end())
          return substitution->second->clone();
    }

    if (auto callExpression = dyn_cast<semantic::SVCallExpressionOp>(source);
        callExpression && !isAdmittedCall(callExpression)) {
      FailureOr<semantic::SVSubroutineSymbolOp> function =
          resolveConstraintFunction(callExpression);
      if (failed(function))
        return failure();
      if (llvm::is_contained(callStack, function->getOperation())) {
        emitError(getSemanticLocation(callExpression))
            << "recursive constraint function calls are not executable";
        return failure();
      }

      SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
      for (Operation *child : getChildren(*function))
        if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
          formals.push_back(formal);
      SmallVector<Operation *> children = getChildren(callExpression);
      uint64_t argumentCount = callExpression.getArgumentCount();
      if (argumentCount > children.size() || formals.size() != argumentCount) {
        emitError(getSemanticLocation(callExpression))
            << "constraint function argument count does not match its "
               "declaration";
        return failure();
      }
      for (semantic::SVFormalArgumentSymbolOp formal : formals) {
        bool constReference =
            formal.getDirection() == semantic::SVArgumentDirection::Ref &&
            formal.getIsConst().value_or(false);
        if (formal.getDirection() != semantic::SVArgumentDirection::In &&
            !constReference) {
          emitError(getSemanticLocation(callExpression))
              << "constraint functions cannot have output, inout, or "
                 "non-const ref arguments";
          return failure();
        }
      }

      auto templates = std::make_unique<Block>();
      for (Operation *argument : ArrayRef(children).take_back(argumentCount)) {
        FailureOr<Operation *> cloned = cloneConstraintExpression(
            argument, substitutions, functionBody, callStack);
        if (failed(cloned))
          return failure();
        templates->push_back(*cloned);
      }
      llvm::DenseMap<Operation *, Operation *> functionSubstitutions;
      for (auto [formal, actual] :
           llvm::zip_equal(formals, templates->getOperations()))
        functionSubstitutions[formal.getOperation()] = &actual;

      FailureOr<Operation *> result =
          getConstraintFunctionResult(*function, callExpression);
      if (failed(result))
        return failure();
      callStack.push_back(function->getOperation());
      FailureOr<Operation *> cloned = cloneConstraintExpression(
          *result, functionSubstitutions, true, callStack);
      callStack.pop_back();
      if (succeeded(cloned)) {
        // Evaluate every input even when the return expression ignores it.
        // Guard creation propagates ERROR; unused input values add no solver
        // dependence to the expanded return expression.
        OperationState state(
            callExpression.getLoc(),
            semantic::SVConversionExpressionOp::getOperationName());
        state.addAttribute("semantic_type", source->getAttr("semantic_type"));
        state.addAttribute("node_id", source->getAttr("node_id"));
        state.addAttribute("is_implicit", builder.getBoolAttr(true));
        state.addAttribute(randomEvaluationInputsAttrName,
                           builder.getI64IntegerAttr(argumentCount));
        auto *region = state.addRegion();
        region->push_back(new Block());
        for (Operation &argument : templates->getOperations())
          region->front().push_back(argument.clone());
        region->front().push_back(*cloned);
        cloned = Operation::create(state);
      }
      if (succeeded(cloned))
        if (auto inputs =
                source->getAttrOfType<IntegerAttr>(randomGuardInputsAttrName)) {
          APInt mask = inputs.getValue();
          if (auto existing = (*cloned)->getAttrOfType<IntegerAttr>(
                  randomGuardInputsAttrName))
            mask |= existing.getValue().zextOrTrunc(mask.getBitWidth());
          (*cloned)->setAttr(randomGuardInputsAttrName,
                             builder.getIntegerAttr(inputs.getType(), mask));
        }
      return cloned;
    }

    Operation *cloned = source->cloneWithoutRegions();
    if (functionBody) {
      if (auto reference =
              source->getAttrOfType<SymbolRefAttr>("referenced_symbol")) {
        auto symbol = semanticSymbols.find(reference.getLeafReference());
        if (symbol != semanticSymbols.end())
          cloned->setAttr(randomFunctionStateAttrName, builder.getUnitAttr());
      }
    }
    for (auto [sourceRegion, clonedRegion] :
         llvm::zip_equal(source->getRegions(), cloned->getRegions())) {
      for (Block &sourceBlock : sourceRegion) {
        auto *clonedBlock = new Block();
        clonedRegion.push_back(clonedBlock);
        for (Operation &child : sourceBlock) {
          FailureOr<Operation *> clonedChild = cloneConstraintExpression(
              &child, substitutions, functionBody, callStack);
          if (failed(clonedChild)) {
            cloned->destroy();
            return failure();
          }
          clonedBlock->push_back(*clonedChild);
        }
      }
    }
    return cloned;
  };

  for (Operation *&root : constraintRoots) {
    llvm::DenseMap<Operation *, Operation *> substitutions;
    SmallVector<Operation *> callStack;
    FailureOr<Operation *> expanded =
        cloneConstraintExpression(root, substitutions, false, callStack);
    if (failed(expanded)) {
      return failure();
    }
    root->getBlock()->getOperations().insert(root->getIterator(), *expanded);
    root->erase();
    root = *expanded;
  }

  return success();
}

} // namespace obelisk::simlowering
