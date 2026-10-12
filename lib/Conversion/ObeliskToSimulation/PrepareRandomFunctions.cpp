//===- PrepareRandomFunctions.cpp - Frozen constraint functions -----------===//

#include "PrepareRandomFunctions.h"
#include "Detail.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <functional>
#include <limits>
#include <memory>
#include <string>

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
    auto layout = getSemanticSelectionLayout(sourceTypeAttr.getValue());
    if (!layout) {
      emitError(getSemanticLocation(expression))
          << "constraint function ordering selection has no fixed integral "
             "dimension";
      return failure();
    }
    int64_t left = layout->left, right = layout->right;
    unsigned elementWidth = layout->elementWidth;
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

namespace {

// Convert automatic, side-effect-free statement bodies into value expressions.
// Branch continuations carry distinct local environments, so a return exits
// exactly its source path and an assignment affects only subsequent reads.
// Every executed initializer/RHS remains an evaluation input even when its
// local result is unused: a bad state dereference still contributes ERROR.
class ConstraintFunctionBody {
public:
  ConstraintFunctionBody(semantic::SVSubroutineSymbolOp function,
                         semantic::SVCallExpressionOp call,
                         const llvm::StringMap<Operation *> &symbols,
                         Builder &builder, Block &expressions)
      : function(function), call(call), symbols(symbols), builder(builder),
        expressions(expressions) {}

  FailureOr<Operation *> compile() {
    resultType = call->getAttrOfType<TypeAttr>("semantic_type");
    if (!resultType)
      return failure();
    FailureOr<Type> normalizedResult = normalizeSemanticType(
        resultType.getValue(), getSemanticLocation(call), function);
    if (failed(normalizedResult) || !sim::getPackedWidth(*normalizedResult))
      return failure();
    if (auto reference = function.getReturnVariableSymbol())
      resultVariable = symbols.lookup(reference->getLeafReference());
    Environment environment;
    bool invalid = false;
    function->walk([&](Operation *node) {
      if (auto local = dyn_cast<semantic::SVVariableSymbolOp>(node)) {
        if (function.getThisVariableSymbol() &&
            function.getThisVariableSymbol()->getLeafReference() ==
                local.getSymName())
          return;
        if (node != resultVariable &&
            local.getLifetime() == semantic::SVVariableLifetime::Static)
          return;
        auto type = node->getAttrOfType<TypeAttr>("semantic_type");
        if (!type) {
          invalid = true;
          return;
        }
        FailureOr<Type> normalized = normalizeSemanticType(
            type.getValue(), getSemanticLocation(node), node);
        auto scalar = succeeded(normalized)
                          ? sim::getPackedScalarType(*normalized)
                          : Type{};
        if (!scalar) {
          invalid = true;
          return;
        }
        std::string initialSpelling =
            isa<sim::LogicType>(scalar)
                ? std::to_string(cast<sim::LogicType>(scalar).getWidth()) +
                      "'bx"
                : "0";
        Operation *initial = literal(node, type, initialSpelling);
        if (node == resultVariable &&
            local.getLifetime() == semantic::SVVariableLifetime::Static)
          unassignedStaticResult = initial;
        environment[node] = initial;
        initialValues[node] = initial;
        if (!local.getIsConst().value_or(false))
          writable.insert(node);
      } else if (auto formal =
                     dyn_cast<semantic::SVFormalArgumentSymbolOp>(node)) {
        if (formal.getDirection() == semantic::SVArgumentDirection::In &&
            !formal.getIsConst().value_or(false))
          writable.insert(node);
      }
    });
    if (invalid)
      return failure();
    SmallVector<Operation *> statements;
    for (Operation *child : getChildren(function))
      if (child->getName().getStringRef().starts_with("obelisk.sv.statement."))
        statements.push_back(child);
    return execute(
        statements, environment,
        [&](Environment &values) -> FailureOr<Operation *> {
          // A two-state automatic return variable has its ordinary default
          // value on fallthrough. Four-state defaults retain X and are never
          // invented as a valid two-state constraint value.
          if (resultVariable)
            return values.lookup(resultVariable) != unassignedStaticResult
                       ? FailureOr<Operation *>(values.lookup(resultVariable))
                       : FailureOr<Operation *>(failure());
          return literal(call, resultType, "0");
        });
  }

private:
  using Environment = llvm::DenseMap<Operation *, Operation *>;
  using Continuation = std::function<FailureOr<Operation *>(Environment &)>;
  struct LoopContext {
    Continuation leave;
    Continuation advance;
  };

  // These source expressions are cloned into both the generated checker and
  // the solver program. Larger or unbounded loops need staged execution rather
  // than an arbitrarily truncated unroll.
  static constexpr uint64_t maxUnrolledIterations = 256;

  Operation *symbol(Operation *node) const {
    auto reference = node->getAttrOfType<SymbolRefAttr>("referenced_symbol");
    return reference ? symbols.lookup(reference.getLeafReference()) : nullptr;
  }

