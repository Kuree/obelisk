//===- LowerUnitSystemCalls.cpp - Lower system-call semantics ----------===//

#include "LowerUnit.h"

#include "obelisk/Dialect/ForeachLoopMetadata.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<Value>
UnitLowering::lowerSystemCall(semantic::SVCallExpressionOp op) {
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
  auto convertResult = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return convert(value, *type, true, location);
  };
  auto dummyTaskResult = [&]() -> Value {
    return constant(builder.getI1Type(), 0);
  };

  if (name == "$itor") {
    if (children.size() != 1) {
      emitError(location) << "$itor requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> result =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(result))
      return failure();
    return convertResult(*result);
  }

  if (name == "$rtoi") {
    if (children.size() != 1) {
      emitError(location) << "$rtoi requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value truncated =
        math::TruncOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(truncated);
  }

  if (name == "$bitstoreal") {
    if (children.size() != 1) {
      emitError(location) << "$bitstoreal requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> bits =
        convert(*input, builder.getI64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(bits))
      return failure();
    Value result = arith::BitcastOp::create(builder, location,
                                            builder.getF64Type(), *bits);
    return convertResult(result);
  }

  if (name == "$realtobits") {
    if (children.size() != 1) {
      emitError(location) << "$realtobits requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result = arith::BitcastOp::create(builder, location,
                                            builder.getI64Type(), *real);
    return convertResult(result);
  }

  if (name == "$bitstoshortreal") {
    if (children.size() != 1) {
      emitError(location) << "$bitstoshortreal requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> bits =
        convert(*input, builder.getI32Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(bits))
      return failure();
    Value result = arith::BitcastOp::create(builder, location,
                                            builder.getF32Type(), *bits);
    return convertResult(result);
  }

  if (name == "$shortrealtobits") {
    if (children.size() != 1) {
      emitError(location) << "$shortrealtobits requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF32Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result = arith::BitcastOp::create(builder, location,
                                            builder.getI32Type(), *real);
    return convertResult(result);
  }

  if (name == "$urandom" || name == "$srandom") {
    constexpr size_t maximum = 1;
    size_t minimum = name == "$urandom" ? 0 : 1;
    if (children.size() < minimum || children.size() > maximum) {
      emitError(location) << name
                          << (name == "$urandom"
                                  ? " accepts zero or one seed argument"
                                  : " requires exactly one seed argument");
      return failure();
    }
    if (!children.empty()) {
      FailureOr<Value> seed32 = lowerInteger(children.front(), i32);
      if (failed(seed32))
        return failure();
      Value seed = arith::ExtUIOp::create(builder, location, i64, *seed32);
      sim::SimRandomSeedOp::create(builder, location, context, seed);
    }
    if (name == "$srandom")
      return dummyTaskResult();
    Value value = sim::SimRandomNextOp::create(builder, location, i64, context);
    value = arith::TruncIOp::create(builder, location, i32, value);
    return convertResult(value);
  }

  if (name == "$urandom_range") {
    if (children.empty() || children.size() > 2) {
      emitError(location) << "$urandom_range requires one or two arguments";
      return failure();
    }
    FailureOr<Value> first32 = lowerInteger(children[0], i32);
    if (failed(first32))
      return failure();
    Value first = arith::ExtUIOp::create(builder, location, i64, *first32);
    Value second = constant(i64, 0);
    if (children.size() == 2) {
      FailureOr<Value> second32 = lowerInteger(children[1], i32);
      if (failed(second32))
        return failure();
      second = arith::ExtUIOp::create(builder, location, i64, *second32);
    }
    Value firstBelow = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, first, second);
    Value low =
        arith::SelectOp::create(builder, location, firstBelow, first, second);
    Value high =
        arith::SelectOp::create(builder, location, firstBelow, second, first);
    Value extent = arith::SubIOp::create(builder, location, high, low);
    extent = arith::AddIOp::create(builder, location, extent, constant(i64, 1));
    Value draw = sim::SimRandomBoundedOp::create(builder, location, i64,
                                                 context, extent);
    Value value = arith::AddIOp::create(builder, location, low, draw);
    return convertResult(value);
  }

  if (name == "$random") {
    if (children.size() > 1) {
      emitError(location) << "$random accepts zero or one seed argument";
      return failure();
    }
    FailureOr<Value> seedDestination = failure();
    if (!children.empty()) {
      seedDestination = lowerExpression(children.front(), true);
      if (failed(seedDestination)) {
        emitError(getSemanticLocation(children.front()))
            << "$random seed must be a writable integral variable";
        return failure();
      }
      FailureOr<Value> seedValue = loadReference(
          *seedDestination, getSemanticLocation(children.front()));
      if (failed(seedValue))
        return failure();
      FailureOr<Value> seed32 =
          convert(*seedValue, i32, isSignedNode(children.front()), location);
      if (failed(seed32))
        return failure();
      Value seed = arith::ExtUIOp::create(builder, location, i64, *seed32);
      sim::SimRandomSeedOp::create(builder, location, context, seed);
    }
    Value value = sim::SimRandomNextOp::create(builder, location, i64, context);
    value = arith::TruncIOp::create(builder, location, i32, value);
    if (succeeded(seedDestination)) {
      Type destinationType = getReferenceElementType(*seedDestination);
      FailureOr<Value> updated =
          convert(value, destinationType, true, location);
      if (failed(updated) ||
          failed(storeReference(*seedDestination, *updated, location)))
        return failure();
    }
    return convertResult(value);
  }

  if (name == "$dist_uniform") {
    if (children.size() != 3) {
      emitError(location) << "$dist_uniform requires exactly three arguments";
      return failure();
    }
    Operation *seed = children[0];
    if (auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(seed)) {
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() == 2)
        seed = outputChildren.front();
    }
    FailureOr<Value> seedDestination = lowerExpression(seed, true);
    if (failed(seedDestination)) {
      emitError(getSemanticLocation(seed))
          << "$dist_uniform seed must be a writable integral variable";
      return failure();
    }
    FailureOr<Value> seedValue =
        loadReference(*seedDestination, getSemanticLocation(seed));
    if (failed(seedValue))
      return failure();
    FailureOr<Value> seed32 =
        convert(*seedValue, i32, isSignedNode(seed), location);
    if (failed(seed32))
      return failure();
    Value extendedSeed =
        arith::ExtUIOp::create(builder, location, i64, *seed32);
    sim::SimRandomSeedOp::create(builder, location, context, extendedSeed);

    FailureOr<Value> first32 = lowerInteger(children[1], i32);
    FailureOr<Value> second32 = lowerInteger(children[2], i32);
    if (failed(first32) || failed(second32))
      return failure();
    Value first = arith::ExtSIOp::create(builder, location, i64, *first32);
    Value second = arith::ExtSIOp::create(builder, location, i64, *second32);
    Value firstBelow = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::slt, first, second);
    Value low =
        arith::SelectOp::create(builder, location, firstBelow, first, second);
    Value high =
        arith::SelectOp::create(builder, location, firstBelow, second, first);
    Value extent = arith::SubIOp::create(builder, location, high, low);
    extent = arith::AddIOp::create(builder, location, extent, constant(i64, 1));
    Value draw = sim::SimRandomBoundedOp::create(builder, location, i64,
                                                 context, extent);
    Value value = arith::AddIOp::create(builder, location, low, draw);
    value = arith::TruncIOp::create(builder, location, i32, value);

    Type destinationType = getReferenceElementType(*seedDestination);
    FailureOr<Value> updated = convert(value, destinationType, true, location);
    if (failed(updated) ||
        failed(storeReference(*seedDestination, *updated, location)))
      return failure();
    return convertResult(value);
  }

  if (name == "$sampled") {
    if (children.size() != 1) {
      emitError(location) << "$sampled requires exactly one argument";
      return failure();
    }
    emitError(location)
        << "$sampled requires concurrent assertion Preponed sampling, which "
           "is not executable yet";
    return failure();
  }

  if (name == "$past") {
    if (children.empty() || children.size() > 4) {
      emitError(location) << "$past requires one to four arguments";
      return failure();
    }
    if (children.size() >= 2 && !getConstantSpelling(children[1])) {
      emitError(getSemanticLocation(children[1]))
          << "$past history depth must be a constant integer";
      return failure();
    }
    if (children.size() >= 3) {
      emitError(getSemanticLocation(children[2]))
          << "$past gating expressions are not supported";
      return failure();
    }
    if (children.size() >= 4) {
      emitError(getSemanticLocation(children[3]))
          << "$past alternate clock arguments are not supported";
      return failure();
    }
    emitError(location)
        << "$past requires assertion-clock history, which is unavailable for "
           "this operand";
    return failure();
  }

  if (name == "$rose" || name == "$fell" || name == "$stable" ||
      name == "$changed") {
    if (children.size() != 1) {
      emitError(location) << name << " requires exactly one argument";
      return failure();
    }
    emitError(location)
        << name
        << " requires assertion-clock history, which is unavailable for this "
           "operand";
    return failure();
  }

  if (name == "$cast") {
    if (children.size() != 2) {
      emitError(location) << "$cast requires exactly two arguments";
      return failure();
    }
    Operation *destination = children.front();
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(destination)) {
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() == 2) {
        Operation *placeholder = outputChildren[1];
        while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
          SmallVector<Operation *> converted = getChildren(placeholder);
          if (converted.size() != 1)
            break;
          placeholder = converted.front();
        }
        if (isa<semantic::SVEmptyArgumentExpressionOp>(placeholder))
          destination = outputChildren.front();
      }
    }

    FailureOr<Value> destinationRef = lowerExpression(destination, true);
    if (failed(destinationRef))
      return failure();
    Type destinationType;
    if (auto ref = dyn_cast<sim::RefType>((*destinationRef).getType())) {
      destinationType = ref.getElementType();
    } else if (auto ref =
                   dyn_cast<sim::ManagedRefType>((*destinationRef).getType())) {
      destinationType = ref.getElementType();
    } else {
      emitError(location)
          << "$cast destination must be a variable or class property";
      return failure();
    }
    auto targetClass = dyn_cast<sim::ClassHandleType>(destinationType);
    if (!targetClass) {
      emitError(location) << "$cast currently requires class-handle operands";
      return failure();
    }
    FailureOr<Value> source =
        isa<semantic::SVNullLiteralOp>(children[1])
            ? FailureOr<Value>(sim::SimClassNullOp::create(
                                   builder, getSemanticLocation(children[1]),
                                   destinationType)
                                   .getResult())
            : lowerExpression(children[1]);
    if (failed(source) || !isa<sim::ClassHandleType>((*source).getType())) {
      emitError(location) << "$cast currently requires class-handle operands";
      return failure();
    }

    Value casted = sim::SimClassCastOp::create(builder, location,
                                               destinationType, *source);
    Value instance = sim::SimClassIsInstanceOp::create(
        builder, location, builder.getI1Type(), *source,
        FlatSymbolRefAttr::get(function.getContext(),
                               targetClass.getClassName().getRootReference()));
    Value sourceID = sim::SimClassIdOp::create(builder, location,
                                               builder.getI64Type(), *source);
    Value nullID = constant(builder.getI64Type(), 0);
    Value isNull = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, sourceID, nullID);
    Value succeeded = arith::OrIOp::create(builder, location, instance, isNull);
    Block *store = addBlock();
    Block *resume = addBlock();
    cf::CondBranchOp::create(builder, location, succeeded, store, resume);
    setCurrent(store);
    if (isa<sim::RefType>((*destinationRef).getType()))
      sim::SimRefStoreOp::create(builder, location, casted, *destinationRef);
    else
      sim::SimManagedStoreOp::create(builder, location, casted,
                                     *destinationRef);
    emitBranch(resume);
    setCurrent(resume);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType))
      return failure();
    return convert(succeeded, *resultType, false, location);
  }

  if (name == "$bits") {
    if (children.size() != 1) {
      emitError(location) << "$bits requires exactly one argument";
      return failure();
    }
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType) {
      emitError(getSemanticLocation(children.front()))
          << "$bits argument has no elaborated semantic type";
      return failure();
    }
    std::optional<uint64_t> width =
        getSemanticBitstreamWidth(semanticType.getValue());
    if (!width) {
      emitError(getSemanticLocation(children.front()))
          << "$bits of a dynamically sized bitstream is not yet executable";
      return failure();
    }
    // `$bits` is an inquiry function: its operand is unevaluated. Preserve
    // Slang/SystemVerilog's signed 32-bit result by retaining the low 32 bits
    // even for an exceptionally large elaborated type.
    Value result = arith::ConstantOp::create(
        builder, location, i32, builder.getIntegerAttr(i32, APInt(32, *width)));
    return convertResult(result);
  }

  if (name == "$isunbounded") {
    if (children.size() != 1) {
      emitError(location) << "$isunbounded requires exactly one argument";
      return failure();
    }
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType) {
      emitError(getSemanticLocation(children.front()))
          << "$isunbounded argument has no elaborated semantic type";
      return failure();
    }
    // `$isunbounded` is an inquiry function. Its operand is unevaluated, and
    // Slang records an unbounded parameter reference with !obelisk.unbounded.
    Value result = constant(builder.getI1Type(),
                            isa<ir::UnboundedType>(semanticType.getValue()));
    return convertResult(result);
  }

  if (name == "$typename") {
    if (children.size() != 1) {
      emitError(location) << "$typename requires exactly one argument";
      return failure();
    }
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType) {
      emitError(getSemanticLocation(children.front()))
          << "$typename argument has no elaborated semantic type";
      return failure();
    }

    StringRef spelling;
    Type type = semanticType.getValue();
    if (auto integral = dyn_cast<ir::IntegralType>(type)) {
      spelling = ir::stringifySVIntegralFlavor(integral.getFlavor());
      if (integral.getFlavor() == ir::SVIntegralFlavor::Generic)
        spelling = integral.getIsFourState() ? "logic" : "bit";
    } else if (isa<ir::StringType>(type))
      spelling = "string";
    else if (isa<ir::RealType>(type))
      spelling = "real";
    else if (isa<ir::ShortRealType>(type))
      spelling = "shortreal";
    else if (isa<ir::RealtimeType>(type))
      spelling = "realtime";
    else if (isa<ir::TimeType>(type))
      spelling = "time";
    else if (auto enumeration = dyn_cast<ir::EnumType>(type))
      spelling = enumeration.getName();
    else if (auto aggregate = dyn_cast<ir::SourceAggregateType>(type))
      spelling = aggregate.getName();
    else {
      emitError(getSemanticLocation(children.front()))
          << "$typename has no executable spelling for type " << type;
      return failure();
    }

    // `$typename` is an inquiry function; only its elaborated operand type is
    // observed. Materialize the spelling directly as a simulation string.
    Type resultType = sim::StringType::get(function.getContext());
    Value result = sim::SimStringLiteralOp::create(
        builder, location, resultType, builder.getStringAttr(spelling));
    return convertResult(result);
  }

  if (name == "$ceil") {
    if (children.size() != 1) {
      emitError(location) << "$ceil requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result =
        math::CeilOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$floor") {
    if (children.size() != 1) {
      emitError(location) << "$floor requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result =
        math::FloorOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$sqrt") {
    if (children.size() != 1) {
      emitError(location) << "$sqrt requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result =
        math::SqrtOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$exp") {
    if (children.size() != 1) {
      emitError(location) << "$exp requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result =
        math::ExpOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$ln") {
    if (children.size() != 1) {
      emitError(location) << "$ln requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result =
        math::LogOp::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$log10") {
    if (children.size() != 1) {
      emitError(location) << "$log10 requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerExpression(children.front());
    if (failed(input))
      return failure();
    FailureOr<Value> real =
        convert(*input, builder.getF64Type(), isSignedNode(children.front()),
                getSemanticLocation(children.front()));
    if (failed(real))
      return failure();
    Value result =
        math::Log10Op::create(builder, location, builder.getF64Type(), *real);
    return convertResult(result);
  }

  if (name == "$pow") {
    if (children.size() != 2) {
      emitError(location) << "$pow requires exactly two arguments";
      return failure();
    }
    FailureOr<Value> base = lowerExpression(children[0]);
    if (failed(base))
      return failure();
    FailureOr<Value> exponent = lowerExpression(children[1]);
    if (failed(exponent))
      return failure();
    FailureOr<Value> realBase =
        convert(*base, builder.getF64Type(), isSignedNode(children[0]),
                getSemanticLocation(children[0]));
    if (failed(realBase))
      return failure();
    FailureOr<Value> realExponent =
        convert(*exponent, builder.getF64Type(), isSignedNode(children[1]),
                getSemanticLocation(children[1]));
    if (failed(realExponent))
      return failure();
    Value result = math::PowFOp::create(builder, location, builder.getF64Type(),
                                        *realBase, *realExponent);
    return convertResult(result);
  }

  if (name == "$atan2") {
    if (children.size() != 2) {
      emitError(location) << "$atan2 requires exactly two arguments";
      return failure();
    }
    FailureOr<Value> y = lowerExpression(children[0]);
    if (failed(y))
      return failure();
    FailureOr<Value> x = lowerExpression(children[1]);
    if (failed(x))
      return failure();
    FailureOr<Value> realY =
        convert(*y, builder.getF64Type(), isSignedNode(children[0]),
                getSemanticLocation(children[0]));
    if (failed(realY))
      return failure();
    FailureOr<Value> realX =
        convert(*x, builder.getF64Type(), isSignedNode(children[1]),
                getSemanticLocation(children[1]));
    if (failed(realX))
      return failure();
    Value result = math::Atan2Op::create(builder, location,
                                         builder.getF64Type(), *realY, *realX);
    return convertResult(result);
  }

  if (name == "$hypot") {
    if (children.size() != 2) {
      emitError(location) << "$hypot requires exactly two arguments";
      return failure();
    }
    FailureOr<Value> x = lowerExpression(children[0]);
    if (failed(x))
      return failure();
    FailureOr<Value> y = lowerExpression(children[1]);
    if (failed(y))
      return failure();
    FailureOr<Value> realX =
        convert(*x, builder.getF64Type(), isSignedNode(children[0]),
                getSemanticLocation(children[0]));
    if (failed(realX))
      return failure();
    FailureOr<Value> realY =
        convert(*y, builder.getF64Type(), isSignedNode(children[1]),
                getSemanticLocation(children[1]));
    if (failed(realY))
      return failure();
    Value xSquared = arith::MulFOp::create(builder, location, *realX, *realX);
    Value ySquared = arith::MulFOp::create(builder, location, *realY, *realY);
    Value sum = arith::AddFOp::create(builder, location, xSquared, ySquared);
    Value result =
        math::SqrtOp::create(builder, location, builder.getF64Type(), sum);
    return convertResult(result);
  }

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

  if (name == "$signed" || name == "$unsigned") {
    if (children.size() != 1) {
      emitError(location) << name << " requires exactly one argument";
      return failure();
    }
    FailureOr<Value> value = lowerExpression(children.front());
    if (failed(value))
      return failure();
    // Signedness is source-semantic metadata on the call expression. The
    // physical width and four-state domain are deliberately unchanged.
    return convertResult(*value);
  }

  auto lowerBitstream = [&](Operation *child) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(child);
    if (failed(value))
      return failure();
    if (sim::getPackedScalarType((*value).getType()))
      return toLogic(*value, getSemanticLocation(child));
    if (sim::getProvenanceSpan((*value).getType()))
      return *value;
    emitError(getSemanticLocation(child))
        << "operand is not a fixed bitstream value: " << (*value).getType();
    return failure();
  };
  auto lowerStateControl = [&](Operation *child) -> FailureOr<Value> {
    FailureOr<Value> control = lowerExpression(child);
    if (failed(control))
      return failure();
    FailureOr<Value> logic = toLogic(*control, getSemanticLocation(child));
    if (failed(logic))
      return failure();
    if (cast<sim::LogicType>((*logic).getType()).getWidth() == 1)
      return *logic;
    return sim::SimLogicExtractOp::create(
               builder, getSemanticLocation(child),
               sim::LogicType::get(function.getContext(), 1), *logic,
               builder.getI64IntegerAttr(0))
        .getResult();
  };
  auto stateConstant = [&](bool value, bool unknown) -> Value {
    auto logic = sim::LogicType::get(function.getContext(), 1);
    auto plane = builder.getI1Type();
    return sim::SimLogicConstantOp::create(
               builder, location, logic,
               builder.getIntegerAttr(plane, value ? 1 : 0),
               builder.getIntegerAttr(plane, unknown ? 1 : 0))
        .getResult();
  };

  if (name == "$clog2") {
    if (children.size() != 1) {
      emitError(location) << "$clog2 requires exactly one argument";
      return failure();
    }
    FailureOr<Value> input = lowerBitstream(children.front());
    if (failed(input))
      return failure();
    if (!isa<sim::LogicType>((*input).getType())) {
      emitError(getSemanticLocation(children.front()))
          << "$clog2 requires an integral operand";
      return failure();
    }
    Value result = sim::SimLogicClog2Op::create(builder, location, i32, *input);
    return convertResult(result);
  }

  if (name == "$countbits" || name == "$countones" || name == "$onehot" ||
      name == "$onehot0" || name == "$isunknown") {
    if ((name == "$countbits" && children.size() < 2) ||
        (name != "$countbits" && children.size() != 1)) {
      emitError(location)
          << name
          << (name == "$countbits"
                  ? " requires a bitstream and at least one control argument"
                  : " requires exactly one argument");
      return failure();
    }
    FailureOr<Value> input = lowerBitstream(children.front());
    if (failed(input))
      return failure();
    SmallVector<Value> controls;
    if (name == "$countbits") {
      for (Operation *child : ArrayRef(children).drop_front()) {
        FailureOr<Value> control = lowerStateControl(child);
        if (failed(control))
          return failure();
        controls.push_back(*control);
      }
    } else if (name == "$isunknown") {
      controls.push_back(stateConstant(false, true)); // X
      controls.push_back(stateConstant(true, true));  // Z
    } else {
      controls.push_back(stateConstant(true, false));
    }
    Value count = sim::SimLogicCountBitsOp::create(builder, location, i32,
                                                   *input, controls);
    if (name == "$countbits" || name == "$countones")
      return convertResult(count);

    arith::CmpIPredicate predicate = name == "$onehot0"
                                         ? arith::CmpIPredicate::ule
                                         : arith::CmpIPredicate::eq;
    int64_t limit = name == "$isunknown" ? 0 : 1;
    if (name == "$isunknown")
      predicate = arith::CmpIPredicate::ne;
    Value result = arith::CmpIOp::create(builder, location, predicate, count,
                                         constant(i32, limit));
    return convertResult(result);
  }

  if (name == "$time" || name == "$stime" || name == "$realtime") {
    if (!children.empty()) {
      emitError(location) << name << " accepts no arguments";
      return failure();
    }
    auto scaleAttr = function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
    if (!scaleAttr || !scaleAttr.getValue().isStrictlyPositive()) {
      function.emitError("code unit has no valid frozen time scale");
      return failure();
    }
    Value now = sim::SimTimeNowOp::create(builder, location, i64, context);
    if (name == "$realtime") {
      Value real = sim::SimTimeToRealOp::create(
          builder, location, builder.getF64Type(), now, scaleAttr);
      return convertResult(real);
    }
    Value scale = arith::ConstantOp::create(builder, location, i64, scaleAttr);
    Value quotient = arith::DivUIOp::create(builder, location, now, scale);
    Value remainder = arith::RemUIOp::create(builder, location, now, scale);
    uint64_t threshold = scaleAttr.getValue().getZExtValue() / 2 +
                         scaleAttr.getValue().getZExtValue() % 2;
    Value halfway = constant(i64, threshold);
    Value increment = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::uge, remainder, halfway);
    Value extended = arith::ExtUIOp::create(builder, location, i64, increment);
    Value rounded =
        arith::AddIOp::create(builder, location, quotient, extended);
    if (name == "$stime")
      rounded = arith::TruncIOp::create(builder, location, i32, rounded);
    return convertResult(rounded);
  }

  if (name == "triggered") {
    if (children.size() != 1) {
      emitError(location) << "event .triggered requires one event operand";
      return failure();
    }
    FailureOr<Value> event = lowerExpression(children.front());
    if (failed(event))
      return failure();
    if (!isa<sim::EventType>((*event).getType())) {
      emitError(location) << ".triggered operand is not an event handle";
      return failure();
    }
    recordSensitivity(*event);
    Value triggered = sim::SimEventTriggeredOp::create(
        builder, location, builder.getI1Type(), *event);
    return convertResult(triggered);
  }

  if (name == "$finish" || name == "$stop") {
    if (children.size() > 1) {
      emitError(location) << name << " accepts at most one verbosity argument";
      return failure();
    }
    Value verbosity = constant(i32, 1);
    if (!children.empty()) {
      FailureOr<Value> lowered = lowerInteger(children.front(), i32);
      if (failed(lowered))
        return failure();
      verbosity = *lowered;
    }
    if (name == "$finish")
      sim::SimFinishOp::create(builder, location, context, verbosity);
    else
      sim::SimStopOp::create(builder, location, context, verbosity);
    if (failed(emitFunctionReturn(location, std::nullopt, false)))
      return failure();
    setCurrent(addBlock());
    return dummyTaskResult();
  }

  bool displayCall =
      llvm::StringSwitch<bool>(name)
          .Cases({"$monitoron", "$monitoroff", "$printtimescale", "$strobe",
                  "$strobeb",   "$strobeo",    "$strobeh",        "$fstrobe",
                  "$fstrobeb",  "$fstrobeo",   "$fstrobeh",       "$monitor",
                  "$monitorb",  "$monitoro",   "$monitorh",       "$fmonitor",
                  "$fmonitorb", "$fmonitoro",  "$fmonitorh",      "$display",
                  "$displayb",  "$displayo",   "$displayh",       "$write",
                  "$writeb",    "$writeo",     "$writeh",         "$fdisplay",
                  "$fdisplayb", "$fdisplayo",  "$fdisplayh",      "$fwrite",
                  "$fwriteb",   "$fwriteo",    "$fwriteh",        "$info",
                  "$warning",   "$error",      "$fatal"},
                 true)
          .Default(false);
  if (displayCall)
    return lowerDisplaySystemCall(op);

  bool fileCall =
      llvm::StringSwitch<bool>(name)
          .Cases({"$fopen", "$fclose", "$fflush", "$fgetc", "$ungetc", "$fgets",
                  "$fread", "$feof", "$fseek", "$ftell", "$rewind"},
                 true)
          .Default(false);
  if (fileCall)
    return lowerFileSystemCall(op);

  unsupported(op) << " (unsupported system call " << name << ")";
  return failure();
}

} // namespace obelisk::simlowering
