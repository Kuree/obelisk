//===- LowerUnitTiming.cpp - Lower timing and event controls -----------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"

#include "llvm/ADT/SetVector.h"

#include <cmath>
#include <functional>
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<Value> UnitLowering::lowerDelayValue(Operation *control) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);
  if (!isa<semantic::SVDelayControlOp>(control) || children.size() != 1) {
    unsupported(control) << " (delay inventory)";
    return failure();
  }
  auto scaleAttr = function->getAttrOfType<IntegerAttr>(delayScaleAttrName);
  if (!scaleAttr) {
    function.emitError("code unit has no frozen delay scale");
    return failure();
  }

  Operation *realLiteral = children.front();
  bool negateRealLiteral = false;
  if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(realLiteral)) {
    SmallVector<Operation *> unaryChildren = getChildren(unary);
    if (unaryChildren.size() == 1 &&
        (unary.getOperatorKind() == semantic::SVUnaryOperator::Plus ||
         unary.getOperatorKind() == semantic::SVUnaryOperator::Minus) &&
        isa<semantic::SVRealLiteralOp, semantic::SVTimeLiteralOp>(
            unaryChildren.front())) {
      negateRealLiteral =
          unary.getOperatorKind() == semantic::SVUnaryOperator::Minus;
      realLiteral = unaryChildren.front();
    }
  }
  auto realConstantSpelling = [&]() -> std::optional<StringRef> {
    if (auto spelling =
            realLiteral->getAttrOfType<StringAttr>("constant_value");
        isa<semantic::SVRealLiteralOp, semantic::SVTimeLiteralOp>(realLiteral))
      return spelling ? std::optional<StringRef>(spelling.getValue())
                      : std::nullopt;
    auto type = children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!type || !isa<semantic::RealType, semantic::ShortRealType,
                      semantic::RealtimeType>(type.getValue()))
      return std::nullopt;
    return getConstantSpelling(children.front());
  }();
  if (realConstantSpelling) {
    auto quantumAttr =
        function->getAttrOfType<IntegerAttr>(delayQuantumAttrName);
    if (!quantumAttr) {
      function.emitError("code unit has incomplete real-delay metadata");
      return failure();
    }
    double amount = 0;
    if (realConstantSpelling->getAsDouble(amount) || !std::isfinite(amount)) {
      emitError(location) << "real delay literal is not finite";
      return failure();
    }
    if (negateRealLiteral)
      amount = -amount;
    if (amount < 0)
      amount = 0;
    uint64_t scale = scaleAttr.getValue().getZExtValue();
    uint64_t quantum = quantumAttr.getValue().getZExtValue();
    if (scale == 0 || quantum == 0 || scale % quantum != 0) {
      function.emitError("code unit has invalid real-delay scaling metadata");
      return failure();
    }
    // Slang has already expressed a time literal in the lexical timeunit.
    // Round the real value to the lexical timeprecision before converting to
    // the design-wide precision, matching TimeScale::apply's std::round rule.
    double precisionSteps = amount * static_cast<double>(scale / quantum);
    double roundedSteps = std::round(precisionSteps);
    long double ticks = static_cast<long double>(roundedSteps) * quantum;
    if (!std::isfinite(roundedSteps) || ticks < 0 ||
        ticks > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
      emitError(location)
          << "scaled real delay exceeds the simulation time range";
      return failure();
    }
    return sim::SimTimeConstantOp::create(
               builder, location, sim::TimeType::get(function.getContext()),
               builder.getI64IntegerAttr(static_cast<uint64_t>(ticks)))
        .getResult();
  }

  if (getConstantSpelling(children.front())) {
    FailureOr<ParsedConstant> parsed =
        parseSVInteger(*getConstantSpelling(children.front()), 64, location);
    if (failed(parsed))
      return failure();
    // An X/Z or negative delay is treated as zero. This normalization happens
    // before scaling so native and bytecode tiers see the same time value.
    bool zero = !parsed->unknown.isZero() ||
                (isSignedNode(children.front()) && parsed->value.isNegative());
    APInt amount(128, zero ? 0 : parsed->value.getZExtValue());
    APInt scaled = amount * APInt(128, scaleAttr.getValue().getZExtValue());
    if (scaled.ugt(APInt(
            128, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())))) {
      emitError(location) << "scaled delay exceeds the simulation time range";
      return failure();
    }
    return sim::SimTimeConstantOp::create(
               builder, location, sim::TimeType::get(function.getContext()),
               builder.getI64IntegerAttr(scaled.getZExtValue()))
        .getResult();
  }

  FailureOr<Value> amount = lowerExpression(children.front());
  if (failed(amount))
    return failure();
  if (isa<FloatType>((*amount).getType())) {
    auto quantumAttr =
        function->getAttrOfType<IntegerAttr>(delayQuantumAttrName);
    if (!quantumAttr) {
      function.emitError("code unit has no frozen delay quantum");
      return failure();
    }
    FailureOr<Value> real =
        convert(*amount, builder.getF64Type(), false, location);
    if (failed(real))
      return failure();
    return sim::SimTimeFromRealOp::create(
               builder, location, sim::TimeType::get(function.getContext()),
               *real, scaleAttr, quantumAttr)
        .getResult();
  }
  FailureOr<Value> scalar = toPackedScalar(*amount, location);
  if (failed(scalar))
    return failure();
  Value normalized = *scalar;
  if (auto logic = dyn_cast<sim::LogicType>(normalized.getType())) {
    Type bitsType = IntegerType::get(function.getContext(), logic.getWidth());
    Value bits =
        sim::SimLogicToBitsOp::create(builder, location, bitsType, normalized);
    Value roundTrip =
        sim::SimLogicFromBitsOp::create(builder, location, logic, bits);
    Value known = sim::SimLogicCompareOp::create(
        builder, location, builder.getI1Type(), sim::CompareKind::CaseEq,
        normalized, roundTrip);
    Value zero = arith::ConstantOp::create(builder, location, bitsType,
                                           builder.getIntegerAttr(bitsType, 0));
    normalized = arith::SelectOp::create(builder, location, known, bits, zero);
  }
  auto integer = dyn_cast<IntegerType>(normalized.getType());
  if (!integer || !integer.isSignless()) {
    emitError(location) << "dynamic delay is not an integral packed value";
    return failure();
  }
  if (isSignedNode(children.front())) {
    Value zero = arith::ConstantOp::create(builder, location, integer,
                                           builder.getIntegerAttr(integer, 0));
    Value nonnegative = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::sge, normalized, zero);
    normalized = arith::SelectOp::create(builder, location, nonnegative,
                                         normalized, zero);
  }
  uint64_t scale = scaleAttr.getValue().getZExtValue();
  uint64_t maximumInput =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / scale;
  if (integer.getWidth() > 64) {
    Value maximumWide = arith::ConstantOp::create(
        builder, location, integer,
        builder.getIntegerAttr(integer,
                               APInt(integer.getWidth(), maximumInput)));
    Value inRange = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ule, normalized, maximumWide);
    normalized = arith::SelectOp::create(builder, location, inRange, normalized,
                                         maximumWide);
  }
  FailureOr<Value> normalized64 =
      convert(normalized, builder.getI64Type(), false, location);
  if (failed(normalized64))
    return failure();

  // Keep the multiplication in the supported nonnegative signed-time range
  // on every backend. The source language's X/Z and negative rules have
  // already mapped those values to zero above.
  Value maximum =
      arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                builder.getI64IntegerAttr(maximumInput));
  Value inRange = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ule, *normalized64, maximum);
  Value checked = arith::SelectOp::create(builder, location, inRange,
                                          *normalized64, maximum);
  return sim::SimTimeScaleOp::create(builder, location,
                                     sim::TimeType::get(function.getContext()),
                                     checked, scaleAttr,
                                     /*is_signed=*/builder.getBoolAttr(false))
      .getResult();
}

