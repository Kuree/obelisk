//===- RandomArithmetic.cpp - Compiler-only four-state arithmetic planes ===//

#include "RandomArithmetic.h"
#include "Detail.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk::simlowering {

struct RandomArithmetic::Impl {
  using Binary = semantic::SVBinaryOperator;
  using Unary = semantic::SVUnaryOperator;
  using Plane = RandomArithmeticPlane;
  Builder builder;
  Width width;
  Candidate candidate;
  Binding binding;
  bool frozenVariables;
  // Temporary expressions retain the target layout used by their original
  // module, including layouts of unpacked values beneath a packed selection.
  OwningOpRef<ModuleOp> expressions;
  SmallVector<Operation *> nodes;
  llvm::DenseMap<Operation *, bool> needs;
  llvm::DenseMap<Operation *, RandomArithmeticPlanes> planes;
  llvm::DenseMap<Operation *, Operation *> knownPredicates;
  llvm::DenseMap<Operation *, std::optional<APInt>> constants;
  struct CanonicalExpression : llvm::FoldingSetNode {
    Operation *expression;
    DictionaryAttr attributes;
    SmallVector<Operation *> children;
    void Profile(llvm::FoldingSetNodeID &id) const {
      id.AddString(expression->getName().getStringRef());
      id.AddPointer(attributes.getAsOpaquePointer());
      for (Operation *child : children)
        id.AddPointer(child);
    }
  };
  llvm::FoldingSet<CanonicalExpression> canonicalExpressions;
  SmallVector<std::unique_ptr<CanonicalExpression>> canonicalNodes;
  llvm::DenseMap<Operation *, Operation *> canonicalValues;

  Operation *canonical(Operation *expression) {
    if (auto found = canonicalValues.find(expression);
        found != canonicalValues.end())
      return found->second;
    auto node = std::make_unique<CanonicalExpression>();
    node->expression = expression;
    node->attributes = expression->getAttrDictionary();
    for (Operation *child : getChildren(expression))
      node->children.push_back(canonical(child));
    llvm::FoldingSetNodeID id;
    node->Profile(id);
    void *position;
    CanonicalExpression *found =
        canonicalExpressions.FindNodeOrInsertPos(id, position);
    Operation *value = found ? found->expression : expression;
    if (!found) {
      canonicalExpressions.InsertNode(node.get(), position);
      canonicalNodes.push_back(std::move(node));
    }
    canonicalValues[expression] = value;
    return value;
  }

  Impl(MLIRContext *context, Width width, Candidate candidate, Binding binding,
       bool frozenVariables)
      : builder(context), width(std::move(width)),
        candidate(std::move(candidate)), binding(std::move(binding)),
        frozenVariables(frozenVariables),
        expressions(ModuleOp::create(UnknownLoc::get(context))) {}
  ~Impl() {
    for (Operation *node : llvm::reverse(nodes))
      node->erase();
  }

