//===- RandomArithmetic.cpp - Compiler-only four-state arithmetic planes ===//

#include "RandomArithmetic.h"
#include "Detail.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLExtras.h"
#include <optional>

using namespace mlir;

namespace obelisk::simlowering {

struct RandomArithmetic::Impl {
  using Binary = semantic::SVBinaryOperator;
  using Unary = semantic::SVUnaryOperator;
  Builder builder;
  Width width;
  Candidate candidate;
  bool frozenVariables;
  // Temporary expressions retain the target layout used by their original
  // module, including layouts of unpacked values beneath a packed selection.
  OwningOpRef<ModuleOp> expressions;
  SmallVector<Operation *> nodes;
  llvm::DenseMap<Operation *, bool> needs;
  llvm::DenseMap<Operation *, RandomArithmeticPlanes> planes;
  llvm::DenseMap<Operation *, Operation *> knownPredicates;

  Impl(MLIRContext *context, Width width, Candidate candidate,
       bool frozenVariables)
      : builder(context), width(std::move(width)),
        candidate(std::move(candidate)), frozenVariables(frozenVariables),
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
    return type && isFourStateSemanticType(type.getValue());
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
    return make(
        source, semantic::SVConversionExpressionOp::getOperationName(),
        precision, signedInput,
        {builder.getNamedAttr("is_implicit", builder.getBoolAttr(true))},
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
    return value;
  }
  Operation *projection(Operation *source, unsigned precision, int32_t plane) {
    return make(source, semantic::SVConversionExpressionOp::getOperationName(),
                precision, !plane && sign(source),
                {builder.getNamedAttr("is_implicit", builder.getBoolAttr(true)),
                 builder.getNamedAttr(randomArithmeticPlaneAttr,
                                      builder.getI32IntegerAttr(plane))},
                {source});
  }
  bool needed(Operation *expression) {
    if (expression->hasAttr(randomArithmeticNodeAttr))
      return false;
    auto semanticType = expression->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType || !getSemanticPackedWidth(semanticType.getValue()))
      return false;
    if (auto found = needs.find(expression); found != needs.end())
      return found->second;
    if (auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(expression);
        binary && !candidate(expression) &&
        (binary.getOperatorKind() == Binary::CaseEquality ||
         binary.getOperatorKind() == Binary::CaseInequality))
      // Expanded procedural case comparisons consume complete X/Z symbols.
      // Source-level four-state constraint comparisons are rejected earlier.
      return false;
    bool result = frozenVariables && fourState(expression) &&
                  (expression->hasAttr(randomVariableAttrName) ||
                   expression->hasAttr(randomVariableBitOffsetAttrName));
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
    } else {
      for (Operation *child : children)
        result |= needed(child);
    }
    needs[expression] = result;
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
    if (auto found = planes.find(expression); found != planes.end())
      return found->second;
    auto result = build(expression);
    if (succeeded(result)) {
      if (isZero(result->unknown))
        result->unknown = nullptr;
      planes[expression] = *result;
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
          bits->unknown.isZero() ? nullptr
                                 : literal(expression, bits->unknown)};
    }
    if (!needed(expression))
      return RandomArithmeticPlanes{
          cast(expression, expression, precision, signedResult), nullptr};
    if (expression->hasAttr(randomVariableAttrName) ||
        expression->hasAttr(randomVariableBitOffsetAttrName)) {
      // Random candidates are two-state. Disabled property bits are state,
      // and their pre-solve unknown symbols survive until consumed by an
      // admitted conversion or absorbing operator. Plane 2 masks the stored
      // unknown bits with the current inactive-bit inventory before capture.
      Operation *unknown = frozenVariables && fourState(expression)
                               ? projection(expression, precision, 2)
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
          unknown};
    }
    if (isa<semantic::SVNamedValueExpressionOp,
            semantic::SVHierarchicalValueExpressionOp,
            semantic::SVMemberAccessExpressionOp>(expression) &&
        !candidate(expression))
      return RandomArithmeticPlanes{
          projection(expression, precision, false),
          fourState(expression) ? projection(expression, precision, true)
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
      if (!fourState(expression))
        result.unknown = nullptr;
      result.value = mask(expression, result.value, result.unknown, precision,
                          signedResult);
      return result;
    }
    if (!candidate(expression) &&
        !isa<semantic::SVConversionExpressionOp, semantic::SVUnaryExpressionOp,
             semantic::SVBinaryExpressionOp,
             semantic::SVConditionalExpressionOp,
             semantic::SVConcatenationExpressionOp,
             semantic::SVCallExpressionOp>(expression))
      return RandomArithmeticPlanes{
          projection(expression, precision, false),
          fourState(expression) ? projection(expression, precision, true)
                                : nullptr};
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
      if (!fourState(expression))
        result.unknown = nullptr;
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
      if (kind == Unary::Minus && unknown)
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
      bool comparison =
          kind == Binary::Equality || kind == Binary::Inequality ||
          kind == Binary::LessThan || kind == Binary::LessThanEqual ||
          kind == Binary::GreaterThan || kind == Binary::GreaterThanEqual;
      unsigned operationWidth =
          comparison ? std::max(*width(left.value), *width(right.value))
                     : precision;
      bool signedOperation = sign(children[0]);
      left = resize(expression, left, operationWidth, signedOperation);
      if (!shift && !power)
        right = resize(expression, right, operationWidth, signedOperation);
      Operation *unknown = nullptr;
      Operation *value = nullptr;
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
          mask(expression, value, unknown, precision, signedResult), unknown};
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
      }
      return RandomArithmeticPlanes{
          mask(expression, value, unknown, precision, signedResult), unknown};
    }
    if (isa<semantic::SVConcatenationExpressionOp>(expression)) {
      Operation *value = zero(expression, precision);
      Operation *unknown = zero(expression, precision);
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
      }
      return RandomArithmeticPlanes{mask(expression, value, unknown, precision),
                                    unknown};
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
      if (name == "$countones" || name == "$onehot" || name == "$onehot0") {
        Operation *node = expression->clone();
        expressions->getBody()->push_back(node);
        nodes.push_back(node);
        node->setAttr(randomArithmeticNodeAttr, builder.getUnitAttr());
        node->setAttr("semantic_type",
                      TypeAttr::get(type(precision, signedResult)));
        node->getRegion(0).front().clear();
        node->getRegion(0).front().push_back(inputs.front().value->clone());
        return RandomArithmeticPlanes{node, nullptr};
      }
    }
    if (!candidate(expression))
      return RandomArithmeticPlanes{
          projection(expression, precision, false),
          fourState(expression) ? projection(expression, precision, true)
                                : nullptr};
    emitError(expression->getLoc())
        << "constraint unknown arithmetic is not lowered for "
        << expression->getName();
    return failure();
  }

  FailureOr<Operation *> known(Operation *expression) {
    if (!needed(expression))
      return static_cast<Operation *>(nullptr);
    if (auto found = knownPredicates.find(expression);
        found != knownPredicates.end())
      return found->second;
    auto result = get(expression);
    if (failed(result))
      return failure();
    Operation *predicate =
        result->unknown
            ? notBool(expression, nonzero(expression, result->unknown,
                                          *width(result->value)))
            : nullptr;
    knownPredicates[expression] = predicate;
    return predicate;
  }
};

RandomArithmetic::RandomArithmetic(MLIRContext *context, Width width,
                                   Candidate candidate, bool frozenVariables)
    : impl(std::make_unique<Impl>(context, std::move(width),
                                  std::move(candidate), frozenVariables)) {}
RandomArithmetic::~RandomArithmetic() = default;
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
