//===- LowerUnitOperators.cpp - Lower expression operators ------------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"

#include <functional>
#include <optional>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<Value> UnitLowering::lowerUnary(semantic::SVUnaryExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 1) {
    unsupported(op) << " (unary arity)";
    return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();

  semantic::SVUnaryOperator kind = op.getOperatorKind();
  bool increment = kind == semantic::SVUnaryOperator::Preincrement ||
                   kind == semantic::SVUnaryOperator::Postincrement;
  bool decrement = kind == semantic::SVUnaryOperator::Predecrement ||
                   kind == semantic::SVUnaryOperator::Postdecrement;
  if (increment || decrement) {
    FailureOr<Value> destination = lowerExpression(children.front(), true);
    if (failed(destination))
      return failure();
    Type referenceType = getReferenceElementType(*destination);
    if (!referenceType) {
      emitError(location) << "increment and decrement require a variable "
                             "reference";
      return failure();
    }
    FailureOr<Value> loaded = loadReference(*destination, location);
    if (failed(loaded))
      return failure();
    Value oldValue = *loaded;
    if (isa<FloatType>(oldValue.getType())) {
      Value one = arith::ConstantOp::create(
          builder, location, oldValue.getType(),
          builder.getFloatAttr(oldValue.getType(), 1.0));
      Value newValue =
          increment
              ? Value(arith::AddFOp::create(builder, location, oldValue, one))
              : Value(arith::SubFOp::create(builder, location, oldValue, one));
      if (failed(storeReference(*destination, newValue, location)))
        return failure();
      bool post = kind == semantic::SVUnaryOperator::Postincrement ||
                  kind == semantic::SVUnaryOperator::Postdecrement;
      return convert(post ? oldValue : newValue, *resultType, false, location);
    }
    FailureOr<Value> oldScalar = toPackedScalar(oldValue, location);
    if (failed(oldScalar))
      return failure();
    Value one;
    Value newScalar;
    if (auto logic = dyn_cast<sim::LogicType>((*oldScalar).getType())) {
      auto planeType =
          IntegerType::get(function.getContext(), logic.getWidth());
      one = sim::SimLogicConstantOp::create(
          builder, location, logic, builder.getIntegerAttr(planeType, 1),
          builder.getIntegerAttr(planeType, 0));
      newScalar = sim::SimLogicBinaryOp::create(
          builder, location, logic,
          increment ? sim::BinaryKind::Add : sim::BinaryKind::Sub, *oldScalar,
          one);
    } else {
      auto integer = cast<IntegerType>((*oldScalar).getType());
      one = arith::ConstantOp::create(builder, location, integer,
                                      builder.getIntegerAttr(integer, 1));
      newScalar =
          increment
              ? Value(arith::AddIOp::create(builder, location, *oldScalar, one))
              : Value(
                    arith::SubIOp::create(builder, location, *oldScalar, one));
    }
    FailureOr<Value> newValue = convert(
        newScalar, referenceType, isSignedNode(children.front()), location);
    if (failed(newValue))
      return failure();
    if (failed(storeReference(*destination, *newValue, location)))
      return failure();
    bool post = kind == semantic::SVUnaryOperator::Postincrement ||
                kind == semantic::SVUnaryOperator::Postdecrement;
    return convert(post ? oldValue : *newValue, *resultType,
                   isSignedNode(children.front()), location);
  }

  FailureOr<Value> input = lowerExpression(children.front());
  if (failed(input))
    return failure();
  if (isa<FloatType>((*input).getType())) {
    Value value;
    if (kind == semantic::SVUnaryOperator::Plus) {
      value = *input;
    } else if (kind == semantic::SVUnaryOperator::Minus) {
      value = arith::NegFOp::create(builder, location, *input);
    } else if (kind == semantic::SVUnaryOperator::LogicalNot) {
      FailureOr<Value> truth = truthValue(*input, location);
      if (failed(truth))
        return failure();
      value = arith::XOrIOp::create(
          builder, location, *truth,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
    } else {
      emitError(location)
          << "floating-point operand does not support this unary operator";
      return failure();
    }
    return convert(value, *resultType, false, location);
  }
  FailureOr<Value> scalarInput = toPackedScalar(*input, location);
  if (failed(scalarInput))
    return failure();
  input = *scalarInput;
  bool reduction = kind >= semantic::SVUnaryOperator::BitwiseAnd &&
                   kind <= semantic::SVUnaryOperator::BitwiseXnor;
  if (reduction) {
    sim::ReductionKind reductionKind;
    switch (kind) {
    case semantic::SVUnaryOperator::BitwiseAnd:
      reductionKind = sim::ReductionKind::And;
      break;
    case semantic::SVUnaryOperator::BitwiseOr:
      reductionKind = sim::ReductionKind::Or;
      break;
    case semantic::SVUnaryOperator::BitwiseXor:
      reductionKind = sim::ReductionKind::Xor;
      break;
    case semantic::SVUnaryOperator::BitwiseNand:
      reductionKind = sim::ReductionKind::Nand;
      break;
    case semantic::SVUnaryOperator::BitwiseNor:
      reductionKind = sim::ReductionKind::Nor;
      break;
    case semantic::SVUnaryOperator::BitwiseXnor:
      reductionKind = sim::ReductionKind::Xnor;
      break;
    default:
      llvm_unreachable("not a reduction operator");
    }
    if (isa<sim::LogicType>((*input).getType())) {
      Value value = sim::SimLogicReductionOp::create(
          builder, location, sim::LogicType::get(function.getContext(), 1),
          reductionKind, *input);
      return convert(value, *resultType, false, location);
    }

    auto integer = cast<IntegerType>((*input).getType());
    Value reduced;
    bool invert = reductionKind == sim::ReductionKind::Nand ||
                  reductionKind == sim::ReductionKind::Nor ||
                  reductionKind == sim::ReductionKind::Xnor;
    if (reductionKind == sim::ReductionKind::And ||
        reductionKind == sim::ReductionKind::Nand) {
      Value ones = arith::ConstantOp::create(
          builder, location, integer,
          builder.getIntegerAttr(integer,
                                 APInt::getAllOnes(integer.getWidth())));
      reduced = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::eq, *input, ones);
    } else if (reductionKind == sim::ReductionKind::Or ||
               reductionKind == sim::ReductionKind::Nor) {
      Value zero = arith::ConstantOp::create(
          builder, location, integer, builder.getIntegerAttr(integer, 0));
      reduced = arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::ne, *input, zero);
    } else {
      Value folded = *input;
      for (uint64_t shift = 1; shift < integer.getWidth(); shift <<= 1) {
        Value amount = arith::ConstantOp::create(
            builder, location, integer, builder.getIntegerAttr(integer, shift));
        Value shifted =
            arith::ShRUIOp::create(builder, location, folded, amount);
        folded = arith::XOrIOp::create(builder, location, folded, shifted);
      }
      reduced = integer.getWidth() == 1
                    ? folded
                    : Value(arith::TruncIOp::create(
                          builder, location, builder.getI1Type(), folded));
    }
    if (invert) {
      Value one = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      reduced = arith::XOrIOp::create(builder, location, reduced, one);
    }
    return convert(reduced, *resultType, false, location);
  }

  if (isa<sim::LogicType>((*input).getType())) {
    sim::UnaryKind normalized;
    switch (kind) {
    case semantic::SVUnaryOperator::Plus:
      normalized = sim::UnaryKind::Plus;
      break;
    case semantic::SVUnaryOperator::Minus:
      normalized = sim::UnaryKind::Negate;
      break;
    case semantic::SVUnaryOperator::BitwiseNot:
      normalized = sim::UnaryKind::BitNot;
      break;
    case semantic::SVUnaryOperator::LogicalNot:
      normalized = sim::UnaryKind::LogicalNot;
      break;
    default:
      unsupported(op) << " (unary operator)";
      return failure();
    }
    Type unaryResult = normalized == sim::UnaryKind::LogicalNot
                           ? sim::getPackedScalarType(*resultType)
                           : (*input).getType();
    if (!unaryResult)
      return failure();
    Value value = sim::SimLogicUnaryOp::create(builder, location, unaryResult,
                                               normalized, *input)
                      .getResult();
    return convert(value, *resultType, false, location);
  }

  Value value;
  switch (kind) {
  case semantic::SVUnaryOperator::Plus:
    value = *input;
    break;
  case semantic::SVUnaryOperator::Minus:
    value = arith::SubIOp::create(
        builder, location,
        arith::ConstantOp::create(
            builder, location, (*input).getType(),
            builder.getIntegerAttr((*input).getType(), 0)),
        *input);
    break;
  case semantic::SVUnaryOperator::BitwiseNot:
    value = arith::XOrIOp::create(
        builder, location, *input,
        arith::ConstantOp::create(
            builder, location, (*input).getType(),
            builder.getIntegerAttr((*input).getType(), -1)));
    break;
  case semantic::SVUnaryOperator::LogicalNot: {
    FailureOr<Value> truth = truthValue(*input, location);
    if (failed(truth))
      return failure();
    value = arith::XOrIOp::create(
        builder, location, *truth,
        arith::ConstantOp::create(builder, location, builder.getIntegerType(1),
                                  builder.getBoolAttr(true)));
    break;
  }
  default:
    unsupported(op) << " (integer unary operator)";
    return failure();
  }
  return convert(value, *resultType, false, location);
}