bool UnitLowering::hasWatchableSignalHandle(Operation *expression) {
  // Walk the addressable select chain down to the declaration it starts from,
  // noting whether any step selects out of an unpacked aggregate.
  bool unpackedStep = false;
  Operation *node = expression;
  while (isa<semantic::SVElementSelectExpressionOp,
             semantic::SVRangeSelectExpressionOp,
             semantic::SVMemberAccessExpressionOp>(node)) {
    SmallVector<Operation *> children = getChildren(node);
    if (children.empty())
      return true;
    FailureOr<Type> sourceType = getNormalizedSemanticType(children.front());
    if (failed(sourceType))
      return true;
    if (!sim::getPackedWidth(*sourceType))
      unpackedStep = true;
    node = children.front();
  }
  if (!unpackedStep)
    return true;
  // A net is viewed only through the packed windows of its storage, which is
  // all a step out of a packed aggregate needs. A step out of an unpacked one
  // has no such view, so its event has to be observed instead; storage
  // references keep subelement and array-element views for every aggregate and
  // stay directly watchable.
  auto path = node->getAttrOfType<StringAttr>("referenced_path");
  if (!path)
    return true;
  Value declaration = values.lookup(path.getValue());
  if (!declaration)
    declaration = lvalues.lookup(path.getValue());
  return !declaration || !isa<sim::NetType>(declaration.getType());
}

