//===- LowerUnitArrayQuerySystemCalls.cpp - Lower array inquiries --------===//

#include "LowerUnit.h"

#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<Value>
UnitLowering::lowerArrayQuerySystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();
  auto i32 = builder.getI32Type();

  auto getStringLiteral = [&](Operation *child) {
    Operation *spelling = child;
    while (isa<semantic::SVConversionExpressionOp>(spelling)) {
      SmallVector<Operation *> convertedChildren = getChildren(spelling);
      if (convertedChildren.size() != 1)
        break;
      spelling = convertedChildren.front();
    }
    return dyn_cast<semantic::SVStringLiteralOp>(spelling);
  };
  auto convertResult = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return convert(value, *type, true, location);
  };

  bool isDimensionCount =
      name == "$dimensions" || name == "$unpacked_dimensions";
  bool isRangeQuery = name == "$left" || name == "$right" || name == "$low" ||
                      name == "$high" || name == "$increment" ||
                      name == "$size";
  if (isDimensionCount || isRangeQuery) {
    size_t maximumArguments = isDimensionCount ? 1 : 2;
    if (children.empty() || children.size() > maximumArguments) {
      emitError(location) << name << " requires "
                          << (isDimensionCount ? "exactly one argument"
                                               : "one or two arguments");
      return failure();
    }
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType) {
      emitError(getSemanticLocation(children.front()))
          << name << " argument has no elaborated semantic type";
      return failure();
    }

    SmallVector<SemanticDimension> dimensions =
        getSemanticDimensions(semanticType.getValue());
    if (isDimensionCount) {
      uint64_t count = dimensions.size();
      if (name == "$unpacked_dimensions") {
        count = 0;
        for (const SemanticDimension &dimension : dimensions) {
          if (!dimension.unpacked)
            break;
          ++count;
        }
      }
      // Dimension inquiry functions are unevaluated and always return a
      // signed 32-bit integer. Match the specified modulo-2^32 conversion.
      Value result = arith::ConstantOp::create(
          builder, location, i32,
          builder.getIntegerAttr(i32, APInt(32, count)));
      return convertResult(result);
    }

    if (dimensions.empty()) {
      emitError(getSemanticLocation(children.front()))
          << name << " argument has no queryable dimension";
      return failure();
    }

    auto rangeExtent = [](int64_t left,
                          int64_t right) -> std::optional<uint64_t> {
      uint64_t lhs = static_cast<uint64_t>(left);
      uint64_t rhs = static_cast<uint64_t>(right);
      uint64_t distance = left >= right ? lhs - rhs : rhs - lhs;
      if (distance == std::numeric_limits<uint64_t>::max())
        return std::nullopt;
      return distance + 1;
    };
    auto fixedQueryValue =
        [&](const SemanticDimension &dimension) -> std::optional<APInt> {
      if (!dimension.isFixed())
        return std::nullopt;
      int64_t value;
      if (name == "$left")
        value = dimension.left;
      else if (name == "$right")
        value = dimension.right;
      else if (name == "$low")
        value = std::min(dimension.left, dimension.right);
      else if (name == "$high")
        value = std::max(dimension.left, dimension.right);
      else if (name == "$increment")
        value = dimension.left >= dimension.right ? 1 : -1;
      else {
        std::optional<uint64_t> extent =
            rangeExtent(dimension.left, dimension.right);
        if (!extent)
          return std::nullopt;
        return APInt(32, *extent);
      }
      return APInt(32, static_cast<uint64_t>(value), true);
    };

    // A string has one packed, runtime-sized dimension. A literal string is
    // nevertheless a known object value and can be answered here without
    // introducing a runtime string representation.
    auto stringLiteral = getStringLiteral(children.front());
    auto queryValue =
        [&](const SemanticDimension &dimension) -> std::optional<APInt> {
      if (std::optional<APInt> value = fixedQueryValue(dimension))
        return value;
      if (dimension.kind != SemanticDimensionKind::String || !stringLiteral)
        return std::nullopt;
      uint64_t size = stringLiteral.getConstantValue().size();
      if (name == "$left" || name == "$low")
        return APInt(32, 0);
      if (name == "$right" || name == "$high")
        return APInt(32, size - 1);
      if (name == "$increment")
        return APInt(32, static_cast<uint64_t>(-1), true);
      return APInt(32, size);
    };

    SmallVector<Value> values;
    values.reserve(dimensions.size());
    for (auto [dimensionIndex, dimension] : llvm::enumerate(dimensions)) {
      if (dimension.kind == SemanticDimensionKind::AssociativeArray &&
          dimensionIndex == 0) {
        FailureOr<Type> normalizedIndex =
            normalizeSemanticType(dimension.indexType, location);
        if (failed(normalizedIndex))
          return failure();
        if (isa<sim::StringType>(*normalizedIndex)) {
          emitError(getSemanticLocation(children.front()))
              << name << " is not defined for string-key associative arrays";
          return failure();
        }
        FailureOr<Value> container = lowerExpression(children.front());
        if (failed(container) ||
            !isa<sim::AssocArrayType>((*container).getType())) {
          emitError(getSemanticLocation(children.front()))
              << name << " requires an associative-array value";
          return failure();
        }
        auto associative = cast<sim::AssocArrayType>((*container).getType());
        if (children.size() != 1) {
          emitError(location)
              << "a dimension selector is not supported for associative "
                 "array queries";
          return failure();
        }
        if (name == "$size") {
          Value size = sim::SimContainerSizeOp::create(
              builder, location, builder.getI64Type(), *container);
          return convertResult(size);
        }
        if (name == "$left")
          return convertResult(arith::ConstantOp::create(
              builder, location, i32, builder.getI32IntegerAttr(0)));
        if (name == "$right")
          return convertResult(arith::ConstantOp::create(
              builder, location, i32, builder.getI32IntegerAttr(-1)));
        if (name == "$increment")
          return convertResult(arith::ConstantOp::create(
              builder, location, i32, builder.getI32IntegerAttr(-1)));

        Value defaultKey =
            createDefaultValue(builder, location, associative.getKeyType());
        bool first = name == "$low";
        FailureOr<std::pair<Value, Value>> traversed = traverseAssoc(
            *container, defaultKey, first ? 1 : -1, true, location);
        if (failed(traversed))
          return failure();
        FailureOr<Type> queryType = getNormalizedSemanticType(op);
        if (failed(queryType))
          return failure();
        FailureOr<Value> key =
            convert(traversed->first, *queryType, associative.getSignedKey(),
                    location, true);
        if (failed(key))
          return failure();
        Value empty;
        if (auto logic = dyn_cast<sim::LogicType>(*queryType)) {
          Type plane = builder.getIntegerType(logic.getWidth());
          empty = sim::SimLogicConstantOp::create(
              builder, location, logic,
              builder.getIntegerAttr(plane, APInt(logic.getWidth(), 0)),
              builder.getIntegerAttr(plane,
                                     APInt::getAllOnes(logic.getWidth())));
        } else {
          empty = createDefaultValue(builder, location, *queryType);
        }
        return arith::SelectOp::create(builder, location, traversed->second,
                                       *key, empty)
            .getResult();
      }
      if ((dimension.kind == SemanticDimensionKind::DynamicArray ||
           dimension.kind == SemanticDimensionKind::Queue) &&
          dimensionIndex == 0) {
        FailureOr<Value> container = lowerExpression(children.front());
        if (failed(container) || !isa<sim::DynamicArrayType, sim::QueueType>(
                                     (*container).getType())) {
          emitError(getSemanticLocation(children.front()))
              << name << " requires a sequential container value";
          return failure();
        }
        Value runtimeSize = sim::SimContainerSizeOp::create(
            builder, location, builder.getI64Type(), *container);
        Value queried;
        if (name == "$left" || name == "$low")
          queried = arith::ConstantOp::create(builder, location, i32,
                                              builder.getI32IntegerAttr(0));
        else if (name == "$increment")
          queried = arith::ConstantOp::create(builder, location, i32,
                                              builder.getI32IntegerAttr(-1));
        else {
          if (name != "$size") {
            Value one = arith::ConstantOp::create(builder, location,
                                                  builder.getI64Type(),
                                                  builder.getI64IntegerAttr(1));
            runtimeSize =
                arith::SubIOp::create(builder, location, runtimeSize, one);
          }
          queried =
              arith::TruncIOp::create(builder, location, i32, runtimeSize);
        }
        values.push_back(queried);
        continue;
      }
      std::optional<APInt> value = queryValue(dimension);
      if (!value) {
        emitError(getSemanticLocation(children.front()))
            << name
            << " requires the runtime value of a dynamically sized object";
        return failure();
      }
      values.push_back(arith::ConstantOp::create(
          builder, location, i32, builder.getIntegerAttr(i32, *value)));
    }

    if (children.size() == 1) {
      return convertResult(values.front());
    }

    // The dimension selector is evaluated exactly once. Case equality against
    // each valid one-based index preserves X/Z: an unknown, non-positive, or
    // out-of-range selector falls through to the all-X result.
    FailureOr<Value> index = lowerExpression(children[1]);
    if (failed(index))
      return failure();
    FailureOr<Value> logicIndex =
        toLogic(*index, getSemanticLocation(children[1]));
    if (failed(logicIndex))
      return failure();
    auto indexType = cast<sim::LogicType>((*logicIndex).getType());
    unsigned indexWidth = indexType.getWidth();
    bool indexSigned = isSignedNode(children[1]);
    auto canRepresentPositive = [&](uint64_t value) {
      if (!indexWidth)
        return false;
      if (!indexSigned)
        return indexWidth >= 64 || value < (uint64_t(1) << indexWidth);
      if (indexWidth > 64)
        return true;
      if (indexWidth == 64)
        return value <=
               static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
      if (indexWidth == 1)
        return false;
      return value <= (uint64_t(1) << (indexWidth - 1)) - 1;
    };

    auto resultType = sim::LogicType::get(function.getContext(), 32);
    auto createLogicConstant = [&](sim::LogicType type, const APInt &value,
                                   const APInt &unknown) -> Value {
      auto planeType = builder.getIntegerType(type.getWidth());
      return sim::SimLogicConstantOp::create(
                 builder, location, type,
                 builder.getIntegerAttr(planeType, value),
                 builder.getIntegerAttr(planeType, unknown))
          .getResult();
    };
    Value result =
        createLogicConstant(resultType, APInt(32, 0), APInt::getAllOnes(32));
    for (auto [zeroBased, value] : llvm::enumerate(values)) {
      uint64_t oneBased = zeroBased + 1;
      if (!canRepresentPositive(oneBased))
        continue;
      Value expected = createLogicConstant(
          indexType, APInt(indexWidth, oneBased), APInt(indexWidth, 0));
      Value matches = sim::SimLogicCompareOp::create(
          builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
          *logicIndex, expected);
      Value queryResult =
          sim::SimLogicFromBitsOp::create(builder, location, resultType, value);
      result = arith::SelectOp::create(builder, location, matches, queryResult,
                                       result);
    }
    return convertResult(result);
  }

  op.emitOpError("is not a supported array-query system call");
  return failure();
}

} // namespace obelisk::simlowering