FailureOr<Value> UnitLowering::lowerBinary(semantic::SVBinaryExpressionOp op) {
  using Binary = semantic::SVBinaryOperator;
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 2) {
    unsupported(op) << " (binary arity)";
    return failure();
  }
  Binary kind = op.getOperatorKind();
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();

  // Logical conjunction and disjunction only evaluate their right operand
  // when the left operand does not determine the result.  Keep the
  // four-state predicate for the eventual logical operation, but branch on an
  // ordinary i1 that recognizes only the controlling known value.
  if (kind == Binary::LogicalAnd || kind == Binary::LogicalOr) {
    FailureOr<Value> lhs = lowerExpression(children[0]);
    if (failed(lhs))
      return failure();
    FailureOr<Value> lhsPredicate =
        conditionalPredicate(*lhs, getSemanticLocation(children[0]));
    if (failed(lhsPredicate))
      return failure();

    Type predicateType = sim::LogicType::get(function.getContext(), 1);
    Value controlling;
    if (kind == Binary::LogicalAnd) {
      Value falsePredicate = sim::SimLogicConstantOp::create(
          builder, location, predicateType,
          builder.getIntegerAttr(builder.getI1Type(), 0),
          builder.getIntegerAttr(builder.getI1Type(), 0));
      controlling = sim::SimLogicCompareOp::create(
          builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
          *lhsPredicate, falsePredicate);
    } else {
      controlling = sim::SimLogicIsTrueOp::create(
          builder, location, builder.getI1Type(), *lhsPredicate);
    }

    Value controllingPredicate = sim::SimLogicConstantOp::create(
        builder, location, predicateType,
        builder.getIntegerAttr(builder.getI1Type(), kind == Binary::LogicalOr),
        builder.getIntegerAttr(builder.getI1Type(), 0));
    FailureOr<Value> controllingResult =
        convert(controllingPredicate, *resultType, false, location);
    if (failed(controllingResult))
      return failure();

    Block *rhsBlock = addBlock();
    Block *mergeBlock = addBlock();
    mergeBlock->addArgument(*resultType, location);
    cf::CondBranchOp::create(builder, location, controlling, mergeBlock,
                             ValueRange{*controllingResult}, rhsBlock,
                             ValueRange{});

    setCurrent(rhsBlock);
    FailureOr<Value> rhs = lowerExpression(children[1]);
    if (failed(rhs))
      return failure();
    FailureOr<Value> rhsPredicate =
        conditionalPredicate(*rhs, getSemanticLocation(children[1]));
    if (failed(rhsPredicate))
      return failure();
    Value logical = sim::SimLogicLogicalOp::create(
        builder, location, predicateType,
        kind == Binary::LogicalAnd ? sim::LogicalKind::And
                                   : sim::LogicalKind::Or,
        *lhsPredicate, *rhsPredicate);
    FailureOr<Value> rhsResult = convert(logical, *resultType, false, location);
    if (failed(rhsResult))
      return failure();
    cf::BranchOp::create(builder, location, mergeBlock, ValueRange{*rhsResult});

    setCurrent(mergeBlock);
    return mergeBlock->getArgument(0);
  }

  // Slang makes the common integral type of a binary expression explicit by
  // wrapping its operands in conversion nodes.  The common signedness governs
  // widening in this context: once either operand makes the operation
  // unsigned, a narrower signed operand is zero-extended.  Ordinary assignment
  // conversions, in contrast, extend according to the source signedness.
  // Lower direct integral operand conversions here so the two contexts remain
  // distinct.
  auto lowerOperand = [&](Operation *operand) -> FailureOr<Value> {
    auto conversion = dyn_cast<semantic::SVConversionExpressionOp>(operand);
    SmallVector<Operation *> conversionChildren =
        conversion ? getChildren(conversion) : SmallVector<Operation *>{};
    if (!conversion || conversionChildren.size() != 1)
      return lowerExpression(operand);
    FailureOr<Type> target = getNormalizedSemanticType(operand);
    if (failed(target) || !sim::getPackedScalarType(*target))
      return lowerExpression(operand);
    FailureOr<Value> input = lowerExpression(conversionChildren.front());
    if (failed(input))
      return failure();
    bool signedConversion = sim::getPackedScalarType((*input).getType())
                                ? isSignedNode(operand)
                                : isSignedNode(conversionChildren.front());
    return convert(*input, *target, signedConversion,
                   getSemanticLocation(operand), isSignedNode(operand));
  };

  FailureOr<Value> lhs = failure();
  FailureOr<Value> rhs = failure();
  if (isa<semantic::SVNullLiteralOp>(children[0])) {
    rhs = lowerExpression(children[1]);
    if (succeeded(rhs)) {
      if (isa<sim::ClassHandleType>((*rhs).getType()))
        lhs = sim::SimClassNullOp::create(builder, location, (*rhs).getType())
                  .getResult();
      else if (isa<sim::EventType>((*rhs).getType()))
        lhs = sim::SimEventNullOp::create(builder, location, (*rhs).getType())
                  .getResult();
    }
  } else if (isa<semantic::SVNullLiteralOp>(children[1])) {
    lhs = lowerExpression(children[0]);
    if (succeeded(lhs)) {
      if (isa<sim::ClassHandleType>((*lhs).getType()))
        rhs = sim::SimClassNullOp::create(builder, location, (*lhs).getType())
                  .getResult();
      else if (isa<sim::EventType>((*lhs).getType()))
        rhs = sim::SimEventNullOp::create(builder, location, (*lhs).getType())
                  .getResult();
    }
  } else {
    lhs = lowerOperand(children[0]);
    rhs = lowerOperand(children[1]);
  }
  if (failed(lhs) || failed(rhs))
    return failure();
  if (isa<sim::StringType>((*lhs).getType()) ||
      isa<sim::StringType>((*rhs).getType())) {
    Type stringType = sim::StringType::get(function.getContext());
    FailureOr<Value> left =
        convert(*lhs, stringType, isSignedNode(children[0]), location);
    FailureOr<Value> right =
        convert(*rhs, stringType, isSignedNode(children[1]), location);
    if (failed(left) || failed(right))
      return failure();
    if (kind == Binary::Add) {
      Value joined = sim::SimStringConcatOp::create(
          builder, location, stringType, ValueRange{*left, *right});
      return convert(joined, *resultType, false, location);
    }
    std::optional<arith::CmpIPredicate> predicate;
    switch (kind) {
    case Binary::Equality:
    case Binary::CaseEquality:
      predicate = arith::CmpIPredicate::eq;
      break;
    case Binary::Inequality:
    case Binary::CaseInequality:
      predicate = arith::CmpIPredicate::ne;
      break;
    case Binary::GreaterThanEqual:
      predicate = arith::CmpIPredicate::sge;
      break;
    case Binary::GreaterThan:
      predicate = arith::CmpIPredicate::sgt;
      break;
    case Binary::LessThanEqual:
      predicate = arith::CmpIPredicate::sle;
      break;
    case Binary::LessThan:
      predicate = arith::CmpIPredicate::slt;
      break;
    default:
      unsupported(op) << " (string operator)";
      return failure();
    }
    Value compared = sim::SimStringCompareOp::create(
        builder, location, builder.getI32Type(), *left, *right,
        builder.getBoolAttr(false));
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI32Type(), builder.getI32IntegerAttr(0));
    Value result =
        arith::CmpIOp::create(builder, location, *predicate, compared, zero);
    return convert(result, *resultType, false, location);
  }
  if (isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
          (*lhs).getType()) ||
      isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
          (*rhs).getType())) {
    if ((*lhs).getType() != (*rhs).getType() ||
        (kind != Binary::Equality && kind != Binary::Inequality &&
         kind != Binary::CaseEquality && kind != Binary::CaseInequality)) {
      unsupported(op) << " (sequential-container operator)";
      return failure();
    }
    bool caseEquality =
        kind == Binary::CaseEquality || kind == Binary::CaseInequality;
    FailureOr<Value> equal =
        caseEquality
            ? conditionalEqual(*lhs, *rhs, (*lhs).getType(), location, true)
            : logicalEqual(*lhs, *rhs, (*lhs).getType(), location);
    if (failed(equal))
      return failure();
    Value result = *equal;
    if (kind == Binary::Inequality || kind == Binary::CaseInequality) {
      if (caseEquality)
        result = arith::XOrIOp::create(
            builder, location, result,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true)));
      else
        result =
            sim::SimLogicUnaryOp::create(builder, location, result.getType(),
                                         sim::UnaryKind::LogicalNot, result);
    }
    return convert(result, *resultType, false, location);
  }
  if (isa<sim::UnpackedArrayType, sim::UnpackedStructType,
          sim::UnpackedUnionType>((*lhs).getType()) ||
      isa<sim::UnpackedArrayType, sim::UnpackedStructType,
          sim::UnpackedUnionType>((*rhs).getType())) {
    if (kind != Binary::Equality && kind != Binary::Inequality &&
        kind != Binary::CaseEquality && kind != Binary::CaseInequality) {
      unsupported(op) << " (unpacked-aggregate operator)";
      return failure();
    }
    Value right = *rhs;
    if ((*lhs).getType() != right.getType()) {
      auto leftArray = dyn_cast<sim::UnpackedArrayType>((*lhs).getType());
      auto rightArray = dyn_cast<sim::UnpackedArrayType>(right.getType());
      if (!leftArray || !rightArray ||
          sim::getAggregateNumElements(leftArray) !=
              sim::getAggregateNumElements(rightArray)) {
        unsupported(op) << " (unpacked-aggregate operator)";
        return failure();
      }
      FailureOr<Value> converted =
          convert(right, leftArray, isSignedNode(children[1]), location,
                  isSignedNode(children[0]));
      if (failed(converted))
        return failure();
      right = *converted;
    }
    bool caseEquality =
        kind == Binary::CaseEquality || kind == Binary::CaseInequality;
    FailureOr<Value> equal =
        caseEquality
            ? conditionalEqual(*lhs, right, (*lhs).getType(), location, true)
            : logicalEqual(*lhs, right, (*lhs).getType(), location);
    if (failed(equal))
      return failure();
    Value result = *equal;
    if (kind == Binary::Inequality || kind == Binary::CaseInequality) {
      if (caseEquality)
        result = arith::XOrIOp::create(
            builder, location, result,
            arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                      builder.getBoolAttr(true)));
      else
        result =
            sim::SimLogicUnaryOp::create(builder, location, result.getType(),
                                         sim::UnaryKind::LogicalNot, result);
    }
    return convert(result, *resultType, false, location);
  }
  if (isa<sim::ClassHandleType>((*lhs).getType()) ||
      isa<sim::ClassHandleType>((*rhs).getType())) {
    if (!isa<sim::ClassHandleType>((*lhs).getType()) ||
        !isa<sim::ClassHandleType>((*rhs).getType()) ||
        (kind != Binary::Equality && kind != Binary::Inequality &&
         kind != Binary::CaseEquality && kind != Binary::CaseInequality)) {
      unsupported(op) << " (class-handle operator)";
      return failure();
    }
    Value lhsID =
        sim::SimClassIdOp::create(builder, location, *lhs).getResult();
    Value rhsID =
        sim::SimClassIdOp::create(builder, location, *rhs).getResult();
    arith::CmpIPredicate predicate =
        kind == Binary::Equality || kind == Binary::CaseEquality
            ? arith::CmpIPredicate::eq
            : arith::CmpIPredicate::ne;
    Value compared =
        arith::CmpIOp::create(builder, location, predicate, lhsID, rhsID);
    return convert(compared, *resultType, false, location);
  }
  if (isa<sim::EventType>((*lhs).getType()) ||
      isa<sim::EventType>((*rhs).getType())) {
    if (!isa<sim::EventType>((*lhs).getType()) ||
        !isa<sim::EventType>((*rhs).getType()) ||
        (kind != Binary::Equality && kind != Binary::Inequality &&
         kind != Binary::CaseEquality && kind != Binary::CaseInequality)) {
      unsupported(op) << " (event-handle operator)";
      return failure();
    }
    Value equal = sim::SimEventEqualOp::create(builder, location,
                                               builder.getI1Type(), *lhs, *rhs);
    if (kind == Binary::Inequality || kind == Binary::CaseInequality)
      equal = arith::XOrIOp::create(
          builder, location, equal,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
    return convert(equal, *resultType, false, location);
  }
  if (isa<FloatType>((*lhs).getType()) || isa<FloatType>((*rhs).getType())) {
    Type arithmeticType = (*lhs).getType().isF64() || (*rhs).getType().isF64()
                              ? Type(builder.getF64Type())
                              : Type(builder.getF32Type());
    if ((*lhs).getType() != arithmeticType) {
      lhs = convert(*lhs, arithmeticType, isSignedNode(children[0]), location);
      if (failed(lhs))
        return failure();
    }
    if ((*rhs).getType() != arithmeticType) {
      rhs = convert(*rhs, arithmeticType, isSignedNode(children[1]), location);
      if (failed(rhs))
        return failure();
    }
    std::optional<arith::CmpFPredicate> predicate;
    switch (kind) {
    case Binary::Equality:
    case Binary::CaseEquality:
      predicate = arith::CmpFPredicate::OEQ;
      break;
    case Binary::Inequality:
    case Binary::CaseInequality:
      predicate = arith::CmpFPredicate::UNE;
      break;
    case Binary::GreaterThanEqual:
      predicate = arith::CmpFPredicate::OGE;
      break;
    case Binary::GreaterThan:
      predicate = arith::CmpFPredicate::OGT;
      break;
    case Binary::LessThanEqual:
      predicate = arith::CmpFPredicate::OLE;
      break;
    case Binary::LessThan:
      predicate = arith::CmpFPredicate::OLT;
      break;
    default:
      break;
    }
    if (predicate) {
      Value compared =
          arith::CmpFOp::create(builder, location, *predicate, *lhs, *rhs);
      return convert(compared, *resultType, false, location);
    }
    Value value;
    switch (kind) {
    case Binary::Add:
      value = arith::AddFOp::create(builder, location, *lhs, *rhs);
      break;
    case Binary::Subtract:
      value = arith::SubFOp::create(builder, location, *lhs, *rhs);
      break;
    case Binary::Multiply:
      value = arith::MulFOp::create(builder, location, *lhs, *rhs);
      break;
    case Binary::Divide:
      value = arith::DivFOp::create(builder, location, *lhs, *rhs);
      break;
    case Binary::Power:
      value =
          math::PowFOp::create(builder, location, arithmeticType, *lhs, *rhs)
              .getResult();
      break;
    default:
      emitError(location)
          << "floating-point operand does not support this binary operator";
      return failure();
    }
    return convert(value, *resultType, false, location);
  }
  FailureOr<Value> scalarLhs = toPackedScalar(*lhs, location);
  FailureOr<Value> scalarRhs = toPackedScalar(*rhs, location);
  Type scalarResultType = sim::getPackedScalarType(*resultType);
  if (failed(scalarLhs) || failed(scalarRhs) || !scalarResultType)
    return failure();
  lhs = *scalarLhs;
  rhs = *scalarRhs;
  bool signedOp = isSignedNode(children.front());

  // Integral power always produces a four-state result. Normalize its base to
  // that result plane even when the source operand was two-state so the
  // expansion below also serves generated constraint checkers.
  if (kind == Binary::Power && isa<sim::LogicType>(scalarResultType) &&
      !isa<sim::LogicType>((*lhs).getType())) {
    lhs = convert(*lhs, scalarResultType, signedOp, location);
    if (failed(lhs))
      return failure();
  }

  if (isa<sim::LogicType>((*lhs).getType())) {
    std::optional<sim::CompareKind> compare;
    switch (kind) {
    case Binary::Equality:
      compare = sim::CompareKind::Eq;
      break;
    case Binary::Inequality:
      compare = sim::CompareKind::Ne;
      break;
    case Binary::CaseEquality:
      compare = sim::CompareKind::CaseEq;
      break;
    case Binary::CaseInequality:
      compare = sim::CompareKind::CaseNe;
      break;
    case Binary::WildcardEquality:
      compare = sim::CompareKind::WildEq;
      break;
    case Binary::WildcardInequality:
      compare = sim::CompareKind::WildNe;
      break;
    case Binary::GreaterThanEqual:
      compare = signedOp ? sim::CompareKind::SGE : sim::CompareKind::UGE;
      break;
    case Binary::GreaterThan:
      compare = signedOp ? sim::CompareKind::SGT : sim::CompareKind::UGT;
      break;
    case Binary::LessThanEqual:
      compare = signedOp ? sim::CompareKind::SLE : sim::CompareKind::ULE;
      break;
    case Binary::LessThan:
      compare = signedOp ? sim::CompareKind::SLT : sim::CompareKind::ULT;
      break;
    default:
      break;
    }
    if (compare) {
      Value compared = sim::SimLogicCompareOp::create(
          builder, location, scalarResultType, *compare, *lhs, *rhs);
      return convert(compared, *resultType, false, location);
    }

    std::optional<sim::ShiftKind> shift;
    switch (kind) {
    case Binary::LogicalShiftLeft:
    case Binary::ArithmeticShiftLeft:
      shift = sim::ShiftKind::Left;
      break;
    case Binary::LogicalShiftRight:
      shift = sim::ShiftKind::Right;
      break;
    case Binary::ArithmeticShiftRight:
      shift = signedOp ? sim::ShiftKind::RightArith : sim::ShiftKind::Right;
      break;
    default:
      break;
    }
    if (shift) {
      Value shifted = sim::SimLogicShiftOp::create(
          builder, location, (*lhs).getType(), *shift, *lhs, *rhs);
      return convert(shifted, *resultType, signedOp, location);
    }

    if (kind == Binary::Power) {
      if (isSignedNode(children[1])) {
        std::optional<unsigned> width = sim::getPackedWidth((*rhs).getType());
        std::optional<StringRef> spelling = getConstantSpelling(children[1]);
        FailureOr<ParsedConstant> exponent =
            width && spelling ? parseSVInteger(*spelling, *width,
                                               getSemanticLocation(children[1]))
                              : FailureOr<ParsedConstant>(failure());
        if (failed(exponent) || !exponent->unknown.isZero() ||
            exponent->value.isNegative()) {
          unsupported(op) << " (signed dynamic or negative integral power)";
          return failure();
        }
      }
      auto logicType = cast<sim::LogicType>((*lhs).getType());
      auto planeType = builder.getIntegerType(logicType.getWidth());
      Type predicateType = sim::LogicType::get(function.getContext(), 1);
      auto logicConstant = [&](const APInt &bits,
                               const APInt &unknown) -> Value {
        return sim::SimLogicConstantOp::create(
            builder, location, logicType,
            builder.getIntegerAttr(planeType, bits),
            builder.getIntegerAttr(planeType, unknown));
      };
      Value value = logicConstant(APInt(logicType.getWidth(), 1),
                                  APInt(logicType.getWidth(), 0));
      Value base = *lhs;
      unsigned exponentWidth;
      if (auto type = dyn_cast<sim::LogicType>((*rhs).getType()))
        exponentWidth = type.getWidth();
      else
        exponentWidth = cast<IntegerType>((*rhs).getType()).getWidth();
      for (unsigned bit = 0; bit != exponentWidth; ++bit) {
        Value condition;
        if (isa<sim::LogicType>((*rhs).getType())) {
          condition = sim::SimLogicExtractOp::create(builder, location,
                                                     predicateType, *rhs, bit);
        } else {
          auto integerType = cast<IntegerType>((*rhs).getType());
          Value bitValue = *rhs;
          if (bit != 0) {
            Value amount = arith::ConstantOp::create(
                builder, location, integerType,
                builder.getIntegerAttr(integerType, bit));
            bitValue =
                arith::ShRUIOp::create(builder, location, bitValue, amount);
          }
          if (integerType.getWidth() != 1)
            bitValue = arith::TruncIOp::create(builder, location,
                                               builder.getI1Type(), bitValue);
          condition = sim::SimLogicFromBitsOp::create(builder, location,
                                                      predicateType, bitValue);
        }
        Value multiplied = sim::SimLogicBinaryOp::create(
            builder, location, logicType, sim::BinaryKind::Mul, value, base);
        value = sim::SimLogicMuxOp::create(builder, location, logicType,
                                           condition, multiplied, value);
        if (bit + 1 != exponentWidth)
          base = sim::SimLogicBinaryOp::create(
              builder, location, logicType, sim::BinaryKind::Mul, base, base);
      }
      Value lhsKnown = sim::SimLogicCompareOp::create(
          builder, location, predicateType, sim::CompareKind::Eq, *lhs, *lhs);
      Value rhsKnown;
      if (isa<sim::LogicType>((*rhs).getType()))
        rhsKnown = sim::SimLogicCompareOp::create(
            builder, location, predicateType, sim::CompareKind::Eq, *rhs, *rhs);
      else
        rhsKnown = sim::SimLogicConstantOp::create(
            builder, location, predicateType,
            builder.getIntegerAttr(builder.getI1Type(), 1),
            builder.getIntegerAttr(builder.getI1Type(), 0));
      Value known = sim::SimLogicLogicalOp::create(
          builder, location, predicateType, sim::LogicalKind::And, lhsKnown,
          rhsKnown);
      Value allUnknown = logicConstant(APInt(logicType.getWidth(), 0),
                                       APInt::getAllOnes(logicType.getWidth()));
      value = sim::SimLogicMuxOp::create(builder, location, logicType, known,
                                         value, allUnknown);
      return convert(value, *resultType, signedOp, location);
    }

    sim::BinaryKind binary;
    switch (kind) {
    case Binary::Add:
      binary = sim::BinaryKind::Add;
      break;
    case Binary::Subtract:
      binary = sim::BinaryKind::Sub;
      break;
    case Binary::Multiply:
      binary = sim::BinaryKind::Mul;
      break;
    case Binary::Divide:
      binary = signedOp ? sim::BinaryKind::SDiv : sim::BinaryKind::UDiv;
      break;
    case Binary::Mod:
      binary = signedOp ? sim::BinaryKind::SMod : sim::BinaryKind::UMod;
      break;
    case Binary::BinaryAnd:
      binary = sim::BinaryKind::And;
      break;
    case Binary::BinaryOr:
      binary = sim::BinaryKind::Or;
      break;
    case Binary::BinaryXor:
      binary = sim::BinaryKind::Xor;
      break;
    case Binary::BinaryXnor:
      binary = sim::BinaryKind::Xnor;
      break;
    default:
      unsupported(op) << " (four-state binary operator)";
      return failure();
    }
    Value value = sim::SimLogicBinaryOp::create(
        builder, location, (*lhs).getType(), binary, *lhs, *rhs);
    return convert(value, *resultType, signedOp, location);
  }

  std::optional<arith::CmpIPredicate> predicate;
  switch (kind) {
  case Binary::Equality:
  case Binary::CaseEquality:
    predicate = arith::CmpIPredicate::eq;
    break;
  case Binary::Inequality:
  case Binary::CaseInequality:
  case Binary::WildcardInequality:
    predicate = arith::CmpIPredicate::ne;
    break;
  case Binary::WildcardEquality:
    predicate = arith::CmpIPredicate::eq;
    break;
  case Binary::GreaterThanEqual:
    predicate =
        signedOp ? arith::CmpIPredicate::sge : arith::CmpIPredicate::uge;
    break;
  case Binary::GreaterThan:
    predicate =
        signedOp ? arith::CmpIPredicate::sgt : arith::CmpIPredicate::ugt;
    break;
  case Binary::LessThanEqual:
    predicate =
        signedOp ? arith::CmpIPredicate::sle : arith::CmpIPredicate::ule;
    break;
  case Binary::LessThan:
    predicate =
        signedOp ? arith::CmpIPredicate::slt : arith::CmpIPredicate::ult;
    break;
  default:
    break;
  }
  if (predicate) {
    Value compared =
        arith::CmpIOp::create(builder, location, *predicate, *lhs, *rhs);
    return convert(compared, *resultType, false, location);
  }

  Value value;
  switch (kind) {
  case Binary::Add:
    value = arith::AddIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::Subtract:
    value = arith::SubIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::Multiply:
    value = arith::MulIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::Divide:
    value = signedOp
                ? Value(arith::DivSIOp::create(builder, location, *lhs, *rhs))
                : Value(arith::DivUIOp::create(builder, location, *lhs, *rhs));
    break;
  case Binary::Mod:
    value = signedOp
                ? Value(arith::RemSIOp::create(builder, location, *lhs, *rhs))
                : Value(arith::RemUIOp::create(builder, location, *lhs, *rhs));
    break;
  case Binary::BinaryAnd:
    value = arith::AndIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::BinaryOr:
    value = arith::OrIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::BinaryXor:
    value = arith::XOrIOp::create(builder, location, *lhs, *rhs);
    break;
  case Binary::LogicalShiftLeft:
  case Binary::ArithmeticShiftLeft:
  case Binary::LogicalShiftRight:
  case Binary::ArithmeticShiftRight: {
    auto lhsType = cast<IntegerType>((*lhs).getType());
    auto rhsType = cast<IntegerType>((*rhs).getType());
    unsigned operationWidth = std::max(lhsType.getWidth(), rhsType.getWidth());
    auto operationType = builder.getIntegerType(operationWidth);
    bool arithmeticRight = kind == Binary::ArithmeticShiftRight && signedOp;
    FailureOr<Value> input =
        convert(*lhs, operationType, arithmeticRight, location);
    FailureOr<Value> amount = convert(*rhs, operationType, false, location);
    if (failed(input) || failed(amount))
      return failure();
    Value zero =
        arith::ConstantOp::create(builder, location, operationType,
                                  builder.getIntegerAttr(operationType, 0));
    Value width = arith::ConstantOp::create(
        builder, location, operationType,
        builder.getIntegerAttr(operationType, lhsType.getWidth()));
    Value oversized = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::uge, *amount, width);
    Value safeAmount =
        arith::SelectOp::create(builder, location, oversized, zero, *amount);
    Value shifted;
    if (kind == Binary::LogicalShiftLeft || kind == Binary::ArithmeticShiftLeft)
      shifted = arith::ShLIOp::create(builder, location, *input, safeAmount);
    else if (!arithmeticRight)
      shifted = arith::ShRUIOp::create(builder, location, *input, safeAmount);
    else
      shifted = arith::ShRSIOp::create(builder, location, *input, safeAmount);
    Value oversizedValue = zero;
    if (arithmeticRight) {
      Value negative = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::slt, *input, zero);
      Value allOnes = arith::ConstantOp::create(
          builder, location, operationType,
          builder.getIntegerAttr(operationType,
                                 APInt::getAllOnes(operationWidth)));
      oversizedValue =
          arith::SelectOp::create(builder, location, negative, allOnes, zero);
    }
    value = arith::SelectOp::create(builder, location, oversized,
                                    oversizedValue, shifted);
    if (operationType != lhsType)
      value = arith::TruncIOp::create(builder, location, lhsType, value);
    break;
  }
  default:
    unsupported(op) << " (two-state binary operator)";
    return failure();
  }
  return convert(value, *resultType, signedOp, location);
}

