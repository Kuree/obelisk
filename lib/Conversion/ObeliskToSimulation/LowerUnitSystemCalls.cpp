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

FailureOr<Value> UnitLowering::lowerAlternateClockSample(
    Operation *expression, Operation *gateExpression,
    semantic::SVSignalEventControlOp clock, uint64_t depth, uint64_t age,
    Location location) {
  auto sourceNode = dyn_cast<semantic::SVNamedValueExpressionOp>(expression);
  SmallVector<Operation *> clockChildren = getChildren(clock);
  size_t expectedClockChildren = clock.getHasIff() ? 2 : 1;
  auto clockNode =
      clockChildren.size() == expectedClockChildren
          ? dyn_cast<semantic::SVNamedValueExpressionOp>(clockChildren.front())
          : semantic::SVNamedValueExpressionOp{};
  Operation *clockCondition = clock.getHasIff() && clockChildren.size() == 2
                                  ? clockChildren[1]
                                  : nullptr;
  auto gateNode =
      gateExpression
          ? dyn_cast<semantic::SVNamedValueExpressionOp>(gateExpression)
          : semantic::SVNamedValueExpressionOp{};
  auto conditionNode =
      clockCondition
          ? dyn_cast<semantic::SVNamedValueExpressionOp>(clockCondition)
          : gateNode;
  if (!sourceNode || !clockNode ||
      ((gateExpression || clockCondition) && !conditionNode)) {
    emitError(location)
        << "alternate-clock sampled values currently require direct named "
           "packed source, condition, and clock signals";
    return failure();
  }
  if (gateExpression && clockCondition) {
    emitError(location)
        << "alternate-clock $past does not yet support both a gating "
           "expression and an iff-qualified explicit clock";
    return failure();
  }

  FailureOr<Value> source = lowerExpression(expression, true);
  FailureOr<Value> watched = lowerExpression(clockChildren.front(), true);
  Operation *conditionExpression =
      clockCondition ? clockCondition : gateExpression;
  FailureOr<Value> condition = failure();
  if (conditionExpression)
    condition = lowerExpression(conditionExpression, true);
  if (failed(source) || failed(watched) ||
      (conditionExpression && failed(condition)))
    return failure();

  auto elementType = [&](Value reference) -> Type {
    if (auto net = dyn_cast<sim::NetType>(reference.getType()))
      return net.getElementType();
    return getReferenceElementType(reference);
  };
  Type sourceType = elementType(*source);
  Type conditionType = conditionExpression ? elementType(*condition) : Type{};
  if (!sourceType || !sim::getPackedWidth(sourceType) ||
      (conditionExpression &&
       (!conditionType || !isa<sim::LogicType, IntegerType>(conditionType)))) {
    emitError(location)
        << "alternate-clock sampled values require packed source storage and "
           "a packed event condition";
    return failure();
  }

  auto captureAttrs = [&](Value value) -> FailureOr<DictionaryAttr> {
    auto argument = dyn_cast<BlockArgument>(value);
    if (!argument || argument.getOwner() != &function.getBody().front())
      return failure();
    DictionaryAttr attrs = function.getArgAttrDict(argument.getArgNumber());
    auto kind = attrs ? dyn_cast_or_null<sim::CaptureKindAttr>(
                            attrs.get(captureKindAttrName))
                      : sim::CaptureKindAttr{};
    IntegerAttr descriptor =
        attrs ? attrs.getAs<IntegerAttr>(descriptorIdAttrName) : IntegerAttr{};
    if (!kind || !descriptor ||
        (kind.getValue() != sim::CaptureKind::Storage &&
         kind.getValue() != sim::CaptureKind::Net))
      return failure();
    return attrs;
  };
  FailureOr<DictionaryAttr> sourceAttrs = captureAttrs(*source);
  FailureOr<DictionaryAttr> clockAttrs = captureAttrs(*watched);
  FailureOr<DictionaryAttr> conditionAttrs = failure();
  if (conditionExpression)
    conditionAttrs = captureAttrs(*condition);
  if (failed(sourceAttrs) || failed(clockAttrs) ||
      (conditionExpression && failed(conditionAttrs))) {
    emitError(location)
        << "alternate-clock sampled values require statically descriptor-"
           "bound source, condition, and clock signals";
    return failure();
  }

  auto captureKey = [&](DictionaryAttr attrs) {
    auto kind = cast<sim::CaptureKindAttr>(attrs.get(captureKindAttrName));
    auto descriptor = attrs.getAs<IntegerAttr>(descriptorIdAttrName);
    return (Twine(static_cast<uint32_t>(kind.getValue())) + ":" +
            Twine(descriptor.getValue().getZExtValue()))
        .str();
  };
  // Descriptor IDs identify the actual elaborated objects. Source spelling is
  // insufficient here: two instances can both name a local signal `data`,
  // while separate assertion code units referring to the same object should
  // intentionally share one sampler.
  std::string key = (Twine(captureKey(*sourceAttrs)) + "|" +
                     Twine(static_cast<uint32_t>(clock.getEdgeKind())) + "|" +
                     captureKey(*clockAttrs) + "|" +
                     (conditionExpression ? Twine(captureKey(*conditionAttrs))
                                          : Twine("true")) +
                     "|" + Twine(depth))
                        .str();
  auto existing = alternateClockSamplePlans.find(key);
  uint64_t siteID = 0;
  if (existing != alternateClockSamplePlans.end()) {
    if (existing->second.type != sourceType || existing->second.depth != depth)
      return emitError(location)
                 << "inconsistent alternate-clock sample plan for " << key,
             failure();
    siteID = existing->second.id;
  } else {
    siteID = stableCodeUnitID((Twine("$clocked_sample|") + key).str());
    alternateClockSamplePlans[key] = {siteID, depth, sourceType};

    MLIRContext *context = function.getContext();
    Value processContext = function.getBody().front().getArgument(0);
    SmallVector<Type> inputs{processContext.getType(), (*source).getType(),
                             (*watched).getType()};
    SmallVector<DictionaryAttr> argumentAttrs{
        captureMetadata(builder, sim::CaptureKind::Context), *sourceAttrs,
        *clockAttrs};
    if (conditionExpression) {
      inputs.push_back((*condition).getType());
      argumentAttrs.push_back(*conditionAttrs);
    }
    std::string symbol =
        (function.getSymName() + ".$clocked_sample." + Twine(siteID)).str();
    auto parentHierarchy =
        function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName);
    StringRef parentName =
        parentHierarchy ? parentHierarchy.getValue() : function.getSymName();
    std::string hierarchy =
        (Twine(parentName) + ".$clocked_sample." + Twine(siteID)).str();
    OpBuilder outlineBuilder(function);
    outlineBuilder.setInsertionPoint(function);
    SmallVector<NamedAttribute> attributes{
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            "home_region",
            sim::EventRegionAttr::get(context, sim::EventRegion::Active)),
        outlineBuilder.getNamedAttr("domain",
                                    sim::ExecutionDomainAttr::get(
                                        context, sim::ExecutionDomain::Design)),
        outlineBuilder.getNamedAttr(
            "obelisk_sim.clocked_sample_plan",
            outlineBuilder.getDictionaryAttr({
                outlineBuilder.getNamedAttr("key",
                                            outlineBuilder.getStringAttr(key)),
                outlineBuilder.getNamedAttr(
                    "id", outlineBuilder.getI64IntegerAttr(siteID)),
                outlineBuilder.getNamedAttr(
                    "hierarchy", outlineBuilder.getStringAttr(hierarchy)),
            })),
        outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                    outlineBuilder.getStringAttr(hierarchy)),
    };
    sim::SimFuncOp sampler = sim::SimFuncOp::create(
        outlineBuilder, location, symbol,
        FunctionType::get(context, inputs, TypeRange{}), sim::EntryKind::Always,
        attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(sampler, SymbolTable::Visibility::Private);
    Block &entry = sampler.getBody().front();
    Block *wait = new Block();
    Block *sample = new Block();
    sampler.getBody().push_back(wait);
    sampler.getBody().push_back(sample);
    OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
    cf::BranchOp::create(entryBuilder, location, wait);
    OpBuilder waitBuilder = OpBuilder::atBlockEnd(wait);
    Operation *suspend = nullptr;
    if (conditionExpression)
      suspend = sim::SimSuspendEdgeIffOp::create(
                    waitBuilder, location,
                    static_cast<sim::EdgeKind>(clock.getEdgeKind()),
                    entry.getArgument(2), entry.getArgument(3), ValueRange{},
                    sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, sample)
                    .getOperation();
    else
      suspend = sim::SimSuspendEdgeOp::create(
                    waitBuilder, location,
                    static_cast<sim::EdgeKind>(clock.getEdgeKind()),
                    entry.getArgument(2), ValueRange{},
                    sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, sample)
                    .getOperation();
    // IEEE 1800-2017 16.9.3 selects samples from strictly prior time steps.
    // Updating in Postponed leaves an occurrence in the caller's current slot
    // invisible during concurrent assertion evaluation in Observed, while the
    // sampled read below still observes this slot's Preponed snapshot.
    suspend->setAttr(
        "resume_region",
        sim::EventRegionAttr::get(context, sim::EventRegion::Postponed));
    OpBuilder sampleBuilder = OpBuilder::atBlockEnd(sample);
    Value currentSample = sim::SimSampledReadOp::create(
        sampleBuilder, location, sourceType, entry.getArgument(0),
        entry.getArgument(1));
    Value gateValue = arith::ConstantOp::create(
        sampleBuilder, location, sampleBuilder.getI1Type(),
        sampleBuilder.getBoolAttr(true));
    sim::SimClockedSampleUpdateOp::create(
        sampleBuilder, location, entry.getArgument(0), currentSample, gateValue,
        sampleBuilder.getI64IntegerAttr(siteID),
        sampleBuilder.getI64IntegerAttr(depth));
    cf::BranchOp::create(sampleBuilder, location, wait);
    sampler->setAttr(sim::metadata::lowered, builder.getUnitAttr());
  }

  Value processContext = function.getBody().front().getArgument(0);
  return sim::SimClockedSampleReadOp::create(
             builder, location, sourceType, processContext,
             builder.getI64IntegerAttr(siteID),
             builder.getI64IntegerAttr(depth), builder.getI64IntegerAttr(age))
      .getResult();
}

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
      name == "$assertpasson" || name == "$assertpassoff" ||
      name == "$assertfailon" || name == "$assertfailoff" ||
      name == "$assertnonvacuouson" || name == "$assertvacuousoff" ||
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

  // IEEE 1800 20.15 and normative Annex N. Every $dist_* function leads with
  // an inout seed and is followed by one or two shape parameters. Annex N
  // defines a separate seed-threaded generator for these functions; it does
  // not draw from or reseed the active process stream.
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
    bool twoParameters = *distribution == OBELISK_RT_DISTRIBUTION_UNIFORM ||
                         *distribution == OBELISK_RT_DISTRIBUTION_NORMAL ||
                         *distribution == OBELISK_RT_DISTRIBUTION_ERLANG;
    size_t expected = twoParameters ? 3 : 2;
    if (children.size() != expected) {
      emitError(location) << name << " requires exactly " << expected
                          << " arguments";
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
    auto draw = sim::SimRandomDistributionOp::create(
        builder, location, TypeRange{i32, i32}, context, *distribution, *seed32,
        *first, second);

    Type destinationType = getReferenceElementType(*seedDestination);
    FailureOr<Value> updated =
        convert(draw.getNextSeed(), destinationType, true, location);
    if (failed(updated) ||
        failed(storeReference(*seedDestination, *updated, location)))
      return failure();
    return convertResult(draw.getResult());
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
    Operation *clockArgument = nullptr;
    semantic::SVSignalEventControlOp explicitEvent;
    bool alternateClock = false;
    if ((name == "$past" && children.size() >= 4) ||
        (name != "$past" && children.size() >= 2))
      clockArgument = children.back();
    if (clockArgument) {
      auto clocking =
          dyn_cast<semantic::SVClockingEventExpressionOp>(clockArgument);
      SmallVector<Operation *> clockingChildren =
          clocking ? getChildren(clocking) : SmallVector<Operation *>{};
      explicitEvent = clockingChildren.size() == 1
                          ? dyn_cast<semantic::SVSignalEventControlOp>(
                                clockingChildren.front())
                          : semantic::SVSignalEventControlOp{};
      SmallVector<Operation *> explicitChildren =
          explicitEvent ? getChildren(explicitEvent)
                        : SmallVector<Operation *>{};
      size_t expectedExplicitChildren =
          explicitEvent && explicitEvent.getHasIff() ? 2 : 1;
      auto explicitSignal = explicitChildren.size() == expectedExplicitChildren
                                ? dyn_cast<semantic::SVNamedValueExpressionOp>(
                                      explicitChildren.front())
                                : semantic::SVNamedValueExpressionOp{};
      auto explicitCondition =
          explicitEvent && explicitEvent.getHasIff() &&
                  explicitChildren.size() == 2
              ? dyn_cast<semantic::SVNamedValueExpressionOp>(
                    explicitChildren[1])
              : semantic::SVNamedValueExpressionOp{};
      if (!explicitSignal ||
          (explicitEvent.getHasIff() && !explicitCondition)) {
        emitError(getSemanticLocation(clockArgument))
            << name
            << " explicit clocks currently require one direct named-"
               "signal edge and an optional direct named iff "
               "condition";
        return failure();
      }

      auto activeEvent = dyn_cast_or_null<semantic::SVSignalEventControlOp>(
          activeSampledClock);
      SmallVector<Operation *> activeChildren =
          activeEvent ? getChildren(activeEvent) : SmallVector<Operation *>{};
      size_t expectedActiveChildren =
          activeEvent && activeEvent.getHasIff() ? 2 : 1;
      auto activeSignal = activeChildren.size() == expectedActiveChildren
                              ? dyn_cast<semantic::SVNamedValueExpressionOp>(
                                    activeChildren.front())
                              : semantic::SVNamedValueExpressionOp{};
      auto activeCondition =
          activeEvent && activeEvent.getHasIff() && activeChildren.size() == 2
              ? dyn_cast<semantic::SVNamedValueExpressionOp>(activeChildren[1])
              : semantic::SVNamedValueExpressionOp{};
      if (!activeSignal || (activeEvent.getHasIff() && !activeCondition)) {
        emitError(getSemanticLocation(clockArgument))
            << name
            << " explicit clock requires a matching statically "
               "enclosing direct event control";
        return failure();
      }
      auto explicitPath = explicitSignal.getReferencedPathAttr();
      auto activePath = activeSignal.getReferencedPathAttr();
      bool sameCondition = explicitEvent.getHasIff() == activeEvent.getHasIff();
      if (sameCondition && explicitEvent.getHasIff()) {
        auto explicitConditionPath = explicitCondition.getReferencedPathAttr();
        auto activeConditionPath = activeCondition.getReferencedPathAttr();
        sameCondition = explicitConditionPath && activeConditionPath &&
                        explicitConditionPath == activeConditionPath;
      }
      alternateClock =
          !explicitPath || !activePath || explicitPath != activePath ||
          explicitEvent.getEdgeKind() != activeEvent.getEdgeKind() ||
          !sameCondition;
      if (alternateClock && !sampleAssertionValues) {
        emitError(getSemanticLocation(clockArgument))
            << name
            << " genuinely alternate clocks are currently executable "
               "only in a statically clocked concurrent predicate";
        return failure();
      }
    }

    uint64_t depth = 1;
    if (name == "$past" && children.size() >= 2 &&
        !isa<semantic::SVEmptyArgumentExpressionOp>(children[1])) {
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

    FailureOr<Value> current = failure();
    Value previous;
    if (alternateClock) {
      Operation *gateExpression =
          name == "$past" && children.size() >= 3 &&
                  !isa<semantic::SVEmptyArgumentExpressionOp>(children[2])
              ? children[2]
              : nullptr;
      if (name == "$past") {
        FailureOr<Value> past = lowerAlternateClockSample(
            children.front(), gateExpression, explicitEvent, depth, depth - 1,
            location);
        return failed(past) ? FailureOr<Value>(failure())
                            : convertResult(*past);
      }
      current = sampledValue(children.front());
      FailureOr<Value> prior = lowerAlternateClockSample(
          children.front(), nullptr, explicitEvent, 1, 0, location);
      if (failed(current) || failed(prior))
        return failure();
      previous = *prior;
    } else {
      current = sampledValue(children.front());
      if (failed(current))
        return failure();
      Value gate = constant(builder.getI1Type(), 1);
      if (name == "$past" && children.size() >= 3 &&
          !isa<semantic::SVEmptyArgumentExpressionOp>(children[2])) {
        FailureOr<Value> sampledGate = sampledValue(children[2]);
        if (failed(sampledGate))
          return failure();
        FailureOr<Value> truth =
            truthValue(*sampledGate, getSemanticLocation(children[2]));
        if (failed(truth))
          return failure();
        gate = *truth;
      }
      previous = sampledHistory(*current, gate, depth);
      if (name == "$past")
        return convertResult(previous);
    }

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
    Type destinationType = getReferenceElementType(*destinationRef);
    if (!destinationType) {
      emitError(location) << "$cast destination must be a writable reference";
      return failure();
    }
    std::optional<semantic::SVDynamicCastKind> kindAttr =
        op.getDynamicCastKind();
    if (!kindAttr) {
      emitError(location) << "$cast has no valid elaborated classification";
      return failure();
    }
    semantic::SVDynamicCastKind kind = *kindAttr;
    auto targetClass = dyn_cast<sim::ClassHandleType>(destinationType);
    Value source;
    if (isa<semantic::SVNullLiteralOp>(children[1])) {
      if (targetClass)
        source = sim::SimClassNullOp::create(
            builder, getSemanticLocation(children[1]), destinationType);
      else if (kind != semantic::SVDynamicCastKind::AlwaysFail) {
        emitError(location) << "$cast null source requires a class target";
        return failure();
      }
    } else {
      FailureOr<Value> lowered = lowerExpression(children[1]);
      if (failed(lowered))
        return failure();
      source = *lowered;
    }

    Value casted;
    Value succeeded;
    bool conditionalStore = false;
    switch (kind) {
    case semantic::SVDynamicCastKind::AlwaysSuccess: {
      FailureOr<Value> converted =
          source ? convert(source, destinationType, isSignedNode(children[1]),
                           location, isSignedNode(destination))
                 : FailureOr<Value>(failure());
      if (failed(converted))
        return failure();
      casted = *converted;
      succeeded = constant(builder.getI1Type(), 1);
      break;
    }
    case semantic::SVDynamicCastKind::AlwaysFail:
      succeeded = constant(builder.getI1Type(), 0);
      break;
    case semantic::SVDynamicCastKind::ClassRuntime: {
      if (!targetClass || !source ||
          !isa<sim::ClassHandleType>(source.getType())) {
        emitError(location) << "runtime class $cast has incompatible operands";
        return failure();
      }
      casted = sim::SimClassCastOp::create(builder, location, destinationType,
                                           source);
      Value instance = sim::SimClassIsInstanceOp::create(
          builder, location, builder.getI1Type(), source,
          FlatSymbolRefAttr::get(
              function.getContext(),
              targetClass.getClassName().getRootReference()));
      Value sourceID = sim::SimClassIdOp::create(
          builder, location, builder.getI64Type(), source);
      Value isNull = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, sourceID,
          constant(builder.getI64Type(), 0));
      succeeded = arith::OrIOp::create(builder, location, instance, isNull);
      conditionalStore = true;
      break;
    }
    case semantic::SVDynamicCastKind::EnumMembership: {
      if (targetClass || !source ||
          isa<sim::ClassHandleType>(source.getType())) {
        emitError(location) << "enum $cast has incompatible operands";
        return failure();
      }
      FailureOr<Value> converted =
          convert(source, destinationType, isSignedNode(children[1]), location,
                  isSignedNode(destination));
      if (failed(converted))
        return failure();
      casted = *converted;
      Type sourceScalar = sim::getPackedScalarType(source.getType());
      Type destinationScalar = sim::getPackedScalarType(destinationType);
      std::optional<unsigned> sourceWidth =
          sourceScalar ? sim::getPackedWidth(sourceScalar) : std::nullopt;
      std::optional<unsigned> destinationWidth =
          destinationScalar ? sim::getPackedWidth(destinationScalar)
                            : std::nullopt;
      if (!sourceWidth || !destinationWidth) {
        emitError(location)
            << "enum $cast requires packed integral operands";
        return failure();
      }
      unsigned comparisonWidth = std::max(*sourceWidth, *destinationWidth);
      Type comparisonType =
          isa<sim::LogicType>(sourceScalar) ||
                  isa<sim::LogicType>(destinationScalar)
              ? Type(sim::LogicType::get(function.getContext(),
                                         comparisonWidth))
              : Type(IntegerType::get(function.getContext(),
                                      comparisonWidth));
      bool comparisonSigned = isSignedNode(children[1]) &&
                              isSignedNode(destination);
      FailureOr<Value> comparisonSource =
          convert(source, comparisonType, comparisonSigned, location,
                  comparisonSigned);
      if (failed(comparisonSource))
        return failure();
      ArrayAttr enumValues =
          op->getAttrOfType<ArrayAttr>(dynamicCastEnumValuesAttrName);
      if (!enumValues || enumValues.empty()) {
        emitError(location) << "enum $cast has no valid frozen membership";
        return failure();
      }
      succeeded = constant(builder.getI1Type(), 0);
      for (Attribute attribute : enumValues) {
        auto frozen = dyn_cast<sim::FrozenConstantAttr>(attribute);
        FailureOr<Value> member = frozen && frozen.getType() == destinationType
                                      ? sim::materializeFrozenConstant(
                                            builder, location, frozen)
                                      : FailureOr<Value>(failure());
        if (failed(member)) {
          emitError(location)
              << "enum $cast has malformed frozen membership";
          return failure();
        }
        FailureOr<Value> comparisonMember =
            convert(*member, comparisonType, comparisonSigned, location,
                    comparisonSigned);
        if (failed(comparisonMember))
          return failure();
        FailureOr<Value> equal = conditionalEqual(
            *comparisonSource, *comparisonMember, comparisonType, location,
            /*caseEquality=*/true);
        if (failed(equal))
          return failure();
        succeeded =
            arith::OrIOp::create(builder, location, succeeded, *equal);
      }
      conditionalStore = true;
      break;
    }
    }

    bool taskForm = op->hasAttr(dynamicCastTaskAttrName);
    if (conditionalStore) {
      Block *store = addBlock();
      Block *resume = addBlock();
      Block *failedCast = taskForm ? addBlock() : resume;
      cf::CondBranchOp::create(builder, location, succeeded, store, failedCast);
      setCurrent(store);
      if (failed(storeReference(*destinationRef, casted, location)))
        return failure();
      emitBranch(resume);
      if (taskForm) {
        setCurrent(failedCast);
        if (failed(emitRuntimeFatal(location,
                                    "$cast failed when used as a task")))
          return failure();
      }
      setCurrent(resume);
    } else if (kind == semantic::SVDynamicCastKind::AlwaysSuccess) {
      if (failed(storeReference(*destinationRef, casted, location)))
        return failure();
    } else if (taskForm) {
      if (failed(
              emitRuntimeFatal(location, "$cast failed when used as a task")))
        return failure();
      setCurrent(addBlock());
      succeeded = constant(builder.getI1Type(), 0);
    }
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

  if (name == "name") {
    if (children.size() != 1) {
      emitError(location) << "enum name() requires exactly one receiver";
      return failure();
    }
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    ArrayAttr values =
        op->getAttrOfType<ArrayAttr>(enumMethodValuesAttrName);
    ArrayAttr names = op->getAttrOfType<ArrayAttr>(enumMethodNamesAttrName);
    if (!semanticType || !isa<semantic::EnumType>(semanticType.getValue()) ||
        !values || values.empty() || !names || values.size() != names.size()) {
      emitError(location) << "enum name() has no valid frozen inventory";
      return failure();
    }
    FailureOr<Value> receiver = lowerExpression(children.front());
    if (failed(receiver) || !sim::getPackedScalarType((*receiver).getType())) {
      emitError(location) << "enum name() requires a packed enum receiver";
      return failure();
    }

    Type stringType = sim::StringType::get(function.getContext());
    Value result = sim::SimStringLiteralOp::create(
        builder, location, stringType, builder.getStringAttr(""));
    for (auto [valueAttribute, nameAttribute] :
         llvm::zip_equal(values, names)) {
      auto frozen = dyn_cast<sim::FrozenConstantAttr>(valueAttribute);
      auto spelling = dyn_cast<StringAttr>(nameAttribute);
      FailureOr<Value> member =
          frozen && frozen.getType() == (*receiver).getType()
              ? sim::materializeFrozenConstant(builder, location, frozen)
              : FailureOr<Value>(failure());
      if (failed(member) || !spelling) {
        emitError(location) << "enum name() has malformed frozen inventory";
        return failure();
      }
      FailureOr<Value> equal = conditionalEqual(
          *receiver, *member, (*receiver).getType(), location,
          /*caseEquality=*/true);
      if (failed(equal))
        return failure();
      Value candidate = sim::SimStringLiteralOp::create(
          builder, location, stringType, spelling);
      result = arith::SelectOp::create(builder, location, *equal, candidate,
                                       result);
    }
    return convertResult(result);
  }

  bool enumIterationMethod =
      llvm::StringSwitch<bool>(name)
          .Cases({"first", "last", "next", "prev", "num"}, true)
          .Default(false);
  if (enumIterationMethod) {
    bool takesCount = name == "next" || name == "prev";
    if ((takesCount && (children.empty() || children.size() > 2)) ||
        (!takesCount && children.size() != 1)) {
      emitError(location) << "enum " << name << "() has invalid arguments";
      return failure();
    }
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    ArrayAttr values = op->getAttrOfType<ArrayAttr>(enumMethodValuesAttrName);
    if (!semanticType || !isa<semantic::EnumType>(semanticType.getValue()) ||
        !values || values.empty()) {
      emitError(location) << "enum " << name
                          << "() has no valid frozen inventory";
      return failure();
    }
    FailureOr<Value> receiver = lowerExpression(children.front());
    if (failed(receiver) || !sim::getPackedScalarType((*receiver).getType())) {
      emitError(location) << "enum " << name
                          << "() requires a packed enum receiver";
      return failure();
    }
    SmallVector<sim::FrozenConstantAttr> frozenMembers;
    frozenMembers.reserve(values.size());
    for (Attribute attribute : values) {
      auto frozen = dyn_cast<sim::FrozenConstantAttr>(attribute);
      if (!frozen || frozen.getType() != (*receiver).getType()) {
        emitError(location)
            << "enum " << name << "() has malformed frozen inventory";
        return failure();
      }
      frozenMembers.push_back(frozen);
    }
    auto materializeMember = [&](sim::FrozenConstantAttr frozen) {
      return sim::materializeFrozenConstant(builder, location, frozen);
    };
    if (name == "first" || name == "last") {
      FailureOr<Value> result = materializeMember(
          name == "first" ? frozenMembers.front() : frozenMembers.back());
      if (failed(result))
        return failure();
      return convertResult(*result);
    }
    if (name == "num")
      return convertResult(
          constant(i64, static_cast<int64_t>(frozenMembers.size())));

    SmallVector<Value> members;
    members.reserve(frozenMembers.size());
    for (sim::FrozenConstantAttr frozen : frozenMembers) {
      FailureOr<Value> member = materializeMember(frozen);
      if (failed(member))
        return failure();
      members.push_back(*member);
    }

    Value count = constant(i64, static_cast<int64_t>(members.size()));
    Value ordinal = count;
    Value valid = constant(builder.getI1Type(), 0);
    for (auto [index, member] : llvm::enumerate(members)) {
      FailureOr<Value> equal =
          conditionalEqual(*receiver, member, (*receiver).getType(), location,
                           /*caseEquality=*/true);
      if (failed(equal))
        return failure();
      valid = arith::OrIOp::create(builder, location, valid, *equal);
      ordinal = arith::SelectOp::create(
          builder, location, *equal, constant(i64, static_cast<int64_t>(index)),
          ordinal);
    }

    Value amount = constant(i64, 1);
    if (children.size() == 2) {
      FailureOr<Value> amount32 = lowerInteger(children[1], i32);
      if (failed(amount32))
        return failure();
      amount = arith::ExtUIOp::create(builder, location, i64, *amount32);
    }
    amount = arith::RemUIOp::create(builder, location, amount, count);
    Value target;
    if (name == "next") {
      target = arith::RemUIOp::create(
          builder, location,
          arith::AddIOp::create(builder, location, ordinal, amount), count);
    } else {
      target = arith::RemUIOp::create(
          builder, location,
          arith::SubIOp::create(
              builder, location,
              arith::AddIOp::create(builder, location, ordinal, count), amount),
          count);
    }

    Value defaultResult =
        createDefaultValue(builder, location, (*receiver).getType());
    if (!defaultResult) {
      emitError(location) << "enum " << name
                          << "() cannot materialize its default value";
      return failure();
    }
    Value result = defaultResult;
    for (auto [index, member] : llvm::enumerate(members)) {
      Value selected = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, target,
          constant(i64, static_cast<int64_t>(index)));
      result =
          arith::SelectOp::create(builder, location, selected, member, result);
    }
    result = arith::SelectOp::create(builder, location, valid, result,
                                     defaultResult);
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
                  "$warning",   "$error",      "$fatal",         "$swrite",
                  "$swriteb",   "$swriteo",    "$swriteh"},
                 true)
          .Default(false);
  if (displayCall)
    return lowerDisplaySystemCall(op);

  if (name == "$sformat" || name == "$sformatf" || name == "$psprintf")
    return lowerStringFormatSystemCall(op);

  bool fileCall =
      llvm::StringSwitch<bool>(name)
          .Cases({"$fopen", "$fclose", "$fflush", "$fgetc", "$ungetc", "$fgets",
                  "$fread", "$feof", "$ferror", "$fseek", "$ftell", "$rewind",
                  "$timeformat", "$readmemb", "$readmemh"},
                 true)
          .Default(false);
  if (fileCall)
    return lowerFileSystemCall(op);

  bool dumpCall = llvm::StringSwitch<bool>(name)
                      .Cases({"$dumpfile", "$dumpvars", "$dumpoff", "$dumpon",
                              "$dumpall", "$dumpflush", "$dumplimit"},
                             true)
                      .Default(false);
  if (dumpCall)
    return lowerDumpSystemCall(op);

  if (name == "$test$plusargs" || name == "$value$plusargs")
    return lowerPlusargSystemCall(op);

  if (name == "$sscanf" || name == "$fscanf")
    return lowerScanSystemCall(op);

  bool coverageCall =
      llvm::StringSwitch<bool>(name)
          .Cases({"$coverage_control", "$coverage_get_max", "$coverage_get",
                  "$coverage_merge", "$coverage_save", "$set_coverage_db_name",
                  "$load_coverage_db", "$get_coverage"},
                 true)
          .Default(false);
  if (coverageCall) {
    // IEEE 1800-2017 40.3.2 explicitly represents an implementation with no
    // assertion, FSM, statement, or toggle coverage by SV_COV_NOCOV. Obelisk
    // does not instrument those four code-coverage classes, so model that
    // standardized capability result instead of inventing counters. The
    // Clause 19 functional-coverage database routines remain unsupported when
    // a design actually declares covergroups.
    auto oneOf = [&](Value value, ArrayRef<int32_t> choices) -> Value {
      Value result = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(false));
      for (int32_t choice : choices) {
        Value equal =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  value, constant(i32, choice));
        result = arith::OrIOp::create(builder, location, result, equal);
      }
      return result;
    };
    auto status = [&](Value valid, Value success) -> FailureOr<Value> {
      Value error = constant(i32, -1); // SV_COV_ERROR
      return convertResult(
          arith::SelectOp::create(builder, location, valid, success, error));
    };
    auto validCoverageType = [&](Value value) -> Value {
      return oneOf(value, {20, 21, 22, 23});
    };
    auto validScopeDefinition = [&](Value value) -> Value {
      return oneOf(value, {10, 11});
    };
    auto lowerCoverageTarget = [&](Operation *target) -> FailureOr<Value> {
      if (isa<semantic::SVArbitrarySymbolExpressionOp>(target))
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(true))
            .getResult();
      FailureOr<Value> lowered = lowerExpression(target);
      if (failed(lowered) || !isa<sim::StringType>((*lowered).getType())) {
        emitError(getSemanticLocation(target))
            << "coverage scope must be a module instance or definition name";
        return failure();
      }
      Value valid = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(false));
      SmallVector<StringRef> definitionNames;
      definitionNames.reserve(coverageDefinitionNames.size());
      for (const auto &entry : coverageDefinitionNames)
        definitionNames.push_back(entry.getKey());
      llvm::sort(definitionNames);
      for (StringRef definitionName : definitionNames) {
        Value candidate = sim::SimStringLiteralOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            builder.getStringAttr(definitionName));
        Value compared = sim::SimStringCompareOp::create(
            builder, location, i32, *lowered, candidate,
            builder.getBoolAttr(false));
        Value equal =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  compared, constant(i32, 0));
        valid = arith::OrIOp::create(builder, location, valid, equal);
      }
      return valid;
    };

    if (name == "$coverage_control") {
      if (children.size() != 4) {
        emitError(location) << "$coverage_control requires four arguments";
        return failure();
      }
      FailureOr<Value> control = lowerInteger(children[0], i32);
      FailureOr<Value> coverageType = lowerInteger(children[1], i32);
      FailureOr<Value> scope = lowerInteger(children[2], i32);
      FailureOr<Value> target = lowerCoverageTarget(children[3]);
      if (failed(control) || failed(coverageType) || failed(scope) ||
          failed(target))
        return failure();
      Value valid = arith::AndIOp::create(
          builder, location, oneOf(*control, {0, 1, 2, 3}),
          arith::AndIOp::create(
              builder, location, validCoverageType(*coverageType),
              arith::AndIOp::create(builder, location,
                                    validScopeDefinition(*scope), *target)));
      Value stop =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                *control, constant(i32, 1));
      Value reset =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                *control, constant(i32, 2));
      Value stopOrReset = arith::OrIOp::create(builder, location, stop, reset);
      Value result = arith::SelectOp::create(
          builder, location, stopOrReset,
          constant(i32, 1),  // SV_COV_OK: valid stop/reset are no-ops.
          constant(i32, 0)); // SV_COV_NOCOV: check/start find no counters.
      return status(valid, result);
    }

    if (name == "$coverage_get_max" || name == "$coverage_get") {
      if (children.size() != 3) {
        emitError(location) << name << " requires three arguments";
        return failure();
      }
      FailureOr<Value> coverageType = lowerInteger(children[0], i32);
      FailureOr<Value> scope = lowerInteger(children[1], i32);
      FailureOr<Value> target = lowerCoverageTarget(children[2]);
      if (failed(coverageType) || failed(scope) || failed(target))
        return failure();
      Value valid = arith::AndIOp::create(
          builder, location, validCoverageType(*coverageType),
          arith::AndIOp::create(builder, location, validScopeDefinition(*scope),
                                *target));
      return status(valid, constant(i32, 0)); // SV_COV_NOCOV
    }

    if (name == "$coverage_merge" || name == "$coverage_save") {
      if (children.size() != 2) {
        emitError(location) << name << " requires two arguments";
        return failure();
      }
      FailureOr<Value> coverageType = lowerInteger(children[0], i32);
      FailureOr<Value> databaseName = lowerExpression(children[1]);
      if (failed(coverageType) || failed(databaseName) ||
          !isa<sim::StringType>((*databaseName).getType())) {
        emitError(location)
            << name << " requires a coverage type and string name";
        return failure();
      }
      // Nothing can have been saved without code-coverage instrumentation.
      // Merge therefore reports ERROR (database/type not found), while save
      // reports NOCOV and creates no entry, exactly as 40.3.2.4/.5 specify.
      Value result = constant(i32, name == "$coverage_merge" ? -1 : 0);
      return status(validCoverageType(*coverageType), result);
    }

    if (name == "$get_coverage") {
      if (!children.empty()) {
        emitError(location) << "$get_coverage takes no arguments";
        return failure();
      }
      if (!semanticCovergroups.empty()) {
        unsupported(op)
            << " (coverage database aggregation with declared covergroups)";
        return failure();
      }
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      auto floatType = succeeded(resultType) ? dyn_cast<FloatType>(*resultType)
                                             : FloatType{};
      if (!floatType) {
        emitError(location) << "$get_coverage has a non-real result type";
        return failure();
      }
      return arith::ConstantOp::create(builder, location, floatType,
                                       builder.getFloatAttr(floatType, 0.0))
          .getResult();
    }

    if (children.size() != 1) {
      emitError(location) << name << " requires one string argument";
      return failure();
    }
    FailureOr<Value> databaseName = lowerExpression(children.front());
    if (failed(databaseName) ||
        !isa<sim::StringType>((*databaseName).getType())) {
      emitError(location) << name << " requires a string argument";
      return failure();
    }
    if (!semanticCovergroups.empty()) {
      unsupported(op) << " (coverage database I/O with declared covergroups)";
      return failure();
    }
    // With no covergroup types there is no data to name or load. Clause 19.9
    // gives these tasks no status result or required side effect in that case.
    return dummyTaskResult();
  }

  unsupported(op) << " (unsupported system call " << name << ")";
  return failure();
}

} // namespace obelisk::simlowering