LogicalResult UnitLowering::emitEventSuspend(Operation *control,
                                             Block *continuation,
                                             ValueRange continuationOperands) {
  Location location = getSemanticLocation(control);
  auto emitDirect = [&](Value watched, sim::EdgeKind edge, Block *successor,
                        ValueRange operands, sim::EventRegionAttr resume = {}) {
    if (isa<sim::EventType>(watched.getType()))
      sim::SimSuspendEventOp::create(builder, location, watched, operands,
                                     sim::ContinuationSiteAttr{}, resume,
                                     successor);
    else if (edge == sim::EdgeKind::Change) {
      auto suspend = sim::SimSuspendChangeOp::create(
          builder, location, watched, operands, sim::ContinuationSiteAttr{},
          resume, successor);
      suspend->setAttr(sim::metadata::proceduralEventWait,
                       builder.getUnitAttr());
    } else {
      auto suspend = sim::SimSuspendEdgeOp::create(
          builder, location, edge, watched, operands,
          sim::ContinuationSiteAttr{}, resume, successor);
      suspend->setAttr(sim::metadata::proceduralEventWait,
                       builder.getUnitAttr());
    }
  };
  auto bindEventPrimary = [&](Value event,
                              Operation *source) -> FailureOr<Value> {
    if (!isa<sim::EventType>(event.getType()))
      return emitError(location)
                 << "clocking event monitor did not select a named event",
             failure();
    auto nodeAttr = source->getAttrOfType<IntegerAttr>("node_id");
    if (!nodeAttr)
      return emitError(location)
                 << "clocking event control is missing its node identity",
             failure();
    uint64_t node = nodeAttr.getValue().getZExtValue();
    std::string identity =
        (function.getSymName() + ".$clocking_event_primary." + Twine(node))
            .str();
    uint64_t codeUnitID = stableCodeUnitID(identity);
    uint64_t scopeID = 0;
    if (auto parentID = function.getCodeUnitId())
      for (sim::SimCodeUnitDeclOp declaration :
           function->getParentOfType<sim::SimDesignOp>()
               .getBody()
               .front()
               .getOps<sim::SimCodeUnitDeclOp>())
        if (declaration.getId() == *parentID) {
          scopeID = declaration.getScopeId();
          break;
        }

    OpBuilder outlineBuilder(function);
    outlineBuilder.setInsertionPoint(function);
    sim::SimCodeUnitDeclOp::create(
        outlineBuilder, location, codeUnitID, scopeID, sim::EntryKind::Observer,
        outlineBuilder.getStringAttr(identity),
        outlineBuilder.getStringAttr("clocking event primary"),
        outlineBuilder.getUnitAttr());
    MLIRContext *context = function.getContext();
    SmallVector<DictionaryAttr> argumentAttrs{
        captureMetadata(outlineBuilder, sim::CaptureKind::Context),
        captureMetadata(outlineBuilder, sim::CaptureKind::Formal)};
    SmallVector<NamedAttribute> attributes{
        outlineBuilder.getNamedAttr(
            "code_unit_id", outlineBuilder.getI64IntegerAttr(codeUnitID)),
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr("home_region",
                                    function.getHomeRegionAttr()),
        outlineBuilder.getNamedAttr("domain", function.getDomainAttr()),
        outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                    outlineBuilder.getStringAttr(identity)),
        outlineBuilder.getNamedAttr(
            observerResultAttrName,
            outlineBuilder.getI32IntegerAttr(
                static_cast<uint32_t>(ObserverResult::Event))),
        outlineBuilder.getNamedAttr("obelisk_sim.observer_width",
                                    outlineBuilder.getI32IntegerAttr(1)),
        outlineBuilder.getNamedAttr("obelisk_sim.observer_four_state",
                                    outlineBuilder.getBoolAttr(false))};
    sim::SimFuncOp evaluator = sim::SimFuncOp::create(
        outlineBuilder, location, identity,
        FunctionType::get(context,
                          TypeRange{sim::ContextType::get(context),
                                    sim::EventType::get(context)},
                          TypeRange{outlineBuilder.getI1Type()}),
        sim::EntryKind::Observer, attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(evaluator,
                                     SymbolTable::Visibility::Private);
    OpBuilder evaluatorBuilder =
        OpBuilder::atBlockEnd(&evaluator.getBody().front());
    Value triggered = sim::SimEventTriggeredOp::create(
        evaluatorBuilder, location, evaluatorBuilder.getI1Type(),
        evaluator.getBody().front().getArgument(1));
    sim::SimReturnOp::create(evaluatorBuilder, location, ValueRange{triggered});
    evaluator->setAttr(sim::metadata::lowered, outlineBuilder.getUnitAttr());

    auto binding = sim::SimObserverBindOp::create(
        builder, location, sim::ObserverType::get(context, builder.getI1Type()),
        evaluator.getSymName(), ValueRange{event, event}, uint32_t{1});
    binding->setAttr(observerEventPrimaryAttrName, builder.getUnitAttr());
    return binding.getResult();
  };
  auto evaluateInitial =
      [&](Operation *expression,
          SmallVectorImpl<Value> &dynamicDependencies) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(expression);
    if (failed(value))
      return failure();
    if (isa<sim::EventType>((*value).getType())) {
      dynamicDependencies.push_back(*value);
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    return toPackedScalar(*value, getSemanticLocation(expression));
  };
  auto emitObserved =
      [&](ArrayRef<semantic::SVSignalEventControlOp> events) -> LogicalResult {
    SmallVector<Value> primaries;
    SmallVector<Value> initials;
    SmallVector<Value> conditions;
    SmallVector<int32_t> edges;
    SmallVector<int32_t> conditionIndices;
    for (semantic::SVSignalEventControlOp event : events) {
      SmallVector<Operation *> children = getChildren(event);
      size_t expected = event.getHasIff() ? 2 : 1;
      if (children.size() != expected) {
        unsupported(event) << " (event expression inventory)";
        return failure();
      }
      SmallVector<Value> dynamicDependencies;
      FailureOr<Value> initial =
          evaluateInitial(children.front(), dynamicDependencies);
      FailureOr<Type> primaryType = getNormalizedSemanticType(children.front());
      FailureOr<Value> primary =
          succeeded(primaryType) && isa<sim::EventType>(*primaryType) &&
                  dynamicDependencies.size() == 1
              ? bindEventPrimary(dynamicDependencies.front(), event)
              : bindObserver(children.front(), dynamicDependencies);
      if (failed(initial) || failed(primary))
        return failure();
      primaries.push_back(*primary);
      initials.push_back(*initial);
      auto edge = static_cast<int32_t>(event.getEdgeKind());
      if (succeeded(primaryType) && isa<sim::EventType>(*primaryType))
        edge = static_cast<int32_t>(sim::EdgeKind::Change);
      edges.push_back(edge);
      if (!event.getHasIff()) {
        conditionIndices.push_back(-1);
        continue;
      }
      FailureOr<Value> condition = bindObserver(children[1]);
      if (failed(condition))
        return failure();
      conditionIndices.push_back(static_cast<int32_t>(conditions.size()));
      conditions.push_back(*condition);
    }
    SmallVector<Value> values(primaries);
    llvm::append_range(values, initials);
    llvm::append_range(values, conditions);
    llvm::append_range(values, continuationOperands);
    sim::SimSuspendObserveOp::create(
        builder, location, values, static_cast<uint32_t>(conditions.size()),
        edges, conditionIndices, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr{}, continuation);
    return success();
  };

  if (auto event = dyn_cast<semantic::SVSignalEventControlOp>(control)) {
    SmallVector<Operation *> children = getChildren(event);
    size_t expected = event.getHasIff() ? 2 : 1;
    if (children.size() != expected) {
      unsupported(event) << " (event expression inventory)";
      return failure();
    }
    if (auto instance = dyn_cast<semantic::SVAssertionInstanceExpressionOp>(
            children.front())) {
      auto type = instance->getAttrOfType<TypeAttr>("semantic_type");
      if (type && isa<semantic::SequenceType>(type.getValue())) {
        if (event.getHasIff()) {
          emitError(location) << "sequence event controls cannot use iff";
          return failure();
        }
        Value endpoint = values.lookup(instance.getReferencedPath());
        if (!endpoint || !isa<sim::EventType>(endpoint.getType())) {
          emitError(location)
              << "sequence event control has no prepared endpoint event for '"
              << instance.getReferencedPath() << "' in "
              << function.getSymName();
          return failure();
        }
        emitDirect(endpoint, sim::EdgeKind::Change, continuation,
                   continuationOperands,
                   sim::EventRegionAttr::get(function.getContext(),
                                             sim::EventRegion::Reactive));
        return success();
      }
    }
    bool virtualClockingBlockEvent =
        children.front()->hasAttr("virtual_interface_clocking_block_event");
    bool staticClockingBlockEvent =
        children.front()->hasAttr(clockingBlockEventAttrName);
    bool clockingBlockEvent =
        virtualClockingBlockEvent || staticClockingBlockEvent;
    bool virtualClockingIff =
        children.front()->hasAttr("virtual_interface_clock_event_has_iff");
    bool staticClockingIff =
        children.front()->hasAttr(clockingEventHasIffAttrName);
    bool monitoredClockingEvent =
        children.front()->hasAttr(clockingEventMonitorRequiredAttrName) ||
        children.front()->hasAttr(clockingEventListAttrName) ||
        children.front()->hasAttr("virtual_interface_clock_event_monitor") ||
        children.front()->hasAttr("virtual_interface_clock_event_list");
    if (clockingBlockEvent && monitoredClockingEvent && event.getHasIff()) {
      FailureOr<Value> handle = failure();
      Value virtualInterface;
      auto virtualAccess =
          dyn_cast<semantic::SVMemberAccessExpressionOp>(children.front());
      if (virtualClockingBlockEvent) {
        SmallVector<Operation *> clockingChildren =
            virtualAccess ? getChildren(virtualAccess)
                          : SmallVector<Operation *>{};
        if (!virtualAccess || clockingChildren.size() != 1) {
          emitError(location)
              << "monitored virtual clocking event has no receiver";
          return failure();
        }
        FailureOr<Value> receiver = lowerExpression(clockingChildren.front());
        if (failed(receiver))
          return failure();
        virtualInterface = *receiver;
        handle = lowerVirtualInterfaceClock(virtualAccess, virtualInterface);
      } else {
        handle = lowerExpression(children.front());
      }
      if (failed(handle) || !isa<sim::EventType>((*handle).getType())) {
        if (succeeded(handle))
          emitError(location)
              << "monitored clocking event did not resolve to an event";
        return failure();
      }
      FailureOr<Value> primary = bindEventPrimary(*handle, event);
      FailureOr<Value> condition =
          virtualClockingBlockEvent
              ? bindVirtualClockingObserver(children[1], virtualAccess,
                                            virtualInterface, *handle)
              : bindObserver(children[1]);
      if (failed(primary) || failed(condition))
        return failure();
      Value initial = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(false));
      SmallVector<Value> observerValues{*primary, initial, *condition};
      llvm::append_range(observerValues, continuationOperands);
      sim::SimSuspendObserveOp::create(
          builder, location, observerValues, 1,
          ArrayRef<int32_t>{static_cast<int32_t>(sim::EdgeKind::Change)},
          ArrayRef<int32_t>{0}, sim::ContinuationSiteAttr{},
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Reactive),
          continuation);
      clockingEventContinuations[continuation] = {*handle, {}};
      return success();
    }
    if (clockingBlockEvent && (virtualClockingIff || staticClockingIff) &&
        event.getHasIff()) {
      emitError(location)
          << "legacy clocking-block events with a declared iff cannot be "
             "combined with an additional event-control iff";
      return failure();
    }
    FailureOr<Type> watchedType =
        clockingBlockEvent
            ? FailureOr<Type>(sim::LogicType::get(function.getContext(), 1))
            : getNormalizedSemanticType(children.front());
    if (failed(watchedType))
      return failure();
    // IEEE 1800-2017 9.4.2 lets an event expression select an aggregate
    // element as long as the expression reduces to a singular value. One that
    // has no watchable handle is re-evaluated by an observer instead, which
    // reports a change in exactly that value.
    bool sampledClockingVariable =
        children.front()->hasAttr(clockingVariableAttrName) ||
        children.front()->hasAttr("virtual_interface_clocking");
    bool computed =
        !clockingBlockEvent &&
        (sampledClockingVariable ||
         !isAddressableExpression(children.front()) ||
         !hasWatchableSignalHandle(children.front()) ||
         (event.getHasIff() && (!isAddressableExpression(children[1]) ||
                                !hasWatchableSignalHandle(children[1]) ||
                                isa<sim::EventType>(*watchedType))));
    if (computed)
      return emitObserved(ArrayRef<semantic::SVSignalEventControlOp>(event));
    FailureOr<Value> handle = failure();
    Value virtualInterface;
    auto virtualClockingAccess =
        dyn_cast<semantic::SVMemberAccessExpressionOp>(children.front());
    if (virtualClockingBlockEvent && virtualClockingAccess) {
      SmallVector<Operation *> clockingChildren =
          getChildren(virtualClockingAccess);
      size_t expectedClockingChildren = virtualClockingIff ? 3 : 1;
      if (clockingChildren.size() != expectedClockingChildren) {
        emitError(location)
            << "virtual clocking-block event has no frozen receiver and event "
               "expressions";
        return failure();
      }
      FailureOr<Value> receiver = lowerExpression(clockingChildren.front());
      if (failed(receiver))
        return failure();
      virtualInterface = *receiver;
      handle =
          lowerVirtualInterfaceClock(virtualClockingAccess, virtualInterface);
    } else {
      handle =
          lowerExpression(children.front(), !isa<sim::EventType>(*watchedType));
    }
    if (failed(handle))
      return failure();
    // IEEE 1800-2017 9.4.2 detects an edge on the value of the expression the
    // event control names, and 6.6 makes a net's value the resolution of every
    // driver on it. A process that also drives this net -- through a clocking
    // block output, a continuous assignment, or a force -- resolves the name to
    // the driver handle it writes, which carries only its own contribution and
    // no resolved value to compare. Take the net view of the same signal for
    // the wait; the driver stays what the writes go through.
    if (isa<sim::DriverType>((*handle).getType()))
      if (auto path =
              children.front()->getAttrOfType<StringAttr>("referenced_path"))
        if (Value net = values.lookup(path.getValue());
            net && isa<sim::NetType>(net.getType()))
          handle = net;
    auto edge = static_cast<sim::EdgeKind>(event.getEdgeKind());
    if (auto clockingEdge =
            children.front()->getAttrOfType<semantic::EdgeKindAttr>(
                "virtual_interface_clock_event_edge"))
      edge = static_cast<sim::EdgeKind>(clockingEdge.getValue());
    else if (auto clockingEdge =
                 children.front()->getAttrOfType<semantic::EdgeKindAttr>(
                     clockingEventEdgeAttrName))
      edge = static_cast<sim::EdgeKind>(clockingEdge.getValue());
    if (virtualClockingBlockEvent && virtualClockingIff) {
      SmallVector<Operation *> clockingChildren =
          getChildren(virtualClockingAccess);
      FailureOr<Value> initial = loadReference(*handle, location);
      FailureOr<Value> primary = bindVirtualClockingObserver(
          clockingChildren[1], virtualClockingAccess, virtualInterface,
          *handle);
      FailureOr<Value> condition = bindVirtualClockingObserver(
          clockingChildren[2], virtualClockingAccess, virtualInterface,
          *handle);
      if (failed(initial) || failed(primary) || failed(condition))
        return failure();
      FailureOr<Value> scalar = toPackedScalar(*initial, location);
      if (failed(scalar))
        return failure();
      SmallVector<Value> observerValues{*primary, *scalar, *condition};
      llvm::append_range(observerValues, continuationOperands);
      sim::SimSuspendObserveOp::create(
          builder, location, observerValues, 1,
          ArrayRef<int32_t>{static_cast<int32_t>(edge)}, ArrayRef<int32_t>{0},
          sim::ContinuationSiteAttr{},
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Reactive),
          continuation);
      clockingEventContinuations[continuation] = {*handle, {}};
      return success();
    }
    if (staticClockingBlockEvent && staticClockingIff) {
      SmallVector<Operation *> clockingChildren = getChildren(children.front());
      if (clockingChildren.size() != 2) {
        emitError(location)
            << "clocking-block event with iff has no frozen clock and "
               "condition expressions";
        return failure();
      }
      SmallVector<Value> dynamicDependencies;
      FailureOr<Value> initial =
          evaluateInitial(clockingChildren[0], dynamicDependencies);
      FailureOr<Value> primary =
          bindObserver(clockingChildren[0], dynamicDependencies);
      FailureOr<Value> condition = bindObserver(clockingChildren[1]);
      if (failed(initial) || failed(primary) || failed(condition))
        return failure();
      SmallVector<Value> observerValues{*primary, *initial, *condition};
      llvm::append_range(observerValues, continuationOperands);
      sim::SimSuspendObserveOp::create(
          builder, location, observerValues, 1,
          ArrayRef<int32_t>{static_cast<int32_t>(edge)}, ArrayRef<int32_t>{0},
          sim::ContinuationSiteAttr{},
          sim::EventRegionAttr::get(function.getContext(),
                                    sim::EventRegion::Reactive),
          continuation);
      clockingEventContinuations[continuation] = {*handle, {}};
      return success();
    }
    if (!event.getHasIff() && isa<sim::ManagedRefType>((*handle).getType())) {
      // IEEE 1800-2017 9.4.2 permits event controls on object members. A
      // managed reference cannot survive a suspension as an interior pointer,
      // so bind the already outlined value observer to the field's stable
      // mutation token. The observer compares the post-write value with this
      // initial value and therefore ignores equal-value writes as required.
      FailureOr<Value> initial = loadReference(*handle, location);
      if (failed(initial))
        return failure();
      FailureOr<Value> scalar =
          isa<sim::ClassHandleType>((*initial).getType())
              ? FailureOr<Value>(sim::SimClassIdOp::create(builder, location,
                                                           builder.getI64Type(),
                                                           *initial)
                                     .getResult())
              : toPackedScalar(*initial, getSemanticLocation(children.front()));
      if (failed(scalar))
        return failure();
      Value watch = sim::SimManagedWatchOp::create(
          builder, location, sim::ManagedWatchType::get(function.getContext()),
          *handle, sim::ManagedWatchKind::Field);
      FailureOr<Value> observer = bindObserver(children.front(), watch);
      if (failed(observer))
        return failure();
      SmallVector<Value> values{*observer, *scalar};
      llvm::append_range(values, continuationOperands);
      sim::SimSuspendObserveOp::create(
          builder, location, values, 0,
          ArrayRef<int32_t>{static_cast<int32_t>(edge)}, ArrayRef<int32_t>{-1},
          sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
      return success();
    }
    if (!event.getHasIff()) {
      sim::EventRegionAttr resume =
          clockingBlockEvent
              ? sim::EventRegionAttr::get(function.getContext(),
                                          sim::EventRegion::Reactive)
              : sim::EventRegionAttr{};
      emitDirect(*handle, edge, continuation, continuationOperands, resume);
      if (clockingBlockEvent)
        clockingEventContinuations[continuation] = {*handle, {}};
      else
        // IEEE 1800-2017 14.16 makes coincidence with the clocking event, not
        // the syntax used to wait for it, what decides when a synchronous
        // drive matures. Record the edge so a clocking block on this very
        // signal and edge recognizes its own occurrence here.
        clockingEventContinuations[continuation] = {*handle, {}, edge, false};
      return success();
    }

    FailureOr<Value> condition = lowerExpression(children[1], true);
    if (failed(condition))
      return failure();
    if (isa<sim::ManagedRefType>((*handle).getType()) ||
        isa<sim::ManagedRefType>((*condition).getType())) {
      // Object fields are addressed by activation-safe managed references,
      // which cannot be retained as the interior pointers required by the
      // direct edge-iff wait.  Observe each field through its stable mutation
      // token instead.  The suspension still resumes only when the primary
      // expression has the requested edge and the iff observer is true;
      // changing the condition alone merely updates what the next edge sees.
      FailureOr<Value> initial = loadReference(*handle, location);
      if (failed(initial))
        return failure();
      FailureOr<Value> scalar =
          toPackedScalar(*initial, getSemanticLocation(children.front()));
      if (failed(scalar))
        return failure();
      auto bindManaged = [&](Operation *expression,
                             Value reference) -> FailureOr<Value> {
        if (!isa<sim::ManagedRefType>(reference.getType()))
          return bindObserver(expression);
        Value watch = sim::SimManagedWatchOp::create(
            builder, getSemanticLocation(expression),
            sim::ManagedWatchType::get(function.getContext()), reference,
            sim::ManagedWatchKind::Field);
        return bindObserver(expression, watch);
      };
      FailureOr<Value> primary = bindManaged(children.front(), *handle);
      FailureOr<Value> guard = bindManaged(children[1], *condition);
      if (failed(primary) || failed(guard))
        return failure();
      SmallVector<Value> observerValues{*primary, *scalar, *guard};
      llvm::append_range(observerValues, continuationOperands);
      sim::SimSuspendObserveOp::create(
          builder, location, observerValues, 1,
          ArrayRef<int32_t>{static_cast<int32_t>(edge)}, ArrayRef<int32_t>{0},
          sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
      return success();
    }
    if (!isa<sim::RefType, sim::NetType>((*handle).getType()) ||
        !isa<sim::RefType, sim::NetType>((*condition).getType())) {
      unsupported(event) << " (iff requires signal handles)";
      return failure();
    }
    auto suspend = sim::SimSuspendEdgeIffOp::create(
        builder, location, edge, *handle, *condition, continuationOperands,
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
    suspend->setAttr(sim::metadata::proceduralEventWait,
                     builder.getUnitAttr());
    return success();
  }

  auto list = dyn_cast<semantic::SVEventListControlOp>(control);
  if (!list) {
    unsupported(control) << " (event timing control)";
    return failure();
  }
  SmallVector<semantic::SVSignalEventControlOp> events;
  bool computed = false;
  for (Operation *eventOp : getChildren(list)) {
    auto event = dyn_cast<semantic::SVSignalEventControlOp>(eventOp);
    if (!event) {
      unsupported(eventOp) << " (event-list member)";
      return failure();
    }
    SmallVector<Operation *> eventChildren = getChildren(event);
    size_t expected = event.getHasIff() ? 2 : 1;
    if (eventChildren.size() != expected) {
      unsupported(event) << " (event expression inventory)";
      return failure();
    }
    computed |= event.getHasIff() ||
                eventChildren.front()->hasAttr(clockingVariableAttrName) ||
                eventChildren.front()->hasAttr("virtual_interface_clocking") ||
                !isAddressableExpression(eventChildren.front()) ||
                !hasWatchableSignalHandle(eventChildren.front());
    FailureOr<Type> watchedType =
        eventChildren.front()->hasAttr("virtual_interface_clocking_block_event")
            ? FailureOr<Type>(sim::LogicType::get(function.getContext(), 1))
            : getNormalizedSemanticType(eventChildren.front());
    if (failed(watchedType))
      return failure();
    computed |= isa<sim::EventType>(*watchedType);
    events.push_back(event);
  }
  if (events.empty()) {
    unsupported(control) << " (empty event list)";
    return failure();
  }
  if (computed)
    return emitObserved(events);

  SmallVector<Value> watched;
  SmallVector<int32_t> edges;
  for (semantic::SVSignalEventControlOp event : events) {
    Operation *expression = getChildren(event).front();
    FailureOr<Value> handle = lowerExpression(expression, true);
    if (failed(handle))
      return failure();
    watched.push_back(*handle);
    edges.push_back(static_cast<int32_t>(event.getEdgeKind()));
  }
  if (watched.size() == 1) {
    emitDirect(watched.front(), static_cast<sim::EdgeKind>(edges.front()),
               continuation, continuationOperands);
    return success();
  }
  SmallVector<Value> values(watched);
  llvm::append_range(values, continuationOperands);
  auto suspend = sim::SimSuspendAnyOp::create(
      builder, location, values, builder.getDenseI32ArrayAttr(edges),
      sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
  suspend->setAttr(sim::metadata::proceduralEventWait, builder.getUnitAttr());
  return success();
}

LogicalResult
UnitLowering::lowerClockingEventMonitor(ArrayRef<Operation *> roots) {
  auto path =
      function->getAttrOfType<StringAttr>(clockingEventMonitorPathAttrName);
  if (!path || roots.size() != 1 ||
      !isa<semantic::SVSignalEventControlOp, semantic::SVEventListControlOp>(
          roots.front()))
    return function.emitError(
        "clocking event monitor requires one frozen event control");
  Value event = values.lookup(path.getValue());
  if (!event || !isa<sim::EventType>(event.getType()))
    return function.emitError(
        "clocking event monitor has no bound event descriptor");

  Location location = getSemanticLocation(roots.front());
  Block *wait = addBlock();
  cf::BranchOp::create(builder, location, wait);
  setCurrent(wait);
  Block *trigger = addBlock();
  if (failed(emitEventSuspend(roots.front(), trigger)))
    return failure();
  setCurrent(trigger);
  sim::SimEventTriggerOp::create(builder, location, event, Value{},
                                 builder.getBoolAttr(false),
                                 sim::EventSiteAttr{}, UnitAttr{});
  cf::BranchOp::create(builder, location, wait);
  return success();
}

LogicalResult
UnitLowering::emitRepeatedEventSuspend(Operation *control, Block *continuation,
                                       ValueRange continuationOperands) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);
  if (!isa<semantic::SVRepeatedEventControlOp>(control) ||
      children.size() != 2) {
    unsupported(control) << " (repeated-event inventory)";
    return failure();
  }
  FailureOr<Value> normalized = lowerRepeatedEventCount(control);
  if (failed(normalized))
    return failure();
  Type countType = builder.getI64Type();
  Value zero = arith::ConstantOp::create(builder, location, countType,
                                         builder.getI64IntegerAttr(0));
  Value positive = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, *normalized, zero);
  Block *wait = addBlock();
  wait->addArgument(countType, location);
  for (Value operand : continuationOperands)
    wait->addArgument(operand.getType(), location);
  Block *resume = addBlock();
  resume->addArgument(countType, location);
  for (Value operand : continuationOperands)
    resume->addArgument(operand.getType(), location);
  SmallVector<Value> initialWaitOperands{*normalized};
  llvm::append_range(initialWaitOperands, continuationOperands);
  cf::CondBranchOp::create(builder, location, positive, wait,
                           initialWaitOperands, continuation,
                           continuationOperands);
  setCurrent(wait);
  if (failed(emitEventSuspend(children[1], resume, wait->getArguments())))
    return failure();
  setCurrent(resume);
  Value one = arith::ConstantOp::create(builder, location, countType,
                                        builder.getI64IntegerAttr(1));
  Value resumeZero = arith::ConstantOp::create(builder, location, countType,
                                               builder.getI64IntegerAttr(0));
  Value remaining =
      arith::SubIOp::create(builder, location, resume->getArgument(0), one);
  Value more = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::sgt, remaining, resumeZero);
  SmallVector<Value> nextWaitOperands{remaining};
  llvm::append_range(nextWaitOperands, resume->getArguments().drop_front());
  cf::CondBranchOp::create(builder, location, more, wait, nextWaitOperands,
                           continuation, resume->getArguments().drop_front());
  setCurrent(continuation);
  return success();
}