FailureOr<Value> UnitLowering::conditionalPredicate(Value value,
                                                    Location location) {
  if (isa<sim::StringType, FloatType>(value.getType())) {
    FailureOr<Value> truth = truthValue(value, location);
    if (failed(truth))
      return failure();
    return sim::SimLogicFromBitsOp::create(
               builder, location, sim::LogicType::get(function.getContext(), 1),
               *truth)
        .getResult();
  }
  FailureOr<Value> scalar = toPackedScalar(value, location);
  if (failed(scalar))
    return failure();
  if (isa<sim::LogicType>((*scalar).getType()))
    return sim::SimLogicReductionOp::create(
               builder, location, sim::LogicType::get(function.getContext(), 1),
               sim::ReductionKind::Or, *scalar)
        .getResult();
  FailureOr<Value> truth = truthValue(*scalar, location);
  if (failed(truth))
    return failure();
  return sim::SimLogicFromBitsOp::create(
             builder, location, sim::LogicType::get(function.getContext(), 1),
             *truth)
      .getResult();
}

FailureOr<Value> UnitLowering::conditionalEqual(Value lhs, Value rhs, Type type,
                                                Location location,
                                                bool caseEquality) {
  auto falseValue = [&]() -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false));
  };
  if (Type scalarType = sim::getPackedScalarType(type)) {
    FailureOr<Value> left = toPackedScalar(lhs, location);
    FailureOr<Value> right = toPackedScalar(rhs, location);
    if (failed(left) || failed(right))
      return failure();
    if (isa<sim::LogicType>(scalarType)) {
      if (caseEquality)
        return sim::SimLogicCompareOp::create(
                   builder, location, builder.getI1Type(),
                   sim::CompareKind::CaseEq, *left, *right)
            .getResult();
      Value compared = sim::SimLogicCompareOp::create(
          builder, location, sim::LogicType::get(function.getContext(), 1),
          sim::CompareKind::Eq, *left, *right);
      return sim::SimLogicIsTrueOp::create(builder, location,
                                           builder.getI1Type(), compared)
          .getResult();
    }
    return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                 *left, *right)
        .getResult();
  }
  if (isa<FloatType>(type))
    return arith::CmpFOp::create(builder, location, arith::CmpFPredicate::OEQ,
                                 lhs, rhs)
        .getResult();
  if (isa<sim::StringType>(type)) {
    Value compared =
        sim::SimStringCompareOp::create(builder, location, builder.getI32Type(),
                                        lhs, rhs, builder.getBoolAttr(false));
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI32Type(), builder.getI32IntegerAttr(0));
    return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                 compared, zero)
        .getResult();
  }
  if (isa<sim::ClassHandleType>(type)) {
    Value left = sim::SimClassIdOp::create(builder, location, lhs);
    Value right = sim::SimClassIdOp::create(builder, location, rhs);
    return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                 left, right)
        .getResult();
  }
  if (isa<sim::EventType>(type))
    return sim::SimEventEqualOp::create(builder, location, builder.getI1Type(),
                                        lhs, rhs)
        .getResult();
  if (auto associative = dyn_cast<sim::AssocArrayType>(type)) {
    Value leftSize = sim::SimContainerSizeOp::create(builder, location,
                                                     builder.getI64Type(), lhs);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), rhs);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *start = addBlock();
    Block *falseBlock = addBlock();
    Block *loop = addBlock();
    Block *body = addBlock();
    Block *next = addBlock();
    Block *result = addBlock();
    loop->addArgument(associative.getKeyType(), location);
    loop->addArgument(builder.getI1Type(), location);
    next->addArgument(associative.getKeyType(), location);
    result->addArgument(builder.getI1Type(), location);
    cf::CondBranchOp::create(builder, location, sameSize, start, ValueRange{},
                             falseBlock, ValueRange{});
    setCurrent(start);
    Value defaultKey =
        createDefaultValue(builder, location, associative.getKeyType());
    FailureOr<std::pair<Value, Value>> first =
        traverseAssoc(lhs, defaultKey, 1, true, location);
    if (failed(first))
      return failure();
    cf::BranchOp::create(builder, location, loop,
                         ValueRange{first->first, first->second});
    setCurrent(loop);
    Value key = loop->getArgument(0);
    Value valid = loop->getArgument(1);
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    cf::CondBranchOp::create(builder, location, valid, body, ValueRange{},
                             result, ValueRange{trueValue});
    setCurrent(body);
    Value exists = sim::SimAssocExistsOp::create(builder, location,
                                                 builder.getI1Type(), rhs, key);
    Block *compare = addBlock();
    cf::CondBranchOp::create(builder, location, exists, compare, ValueRange{},
                             falseBlock, ValueRange{});
    setCurrent(compare);
    Value left = sim::SimAssocReadOp::create(
        builder, location, associative.getElementType(), lhs, key);
    Value right = sim::SimAssocReadOp::create(
        builder, location, associative.getElementType(), rhs, key);
    FailureOr<Value> equal = conditionalEqual(
        left, right, associative.getElementType(), location, caseEquality);
    if (failed(equal))
      return failure();
    cf::CondBranchOp::create(builder, location, *equal, next, ValueRange{key},
                             falseBlock, ValueRange{});
    setCurrent(next);
    FailureOr<std::pair<Value, Value>> following =
        traverseAssoc(lhs, next->getArgument(0), 1, false, location);
    if (failed(following))
      return failure();
    cf::BranchOp::create(builder, location, loop,
                         ValueRange{following->first, following->second});
    setCurrent(falseBlock);
    cf::BranchOp::create(builder, location, result, ValueRange{falseValue()});
    setCurrent(result);
    return result->getArgument(0);
  }
  if (isa<sim::DynamicArrayType, sim::QueueType>(type)) {
    Value leftSize = sim::SimContainerSizeOp::create(builder, location,
                                                     builder.getI64Type(), lhs);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), rhs);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *compareBlock = addBlock();
    Block *falseBlock = addBlock();
    Block *loopBlock = addBlock();
    Block *bodyBlock = addBlock();
    Block *resultBlock = addBlock();
    loopBlock->addArgument(builder.getI64Type(), location);
    resultBlock->addArgument(builder.getI1Type(), location);
    cf::CondBranchOp::create(builder, location, sameSize, compareBlock,
                             ValueRange{}, falseBlock, ValueRange{});

    setCurrent(compareBlock);
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    cf::BranchOp::create(builder, location, loopBlock, ValueRange{zero});

    setCurrent(loopBlock);
    Value index = loopBlock->getArgument(0);
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, index, leftSize);
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    cf::CondBranchOp::create(builder, location, inRange, bodyBlock,
                             ValueRange{}, resultBlock, ValueRange{trueValue});

    setCurrent(bodyBlock);
    Type elementType = isa<sim::DynamicArrayType>(type)
                           ? cast<sim::DynamicArrayType>(type).getElementType()
                           : cast<sim::QueueType>(type).getElementType();
    Value left = sim::SimContainerReadOp::create(builder, location, elementType,
                                                 lhs, index);
    Value right = sim::SimContainerReadOp::create(builder, location,
                                                  elementType, rhs, index);
    FailureOr<Value> equal =
        conditionalEqual(left, right, elementType, location, caseEquality);
    if (failed(equal))
      return failure();
    Block *nextBlock = addBlock();
    cf::CondBranchOp::create(builder, location, *equal, nextBlock, ValueRange{},
                             falseBlock, ValueRange{});
    setCurrent(nextBlock);
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
    Value next = arith::AddIOp::create(builder, location, index, one);
    cf::BranchOp::create(builder, location, loopBlock, ValueRange{next});

    setCurrent(falseBlock);
    Value falseValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(false));
    cf::BranchOp::create(builder, location, resultBlock,
                         ValueRange{falseValue});
    setCurrent(resultBlock);
    return resultBlock->getArgument(0);
  }
  if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type)) {
    unsigned count = sim::getAggregateNumElements(unionType);
    if (!unionType.getIsTagged()) {
      Value equal = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));
      for (unsigned index = 0; index < count; ++index) {
        Type elementType = sim::getAggregateElementType(unionType, index);
        if (!elementType)
          return failure();
        Value left = sim::SimUnionExtractOp::create(builder, location,
                                                    elementType, lhs, index);
        Value right = sim::SimUnionExtractOp::create(builder, location,
                                                     elementType, rhs, index);
        FailureOr<Value> elementEqual =
            conditionalEqual(left, right, elementType, location, caseEquality);
        if (failed(elementEqual))
          return failure();
        equal = arith::AndIOp::create(builder, location, equal, *elementEqual);
      }
      return equal;
    }

    Block *falseBlock = addBlock();
    Block *resultBlock = addBlock();
    resultBlock->addArgument(builder.getI1Type(), location);
    for (unsigned index = 0; index < count; ++index) {
      Value leftActive = sim::SimUnionIsActiveOp::create(
          builder, location, builder.getI1Type(), lhs, index);
      Value rightActive = sim::SimUnionIsActiveOp::create(
          builder, location, builder.getI1Type(), rhs, index);
      Value sameActive = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, leftActive, rightActive);
      Block *sameBlock = addBlock();
      Block *compareBlock = addBlock();
      Block *nextBlock = addBlock();
      cf::CondBranchOp::create(builder, location, sameActive, sameBlock,
                               ValueRange{}, falseBlock, ValueRange{});

      setCurrent(sameBlock);
      cf::CondBranchOp::create(builder, location, leftActive, compareBlock,
                               ValueRange{}, nextBlock, ValueRange{});

      setCurrent(compareBlock);
      Type elementType = sim::getAggregateElementType(unionType, index);
      if (!elementType)
        return failure();
      Value left = sim::SimUnionExtractOp::create(builder, location,
                                                  elementType, lhs, index);
      Value right = sim::SimUnionExtractOp::create(builder, location,
                                                   elementType, rhs, index);
      FailureOr<Value> elementEqual =
          conditionalEqual(left, right, elementType, location, caseEquality);
      if (failed(elementEqual))
        return failure();
      cf::CondBranchOp::create(builder, location, *elementEqual, nextBlock,
                               ValueRange{}, falseBlock, ValueRange{});
      setCurrent(nextBlock);
    }
    Value trueValue = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    cf::BranchOp::create(builder, location, resultBlock, ValueRange{trueValue});

    setCurrent(falseBlock);
    cf::BranchOp::create(builder, location, resultBlock,
                         ValueRange{falseValue()});
    setCurrent(resultBlock);
    return resultBlock->getArgument(0);
  }
  if (!isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type))
    return falseValue();

  Value equal = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(true));
  unsigned count = sim::getAggregateNumElements(type);
  for (unsigned index = 0; index < count; ++index) {
    Type elementType = sim::getAggregateElementType(type, index);
    if (!elementType)
      return failure();
    Value left = sim::SimAggregateExtractOp::create(builder, location,
                                                    elementType, lhs, index);
    Value right = sim::SimAggregateExtractOp::create(builder, location,
                                                     elementType, rhs, index);
    FailureOr<Value> elementEqual =
        conditionalEqual(left, right, elementType, location, caseEquality);
    if (failed(elementEqual))
      return failure();
    equal = arith::AndIOp::create(builder, location, equal, *elementEqual);
  }
  return equal;
}

