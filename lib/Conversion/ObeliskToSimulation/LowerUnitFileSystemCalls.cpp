//===- LowerUnitFileSystemCalls.cpp - Lower file I/O semantics ----------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<Value>
UnitLowering::lowerFileSystemCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  StringRef name = op.getCalleeName();
  Value context = function.getBody().front().getArgument(0);
  auto i32 = builder.getI32Type();
  auto i64 = builder.getI64Type();

  auto constant = [&](IntegerType type, int64_t value) -> Value {
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getIntegerAttr(type, value));
  };
  auto lowerInteger = [&](Operation *child,
                          IntegerType type) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    return convert(*value, type, isSignedNode(child), location);
  };
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
  auto lowerBytes = [&](Operation *child) -> FailureOr<Value> {
    auto literal = getStringLiteral(child);
    if (!literal) {
      emitError(getSemanticLocation(child))
          << "only literal strings are supported by this system call";
      return failure();
    }
    return sim::SimBytesConstantOp::create(builder,
                                           getSemanticLocation(literal),
                                           literal.getConstantValue())
        .getResult();
  };
  auto convertResult = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return convert(value, *type, true, location);
  };
  auto dummyTaskResult = [&]() -> Value {
    return constant(builder.getI1Type(), 0);
  };

  if (name == "$fopen") {
    if (children.size() != 1 && children.size() != 2) {
      emitError(location) << "$fopen requires one or two arguments";
      return failure();
    }
    Value descriptor;
    bool literalPath = static_cast<bool>(getStringLiteral(children[0]));
    bool literalMode = children.size() == 2 &&
                       static_cast<bool>(getStringLiteral(children[1]));
    if (literalPath && (children.size() == 1 || literalMode)) {
      FailureOr<Value> path = lowerBytes(children[0]);
      if (failed(path))
        return failure();
      if (children.size() == 1)
        descriptor = sim::SimFileOpenMCDOp::create(builder, location, i32,
                                                   context, *path)
                         .getDescriptor();
      else {
        FailureOr<Value> mode = lowerBytes(children[1]);
        if (failed(mode))
          return failure();
        descriptor = sim::SimFileOpenOp::create(builder, location, i32, context,
                                                *path, *mode)
                         .getDescriptor();
      }
    } else {
      FailureOr<Value> pathValue = lowerExpression(children[0]);
      if (failed(pathValue))
        return failure();
      Type stringType = sim::StringType::get(function.getContext());
      FailureOr<Value> path =
          convert(*pathValue, stringType, isSignedNode(children[0]), location);
      if (failed(path))
        return failure();
      if (children.size() == 1)
        descriptor = sim::SimFileOpenStringMCDOp::create(builder, location, i32,
                                                         context, *path)
                         .getDescriptor();
      else {
        FailureOr<Value> modeValue = lowerExpression(children[1]);
        if (failed(modeValue))
          return failure();
        FailureOr<Value> mode = convert(*modeValue, stringType,
                                        isSignedNode(children[1]), location);
        if (failed(mode))
          return failure();
        descriptor = sim::SimFileOpenStringOp::create(builder, location, i32,
                                                      context, *path, *mode)
                         .getDescriptor();
      }
    }
    return convertResult(descriptor);
  }

  auto oneDescriptor = [&]() -> FailureOr<Value> {
    if (children.size() != 1) {
      emitError(location) << name << " requires one descriptor argument";
      return failure();
    }
    return lowerInteger(children.front(), i32);
  };
  if (name == "$fclose" || name == "$feof" || name == "$ftell" ||
      name == "$rewind" || name == "$fgetc") {
    FailureOr<Value> descriptor = oneDescriptor();
    if (failed(descriptor))
      return failure();
    Value result;
    if (name == "$fclose") {
      sim::SimFileCloseOp::create(builder, location, context, *descriptor);
      return dummyTaskResult();
    } else if (name == "$feof")
      result = sim::SimFileEofOp::create(builder, location, i32, context,
                                         *descriptor);
    else if (name == "$ftell")
      result = sim::SimFileTellOp::create(builder, location, i64, context,
                                          *descriptor);
    else if (name == "$rewind")
      result = sim::SimFileRewindOp::create(builder, location, i32, context,
                                            *descriptor);
    else
      result = sim::SimFileGetcOp::create(builder, location, i32, context,
                                          *descriptor);
    return convertResult(result);
  }

  if (name == "$fflush") {
    if (children.size() > 1) {
      emitError(location) << "$fflush accepts zero or one argument";
      return failure();
    }
    Value descriptor = constant(i32, 0);
    if (!children.empty()) {
      FailureOr<Value> lowered = lowerInteger(children.front(), i32);
      if (failed(lowered))
        return failure();
      descriptor = *lowered;
    }
    sim::SimFileFlushOp::create(builder, location, context, descriptor);
    return dummyTaskResult();
  }

  if (name == "$ungetc") {
    if (children.size() != 2) {
      emitError(location) << "$ungetc requires a byte and descriptor";
      return failure();
    }
    FailureOr<Value> byte = lowerInteger(children[0], i32);
    FailureOr<Value> descriptor = lowerInteger(children[1], i32);
    if (failed(byte) || failed(descriptor))
      return failure();
    Value result = sim::SimFileUngetcOp::create(builder, location, i32, context,
                                                *byte, *descriptor);
    return convertResult(result);
  }

  if (name == "$fseek") {
    if (children.size() != 3) {
      emitError(location) << "$fseek requires descriptor, offset, and origin";
      return failure();
    }
    FailureOr<Value> descriptor = lowerInteger(children[0], i32);
    FailureOr<Value> offset = lowerInteger(children[1], i64);
    FailureOr<Value> origin = lowerInteger(children[2], i32);
    if (failed(descriptor) || failed(offset) || failed(origin))
      return failure();
    Value result = sim::SimFileSeekOp::create(builder, location, i32, context,
                                              *descriptor, *offset, *origin);
    return convertResult(result);
  }

  if (name == "$timeformat") {
    // IEEE 1800 20.4.2 gives every argument a default, and calling with none
    // restores them all.
    if (children.size() > 4) {
      emitError(location) << "$timeformat accepts at most four arguments";
      return failure();
    }
    auto argument = [&](size_t index, int64_t fallback) -> FailureOr<Value> {
      if (index >= children.size() ||
          isa<semantic::SVEmptyArgumentExpressionOp>(children[index]))
        return constant(i32, fallback);
      return lowerInteger(children[index], i32);
    };
    FailureOr<Value> units = argument(0, -9);
    FailureOr<Value> fractionDigits = argument(1, 0);
    FailureOr<Value> width = argument(3, 20);
    if (failed(units) || failed(fractionDigits) || failed(width))
      return failure();
    Value suffix;
    if (children.size() > 2 &&
        !isa<semantic::SVEmptyArgumentExpressionOp>(children[2])) {
      FailureOr<Value> lowered = lowerBytes(children[2]);
      if (failed(lowered))
        return failure();
      suffix = *lowered;
    } else {
      suffix = sim::SimBytesConstantOp::create(builder, location, "");
    }
    sim::SimTimeFormatOp::create(builder, location, context, *units,
                                 *fractionDigits, suffix, *width);
    return dummyTaskResult();
  }

  if (name == "$ferror") {
    if (children.size() != 2) {
      emitError(location) << "$ferror requires a descriptor and a string "
                             "destination";
      return failure();
    }
    FailureOr<Value> descriptor = lowerInteger(children[0], i32);
    if (failed(descriptor))
      return failure();
    Operation *actual = children[1];
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() == 2 &&
          isa<semantic::SVEmptyArgumentExpressionOp>(outputChildren[1]))
        actual = outputChildren.front();
    }
    FailureOr<Value> destination = lowerExpression(actual, true);
    if (failed(destination))
      return failure();
    auto reference = dyn_cast<sim::RefType>((*destination).getType());
    if (!reference || !isa<sim::StringType>(reference.getElementType())) {
      emitError(getSemanticLocation(actual))
          << "$ferror destination must be a string variable";
      return failure();
    }
    auto query = sim::SimFileErrorStringOp::create(
        builder, location,
        TypeRange{sim::StringType::get(function.getContext()), i32}, context,
        *descriptor);
    sim::SimRefStoreOp::create(builder, location, query.getMessage(),
                               *destination);
    return convertResult(query.getCode());
  }

  if (name == "$fgets" || name == "$fread") {
    if (children.size() != 2) {
      emitError(location)
          << name << " currently requires a packed destination and descriptor";
      return failure();
    }
    Operation *actual = children[0];
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() == 2 &&
          isa<semantic::SVEmptyArgumentExpressionOp>(outputChildren[1]))
        actual = outputChildren.front();
    }
    FailureOr<Value> destination = lowerExpression(actual, true);
    FailureOr<Value> descriptor = lowerInteger(children[1], i32);
    if (failed(destination) || failed(descriptor))
      return failure();
    auto reference = dyn_cast<sim::RefType>((*destination).getType());
    if (!reference) {
      emitError(getSemanticLocation(actual))
          << name << " destination must be a packed variable";
      return failure();
    }
    if (name == "$fgets" && isa<sim::StringType>(reference.getElementType())) {
      auto read = sim::SimFileGetlineStringOp::create(
          builder, location,
          TypeRange{sim::StringType::get(function.getContext()), i32}, context,
          *descriptor);
      sim::SimRefStoreOp::create(builder, location, read.getData(),
                                 *destination);
      return convertResult(read.getCount());
    }
    std::optional<unsigned> width =
        sim::getPackedWidth(reference.getElementType());
    if (!width) {
      emitError(getSemanticLocation(actual))
          << name << " destination must be a packed integral variable";
      return failure();
    }
    IntegerType packedType = builder.getIntegerType(*width);
    Value data;
    Value count;
    if (name == "$fgets") {
      auto read = sim::SimFileGetlineOp::create(
          builder, location, TypeRange{packedType, i32}, context, *descriptor);
      data = read.getData();
      count = read.getCount();
    } else {
      auto read = sim::SimFileReadPackedOp::create(
          builder, location, TypeRange{packedType, i32}, context, *descriptor);
      data = read.getData();
      count = read.getCount();
    }
    FailureOr<Value> converted =
        convert(data, reference.getElementType(), false, location);
    if (failed(converted))
      return failure();
    sim::SimRefStoreOp::create(builder, location, *converted, *destination);
    return convertResult(count);
  }

  op.emitOpError("is not a supported file system call");
  return failure();
}

} // namespace obelisk::simlowering