FailureOr<Value> UnitLowering::lowerRepeatedEventCount(Operation *control) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);
  if (!isa<semantic::SVRepeatedEventControlOp>(control) ||
      children.size() != 2) {
    unsupported(control) << " (repeated-event inventory)";
    return failure();
  }
  FailureOr<Value> count = lowerExpression(children[0]);
  if (failed(count))
    return failure();
  return convert(*count, builder.getI64Type(), isSignedNode(children[0]),
                 location);
}

LogicalResult
UnitLowering::emitCycleDelaySuspend(semantic::SVCycleDelayControlOp control,
                                    Block *continuation,
                                    ValueRange continuationOperands) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);
  bool hasIff = control->hasAttr(clockingEventHasIffAttrName);
  size_t expectedChildren = hasIff ? 3 : 1;
  if (children.size() != expectedChildren) {
    unsupported(control) << " (cycle-delay inventory)";
    return failure();
  }
  auto clockPath =
      control->getAttrOfType<StringAttr>(clockingEventPathAttrName);
  auto eventEdge =
      control->getAttrOfType<semantic::EdgeKindAttr>(clockingEventEdgeAttrName);
  if (!clockPath || !eventEdge) {
    emitError(location)
        << "cycle delay has no supported default clocking event";
    return failure();
  }
  FailureOr<Value> clock =
      lowerReferencedValue(control, clockPath.getValue(), /*lvalue=*/true);
  if (failed(clock))
    return failure();
  std::optional<Value> incomingOccurrence = getCurrentClockingOccurrence(
      current, *clock, static_cast<sim::EdgeKind>(eventEdge.getValue()),
      hasIff);

  Value primaryObserver;
  Value conditionObserver;

  auto emitClockWait = [&](OpBuilder &waitBuilder, Block *successor,
                           ValueRange operands) -> LogicalResult {
    sim::EventRegionAttr reactive = sim::EventRegionAttr::get(
        function.getContext(), sim::EventRegion::Reactive);
    sim::EdgeKind edge = static_cast<sim::EdgeKind>(eventEdge.getValue());
    if (hasIff) {
      auto primaryType = cast<sim::ObserverType>(primaryObserver.getType());
      Value initial;
      if (auto reference = dyn_cast<sim::RefType>((*clock).getType()))
        initial = sim::SimRefLoadOp::create(waitBuilder, location,
                                            reference.getElementType(), *clock);
      else if (auto net = dyn_cast<sim::NetType>((*clock).getType()))
        initial = sim::SimNetReadOp::create(waitBuilder, location,
                                            net.getElementType(), *clock);
      if (!initial) {
        emitError(location) << "cycle-delay clock is not directly readable";
        return failure();
      }
      if (initial.getType() != primaryType.getResultType())
        initial = sim::SimPackedFlattenOp::create(
            waitBuilder, location, primaryType.getResultType(), initial);
      SmallVector<Value> values{primaryObserver, initial, conditionObserver};
      llvm::append_range(values, operands);
      sim::SimSuspendObserveOp::create(
          waitBuilder, location, values, 1,
          ArrayRef<int32_t>{static_cast<int32_t>(edge)}, ArrayRef<int32_t>{0},
          sim::ContinuationSiteAttr{}, reactive, successor);
      return success();
    }
    if (isa<sim::EventType>((*clock).getType()))
      sim::SimSuspendEventOp::create(waitBuilder, location, *clock, operands,
                                     sim::ContinuationSiteAttr{}, reactive,
                                     successor);
    else if (edge == sim::EdgeKind::Change)
      sim::SimSuspendChangeOp::create(waitBuilder, location, *clock, operands,
                                      sim::ContinuationSiteAttr{}, reactive,
                                      successor);
    else
      sim::SimSuspendEdgeOp::create(waitBuilder, location, edge, *clock,
                                    operands, sim::ContinuationSiteAttr{},
                                    reactive, successor);
    return success();
  };

  FailureOr<Type> expressionType = getNormalizedSemanticType(children.front());
  std::optional<unsigned> countWidth =
      succeeded(expressionType) ? sim::getPackedWidth(*expressionType)
                                : std::nullopt;
  if (failed(expressionType) || !countWidth || *countWidth == 0)
    return failure();

  std::optional<APInt> constantCount;
  if (std::optional<StringRef> spelling =
          getConstantSpelling(children.front())) {
    FailureOr<ParsedConstant> parsed =
        parseSVInteger(*spelling, *countWidth, location);
    if (failed(parsed))
      return failure();
    if (!parsed->unknown.isZero() ||
        (isSignedNode(children.front()) && parsed->value.isNegative())) {
      constantCount = APInt(*countWidth, 0);
    } else {
      constantCount = parsed->value;
    }
  }

  bool zeroCount = constantCount && constantCount->isZero();
  if (zeroCount && incomingOccurrence && !*incomingOccurrence) {
    // IEEE 1800-2017 14.11: a ##0 whose clocking event has already occurred in
    // this time step continues without suspension. Reaching here from that
    // event unconditionally is what makes it already occurred, so preserve the
    // occurrence rather than crossing a boundary.
    timingBoundaryContinuations.erase(continuation);
    cf::BranchOp::create(builder, location, continuation, continuationOperands);
    return success();
  }
  if (hasIff) {
    FailureOr<Value> primary = bindObserver(children[1]);
    FailureOr<Value> condition = bindObserver(children[2]);
    if (failed(primary) || failed(condition))
      return failure();
    primaryObserver = *primary;
    conditionObserver = *condition;
  }
  if (zeroCount) {
    // IEEE 1800-2017 14.11: otherwise the ##0 suspends until the clocking
    // event occurs. A conditional occurrence takes the fall-through only on
    // the paths that arrived through the event; either way the continuation
    // then runs inside it.
    Block *wait = current;
    if (incomingOccurrence && *incomingOccurrence) {
      wait = addBlock();
      cf::CondBranchOp::create(builder, location, *incomingOccurrence,
                               continuation, continuationOperands, wait,
                               ValueRange{});
    }
    OpBuilder waitBuilder = OpBuilder::atBlockEnd(wait);
    if (failed(emitClockWait(waitBuilder, continuation, continuationOperands)))
      return failure();
    clockingEventContinuations[continuation] = {*clock, {}};
    return success();
  }
  if (constantCount && constantCount->isOne()) {
    if (failed(emitClockWait(builder, continuation, continuationOperands)))
      return failure();
    clockingEventContinuations[continuation] = {*clock, {}};
    return success();
  }

  Type countType = builder.getIntegerType(*countWidth);
  Value count;
  Value positive;
  if (constantCount) {
    count = arith::ConstantOp::create(
        builder, location, countType,
        builder.getIntegerAttr(countType, *constantCount));
  } else {
    FailureOr<Value> value = lowerExpression(children.front());
    FailureOr<Value> scalar = succeeded(value)
                                  ? toPackedScalar(*value, location)
                                  : FailureOr<Value>(failure());
    FailureOr<Value> normalized =
        succeeded(scalar) ? convert(*scalar, countType,
                                    isSignedNode(children.front()), location)
                          : FailureOr<Value>(failure());
    if (failed(normalized))
      return failure();
    count = *normalized;
    Value zero = arith::ConstantOp::create(
        builder, location, countType,
        builder.getIntegerAttr(countType, APInt(*countWidth, 0)));
    positive = arith::CmpIOp::create(builder, location,
                                     isSignedNode(children.front())
                                         ? arith::CmpIPredicate::sgt
                                         : arith::CmpIPredicate::ne,
                                     count, zero);
    if (!incomingOccurrence || *incomingOccurrence)
      continuation->addArgument(builder.getI1Type(), location);
  }

  Block *wait = addBlock();
  wait->addArgument(countType, location);
  for (Value operand : continuationOperands)
    wait->addArgument(operand.getType(), location);
  Block *resume = addBlock();
  resume->addArgument(countType, location);
  for (Value operand : continuationOperands)
    resume->addArgument(operand.getType(), location);
  SmallVector<Value> initialWaitOperands{count};
  llvm::append_range(initialWaitOperands, continuationOperands);
  if (constantCount) {
    cf::BranchOp::create(builder, location, wait, initialWaitOperands);
  } else {
    if (continuation->getNumArguments() == continuationOperands.size()) {
      cf::CondBranchOp::create(builder, location, positive, wait,
                               initialWaitOperands, continuation,
                               continuationOperands);
    } else {
      Value didNotWait = incomingOccurrence && *incomingOccurrence
                             ? *incomingOccurrence
                             : arith::ConstantOp::create(
                                   builder, location, builder.getI1Type(),
                                   builder.getBoolAttr(false));
      SmallVector<Value> zeroOperands(continuationOperands);
      zeroOperands.push_back(didNotWait);
      cf::CondBranchOp::create(builder, location, positive, wait,
                               initialWaitOperands, continuation, zeroOperands);
    }
  }

  OpBuilder waitBuilder = OpBuilder::atBlockEnd(wait);
  if (failed(emitClockWait(waitBuilder, resume, wait->getArguments())))
    return failure();
  OpBuilder resumeBuilder = OpBuilder::atBlockEnd(resume);
  Value one = arith::ConstantOp::create(
      resumeBuilder, location, countType,
      resumeBuilder.getIntegerAttr(countType, APInt(*countWidth, 1)));
  Value zero = arith::ConstantOp::create(
      resumeBuilder, location, countType,
      resumeBuilder.getIntegerAttr(countType, APInt(*countWidth, 0)));
  Value remaining = arith::SubIOp::create(resumeBuilder, location,
                                          resume->getArgument(0), one);
  Value more = arith::CmpIOp::create(resumeBuilder, location,
                                     arith::CmpIPredicate::ne, remaining, zero);
  SmallVector<Value> nextWaitOperands{remaining};
  llvm::append_range(nextWaitOperands, resume->getArguments().drop_front());
  ValueRange finalOperands = resume->getArguments().drop_front();
  if (constantCount) {
    cf::CondBranchOp::create(resumeBuilder, location, more, wait,
                             nextWaitOperands, continuation, finalOperands);
    clockingEventContinuations[continuation] = {*clock, {}};
  } else {
    if (continuation->getNumArguments() == continuationOperands.size()) {
      cf::CondBranchOp::create(resumeBuilder, location, more, wait,
                               nextWaitOperands, continuation, finalOperands);
      clockingEventContinuations[continuation] = {*clock, {}};
    } else {
      Value didWait = arith::ConstantOp::create(
          resumeBuilder, location, builder.getI1Type(),
          resumeBuilder.getBoolAttr(true));
      SmallVector<Value> waitedOperands(finalOperands);
      waitedOperands.push_back(didWait);
      cf::CondBranchOp::create(resumeBuilder, location, more, wait,
                               nextWaitOperands, continuation, waitedOperands);
      clockingEventContinuations[continuation] = {
          *clock, continuation->getArgument(continuationOperands.size())};
    }
  }
  return success();
}