FailureOr<Value> UnitLowering::logicalEqual(Value lhs, Value rhs, Type type,
                                            Location location) {
  Type logicType = sim::LogicType::get(function.getContext(), 1);
  auto known = [&](bool value) -> Value {
    Type i1 = builder.getI1Type();
    return sim::SimLogicConstantOp::create(
        builder, location, logicType, builder.getIntegerAttr(i1, value ? 1 : 0),
        builder.getIntegerAttr(i1, 0));
  };
  auto fromBits = [&](Value value) -> Value {
    return sim::SimLogicFromBitsOp::create(builder, location, logicType, value);
  };
  auto conjunction = [&](Value left, Value right) -> Value {
    return sim::SimLogicBinaryOp::create(builder, location, logicType,
                                         sim::BinaryKind::And, left, right);
  };

  if (Type scalarType = sim::getPackedScalarType(type)) {
    FailureOr<Value> left = toPackedScalar(lhs, location);
    FailureOr<Value> right = toPackedScalar(rhs, location);
    if (failed(left) || failed(right))
      return failure();
    if (isa<sim::LogicType>(scalarType))
      return sim::SimLogicCompareOp::create(builder, location, logicType,
                                            sim::CompareKind::Eq, *left, *right)
          .getResult();
    Value equal = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, *left, *right);
    return fromBits(equal);
  }
  if (isa<FloatType, sim::StringType, sim::ClassHandleType, sim::EventType>(
          type)) {
    FailureOr<Value> equal = conditionalEqual(lhs, rhs, type, location);
    if (failed(equal))
      return failure();
    return fromBits(*equal);
  }
  if (auto associative = dyn_cast<sim::AssocArrayType>(type)) {
    Value leftSize = sim::SimContainerSizeOp::create(builder, location,
                                                     builder.getI64Type(), lhs);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), rhs);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *start = addBlock();
    Block *falseBlock = addBlock();
    Block *loop = addBlock();
    Block *body = addBlock();
    Block *next = addBlock();
    Block *result = addBlock();
    loop->addArgument(associative.getKeyType(), location);
    loop->addArgument(builder.getI1Type(), location);
    loop->addArgument(logicType, location);
    next->addArgument(associative.getKeyType(), location);
    next->addArgument(logicType, location);
    result->addArgument(logicType, location);
    cf::CondBranchOp::create(builder, location, sameSize, start, ValueRange{},
                             falseBlock, ValueRange{});
    setCurrent(start);
    Value defaultKey =
        createDefaultValue(builder, location, associative.getKeyType());
    FailureOr<std::pair<Value, Value>> first =
        traverseAssoc(lhs, defaultKey, 1, true, location);
    if (failed(first))
      return failure();
    cf::BranchOp::create(builder, location, loop,
                         ValueRange{first->first, first->second, known(true)});
    setCurrent(loop);
    Value key = loop->getArgument(0);
    Value valid = loop->getArgument(1);
    Value accumulated = loop->getArgument(2);
    cf::CondBranchOp::create(builder, location, valid, body, ValueRange{},
                             result, ValueRange{accumulated});
    setCurrent(body);
    Value exists = sim::SimAssocExistsOp::create(builder, location,
                                                 builder.getI1Type(), rhs, key);
    Block *compare = addBlock();
    cf::CondBranchOp::create(builder, location, exists, compare, ValueRange{},
                             falseBlock, ValueRange{});
    setCurrent(compare);
    Value left = sim::SimAssocReadOp::create(
        builder, location, associative.getElementType(), lhs, key);
    Value right = sim::SimAssocReadOp::create(
        builder, location, associative.getElementType(), rhs, key);
    FailureOr<Value> elementEqual =
        logicalEqual(left, right, associative.getElementType(), location);
    if (failed(elementEqual))
      return failure();
    Value combined = conjunction(accumulated, *elementEqual);
    cf::BranchOp::create(builder, location, next, ValueRange{key, combined});
    setCurrent(next);
    FailureOr<std::pair<Value, Value>> following =
        traverseAssoc(lhs, next->getArgument(0), 1, false, location);
    if (failed(following))
      return failure();
    cf::BranchOp::create(
        builder, location, loop,
        ValueRange{following->first, following->second, next->getArgument(1)});
    setCurrent(falseBlock);
    cf::BranchOp::create(builder, location, result, ValueRange{known(false)});
    setCurrent(result);
    return result->getArgument(0);
  }
  if (isa<sim::DynamicArrayType, sim::QueueType>(type)) {
    Value leftSize = sim::SimContainerSizeOp::create(builder, location,
                                                     builder.getI64Type(), lhs);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), rhs);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *compareBlock = addBlock();
    Block *falseBlock = addBlock();
    Block *loopBlock = addBlock();
    Block *bodyBlock = addBlock();
    Block *resultBlock = addBlock();
    loopBlock->addArgument(builder.getI64Type(), location);
    loopBlock->addArgument(logicType, location);
    resultBlock->addArgument(logicType, location);
    cf::CondBranchOp::create(builder, location, sameSize, compareBlock,
                             ValueRange{}, falseBlock, ValueRange{});

    setCurrent(compareBlock);
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    cf::BranchOp::create(builder, location, loopBlock,
                         ValueRange{zero, known(true)});

    setCurrent(loopBlock);
    Value index = loopBlock->getArgument(0);
    Value accumulated = loopBlock->getArgument(1);
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, index, leftSize);
    cf::CondBranchOp::create(builder, location, inRange, bodyBlock,
                             ValueRange{}, resultBlock,
                             ValueRange{accumulated});

    setCurrent(bodyBlock);
    Type elementType = isa<sim::DynamicArrayType>(type)
                           ? cast<sim::DynamicArrayType>(type).getElementType()
                           : cast<sim::QueueType>(type).getElementType();
    Value left = sim::SimContainerReadOp::create(builder, location, elementType,
                                                 lhs, index);
    Value right = sim::SimContainerReadOp::create(builder, location,
                                                  elementType, rhs, index);
    FailureOr<Value> elementEqual =
        logicalEqual(left, right, elementType, location);
    if (failed(elementEqual))
      return failure();
    Value nextAccumulated = conjunction(accumulated, *elementEqual);
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
    Value next = arith::AddIOp::create(builder, location, index, one);
    cf::BranchOp::create(builder, location, loopBlock,
                         ValueRange{next, nextAccumulated});

    setCurrent(falseBlock);
    cf::BranchOp::create(builder, location, resultBlock,
                         ValueRange{known(false)});
    setCurrent(resultBlock);
    return resultBlock->getArgument(0);
  }
  if (isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type)) {
    Value equal = known(true);
    unsigned count = sim::getAggregateNumElements(type);
    for (unsigned index = 0; index < count; ++index) {
      Type elementType = sim::getAggregateElementType(type, index);
      if (!elementType)
        return failure();
      Value left = sim::SimAggregateExtractOp::create(builder, location,
                                                      elementType, lhs, index);
      Value right = sim::SimAggregateExtractOp::create(builder, location,
                                                       elementType, rhs, index);
      FailureOr<Value> elementEqual =
          logicalEqual(left, right, elementType, location);
      if (failed(elementEqual))
        return failure();
      equal = conjunction(equal, *elementEqual);
    }
    return equal;
  }

  // Tagged and untagged union comparison first requires matching active
  // alternatives. The existing exact comparator supplies a known result for
  // that structural check.
  FailureOr<Value> equal = conditionalEqual(lhs, rhs, type, location);
  if (failed(equal))
    return failure();
  return fromBits(*equal);
}