  Operation *make(Operation *source, StringRef name, TypeAttr type,
                  ArrayRef<NamedAttribute> attributes,
                  ArrayRef<Operation *> operands) {
    OperationState state(source->getLoc(), name);
    if (type)
      state.addAttribute("semantic_type", type);
    state.addAttribute("node_id", source->getAttr("node_id"));
    state.addAttribute(randomFunctionExpressionAttrName, builder.getUnitAttr());
    state.addAttributes(attributes);
    auto *region = state.addRegion();
    region->push_back(new Block());
    for (Operation *operand : operands)
      region->front().push_back(operand->clone());
    Operation *node = Operation::create(state);
    expressions.push_back(node);
    return node;
  }

  Operation *literal(Operation *source, TypeAttr type, StringRef value) {
    return make(
        source, semantic::SVIntegerLiteralOp::getOperationName(), type,
        {builder.getNamedAttr("constant_value", builder.getStringAttr(value))},
        {});
  }

  Operation *convert(Operation *source, TypeAttr type, Operation *value) {
    return make(
        source, semantic::SVConversionExpressionOp::getOperationName(), type,
        {builder.getNamedAttr("is_implicit", builder.getBoolAttr(true))},
        {value});
  }

  Operation *evaluateBefore(Operation *source, Operation *value,
                            Operation *result) {
    return make(source, semantic::SVConversionExpressionOp::getOperationName(),
                resultType,
                {builder.getNamedAttr("is_implicit", builder.getBoolAttr(true)),
                 builder.getNamedAttr(randomEvaluationInputsAttrName,
                                      builder.getI64IntegerAttr(1))},
                {value, result});
  }

  Operation *branch(Operation *source, Operation *condition, Operation *yes,
                    Operation *no) {
    // Procedural if treats an unknown truth value as false. A conditional
    // expression instead merges its arms, so reduce the full condition to
    // one bit before converting its truth value to a two-state predicate.
    TypeAttr truthType = TypeAttr::get(
        semantic::IntegralType::get(builder.getContext(), 1, false, true, 0, 0,
                                    semantic::SVIntegralFlavor::Logic));
    TypeAttr predicateType = TypeAttr::get(
        semantic::IntegralType::get(builder.getContext(), 1, false, false, 0, 0,
                                    semantic::SVIntegralFlavor::Bit));
    auto logicalNot = [&](Operation *value) {
      return make(
          source, semantic::SVUnaryExpressionOp::getOperationName(), truthType,
          {builder.getNamedAttr("operator_kind",
                                builder.getI32IntegerAttr(static_cast<int32_t>(
                                    semantic::SVUnaryOperator::LogicalNot)))},
          {value});
    };
    Operation *predicate =
        convert(source, predicateType, logicalNot(logicalNot(condition)));
    return make(
        source, semantic::SVConditionalExpressionOp::getOperationName(),
        resultType,
        {builder.getNamedAttr(randomFunctionConditionalAttrName,
                              builder.getUnitAttr()),
         builder.getNamedAttr("condition_count", builder.getI64IntegerAttr(1)),
         builder.getNamedAttr("condition_pattern_flags",
                              builder.getDenseI64ArrayAttr({0})),
         builder.getNamedAttr(
             "is_signed",
             builder.getBoolAttr(isSignedSemanticType(resultType.getValue())))},
        {predicate, yes, no});
  }

  std::optional<uint64_t> width(Operation *source) const {
    auto type = source->getAttrOfType<TypeAttr>("semantic_type");
    return type ? getSemanticBitstreamWidth(type.getValue()) : std::nullopt;
  }

  FailureOr<ParsedConstant> scalarConstant(Operation *source) {
    auto bitWidth = width(source);
    if (!bitWidth || *bitWidth == 0 ||
        *bitWidth > std::numeric_limits<unsigned>::max())
      return failure();
    if (isa<semantic::SVConversionExpressionOp>(source)) {
      auto operands = getChildren(source);
      if (operands.size() != 1)
        return failure();
      FailureOr<ParsedConstant> value = scalarConstant(operands.front());
      auto type = operands.front()->getAttrOfType<TypeAttr>("semantic_type");
      if (failed(value) || !type)
        return failure();
      bool signExtend = isSignedSemanticType(type.getValue());
      value->value = signExtend ? value->value.sextOrTrunc(*bitWidth)
                                : value->value.zextOrTrunc(*bitWidth);
      value->unknown = signExtend ? value->unknown.sextOrTrunc(*bitWidth)
                                  : value->unknown.zextOrTrunc(*bitWidth);
      return coerceConstant(source, *value);
    }
    if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(source)) {
      auto operands = getChildren(unary);
      if (operands.size() != 1 ||
          (unary.getOperatorKind() != semantic::SVUnaryOperator::Plus &&
           unary.getOperatorKind() != semantic::SVUnaryOperator::Minus))
        return failure();
      FailureOr<ParsedConstant> value = scalarConstant(operands.front());
      if (failed(value) || value->value.getBitWidth() != *bitWidth)
        return failure();
      if (unary.getOperatorKind() == semantic::SVUnaryOperator::Minus) {
        if (value->unknown.isZero())
          value->value = -value->value;
        else {
          value->value = APInt::getZero(*bitWidth);
          value->unknown = APInt::getAllOnes(*bitWidth);
        }
      }
      return coerceConstant(source, *value);
    }
    std::optional<StringRef> spelling = getConstantSpelling(source);
    if (!spelling)
      return failure();
    FailureOr<ParsedConstant> parsed =
        parseSVInteger(*spelling, *bitWidth, getSemanticLocation(source));
    if (failed(parsed))
      return failure();
    return coerceConstant(source, *parsed);
  }