LogicalResult UnitLowering::lowerTiming(Operation *control,
                                        Operation *statement) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);
  if (initializeProceduralTimingPaths) {
    auto initialize = std::move(initializeProceduralTimingPaths);
    initializeProceduralTimingPaths = {};
    if (failed(initialize()))
      return failure();
  }
  auto lowerControlledStatement =
      [&](Operation *sampledClock) -> LogicalResult {
    Operation *savedClock = activeSampledClock;
    activeSampledClock = sampledClock;
    if (prepareProceduralTimingPaths) {
      auto prepare = std::move(prepareProceduralTimingPaths);
      prepareProceduralTimingPaths = {};
      if (failed(prepare())) {
        activeSampledClock = savedClock;
        return failure();
      }
    }
    LogicalResult result = lowerStatement(statement);
    activeSampledClock = savedClock;
    return result;
  };

  if (isa<semantic::SVImplicitEventControlOp>(control)) {
    // The dependency set belongs to the controlled statement, including
    // reads reached through direct zero-time calls. Build that continuation
    // first, then terminate the pre-control block with the derived wait.
    Block *waitBlock = current;
    Block *continuation = addBlock();
    setCurrent(continuation);
    llvm::SetVector<Value> dependencies;
    llvm::SetVector<Value> *saved = observedDependencies;
    observedDependencies = &dependencies;
    LogicalResult result = lowerControlledStatement(nullptr);
    observedDependencies = saved;
    if (failed(result))
      return failure();
    // IEEE 1800-2017 9.4.2.2: an enclosing implicit event list covers every
    // read of the statement it controls, and this nested statement is part of
    // it. Only the identifiers of a nested event *expression* are excluded,
    // and an implicit list has none.
    if (saved)
      saved->insert_range(dependencies);
    Block *statementEnd = current;
    if (dependencies.empty()) {
      // IEEE 1800-2017 9.4.2.2 derives the implicit event expression from
      // readable operands in the controlled statement.  If there are none,
      // the process has no event that can resume it.  Keep the continuation
      // in the CFG for ordinary structured lowering, but permanently suspend
      // instead of rejecting the legal (and intentionally inert) process.
      setCurrent(waitBlock);
      sim::SimSuspendForeverOp::create(builder, location, ValueRange{},
                                       sim::ContinuationSiteAttr{},
                                       sim::EventRegionAttr{}, continuation);
      setCurrent(statementEnd);
      return success();
    }
    setCurrent(waitBlock);
    SmallVector<int32_t> edges(dependencies.size(),
                               static_cast<int32_t>(sim::EdgeKind::Change));
    if (dependencies.size() == 1) {
      auto suspend = sim::SimSuspendChangeOp::create(
          builder, location, dependencies.front(), ValueRange{},
          sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
      suspend->setAttr(sim::metadata::proceduralEventWait,
                       builder.getUnitAttr());
      if (control == topLevelWildcardControl)
        suspend->setAttr(sim::metadata::topLevelWildcardWait,
                         builder.getUnitAttr());
    } else {
      auto suspend = sim::SimSuspendAnyOp::create(
          builder, location, dependencies.getArrayRef(),
          builder.getDenseI32ArrayAttr(edges), sim::ContinuationSiteAttr{},
          sim::EventRegionAttr{}, continuation);
      suspend->setAttr(sim::metadata::proceduralEventWait,
                       builder.getUnitAttr());
      if (control == topLevelWildcardControl)
        suspend->setAttr(sim::metadata::topLevelWildcardWait,
                         builder.getUnitAttr());
    }
    setCurrent(statementEnd);
    return success();
  }

  if (isa<semantic::SVRepeatedEventControlOp>(control)) {
    Block *continuation = addBlock();
    if (failed(emitRepeatedEventSuspend(control, continuation)))
      return failure();
    return lowerControlledStatement(nullptr);
  }

  Block *continuation = addBlock();
  // A new timing boundary ends any clocking occurrence inherited while this
  // continuation block was allocated. emitEventSuspend reattaches it only for
  // a virtual clocking-block event.
  timingBoundaryContinuations.insert(continuation);
  if (isa<semantic::SVDelayControlOp>(control)) {
    FailureOr<Value> delay = lowerDelayValue(control);
    if (failed(delay))
      return failure();
    sim::SimSuspendDelayOp::create(
        builder, location, *delay, sim::TimingSiteAttr{}, ValueRange{},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
  } else if (isa<semantic::SVOneStepDelayControlOp>(control)) {
    if (!children.empty()) {
      unsupported(control) << " (#1step inventory)";
      return failure();
    }
    Value delay = sim::SimTimeConstantOp::create(
        builder, location, sim::TimeType::get(function.getContext()),
        builder.getI64IntegerAttr(1));
    sim::SimSuspendDelayOp::create(
        builder, location, delay, sim::TimingSiteAttr{}, ValueRange{},
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
  } else if (auto cycle = dyn_cast<semantic::SVCycleDelayControlOp>(control)) {
    if (failed(emitCycleDelaySuspend(cycle, continuation)))
      return failure();
  } else if (isa<semantic::SVSignalEventControlOp,
                 semantic::SVEventListControlOp>(control)) {
    if (failed(emitEventSuspend(control, continuation)))
      return failure();
  } else {
    unsupported(control) << " (timing control)";
    return failure();
  }
  setCurrent(continuation);
  return lowerControlledStatement(
      isa<semantic::SVSignalEventControlOp>(control) ? control : nullptr);
}

LogicalResult UnitLowering::lowerWait(semantic::SVWaitStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.size() != 2) {
    unsupported(op) << " (wait inventory)";
    return failure();
  }

  Block *conditionBlock = addBlock();
  Block *suspendBlock = addBlock();
  Block *bodyBlock = addBlock();
  emitBranch(conditionBlock);
  setCurrent(conditionBlock);
  llvm::SetVector<Value> dependencies;
  llvm::SetVector<Value> *saved = observedDependencies;
  observedDependencies = &dependencies;
  FailureOr<Value> conditionValue = lowerExpression(children[0]);
  observedDependencies = saved;
  if (failed(conditionValue))
    return failure();
  FailureOr<Value> condition = truthValue(*conditionValue, location);
  if (failed(condition))
    return failure();

  // Short-circuit operators can place a managed read in a block which does
  // not dominate the suspension block.  Materialize each managed dependency
  // from the post-evaluation state at the condition merge.  Besides producing
  // valid SSA, this is required for expressions whose evaluation changes a
  // container-valued field: the waiter must observe the container which is
  // current when it actually suspends.
  DominanceInfo dominance(function);
  auto dominatesCurrent = [&](Value value) {
    Block *definition = value.getParentBlock();
    return definition == current || dominance.dominates(definition, current);
  };
  std::function<FailureOr<Value>(Value)> rematerializeManagedInput =
      [&](Value value) -> FailureOr<Value> {
    if (auto extract = value.getDefiningOp<sim::SimAggregateExtractOp>()) {
      FailureOr<Value> input = rematerializeManagedInput(extract.getInput());
      if (failed(input))
        return failure();
      return sim::SimAggregateExtractOp::create(builder, extract.getLoc(),
                                                extract.getResult().getType(),
                                                *input, extract.getIndexAttr())
          .getResult();
    }
    if (auto extract = value.getDefiningOp<sim::SimUnionExtractOp>()) {
      FailureOr<Value> input = rematerializeManagedInput(extract.getInput());
      if (failed(input))
        return failure();
      return sim::SimUnionExtractOp::create(builder, extract.getLoc(),
                                            extract.getResult().getType(),
                                            *input, extract.getIndexAttr())
          .getResult();
    }
    if (auto extract = value.getDefiningOp<sim::SimRefSubelementOp>()) {
      FailureOr<Value> input = rematerializeManagedInput(extract.getInput());
      if (failed(input))
        return failure();
      return sim::SimRefSubelementOp::create(builder, extract.getLoc(),
                                             extract.getResult().getType(),
                                             *input, extract.getIndicesAttr())
          .getResult();
    }
    if (auto field = value.getDefiningOp<sim::SimClassFieldRefOp>()) {
      FailureOr<Value> object = rematerializeManagedInput(field.getObject());
      if (failed(object))
        return failure();
      return sim::SimClassFieldRefOp::create(builder, field.getLoc(),
                                             field.getResult().getType(),
                                             *object, field.getFieldAttr())
          .getResult();
    }
    if (auto load = value.getDefiningOp<sim::SimManagedLoadOp>()) {
      FailureOr<Value> reference =
          rematerializeManagedInput(load.getReference());
      if (failed(reference))
        return failure();
      return sim::SimManagedLoadOp::create(
                 builder, load.getLoc(), load.getResult().getType(), *reference)
          .getResult();
    }
    if (auto load = value.getDefiningOp<sim::SimRefLoadOp>()) {
      FailureOr<Value> input = rematerializeManagedInput(load.getReference());
      if (failed(input))
        return failure();
      return sim::SimRefLoadOp::create(builder, load.getLoc(),
                                       load.getResult().getType(), *input)
          .getResult();
    }
    if (auto cast = value.getDefiningOp<sim::SimClassCastOp>()) {
      FailureOr<Value> object = rematerializeManagedInput(cast.getObject());
      if (failed(object))
        return failure();
      return sim::SimClassCastOp::create(builder, cast.getLoc(),
                                         cast.getResult().getType(), *object)
          .getResult();
    }
    if (dominatesCurrent(value))
      return value;
    emitError(location)
        << "managed wait dependency cannot be materialized at the stable "
           "condition point from "
        << value.getType();
    return failure();
  };
  SmallVector<Value> stableDependencies;
  stableDependencies.reserve(dependencies.size());
  for (Value dependency : dependencies) {
    auto watch = dependency.getDefiningOp<sim::SimManagedWatchOp>();
    if (!watch) {
      stableDependencies.push_back(dependency);
      continue;
    }
    FailureOr<Value> input = rematerializeManagedInput(watch.getInput());
    if (failed(input))
      return failure();
    stableDependencies.push_back(sim::SimManagedWatchOp::create(
        builder, watch.getLoc(), watch.getResult().getType(), *input,
        watch.getKind()));
  }
  if (dependencies.empty()) {
    std::optional<bool> truth = foldConstantTruth(*condition);
    if (!truth) {
      unsupported(op)
          << " (computed wait condition has no readable dependency)";
      return failure();
    }
    emitBranch(*truth ? bodyBlock : suspendBlock);
    if (!*truth) {
      setCurrent(suspendBlock);
      sim::SimSuspendForeverOp::create(builder, location, ValueRange{},
                                       sim::ContinuationSiteAttr{},
                                       sim::EventRegionAttr{}, bodyBlock);
    } else
      suspendBlock->erase();
    setCurrent(bodyBlock);
    return lowerStatement(children[1]);
  }
  if (stableDependencies.size() != 1 ||
      isa<sim::ManagedWatchType>(stableDependencies.front().getType()) ||
      !isAddressableExpression(children[0]) ||
      !storageDecidesTruth(children[0])) {
    if (!children[0]->hasAttr("obelisk_sim.observer")) {
      unsupported(op) << " (computed wait condition requires an observer)";
      return failure();
    }
    cf::CondBranchOp::create(builder, location, *condition, bodyBlock,
                             ValueRange{}, suspendBlock, ValueRange{});
    setCurrent(suspendBlock);
    SmallVector<Value> managedDependencies;
    for (Value dependency : stableDependencies)
      if (isa<sim::ManagedWatchType>(dependency.getType()))
        managedDependencies.push_back(dependency);
    FailureOr<Value> observer = bindObserver(children[0], managedDependencies);
    if (failed(observer))
      return failure();
    SmallVector<Value> values{*observer, *condition};
    sim::SimSuspendObserveOp::create(
        builder, location, values, 0, ArrayRef<int32_t>{0},
        ArrayRef<int32_t>{-1}, sim::ContinuationSiteAttr{},
        sim::EventRegionAttr{}, bodyBlock);
    setCurrent(bodyBlock);
    return lowerStatement(children[1]);
  }
  FailureOr<Value> watched = lowerExpression(children[0], true);
  if (failed(watched))
    return failure();
  if (!isa<sim::RefType, sim::NetType>((*watched).getType())) {
    unsupported(op) << " (wait condition is not directly watchable)";
    return failure();
  }
  cf::CondBranchOp::create(builder, location, *condition, bodyBlock,
                           ValueRange{}, suspendBlock, ValueRange{});
  setCurrent(suspendBlock);
  sim::SimSuspendLevelOp::create(builder, location, *watched, ValueRange{},
                                 sim::ContinuationSiteAttr{},
                                 sim::EventRegionAttr{}, bodyBlock);

  setCurrent(bodyBlock);
  return lowerStatement(children[1]);
}

LogicalResult
UnitLowering::lowerEventTrigger(semantic::SVEventTriggerStatementOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  size_t expected = op.getHasTimingControl() ? 2 : 1;
  if (children.size() != expected) {
    unsupported(op) << " (event trigger inventory)";
    return failure();
  }
  if (op.getHasTimingControl() && !op.getIsNonblocking()) {
    emitError(location) << "a timed named-event trigger must be nonblocking";
    return failure();
  }
  FailureOr<Value> event = lowerExpression(children.front());
  if (failed(event))
    return failure();
  if (!isa<sim::EventType>((*event).getType())) {
    emitError(location) << "event trigger operand is not an event handle";
    return failure();
  }
  Value delay;
  if (op.getHasTimingControl()) {
    FailureOr<Value> loweredDelay = lowerDelayValue(children[1]);
    if (failed(loweredDelay))
      return failure();
    delay = *loweredDelay;
  }
  sim::SimEventTriggerOp::create(builder, location, *event, delay,
                                 builder.getBoolAttr(op.getIsNonblocking()),
                                 sim::EventSiteAttr{}, UnitAttr{});
  return success();
}

} // namespace obelisk::simlowering