FailureOr<Value> UnitLowering::mergeConditionalValues(Value condition,
                                                      Value trueValue,
                                                      Value falseValue,
                                                      Type type,
                                                      Location location) {
  if (sim::getPackedScalarType(type)) {
    FailureOr<Value> leftScalar = toPackedScalar(trueValue, location);
    FailureOr<Value> rightScalar = toPackedScalar(falseValue, location);
    if (failed(leftScalar) || failed(rightScalar))
      return failure();
    FailureOr<Value> left = toLogic(*leftScalar, location);
    FailureOr<Value> right = toLogic(*rightScalar, location);
    if (failed(left) || failed(right))
      return failure();
    Value merged = sim::SimLogicMuxOp::create(
        builder, location, (*left).getType(), condition, *left, *right);
    return convert(merged, type, false, location);
  }
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
    SmallVector<Value> elements;
    unsigned count = sim::getAggregateNumElements(array);
    elements.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
      Type elementType = array.getElementType();
      Value left = sim::SimAggregateExtractOp::create(
          builder, location, elementType, trueValue, index);
      Value right = sim::SimAggregateExtractOp::create(
          builder, location, elementType, falseValue, index);
      FailureOr<Value> equal =
          conditionalEqual(left, right, elementType, location);
      if (failed(equal))
        return failure();
      Value defaultValue = createDefaultValue(builder, location, elementType);
      if (!defaultValue) {
        emitError(location)
            << "cannot materialize conditional default for " << elementType;
        return failure();
      }
      elements.push_back(arith::SelectOp::create(builder, location, *equal,
                                                 left, defaultValue));
    }
    return sim::SimAggregateConstructOp::create(builder, location, type,
                                                elements)
        .getResult();
  }
  if (isa<sim::DynamicArrayType, sim::QueueType>(type)) {
    Type elementType = isa<sim::DynamicArrayType>(type)
                           ? cast<sim::DynamicArrayType>(type).getElementType()
                           : cast<sim::QueueType>(type).getElementType();
    Value leftSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), trueValue);
    Value rightSize = sim::SimContainerSizeOp::create(
        builder, location, builder.getI64Type(), falseValue);
    Value sameSize = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, leftSize, rightSize);
    Block *createBlock = addBlock();
    Block *defaultBlock = addBlock();
    Block *loopBlock = addBlock();
    Block *bodyBlock = addBlock();
    Block *resultBlock = addBlock();
    loopBlock->addArgument(builder.getI64Type(), location);
    resultBlock->addArgument(type, location);
    cf::CondBranchOp::create(builder, location, sameSize, createBlock,
                             ValueRange{}, defaultBlock, ValueRange{});

    setCurrent(defaultBlock);
    Value defaultContainer = createDefaultValue(builder, location, type);
    if (!defaultContainer)
      return failure();
    cf::BranchOp::create(builder, location, resultBlock,
                         ValueRange{defaultContainer});

    setCurrent(createBlock);
    Value result = sim::SimContainerCreateLikeOp::create(
        builder, location, type, trueValue, falseValue, leftSize);
    Value zero = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(0));
    cf::BranchOp::create(builder, location, loopBlock, ValueRange{zero});

    setCurrent(loopBlock);
    Value index = loopBlock->getArgument(0);
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, index, leftSize);
    cf::CondBranchOp::create(builder, location, inRange, bodyBlock,
                             ValueRange{}, resultBlock, ValueRange{result});

    setCurrent(bodyBlock);
    Value left = sim::SimContainerReadOp::create(builder, location, elementType,
                                                 trueValue, index);
    Value right = sim::SimContainerReadOp::create(
        builder, location, elementType, falseValue, index);
    FailureOr<Value> equal =
        conditionalEqual(left, right, elementType, location);
    if (failed(equal))
      return failure();
    Value defaultElement = createDefaultValue(builder, location, elementType);
    if (!defaultElement)
      return failure();
    Value selected = arith::SelectOp::create(builder, location, *equal, left,
                                             defaultElement);
    sim::SimContainerWriteOp::create(builder, location, result, index,
                                     selected);
    Value one = arith::ConstantOp::create(
        builder, location, builder.getI64Type(), builder.getI64IntegerAttr(1));
    Value next = arith::AddIOp::create(builder, location, index, one);
    cf::BranchOp::create(builder, location, loopBlock, ValueRange{next});

    setCurrent(resultBlock);
    return resultBlock->getArgument(0);
  }
  Value defaultValue = createDefaultValue(builder, location, type);
  if (!defaultValue) {
    emitError(location) << "cannot materialize ambiguous conditional default "
                        << type;
    return failure();
  }
  return defaultValue;
}