  FailureOr<ParsedConstant> coerceConstant(Operation *source,
                                           ParsedConstant value) {
    auto type = source->getAttrOfType<TypeAttr>("semantic_type");
    if (!type)
      return failure();
    FailureOr<Type> normalized = normalizeSemanticType(
        type.getValue(), getSemanticLocation(source), function);
    if (failed(normalized))
      return failure();
    if (isa<IntegerType>(sim::getPackedScalarType(*normalized))) {
      value.value &= ~value.unknown;
      value.unknown.clearAllBits();
    }
    return value;
  }

  FailureOr<APInt> constant(Operation *source) {
    FailureOr<ParsedConstant> value = scalarConstant(source);
    if (failed(value) || !value->unknown.isZero())
      return failure();
    return value->value;
  }

  // Each conversion must preserve every source bit. Shift steps additionally
  // require the declared width, so widening a signed value before shifting
  // cannot reintroduce sign bits when it is truncated back into the variable.
  Operation *selectedVariable(Operation *source, uint64_t declaredWidth,
                              bool exactWidth) const {
    while (isa<semantic::SVConversionExpressionOp>(source)) {
      auto operands = getChildren(source);
      auto convertedWidth = width(source);
      if (operands.size() != 1 || !convertedWidth ||
          (exactWidth ? *convertedWidth != declaredWidth
                      : *convertedWidth < declaredWidth))
        return nullptr;
      source = operands.front();
    }
    return isa<semantic::SVNamedValueExpressionOp>(source) &&
                   width(source) == declaredWidth
               ? symbol(source)
               : nullptr;
  }

