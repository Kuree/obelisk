//===- LowerUnitSystemCalls.cpp - Lower system-call semantics ----------===//

#include "LowerUnit.h"

#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringSwitch.h"

#include <optional>

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
  auto convertResult = [&](Value value) -> FailureOr<Value> {
    FailureOr<Type> type = getNormalizedSemanticType(op);
    if (failed(type))
      return failure();
    return convert(value, *type, true, location);
  };
  auto dummyTaskResult = [&]() -> Value {
    return constant(builder.getI1Type(), 0);
  };

  if (name == "$asserton" || name == "$assertoff" || name == "$assertkill" ||
      name == "$assertcontrol") {
    auto action =
        op->getAttrOfType<IntegerAttr>("obelisk_sim.assertion_control_action");
    auto targets = op->getAttrOfType<DenseI64ArrayAttr>(
        "obelisk_sim.assertion_control_ids");
    if (!action || !targets) {
      emitError(location) << name
                          << " has no prepared assertion-control selection";
      return failure();
    }
    for (int64_t target : targets.asArrayRef())
      sim::SimAssertionControlOp::create(
          builder, location, context,
          builder.getI32IntegerAttr(static_cast<int32_t>(action.getInt())),
          builder.getI64IntegerAttr(target));
    return dummyTaskResult();
  }

  bool realConversion =
      llvm::StringSwitch<bool>(name)
          .Cases({"$itor", "$rtoi", "$bitstoreal", "$realtobits",
                  "$bitstoshortreal", "$shortrealtobits"},
                 true)
          .Default(false);
  if (realConversion)
    return lowerRealConversionSystemCall(op);

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

  // IEEE 1800 20.15. Every $dist_* function leads with an inout seed and is
  // followed by one or two shape parameters. The seed is used to reseed the
  // active stream and then receives the draw, so repeated calls threading the
  // same variable walk a sequence rather than repeating one value.
  std::optional<uint32_t> distribution =
      llvm::StringSwitch<std::optional<uint32_t>>(name)
          .Case("$dist_uniform", OBELISK_RT_DISTRIBUTION_UNIFORM)
          .Case("$dist_normal", OBELISK_RT_DISTRIBUTION_NORMAL)
          .Case("$dist_exponential", OBELISK_RT_DISTRIBUTION_EXPONENTIAL)
          .Case("$dist_poisson", OBELISK_RT_DISTRIBUTION_POISSON)
          .Case("$dist_chi_square", OBELISK_RT_DISTRIBUTION_CHI_SQUARE)
          .Case("$dist_t", OBELISK_RT_DISTRIBUTION_T)
          .Case("$dist_erlang", OBELISK_RT_DISTRIBUTION_ERLANG)
          .Default(std::nullopt);
  if (distribution) {
    bool twoParameters =
        *distribution == OBELISK_RT_DISTRIBUTION_UNIFORM ||
        *distribution == OBELISK_RT_DISTRIBUTION_NORMAL ||
        *distribution == OBELISK_RT_DISTRIBUTION_ERLANG;
    size_t expected = twoParameters ? 3 : 2;
    if (children.size() != expected) {
      emitError(location)
          << name << " requires exactly " << expected << " arguments";
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
          << name << " seed must be a writable integral variable";
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

    FailureOr<Value> first = lowerInteger(children[1], i32);
    if (failed(first))
      return failure();
    Value second = constant(i32, 0);
    if (twoParameters) {
      FailureOr<Value> lowered = lowerInteger(children[2], i32);
      if (failed(lowered))
        return failure();
      second = *lowered;
    }
    Value value = sim::SimRandomDistributionOp::create(
        builder, location, i32, context, *distribution, *first, second);

    Type destinationType = getReferenceElementType(*seedDestination);
    FailureOr<Value> updated = convert(value, destinationType, true, location);
    if (failed(updated) ||
        failed(storeReference(*seedDestination, *updated, location)))
      return failure();
    return convertResult(value);
  }

  auto sampledValue = [&](Operation *expression) -> FailureOr<Value> {
    if (!isAddressableExpression(expression)) {
      emitError(getSemanticLocation(expression))
          << "sampled-value expressions currently require statically "
             "addressable packed storage";
      return failure();
    }
    FailureOr<Value> source = lowerExpression(expression, true);
    if (failed(source))
      return failure();
    Type resultType;
    if (auto ref = dyn_cast<sim::RefType>((*source).getType()))
      resultType = ref.getElementType();
    else if (auto net = dyn_cast<sim::NetType>((*source).getType()))
      resultType = net.getElementType();
    if (!resultType || !sim::getPackedWidth(resultType)) {
      emitError(getSemanticLocation(expression))
          << "sampled-value expressions currently require packed storage";
      return failure();
    }
    return sim::SimSampledReadOp::create(builder, location, resultType,
                                         context, *source)
        .getResult();
  };
  auto sampledSiteID = [&]() {
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    return stableCodeUnitID((function.getSymName() + ".$sampled." +
                             Twine(node) + "." + Twine(name))
                                .str());
  };
  auto sampledHistory = [&](Value current, Value gate,
                            uint64_t depth) -> Value {
    return sim::SimSampledHistoryOp::create(
               builder, location, current.getType(), context, current, gate,
               builder.getI64IntegerAttr(sampledSiteID()),
               builder.getI64IntegerAttr(depth))
        .getResult();
  };

  if (name == "$sampled") {
    if (children.size() != 1) {
      emitError(location) << "$sampled requires exactly one argument";
      return failure();
    }
    FailureOr<Value> sampled = sampledValue(children.front());
    return failed(sampled) ? FailureOr<Value>(failure())
                           : convertResult(*sampled);
  }

  bool historyFunction = name == "$past" || name == "$rose" ||
                         name == "$fell" || name == "$stable" ||
                         name == "$changed";
  if (historyFunction) {
    size_t maximum = name == "$past" ? 4 : 2;
    if (children.empty() || children.size() > maximum) {
      emitError(location) << name << " requires "
                          << (name == "$past" ? "one to four" : "one or two")
                          << " arguments";
      return failure();
    }
    if ((name == "$past" && children.size() >= 4) ||
        (name != "$past" && children.size() >= 2)) {
      emitError(getSemanticLocation(children.back()))
          << name << " alternate clock arguments require concurrent property "
                     "clock binding, which is not executable yet";
      return failure();
    }

    uint64_t depth = 1;
    if (name == "$past" && children.size() >= 2) {
      std::optional<StringRef> spelling = getConstantSpelling(children[1]);
      if (!spelling) {
        emitError(getSemanticLocation(children[1]))
            << "$past history depth must be a constant positive integer";
        return failure();
      }
      FailureOr<ParsedConstant> parsed =
          parseSVInteger(*spelling, 64, getSemanticLocation(children[1]));
      if (failed(parsed) || !parsed->unknown.isZero() ||
          parsed->value.isZero() || parsed->value.isNegative()) {
        emitError(getSemanticLocation(children[1]))
            << "$past history depth must be a constant positive integer";
        return failure();
      }
      depth = parsed->value.getZExtValue();
    }

    FailureOr<Value> current = sampledValue(children.front());
    if (failed(current))
      return failure();
    Value gate = constant(builder.getI1Type(), 1);
    if (name == "$past" && children.size() >= 3) {
      FailureOr<Value> sampledGate = sampledValue(children[2]);
      if (failed(sampledGate))
        return failure();
      FailureOr<Value> truth =
          truthValue(*sampledGate, getSemanticLocation(children[2]));
      if (failed(truth))
        return failure();
      gate = *truth;
    }
    Value previous = sampledHistory(*current, gate, depth);
    if (name == "$past")
      return convertResult(previous);

    FailureOr<Value> equal =
        conditionalEqual(*current, previous, (*current).getType(), location,
                         /*caseEquality=*/true);
    if (failed(equal))
      return failure();
    if (name == "$stable")
      return convertResult(*equal);
    if (name == "$changed") {
      Value one = constant(builder.getI1Type(), 1);
      return convertResult(
          arith::XOrIOp::create(builder, location, *equal, one));
    }

    FailureOr<Value> currentScalar = toPackedScalar(*current, location);
    FailureOr<Value> previousScalar = toPackedScalar(previous, location);
    if (failed(currentScalar) || failed(previousScalar))
      return failure();
    Value currentBit, previousBit;
    if (auto logic = dyn_cast<sim::LogicType>((*currentScalar).getType())) {
      Type bitType = sim::LogicType::get(function.getContext(), 1);
      currentBit = sim::SimLogicExtractOp::create(builder, location, bitType,
                                                  *currentScalar, 0);
      previousBit = sim::SimLogicExtractOp::create(builder, location, bitType,
                                                   *previousScalar, 0);
      bool target = name == "$rose";
      Value targetBit = sim::SimLogicConstantOp::create(
          builder, location, bitType,
          builder.getIntegerAttr(builder.getI1Type(), target ? 1 : 0),
          builder.getIntegerAttr(builder.getI1Type(), 0));
      Value currentIsTarget = sim::SimLogicCompareOp::create(
          builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
          currentBit, targetBit);
      Value previousIsTarget = sim::SimLogicCompareOp::create(
          builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
          previousBit, targetBit);
      Value notPrevious = arith::XOrIOp::create(
          builder, location, previousIsTarget,
          constant(builder.getI1Type(), 1));
      return convertResult(arith::AndIOp::create(
          builder, location, currentIsTarget, notPrevious));
    }
    auto integer = cast<IntegerType>((*currentScalar).getType());
    auto bit = [&](Value value) -> Value {
      if (integer.getWidth() == 1)
        return value;
      return arith::TruncIOp::create(builder, location, builder.getI1Type(),
                                    value);
    };
    currentBit = bit(*currentScalar);
    previousBit = bit(*previousScalar);
    Value transition = name == "$rose"
                           ? arith::AndIOp::create(
                                 builder, location, currentBit,
                                 arith::XOrIOp::create(
                                     builder, location, previousBit,
                                     constant(builder.getI1Type(), 1)))
                           : arith::AndIOp::create(
                                 builder, location, previousBit,
                                 arith::XOrIOp::create(
                                     builder, location, currentBit,
                                     constant(builder.getI1Type(), 1)));
    return convertResult(transition);
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

  bool realMath = llvm::StringSwitch<bool>(name)
                      .Cases({"$ceil", "$floor", "$sqrt", "$exp", "$ln",
                              "$log10", "$pow", "$atan2", "$hypot"},
                             true)
                      .Default(false);
  if (realMath)
    return lowerRealMathSystemCall(op);

  bool arrayQuery =
      llvm::StringSwitch<bool>(name)
          .Cases({"$dimensions", "$unpacked_dimensions", "$left", "$right",
                  "$low", "$high", "$increment", "$size"},
                 true)
          .Default(false);
  if (arrayQuery)
    return lowerArrayQuerySystemCall(op);

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
                  "$fread", "$feof", "$ferror", "$fseek", "$ftell", "$rewind",
                  "$timeformat"},
                 true)
          .Default(false);
  if (fileCall)
    return lowerFileSystemCall(op);

  if (name == "$test$plusargs" || name == "$value$plusargs")
    return lowerPlusargSystemCall(op);

  if (name == "$sscanf" || name == "$fscanf")
    return lowerScanSystemCall(op);

  unsupported(op) << " (unsupported system call " << name << ")";
  return failure();
}

} // namespace obelisk::simlowering