FailureOr<Value> UnitLowering::lowerConditionalExpression(
    semantic::SVConditionalExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  ArrayRef<int64_t> patternFlags = op.getConditionPatternFlags();
  if (op.getConditionCount() == 0 ||
      patternFlags.size() != op.getConditionCount()) {
    emitError(location) << "malformed conditional-expression metadata";
    return failure();
  }
  size_t conditionChildren = op.getConditionCount();
  for (int64_t flag : patternFlags) {
    if (flag != 0 && flag != 1) {
      emitError(location)
          << "conditional-expression pattern flags must be zero or one";
      return failure();
    }
    conditionChildren += static_cast<size_t>(flag);
  }
  if (children.size() != conditionChildren + 2) {
    emitError(location) << "malformed conditional-expression inventory";
    return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();

  ArrayRef<Operation *> conditions =
      ArrayRef<Operation *>(children).take_front(conditionChildren);
  Operation *trueExpression = children[conditionChildren];
  Operation *falseExpression = children[conditionChildren + 1];

  // An unknown condition has to merge both arm values, but each arm is lowered
  // exactly once and the ambiguous path routes through the same blocks. Giving
  // the ambiguous case its own copy of both arms doubles the work per nesting
  // level, so a chain of N conditionals costs 2^N.
  Block *trueBlock = addBlock();
  Block *falseBlock = addBlock();
  Block *ambiguousBlock = addBlock();
  Block *mergeBlock = addBlock();
  Type predicateType = sim::LogicType::get(function.getContext(), 1);
  Type i1Type = builder.getI1Type();
  // trueBlock(ambiguous), falseBlock(trueArmValue, ambiguous). The saved true
  // value is only meaningful when the ambiguous flag is set.
  trueBlock->addArgument(i1Type, location);
  falseBlock->addArgument(*resultType, location);
  falseBlock->addArgument(i1Type, location);
  ambiguousBlock->addArgument(*resultType, location);
  ambiguousBlock->addArgument(*resultType, location);
  mergeBlock->addArgument(*resultType, location);

  Value sawUnknown = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(false));
  // Placeholder for the true-arm slot on the paths that never evaluate it.
  Value unusedArm = createDefaultValue(builder, location, *resultType);
  if (!unusedArm)
    return failure();
  Value notAmbiguous = arith::ConstantOp::create(
      builder, location, i1Type, builder.getBoolAttr(false));
  size_t childIndex = 0;
  for (size_t conditionIndex = 0; conditionIndex < op.getConditionCount();
       ++conditionIndex) {
    Operation *expression = conditions[childIndex++];
    FailureOr<Value> input = lowerExpression(expression);
    if (failed(input))
      return failure();
    FailureOr<Value> predicate;
    if (patternFlags[conditionIndex]) {
      Operation *pattern = conditions[childIndex++];
      FailureOr<Value> matched =
          lowerPattern(*input, pattern, semantic::SVCaseCondition::Normal);
      if (failed(matched))
        return failure();
      predicate =
          conditionalPredicate(*matched, getSemanticLocation(expression));
    } else {
      predicate = conditionalPredicate(*input, getSemanticLocation(expression));
    }
    if (failed(predicate))
      return failure();

    Type bitsType = builder.getI1Type();
    Value bits =
        sim::SimLogicToBitsOp::create(builder, location, bitsType, *predicate);
    Value roundTrip =
        sim::SimLogicFromBitsOp::create(builder, location, predicateType, bits);
    Value known = sim::SimLogicCompareOp::create(
        builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
        *predicate, roundTrip);
    Value isTrue =
        sim::SimLogicIsTrueOp::create(builder, getSemanticLocation(expression),
                                      builder.getI1Type(), *predicate);
    Value isFalse = arith::AndIOp::create(
        builder, getSemanticLocation(expression), known,
        arith::XOrIOp::create(
            builder, getSemanticLocation(expression), isTrue,
            arith::ConstantOp::create(builder, getSemanticLocation(expression),
                                      builder.getI1Type(),
                                      builder.getBoolAttr(true))));
    sawUnknown = arith::OrIOp::create(
        builder, getSemanticLocation(expression), sawUnknown,
        arith::XOrIOp::create(
            builder, getSemanticLocation(expression), known,
            arith::ConstantOp::create(builder, getSemanticLocation(expression),
                                      builder.getI1Type(),
                                      builder.getBoolAttr(true))));

    Block *nonFalse = addBlock();
    cf::CondBranchOp::create(builder, getSemanticLocation(expression), isFalse,
                             falseBlock, ValueRange{unusedArm, notAmbiguous},
                             nonFalse, ValueRange{});
    setCurrent(nonFalse);
    if (conditionIndex + 1 != op.getConditionCount())
      continue;
    // Both the known-true and the ambiguous case start by evaluating the true
    // arm; the flag decides whether the false arm follows and merges.
    cf::BranchOp::create(builder, getSemanticLocation(expression), trueBlock,
                         ValueRange{sawUnknown});
  }

  auto lowerArm = [&](Operation *expression) -> FailureOr<Value> {
    if (isa<semantic::SVNullLiteralOp>(expression)) {
      Value value = createDefaultValue(builder, getSemanticLocation(expression),
                                       *resultType);
      return value ? FailureOr<Value>(value) : FailureOr<Value>(failure());
    }
    FailureOr<Value> value = lowerExpression(expression);
    if (failed(value))
      return failure();
    return convert(*value, *resultType, isSignedNode(expression),
                   getSemanticLocation(expression), isSignedNode(op));
  };

  setCurrent(trueBlock);
  FailureOr<Value> trueResult = lowerArm(trueExpression);
  if (failed(trueResult))
    return failure();
  // Known-true takes this value; ambiguous carries it into the false arm.
  cf::CondBranchOp::create(builder, getSemanticLocation(trueExpression),
                           trueBlock->getArgument(0), falseBlock,
                           ValueRange{*trueResult, trueBlock->getArgument(0)},
                           mergeBlock, ValueRange{*trueResult});

  setCurrent(falseBlock);
  FailureOr<Value> falseResult = lowerArm(falseExpression);
  if (failed(falseResult))
    return failure();
  cf::CondBranchOp::create(
      builder, getSemanticLocation(falseExpression), falseBlock->getArgument(1),
      ambiguousBlock, ValueRange{falseBlock->getArgument(0), *falseResult},
      mergeBlock, ValueRange{*falseResult});

  setCurrent(ambiguousBlock);
  Type i1 = builder.getI1Type();
  Value ambiguousCondition = sim::SimLogicConstantOp::create(
      builder, location, predicateType, builder.getIntegerAttr(i1, 0),
      builder.getIntegerAttr(i1, 1));
  FailureOr<Value> merged = mergeConditionalValues(
      ambiguousCondition, ambiguousBlock->getArgument(0),
      ambiguousBlock->getArgument(1), *resultType, location);
  if (failed(merged))
    return failure();
  cf::BranchOp::create(builder, location, mergeBlock, ValueRange{*merged});

  setCurrent(mergeBlock);
  return mergeBlock->getArgument(0);
}