  std::optional<uint64_t> shiftLoopBound(Operation *condition, Operation *step,
                                         Operation *body) {
    auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(step);
    auto operands = assignment ? getChildren(step) : SmallVector<Operation *>{};
    Operation *variable =
        operands.size() == 2 ? symbol(operands.front()) : nullptr;
    if (!assignment ||
        assignment.getAssignmentKind() !=
            semantic::SVAssignmentKind::Blocking ||
        assignment.getHasTimingControl() || !variable ||
        !writable.contains(variable) ||
        !isa<semantic::SVNamedValueExpressionOp>(operands.front()))
      return std::nullopt;
    uint64_t declaredWidth = width(variable).value_or(0);
    if (declaredWidth == 0)
      return std::nullopt;
    Operation *rhs = operands.back();
    while (isa<semantic::SVConversionExpressionOp>(rhs)) {
      auto inputs = getChildren(rhs);
      if (inputs.size() != 1 || width(rhs) != declaredWidth)
        return std::nullopt;
      rhs = inputs.front();
    }
    auto shift = dyn_cast<semantic::SVBinaryExpressionOp>(rhs);
    auto inputs = shift ? getChildren(shift) : SmallVector<Operation *>{};
    if (!shift ||
        shift.getOperatorKind() !=
            semantic::SVBinaryOperator::LogicalShiftRight ||
        inputs.size() != 2 || width(shift) != declaredWidth ||
        (selectedVariable(inputs.front(), declaredWidth, true) != variable &&
         !(isa<semantic::SVLValueReferenceExpressionOp>(inputs.front()) &&
           width(inputs.front()) == declaredWidth)))
      return std::nullopt;
    FailureOr<APInt> amount = constant(inputs.back());
    auto amountType = inputs.back()->getAttrOfType<TypeAttr>("semantic_type");
    if (failed(amount) || !amountType || amount->isZero() ||
        (isSignedSemanticType(amountType.getValue()) && amount->isNegative()) ||
        amount->getActiveBits() > 64)
      return std::nullopt;
    auto comparison = dyn_cast<semantic::SVBinaryExpressionOp>(condition);
    auto predicates =
        comparison ? getChildren(comparison) : SmallVector<Operation *>{};
    if (!comparison ||
        comparison.getOperatorKind() !=
            semantic::SVBinaryOperator::Inequality ||
        predicates.size() != 2)
      return std::nullopt;
    bool nonzeroTest = false;
    for (unsigned index = 0; index != 2; ++index) {
      FailureOr<APInt> zero = constant(predicates[1 - index]);
      nonzeroTest |= selectedVariable(predicates[index], declaredWidth,
                                      false) == variable &&
                     succeeded(zero) && zero->isZero();
    }
    if (!nonzeroTest)
      return std::nullopt;
    bool writesVariant = false;
    body->walk([&](Operation *node) {
      if (isa<semantic::SVAssignmentExpressionOp,
              semantic::SVUnaryExpressionOp>(node)) {
        bool update = isa<semantic::SVAssignmentExpressionOp>(node);
        if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(node))
          update |= unary.getOperatorKind() ==
                        semantic::SVUnaryOperator::Preincrement ||
                    unary.getOperatorKind() ==
                        semantic::SVUnaryOperator::Predecrement ||
                    unary.getOperatorKind() ==
                        semantic::SVUnaryOperator::Postincrement ||
                    unary.getOperatorKind() ==
                        semantic::SVUnaryOperator::Postdecrement;
        auto targets = getChildren(node);
        if (update && !targets.empty())
          targets.front()->walk([&](Operation *target) {
            writesVariant |= symbol(target) == variable;
          });
      } else if (isa<semantic::SVVariableDeclStatementOp>(node)) {
        writesVariant |= symbol(node) == variable;
      }
    });
    if (writesVariant)
      return std::nullopt;
    uint64_t shiftBits = amount->getZExtValue();
    uint64_t bound = declaredWidth / shiftBits +
                     static_cast<uint64_t>(declaredWidth % shiftBits != 0);
    return bound <= maxUnrolledIterations ? std::optional<uint64_t>(bound)
                                          : std::nullopt;
  }

  SmallVector<Operation *> expressionStatements(ArrayRef<Operation *> values) {
    SmallVector<Operation *> statements;
    for (Operation *value : values)
      statements.push_back(
          make(value, semantic::SVExpressionStatementOp::getOperationName(), {},
               {}, {value}));
    return statements;
  }

  FailureOr<Operation *> expression(Operation *source, Environment &environment,
                                    Operation *assignmentValue = nullptr) {
    if (isa<semantic::SVAssignmentExpressionOp>(source))
      return failure();
    if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(source))
      if (unary.getOperatorKind() == semantic::SVUnaryOperator::Preincrement ||
          unary.getOperatorKind() == semantic::SVUnaryOperator::Predecrement ||
          unary.getOperatorKind() == semantic::SVUnaryOperator::Postincrement ||
          unary.getOperatorKind() == semantic::SVUnaryOperator::Postdecrement)
        return failure();
    if (isa<semantic::SVLValueReferenceExpressionOp>(source))
      return assignmentValue ? FailureOr<Operation *>(assignmentValue)
                             : FailureOr<Operation *>(failure());
    if (Operation *declaration = symbol(source))
      if (Operation *value = environment.lookup(declaration))
        return value != unassignedStaticResult
                   ? FailureOr<Operation *>(value)
                   : FailureOr<Operation *>(failure());
    Operation *cloned = source->cloneWithoutRegions();
    cloned->setAttr(randomFunctionExpressionAttrName, builder.getUnitAttr());
    for (auto [sourceRegion, clonedRegion] :
         llvm::zip_equal(source->getRegions(), cloned->getRegions())) {
      for (Block &block : sourceRegion) {
        auto *target = new Block();
        clonedRegion.push_back(target);
        for (Operation &child : block) {
          FailureOr<Operation *> value =
              expression(&child, environment, assignmentValue);
          if (failed(value)) {
            cloned->destroy();
            return failure();
          }
          target->push_back((*value)->clone());
        }
      }
    }
    expressions.push_back(cloned);
    return cloned;
  }

  FailureOr<Operation *> execute(ArrayRef<Operation *> statements,
                                 Environment &environment,
                                 const Continuation &continuation,
                                 LoopContext *loop = nullptr) {
    if (statements.empty())
      return continuation(environment);
    Operation *statement = statements.front();
    Continuation next = [&](Environment &values) {
      return execute(statements.drop_front(), values, continuation, loop);
    };
    if (isa<semantic::SVStatementListOp, semantic::SVBlockStatementOp>(
            statement)) {
      if (auto block = dyn_cast<semantic::SVBlockStatementOp>(statement))
        if (block.getBlockKind() != semantic::SVStatementBlockKind::Sequential)
          return failure();
      auto children = getChildren(statement);
      return execute(children, environment, next, loop);
    }
    if (isa<semantic::SVEmptyStatementOp>(statement) ||
        statement->getName().getStringRef().starts_with("obelisk.sv.symbol."))
      return next(environment);
    if (isa<semantic::SVBreakStatementOp>(statement))
      return loop ? loop->leave(environment)
                  : FailureOr<Operation *>(failure());
    if (isa<semantic::SVContinueStatementOp>(statement))
      return loop ? loop->advance(environment)
                  : FailureOr<Operation *>(failure());
    if (auto declaration =
            dyn_cast<semantic::SVVariableDeclStatementOp>(statement)) {
      Operation *variable = symbol(statement);
      if (!variable)
        return failure();
      if (!environment.contains(variable))
        return next(environment);
      environment[variable] = initialValues.lookup(variable);
      auto initializers = getChildren(statement);
      if (initializers.empty())
        initializers = getChildren(variable);
      if (initializers.empty())
        return next(environment);
      if (initializers.size() != 1)
        return failure();
      FailureOr<Operation *> value =
          expression(initializers.front(), environment);
      if (failed(value))
        return failure();
      Operation *converted =
          convert(statement, variable->getAttrOfType<TypeAttr>("semantic_type"),
                  *value);
      environment[variable] = converted;
      FailureOr<Operation *> result = next(environment);
      return succeeded(result) ? FailureOr<Operation *>(evaluateBefore(
                                     statement, converted, *result))
                               : result;
    }
    if (isa<semantic::SVRepeatLoopStatementOp>(statement)) {
      auto children = getChildren(statement);
      if (children.size() != 2)
        return failure();
      FailureOr<ParsedConstant> count = scalarConstant(children.front());
      auto type = children.front()->getAttrOfType<TypeAttr>("semantic_type");
      if (failed(count) || !type)
        return failure();
      uint64_t iterations = 0;
      // IEEE 1800-2023 12.7.2 treats negative, X and Z counts as zero. Apply
      // intervening casts first; a two-state cast may leave known positive
      // bits after discarding its unknown bits.
      if (count->unknown.isZero() && (!isSignedSemanticType(type.getValue()) ||
                                      !count->value.isNegative())) {
        if (count->value.getActiveBits() > 64 ||
            count->value.getZExtValue() > maxUnrolledIterations)
          return failure();
        iterations = count->value.getZExtValue();
      }
      std::function<FailureOr<Operation *>(uint64_t, Environment &)> iterate;
      iterate = [&](uint64_t remaining,
                    Environment &values) -> FailureOr<Operation *> {
        if (remaining == 0)
          return next(values);
        if (remainingUnrollBudget == 0)
          return failure();
        --remainingUnrollBudget;
        Continuation advance = [&](Environment &updated) {
          return iterate(remaining - 1, updated);
        };
        LoopContext current{next, advance};
        return execute({children[1]}, values, advance, &current);
      };
      return iterate(iterations, environment);
    }
    if (auto forLoop = dyn_cast<semantic::SVForLoopStatementOp>(statement)) {
      auto children = getChildren(statement);
      uint64_t initializerCount = forLoop.getInitializerCount();
      if (!forLoop.getHasCondition() || forLoop.getStepCount() != 1 ||
          initializerCount > children.size() ||
          children.size() - initializerCount != 3)
        return failure();
      Operation *condition = children[initializerCount];
      Operation *step = children[initializerCount + 1];
      Operation *body = children.back();
      std::optional<uint64_t> bound = shiftLoopBound(condition, step, body);
      if (!bound)
        return failure();
      SmallVector<Operation *> initializers =
          expressionStatements(ArrayRef(children).take_front(initializerCount));
      SmallVector<Operation *> steps = expressionStatements({step});
      std::function<FailureOr<Operation *>(uint64_t, Environment &)> iterate;
      iterate = [&](uint64_t remaining,
                    Environment &values) -> FailureOr<Operation *> {
        // The validated step clears every bit after this many iterations.
        // The only admitted condition then evaluates false, without a read
        // that could introduce an error or a function side effect.
        if (remaining == 0)
          return next(values);
        if (remainingUnrollBudget == 0)
          return failure();
        --remainingUnrollBudget;
        FailureOr<Operation *> predicate = expression(condition, values);
        if (failed(predicate))
          return failure();
        Environment trueValues = values;
        Environment falseValues = values;
        Continuation advance = [&](Environment &updated) {
          return execute(
              steps, updated,
              [&](Environment &stepped) {
                return iterate(remaining - 1, stepped);
              },
              loop);
        };
        LoopContext current{next, advance};
        FailureOr<Operation *> yes =
            execute({body}, trueValues, advance, &current);
        FailureOr<Operation *> no = next(falseValues);
        return succeeded(yes) && succeeded(no)
                   ? FailureOr<Operation *>(
                         branch(statement, *predicate, *yes, *no))
                   : FailureOr<Operation *>(failure());
      };
      return execute(
          initializers, environment,
          [&](Environment &initialized) {
            return iterate(*bound, initialized);
          },
          loop);
    }
    if (auto ret = dyn_cast<semantic::SVReturnStatementOp>(statement)) {
      auto values = getChildren(ret);
      if (values.empty())
        return resultVariable && environment.lookup(resultVariable) !=
                                     unassignedStaticResult
                   ? FailureOr<Operation *>(environment.lookup(resultVariable))
                   : FailureOr<Operation *>(failure());
      if (values.size() != 1)
        return failure();
      FailureOr<Operation *> value = expression(values.front(), environment);
      return succeeded(value) ? FailureOr<Operation *>(
                                    convert(statement, resultType, *value))
                              : value;
    }
    if (auto caseStatement = dyn_cast<semantic::SVCaseStatementOp>(statement)) {
      // 12.5 evaluates the selector first, then the labels in source order,
      // and executes only the first matching item. Qualifier reports and
      // wildcard/pattern matching need their own staged implementation.
      if (caseStatement.getConditionKind() !=
              semantic::SVCaseCondition::Normal ||
          caseStatement.getCheckKind() != semantic::SVUniquePriorityCheck::None)
        return failure();
      auto children = getChildren(statement);
      auto labelCounts = caseStatement.getItemLabelCounts();
      uint64_t itemCount = caseStatement.getItemCount();
      uint64_t statementCount =
          itemCount + static_cast<unsigned>(caseStatement.getHasDefault());
      if (children.empty() || itemCount != labelCounts.size() ||
          statementCount > children.size() - 1)
        return failure();
      uint64_t totalLabels = 0;
      for (int64_t count : labelCounts) {
        if (count <= 0 ||
            static_cast<uint64_t>(count) > children.size() - totalLabels)
          return failure();
        totalLabels += count;
      }
      if (children.size() - 1 - statementCount != totalLabels)
        return failure();
      auto comparisons = ArrayRef(children).take_front(1 + totalLabels);
      auto bodies = ArrayRef(children).take_back(statementCount);
      uint64_t comparisonWidth = 0;
      bool comparisonSigned = true;
      bool comparisonFourState = false;
      for (Operation *operand : comparisons) {
        auto type = operand->getAttrOfType<TypeAttr>("semantic_type");
        FailureOr<Type> normalized =
            type ? normalizeSemanticType(type.getValue(),
                                         getSemanticLocation(operand), function)
                 : FailureOr<Type>(failure());
        Type scalar = succeeded(normalized)
                          ? sim::getPackedScalarType(*normalized)
                          : Type{};
        auto operandWidth = scalar ? sim::getPackedWidth(scalar) : std::nullopt;
        if (!operandWidth || *operandWidth == 0)
          return failure();
        comparisonWidth = std::max(comparisonWidth, uint64_t(*operandWidth));
        comparisonSigned &= isSignedSemanticType(type.getValue());
        comparisonFourState |= isa<sim::LogicType>(scalar);
      }
      auto integralType = [&](uint64_t bitWidth, bool fourState) {
        return TypeAttr::get(semantic::IntegralType::get(
            builder.getContext(), bitWidth, comparisonSigned, fourState,
            bitWidth - 1, 0,
            fourState ? semantic::SVIntegralFlavor::Logic
                      : semantic::SVIntegralFlavor::Bit));
      };
      TypeAttr comparisonType =
          integralType(comparisonWidth, comparisonFourState);
      auto contextValue = [&](Operation *source) -> FailureOr<Operation *> {
        // Imported implicit conversions express the common case context.
        // Reapply that context here: a narrow signed operand must zero-extend
        // when any case operand is unsigned (12.5), while explicit casts
        // retain their self-determined result.
        if (auto conversion =
                dyn_cast<semantic::SVConversionExpressionOp>(source)) {
          auto implicit = source->getAttrOfType<BoolAttr>("is_implicit");
          auto inputs = getChildren(source);
          if ((!implicit || implicit.getValue()) && inputs.size() == 1)
            source = inputs.front();
        }
        FailureOr<Operation *> value = expression(source, environment);
        auto bitWidth = width(source);
        auto type = source->getAttrOfType<TypeAttr>("semantic_type");
        FailureOr<Type> normalized =
            type ? normalizeSemanticType(type.getValue(),
                                         getSemanticLocation(source), function)
                 : FailureOr<Type>(failure());
        if (failed(value) || !bitWidth || failed(normalized))
          return failure();
        if (isa<semantic::SVUnbasedUnsizedIntegerLiteralOp>(source)) {
          // '0/'1/'x/'z fill the complete case context, including an
          // unsigned context, instead of behaving like an ordinary 1-bit
          // based literal (5.7.1).
          FailureOr<ParsedConstant> digit = scalarConstant(source);
          if (failed(digit))
            return failure();
          char fill = digit->unknown.isZero()
                          ? (digit->value.isZero() ? '0' : '1')
                          : (digit->value.isZero() ? 'x' : 'z');
          return literal(source, comparisonType,
                         std::to_string(comparisonWidth) + "'b" +
                             std::string(comparisonWidth, fill));
        }
        TypeAttr sourceContextType = integralType(
            *bitWidth,
            isa<sim::LogicType>(sim::getPackedScalarType(*normalized)));
        // An unsized based literal with an unknown high bit also fills
        // through a wider context regardless of common signedness (5.7.1).
        if (auto fillWidth = getUnsizedUnknownFillWidth(source);
            fillWidth && comparisonWidth > *fillWidth)
          sourceContextType = TypeAttr::get(semantic::IntegralType::get(
              builder.getContext(), *bitWidth, true, true, *bitWidth - 1, 0,
              semantic::SVIntegralFlavor::Logic));
        Operation *typed = convert(source, sourceContextType, *value);
        return convert(source, comparisonType, typed);
      };
      FailureOr<Operation *> selector = contextValue(comparisons.front());
      if (failed(selector))
        return failure();
      SmallVector<Operation *> labels;
      for (Operation *label : comparisons.drop_front()) {
        FailureOr<Operation *> value = contextValue(label);
        if (failed(value))
          return failure();
        labels.push_back(*value);
      }
      TypeAttr predicateType = TypeAttr::get(
          semantic::IntegralType::get(builder.getContext(), 1, false, false, 0,
                                      0, semantic::SVIntegralFlavor::Bit));
      Environment unmatchedValues = environment;
      FailureOr<Operation *> unmatched =
          caseStatement.getHasDefault()
              ? execute({bodies.back()}, unmatchedValues, next, loop)
              : next(unmatchedValues);
      if (failed(unmatched))
        return failure();
      Operation *selection = *unmatched;
      uint64_t labelIndex = totalLabels;
      for (uint64_t item = itemCount; item != 0;) {
        --item;
        Environment values = environment;
        FailureOr<Operation *> result =
            execute({bodies[item]}, values, next, loop);
        if (failed(result))
          return failure();
        // Nested conditionals preserve label short-circuiting, including
        // evaluation errors in labels after an earlier match.
        for (int64_t count = labelCounts[item]; count != 0; --count) {
          --labelIndex;
          Operation *matches = make(
              statement, semantic::SVBinaryExpressionOp::getOperationName(),
              predicateType,
              {builder.getNamedAttr(
                  "operator_kind",
                  builder.getI32IntegerAttr(static_cast<int32_t>(
                      semantic::SVBinaryOperator::CaseEquality)))},
              {*selector, labels[labelIndex]});
          selection = branch(statement, matches, *result, selection);
        }
      }
      return evaluateBefore(statement, *selector, selection);
    }
    if (auto conditional =
            dyn_cast<semantic::SVConditionalStatementOp>(statement)) {
      auto children = getChildren(statement);
      if (conditional.getConditionCount() != 1 ||
          conditional.getConditionPatternFlags().size() != 1 ||
          conditional.getConditionPatternFlags().front() != 0 ||
          children.size() !=
              2u + static_cast<unsigned>(conditional.getHasElse()))
        return failure();
      FailureOr<Operation *> condition =
          expression(children.front(), environment);
      if (failed(condition))
        return failure();
      Environment trueValues = environment;
      Environment falseValues = environment;
      FailureOr<Operation *> yes =
          execute({children[1]}, trueValues, next, loop);
      FailureOr<Operation *> no =
          conditional.getHasElse()
              ? execute({children[2]}, falseValues, next, loop)
              : next(falseValues);
      if (failed(yes) || failed(no))
        return failure();
      return branch(statement, *condition, *yes, *no);
    }
    if (auto expr = dyn_cast<semantic::SVExpressionStatementOp>(statement)) {
      auto children = getChildren(expr);
      auto assignment =
          children.size() == 1
              ? dyn_cast<semantic::SVAssignmentExpressionOp>(children.front())
              : semantic::SVAssignmentExpressionOp{};
      auto unary =
          children.size() == 1
              ? dyn_cast<semantic::SVUnaryExpressionOp>(children.front())
              : semantic::SVUnaryExpressionOp{};
      bool increment =
          unary &&
          (unary.getOperatorKind() == semantic::SVUnaryOperator::Preincrement ||
           unary.getOperatorKind() == semantic::SVUnaryOperator::Postincrement);
      bool decrement =
          unary &&
          (unary.getOperatorKind() == semantic::SVUnaryOperator::Predecrement ||
           unary.getOperatorKind() == semantic::SVUnaryOperator::Postdecrement);
      auto operands = assignment ? getChildren(assignment)
                      : unary    ? getChildren(unary)
                                 : SmallVector<Operation *>{};
      Operation *variable = operands.size() == (assignment ? 2u : 1u)
                                ? symbol(operands.front())
                                : nullptr;
      if ((!assignment && !increment && !decrement) ||
          (assignment && (assignment.getHasTimingControl() ||
                          assignment.getAssignmentKind() !=
                              semantic::SVAssignmentKind::Blocking)) ||
          !variable || !writable.contains(variable) ||
          !isa<semantic::SVNamedValueExpressionOp>(operands.front()))
        return failure();
      Operation *oldValue = environment.lookup(variable);
      if (unassignedStaticResult && oldValue == unassignedStaticResult)
        oldValue = nullptr;
      else if (!oldValue)
        oldValue = operands.front();
      auto type = variable->getAttrOfType<TypeAttr>("semantic_type");
      if (!assignment && !oldValue)
        return failure();
      FailureOr<Operation *> value =
          assignment
              ? expression(operands.back(), environment, oldValue)
              : FailureOr<Operation *>(make(
                    unary, semantic::SVBinaryExpressionOp::getOperationName(),
                    type,
                    {builder.getNamedAttr(
                        "operator_kind",
                        builder.getI32IntegerAttr(static_cast<int32_t>(
                            increment
                                ? semantic::SVBinaryOperator::Add
                                : semantic::SVBinaryOperator::Subtract)))},
                    {oldValue, literal(unary, type, "1")}));
      if (failed(value))
        return failure();
      Operation *converted =
          convert(statement, variable->getAttrOfType<TypeAttr>("semantic_type"),
                  *value);
      environment[variable] = converted;
      FailureOr<Operation *> result = next(environment);
      return succeeded(result) ? FailureOr<Operation *>(evaluateBefore(
                                     statement, converted, *result))
                               : result;
    }
    return failure();
  }

  semantic::SVSubroutineSymbolOp function;
  semantic::SVCallExpressionOp call;
  const llvm::StringMap<Operation *> &symbols;
  Builder &builder;
  Block &expressions;
  TypeAttr resultType;
  Operation *resultVariable = nullptr;
  Operation *unassignedStaticResult = nullptr;
  Environment initialValues;
  uint64_t remainingUnrollBudget = 4096;
  llvm::SmallPtrSet<Operation *, 16> writable;
};

} // namespace

