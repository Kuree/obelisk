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
namespace {
bool needsExpressionHistoryDefault(Operation *expression) {
  return !isa<semantic::SVNamedValueExpressionOp>(expression) ||
         getConstantSpelling(expression).has_value() ||
         expression->hasAttr("obelisk_sim.sample_default_constant") ||
         expression->hasAttr("obelisk_sim.sample_default_snapshot") ||
         expression->hasAttr("obelisk_sim.sample_default_dynamic") ||
         expression->hasAttr("obelisk_sim.sample_default_current");
}

uint64_t historyValidityID(uint64_t siteID) {
  return stableCodeUnitID((Twine(siteID) + ".$history_valid").str());
}
} // namespace

FailureOr<Value> UnitLowering::lowerAlternateClockSample(
    Operation *expression, Operation *gateExpression, Operation *clock,
    uint64_t depth, uint64_t age, Location location) {
  auto event = dyn_cast<semantic::SVSignalEventControlOp>(clock);
  auto globalCall = dyn_cast<semantic::SVCallExpressionOp>(clock);
  bool globalClock =
      globalCall && isGlobalSampledFunction(globalCall.getCalleeName());
  if (!event && !globalClock)
    return emitError(location) << "sampled value has no static clock",
           failure();

  SmallVector<Operation *> clockChildren;
  Operation *clockExpression = nullptr;
  Operation *clockCondition = nullptr;
  semantic::EdgeKind edge = semantic::EdgeKind::Change;
  StringAttr globalClockPath;
  if (event) {
    clockChildren = getChildren(event);
    size_t expectedClockChildren = event.getHasIff() ? 2 : 1;
    if (clockChildren.size() != expectedClockChildren)
      return emitError(location) << "sampled value has a malformed clock",
             failure();
    clockExpression = clockChildren.front();
    clockCondition = event.getHasIff() ? clockChildren[1] : nullptr;
    edge = event.getEdgeKind();
  } else {
    globalClockPath =
        clock->getAttrOfType<StringAttr>(clockingEventPathAttrName);
    auto frozenEdge =
        clock->getAttrOfType<semantic::EdgeKindAttr>(clockingEventEdgeAttrName);
    if (!clock->hasAttr(clockingBlockEventAttrName) || !globalClockPath ||
        !frozenEdge || clock->hasAttr(clockingEventHasIffAttrName) ||
        clock->hasAttr(clockingEventListAttrName) ||
        clock->hasAttr(clockingEventMonitorRequiredAttrName))
      return emitError(location)
                 << "global sampled values currently require one direct "
                    "global clock signal without iff",
             failure();
    edge = frozenEdge.getValue();
  }
  auto clockNode =
      clockExpression
          ? dyn_cast<semantic::SVNamedValueExpressionOp>(clockExpression)
          : semantic::SVNamedValueExpressionOp{};
  auto gateNode =
      gateExpression
          ? dyn_cast<semantic::SVNamedValueExpressionOp>(gateExpression)
          : semantic::SVNamedValueExpressionOp{};
  auto clockConditionNode =
      clockCondition
          ? dyn_cast<semantic::SVNamedValueExpressionOp>(clockCondition)
          : semantic::SVNamedValueExpressionOp{};
  if (!isAddressableExpression(expression) || (!globalClock && !clockNode) ||
      (gateExpression && !gateNode) ||
      (clockCondition && !clockConditionNode)) {
    emitError(location)
        << "alternate-clock sampled values currently require direct named "
           "packed source, gate, clock-iff condition, and clock signals";
    return failure();
  }

  FailureOr<Value> source = lowerExpression(expression, true);
  FailureOr<Value> watched =
      globalClock
          ? lowerReferencedValue(clock, globalClockPath.getValue(), true)
          : lowerExpression(clockExpression, true);
  FailureOr<Value> gate = failure();
  FailureOr<Value> clockQualifier = failure();
  if (gateExpression)
    gate = lowerExpression(gateExpression, true);
  if (clockCondition)
    clockQualifier = lowerExpression(clockCondition, true);
  if (failed(source) || failed(watched) || (gateExpression && failed(gate)) ||
      (clockCondition && failed(clockQualifier)))
    return failure();

  auto elementType = [&](Value reference) -> Type {
    if (auto net = dyn_cast<sim::NetType>(reference.getType()))
      return net.getElementType();
    return getReferenceElementType(reference);
  };
  Type sourceType = elementType(*source);
  Type gateType = gateExpression ? elementType(*gate) : Type{};
  Type clockConditionType =
      clockCondition ? elementType(*clockQualifier) : Type{};
  auto isPackedCondition = [](Type type) {
    return type && isa<sim::LogicType, IntegerType>(type);
  };
  if (!sourceType || !sim::getPackedWidth(sourceType) ||
      (gateExpression && !isPackedCondition(gateType)) ||
      (clockCondition && !isPackedCondition(clockConditionType))) {
    emitError(location)
        << "alternate-clock sampled values require packed source storage and "
           "packed gate and clock-iff conditions";
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
  DictionaryAttr gateAttrs;
  DictionaryAttr clockConditionAttrs;
  if (gateExpression) {
    FailureOr<DictionaryAttr> captured = captureAttrs(*gate);
    if (succeeded(captured))
      gateAttrs = *captured;
  }
  if (clockCondition) {
    FailureOr<DictionaryAttr> captured = captureAttrs(*clockQualifier);
    if (succeeded(captured))
      clockConditionAttrs = *captured;
  }
  if (failed(sourceAttrs) || failed(clockAttrs) ||
      (gateExpression && !gateAttrs) ||
      (clockCondition && !clockConditionAttrs)) {
    emitError(location)
        << "alternate-clock sampled values require statically descriptor-"
           "bound source, gate, clock-iff condition, and clock signals";
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
  // intentionally share one sampler. A gate-only plan and an iff-only plan
  // using the same condition also share: both lower to one edge-iff suspend
  // and an unconditional update. Only the simultaneous form has a distinct
  // update gate.
  DictionaryAttr suspendConditionAttrs =
      clockCondition ? clockConditionAttrs
                     : (gateExpression ? gateAttrs : DictionaryAttr{});
  DictionaryAttr updateGateAttrs =
      clockCondition && gateExpression ? gateAttrs : DictionaryAttr{};
  std::string key =
      (Twine(captureKey(*sourceAttrs)) + "|" +
       Twine(static_cast<uint32_t>(edge)) + "|" + captureKey(*clockAttrs) +
       "|condition:" +
       (suspendConditionAttrs ? Twine(captureKey(suspendConditionAttrs))
                              : Twine("true")) +
       "|gate:" +
       (updateGateAttrs ? Twine(captureKey(updateGateAttrs)) : Twine("true")) +
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
    std::optional<unsigned> clockConditionArgument;
    std::optional<unsigned> gateArgument;
    if (clockCondition) {
      clockConditionArgument = inputs.size();
      inputs.push_back((*clockQualifier).getType());
      argumentAttrs.push_back(clockConditionAttrs);
    }
    if (gateExpression) {
      gateArgument = inputs.size();
      inputs.push_back((*gate).getType());
      argumentAttrs.push_back(gateAttrs);
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
    // A gate-only plan can use the edge-iff suspension directly. When both
    // controls are present, the clock's iff decides whether the event occurs,
    // while the independent $past gate controls the history update below.
    std::optional<unsigned> suspendConditionArgument =
        clockConditionArgument ? clockConditionArgument : gateArgument;
    if (suspendConditionArgument)
      suspend = sim::SimSuspendEdgeIffOp::create(
                    waitBuilder, location, static_cast<sim::EdgeKind>(edge),
                    entry.getArgument(2),
                    entry.getArgument(*suspendConditionArgument), ValueRange{},
                    sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, sample)
                    .getOperation();
    else
      suspend = sim::SimSuspendEdgeOp::create(
                    waitBuilder, location, static_cast<sim::EdgeKind>(edge),
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
    if (clockCondition && gateExpression) {
      Value sampledGate = sim::SimSampledReadOp::create(
          sampleBuilder, location, gateType, entry.getArgument(0),
          entry.getArgument(*gateArgument));
      if (isa<sim::LogicType>(gateType)) {
        gateValue = sim::SimLogicIsTrueOp::create(
            sampleBuilder, location, sampleBuilder.getI1Type(), sampledGate);
      } else {
        auto integer = cast<IntegerType>(gateType);
        Value zero =
            arith::ConstantOp::create(sampleBuilder, location, integer,
                                      sampleBuilder.getIntegerAttr(integer, 0));
        gateValue =
            arith::CmpIOp::create(sampleBuilder, location,
                                  arith::CmpIPredicate::ne, sampledGate, zero);
      }
    }
    sim::SimClockedSampleUpdateOp::create(
        sampleBuilder, location, entry.getArgument(0), currentSample, gateValue,
        sampleBuilder.getI64IntegerAttr(siteID),
        sampleBuilder.getI64IntegerAttr(depth));
    if (needsExpressionHistoryDefault(expression)) {
      Value valid = arith::ConstantOp::create(sampleBuilder, location,
                                              sampleBuilder.getI1Type(),
                                              sampleBuilder.getBoolAttr(true));
      sim::SimClockedSampleUpdateOp::create(
          sampleBuilder, location, entry.getArgument(0), valid, gateValue,
          sampleBuilder.getI64IntegerAttr(historyValidityID(siteID)),
          sampleBuilder.getI64IntegerAttr(depth));
    }
    cf::BranchOp::create(sampleBuilder, location, wait);
    sampler->setAttr(sim::metadata::lowered, builder.getUnitAttr());
  }

  Value processContext = function.getBody().front().getArgument(0);
  Value prior =
      sim::SimClockedSampleReadOp::create(
          builder, location, sourceType, processContext,
          builder.getI64IntegerAttr(siteID), builder.getI64IntegerAttr(depth),
          builder.getI64IntegerAttr(age))
          .getResult();
  if (!needsExpressionHistoryDefault(expression))
    return prior;
  Value valid = sim::SimClockedSampleReadOp::create(
      builder, location, builder.getI1Type(), processContext,
      builder.getI64IntegerAttr(historyValidityID(siteID)),
      builder.getI64IntegerAttr(depth), builder.getI64IntegerAttr(age));
  Block *missing = addBlock();
  Block *join = addBlock();
  join->addArgument(sourceType, location);
  cf::CondBranchOp::create(builder, location, valid, join, ValueRange{prior},
                           missing, ValueRange{});
  setCurrent(missing);
  bool savedDefaults = sampleAssertionDefaults;
  sampleAssertionDefaults = true;
  FailureOr<Value> initial = lowerSampledValue(expression, location);
  sampleAssertionDefaults = savedDefaults;
  if (failed(initial))
    return failure();
  cf::BranchOp::create(builder, location, join, ValueRange{*initial});
  setCurrent(join);
  return join->getArgument(0);
}

FailureOr<Value> UnitLowering::lowerSampledValue(Operation *expression,
                                                 Location location) {
  // IEEE 1800-2023 16.5.1 defines expression sampling recursively. Evaluate
  // operators on sampled operands, including nested sampled-value calls;
  // the expression itself need not denote storage. Reuse assertion rvalue
  // lowering so packed selections sample their base and selector correctly.
  // Restore the caller's mode before lowering surrounding procedural work.
  bool savedSampleAssertionValues = sampleAssertionValues;
  sampleAssertionValues = !sampleAssertionDefaults;
  FailureOr<Value> sampled = lowerExpression(expression);
  sampleAssertionValues = savedSampleAssertionValues;
  if (failed(sampled))
    return failure();
  if (!sim::getPackedWidth((*sampled).getType())) {
    emitError(getSemanticLocation(expression))
        << "sampled-value expressions currently require packed values";
    return failure();
  }
  return *sampled;
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

  if (name == "$sdf_annotate" &&
      op->hasAttr("obelisk.sdf_compile_time_applied")) {
    // IEEE 1800-2017 32.9 makes this task load timing data. The frontend has
    // already resolved this statically named file into the exact Clause 30
    // attributes, so executing a runtime reader would duplicate annotation
    // and put a name lookup on the AOT path.
    return dummyTaskResult();
  }

  if (name.starts_with("$async$") || name.starts_with("$sync$"))
    return lowerPlaSystemCall(op);

  if (name == "$timeunit" || name == "$timeprecision") {
    if (children.size() > 1) {
      emitError(location) << name << " accepts zero or one scope";
      return failure();
    }
    StringAttr targetPath = op.getSystemScopePathAttr();
    if (!children.empty())
      targetPath =
          children.front()->getAttrOfType<StringAttr>("referenced_path");
    if (!targetPath) {
      emitError(location) << name << " has no elaborated target scope";
      return failure();
    }
    sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
    sim::SimScopeDeclOp targetScope;
    if (design)
      for (sim::SimScopeDeclOp scope :
           design.getBody().front().getOps<sim::SimScopeDeclOp>())
        if (scope.getHierarchicalName() &&
            *scope.getHierarchicalName() == targetPath.getValue()) {
          targetScope = scope;
          break;
        }
    IntegerAttr scale;
    if (targetScope) {
      StringRef scaleName = name == "$timeunit" ? "dpi_unit_femtoseconds"
                                                : "dpi_precision_femtoseconds";
      scale = targetScope->getAttrOfType<IntegerAttr>(scaleName);
    } else if (children.empty()) {
      StringRef scaleName = name == "$timeunit"
                                ? "system_scope_time_unit_fs"
                                : "system_scope_time_precision_fs";
      scale = op->getAttrOfType<IntegerAttr>(scaleName);
    }
    if (!scale) {
      emitError(location) << name << " target scope '" << targetPath.getValue()
                          << "' has no simulation descriptor";
      return failure();
    }
    if (!scale.getValue().isStrictlyPositive()) {
      emitError(location) << name << " target has no frozen time scale";
      return failure();
    }
    uint64_t femtoseconds = scale.getValue().getZExtValue();
    int32_t exponent = -15;
    while (femtoseconds > 1 && femtoseconds % 10 == 0) {
      femtoseconds /= 10;
      ++exponent;
    }
    if (femtoseconds != 1) {
      emitError(location) << name
                          << " target time scale is not a decimal power";
      return failure();
    }
    return convertResult(constant(i32, exponent));
  }

  if (name == "$asserton" || name == "$assertoff" || name == "$assertkill" ||
      name == "$assertpasson" || name == "$assertpassoff" ||
      name == "$assertfailon" || name == "$assertfailoff" ||
      name == "$assertnonvacuouson" || name == "$assertvacuousoff" ||
      name == "$assertcontrol") {
    auto action =
        op->getAttrOfType<IntegerAttr>("obelisk_sim.assertion_control_action");
    auto actionArgument = op->getAttrOfType<IntegerAttr>(
        "obelisk_sim.assertion_control_action_argument");
    auto targets = op->getAttrOfType<DenseI64ArrayAttr>(
        "obelisk_sim.assertion_control_ids");
    if (static_cast<bool>(action) == static_cast<bool>(actionArgument) ||
        !targets) {
      emitError(location) << name
                          << " has no prepared assertion-control selection";
      return failure();
    }
    auto depths = op->getAttrOfType<DenseI64ArrayAttr>(
        "obelisk_sim.assertion_control_depths");
    auto levelsArgument = op->getAttrOfType<IntegerAttr>(
        "obelisk_sim.assertion_control_levels_argument");
    auto assertionTypes = op->getAttrOfType<DenseI64ArrayAttr>(
        "obelisk_sim.assertion_control_assertion_types");
    auto assertionTypesArgument = op->getAttrOfType<IntegerAttr>(
        "obelisk_sim.assertion_control_assertion_types_argument");
    auto directiveTypes = op->getAttrOfType<DenseI64ArrayAttr>(
        "obelisk_sim.assertion_control_directive_types");
    auto directiveTypesArgument = op->getAttrOfType<IntegerAttr>(
        "obelisk_sim.assertion_control_directive_types_argument");
    if (static_cast<bool>(depths) != static_cast<bool>(levelsArgument) ||
        static_cast<bool>(assertionTypes) !=
            static_cast<bool>(assertionTypesArgument) ||
        static_cast<bool>(directiveTypes) !=
            static_cast<bool>(directiveTypesArgument) ||
        (depths && depths.size() != targets.size()) ||
        (assertionTypes && assertionTypes.size() != targets.size()) ||
        (directiveTypes && directiveTypes.size() != targets.size())) {
      emitError(location)
          << name << " has malformed dynamic assertion-control metadata";
      return failure();
    }

    auto lowerControlInteger = [&](IntegerAttr argument,
                                   StringRef role) -> FailureOr<Value> {
      uint64_t index = argument.getValue().getZExtValue();
      if (index >= children.size()) {
        emitError(location)
            << name << " has an invalid assertion-control " << role << " index";
        return failure();
      }
      FailureOr<Value> lowered = lowerExpression(children[index]);
      if (failed(lowered))
        return failure();
      FailureOr<Value> converted =
          convert(*lowered, i64, isSignedNode(children[index]), location);
      if (failed(converted)) {
        emitError(getSemanticLocation(children[index]))
            << name << " " << role << " value is not an executable integer";
        return failure();
      }
      return *converted;
    };

    Value assertionTypeMask;
    if (assertionTypes) {
      FailureOr<Value> lowered =
          lowerControlInteger(assertionTypesArgument, "assertion-type mask");
      if (failed(lowered))
        return failure();
      assertionTypeMask = *lowered;
    }
    Value directiveTypeMask;
    if (directiveTypes) {
      FailureOr<Value> lowered =
          lowerControlInteger(directiveTypesArgument, "directive-type mask");
      if (failed(lowered))
        return failure();
      directiveTypeMask = *lowered;
    }
    Value levels;
    if (depths) {
      FailureOr<Value> lowered = lowerControlInteger(levelsArgument, "levels");
      if (failed(lowered))
        return failure();
      levels = *lowered;
    }
    Value dynamicAction;
    if (actionArgument) {
      FailureOr<Value> lowered =
          lowerControlInteger(actionArgument, "control type");
      if (failed(lowered))
        return failure();
      dynamicAction = *lowered;
    }

    Value zero;
    if (depths || assertionTypes || directiveTypes)
      zero = constant(i64, 0);

    // A dynamic mask receives the same validity check as a literal mask, but
    // at the point where the task executes and after its argument expressions
    // have been evaluated. All eight assertion-type bits are defined by IEEE
    // 1800-2017 20.12, including unique/unique0/priority violation reports.
    Value invalidMask;
    auto addInvalidMask = [&](Value mask, uint64_t supported) {
      Value unsupported = arith::AndIOp::create(builder, location, mask,
                                                constant(i64, ~supported));
      Value invalid = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, unsupported, zero);
      invalidMask = invalidMask ? arith::OrIOp::create(builder, location,
                                                       invalidMask, invalid)
                                : invalid;
    };
    if (assertionTypes)
      addInvalidMask(assertionTypeMask, UINT64_C(255));
    if (directiveTypes)
      addInvalidMask(directiveTypeMask, UINT64_C(7));
    if (invalidMask) {
      Block *invalid = addBlock();
      Block *valid = addBlock();
      cf::CondBranchOp::create(builder, location, invalidMask, invalid, valid);
      setCurrent(invalid);
      if (failed(emitRuntimeFatal(
              location,
              "assertion-control mask contains a value outside the assertion "
              "or directive types defined by IEEE 1800-2017 20.12")))
        return failure();
      setCurrent(valid);
    }

    Block *dynamicActionResume = nullptr;
    if (dynamicAction) {
      Value belowRange =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                                dynamicAction, constant(i64, 1));
      Value aboveRange =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ugt,
                                dynamicAction, constant(i64, 11));
      Value invalidAction =
          arith::OrIOp::create(builder, location, belowRange, aboveRange);
      Block *invalid = addBlock();
      Block *valid = addBlock();
      dynamicActionResume = addBlock();
      cf::CondBranchOp::create(builder, location, invalidAction, invalid,
                               valid);
      setCurrent(invalid);
      emitRuntimeWarning(
          location,
          "$assertcontrol control type is outside the valid range 1 through "
          "11; the task has no effect");
      emitBranch(dynamicActionResume);
      setCurrent(valid);
      dynamicAction =
          arith::TruncIOp::create(builder, location, i32, dynamicAction);
    }

    ArrayRef<int64_t> targetValues = targets.asArrayRef();
    ArrayRef<int64_t> depthValues =
        depths ? depths.asArrayRef() : ArrayRef<int64_t>{};
    ArrayRef<int64_t> assertionTypeValues =
        assertionTypes ? assertionTypes.asArrayRef() : ArrayRef<int64_t>{};
    ArrayRef<int64_t> directiveTypeValues =
        directiveTypes ? directiveTypes.asArrayRef() : ArrayRef<int64_t>{};
    for (auto [index, target] : llvm::enumerate(targetValues)) {
      int64_t depth = depths ? depthValues[index] : -1;
      Value selected;
      auto addSelection = [&](Value condition) {
        selected = selected ? arith::AndIOp::create(builder, location, selected,
                                                    condition)
                            : condition;
      };
      if (depths && depth >= 0) {
        Value allLevels = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::eq, levels, zero);
        Value includesDepth = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ugt, levels,
            constant(i64, static_cast<uint64_t>(depth)));
        addSelection(
            arith::OrIOp::create(builder, location, allLevels, includesDepth));
      }
      if (assertionTypes) {
        Value matched = arith::AndIOp::create(
            builder, location, assertionTypeMask,
            constant(i64, static_cast<uint64_t>(assertionTypeValues[index])));
        addSelection(arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, matched, zero));
      }
      if (directiveTypes && directiveTypeValues[index] != 0) {
        Value matched = arith::AndIOp::create(
            builder, location, directiveTypeMask,
            constant(i64, static_cast<uint64_t>(directiveTypeValues[index])));
        addSelection(arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, matched, zero));
      }
      if (!selected) {
        if (dynamicAction)
          sim::SimAssertionControlDynamicOp::create(
              builder, location, context, dynamicAction,
              builder.getI64IntegerAttr(target));
        else
          sim::SimAssertionControlOp::create(
              builder, location, context,
              builder.getI32IntegerAttr(static_cast<int32_t>(action.getInt())),
              builder.getI64IntegerAttr(target));
        continue;
      }

      Block *apply = addBlock();
      Block *resume = addBlock();
      cf::CondBranchOp::create(builder, location, selected, apply, resume);
      setCurrent(apply);
      if (dynamicAction)
        sim::SimAssertionControlDynamicOp::create(
            builder, location, context, dynamicAction,
            builder.getI64IntegerAttr(target));
      else
        sim::SimAssertionControlOp::create(
            builder, location, context,
            builder.getI32IntegerAttr(static_cast<int32_t>(action.getInt())),
            builder.getI64IntegerAttr(target));
      emitBranch(resume);
      setCurrent(resume);
    }
    if (dynamicActionResume) {
      emitBranch(dynamicActionResume);
      setCurrent(dynamicActionResume);
    }
    return dummyTaskResult();
  }

  if (name == "$countdrivers") {
    if (children.empty() || children.size() > 6) {
      emitError(location) << "$countdrivers requires one to six arguments";
      return failure();
    }
    FailureOr<Value> net = lowerExpression(children.front(), true);
    auto netType = succeeded(net) ? dyn_cast<sim::NetType>((*net).getType())
                                  : sim::NetType{};
    if (!netType || sim::getPackedWidth(netType.getElementType()) != 1) {
      emitError(getSemanticLocation(children.front()))
          << "$countdrivers requires a scalar net or a bit-select of a "
             "vector net";
      return failure();
    }
    auto counts = sim::SimNetCountDriversOp::create(
        builder, location, TypeRange{i32, i32, i32, i32, i32}, *net);
    SmallVector<Value, 5> values{counts.getForced(), counts.getTotal(),
                                 counts.getZero(), counts.getOne(),
                                 counts.getUnknown()};
    for (size_t index = 1; index != children.size(); ++index) {
      Operation *actual = children[index];
      if (auto assignment =
              dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
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
            actual = outputChildren.front();
        }
      }
      FailureOr<Value> destination = lowerExpression(actual, true);
      Type destinationType = succeeded(destination)
                                 ? getReferenceElementType(*destination)
                                 : Type{};
      if (!destinationType) {
        emitError(getSemanticLocation(actual))
            << "$countdrivers output argument must be a writable variable";
        return failure();
      }
      FailureOr<Value> converted =
          convert(values[index - 1], destinationType, false, location);
      if (failed(converted) ||
          failed(storeReference(*destination, *converted, location)))
        return failure();
    }
    Value multiple =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ugt,
                              counts.getTotal(), constant(i32, 1));
    return convertResult(multiple);
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
      // The elaborated inout argument commonly carries an implicit integral
      // conversion.  That conversion describes the value passed to the
      // system function, but it is not itself an lvalue.  Preserve the
      // underlying writable variable so the updated seed is stored back.
      Operation *seedNode = children.front();
      while (isa<semantic::SVConversionExpressionOp>(seedNode)) {
        SmallVector<Operation *> converted = getChildren(seedNode);
        if (converted.size() != 1)
          break;
        seedNode = converted.front();
      }
      seedDestination = lowerExpression(seedNode, true);
      if (failed(seedDestination)) {
        emitError(getSemanticLocation(seedNode))
            << "$random seed must be a writable integral variable";
        return failure();
      }
      FailureOr<Value> seedValue =
          loadReference(*seedDestination, getSemanticLocation(seedNode));
      if (failed(seedValue))
        return failure();
      FailureOr<Value> seed32 =
          convert(*seedValue, i32, isSignedNode(seedNode), location);
      if (failed(seed32))
        return failure();
      Value low = constant(i32, INT32_MIN);
      Value high = constant(i32, INT32_MAX);
      auto draw = sim::SimRandomDistributionOp::create(
          builder, location, TypeRange{i32, i32}, context,
          OBELISK_RT_DISTRIBUTION_UNIFORM, *seed32, low, high);

      Type destinationType = getReferenceElementType(*seedDestination);
      FailureOr<Value> updated =
          convert(draw.getNextSeed(), destinationType, true, location);
      if (failed(updated) ||
          failed(storeReference(*seedDestination, *updated, location)))
        return failure();
      return convertResult(draw.getResult());
    }
    Value value =
        sim::SimLegacyRandomOp::create(builder, location, i32, context);
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

  std::optional<uint32_t> queueAction =
      llvm::StringSwitch<std::optional<uint32_t>>(name)
          .Case("$q_initialize", OBELISK_RT_STOCHASTIC_QUEUE_INITIALIZE)
          .Case("$q_add", OBELISK_RT_STOCHASTIC_QUEUE_ADD)
          .Case("$q_remove", OBELISK_RT_STOCHASTIC_QUEUE_REMOVE)
          .Case("$q_full", OBELISK_RT_STOCHASTIC_QUEUE_FULL)
          .Case("$q_exam", OBELISK_RT_STOCHASTIC_QUEUE_EXAM)
          .Default(std::nullopt);
  if (queueAction) {
    size_t expected = name == "$q_full" ? 2 : 4;
    if (children.size() != expected) {
      emitError(location) << name << " requires exactly " << expected
                          << " arguments";
      return failure();
    }

    Type logic32 = sim::LogicType::get(function.getContext(), 32);
    Type logic64 = sim::LogicType::get(function.getContext(), 64);
    auto lowerLogic32 = [&](Operation *child) -> FailureOr<Value> {
      FailureOr<Value> value = lowerExpression(child);
      if (failed(value))
        return failure();
      return convert(*value, logic32, isSignedNode(child),
                     getSemanticLocation(child));
    };
    auto outputTarget = [&](Operation *actual) -> Operation * {
      auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(actual);
      if (!assignment)
        return actual;
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() != 2)
        return actual;
      Operation *placeholder = outputChildren[1];
      while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
        SmallVector<Operation *> converted = getChildren(placeholder);
        if (converted.size() != 1)
          break;
        placeholder = converted.front();
      }
      return isa<semantic::SVEmptyArgumentExpressionOp>(placeholder)
                 ? outputChildren.front()
                 : actual;
    };
    auto storeOutput = [&](Operation *actual, Value value,
                           bool sourceSigned = false) -> LogicalResult {
      Operation *target = outputTarget(actual);
      FailureOr<Value> destination = lowerExpression(target, true);
      Type destinationType = succeeded(destination)
                                 ? getReferenceElementType(*destination)
                                 : Type{};
      if (!destinationType) {
        emitError(getSemanticLocation(target))
            << name << " output argument must be a writable variable";
        return failure();
      }
      FailureOr<Value> converted =
          convert(value, destinationType, sourceSigned, location);
      if (failed(converted))
        return failure();
      return storeReference(*destination, *converted, location);
    };

    FailureOr<Value> id = lowerLogic32(children[0]);
    if (failed(id))
      return failure();
    auto plane = builder.getI32Type();
    Value zero = sim::SimLogicConstantOp::create(
        builder, location, logic32, builder.getIntegerAttr(plane, 0),
        builder.getIntegerAttr(plane, 0));
    Value first = zero;
    Value second = zero;
    if (*queueAction == OBELISK_RT_STOCHASTIC_QUEUE_INITIALIZE ||
        *queueAction == OBELISK_RT_STOCHASTIC_QUEUE_ADD) {
      FailureOr<Value> loweredFirst = lowerLogic32(children[1]);
      FailureOr<Value> loweredSecond = lowerLogic32(children[2]);
      if (failed(loweredFirst) || failed(loweredSecond))
        return failure();
      first = *loweredFirst;
      second = *loweredSecond;
    } else if (*queueAction == OBELISK_RT_STOCHASTIC_QUEUE_EXAM) {
      FailureOr<Value> loweredCode = lowerLogic32(children[1]);
      if (failed(loweredCode))
        return failure();
      first = *loweredCode;
    }

    auto scale = function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
    if (!scale || !scale.getValue().isStrictlyPositive()) {
      function.emitError("code unit has no valid frozen time scale");
      return failure();
    }
    auto queue = sim::SimStochasticQueueOp::create(
        builder, location, TypeRange{logic64, logic64, i32}, context,
        *queueAction, *id, first, second, scale.getValue().getZExtValue());

    size_t statusIndex = name == "$q_full" ? 1 : 3;
    if (*queueAction == OBELISK_RT_STOCHASTIC_QUEUE_REMOVE) {
      if (failed(storeOutput(children[1], queue.getPrimary())) ||
          failed(storeOutput(children[2], queue.getSecondary())))
        return failure();
    } else if (*queueAction == OBELISK_RT_STOCHASTIC_QUEUE_EXAM) {
      if (failed(storeOutput(children[2], queue.getPrimary())))
        return failure();
    }
    if (failed(storeOutput(children[statusIndex], queue.getStatus())))
      return failure();
    if (*queueAction == OBELISK_RT_STOCHASTIC_QUEUE_FULL)
      return convertResult(queue.getPrimary());
    return dummyTaskResult();
  }

  auto sampledValue = [&](Operation *expression) -> FailureOr<Value> {
    return lowerSampledValue(expression, location);
  };
  auto sampledSiteID = [&]() {
    auto nodeAttr = op->getAttrOfType<IntegerAttr>("node_id");
    uint64_t node = nodeAttr ? nodeAttr.getValue().getZExtValue() : 0;
    return stableCodeUnitID(
        (function.getSymName() + ".$sampled." + Twine(node) + "." + Twine(name))
            .str());
  };
  auto sampledHistory = [&](Value current, Value gate,
                            uint64_t depth) -> FailureOr<Value> {
    Value previous = sim::SimSampledHistoryOp::create(
                         builder, location, current.getType(), context, current,
                         gate, builder.getI64IntegerAttr(sampledSiteID()),
                         builder.getI64IntegerAttr(depth))
                         .getResult();
    // A plain static variable without an initializer already has the type
    // default represented by the runtime ring. Keep that common path unchanged.
    if (!needsExpressionHistoryDefault(children.front()))
      return previous;
    // A parallel one-bit history describes which samples actually exist.
    // It uses the same gate/depth and the existing instruction/ABI. Underflow
    // evaluates the expression's default, rather than the result type's zero/X.
    uint64_t validID = historyValidityID(sampledSiteID());
    Value valid = sim::SimSampledHistoryOp::create(
        builder, location, builder.getI1Type(), context,
        constant(builder.getI1Type(), 1), gate,
        builder.getI64IntegerAttr(validID), builder.getI64IntegerAttr(depth));
    Block *missing = addBlock();
    Block *join = addBlock();
    join->addArgument(current.getType(), location);
    cf::CondBranchOp::create(builder, location, valid, join,
                             ValueRange{previous}, missing, ValueRange{});
    setCurrent(missing);
    bool savedDefaults = sampleAssertionDefaults;
    sampleAssertionDefaults = true;
    FailureOr<Value> initial = lowerSampledValue(children.front(), location);
    sampleAssertionDefaults = savedDefaults;
    if (failed(initial))
      return failure();
    cf::BranchOp::create(builder, location, join, ValueRange{*initial});
    setCurrent(join);
    return join->getArgument(0);
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

  if (isGlobalFutureSampledFunction(name)) {
    if (children.size() != 1) {
      emitError(location) << name << " requires exactly one argument";
      return failure();
    }
    if (!function->hasAttr("obelisk_sim.global_future_resolver")) {
      emitError(location)
          << name << " requires the detached global-future assertion resolver";
      return failure();
    }
    FailureOr<Value> future = sampledValue(children.front());
    if (failed(future))
      return failure();
    if (name == "$future_gclk")
      return convertResult(*future);

    Value current = globalFutureCurrentCaptures.lookup(op.getOperation());
    if (!current) {
      emitError(location) << name << " has no frozen endpoint value";
      return failure();
    }
    FailureOr<Value> equal =
        conditionalEqual(current, *future, current.getType(), location,
                         /*caseEquality=*/true);
    if (failed(equal))
      return failure();
    if (name == "$steady_gclk")
      return convertResult(*equal);
    if (name == "$changing_gclk")
      return convertResult(arith::XOrIOp::create(
          builder, location, *equal, constant(builder.getI1Type(), 1)));

    FailureOr<Value> currentScalar = toPackedScalar(current, location);
    FailureOr<Value> futureScalar = toPackedScalar(*future, location);
    if (failed(currentScalar) || failed(futureScalar))
      return failure();
    bool rising = name == "$rising_gclk";
    Value currentBit, futureBit;
    if (isa<sim::LogicType>((*currentScalar).getType())) {
      Type bitType = sim::LogicType::get(function.getContext(), 1);
      currentBit = sim::SimLogicExtractOp::create(builder, location, bitType,
                                                  *currentScalar, 0);
      futureBit = sim::SimLogicExtractOp::create(builder, location, bitType,
                                                 *futureScalar, 0);
      Value target = sim::SimLogicConstantOp::create(
          builder, location, bitType,
          builder.getIntegerAttr(builder.getI1Type(), rising ? 1 : 0),
          builder.getIntegerAttr(builder.getI1Type(), 0));
      Value currentIsTarget = sim::SimLogicCompareOp::create(
          builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
          currentBit, target);
      Value futureIsTarget = sim::SimLogicCompareOp::create(
          builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
          futureBit, target);
      Value currentIsNotTarget = arith::XOrIOp::create(
          builder, location, currentIsTarget, constant(builder.getI1Type(), 1));
      return convertResult(arith::AndIOp::create(
          builder, location, currentIsNotTarget, futureIsTarget));
    }
    auto integer = cast<IntegerType>((*currentScalar).getType());
    auto bit = [&](Value value) -> Value {
      if (integer.getWidth() == 1)
        return value;
      return arith::TruncIOp::create(builder, location, builder.getI1Type(),
                                     value);
    };
    currentBit = bit(*currentScalar);
    futureBit = bit(*futureScalar);
    Value target = constant(builder.getI1Type(), rising ? 1 : 0);
    Value currentIsTarget = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, currentBit, target);
    Value futureIsTarget = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, futureBit, target);
    return convertResult(arith::AndIOp::create(
        builder, location,
        arith::XOrIOp::create(builder, location, currentIsTarget,
                              constant(builder.getI1Type(), 1)),
        futureIsTarget));
  }

  bool globalPastFunction = isGlobalPastSampledFunction(name);
  StringRef historyName = llvm::StringSwitch<StringRef>(name)
                              .Case("$past_gclk", "$past")
                              .Case("$rose_gclk", "$rose")
                              .Case("$fell_gclk", "$fell")
                              .Case("$stable_gclk", "$stable")
                              .Case("$changed_gclk", "$changed")
                              .Default(name);
  bool historyFunction = historyName == "$past" || historyName == "$rose" ||
                         historyName == "$fell" || historyName == "$stable" ||
                         historyName == "$changed";
  if (historyFunction) {
    size_t maximum = globalPastFunction ? 1 : historyName == "$past" ? 4 : 2;
    if (children.empty() || children.size() > maximum) {
      emitError(location) << name << " requires "
                          << (globalPastFunction       ? "exactly one"
                              : historyName == "$past" ? "one to four"
                                                       : "one or two")
                          << " arguments";
      return failure();
    }
    if (sampleAssertionDefaults) {
      if (historyName == "$past") {
        FailureOr<Value> initial = sampledValue(children.front());
        return failed(initial) ? FailureOr<Value>(failure())
                               : convertResult(*initial);
      }
      return convertResult(
          constant(builder.getI1Type(), historyName == "$stable"));
    }
    Operation *clockArgument = nullptr;
    semantic::SVSignalEventControlOp explicitEvent;
    bool alternateClock = false;
    if (!globalPastFunction &&
        ((historyName == "$past" && children.size() >= 4) ||
         (historyName != "$past" && children.size() >= 2)))
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

    if (globalPastFunction)
      alternateClock = true;

    uint64_t depth = 1;
    if (historyName == "$past" && !globalPastFunction && children.size() >= 2 &&
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
          historyName == "$past" && !globalPastFunction &&
                  children.size() >= 3 &&
                  !isa<semantic::SVEmptyArgumentExpressionOp>(children[2])
              ? children[2]
              : nullptr;
      Operation *sampleClock =
          globalPastFunction ? op.getOperation() : explicitEvent.getOperation();
      if (historyName == "$past") {
        FailureOr<Value> past =
            lowerAlternateClockSample(children.front(), gateExpression,
                                      sampleClock, depth, depth - 1, location);
        return failed(past) ? FailureOr<Value>(failure())
                            : convertResult(*past);
      }
      current = sampledValue(children.front());
      FailureOr<Value> prior = lowerAlternateClockSample(
          children.front(), nullptr, sampleClock, 1, 0, location);
      if (failed(current) || failed(prior))
        return failure();
      previous = *prior;
    } else {
      current = sampledValue(children.front());
      if (failed(current))
        return failure();
      Value gate = constant(builder.getI1Type(), 1);
      if (historyName == "$past" && children.size() >= 3 &&
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
      FailureOr<Value> prior = sampledHistory(*current, gate, depth);
      if (failed(prior))
        return failure();
      previous = *prior;
      if (historyName == "$past")
        return convertResult(previous);
    }

    FailureOr<Value> equal =
        conditionalEqual(*current, previous, (*current).getType(), location,
                         /*caseEquality=*/true);
    if (failed(equal))
      return failure();
    if (historyName == "$stable")
      return convertResult(*equal);
    if (historyName == "$changed") {
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
      bool target = historyName == "$rose";
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
      Value notPrevious =
          arith::XOrIOp::create(builder, location, previousIsTarget,
                                constant(builder.getI1Type(), 1));
      return convertResult(arith::AndIOp::create(builder, location,
                                                 currentIsTarget, notPrevious));
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
    Value transition =
        historyName == "$rose"
            ? arith::AndIOp::create(
                  builder, location, currentBit,
                  arith::XOrIOp::create(builder, location, previousBit,
                                        constant(builder.getI1Type(), 1)))
            : arith::AndIOp::create(
                  builder, location, previousBit,
                  arith::XOrIOp::create(builder, location, currentBit,
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
      Value sourceID = sim::SimClassIdOp::create(builder, location,
                                                 builder.getI64Type(), source);
      Value isNull =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                sourceID, constant(builder.getI64Type(), 0));
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
        emitError(location) << "enum $cast requires packed integral operands";
        return failure();
      }
      unsigned comparisonWidth = std::max(*sourceWidth, *destinationWidth);
      Type comparisonType =
          isa<sim::LogicType>(sourceScalar) ||
                  isa<sim::LogicType>(destinationScalar)
              ? Type(
                    sim::LogicType::get(function.getContext(), comparisonWidth))
              : Type(IntegerType::get(function.getContext(), comparisonWidth));
      bool comparisonSigned =
          isSignedNode(children[1]) && isSignedNode(destination);
      FailureOr<Value> comparisonSource = convert(
          source, comparisonType, comparisonSigned, location, comparisonSigned);
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
        FailureOr<Value> member =
            frozen && frozen.getType() == destinationType
                ? sim::materializeFrozenConstant(builder, location, frozen)
                : FailureOr<Value>(failure());
        if (failed(member)) {
          emitError(location) << "enum $cast has malformed frozen membership";
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
        succeeded = arith::OrIOp::create(builder, location, succeeded, *equal);
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
        if (failed(
                emitRuntimeFatal(location, "$cast failed when used as a task")))
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
      FailureOr<Value> operand = lowerExpression(children.front());
      if (failed(operand))
        return failure();

      auto constant32 = [&](uint64_t value) -> Value {
        return arith::ConstantOp::create(
            builder, location, i32,
            builder.getIntegerAttr(i32, APInt(32, value)));
      };
      auto constant64 = [&](uint64_t value) -> Value {
        return arith::ConstantOp::create(
            builder, location, i64,
            builder.getIntegerAttr(i64, APInt(64, value)));
      };
      auto add = [&](Value lhs, Value rhs) -> Value {
        return arith::AddIOp::create(builder, location, lhs, rhs);
      };

      // Implicit event controls need stable handles at their eventual wait
      // point. Dynamic parents carry value-semantic nested mutations through
      // their own watch. Fixed aggregates have no such identity, so enumerate
      // only their fixed shape here and watch each dynamic leaf directly.
      // Ordinary `$bits` calls do not build this dependency-only inventory.
      std::function<LogicalResult(Value, Type)> recordLiveDependencies;
      recordLiveDependencies = [&](Value value,
                                   Type sourceType) -> LogicalResult {
        if (getSemanticBitstreamWidth(sourceType) ||
            isa<semantic::VoidType, semantic::StringType>(sourceType))
          return success();

        if (isa<semantic::DynArrayType, semantic::QueueType,
                semantic::AssocArrayType>(sourceType)) {
          if (!isa<sim::DynamicArrayType, sim::QueueType, sim::AssocArrayType>(
                  value.getType()))
            return failure();
          recordContainerSizeRead(value, location);
          return success();
        }

        Type fixedElementType;
        if (auto array =
                dyn_cast<semantic::RangedUnpackedArrayType>(sourceType))
          fixedElementType = array.getElementType();
        else if (auto array = dyn_cast<semantic::UnpackedArrayType>(sourceType))
          fixedElementType = array.getElementType();
        if (fixedElementType) {
          auto array = dyn_cast<sim::UnpackedArrayType>(value.getType());
          if (!array)
            return failure();
          for (uint64_t ordinal = 0,
                        count = sim::getAggregateNumElements(array);
               ordinal != count; ++ordinal) {
            Value element = sim::SimAggregateExtractOp::create(
                builder, location, array.getElementType(), value, ordinal);
            if (failed(recordLiveDependencies(element, fixedElementType)))
              return failure();
          }
          return success();
        }

        SmallVector<Type> fieldTypes;
        bool unionType = false;
        bool taggedUnion = false;
        if (auto aggregate =
                dyn_cast<semantic::SourceAggregateType>(sourceType)) {
          unionType = aggregate.getIsUnion();
          taggedUnion = aggregate.getIsTagged();
          for (Attribute fieldAttr : aggregate.getFields()) {
            auto field = dyn_cast<DictionaryAttr>(fieldAttr);
            auto type = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
            if (!type)
              return failure();
            fieldTypes.push_back(type.getValue());
          }
        } else if (auto structure =
                       dyn_cast<semantic::UnpackedStructType>(sourceType)) {
          for (NamedAttribute field : structure.getFields()) {
            auto type = dyn_cast<TypeAttr>(field.getValue());
            if (!type)
              return failure();
            fieldTypes.push_back(type.getValue());
          }
        } else if (isa<semantic::UnpackedUnionType>(sourceType)) {
          unionType = true;
        }
        if (fieldTypes.empty())
          return failure();
        if (unionType) {
          auto unionValue = dyn_cast<sim::UnpackedUnionType>(value.getType());
          if (!taggedUnion || !unionValue || !unionValue.getIsTagged())
            return failure();
        } else if (!isa<sim::UnpackedStructType>(value.getType())) {
          return failure();
        }
        for (auto [ordinal, fieldType] : llvm::enumerate(fieldTypes)) {
          if (getSemanticBitstreamWidth(fieldType) ||
              isa<semantic::VoidType, semantic::StringType>(fieldType))
            continue;
          Type valueFieldType =
              sim::getAggregateElementType(value.getType(), ordinal);
          if (!valueFieldType)
            return failure();
          Value field =
              unionType
                  ? Value(sim::SimUnionExtractOp::create(
                        builder, location, valueFieldType, value, ordinal))
                  : Value(sim::SimAggregateExtractOp::create(
                        builder, location, valueFieldType, value, ordinal));
          if (failed(recordLiveDependencies(field, fieldType)))
            return failure();
        }
        return success();
      };

      std::function<FailureOr<Value>(Value, Type, bool)> lowerLiveWidth;
      lowerLiveWidth = [&](Value value, Type sourceType,
                           bool recordDependency) -> FailureOr<Value> {
        if (std::optional<uint64_t> fixed =
                getSemanticBitstreamWidth(sourceType))
          return constant32(*fixed);
        if (isa<semantic::VoidType>(sourceType))
          return constant32(0);

        if (isa<semantic::StringType>(sourceType)) {
          if (!isa<sim::StringType>(value.getType()))
            return failure();
          Value size =
              sim::SimStringLengthOp::create(builder, location, i64, value);
          Value size32 = arith::TruncIOp::create(builder, location, i32, size);
          return Value(
              arith::MulIOp::create(builder, location, size32, constant32(8)));
        }

        Type elementType;
        if (auto array = dyn_cast<semantic::DynArrayType>(sourceType))
          elementType = array.getElementType();
        else if (auto queue = dyn_cast<semantic::QueueType>(sourceType))
          elementType = queue.getElementType();
        else if (auto associative =
                     dyn_cast<semantic::AssocArrayType>(sourceType))
          elementType = associative.getElementType();
        if (elementType) {
          Type valueElementType;
          if (auto array = dyn_cast<sim::DynamicArrayType>(value.getType()))
            valueElementType = array.getElementType();
          else if (auto queue = dyn_cast<sim::QueueType>(value.getType()))
            valueElementType = queue.getElementType();
          else if (auto associative =
                       dyn_cast<sim::AssocArrayType>(value.getType()))
            valueElementType = associative.getElementType();
          if (!valueElementType)
            return failure();
          // One managed-container watch covers every value-semantic mutation
          // reachable through that container. Do not manufacture a
          // non-dominating wait operand for a nested handle discovered only
          // inside the generated traversal loop.
          if (recordDependency)
            recordContainerSizeRead(value, location);
          Value size =
              sim::SimContainerSizeOp::create(builder, location, i64, value);
          if (std::optional<uint64_t> stride =
                  getSemanticBitstreamWidth(elementType)) {
            Value size32 =
                arith::TruncIOp::create(builder, location, i32, size);
            return Value(arith::MulIOp::create(builder, location, size32,
                                               constant32(*stride)));
          }

          if (isa<sim::DynamicArrayType, sim::QueueType>(value.getType())) {
            Block *header = addBlock();
            header->addArgument(i64, location);
            header->addArgument(i32, location);
            Block *body = addBlock();
            Block *exit = addBlock();
            exit->addArgument(i32, location);
            cf::BranchOp::create(builder, location, header,
                                 ValueRange{constant64(0), constant32(0)});
            setCurrent(header);
            Value index = header->getArgument(0);
            Value accumulated = header->getArgument(1);
            Value more = arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::ult, index, size);
            cf::CondBranchOp::create(builder, location, more, body,
                                     ValueRange{}, exit,
                                     ValueRange{accumulated});
            setCurrent(body);
            Value element = sim::SimContainerReadOp::create(
                builder, location, valueElementType, value, index);
            FailureOr<Value> nested =
                lowerLiveWidth(element, elementType, false);
            if (failed(nested))
              return failure();
            Value next =
                arith::AddIOp::create(builder, location, index, constant64(1));
            cf::BranchOp::create(builder, location, header,
                                 ValueRange{next, add(accumulated, *nested)});
            setCurrent(exit);
            return exit->getArgument(0);
          }

          auto associative = dyn_cast<sim::AssocArrayType>(value.getType());
          if (!associative)
            return failure();
          Value initialKey =
              createDefaultValue(builder, location, associative.getKeyType());
          if (!initialKey)
            return failure();
          FailureOr<std::pair<Value, Value>> first =
              traverseAssoc(value, initialKey, 1, true, location);
          if (failed(first))
            return failure();
          Block *header = addBlock();
          header->addArgument(associative.getKeyType(), location);
          header->addArgument(builder.getI1Type(), location);
          header->addArgument(i32, location);
          Block *body = addBlock();
          Block *exit = addBlock();
          exit->addArgument(i32, location);
          cf::BranchOp::create(
              builder, location, header,
              ValueRange{first->first, first->second, constant32(0)});
          setCurrent(header);
          Value key = header->getArgument(0);
          Value valid = header->getArgument(1);
          Value accumulated = header->getArgument(2);
          cf::CondBranchOp::create(builder, location, valid, body, ValueRange{},
                                   exit, ValueRange{accumulated});
          setCurrent(body);
          Value element = sim::SimAssocReadOp::create(
              builder, location, valueElementType, value, key);
          FailureOr<Value> nested = lowerLiveWidth(element, elementType, false);
          if (failed(nested))
            return failure();
          FailureOr<std::pair<Value, Value>> next =
              traverseAssoc(value, key, 1, false, location);
          if (failed(next))
            return failure();
          cf::BranchOp::create(
              builder, location, header,
              ValueRange{next->first, next->second, add(accumulated, *nested)});
          setCurrent(exit);
          return exit->getArgument(0);
        }

        Type fixedElementType;
        if (auto array =
                dyn_cast<semantic::RangedUnpackedArrayType>(sourceType))
          fixedElementType = array.getElementType();
        else if (auto array = dyn_cast<semantic::UnpackedArrayType>(sourceType))
          fixedElementType = array.getElementType();
        if (fixedElementType) {
          auto array = dyn_cast<sim::UnpackedArrayType>(value.getType());
          if (!array)
            return failure();
          Value total = constant32(0);
          for (uint64_t ordinal = 0,
                        count = sim::getAggregateNumElements(array);
               ordinal != count; ++ordinal) {
            Value element = sim::SimAggregateExtractOp::create(
                builder, location, array.getElementType(), value, ordinal);
            FailureOr<Value> nested =
                lowerLiveWidth(element, fixedElementType, false);
            if (failed(nested))
              return failure();
            total = add(total, *nested);
          }
          return total;
        }

        SmallVector<Type> fieldTypes;
        bool unionType = false;
        bool taggedUnion = false;
        if (auto aggregate =
                dyn_cast<semantic::SourceAggregateType>(sourceType)) {
          unionType = aggregate.getIsUnion();
          taggedUnion = aggregate.getIsTagged();
          for (Attribute fieldAttr : aggregate.getFields()) {
            auto field = dyn_cast<DictionaryAttr>(fieldAttr);
            auto type = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
            if (!type)
              return failure();
            fieldTypes.push_back(type.getValue());
          }
        } else if (auto structure =
                       dyn_cast<semantic::UnpackedStructType>(sourceType)) {
          for (NamedAttribute field : structure.getFields()) {
            auto type = dyn_cast<TypeAttr>(field.getValue());
            if (!type)
              return failure();
            fieldTypes.push_back(type.getValue());
          }
        } else if (isa<semantic::UnpackedUnionType>(sourceType)) {
          unionType = true;
        }
        if (unionType) {
          auto unionValue = dyn_cast<sim::UnpackedUnionType>(value.getType());
          if (!taggedUnion || !unionValue || !unionValue.getIsTagged() ||
              fieldTypes.empty())
            return failure();
          Block *exit = addBlock();
          exit->addArgument(i32, location);
          for (auto [ordinal, fieldType] : llvm::enumerate(fieldTypes)) {
            Value active = sim::SimUnionIsActiveOp::create(
                builder, location, builder.getI1Type(), value, ordinal);
            Block *selected = addBlock();
            Block *next = addBlock();
            cf::CondBranchOp::create(builder, location, active, selected,
                                     ValueRange{}, next, ValueRange{});
            setCurrent(selected);
            FailureOr<Value> nested = failure();
            if (isa<semantic::VoidType>(fieldType)) {
              nested = constant32(0);
            } else {
              Type valueFieldType =
                  sim::getAggregateElementType(value.getType(), ordinal);
              if (!valueFieldType)
                return failure();
              Value field = sim::SimUnionExtractOp::create(
                  builder, location, valueFieldType, value, ordinal);
              nested = lowerLiveWidth(field, fieldType, false);
            }
            if (failed(nested))
              return failure();
            cf::BranchOp::create(builder, location, exit, ValueRange{*nested});
            setCurrent(next);
          }
          cf::BranchOp::create(builder, location, exit,
                               ValueRange{constant32(0)});
          setCurrent(exit);
          return exit->getArgument(0);
        }
        if (fieldTypes.empty() ||
            !isa<sim::UnpackedStructType>(value.getType()))
          return failure();

        Value total = constant32(0);
        for (auto [ordinal, fieldType] : llvm::enumerate(fieldTypes)) {
          if (std::optional<uint64_t> fixed =
                  getSemanticBitstreamWidth(fieldType)) {
            total = add(total, constant32(*fixed));
            continue;
          }
          Type valueFieldType =
              sim::getAggregateElementType(value.getType(), ordinal);
          if (!valueFieldType)
            return failure();
          Value field = sim::SimAggregateExtractOp::create(
              builder, location, valueFieldType, value, ordinal);
          FailureOr<Value> nested =
              lowerLiveWidth(field, fieldType, recordDependency);
          if (failed(nested))
            return failure();
          total = add(total, *nested);
        }
        return total;
      };

      if (observedDependencies &&
          failed(recordLiveDependencies(*operand, semanticType.getValue()))) {
        emitError(getSemanticLocation(children.front()))
            << "$bits recursive dependency inventory is not executable";
        return failure();
      }
      FailureOr<Value> result =
          lowerLiveWidth(*operand, semanticType.getValue(), false);
      if (failed(result)) {
        emitError(getSemanticLocation(children.front()))
            << "$bits of this dynamically sized bitstream is not yet "
               "executable";
        return failure();
      }
      return convertResult(*result);
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

    auto spelling = op.getTypenameSpellingAttr();
    if (!spelling) {
      emitError(getSemanticLocation(children.front()))
          << "$typename has no frozen elaborated spelling";
      return failure();
    }

    // `$typename` is an inquiry function; only its elaborated operand type is
    // observed. Materialize the spelling directly as a simulation string.
    Type resultType = sim::StringType::get(function.getContext());
    Value result = sim::SimStringLiteralOp::create(builder, location,
                                                   resultType, spelling);
    return convertResult(result);
  }

  if (name == "name") {
    if (children.size() != 1) {
      emitError(location) << "enum name() requires exactly one receiver";
      return failure();
    }
    auto semanticType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    ArrayAttr values = op->getAttrOfType<ArrayAttr>(enumMethodValuesAttrName);
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
      FailureOr<Value> equal =
          conditionalEqual(*receiver, *member, (*receiver).getType(), location,
                           /*caseEquality=*/true);
      if (failed(equal))
        return failure();
      Value candidate = sim::SimStringLiteralOp::create(builder, location,
                                                        stringType, spelling);
      result =
          arith::SelectOp::create(builder, location, *equal, candidate, result);
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

  // The whole of IEEE 1800-2017 Table 20-4.
  bool realMath =
      llvm::StringSwitch<bool>(name)
          .Cases({"$ceil",  "$floor", "$sqrt",  "$exp",  "$ln",   "$log10",
                  "$pow",   "$atan2", "$hypot", "$sin",  "$cos",  "$tan",
                  "$asin",  "$acos",  "$atan",  "$sinh", "$cosh", "$tanh",
                  "$asinh", "$acosh", "$atanh"},
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
    if (name == "$realtime") {
      Value now = sim::SimTimeNowOp::create(builder, location, i64, context);
      Value real = sim::SimTimeToRealOp::create(
          builder, location, builder.getF64Type(), now, scaleAttr);
      return convertResult(real);
    }
    FailureOr<Value> rounded = currentTimeInUnits(location);
    if (failed(rounded))
      return failure();
    if (name == "$stime")
      rounded =
          arith::TruncIOp::create(builder, location, i32, *rounded).getResult();
    return convertResult(*rounded);
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

  if (name == "$exit") {
    if (!children.empty()) {
      emitError(location) << "$exit accepts no arguments";
      return failure();
    }
    sim::SimProgramExitOp::create(builder, location, context);
    if (failed(emitFunctionReturn(location, std::nullopt, false,
                                  /*emitBlockEventEnd=*/false)))
      return failure();
    setCurrent(addBlock());
    return dummyTaskResult();
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
    if (failed(emitTerminationDiagnostic(name, verbosity, location)))
      return failure();
    if (name == "$finish")
      sim::SimFinishOp::create(builder, location, context, verbosity);
    else
      sim::SimStopOp::create(builder, location, context, verbosity);
    if (failed(emitFunctionReturn(location, std::nullopt, false,
                                  /*emitBlockEventEnd=*/false)))
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
                  "$warning",   "$error",      "$fatal",          "$swrite",
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
                  "$timeformat", "$readmemb", "$readmemh", "$writememb",
                  "$writememh", "$system"},
                 true)
          .Default(false);
  if (fileCall)
    return lowerFileSystemCall(op);

  bool dumpCall =
      llvm::StringSwitch<bool>(name)
          .Cases({"$dumpfile", "$dumpvars", "$dumpoff", "$dumpon", "$dumpall",
                  "$dumpflush", "$dumplimit", "$dumpports", "$dumpportsoff",
                  "$dumpportson", "$dumpportsall", "$dumpportsflush",
                  "$dumpportslimit"},
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
    ensureCoverageInventory();
    // IEEE 1800-2017 40.3.2 distinguishes string module-definition targets
    // from elaborated instance-path targets. Keep that distinction explicit
    // in Simulation IR so neither the backends nor the runtime have to infer
    // it from sentinel values.
    struct CoverageTarget {
      Value definition;
      uint64_t instance = 0;

      bool isDefinition() const { return static_cast<bool>(definition); }
    };
    auto lowerCoverageTarget =
        [&](Operation *target) -> FailureOr<CoverageTarget> {
      if (auto symbol =
              dyn_cast<semantic::SVArbitrarySymbolExpressionOp>(target)) {
        std::optional<StringRef> path = symbol.getReferencedPath();
        auto found = path ? coverageInstanceIDs.find(*path)
                          : coverageInstanceIDs.end();
        if (found == coverageInstanceIDs.end()) {
          emitError(getSemanticLocation(target))
              << "coverage instance target has no elaborated module scope";
          return failure();
        }
        return CoverageTarget{Value{}, found->second};
      }
      FailureOr<Value> lowered = lowerExpression(target);
      if (failed(lowered) || !isa<sim::StringType>((*lowered).getType())) {
        emitError(getSemanticLocation(target))
            << "coverage scope must be a module instance or definition name";
        return failure();
      }
      return CoverageTarget{*lowered, 0};
    };

    if (name == "$coverage_control") {
      if (children.size() != 4) {
        emitError(location) << "$coverage_control requires four arguments";
        return failure();
      }
      FailureOr<Value> control = lowerInteger(children[0], i32);
      FailureOr<Value> coverageType = lowerInteger(children[1], i32);
      FailureOr<Value> scope = lowerInteger(children[2], i32);
      FailureOr<CoverageTarget> target = lowerCoverageTarget(children[3]);
      if (failed(control) || failed(coverageType) || failed(scope) ||
          failed(target))
        return failure();
      Value result =
          target->isDefinition()
              ? sim::SimCoverageControlDefinitionOp::create(
                    builder, location, i32, context, *control, *coverageType,
                    *scope, target->definition)
                    .getStatus()
              : sim::SimCoverageControlInstanceOp::create(
                    builder, location, i32, context, *control, *coverageType,
                    *scope, builder.getI64IntegerAttr(target->instance))
                    .getStatus();
      return convertResult(result);
    }

    if (name == "$coverage_get_max" || name == "$coverage_get") {
      if (children.size() != 3) {
        emitError(location) << name << " requires three arguments";
        return failure();
      }
      FailureOr<Value> coverageType = lowerInteger(children[0], i32);
      FailureOr<Value> scope = lowerInteger(children[1], i32);
      FailureOr<CoverageTarget> target = lowerCoverageTarget(children[2]);
      if (failed(coverageType) || failed(scope) || failed(target))
        return failure();
      BoolAttr maximum = builder.getBoolAttr(name == "$coverage_get_max");
      Value result =
          target->isDefinition()
              ? sim::SimCoverageQueryDefinitionOp::create(
                    builder, location, i32, context, *coverageType, *scope,
                    target->definition, maximum)
                    .getValue()
              : sim::SimCoverageQueryInstanceOp::create(
                    builder, location, i32, context, *coverageType, *scope,
                    builder.getI64IntegerAttr(target->instance), maximum)
                    .getValue();
      return convertResult(result);
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
      Value result =
          name == "$coverage_merge"
              ? sim::SimCoverageMergeOp::create(builder, location, i32,
                                                context, *coverageType,
                                                *databaseName)
                    .getStatus()
              : sim::SimCoverageSaveOp::create(builder, location, i32,
                                               context, *coverageType,
                                               *databaseName)
                    .getStatus();
      return convertResult(result);
    }

    if (name == "$get_coverage") {
      if (!children.empty()) {
        emitError(location) << "$get_coverage takes no arguments";
        return failure();
      }
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      auto floatType = succeeded(resultType) ? dyn_cast<FloatType>(*resultType)
                                             : FloatType{};
      if (!floatType) {
        emitError(location) << "$get_coverage has a non-real result type";
        return failure();
      }
      Value result = sim::SimFunctionalCoverageGetOp::create(
          builder, location, builder.getF64Type(), context);
      return convertResult(result);
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
    if (name == "$set_coverage_db_name")
      sim::SimFunctionalCoverageSetDbNameOp::create(builder, location, context,
                                                    *databaseName);
    else
      sim::SimFunctionalCoverageLoadDbOp::create(builder, location, context,
                                                 *databaseName);
    return dummyTaskResult();
  }

  unsupported(op) << " (unsupported system call " << name << ")";
  return failure();
}

} // namespace obelisk::simlowering