FailureOr<Value> UnitLowering::lowerInside(semantic::SVInsideExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op.getItemCount() <= 0 ||
      children.size() != static_cast<size_t>(1 + op.getItemCount())) {
    emitError(location) << "malformed inside item inventory";
    return failure();
  }
  FailureOr<Value> loweredSelector = lowerExpression(children.front());
  if (failed(loweredSelector))
    return failure();
  FailureOr<Value> scalarSelector = toPackedScalar(*loweredSelector, location);
  if (failed(scalarSelector))
    return failure();
  Value selector = *scalarSelector;
  bool logic = isa<sim::LogicType>(selector.getType());
  Value matched;
  if (logic) {
    auto i1 = builder.getI1Type();
    matched = sim::SimLogicConstantOp::create(
        builder, location, sim::LogicType::get(function.getContext(), 1),
        builder.getIntegerAttr(i1, 0), builder.getIntegerAttr(i1, 0));
  } else {
    matched = arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(false));
  }

  auto combine = [&](Value lhs, Value rhs, bool conjunction,
                     Location itemLocation) -> Value {
    if (logic)
      return sim::SimLogicLogicalOp::create(
          builder, itemLocation, sim::LogicType::get(function.getContext(), 1),
          conjunction ? sim::LogicalKind::And : sim::LogicalKind::Or, lhs, rhs);
    return conjunction
               ? Value(arith::AndIOp::create(builder, itemLocation, lhs, rhs))
               : Value(arith::OrIOp::create(builder, itemLocation, lhs, rhs));
  };
  auto compare = [&](Value candidate, sim::CompareKind logicKind,
                     arith::CmpIPredicate integerKind,
                     Location itemLocation) -> FailureOr<Value> {
    FailureOr<Value> normalized =
        convert(candidate, (*loweredSelector).getType(), false, itemLocation);
    if (failed(normalized))
      return failure();
    FailureOr<Value> scalar = toPackedScalar(*normalized, itemLocation);
    if (failed(scalar))
      return failure();
    if ((*scalar).getType() != selector.getType()) {
      emitError(itemLocation)
          << "inside item does not normalize to the selector type";
      return failure();
    }
    if (logic)
      return sim::SimLogicCompareOp::create(
                 builder, itemLocation,
                 sim::LogicType::get(function.getContext(), 1), logicKind,
                 selector, *scalar)
          .getResult();
    return arith::CmpIOp::create(builder, itemLocation, integerKind, selector,
                                 *scalar)
        .getResult();
  };
  std::function<LogicalResult(Value, Location)> addCandidate =
      [&](Value candidate, Location itemLocation) -> LogicalResult {
    if (auto array = dyn_cast<sim::UnpackedArrayType>(candidate.getType())) {
      unsigned count = sim::getAggregateNumElements(array);
      for (unsigned index = 0; index < count; ++index) {
        Value element = sim::SimAggregateExtractOp::create(
            builder, itemLocation, array.getElementType(), candidate, index);
        if (failed(addCandidate(element, itemLocation)))
          return failure();
      }
      return success();
    }
    FailureOr<Value> equal = compare(candidate, sim::CompareKind::WildEq,
                                     arith::CmpIPredicate::eq, itemLocation);
    if (failed(equal))
      return failure();
    matched = combine(matched, *equal, false, itemLocation);
    return success();
  };

  for (Operation *item : ArrayRef<Operation *>(children).drop_front()) {
    Location itemLocation = getSemanticLocation(item);
    if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(item)) {
      if (range.getRangeKind() != semantic::SVValueRangeKind::Simple) {
        emitError(itemLocation)
            << "real-valued tolerance ranges are not executable";
        return failure();
      }
      SmallVector<Operation *> endpoints = getChildren(item);
      if (endpoints.size() != 2) {
        emitError(itemLocation) << "malformed inside range inventory";
        return failure();
      }
      Value inRange;
      bool signedSelector = isSignedNode(children.front());
      if (!isUnboundedEndpoint(endpoints[0])) {
        FailureOr<Value> lower = lowerExpression(endpoints[0]);
        if (failed(lower))
          return failure();
        FailureOr<Value> above = compare(
            *lower,
            signedSelector ? sim::CompareKind::SGE : sim::CompareKind::UGE,
            signedSelector ? arith::CmpIPredicate::sge
                           : arith::CmpIPredicate::uge,
            itemLocation);
        if (failed(above))
          return failure();
        inRange = *above;
      }
      if (!isUnboundedEndpoint(endpoints[1])) {
        FailureOr<Value> upper = lowerExpression(endpoints[1]);
        if (failed(upper))
          return failure();
        FailureOr<Value> below = compare(
            *upper,
            signedSelector ? sim::CompareKind::SLE : sim::CompareKind::ULE,
            signedSelector ? arith::CmpIPredicate::sle
                           : arith::CmpIPredicate::ule,
            itemLocation);
        if (failed(below))
          return failure();
        inRange =
            inRange ? combine(inRange, *below, true, itemLocation) : *below;
      }
      if (!inRange) {
        emitError(itemLocation)
            << "inside range cannot have two unbounded endpoints";
        return failure();
      }
      matched = combine(matched, inRange, false, itemLocation);
      continue;
    }
    FailureOr<Value> candidate = lowerExpression(item);
    if (failed(candidate) || failed(addCandidate(*candidate, itemLocation)))
      return failure();
  }
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  return convert(matched, *resultType, false, location);
}

} // namespace obelisk::simlowering