static FailureOr<Operation *>
getConstraintFunctionResult(semantic::SVSubroutineSymbolOp function,
                            semantic::SVCallExpressionOp call,
                            const llvm::StringMap<Operation *> &symbols,
                            Builder &builder, Block &expressions) {
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
    if (auto block = dyn_cast<semantic::SVBlockStatementOp>(nested))
      unsupportedStatement |=
          block.getBlockKind() != semantic::SVStatementBlockKind::Sequential;
    if (name.starts_with("obelisk.sv.statement.") &&
        !isa<semantic::SVStatementListOp, semantic::SVBlockStatementOp>(nested))
      unsupportedStatement = true;
  });
  if (unsupportedLocal || unsupportedStatement) {
    FailureOr<Operation *> result =
        ConstraintFunctionBody(function, call, symbols, builder, expressions)
            .compile();
    if (succeeded(result))
      return result;
    emitError(getSemanticLocation(call))
        << "constraint function " << function.getName().value_or("")
        << " must be a side-effect-free expression function or an admitted "
           "automatic statement body";
    return failure();
  }
  auto requiresStatementEvaluation = [&](Operation *expression) {
    bool required = false;
    expression->walk([&](Operation *node) {
      required |= isa<semantic::SVAssignmentExpressionOp>(node);
      if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(node))
        required |=
            unary.getOperatorKind() ==
                semantic::SVUnaryOperator::Preincrement ||
            unary.getOperatorKind() ==
                semantic::SVUnaryOperator::Predecrement ||
            unary.getOperatorKind() ==
                semantic::SVUnaryOperator::Postincrement ||
            unary.getOperatorKind() == semantic::SVUnaryOperator::Postdecrement;
      if (auto reference =
              node->getAttrOfType<SymbolRefAttr>("referenced_symbol"))
        required |= function.getReturnVariableSymbol() &&
                    reference.getLeafReference() ==
                        function.getReturnVariableSymbol()->getLeafReference();
    });
    return required;
  };
  if (returns.size() == 1 && expressionStatements.empty()) {
    SmallVector<Operation *> values = getChildren(returns.front());
    if (values.size() == 1 && !requiresStatementEvaluation(values.front()))
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
    if (assignment && !assignment.getHasTimingControl() &&
        assignment.getAssignmentKind() ==
            semantic::SVAssignmentKind::Blocking &&
        function.getReturnVariableSymbol() && lhs &&
        lhs.getLeafReference() ==
            function.getReturnVariableSymbol()->getLeafReference() &&
        !requiresStatementEvaluation(operands.back()))
      return operands.back();
  }
  FailureOr<Operation *> result =
      ConstraintFunctionBody(function, call, symbols, builder, expressions)
          .compile();
  if (succeeded(result))
    return result;
  emitError(getSemanticLocation(call))
      << "constraint function " << function.getName().value_or("")
      << " must be a side-effect-free expression function or an admitted "
         "automatic statement body";
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

      auto functionExpressions = std::make_unique<Block>();
      FailureOr<Operation *> result = getConstraintFunctionResult(
          *function, callExpression, semanticSymbols, builder,
          *functionExpressions);
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