  bool sign(Operation *source) const {
    auto type = source->getAttrOfType<TypeAttr>("semantic_type");
    return type && isSignedSemanticType(type.getValue());
  }
  bool fourState(Operation *source) const {
    auto type = source->getAttrOfType<TypeAttr>("semantic_type");
    if (!type)
      return false;
    Type element = type.getValue();
    while (true) {
      if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(element))
        element = array.getElementType();
      else if (auto array = dyn_cast<semantic::UnpackedArrayType>(element))
        element = array.getElementType();
      else
        return isFourStateSemanticType(element);
    }
  }
  Type type(unsigned precision, bool isSigned = false) {
    return semantic::IntegralType::get(builder.getContext(), precision,
                                       isSigned, false, precision - 1, 0,
                                       semantic::SVIntegralFlavor::Bit);
  }
  Operation *make(Operation *source, StringRef name, unsigned precision,
                  bool isSigned, ArrayRef<NamedAttribute> attributes,
                  ArrayRef<Operation *> operands) {
    OperationState state(source->getLoc(), name);
    state.addAttribute("node_id", source->getAttr("node_id"));
    state.addAttribute("semantic_type",
                       TypeAttr::get(type(precision, isSigned)));
    state.addAttribute(randomArithmeticNodeAttr, builder.getUnitAttr());
    if (expressionIsFunction(source))
      state.addAttribute(randomFunctionExpressionAttrName,
                         builder.getUnitAttr());
    state.addAttributes(attributes);
    auto *region = state.addRegion();
    region->push_back(new Block());
    for (Operation *operand : operands)
      region->front().push_back(operand->clone());
    Operation *node = Operation::create(state);
    if (auto module = source->getParentOfType<ModuleOp>())
      if (auto layout = module->getAttr("llvm.data_layout"))
        expressions->getOperation()->setAttr("llvm.data_layout", layout);
    expressions->getBody()->push_back(node);
    nodes.push_back(node);
    return node;
  }
  bool expressionIsFunction(Operation *source) const {
    return source->hasAttr(randomFunctionExpressionAttrName);
  }
  Operation *literal(Operation *source, const APInt &bits,
                     bool isSigned = false) {
    SmallString<64> digits;
    bits.toString(digits, 16, false);
    std::string spelling =
        std::to_string(bits.getBitWidth()) + "'h" + digits.str().str();
    return make(source, semantic::SVIntegerLiteralOp::getOperationName(),
                bits.getBitWidth(), isSigned,
                {builder.getNamedAttr("constant_value",
                                      builder.getStringAttr(spelling))},
                {});
  }
  Operation *zero(Operation *source, unsigned precision) {
    return literal(source, APInt::getZero(precision));
  }
  Operation *ones(Operation *source, unsigned precision) {
    return literal(source, APInt::getAllOnes(precision));
  }
  std::optional<APInt> constant(Operation *expression) {
    Operation *key = canonical(expression);
    if (auto found = constants.find(key); found != constants.end())
      return found->second;
    auto value = constantValue(expression);
    constants[key] = value;
    return value;
  }
  std::optional<APInt> constantValue(Operation *expression) {
    auto spelling = getConstantSpelling(expression);
    auto precision = width(expression);
    if (failed(precision))
      return std::nullopt;
    if (!spelling) {
      auto children = getChildren(expression);
      if (isa<semantic::SVConversionExpressionOp>(expression) &&
          children.size() == 1) {
        auto bits = constant(children.front());
        if (bits)
          return sign(children.front()) ? bits->sextOrTrunc(*precision)
                                        : bits->zextOrTrunc(*precision);
      }
      return std::nullopt;
    }
    auto value = parseSVInteger(*spelling, *precision, expression->getLoc());
    if (failed(value) || !value->unknown.isZero())
      return std::nullopt;
    return value->value;
  }
  bool isZero(Operation *expression) {
    if (!expression)
      return true;
    auto bits = constant(expression);
    return bits && bits->isZero();
  }
  bool isOnes(Operation *expression) {
    auto bits = expression ? constant(expression) : std::nullopt;
    return bits && bits->isAllOnes();
  }
  Operation *binary(Operation *source, Binary kind, Operation *left,
                    Operation *right, unsigned precision,
                    bool isSigned = false) {
    return make(source, semantic::SVBinaryExpressionOp::getOperationName(),
                precision, isSigned,
                {builder.getNamedAttr(
                    "operator_kind",
                    builder.getI32IntegerAttr(static_cast<int32_t>(kind)))},
                {left, right});
  }
  Operation *unary(Operation *source, Unary kind, Operation *value,
                   unsigned precision, bool isSigned = false) {
    return make(source, semantic::SVUnaryExpressionOp::getOperationName(),
                precision, isSigned,
                {builder.getNamedAttr(
                    "operator_kind",
                    builder.getI32IntegerAttr(static_cast<int32_t>(kind)))},
                {value});
  }
  Operation *cast(Operation *source, Operation *value, unsigned precision,
                  bool signedInput = false) {
    auto sourceType = value->getAttrOfType<TypeAttr>("semantic_type");
    bool implicit = !sourceType ||
                    getSemanticPackedWidth(sourceType.getValue()).has_value();
    return make(
        source, semantic::SVConversionExpressionOp::getOperationName(),
        precision, signedInput,
        {builder.getNamedAttr("is_implicit", builder.getBoolAttr(implicit))},
        {value});
  }
  Operation *select(Operation *source, Operation *condition, Operation *yes,
                    Operation *no, unsigned precision, bool isSigned = false) {
    if (auto bits = constant(condition))
      return bits->isZero() ? no : yes;
    return make(
        source, semantic::SVConditionalExpressionOp::getOperationName(),
        precision, isSigned,
        {builder.getNamedAttr("condition_count", builder.getI64IntegerAttr(1)),
         builder.getNamedAttr("condition_pattern_flags",
                              builder.getDenseI64ArrayAttr({0}))},
        {condition, yes, no});
  }
  Operation *bitAnd(Operation *source, Operation *a, Operation *b,
                    unsigned precision) {
    if (isZero(a) || isZero(b))
      return zero(source, precision);
    if (isOnes(a))
      return b;
    if (isOnes(b))
      return a;
    return binary(source, Binary::BinaryAnd, a, b, precision);
  }
  Operation *bitOr(Operation *source, Operation *a, Operation *b,
                   unsigned precision) {
    if (isZero(a))
      return b ? b : zero(source, precision);
    if (isZero(b))
      return a;
    if (isOnes(a))
      return a;
    if (isOnes(b))
      return b;
    return binary(source, Binary::BinaryOr, a, b, precision);
  }
  Operation *bitNot(Operation *source, Operation *value, unsigned precision) {
    if (!value)
      return ones(source, precision);
    if (auto bits = constant(value))
      return literal(source, ~*bits);
    return unary(source, Unary::BitwiseNot, value, precision);
  }
  Operation *nonzero(Operation *source, Operation *value, unsigned precision) {
    if (isZero(value))
      return zero(source, 1);
    if (auto bits = constant(value))
      return literal(source, APInt(1, !bits->isZero()));
    return binary(source, Binary::Inequality, value, zero(source, precision),
                  1);
  }
  Operation *notBool(Operation *source, Operation *value) {
    if (auto bits = constant(value))
      return literal(source, APInt(1, bits->isZero()));
    return unary(source, Unary::LogicalNot, value, 1);
  }
  Operation *unknownMask(Operation *source, Operation *condition,
                         unsigned precision) {
    return select(source, condition, ones(source, precision),
                  zero(source, precision), precision);
  }
  Operation *mask(Operation *source, Operation *value, Operation *unknown,
                  unsigned precision, bool isSigned = false) {
    if (isZero(unknown))
      return sign(value) == isSigned ? value
                                     : cast(source, value, precision, isSigned);
    Operation *result =
        bitAnd(source, value, bitNot(source, unknown, precision), precision);
    return isSigned ? cast(source, result, precision, true) : result;
  }
  RandomArithmeticPlanes resize(Operation *source, RandomArithmeticPlanes value,
                                unsigned precision, bool signedInput) {
    if (*width(value.value) != precision || sign(value.value) != signedInput)
      value.value = cast(source, value.value, precision, signedInput);
    if (value.unknown) {
      unsigned inputWidth = *width(value.unknown);
      if (sign(value.unknown) != signedInput)
        value.unknown = cast(source, value.unknown, inputWidth, signedInput);
      if (inputWidth != precision)
        value.unknown = cast(source, value.unknown, precision, signedInput);
    }
    if (value.highImpedance) {
      unsigned inputWidth = *width(value.highImpedance);
      if (sign(value.highImpedance) != signedInput)
        value.highImpedance =
            cast(source, value.highImpedance, inputWidth, signedInput);
      if (inputWidth != precision)
        value.highImpedance =
            cast(source, value.highImpedance, precision, signedInput);
    }
    return value;
  }
  Operation *projection(Operation *source, unsigned precision, Plane plane) {
    return make(source, semantic::SVConversionExpressionOp::getOperationName(),
                precision, plane == Plane::Value && sign(source),
                {builder.getNamedAttr("is_implicit", builder.getBoolAttr(true)),
                 builder.getNamedAttr(
                     randomArithmeticPlaneAttr,
                     builder.getI32IntegerAttr(static_cast<int32_t>(plane)))},
                {source});
  }
  Operation *integralCall(Operation *source, StringRef name, Operation *input,
                          unsigned precision, bool isSigned = false) {
    Operation *node = source->clone();
    expressions->getBody()->push_back(node);
    nodes.push_back(node);
    node->setAttr(randomArithmeticNodeAttr, builder.getUnitAttr());
    node->setAttr("semantic_type", TypeAttr::get(type(precision, isSigned)));
    node->setAttr("callee_name", builder.getStringAttr(name));
    node->setAttr("argument_count", builder.getI64IntegerAttr(1));
    node->setAttr("defaulted_arguments", builder.getDenseI64ArrayAttr({0}));
    node->getRegion(0).front().clear();
    node->getRegion(0).front().push_back(input->clone());
    return node;
  }
  bool needed(Operation *expression) {
    if (expression->hasAttr(randomArithmeticNodeAttr))
      return false;
    auto semanticType = expression->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType || !getSemanticPackedWidth(semanticType.getValue()))
      return false;
    Operation *key = canonical(expression);
    if (auto found = needs.find(key); found != needs.end())
      return found->second;
    bool result = expression->hasAttr(randomFunctionCaseConditionAttrName) ||
                  (frozenVariables && fourState(expression) &&
                   (expression->hasAttr(randomVariableAttrName) ||
                    expression->hasAttr(randomVariableBitOffsetAttrName)));
    // State-selected integral paths must remain symbolic instead of capturing
    // their current element before randomization. Their finite selection is a
    // compiler expression, including when the selected data is two-state.
    if (candidate(expression) &&
        isa<semantic::SVElementSelectExpressionOp,
            semantic::SVRangeSelectExpressionOp>(expression) &&
        !expression->hasAttr(randomVariableBitOffsetAttrName))
      result = true;
    if (auto op = dyn_cast<semantic::SVBinaryExpressionOp>(expression)) {
      auto children = getChildren(expression);
      if (children.size() == 2) {
        if (op.getOperatorKind() == Binary::Divide ||
            op.getOperatorKind() == Binary::Mod) {
          auto bits = constant(children[1]);
          auto precision = width(expression);
          result = !bits || failed(precision) ||
                   bits->zextOrTrunc(*precision).isZero();
        } else if (op.getOperatorKind() == Binary::Power && sign(children[1])) {
          auto base = constant(children[0]);
          auto exponent = constant(children[1]);
          result = (!base || base->isZero()) &&
                   (!exponent || exponent->isNegative());
        }
      }
    }
    if (!candidate(expression) && fourState(expression) &&
        (!getConstantSpelling(expression) || expressionIsFunction(expression)))
      result = true;
    auto children = getChildren(expression);
    auto call = dyn_cast<semantic::SVCallExpressionOp>(expression);
    // countbits control literals may legally name X and Z. They describe the
    // symbols to count and do not contribute unknown result bits.
    if (call && call.getIsSystemCall() &&
        call.getCalleeName() == "$countbits") {
      if (!children.empty())
        result |= needed(children.front());
      for (Operation *control : ArrayRef(children).drop_front())
        if (!getConstantSpelling(control))
          result |= needed(control);
    } else {
      for (Operation *child : children)
        result |= needed(child);
    }
    needs[key] = result;
    return result;
  }
  RandomArithmeticPlanes truth(Operation *source,
                               RandomArithmeticPlanes input) {
    unsigned precision = *width(input.value);
    Operation *value = nonzero(source, input.value, precision);
    Operation *unknown =
        input.unknown ? bitAnd(source, notBool(source, value),
                               nonzero(source, input.unknown, precision), 1)
                      : nullptr;
    return {value, unknown};
  }

  FailureOr<RandomArithmeticPlanes> get(Operation *expression) {
    Operation *key = canonical(expression);
    if (auto found = planes.find(key); found != planes.end())
      return found->second;
    auto result = build(expression);
    if (succeeded(result)) {
      if (isZero(result->unknown))
        result->unknown = nullptr;
      if (isZero(result->highImpedance))
        result->highImpedance = nullptr;
      planes[key] = *result;
    }
    return result;
  }

  /// Expand a fixed random value path over its legal state-selected indices.
  /// Each terminal path has an exact property bit binding. In particular, a
  /// disabled selected element is STATE even if another element is active.
  /// Z3 compiles these conditionals; no selection solver runs on the target.
  FailureOr<std::optional<RandomArithmeticPlanes>>
  fixedSelection(Operation *expression, unsigned precision) {
    Operation *root = expression;
    while (!root->hasAttr(randomVariableAttrName) &&
           !root->hasAttr(randomVariableBitOffsetAttrName)) {
      auto operands = getChildren(root);
      if (!isa<semantic::SVElementSelectExpressionOp,
               semantic::SVRangeSelectExpressionOp,
               semantic::SVMemberAccessExpressionOp>(root) ||
          operands.empty())
        return std::optional<RandomArithmeticPlanes>{};
      root = operands.front();
    }
    Operation *selection = expression;
    SmallVector<unsigned> path;
    while (true) {
      auto operands = getChildren(selection);
      if (isa<semantic::SVElementSelectExpressionOp>(selection) &&
          operands.size() == 2 && !constant(operands[1]))
        break;
      if (auto range = dyn_cast<semantic::SVRangeSelectExpressionOp>(selection);
          range && operands.size() == 3 &&
          range.getSelectionKind() != semantic::SVRangeSelectionKind::Simple &&
          !constant(operands[1]))
        break;
      if (!isa<semantic::SVElementSelectExpressionOp,
               semantic::SVRangeSelectExpressionOp,
               semantic::SVMemberAccessExpressionOp>(selection) ||
          operands.empty())
        return std::optional<RandomArithmeticPlanes>{};
      path.push_back(0);
      selection = operands.front();
    }
    auto selectedOperands = getChildren(selection);
    auto sourceType =
        selectedOperands.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!sourceType)
      return failure();
    auto layout = getSemanticSelectionLayout(sourceType.getValue());
    if (!layout)
      return std::optional<RandomArithmeticPlanes>{};
    int64_t left = layout->left, right = layout->right;
    auto sourceWidth = width(selectedOperands.front());
    APInt extent(65, static_cast<uint64_t>(left), true);
    extent -= APInt(65, static_cast<uint64_t>(right), true);
    if (extent.isNegative())
      extent = -extent;
    ++extent;
    if (failed(sourceWidth) || extent.getActiveBits() > 32 || extent.isZero() ||
        extent.ugt(*sourceWidth))
      return failure();

    auto index = get(selectedOperands[1]);
    auto indexWidth = width(selectedOperands[1]);
    if (failed(index) || failed(indexWidth) ||
        *indexWidth == std::numeric_limits<unsigned>::max())
      return failure();
    unsigned comparisonWidth =
        std::max(64u, *indexWidth + unsigned(!sign(selectedOperands[1])));
    auto widened =
        resize(expression, *index, comparisonWidth, sign(selectedOperands[1]));
    Operation *signedIndex =
        cast(expression, widened.value, comparisonWidth, true);
    SmallVector<std::pair<int64_t, RandomArithmeticPlanes>> alternatives;
    int64_t first = std::min(left, right), last = std::max(left, right);
    for (int64_t selected = first;; ++selected) {
      Operation *branch = expression->clone();
      expressions->getBody()->push_back(branch);
      nodes.push_back(branch);
      Operation *branchSelection = branch;
      for (unsigned child : path)
        branchSelection = getChildren(branchSelection)[child];
      Operation *oldIndex = getChildren(branchSelection)[1];
      Operation *knownIndex = literal(
          expression, APInt(65, static_cast<uint64_t>(selected), true), true);
      oldIndex->getBlock()->getOperations().insert(oldIndex->getIterator(),
                                                   knownIndex->clone());
      oldIndex->erase();
      // Every now-constant intermediate selection needs its own binding too:
      // safe reads and frozen-plane captures visit the entire address path.
      for (Operation *boundPath = branch;;) {
        auto offset = binding(boundPath);
        if (failed(offset))
          return failure();
        if (*offset)
          boundPath->setAttr(randomVariableBitOffsetAttrName,
                             builder.getI64IntegerAttr(**offset));
        auto operands = getChildren(boundPath);
        if (!isa<semantic::SVElementSelectExpressionOp,
                 semantic::SVRangeSelectExpressionOp,
                 semantic::SVMemberAccessExpressionOp>(boundPath) ||
            operands.empty())
          break;
        boundPath = operands.front();
      }
      auto data = get(branch);
      if (failed(data))
        return failure();
      alternatives.push_back({selected, *data});
      if (selected == last)
        break;
    }
    // A balanced tree keeps the expression depth logarithmic. Semantic AST
    // operands are cloned, so a linear chain would also copy preceding arms
    // quadratically as the fixed dimension grows.
    std::function<RandomArithmeticPlanes(unsigned, unsigned)> combine =
        [&](unsigned begin, unsigned end) -> RandomArithmeticPlanes {
      if (end - begin == 1)
        return alternatives[begin].second;
      unsigned middle = begin + (end - begin) / 2;
      auto lower = combine(begin, middle), upper = combine(middle, end);
      Operation *condition = binary(
          expression, Binary::LessThan, signedIndex,
          literal(expression,
                  APInt(comparisonWidth,
                        static_cast<uint64_t>(alternatives[middle].first),
                        true),
                  true),
          1);
      auto selectPlane = [&](Operation *yes, Operation *no) -> Operation * {
        if (!yes && !no)
          return nullptr;
        return select(expression, condition,
                      yes ? yes : zero(expression, precision),
                      no ? no : zero(expression, precision), precision);
      };
      return {select(expression, condition, lower.value, upper.value, precision,
                     sign(expression)),
              selectPlane(lower.unknown, upper.unknown),
              selectPlane(lower.highImpedance, upper.highImpedance)};
    };
    auto result = combine(0, alternatives.size());
    auto boundary = [&](int64_t value) {
      return literal(expression,
                     APInt(comparisonWidth, static_cast<uint64_t>(value), true),
                     true);
    };
    Operation *inRange = bitAnd(expression,
                                binary(expression, Binary::GreaterThanEqual,
                                       signedIndex, boundary(first), 1),
                                binary(expression, Binary::LessThanEqual,
                                       signedIndex, boundary(last), 1),
                                1);
    result.value =
        select(expression, inRange, result.value, zero(expression, precision),
               precision, sign(expression));
    if (result.unknown || fourState(expression))
      result.unknown =
          select(expression, inRange,
                 result.unknown ? result.unknown : zero(expression, precision),
                 fourState(expression) ? ones(expression, precision)
                                       : zero(expression, precision),
                 precision);
    if (result.highImpedance)
      result.highImpedance = select(expression, inRange, result.highImpedance,
                                    zero(expression, precision), precision);
    if (index->unknown) {
      Operation *invalid = nonzero(expression, index->unknown, *indexWidth);
      result.value = select(expression, invalid, zero(expression, precision),
                            result.value, precision, sign(expression));
      result.unknown =
          select(expression, invalid, ones(expression, precision),
                 result.unknown ? result.unknown : zero(expression, precision),
                 precision);
      if (result.highImpedance)
        result.highImpedance =
            select(expression, invalid, zero(expression, precision),
                   result.highImpedance, precision);
    }
    return std::optional<RandomArithmeticPlanes>(result);
  }

  FailureOr<RandomArithmeticPlanes> packedSelection(Operation *expression,
                                                    unsigned precision) {
    auto operands = getChildren(expression);
    bool element = isa<semantic::SVElementSelectExpressionOp>(expression);
    if (operands.size() != (element ? 2u : 3u))
      return failure();
    auto sourceType = operands[0]->getAttrOfType<TypeAttr>("semantic_type");
    auto sourceWidth = sourceType
                           ? getSemanticPackedWidth(sourceType.getValue())
                           : std::nullopt;
    auto layout = sourceType ? getSemanticSelectionLayout(sourceType.getValue())
                             : std::nullopt;
    if (!sourceWidth || !layout)
      return failure();
    auto source = get(operands[0]), first = get(operands[1]);
    auto firstWidth = width(operands[1]);
    if (failed(source) || failed(first) || failed(firstWidth))
      return failure();
    unsigned scaleBits = APInt(64, layout->elementWidth - 1).getActiveBits();
    unsigned indexWidth = std::max(64u, *firstWidth);
    auto range = dyn_cast<semantic::SVRangeSelectExpressionOp>(expression);
    bool simple = range && range.getSelectionKind() ==
                               semantic::SVRangeSelectionKind::Simple;
    if (simple) {
      auto secondWidth = width(operands[2]);
      if (failed(secondWidth) || !constant(operands[1]) ||
          !constant(operands[2]))
        return failure();
      indexWidth = std::max(indexWidth, *secondWidth);
    } else if (range) {
      auto count = constant(operands[2]);
      if (!count || count->isZero() ||
          (sign(operands[2]) && count->isNegative()) ||
          count->getActiveBits() > 32 ||
          uint64_t(count->getZExtValue()) * layout->elementWidth != precision)
        return failure();
    }
    if (indexWidth > std::numeric_limits<unsigned>::max() - 2 - scaleBits)
      return failure();
    unsigned arithmeticWidth = indexWidth + 2 + scaleBits;
    auto number = [&](int64_t value) {
      return literal(expression,
                     APInt(arithmeticWidth, static_cast<uint64_t>(value), true),
                     true);
    };
    auto offset = [&](Operation *indexSource,
                      RandomArithmeticPlanes index) -> Operation * {
      auto widened =
          resize(expression, index, arithmeticWidth, sign(indexSource));
      Operation *selected =
          cast(expression, widened.value, arithmeticWidth, true);
      Operation *right = number(layout->right);
      Operation *ordinal =
          binary(expression, Binary::Subtract,
                 layout->left >= layout->right ? selected : right,
                 layout->left >= layout->right ? right : selected,
                 arithmeticWidth, true);
      if (layout->elementWidth == 1)
        return ordinal;
      return binary(expression, Binary::Multiply, ordinal,
                    number(layout->elementWidth), arithmeticWidth, true);
    };
    Operation *low = offset(operands[1], *first);
    if (simple) {
      auto second = get(operands[2]);
      if (failed(second))
        return failure();
      Operation *other = offset(operands[2], *second);
      low = select(expression,
                   binary(expression, Binary::LessThan, low, other, 1), low,
                   other, arithmeticWidth, true);
    } else if (range) {
      bool high = (layout->left >= layout->right &&
                   range.getSelectionKind() ==
                       semantic::SVRangeSelectionKind::IndexedDown) ||
                  (layout->left < layout->right &&
                   range.getSelectionKind() ==
                       semantic::SVRangeSelectionKind::IndexedUp);
      if (high && precision > layout->elementWidth)
        low = binary(expression, Binary::Subtract, low,
                     number(precision - layout->elementWidth), arithmeticWidth,
                     true);
    }
    Operation *inRange =
        precision > *sourceWidth
            ? zero(expression, 1)
            : bitAnd(expression,
                     binary(expression, Binary::GreaterThanEqual, low,
                            number(0), 1),
                     binary(expression, Binary::LessThanEqual, low,
                            number(*sourceWidth - precision), 1),
                     1);
    if (first->unknown)
      inRange = bitAnd(
          expression, inRange,
          notBool(expression, nonzero(expression, first->unknown, *firstWidth)),
          1);
    auto extract = [&](Operation *plane) -> Operation * {
      return plane ? cast(expression,
                          binary(expression, Binary::LogicalShiftRight, plane,
                                 low, *sourceWidth),
                          precision)
                   : nullptr;
    };
    Operation *value = extract(source->value);
    Operation *unknown = extract(source->unknown);
    Operation *highImpedance = extract(source->highImpedance);
    value = select(expression, inRange, value, zero(expression, precision),
                   precision, sign(expression));
    if (unknown || fourState(expression))
      unknown = select(expression, inRange,
                       unknown ? unknown : zero(expression, precision),
                       fourState(expression) ? ones(expression, precision)
                                             : zero(expression, precision),
                       precision);
    if (highImpedance)
      highImpedance = select(expression, inRange, highImpedance,
                             zero(expression, precision), precision);
    return RandomArithmeticPlanes{
        mask(expression, value, unknown, precision, sign(expression)), unknown,
        highImpedance};
  }

  RandomArithmeticPlanes symbolComparison(Operation *expression, Binary kind,
                                          RandomArithmeticPlanes left,
                                          RandomArithmeticPlanes right,
                                          unsigned operationWidth) {
    bool caseComparison =
        kind == Binary::CaseEquality || kind == Binary::CaseInequality;
    Operation *unknown = nullptr;
    Operation *value = nullptr;
    auto plane = [&](Operation *value) {
      return value ? value : zero(expression, operationWidth);
    };
    Operation *different = binary(expression, Binary::BinaryXor, left.value,
                                  right.value, operationWidth);
    if (caseComparison) {
      // Case equality consumes complete symbols and always produces a
      // known predicate. casex/casez ignore the selected symbol in either
      // operand, unlike ==? which takes wildcards only from its RHS.
      different =
          bitOr(expression, different,
                binary(expression, Binary::BinaryXor, plane(left.unknown),
                       plane(right.unknown), operationWidth),
                operationWidth);
      different =
          bitOr(expression, different,
                binary(expression, Binary::BinaryXor, plane(left.highImpedance),
                       plane(right.highImpedance), operationWidth),
                operationWidth);
      if (auto condition = expression->getAttrOfType<IntegerAttr>(
              randomFunctionCaseConditionAttrName)) {
        auto kind = static_cast<semantic::SVCaseCondition>(condition.getInt());
        if (kind == semantic::SVCaseCondition::WildcardXOrZ)
          different = mask(
              expression, different,
              bitOr(expression, left.unknown, right.unknown, operationWidth),
              operationWidth);
        else if (kind == semantic::SVCaseCondition::WildcardJustZ)
          different = mask(expression, different,
                           bitOr(expression, left.highImpedance,
                                 right.highImpedance, operationWidth),
                           operationWidth);
      }
      value = nonzero(expression, different, operationWidth);
      if (kind == Binary::CaseEquality)
        value = notBool(expression, value);
      return RandomArithmeticPlanes{value, nullptr};
    }
    Operation *mismatch = nonzero(
        expression,
        mask(expression, different,
             bitOr(expression, left.unknown, right.unknown, operationWidth),
             operationWidth),
        operationWidth);
    Operation *unmatchedUnknown =
        mask(expression, plane(left.unknown), right.unknown, operationWidth);
    unknown = bitAnd(expression,
                     nonzero(expression, unmatchedUnknown, operationWidth),
                     notBool(expression, mismatch), 1);
    value = kind == Binary::WildcardEquality
                ? notBool(expression, bitOr(expression, mismatch, unknown, 1))
                : mismatch;
    return RandomArithmeticPlanes{mask(expression, value, unknown, 1), unknown};
  }

  FailureOr<RandomArithmeticPlanes> inside(Operation *expression) {
    auto operands = getChildren(expression);
    if (operands.size() < 2)
      return failure();
    auto input = get(operands.front());
    if (failed(input))
      return failure();
    RandomArithmeticPlanes result{zero(expression, 1), nullptr};
    for (Operation *item : ArrayRef(operands).drop_front()) {
      RandomArithmeticPlanes match;
      if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(item)) {
        auto bounds = getChildren(item);
        if (range.getRangeKind() != semantic::SVValueRangeKind::Simple ||
            bounds.size() != 2)
          return failure();
        bool unboundedLow = isUnboundedEndpoint(bounds[0]);
        bool unboundedHigh = isUnboundedEndpoint(bounds[1]);
        if (unboundedLow && unboundedHigh)
          return failure();
        auto low =
            unboundedLow
                ? FailureOr<RandomArithmeticPlanes>(
                      RandomArithmeticPlanes{zero(expression, 1), nullptr})
                : get(bounds[0]);
        auto high =
            unboundedHigh
                ? FailureOr<RandomArithmeticPlanes>(
                      RandomArithmeticPlanes{zero(expression, 1), nullptr})
                : get(bounds[1]);
        if (failed(low) || failed(high))
          return failure();
        unsigned precision = std::max(
            {*width(input->value), *width(low->value), *width(high->value)});
        bool commonSigned = sign(operands.front()) &&
                            (unboundedLow || sign(bounds[0])) &&
                            (unboundedHigh || sign(bounds[1]));
        auto value = resize(expression, *input, precision, commonSigned);
        auto lower = resize(expression, *low, precision, commonSigned);
        auto upper = resize(expression, *high, precision, commonSigned);
        auto compare = [&](Binary kind, RandomArithmeticPlanes bound) {
          Operation *unknown = nonzero(
              expression,
              bitOr(expression, value.unknown, bound.unknown, precision),
              precision);
          Operation *bits =
              binary(expression, kind, value.value, bound.value, 1);
          return RandomArithmeticPlanes{mask(expression, bits, unknown, 1),
                                        unknown};
        };
        auto lowerMatch =
            unboundedLow ? RandomArithmeticPlanes{ones(expression, 1), nullptr}
                         : compare(Binary::GreaterThanEqual, lower);
        auto upperMatch =
            unboundedHigh ? RandomArithmeticPlanes{ones(expression, 1), nullptr}
                          : compare(Binary::LessThanEqual, upper);
        match.value = bitAnd(expression, lowerMatch.value, upperMatch.value, 1);
        match.unknown = bitOr(
            expression,
            bitAnd(expression, lowerMatch.unknown,
                   bitOr(expression, upperMatch.value, upperMatch.unknown, 1),
                   1),
            bitAnd(expression, upperMatch.unknown,
                   bitOr(expression, lowerMatch.value, lowerMatch.unknown, 1),
                   1),
            1);
      } else {
        auto entry = get(item);
        if (failed(entry))
          return failure();
        unsigned precision =
            std::max(*width(input->value), *width(entry->value));
        bool commonSigned = sign(operands.front()) && sign(item);
        match = symbolComparison(
            expression, Binary::WildcardEquality,
            resize(expression, *input, precision, commonSigned),
            resize(expression, *entry, precision, commonSigned), precision);
      }
      // 11.4.13 specifies a four-state OR over the member comparisons: a
      // known match absorbs other unknown results, while unmatched unknown
      // left-hand symbols remain X. RHS symbols are wildcard selectors.
      result.unknown = bitOr(expression,
                             bitAnd(expression, result.unknown,
                                    notBool(expression, match.value), 1),
                             bitAnd(expression, match.unknown,
                                    notBool(expression, result.value), 1),
                             1);
      result.value = bitOr(expression, result.value, match.value, 1);
    }
    return result;
  }

  FailureOr<RandomArithmeticPlanes> build(Operation *expression) {
    auto precisionResult = width(expression);
    if (failed(precisionResult))
      return failure();
    unsigned precision = *precisionResult;
    bool signedResult = sign(expression);
    auto children = getChildren(expression);
    if (expression->hasAttr(randomEvaluationErrorAttrName))
      return RandomArithmeticPlanes{zero(expression, precision), nullptr};
    if (auto spelling = getConstantSpelling(expression)) {
      auto bits = parseSVInteger(*spelling, precision, expression->getLoc());
      if (failed(bits) ||
          (!bits->unknown.isZero() && !expressionIsFunction(expression))) {
        emitError(expression->getLoc())
            << "four-state constants are illegal in constraints (IEEE "
               "1800-2023 18.3)";
        return failure();
      }
      return RandomArithmeticPlanes{
          literal(expression, bits->value & ~bits->unknown, signedResult),
          bits->unknown.isZero() ? nullptr : literal(expression, bits->unknown),
          (bits->value & bits->unknown).isZero()
              ? nullptr
              : literal(expression, bits->value & bits->unknown)};
    }
    if (expression->hasAttr(randomVariableAttrName) ||
        expression->hasAttr(randomVariableBitOffsetAttrName)) {
      // Random candidates are two-state. Disabled property bits are state,
      // and their pre-solve unknown symbols survive until consumed by an
      // admitted conversion or absorbing operator. FrozenUnknown masks stored
      // unknown bits with the current inactive-bit inventory before capture.
      Operation *unknown =
          frozenVariables && fourState(expression)
              ? projection(expression, precision, Plane::FrozenUnknown)
              : nullptr;
      Operation *highImpedance =
          unknown
              ? projection(expression, precision, Plane::FrozenHighImpedance)
              : nullptr;
      // This reference is already the canonical solver bitstream. Keep its
      // binding, but prevent the emitted value cast from normalizing the
      // reference again and recursively rebuilding its unknown mask.
      Operation *reference = expression->clone();
      reference->setAttr(randomArithmeticNodeAttr, builder.getUnitAttr());
      expressions->getBody()->push_back(reference);
      nodes.push_back(reference);
      return RandomArithmeticPlanes{
          mask(expression, cast(expression, reference, precision, signedResult),
               unknown, precision, signedResult),
          unknown, highImpedance};
    }
    if (!needed(expression))
      return RandomArithmeticPlanes{
          cast(expression, expression, precision, signedResult), nullptr};
    if (candidate(expression) &&
        isa<semantic::SVElementSelectExpressionOp,
            semantic::SVRangeSelectExpressionOp,
            semantic::SVMemberAccessExpressionOp>(expression)) {
      auto selection = fixedSelection(expression, precision);
      if (failed(selection))
        return failure();
      if (*selection)
        return **selection;
    }
    if (isa<semantic::SVElementSelectExpressionOp,
            semantic::SVRangeSelectExpressionOp>(expression)) {
      auto source = children.empty()
                        ? TypeAttr{}
                        : children[0]->getAttrOfType<TypeAttr>("semantic_type");
      if (source && getSemanticPackedWidth(source.getValue()))
        return packedSelection(expression, precision);
    }
    if (isa<semantic::SVNamedValueExpressionOp,
            semantic::SVHierarchicalValueExpressionOp,
            semantic::SVMemberAccessExpressionOp>(expression) &&
        !candidate(expression))
      return RandomArithmeticPlanes{
          projection(expression, precision, Plane::Value),
          fourState(expression)
              ? projection(expression, precision, Plane::Unknown)
              : nullptr,
          fourState(expression)
              ? projection(expression, precision, Plane::HighImpedance)
              : nullptr};
    if (auto count = expression->getAttrOfType<IntegerAttr>(
            randomEvaluationInputsAttrName)) {
      if (count.getValue().isNegative() ||
          count.getValue().getActiveBits() > 64 ||
          count.getValue().getZExtValue() + 1 != children.size())
        return failure();
      auto input = get(children.back());
      if (failed(input))
        return failure();
      auto result = resize(expression, *input, precision, signedResult);
      if (!fourState(expression)) {
        result.unknown = nullptr;
        result.highImpedance = nullptr;
      }
      result.value = mask(expression, result.value, result.unknown, precision,
                          signedResult);
      return result;
    }
    if (isa<semantic::SVInsideExpressionOp>(expression))
      return inside(expression);
    if (!candidate(expression) &&
        !isa<semantic::SVConversionExpressionOp, semantic::SVUnaryExpressionOp,
             semantic::SVBinaryExpressionOp,
             semantic::SVConditionalExpressionOp,
             semantic::SVConcatenationExpressionOp,
             semantic::SVCallExpressionOp>(expression))
      return RandomArithmeticPlanes{
          projection(expression, precision, Plane::Value),
          fourState(expression)
              ? projection(expression, precision, Plane::Unknown)
              : nullptr,
          fourState(expression)
              ? projection(expression, precision, Plane::HighImpedance)
              : nullptr};
    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(expression);
        call && call.getIsSystemCall() &&
        call.getCalleeName() == "$countbits" && children.size() >= 2) {
      auto input = get(children.front());
      if (failed(input))
        return failure();
      unsigned inputWidth = *width(input->value);
      // IEEE 20.9 treats controls as a set of four symbols and uses only each
      // control's LSB. Symbol selectors consume X/Z rather than tainting the
      // known integer result. Keep X and Z separate until this boundary.
      Operation *controls[4] = {nullptr, nullptr, nullptr, nullptr};
      for (Operation *child : ArrayRef(children).drop_front()) {
        RandomArithmeticPlanes control;
        if (auto spelling = getConstantSpelling(child)) {
          auto bits = parseSVInteger(*spelling, 1, child->getLoc());
          if (failed(bits))
            return failure();
          control = {literal(expression, bits->value & ~bits->unknown),
                     literal(expression, bits->unknown),
                     literal(expression, bits->value & bits->unknown)};
        } else {
          auto planes = get(child);
          if (failed(planes))
            return failure();
          control = resize(expression, *planes, 1, false);
        }
        Operation *symbols[4] = {
            bitNot(expression,
                   bitOr(expression, control.value, control.unknown, 1), 1),
            control.value,
            mask(expression,
                 control.unknown ? control.unknown : zero(expression, 1),
                 control.highImpedance, 1),
            control.highImpedance};
        for (unsigned symbol = 0; symbol < 4; ++symbol)
          controls[symbol] =
              bitOr(expression, controls[symbol], symbols[symbol], 1);
      }
      Operation *symbols[4] = {
          bitNot(expression,
                 bitOr(expression, input->value, input->unknown, inputWidth),
                 inputWidth),
          input->value,
          mask(expression,
               input->unknown ? input->unknown : zero(expression, inputWidth),
               input->highImpedance, inputWidth),
          input->highImpedance};
      Operation *matches = zero(expression, inputWidth);
      for (unsigned symbol = 0; symbol < 4; ++symbol)
        if (!isZero(controls[symbol]))
          matches = bitOr(
              expression, matches,
              bitAnd(expression, symbols[symbol],
                     unknownMask(expression, controls[symbol], inputWidth),
                     inputWidth),
              inputWidth);
      return RandomArithmeticPlanes{integralCall(expression, "$countones",
                                                 matches, precision,
                                                 signedResult),
                                    nullptr};
    }
    SmallVector<RandomArithmeticPlanes> inputs;
    for (Operation *child : children) {
      auto input = get(child);
      if (failed(input))
        return failure();
      inputs.push_back(*input);
    }
    if (isa<semantic::SVConversionExpressionOp>(expression) &&
        inputs.size() == 1) {
      // Assignment-like casts extend according to the source signedness,
      // then adopt the destination type. Extend the unknown sign bit by the
      // same rule as the value plane, including an unsigned destination.
      auto result =
          resize(expression, inputs.front(), precision, sign(children.front()));
      // The destination's value set is the conversion boundary. Arithmetic
      // width conversions emitted by Slang retain the four-state type.
      if (!fourState(expression)) {
        result.unknown = nullptr;
        result.highImpedance = nullptr;
      }
      result.value = mask(expression, result.value, result.unknown, precision,
                          signedResult);
      return result;
    }
    if (auto op = dyn_cast<semantic::SVUnaryExpressionOp>(expression);
        op && inputs.size() == 1) {
      auto input = inputs.front();
      Unary kind = op.getOperatorKind();
      if (kind == Unary::LogicalNot) {
        input = truth(expression, input);
        return RandomArithmeticPlanes{mask(expression,
                                           notBool(expression, input.value),
                                           input.unknown, 1),
                                      input.unknown};
      }
      if (kind == Unary::BitwiseAnd || kind == Unary::BitwiseNand ||
          kind == Unary::BitwiseOr || kind == Unary::BitwiseNor ||
          kind == Unary::BitwiseXor || kind == Unary::BitwiseXnor) {
        unsigned inputWidth = *width(input.value);
        Operation *value;
        Operation *unknown = nullptr;
        if (kind == Unary::BitwiseOr || kind == Unary::BitwiseNor) {
          auto logical = truth(expression, input);
          value = logical.value;
          unknown = logical.unknown;
        } else if (kind == Unary::BitwiseAnd || kind == Unary::BitwiseNand) {
          Operation *knownZero = nonzero(
              expression,
              bitNot(expression,
                     bitOr(expression, input.value, input.unknown, inputWidth),
                     inputWidth),
              inputWidth);
          unknown =
              input.unknown
                  ? bitAnd(expression, notBool(expression, knownZero),
                           nonzero(expression, input.unknown, inputWidth), 1)
                  : nullptr;
          value =
              notBool(expression,
                      bitOr(expression, knownZero,
                            nonzero(expression, input.unknown, inputWidth), 1));
        } else {
          value = unary(expression, Unary::BitwiseXor, input.value, 1);
          unknown = input.unknown
                        ? nonzero(expression, input.unknown, inputWidth)
                        : nullptr;
        }
        if (kind == Unary::BitwiseNand || kind == Unary::BitwiseNor ||
            kind == Unary::BitwiseXnor)
          value = notBool(expression, value);
        return RandomArithmeticPlanes{mask(expression, value, unknown, 1),
                                      unknown};
      }
      input = resize(expression, input, precision, signedResult);
      Operation *unknown = input.unknown;
      if ((kind == Unary::Plus || kind == Unary::Minus) && unknown)
        unknown = unknownMask(
            expression, nonzero(expression, unknown, precision), precision);
      Operation *value =
          unary(expression, kind, input.value, precision, signedResult);
      return RandomArithmeticPlanes{
          mask(expression, value, unknown, precision, signedResult), unknown};
    }
    if (auto op = dyn_cast<semantic::SVBinaryExpressionOp>(expression);
        op && inputs.size() == 2) {
      Binary kind = op.getOperatorKind();
      auto left = inputs[0], right = inputs[1];
      if (kind == Binary::LogicalAnd || kind == Binary::LogicalOr ||
          kind == Binary::LogicalImplication ||
          kind == Binary::LogicalEquivalence) {
        left = truth(expression, left);
        right = truth(expression, right);
        if (kind == Binary::LogicalImplication) {
          left.value = mask(expression, notBool(expression, left.value),
                            left.unknown, 1);
          kind = Binary::LogicalOr;
        }
        Operation *unknown;
        Operation *value;
        if (kind == Binary::LogicalAnd) {
          auto possibleLeft = bitOr(expression, left.value, left.unknown, 1);
          auto possibleRight = bitOr(expression, right.value, right.unknown, 1);
          unknown = bitOr(
              expression, bitAnd(expression, left.unknown, possibleRight, 1),
              bitAnd(expression, right.unknown, possibleLeft, 1), 1);
          value = bitAnd(expression, left.value, right.value, 1);
        } else if (kind == Binary::LogicalOr) {
          unknown = bitOr(expression,
                          bitAnd(expression, left.unknown,
                                 notBool(expression, right.value), 1),
                          bitAnd(expression, right.unknown,
                                 notBool(expression, left.value), 1),
                          1);
          value = bitOr(expression, left.value, right.value, 1);
        } else {
          unknown = bitOr(expression, left.unknown, right.unknown, 1);
          value =
              binary(expression, Binary::Equality, left.value, right.value, 1);
        }
        return RandomArithmeticPlanes{mask(expression, value, unknown, 1),
                                      unknown};
      }
      bool shift = kind == Binary::LogicalShiftLeft ||
                   kind == Binary::ArithmeticShiftLeft ||
                   kind == Binary::LogicalShiftRight ||
                   kind == Binary::ArithmeticShiftRight;
      bool power = kind == Binary::Power;
      bool caseComparison =
          kind == Binary::CaseEquality || kind == Binary::CaseInequality;
      bool wildcardComparison = kind == Binary::WildcardEquality ||
                                kind == Binary::WildcardInequality;
      bool comparison =
          caseComparison || wildcardComparison || kind == Binary::Equality ||
          kind == Binary::Inequality || kind == Binary::LessThan ||
          kind == Binary::LessThanEqual || kind == Binary::GreaterThan ||
          kind == Binary::GreaterThanEqual;
      unsigned operationWidth =
          comparison ? std::max(*width(left.value), *width(right.value))
                     : precision;
      bool signedOperation = sign(children[0]);
      left = resize(expression, left, operationWidth, signedOperation);
      if (!shift && !power)
        right = resize(expression, right, operationWidth, signedOperation);
      Operation *unknown = nullptr;
      Operation *highImpedance = nullptr;
      Operation *value = nullptr;
      if (caseComparison || wildcardComparison)
        return symbolComparison(expression, kind, left, right, operationWidth);
      if (kind == Binary::BinaryAnd || kind == Binary::BinaryOr) {
        if (kind == Binary::BinaryAnd) {
          unknown = bitOr(expression,
                          bitAnd(expression, left.unknown,
                                 bitOr(expression, right.value, right.unknown,
                                       operationWidth),
                                 operationWidth),
                          bitAnd(expression, right.unknown,
                                 bitOr(expression, left.value, left.unknown,
                                       operationWidth),
                                 operationWidth),
                          operationWidth);
          value = bitAnd(expression, left.value, right.value, operationWidth);
        } else {
          unknown =
              bitOr(expression,
                    bitAnd(expression, left.unknown,
                           bitNot(expression, right.value, operationWidth),
                           operationWidth),
                    bitAnd(expression, right.unknown,
                           bitNot(expression, left.value, operationWidth),
                           operationWidth),
                    operationWidth);
          value = bitOr(expression, left.value, right.value, operationWidth);
        }
      } else if (kind == Binary::BinaryXor || kind == Binary::BinaryXnor) {
        unknown =
            bitOr(expression, left.unknown, right.unknown, operationWidth);
        value = binary(expression, kind, left.value, right.value,
                       operationWidth, signedResult);
      } else if (comparison) {
        Operation *anyUnknown = nonzero(
            expression,
            bitOr(expression, left.unknown, right.unknown, operationWidth),
            operationWidth);
        unknown = anyUnknown;
        value = binary(expression, kind, left.value, right.value, 1);
        if (kind == Binary::Equality || kind == Binary::Inequality) {
          Operation *different =
              binary(expression, Binary::BinaryXor, left.value, right.value,
                     operationWidth);
          Operation *knownDifferences = mask(
              expression, different,
              bitOr(expression, left.unknown, right.unknown, operationWidth),
              operationWidth);
          Operation *mismatch =
              nonzero(expression, knownDifferences, operationWidth);
          unknown =
              bitAnd(expression, anyUnknown, notBool(expression, mismatch), 1);
          value = kind == Binary::Equality
                      ? notBool(expression,
                                bitOr(expression, mismatch, anyUnknown, 1))
                      : mismatch;
        }
      } else if (shift) {
        if (left.unknown)
          unknown = binary(
              expression, kind, left.unknown, right.value, operationWidth,
              kind == Binary::ArithmeticShiftRight && signedOperation);
        if (right.unknown)
          unknown = bitOr(expression, unknown,
                          unknownMask(expression,
                                      nonzero(expression, right.unknown,
                                              *width(right.value)),
                                      operationWidth),
                          operationWidth);
        value = binary(expression, kind, left.value, right.value,
                       operationWidth, signedResult);
        if (left.highImpedance) {
          highImpedance = binary(
              expression, kind, left.highImpedance, right.value, operationWidth,
              kind == Binary::ArithmeticShiftRight && signedOperation);
          if (right.unknown)
            highImpedance = mask(expression, highImpedance,
                                 unknownMask(expression,
                                             nonzero(expression, right.unknown,
                                                     *width(right.value)),
                                             operationWidth),
                                 operationWidth);
        }
      } else {
        Operation *undefined =
            bitOr(expression, nonzero(expression, left.unknown, operationWidth),
                  nonzero(expression, right.unknown, *width(right.value)), 1);
        if (kind == Binary::Divide || kind == Binary::Mod) {
          Operation *zeroDivisor = notBool(
              expression, nonzero(expression, right.value, operationWidth));
          undefined = bitOr(expression, undefined, zeroDivisor, 1);
          right.value = select(expression, zeroDivisor,
                               literal(expression, APInt(operationWidth, 1)),
                               right.value, operationWidth, signedOperation);
        } else if (power && sign(children[1])) {
          Operation *negative = binary(
              expression, Binary::LessThan, right.value,
              literal(expression, APInt(*width(right.value), 0), true), 1);
          undefined =
              bitOr(expression, undefined,
                    bitAnd(expression,
                           notBool(expression, nonzero(expression, left.value,
                                                       operationWidth)),
                           negative, 1),
                    1);
        }
        if (!isZero(undefined))
          unknown = unknownMask(expression, undefined, operationWidth);
        value = binary(expression, kind, left.value, right.value,
                       operationWidth, signedResult);
      }
      return RandomArithmeticPlanes{
          mask(expression, value, unknown, precision, signedResult), unknown,
          highImpedance};
    }
    if (auto op = dyn_cast<semantic::SVConditionalExpressionOp>(expression);
        op && inputs.size() == 3 && op.getConditionCount() == 1) {
      auto condition = truth(expression, inputs[0]);
      auto yes = resize(expression, inputs[1], precision, signedResult);
      auto no = resize(expression, inputs[2], precision, signedResult);
      Operation *unknown = select(
          expression, condition.value,
          yes.unknown ? yes.unknown : zero(expression, precision),
          no.unknown ? no.unknown : zero(expression, precision), precision);
      Operation *value = select(expression, condition.value, yes.value,
                                no.value, precision, signedResult);
      Operation *highImpedance = select(
          expression, condition.value,
          yes.highImpedance ? yes.highImpedance : zero(expression, precision),
          no.highImpedance ? no.highImpedance : zero(expression, precision),
          precision);
      if (condition.unknown) {
        Operation *different = binary(expression, Binary::BinaryXor, yes.value,
                                      no.value, precision);
        Operation *mergedUnknown = bitOr(
            expression, different,
            bitOr(expression, yes.unknown, no.unknown, precision), precision);
        Operation *mergedValue =
            mask(expression, yes.value, mergedUnknown, precision, signedResult);
        unknown = select(expression, condition.unknown, mergedUnknown, unknown,
                         precision);
        value = select(expression, condition.unknown, mergedValue, value,
                       precision, signedResult);
        highImpedance = select(
            expression, condition.unknown,
            bitAnd(expression, yes.highImpedance, no.highImpedance, precision),
            highImpedance, precision);
      }
      return RandomArithmeticPlanes{
          mask(expression, value, unknown, precision, signedResult), unknown,
          highImpedance};
    }
    if (isa<semantic::SVConcatenationExpressionOp>(expression)) {
      Operation *value = zero(expression, precision);
      Operation *unknown = zero(expression, precision);
      Operation *highImpedance = zero(expression, precision);
      for (auto input : inputs) {
        unsigned inputWidth = *width(input.value);
        auto amount = literal(expression, APInt(precision, inputWidth));
        value = bitOr(expression,
                      binary(expression, Binary::LogicalShiftLeft, value,
                             amount, precision),
                      cast(expression, input.value, precision), precision);
        unknown =
            bitOr(expression,
                  binary(expression, Binary::LogicalShiftLeft, unknown, amount,
                         precision),
                  input.unknown ? cast(expression, input.unknown, precision)
                                : zero(expression, precision),
                  precision);
        highImpedance =
            bitOr(expression,
                  binary(expression, Binary::LogicalShiftLeft, highImpedance,
                         amount, precision),
                  input.highImpedance
                      ? cast(expression, input.highImpedance, precision)
                      : zero(expression, precision),
                  precision);
      }
      return RandomArithmeticPlanes{mask(expression, value, unknown, precision),
                                    unknown, highImpedance};
    }
    if (isa<semantic::SVReplicationExpressionOp>(expression) &&
        children.size() == 2) {
      auto count = constant(children.front());
      unsigned inputWidth = *width(inputs.back().value);
      if (!count || count->isZero() || count->isNegative() ||
          count->getActiveBits() > 64) {
        emitError(expression->getLoc())
            << "random replication requires a known positive count";
        return failure();
      }
      if (precision % inputWidth ||
          count->getZExtValue() != precision / inputWidth) {
        emitError(expression->getLoc())
            << "random replication width is inconsistent";
        return failure();
      }
      // Validate even a zero plane, which would otherwise fold away the
      // replication before the existing program encoder sees its count.
      auto replicate = [&](Operation *plane) -> Operation * {
        if (isZero(plane))
          return zero(expression, precision);
        return make(expression,
                    semantic::SVReplicationExpressionOp::getOperationName(),
                    precision, false, {}, {children.front(), plane});
      };
      auto input = inputs.back();
      Operation *unknown = replicate(input.unknown);
      return RandomArithmeticPlanes{
          mask(expression, replicate(input.value), unknown, precision), unknown,
          replicate(input.highImpedance)};
    }
    if (auto call = dyn_cast<semantic::SVCallExpressionOp>(expression);
        call && call.getIsSystemCall() && inputs.size() == 1) {
      StringRef name = call.getCalleeName();
      if (name == "$signed" || name == "$unsigned")
        return resize(expression, inputs.front(), precision, signedResult);
      if (name == "$isunknown")
        return RandomArithmeticPlanes{nonzero(expression,
                                              inputs.front().unknown,
                                              *width(inputs.front().value)),
                                      nullptr};
      if (name == "$clog2" || name == "$countones" || name == "$onehot" ||
          name == "$onehot0")
        return RandomArithmeticPlanes{integralCall(expression, name,
                                                   inputs.front().value,
                                                   precision, signedResult),
                                      nullptr};
    }
    if (!candidate(expression))
      return RandomArithmeticPlanes{
          projection(expression, precision, Plane::Value),
          fourState(expression)
              ? projection(expression, precision, Plane::Unknown)
              : nullptr,
          fourState(expression)
              ? projection(expression, precision, Plane::HighImpedance)
              : nullptr};
    emitError(expression->getLoc())
        << "constraint unknown arithmetic is not lowered for "
        << expression->getName();
    return failure();
  }

  FailureOr<Operation *> known(Operation *expression) {
    if (!needed(expression))
      return static_cast<Operation *>(nullptr);
    Operation *key = canonical(expression);
    if (auto found = knownPredicates.find(key); found != knownPredicates.end())
      return found->second;
    auto result = get(expression);
    if (failed(result))
      return failure();
    Operation *predicate =
        result->unknown
            ? notBool(expression, nonzero(expression, result->unknown,
                                          *width(result->value)))
            : nullptr;
    knownPredicates[key] = predicate;
    return predicate;
  }
};

RandomArithmetic::RandomArithmetic(MLIRContext *context, Width width,
                                   Candidate candidate, Binding binding,
                                   bool frozenVariables)
    : impl(std::make_unique<Impl>(context, std::move(width),
                                  std::move(candidate), std::move(binding),
                                  frozenVariables)) {}
RandomArithmetic::~RandomArithmetic() = default;
Operation *RandomArithmetic::canonical(Operation *expression) {
  return impl->canonical(expression);
}
bool RandomArithmetic::needed(Operation *expression) {
  return impl->needed(expression);
}
FailureOr<RandomArithmeticPlanes> RandomArithmetic::get(Operation *expression) {
  return impl->get(expression);
}
FailureOr<Operation *> RandomArithmetic::known(Operation *expression) {
  return impl->known(expression);
}

} // namespace obelisk::simlowering
