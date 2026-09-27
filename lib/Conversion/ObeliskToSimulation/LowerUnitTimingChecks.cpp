//===- LowerUnitTimingChecks.cpp - Lower system timing checks ------------===//

#include "LowerUnit.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

using namespace mlir;

namespace obelisk::simlowering {

LogicalResult
UnitLowering::lowerSystemTimingCheck(ArrayRef<Operation *> roots) {
  Location location = function.getLoc();
  auto kindAttr = function->getAttrOfType<IntegerAttr>("timing_check_kind");
  auto expressionChildren = function->getAttrOfType<DenseI64ArrayAttr>(
      "timing_check_arg_expression_children");
  auto conditionChildren = function->getAttrOfType<DenseI64ArrayAttr>(
      "timing_check_arg_condition_children");
  auto frozenConditionPredicates = function->getAttrOfType<DenseI32ArrayAttr>(
      "timing_check_arg_condition_predicates");
  auto edges = function->getAttrOfType<DenseI32ArrayAttr>(
      "timing_check_arg_effective_edges");
  auto ticks = function->getAttrOfType<DenseI64ArrayAttr>(
      "simulation.timing_check_arg_ticks");
  if (!kindAttr || !expressionChildren || !conditionChildren || !edges ||
      !ticks || expressionChildren.size() != conditionChildren.size() ||
      static_cast<size_t>(expressionChildren.size()) !=
          static_cast<size_t>(edges.size()) ||
      (frozenConditionPredicates &&
       frozenConditionPredicates.size() != conditionChildren.size()) ||
      expressionChildren.size() != ticks.size())
    return function.emitError("basic timing check has a malformed frozen ABI");

  int32_t kind = static_cast<int32_t>(kindAttr.getInt());
  bool combined = kind == 3 || kind == 6;
  bool skew = kind == 7;
  bool timeSkew = kind == 8;
  bool fullSkew = kind == 9;
  bool skewWithMode = timeSkew || fullSkew;
  bool period = kind == 10;
  bool width = kind == 11;
  bool noChange = kind == 12;
  bool slotFinal = (kind >= 1 && kind <= 9) || noChange;
  if (kind != 1 && kind != 2 && kind != 3 && kind != 4 && kind != 5 &&
      kind != 6 && !skew && !skewWithMode && !period && !width && !noChange)
    return function.emitError("unsupported basic timing-check kind");
  size_t minimumArguments = combined || fullSkew || noChange ? 4
                            : period || width                ? 2
                                                             : 3;
  if (static_cast<size_t>(expressionChildren.size()) < minimumArguments)
    return function.emitError("basic timing check has a malformed frozen ABI");
  auto childFor = [&](size_t index) -> Operation * {
    if (index >= static_cast<size_t>(expressionChildren.size()))
      return nullptr;
    int64_t child = expressionChildren[index];
    return child >= 0 && static_cast<size_t>(child) < roots.size()
               ? roots[child]
               : nullptr;
  };
  Operation *event0 = childFor(0);
  Operation *event1 = period || width ? nullptr : childFor(1);
  size_t firstLimit = period || width ? 1 : 2;
  if (!event0 || (!(period || width) && !event1) ||
      (!noChange && ticks[firstLimit] < 0) ||
      ((combined || fullSkew) && ticks[3] < 0) ||
      (width && ticks.size() > 2 && ticks[2] < 0))
    return function.emitError(
        "basic timing check has no direct events or limit");
  std::optional<bool> remainActive = false;
  std::optional<bool> eventMode = false;
  Value remainActiveValue;
  Value eventModeValue;
  if (skewWithMode) {
    auto eventBased =
        function->getAttrOfType<BoolAttr>("timing_check_event_based");
    auto remain =
        function->getAttrOfType<BoolAttr>("timing_check_remain_active");
    size_t eventBasedIndex = fullSkew ? 5 : 4;
    size_t remainActiveIndex = fullSkew ? 6 : 5;
    auto lowerInvariantFlag = [&](BoolAttr frozen, size_t index,
                                  std::optional<bool> &constant,
                                  Value &runtime) -> LogicalResult {
      if (frozen) {
        constant = frozen.getValue();
        return success();
      }
      Operation *child = childFor(index);
      if (!child) {
        constant = false;
        return success();
      }
      FailureOr<Value> lowered = lowerExpression(child, false);
      if (failed(lowered))
        return failure();
      FailureOr<Value> truth = truthValue(*lowered, getSemanticLocation(child));
      if (failed(truth))
        return failure();
      constant.reset();
      runtime = *truth;
      return success();
    };
    // IEEE 1800-2017 31.4.2/.3 define these flags as invariant timing-check
    // arguments.  Keep frozen attributes as compile-time specializations;
    // otherwise evaluate each existing semantic argument child exactly once
    // before the coordinator's first suspension.
    if (failed(lowerInvariantFlag(eventBased, eventBasedIndex, eventMode,
                                  eventModeValue)) ||
        failed(lowerInvariantFlag(remain, remainActiveIndex, remainActive,
                                  remainActiveValue)))
      return failure();
  }
  bool dynamicMode = skewWithMode && !eventMode.has_value();
  bool emitEventMode = skewWithMode && (!eventMode || *eventMode);
  bool emitTimerMode = skewWithMode && (!eventMode || !*eventMode);
  Value context = function.getBody().front().getArgument(0);
  auto delayedStorageIDs = function->getAttrOfType<DenseI64ArrayAttr>(
      "simulation.timing_delayed_storage_ids");
  if (delayedStorageIDs && delayedStorageIDs.size() != 2)
    return function.emitError(
        "negative timing check has a malformed delayed-terminal ABI");

  SmallVector<Value, 2> handles;
  SmallVector<Value, 2> conditions;
  SmallVector<int32_t, 2> conditionPredicates;
  SmallVector<int32_t, 2> eventEdges;
  size_t sourceEventCount = period || width ? 1 : 2;
  SmallVector<int32_t, 2> conditionIndices(sourceEventCount, -1);
  SmallVector<Value, 2> eventConditions(sourceEventCount);
  std::array<Operation *, 2> events{event0, event1};
  ArrayRef<Operation *> sourceEvents(events.data(), sourceEventCount);
  for (auto [index, event] : llvm::enumerate(sourceEvents)) {
    FailureOr<Value> handle = lowerExpression(event, true);
    if (failed(handle) || !isa<sim::RefType, sim::NetType, sim::DriverType>(
                              succeeded(handle) ? (*handle).getType() : Type{}))
      return function.emitError(
          "basic timing-check event is not a direct signal handle");
    if (delayedStorageIDs && index < 2 && delayedStorageIDs[index] >= 0) {
      Type elementType;
      if (auto ref = dyn_cast<sim::RefType>((*handle).getType()))
        elementType = ref.getElementType();
      else if (auto net = dyn_cast<sim::NetType>((*handle).getType()))
        elementType = net.getElementType();
      else
        elementType =
            cast<sim::DriverType>((*handle).getType()).getElementType();
      *handle = sim::SimContextStorageOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), elementType), context,
          builder.getI64IntegerAttr(delayedStorageIDs[index]));
    }
    int32_t edge = edges[index];
    bool standardEdge = edge >= static_cast<int32_t>(sim::EdgeKind::Change) &&
                        edge <= static_cast<int32_t>(sim::EdgeKind::Both);
    bool customEdge = (edge & ~0x3f) == 0x100 && (edge & 0x3f) != 0;
    if (!standardEdge && !customEdge)
      return function.emitError("basic timing-check event has an invalid edge");
    // IEEE 1800-2017 31.8 defines one timing check when one or more bits of
    // a vector transition change. Preserve the whole direct handle: the
    // existing subscription scan reduces matching bits to one publication
    // occurrence.
    handles.push_back(*handle);
    eventEdges.push_back(edge);

    int64_t conditionChild = conditionChildren[index];
    if (conditionChild < 0)
      continue;
    if (static_cast<size_t>(conditionChild) >= roots.size())
      return function.emitError(
          "basic timing-check condition has an invalid child");
    int32_t predicate =
        frozenConditionPredicates ? frozenConditionPredicates[index] : 0;
    Operation *conditionRoot = roots[conditionChild];
    Operation *conditionOperand =
        getTimingConditionOperand(conditionRoot, predicate);
    if (!conditionOperand)
      return function.emitError(
          "basic timing-check condition has no normalized operand");
    FailureOr<Value> condition = failure();
    if (conditionOperand->hasAttr("simulation.observer"))
      condition = bindObserver(conditionOperand);
    else
      condition = lowerExpression(conditionOperand, true);
    if (failed(condition))
      return failure();

    if (isa<sim::ObserverType>((*condition).getType())) {
      auto observed = cast<sim::ObserverType>((*condition).getType());
      std::optional<unsigned> width =
          sim::getPackedWidth(observed.getResultType());
      if (!width || *width != 1)
        return function.emitError(
            "computed timing-check condition must return one packed bit");
      // IEEE 1800-2017 31.7 samples the condition only after its primary
      // event matches. The observer token carries compiled code and frozen
      // handles into that existing clock wait; its dependencies never become
      // independent wakeups.
      conditionIndices[index] = static_cast<int32_t>(conditions.size());
      conditions.push_back(*condition);
      conditionPredicates.push_back(predicate);
      eventConditions[index] = *condition;
      continue;
    }
    if (!isa<sim::RefType, sim::NetType, sim::DriverType>(
            (*condition).getType()))
      return function.emitError(
          "basic timing-check condition is neither a direct handle nor a "
          "compiled observer");

    Type elementType;
    if (auto ref = dyn_cast<sim::RefType>((*condition).getType()))
      elementType = ref.getElementType();
    else if (auto net = dyn_cast<sim::NetType>((*condition).getType()))
      elementType = net.getElementType();
    else
      elementType =
          cast<sim::DriverType>((*condition).getType()).getElementType();
    std::optional<unsigned> conditionWidth = sim::getPackedWidth(elementType);
    if (!conditionWidth || *conditionWidth == 0)
      return function.emitError(
          "basic timing-check condition has no packed LSB");
    if ((predicate == 0 || predicate == 1) && *conditionWidth != 1) {
      Type scalar = sim::getPackedScalarType(elementType);
      Type bitType = isa<sim::LogicType>(scalar)
                         ? Type(sim::LogicType::get(function.getContext(), 1))
                         : Type(builder.getI1Type());
      if (isa<sim::RefType>((*condition).getType())) {
        Type type = sim::RefType::get(function.getContext(), bitType);
        condition =
            sim::SimRefExtractOp::create(builder, location, type, *condition,
                                         builder.getI64IntegerAttr(0))
                .getResult();
      } else if (isa<sim::NetType>((*condition).getType())) {
        Type type = sim::NetType::get(function.getContext(), bitType);
        condition =
            sim::SimNetExtractOp::create(builder, location, type, *condition,
                                         builder.getI64IntegerAttr(0))
                .getResult();
      } else {
        Type type = sim::DriverType::get(function.getContext(), bitType);
        condition =
            sim::SimDriverExtractOp::create(builder, location, type, *condition,
                                            builder.getI64IntegerAttr(0))
                .getResult();
      }
    }
    // IEEE 1800-2017 31.7 samples the operand's LSB before applying the
    // frozen predicate. Direct equality forms retain the packed handle; the
    // runtime reads precisely bit zero rather than constructing an SSA compare.
    conditionIndices[index] = static_cast<int32_t>(conditions.size());
    conditions.push_back(*condition);
    conditionPredicates.push_back(predicate);
    eventConditions[index] = *condition;
  }

  std::array<unsigned, 2> rawEventBits{0, 1};
  if (skewWithMode) {
    for (size_t index = 0; index != sourceEventCount; ++index) {
      if (conditionChildren[index] < 0)
        continue;
      // IEEE 1800-2017 31.4.2/.3 give a false conditioned timestamp event
      // state-machine meaning distinct from no transition. Subscribe to an
      // unconditioned shadow of only that direct event; the ordinary primary
      // remains condition-qualified and no runtime timing-check state exists.
      rawEventBits[index] = handles.size();
      handles.push_back(handles[index]);
      eventEdges.push_back(eventEdges[index]);
      conditionIndices.push_back(-1);
    }
  }

  if (width || noChange) {
    int32_t edge = eventEdges.front();
    if (edge != static_cast<int32_t>(sim::EdgeKind::Posedge) &&
        edge != static_cast<int32_t>(sim::EdgeKind::Negedge))
      return function.emitError(
          width ? "$width requires a canonical posedge or negedge event"
                : "$nochange requires a canonical posedge or negedge "
                  "reference event");
    handles.push_back(handles.front());
    eventEdges.push_back(edge == static_cast<int32_t>(sim::EdgeKind::Posedge)
                             ? static_cast<int32_t>(sim::EdgeKind::Negedge)
                             : static_cast<int32_t>(sim::EdgeKind::Posedge));
    conditionIndices.push_back(-1);
    if (eventConditions.front()) {
      // Each derived $width/$nochange event samples the same Clause 31.7
      // condition at publication time. Keep two ABI slots because a clock-set
      // condition belongs to exactly one primary, even when both slots name one
      // handle.
      conditionIndices.back() = static_cast<int32_t>(conditions.size());
      conditions.push_back(eventConditions.front());
      conditionPredicates.push_back(conditionPredicates.front());
    }
  }

  std::optional<CapturedLValue> notifier;
  size_t notifierIndex = combined || fullSkew || noChange ? 4 : period ? 2 : 3;
  if (static_cast<size_t>(expressionChildren.size()) > notifierIndex &&
      expressionChildren[notifierIndex] >= 0) {
    Operation *notifierExpression = childFor(notifierIndex);
    SmallVector<Operation *> notifierChildren = getChildren(notifierExpression);
    if (isa<semantic::SVAssignmentExpressionOp>(notifierExpression) &&
        !notifierChildren.empty())
      notifierExpression = notifierChildren.front();
    FailureOr<CapturedLValue> captured =
        captureLValue(notifierExpression, location);
    if (failed(captured))
      return failure();
    notifier = std::move(*captured);
  }

  Type i1 = builder.getI1Type();
  Type i64 = builder.getI64Type();
  Value zero64 = arith::ConstantOp::create(builder, location, i64,
                                           builder.getI64IntegerAttr(0));
  Value one64 = arith::ConstantOp::create(builder, location, i64,
                                          builder.getI64IntegerAttr(1));
  Value two64 = arith::ConstantOp::create(builder, location, i64,
                                          builder.getI64IntegerAttr(2));
  Value falseValue = arith::ConstantOp::create(builder, location, i1,
                                               builder.getBoolAttr(false));
  Value trueValue;
  if (skewWithMode)
    trueValue = arith::ConstantOp::create(builder, location, i1,
                                          builder.getBoolAttr(true));
  Value noChangeStart;
  Value noChangeEnd;
  if (noChange) {
    noChangeStart = arith::ConstantOp::create(
        builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
    noChangeEnd = arith::ConstantOp::create(
        builder, location, i64, builder.getI64IntegerAttr(ticks[3]));
  }
  Value timestamp;
  Value timestampValid;
  if (!noChange) {
    timestamp = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i64),
        zero64);
    timestampValid = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i1),
        falseValue);
  }
  Value slotEvent0Count;
  Value slotEvent1Count;
  Value slotEventViolationCount;
  Value slotTimerRestart;
  Value slotTimerCancel;
  if (slotFinal && !noChange) {
    slotEvent0Count = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i64),
        zero64);
    slotEvent1Count = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i64),
        zero64);
    if (emitEventMode)
      slotEventViolationCount = sim::SimRefAllocOp::create(
          builder, location, sim::RefType::get(function.getContext(), i64),
          zero64);
    if (emitTimerMode) {
      slotTimerRestart = sim::SimRefAllocOp::create(
          builder, location, sim::RefType::get(function.getContext(), i1),
          falseValue);
      slotTimerCancel = sim::SimRefAllocOp::create(
          builder, location, sim::RefType::get(function.getContext(), i1),
          falseValue);
    }
  }
  Value oppositeTimestamp;
  Value oppositeTimestampValid;
  if (combined) {
    oppositeTimestamp = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i64),
        zero64);
    oppositeTimestampValid = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i1),
        falseValue);
  }
  Value timestampDirection;
  if (fullSkew)
    timestampDirection = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i1),
        falseValue);

  Value timerDeadline;
  Value timerEvent;
  Value timerSignal;
  Value slotTimerFired;
  unsigned timerEventBit = 0;
  if (emitTimerMode) {
    timerDeadline = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i64),
        zero64);
    timerEvent = sim::SimEventCreateOp::create(
        builder, location, sim::EventType::get(function.getContext()));
    slotTimerFired = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i1),
        falseValue);

    auto timerStorage = function->getAttrOfType<IntegerAttr>(
        "simulation.timing_timer_storage");
    auto helperSymbol = function->getAttrOfType<FlatSymbolRefAttr>(
        "simulation.timing_timer_helper");
    if (!timerStorage || !helperSymbol)
      return function.emitError(
          "timer timing check has no serial helper inventory");
    function->removeAttr("simulation.timing_timer_storage");
    function->removeAttr("simulation.timing_timer_helper");
    timerSignal = sim::SimContextStorageOp::create(
        builder, location, sim::RefType::get(function.getContext(), i1),
        context, timerStorage);
    timerEventBit = handles.size();
    handles.push_back(timerSignal);
    eventEdges.push_back(static_cast<int32_t>(sim::EdgeKind::Both));
    conditionIndices.push_back(-1);

    SmallVector<Value> helperCaptures{context, timerEvent, timerSignal};
    sim::SimSpawnOp::create(builder, location, helperSymbol, helperCaptures,
                            ArrayAttr{}, ArrayAttr{});
  }

  auto codeUnit = function->getAttrOfType<IntegerAttr>("code_unit_id");
  uint32_t occurrenceSite =
      codeUnit ? static_cast<uint32_t>(codeUnit.getValue().getZExtValue()) : 0;
  if (occurrenceSite == 0)
    occurrenceSite = 1;

  Block *wait = addBlock();
  Block *drain = addBlock();
  Block *process = addBlock();
  Block *eventProcess = dynamicMode ? addBlock() : nullptr;
  Block *timerProcess = dynamicMode ? addBlock() : nullptr;
  Block *processJoin = dynamicMode ? addBlock() : nullptr;
  Block *slotFinalize = slotFinal ? addBlock() : nullptr;
  Block *slotReset = slotFinal ? addBlock() : nullptr;
  Block *eventFinalize = dynamicMode ? addBlock() : nullptr;
  Block *timerFinalize = dynamicMode ? addBlock() : nullptr;
  Block *modeFinalizeJoin = dynamicMode ? addBlock() : nullptr;
  if (modeFinalizeJoin) {
    modeFinalizeJoin->addArgument(builder.getI1Type(), location);
    modeFinalizeJoin->addArgument(builder.getI64Type(), location);
  }
  Block *timerSchedule = emitTimerMode ? addBlock() : nullptr;
  Block *timerMaybeCancel = emitTimerMode ? addBlock() : nullptr;
  Block *timerCancel = emitTimerMode ? addBlock() : nullptr;
  Block *timerContinue = emitTimerMode ? addBlock() : nullptr;
  if (timerContinue)
    timerContinue->addArgument(i1, location);
  Block *violation = addBlock();
  violation->addArgument(i64, location);
  emitBranch(wait);

  setCurrent(wait);
  SmallVector<Value, 4> waitValues;
  llvm::append_range(waitValues, handles);
  llvm::append_range(waitValues, conditions);
  bool needsConditionPredicates = llvm::any_of(
      conditionPredicates, [](int32_t predicate) { return predicate != 0; });
  sim::SimSuspendClockSetOp::create(
      builder, location, waitValues,
      builder.getI32IntegerAttr(conditions.size()),
      builder.getDenseI32ArrayAttr(eventEdges),
      builder.getDenseI32ArrayAttr(conditionIndices),
      !needsConditionPredicates
          ? DenseI32ArrayAttr{}
          : builder.getDenseI32ArrayAttr(conditionPredicates),
      builder.getI64IntegerAttr(occurrenceSite),
      slotFinal ? builder.getUnitAttr() : UnitAttr{},
      schedule::ContinuationSiteAttr{},
      sim::EventRegionAttr::get(function.getContext(),
                                sim::EventRegion::Observed),
      drain);

  setCurrent(drain);
  Value cohort = sim::SimClockOccurrenceConsumeOp::create(
      builder, location, i64, context,
      builder.getI64IntegerAttr(occurrenceSite));
  Value hasCohort = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ne, cohort, zero64);
  cf::CondBranchOp::create(builder, location, hasCohort, process, ValueRange{},
                           slotFinal ? slotFinalize : wait, ValueRange{});

  setCurrent(process);
  auto occurred = [&](Value mask) {
    Value selected = arith::AndIOp::create(builder, location, cohort, mask);
    return Value(arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, selected, zero64));
  };
  Value event0Occurred = occurred(one64);
  Value event1Occurred = occurred(two64);
  auto occurrenceMask = [&](unsigned bit) {
    return Value(arith::ConstantOp::create(
        builder, location, i64,
        builder.getI64IntegerAttr(static_cast<int64_t>(uint64_t{1} << bit))));
  };
  Value rawEvent0Occurred =
      skewWithMode ? occurred(occurrenceMask(rawEventBits[0])) : event0Occurred;
  Value rawEvent1Occurred =
      skewWithMode ? occurred(occurrenceMask(rawEventBits[1])) : event1Occurred;
  Value timerFired =
      emitTimerMode ? occurred(occurrenceMask(timerEventBit)) : Value{};
  Value event0Count =
      arith::SelectOp::create(builder, location, event0Occurred, one64, zero64);
  Value event1Count =
      arith::SelectOp::create(builder, location, event1Occurred, one64, zero64);
  if (noChange) {
    // IEEE 1800-2017 31.4.6 derives the trailing reference occurrence from
    // the opposite standard edge and judges both open endpoints only after
    // the complete numeric slot is known. The specialized state is the sole
    // storage needed for retroactive positive-start data and deferred
    // negative-end data; no generic timing interpreter or timer is emitted.
    sim::SimNoChangeUpdateOp::create(builder, location, i64, context, cohort,
                                     noChangeStart, noChangeEnd,
                                     builder.getI64IntegerAttr(occurrenceSite));
    cf::BranchOp::create(builder, location, drain, ValueRange{});
  } else {
    if (dynamicMode) {
      // IEEE 1800-2017 31.4.2/.3 make event_based_flag an invariant mode
      // selection.  Branch on its one entry evaluation; the selected existing
      // state machine alone mutates timing-check state for every cohort.
      cf::CondBranchOp::create(builder, location, eventModeValue, eventProcess,
                               ValueRange{}, timerProcess, ValueRange{});
      setCurrent(eventProcess);
    }
    if (emitEventMode) {
      Value processNow =
          sim::SimTimeNowOp::create(builder, location, i64, context);
      Value processPrevious =
          sim::SimRefLoadOp::create(builder, location, i64, timestamp);
      Value processValid =
          sim::SimRefLoadOp::create(builder, location, i1, timestampValid);
      Value processDelta =
          arith::SubIOp::create(builder, location, processNow, processPrevious);
      Value processViolation;
      Value processReportCount;

      if (timeSkew) {
        Value limit = arith::ConstantOp::create(
            builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
        Value beyondLimit = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ugt, processDelta, limit);
        Value noReference =
            arith::XOrIOp::create(builder, location, event0Occurred, trueValue);
        Value falseReference = arith::AndIOp::create(
            builder, location, rawEvent0Occurred, noReference);
        processViolation = arith::AndIOp::create(builder, location,
                                                 event1Occurred, processValid);
        processViolation = arith::AndIOp::create(builder, location,
                                                 processViolation, beyondLimit);
        processViolation = arith::AndIOp::create(builder, location,
                                                 processViolation, noReference);
        if (!remainActive || !*remainActive) {
          Value noFalseReference = arith::XOrIOp::create(
              builder, location, falseReference, trueValue);
          if (!remainActive)
            noFalseReference = arith::OrIOp::create(
                builder, location, noFalseReference, remainActiveValue);
          processViolation = arith::AndIOp::create(
              builder, location, processViolation, noFalseReference);
        }
        Value reports = remainActive ? (*remainActive ? event1Count : one64)
                                     : Value(arith::SelectOp::create(
                                           builder, location, remainActiveValue,
                                           event1Count, one64));
        processReportCount = arith::SelectOp::create(
            builder, location, processViolation, reports, zero64);

        Value nextTimestamp = arith::SelectOp::create(
            builder, location, event0Occurred, processNow, processPrevious);
        Value nextValid = processValid;
        if (!remainActive || !*remainActive) {
          // IEEE 1800-2017 31.4.2 makes each false conditioned reference and
          // first violation take effect in occurrence order. Folding here, as
          // ordered cohorts are consumed, distinguishes true-then-false from
          // false-then-true references at one numeric simulation time.
          Value deactivate = arith::OrIOp::create(
              builder, location, processViolation, falseReference);
          if (!remainActive) {
            Value notRemain = arith::XOrIOp::create(
                builder, location, remainActiveValue, trueValue);
            deactivate =
                arith::AndIOp::create(builder, location, deactivate, notRemain);
          }
          nextValid = arith::SelectOp::create(builder, location, deactivate,
                                              falseValue, nextValid);
        }
        nextValid = arith::SelectOp::create(builder, location, event0Occurred,
                                            trueValue, nextValid);
        sim::SimRefStoreOp::create(builder, location, nextTimestamp, timestamp);
        sim::SimRefStoreOp::create(builder, location, nextValid,
                                   timestampValid);
      } else {
        Value direction = sim::SimRefLoadOp::create(builder, location, i1,
                                                    timestampDirection);
        Value limit0 = arith::ConstantOp::create(
            builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
        Value limit1 = arith::ConstantOp::create(
            builder, location, i64, builder.getI64IntegerAttr(ticks[3]));
        Value limit = arith::SelectOp::create(builder, location, direction,
                                              limit1, limit0);
        Value beyondLimit = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ugt, processDelta, limit);
        Value notEvent0 =
            arith::XOrIOp::create(builder, location, event0Occurred, trueValue);
        Value notEvent1 =
            arith::XOrIOp::create(builder, location, event1Occurred, trueValue);
        Value falseEvent0 = arith::AndIOp::create(builder, location,
                                                  rawEvent0Occurred, notEvent0);
        Value falseEvent1 = arith::AndIOp::create(builder, location,
                                                  rawEvent1Occurred, notEvent1);
        Value effectiveValid = processValid;
        if (!remainActive || !*remainActive) {
          Value direction0 =
              arith::XOrIOp::create(builder, location, direction, trueValue);
          Value falseTimestamp0 =
              arith::AndIOp::create(builder, location, falseEvent0, direction0);
          Value falseTimestamp1 =
              arith::AndIOp::create(builder, location, falseEvent1, direction);
          Value falseTimestamp = arith::OrIOp::create(
              builder, location, falseTimestamp0, falseTimestamp1);
          falseTimestamp = arith::AndIOp::create(builder, location,
                                                 falseTimestamp, processValid);
          if (!remainActive) {
            Value notRemain = arith::XOrIOp::create(
                builder, location, remainActiveValue, trueValue);
            falseTimestamp = arith::AndIOp::create(builder, location,
                                                   falseTimestamp, notRemain);
          }
          effectiveValid = arith::SelectOp::create(
              builder, location, falseTimestamp, falseValue, processValid);
        }

        Value both = arith::AndIOp::create(builder, location, event0Occurred,
                                           event1Occurred);
        Value any = arith::OrIOp::create(builder, location, event0Occurred,
                                         event1Occurred);
        Value notBoth =
            arith::XOrIOp::create(builder, location, both, trueValue);
        Value oneEvent = arith::AndIOp::create(builder, location, any, notBoth);
        Value incomingDirection = event1Occurred;
        Value directionChanged = arith::XOrIOp::create(
            builder, location, direction, incomingDirection);
        Value opposite =
            arith::AndIOp::create(builder, location, oneEvent, effectiveValid);
        opposite = arith::AndIOp::create(builder, location, opposite,
                                         directionChanged);
        processViolation =
            arith::AndIOp::create(builder, location, opposite, beyondLimit);
        processReportCount = arith::SelectOp::create(
            builder, location, processViolation, one64, zero64);

        Value notValid =
            arith::XOrIOp::create(builder, location, effectiveValid, trueValue);
        Value sameDirection = arith::XOrIOp::create(
            builder, location, directionChanged, trueValue);
        Value timestampReason =
            arith::OrIOp::create(builder, location, notValid, sameDirection);
        timestampReason = arith::OrIOp::create(
            builder, location, timestampReason, processViolation);
        Value becomesTimestamp =
            arith::AndIOp::create(builder, location, oneEvent, timestampReason);
        Value notBeyond =
            arith::XOrIOp::create(builder, location, beyondLimit, trueValue);
        Value completedWithin =
            arith::AndIOp::create(builder, location, opposite, notBeyond);
        Value nextTimestamp = arith::SelectOp::create(
            builder, location, becomesTimestamp, processNow, processPrevious);
        Value nextDirection = arith::SelectOp::create(
            builder, location, becomesTimestamp, incomingDirection, direction);
        Value nextValid = arith::SelectOp::create(
            builder, location, completedWithin, falseValue, effectiveValid);
        nextValid = arith::SelectOp::create(builder, location, becomesTimestamp,
                                            trueValue, nextValid);
        nextValid = arith::SelectOp::create(builder, location, both, falseValue,
                                            nextValid);
        // IEEE 1800-2017 31.4.3 assigns each accepted event its timestamp or
        // timecheck role from the preceding ordered state. Retain direction in
        // actor-local SSA storage so a later false event in the same slot acts
        // on the actual last timestamp direction, not an aggregate XOR.
        sim::SimRefStoreOp::create(builder, location, nextTimestamp, timestamp);
        sim::SimRefStoreOp::create(builder, location, nextDirection,
                                   timestampDirection);
        sim::SimRefStoreOp::create(builder, location, nextValid,
                                   timestampValid);
      }

      Value accumulated = sim::SimRefLoadOp::create(builder, location, i64,
                                                    slotEventViolationCount);
      accumulated = arith::AddIOp::create(builder, location, accumulated,
                                          processReportCount);
      sim::SimRefStoreOp::create(builder, location, accumulated,
                                 slotEventViolationCount);
      if (dynamicMode)
        cf::BranchOp::create(builder, location, processJoin, ValueRange{});
    }
    if (emitTimerMode) {
      if (dynamicMode)
        setCurrent(timerProcess);
      Value priorTimerFired =
          sim::SimRefLoadOp::create(builder, location, i1, slotTimerFired);
      Value sawTimer =
          arith::OrIOp::create(builder, location, priorTimerFired, timerFired);
      sim::SimRefStoreOp::create(builder, location, sawTimer, slotTimerFired);
      Value processNow =
          sim::SimTimeNowOp::create(builder, location, i64, context);
      Value processPrevious =
          sim::SimRefLoadOp::create(builder, location, i64, timestamp);
      Value processValid =
          sim::SimRefLoadOp::create(builder, location, i1, timestampValid);
      Value processDelta =
          arith::SubIOp::create(builder, location, processNow, processPrevious);
      Value previousRestart =
          sim::SimRefLoadOp::create(builder, location, i1, slotTimerRestart);
      Value previousCancel =
          sim::SimRefLoadOp::create(builder, location, i1, slotTimerCancel);
      Value restart;
      Value cancel;

      if (timeSkew) {
        Value noReference =
            arith::XOrIOp::create(builder, location, event0Occurred, trueValue);
        Value falseReference = arith::AndIOp::create(
            builder, location, rawEvent0Occurred, noReference);
        Value nextTimestamp = arith::SelectOp::create(
            builder, location, event0Occurred, processNow, processPrevious);
        Value nextValid = processValid;
        if (!remainActive || !*remainActive) {
          Value deactivate = falseReference;
          if (!remainActive) {
            Value notRemain = arith::XOrIOp::create(
                builder, location, remainActiveValue, trueValue);
            deactivate =
                arith::AndIOp::create(builder, location, deactivate, notRemain);
          }
          nextValid = arith::SelectOp::create(builder, location, deactivate,
                                              falseValue, nextValid);
        }
        nextValid = arith::SelectOp::create(builder, location, event0Occurred,
                                            trueValue, nextValid);
        Value effectiveDelta = arith::SelectOp::create(
            builder, location, event0Occurred, zero64, processDelta);
        Value limit = arith::ConstantOp::create(
            builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
        Value withinLimit =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ule,
                                  effectiveDelta, limit);
        Value completes =
            arith::AndIOp::create(builder, location, event1Occurred, nextValid);
        completes =
            arith::AndIOp::create(builder, location, completes, withinLimit);
        nextValid = arith::SelectOp::create(builder, location, completes,
                                            falseValue, nextValid);
        restart = arith::OrIOp::create(builder, location, previousRestart,
                                       event0Occurred);
        cancel = completes;
        if (!remainActive || !*remainActive) {
          Value dormant = arith::AndIOp::create(builder, location,
                                                falseReference, processValid);
          if (!remainActive) {
            Value notRemain = arith::XOrIOp::create(
                builder, location, remainActiveValue, trueValue);
            dormant =
                arith::AndIOp::create(builder, location, dormant, notRemain);
          }
          cancel = arith::OrIOp::create(builder, location, cancel, dormant);
        }
        // IEEE 1800-2017 31.4.2 starts or replaces the timer on every true
        // reference, cancels on an in-limit data event, and makes a false
        // conditioned reference dormant unless remain_active says to ignore it.
        // Ordered cohort folding preserves the last same-slot qualification.
        sim::SimRefStoreOp::create(builder, location, nextTimestamp, timestamp);
        sim::SimRefStoreOp::create(builder, location, nextValid,
                                   timestampValid);
      } else {
        Value direction = sim::SimRefLoadOp::create(builder, location, i1,
                                                    timestampDirection);
        Value notEvent0 =
            arith::XOrIOp::create(builder, location, event0Occurred, trueValue);
        Value notEvent1 =
            arith::XOrIOp::create(builder, location, event1Occurred, trueValue);
        Value falseEvent0 = arith::AndIOp::create(builder, location,
                                                  rawEvent0Occurred, notEvent0);
        Value falseEvent1 = arith::AndIOp::create(builder, location,
                                                  rawEvent1Occurred, notEvent1);
        Value effectiveValid = processValid;
        Value falseTimestamp = falseValue;
        if (!remainActive || !*remainActive) {
          Value direction0 =
              arith::XOrIOp::create(builder, location, direction, trueValue);
          Value falseTimestamp0 =
              arith::AndIOp::create(builder, location, falseEvent0, direction0);
          Value falseTimestamp1 =
              arith::AndIOp::create(builder, location, falseEvent1, direction);
          falseTimestamp = arith::OrIOp::create(
              builder, location, falseTimestamp0, falseTimestamp1);
          falseTimestamp = arith::AndIOp::create(builder, location,
                                                 falseTimestamp, processValid);
          if (!remainActive) {
            Value notRemain = arith::XOrIOp::create(
                builder, location, remainActiveValue, trueValue);
            falseTimestamp = arith::AndIOp::create(builder, location,
                                                   falseTimestamp, notRemain);
          }
          effectiveValid = arith::SelectOp::create(
              builder, location, falseTimestamp, falseValue, processValid);
        }
        Value both = arith::AndIOp::create(builder, location, event0Occurred,
                                           event1Occurred);
        Value any = arith::OrIOp::create(builder, location, event0Occurred,
                                         event1Occurred);
        Value notBoth =
            arith::XOrIOp::create(builder, location, both, trueValue);
        Value oneEvent = arith::AndIOp::create(builder, location, any, notBoth);
        Value incomingDirection = event1Occurred;
        Value directionChanged = arith::XOrIOp::create(
            builder, location, direction, incomingDirection);
        Value opposite =
            arith::AndIOp::create(builder, location, oneEvent, effectiveValid);
        opposite = arith::AndIOp::create(builder, location, opposite,
                                         directionChanged);
        Value limit0 = arith::ConstantOp::create(
            builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
        Value limit1 = arith::ConstantOp::create(
            builder, location, i64, builder.getI64IntegerAttr(ticks[3]));
        Value limit = arith::SelectOp::create(builder, location, direction,
                                              limit1, limit0);
        Value withinLimit = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ule, processDelta, limit);
        Value completedWithin =
            arith::AndIOp::create(builder, location, opposite, withinLimit);
        Value notValid =
            arith::XOrIOp::create(builder, location, effectiveValid, trueValue);
        Value sameDirection = arith::XOrIOp::create(
            builder, location, directionChanged, trueValue);
        Value notWithin =
            arith::XOrIOp::create(builder, location, withinLimit, trueValue);
        Value expiredOpposite =
            arith::AndIOp::create(builder, location, opposite, notWithin);
        Value timestampReason =
            arith::OrIOp::create(builder, location, notValid, sameDirection);
        timestampReason = arith::OrIOp::create(
            builder, location, timestampReason, expiredOpposite);
        Value becomesTimestamp =
            arith::AndIOp::create(builder, location, oneEvent, timestampReason);
        Value nextTimestamp = arith::SelectOp::create(
            builder, location, becomesTimestamp, processNow, processPrevious);
        Value nextDirection = arith::SelectOp::create(
            builder, location, becomesTimestamp, incomingDirection, direction);
        Value nextValid = arith::SelectOp::create(
            builder, location, completedWithin, falseValue, effectiveValid);
        nextValid = arith::SelectOp::create(builder, location, becomesTimestamp,
                                            trueValue, nextValid);
        nextValid = arith::SelectOp::create(builder, location, both, falseValue,
                                            nextValid);
        restart = arith::OrIOp::create(builder, location, previousRestart,
                                       becomesTimestamp);
        cancel = arith::OrIOp::create(builder, location, completedWithin, both);
        cancel =
            arith::OrIOp::create(builder, location, cancel, falseTimestamp);
        // IEEE 1800-2017 31.4.3 assigns each occurrence its directional role
        // before the timer is armed at slot finalization. Same-direction events
        // restart, an in-limit opposite event cancels, and false timestamp
        // conditions obey remain_active without allocating a runtime table.
        sim::SimRefStoreOp::create(builder, location, nextTimestamp, timestamp);
        sim::SimRefStoreOp::create(builder, location, nextDirection,
                                   timestampDirection);
        sim::SimRefStoreOp::create(builder, location, nextValid,
                                   timestampValid);
      }
      sim::SimRefStoreOp::create(builder, location, restart, slotTimerRestart);
      cancel = arith::OrIOp::create(builder, location, previousCancel, cancel);
      sim::SimRefStoreOp::create(builder, location, cancel, slotTimerCancel);
      if (dynamicMode)
        cf::BranchOp::create(builder, location, processJoin, ValueRange{});
    }
    if (dynamicMode)
      setCurrent(processJoin);
  }
  if (slotFinal && !noChange) {
    Value accumulated0 =
        sim::SimRefLoadOp::create(builder, location, i64, slotEvent0Count);
    Value accumulated1 =
        sim::SimRefLoadOp::create(builder, location, i64, slotEvent1Count);
    accumulated0 =
        arith::AddIOp::create(builder, location, accumulated0, event0Count);
    accumulated1 =
        arith::AddIOp::create(builder, location, accumulated1, event1Count);
    sim::SimRefStoreOp::create(builder, location, accumulated0,
                               slotEvent0Count);
    sim::SimRefStoreOp::create(builder, location, accumulated1,
                               slotEvent1Count);
    cf::BranchOp::create(builder, location, drain, ValueRange{});

    setCurrent(slotFinalize);
    event0Count =
        sim::SimRefLoadOp::create(builder, location, i64, slotEvent0Count);
    event1Count =
        sim::SimRefLoadOp::create(builder, location, i64, slotEvent1Count);
    event0Occurred = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, event0Count, zero64);
    event1Occurred = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, event1Count, zero64);
  }
  if (noChange)
    setCurrent(slotFinalize);
  Value now;
  Value previous;
  Value valid;
  if (!skewWithMode && !noChange) {
    now = sim::SimTimeNowOp::create(builder, location, i64, context);
    previous = sim::SimRefLoadOp::create(builder, location, i64, timestamp);
    valid = sim::SimRefLoadOp::create(builder, location, i1, timestampValid);
  }

  Value qualified;
  Value baseReportCount = one64;
  if (noChange) {
    baseReportCount = sim::SimNoChangeUpdateOp::create(
        builder, location, i64, context, zero64, noChangeStart, noChangeEnd,
        builder.getI64IntegerAttr(occurrenceSite));
    qualified = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, baseReportCount, zero64);
  } else if (combined) {
    bool negativeAdjusted =
        function->hasAttr("simulation.negative_timing_adjusted");
    bool sharedTimestampIsInterior = ticks[2] > 0 && ticks[3] > 0;
    Value previous1 =
        sim::SimRefLoadOp::create(builder, location, i64, oppositeTimestamp);
    Value valid1 = sim::SimRefLoadOp::create(builder, location, i1,
                                             oppositeTimestampValid);
    auto checkOpposite = [&](Value checkOccurred, Value timestampOccurred,
                             Value priorTimestamp, Value priorValid,
                             int64_t limitTicks) {
      Value effectiveTimestamp = arith::SelectOp::create(
          builder, location, timestampOccurred, now, priorTimestamp);
      Value effectiveValid = arith::OrIOp::create(builder, location, priorValid,
                                                  timestampOccurred);
      Value delta =
          arith::SubIOp::create(builder, location, now, effectiveTimestamp);
      Value limit = arith::ConstantOp::create(
          builder, location, i64, builder.getI64IntegerAttr(limitTicks));
      Value positiveLimit = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, limit, zero64);
      Value inWindow = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ult, delta, limit);
      if (negativeAdjusted && !sharedTimestampIsInterior) {
        // IEEE 1800-2017 31.9.1 excludes both original window endpoints.
        // Strict delay solving makes a same-tick delayed pair an interior
        // point only while both adjusted sides remain positive.  A repaired
        // or clamped zero side instead places delta zero on the endpoint.
        Value nonzeroDelta = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, delta, zero64);
        inWindow =
            arith::AndIOp::create(builder, location, inWindow, nonzeroDelta);
      }
      Value result = arith::AndIOp::create(builder, location, checkOccurred,
                                           effectiveValid);
      result = arith::AndIOp::create(builder, location, result, positiveLimit);
      return Value(arith::AndIOp::create(builder, location, result, inWindow));
    };
    // IEEE 1800-2017 31.3.3/.6 make whichever event occurs second the
    // timecheck event. For $setuphold, reference event 0 checks setup slot 2
    // and data event 1 checks hold slot 3. For $recrem, data event 1 checks
    // recovery slot 2 and reference event 0 checks removal slot 3. Both
    // regions include their shared timestamp endpoint and exclude only the
    // limit endpoint. OR the directions before the cold path so one
    // simultaneous cohort can toggle the Clause 31.6 notifier at most once.
    int64_t event1Limit = kind == 3 ? ticks[3] : ticks[2];
    int64_t event0Limit = kind == 3 ? ticks[2] : ticks[3];
    Value event1Violation = checkOpposite(event1Occurred, event0Occurred,
                                          previous, valid, event1Limit);
    Value event0Violation = checkOpposite(event0Occurred, event1Occurred,
                                          previous1, valid1, event0Limit);
    qualified = arith::OrIOp::create(builder, location, event1Violation,
                                     event0Violation);
    Value event1Reports = arith::SelectOp::create(
        builder, location, event1Violation, event1Count, zero64);
    Value event0Reports = arith::SelectOp::create(
        builder, location, event0Violation, event0Count, zero64);
    Value event1HasMore =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ugt,
                              event1Reports, event0Reports);
    // IEEE 1800-2017 31.3.3/.6 define one combined check whose timecheck
    // direction depends on which event occurs first. Pair same-slot opposite
    // occurrences by ordinal and report only the larger qualifying count;
    // one simultaneous pair therefore toggles the notifier exactly once.
    baseReportCount = arith::SelectOp::create(builder, location, event1HasMore,
                                              event1Reports, event0Reports);
    Value nextTimestamp0 = arith::SelectOp::create(
        builder, location, event0Occurred, now, previous);
    Value nextValid0 =
        arith::OrIOp::create(builder, location, valid, event0Occurred);
    Value nextTimestamp1 = arith::SelectOp::create(
        builder, location, event1Occurred, now, previous1);
    Value nextValid1 =
        arith::OrIOp::create(builder, location, valid1, event1Occurred);
    sim::SimRefStoreOp::create(builder, location, nextTimestamp0, timestamp);
    sim::SimRefStoreOp::create(builder, location, nextValid0, timestampValid);
    sim::SimRefStoreOp::create(builder, location, nextTimestamp1,
                               oppositeTimestamp);
    sim::SimRefStoreOp::create(builder, location, nextValid1,
                               oppositeTimestampValid);
  } else if (skewWithMode) {
    if (dynamicMode) {
      cf::CondBranchOp::create(builder, location, eventModeValue, eventFinalize,
                               ValueRange{}, timerFinalize, ValueRange{});
      setCurrent(timerFinalize);
    }
    if (emitTimerMode) {
      Value both = arith::AndIOp::create(builder, location, event0Occurred,
                                         event1Occurred);
      Value finalizedValid =
          sim::SimRefLoadOp::create(builder, location, i1, timestampValid);
      finalizedValid = arith::SelectOp::create(builder, location, both,
                                               falseValue, finalizedValid);
      sim::SimRefStoreOp::create(builder, location, finalizedValid,
                                 timestampValid);
      Value restart =
          sim::SimRefLoadOp::create(builder, location, i1, slotTimerRestart);
      Value arm =
          arith::AndIOp::create(builder, location, finalizedValid, restart);
      cf::CondBranchOp::create(builder, location, arm, timerSchedule,
                               ValueRange{}, timerMaybeCancel, ValueRange{});

      setCurrent(timerSchedule);
      Value delayTicks;
      if (timeSkew) {
        delayTicks = arith::ConstantOp::create(
            builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
      } else {
        Value direction = sim::SimRefLoadOp::create(builder, location, i1,
                                                    timestampDirection);
        Value limit0 = arith::ConstantOp::create(
            builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
        Value limit1 = arith::ConstantOp::create(
            builder, location, i64, builder.getI64IntegerAttr(ticks[3]));
        delayTicks = arith::SelectOp::create(builder, location, direction,
                                             limit1, limit0);
      }
      Value armNow = sim::SimTimeNowOp::create(builder, location, i64, context);
      Value wrappedDeadline =
          arith::AddIOp::create(builder, location, armNow, delayTicks);
      Value deadlineWrapped =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                                wrappedDeadline, armNow);
      Value permanentDeadline = arith::ConstantOp::create(
          builder, location, i64, builder.getI64IntegerAttr(-1));
      Value deadline =
          arith::SelectOp::create(builder, location, deadlineWrapped,
                                  permanentDeadline, wrappedDeadline);
      sim::SimRefStoreOp::create(builder, location, deadline, timerDeadline);
      Value timerDelay = sim::SimTimeScaleOp::create(
          builder, location, sim::TimeType::get(function.getContext()),
          delayTicks, builder.getI64IntegerAttr(1), builder.getBoolAttr(false));
      // IEEE 1800-2017 31.4.2/.3 replace one logical timer on every restart.
      // The cold indexed calendar owns at most one delayed Re-NBA event per
      // private identity; saturation matches the scheduler, so overflow becomes
      // a permanent UINT64_MAX deadline rather than an early wrapped report.
      sim::SimEventTriggerOp::create(
          builder, location, timerEvent, timerDelay, builder.getBoolAttr(true),
          schedule::EventSiteAttr{}, builder.getUnitAttr());
      cf::BranchOp::create(builder, location, timerContinue,
                           ValueRange{trueValue});

      setCurrent(timerMaybeCancel);
      Value cancel =
          sim::SimRefLoadOp::create(builder, location, i1, slotTimerCancel);
      cf::CondBranchOp::create(builder, location, cancel, timerCancel,
                               ValueRange{}, timerContinue,
                               ValueRange{falseValue});

      setCurrent(timerCancel);
      sim::SimEventTriggerOp::create(
          builder, location, timerEvent, Value{}, builder.getBoolAttr(true),
          schedule::EventSiteAttr{}, builder.getUnitAttr());
      cf::BranchOp::create(builder, location, timerContinue,
                           ValueRange{falseValue});

      setCurrent(timerContinue);
      Value armedThisSlot = timerContinue->getArgument(0);
      Value fired =
          sim::SimRefLoadOp::create(builder, location, i1, slotTimerFired);
      Value active =
          sim::SimRefLoadOp::create(builder, location, i1, timestampValid);
      Value currentDeadline =
          sim::SimRefLoadOp::create(builder, location, i64, timerDeadline);
      Value expiryNow =
          sim::SimTimeNowOp::create(builder, location, i64, context);
      Value atDeadline =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                expiryNow, currentDeadline);
      Value notArmed =
          arith::XOrIOp::create(builder, location, armedThisSlot, trueValue);
      qualified = arith::AndIOp::create(builder, location, fired, active);
      qualified =
          arith::AndIOp::create(builder, location, qualified, atDeadline);
      qualified = arith::AndIOp::create(builder, location, qualified, notArmed);
      Value nextActive = arith::SelectOp::create(builder, location, qualified,
                                                 falseValue, active);
      sim::SimRefStoreOp::create(builder, location, nextActive, timestampValid);
      baseReportCount = one64;
      if (dynamicMode)
        cf::BranchOp::create(builder, location, modeFinalizeJoin,
                             ValueRange{qualified, baseReportCount});
    }
    if (emitEventMode) {
      if (dynamicMode)
        setCurrent(eventFinalize);
      if (timeSkew) {
        baseReportCount = sim::SimRefLoadOp::create(builder, location, i64,
                                                    slotEventViolationCount);
        Value simultaneous = arith::AndIOp::create(
            builder, location, event0Occurred, event1Occurred);
        Value notSimultaneous =
            arith::XOrIOp::create(builder, location, simultaneous, trueValue);
        qualified =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  baseReportCount, zero64);
        qualified = arith::AndIOp::create(builder, location, qualified,
                                          notSimultaneous);
      } else {
        Value both = arith::AndIOp::create(builder, location, event0Occurred,
                                           event1Occurred);
        baseReportCount = sim::SimRefLoadOp::create(builder, location, i64,
                                                    slotEventViolationCount);
        Value notBoth =
            arith::XOrIOp::create(builder, location, both, trueValue);
        qualified =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  baseReportCount, zero64);
        qualified =
            arith::AndIOp::create(builder, location, qualified, notBoth);
        // IEEE 1800-2017 31.4.3 defines opposite events at one numeric time as
        // simultaneous even when scheduler producer regions publish distinct
        // ordered cohorts. They report nothing and leave the check dormant.
        Value finalizedValid =
            sim::SimRefLoadOp::create(builder, location, i1, timestampValid);
        finalizedValid = arith::SelectOp::create(builder, location, both,
                                                 falseValue, finalizedValid);
        sim::SimRefStoreOp::create(builder, location, finalizedValid,
                                   timestampValid);
      }
      if (dynamicMode)
        cf::BranchOp::create(builder, location, modeFinalizeJoin,
                             ValueRange{qualified, baseReportCount});
    }
    if (dynamicMode) {
      setCurrent(modeFinalizeJoin);
      qualified = modeFinalizeJoin->getArgument(0);
      baseReportCount = modeFinalizeJoin->getArgument(1);
    }
  } else if (!skew && !period && !width) {
    // IEEE 1800-2017 31.3.1/.4 use an open setup/removal window, while
    // 31.3.2/.5 include the hold/recovery timestamp endpoint. Keep the two
    // lattices explicit: simultaneous events do not violate setup/removal but
    // do violate a positive hold/recovery check.
    bool setupStyle = kind == 1 || kind == 5;
    unsigned timestampEvent = kind == 5 ? 1 : 0;
    Value timestampOccurred = timestampEvent ? event1Occurred : event0Occurred;
    Value checkOccurred = timestampEvent ? event0Occurred : event1Occurred;
    Value effectiveTimestamp =
        setupStyle ? previous
                   : Value(arith::SelectOp::create(
                         builder, location, timestampOccurred, now, previous));
    Value effectiveValid =
        setupStyle ? valid
                   : Value(arith::OrIOp::create(builder, location, valid,
                                                timestampOccurred));
    Value delta =
        arith::SubIOp::create(builder, location, now, effectiveTimestamp);
    Value limit = arith::ConstantOp::create(
        builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
    Value positiveLimit = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, limit, zero64);
    Value inWindow = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, delta, limit);
    qualified =
        arith::AndIOp::create(builder, location, checkOccurred, effectiveValid);
    qualified =
        arith::AndIOp::create(builder, location, qualified, positiveLimit);
    qualified = arith::AndIOp::create(builder, location, qualified, inWindow);
    baseReportCount = timestampEvent ? event0Count : event1Count;
    if (setupStyle) {
      Value notSimultaneous = arith::XOrIOp::create(
          builder, location, timestampOccurred,
          arith::ConstantOp::create(builder, location, i1,
                                    builder.getBoolAttr(true)));
      Value nonzeroDelta = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne, delta, zero64);
      qualified =
          arith::AndIOp::create(builder, location, qualified, notSimultaneous);
      qualified =
          arith::AndIOp::create(builder, location, qualified, nonzeroDelta);
    }
    Value nextTimestamp = arith::SelectOp::create(
        builder, location, timestampOccurred, now, previous);
    Value nextValid =
        arith::OrIOp::create(builder, location, valid, timestampOccurred);
    sim::SimRefStoreOp::create(builder, location, nextTimestamp, timestamp);
    sim::SimRefStoreOp::create(builder, location, nextValid, timestampValid);
  } else if (skew) {
    Value delta = arith::SubIOp::create(builder, location, now, previous);
    Value limit = arith::ConstantOp::create(
        builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
    Value beyondLimit = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ugt, delta, limit);
    Value noReference = arith::XOrIOp::create(
        builder, location, event0Occurred,
        arith::ConstantOp::create(builder, location, i1,
                                  builder.getBoolAttr(true)));
    qualified = arith::AndIOp::create(builder, location, event1Occurred, valid);
    qualified =
        arith::AndIOp::create(builder, location, qualified, beyondLimit);
    qualified =
        arith::AndIOp::create(builder, location, qualified, noReference);
    baseReportCount = event1Count;
    // IEEE 1800-2017 31.4.1 excludes every data transition at a numeric time
    // containing a reference transition, including Reactive, Re-Inactive, or
    // Re-NBA producers. The slot-final wait supplies complete counts before
    // this strict comparison; every genuinely later data occurrence remains
    // a distinct report and a new reference replaces the old timestamp.
    Value nextTimestamp = arith::SelectOp::create(
        builder, location, event0Occurred, now, previous);
    Value nextValid =
        arith::OrIOp::create(builder, location, valid, event0Occurred);
    sim::SimRefStoreOp::create(builder, location, nextTimestamp, timestamp);
    sim::SimRefStoreOp::create(builder, location, nextValid, timestampValid);
  } else if (period) {
    Value delta = arith::SubIOp::create(builder, location, now, previous);
    Value limit = arith::ConstantOp::create(
        builder, location, i64, builder.getI64IntegerAttr(ticks[1]));
    Value tooShort = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, delta, limit);
    qualified = arith::AndIOp::create(builder, location, event0Occurred, valid);
    qualified = arith::AndIOp::create(builder, location, qualified, tooShort);
    // IEEE 1800-2017 31.4.5 derives the timecheck event from the same edge as
    // the timestamp event. Every occurrence checks the previous timestamp
    // and then becomes the next one; the strict endpoint makes limit zero
    // nonviolating without a special runtime path.
    Value nextTimestamp = arith::SelectOp::create(
        builder, location, event0Occurred, now, previous);
    Value nextValid =
        arith::OrIOp::create(builder, location, valid, event0Occurred);
    sim::SimRefStoreOp::create(builder, location, nextTimestamp, timestamp);
    sim::SimRefStoreOp::create(builder, location, nextValid, timestampValid);
  } else {
    Value delta = arith::SubIOp::create(builder, location, now, previous);
    Value threshold = arith::ConstantOp::create(
        builder, location, i64,
        builder.getI64IntegerAttr(ticks.size() > 2 ? ticks[2] : 0));
    Value limit = arith::ConstantOp::create(
        builder, location, i64, builder.getI64IntegerAttr(ticks[1]));
    Value aboveThreshold = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ugt, delta, threshold);
    Value belowLimit = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, delta, limit);
    qualified = arith::AndIOp::create(builder, location, event1Occurred, valid);
    qualified =
        arith::AndIOp::create(builder, location, qualified, aboveThreshold);
    qualified = arith::AndIOp::create(builder, location, qualified, belowLimit);
    // IEEE 1800-2017 31.4.4 derives the timecheck from the opposite standard
    // edge and reports only for threshold < width < limit. The omitted
    // threshold is frozen as zero above; both endpoints remain nonviolating.
    Value nextTimestamp = arith::SelectOp::create(
        builder, location, event0Occurred, now, previous);
    Value opened =
        arith::OrIOp::create(builder, location, valid, event0Occurred);
    Value nextValid = arith::SelectOp::create(builder, location, event1Occurred,
                                              falseValue, opened);
    sim::SimRefStoreOp::create(builder, location, nextTimestamp, timestamp);
    sim::SimRefStoreOp::create(builder, location, nextValid, timestampValid);
  }
  Value reportCount = arith::SelectOp::create(builder, location, qualified,
                                              baseReportCount, zero64);
  Value hasReport = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ne, reportCount, zero64);
  cf::CondBranchOp::create(builder, location, hasReport, violation,
                           ValueRange{reportCount},
                           slotFinal ? slotReset : drain, ValueRange{});

  if (slotFinal) {
    setCurrent(slotReset);
    if (!noChange) {
      sim::SimRefStoreOp::create(builder, location, zero64, slotEvent0Count);
      sim::SimRefStoreOp::create(builder, location, zero64, slotEvent1Count);
    }
    if (emitEventMode) {
      sim::SimRefStoreOp::create(builder, location, zero64,
                                 slotEventViolationCount);
    }
    if (emitTimerMode)
      sim::SimRefStoreOp::create(builder, location, falseValue,
                                 slotTimerRestart);
    if (emitTimerMode)
      sim::SimRefStoreOp::create(builder, location, falseValue,
                                 slotTimerCancel);
    if (emitTimerMode)
      sim::SimRefStoreOp::create(builder, location, falseValue, slotTimerFired);
    cf::BranchOp::create(builder, location, wait, ValueRange{});
  }

  setCurrent(violation);
  Value remainingReports = violation->getArgument(0);
  if (notifier) {
    FailureOr<Value> old = loadCapturedLValue(*notifier, location);
    if (failed(old))
      return failure();
    FailureOr<Value> logic = toLogic(*old, location);
    if (failed(logic) ||
        cast<sim::LogicType>((*logic).getType()).getWidth() != 1)
      return function.emitError("timing-check notifier is not scalar logic");
    Type logic1 = sim::LogicType::get(function.getContext(), 1);
    auto logicConstant = [&](bool value, bool unknown) {
      return Value(sim::SimLogicConstantOp::create(
          builder, location, logic1,
          builder.getIntegerAttr(i1, static_cast<int64_t>(value)),
          builder.getIntegerAttr(i1, static_cast<int64_t>(unknown))));
    };
    Value zero = logicConstant(false, false);
    Value one = logicConstant(true, false);
    Value highImpedance = logicConstant(true, true);
    Value isZero = sim::SimLogicCompareOp::create(
        builder, location, i1, sim::CompareKind::CaseEq, *logic, zero);
    Value isHighImpedance = sim::SimLogicCompareOp::create(
        builder, location, i1, sim::CompareKind::CaseEq, *logic, highImpedance);
    // IEEE 1800-2017 31.6 toggles 0/1, preserves Z, and permits either
    // known result for X. Choose X->0 deterministically across all tiers.
    Value toggled =
        arith::SelectOp::create(builder, location, isZero, one, zero);
    Value next = arith::SelectOp::create(builder, location, isHighImpedance,
                                         highImpedance, toggled);
    if (failed(writeCapturedLValue(*notifier, next, false, false, location)))
      return failure();
  } else {
    // IEEE 1800-2017 31.6 permits the notifier to be omitted; violation
    // reporting still occurs. Keep this cold path nonfatal.
    Value descriptor = arith::ConstantOp::create(
        builder, location, builder.getI32Type(),
        builder.getI32IntegerAttr(static_cast<int32_t>(0x80000002u)));
    Value message = sim::SimBytesConstantOp::create(
        builder, location, "warning: system timing check violation");
    sim::SimDisplayOp::create(
        builder, location, context, descriptor, ValueRange{message}, true,
        sim::Radix::Decimal, builder.getDenseI32ArrayAttr({0}),
        function->getAttrOfType<StringAttr>(sim::metadata::hierarchicalName),
        StringAttr{}, function->getAttrOfType<IntegerAttr>(delayScaleAttrName),
        IntegerAttr{});
  }
  Value nextReports =
      arith::SubIOp::create(builder, location, remainingReports, one64);
  Value hasMoreReports = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ne, nextReports, zero64);
  cf::CondBranchOp::create(builder, location, hasMoreReports, violation,
                           ValueRange{nextReports},
                           slotFinal ? slotReset : drain, ValueRange{});
  return success();
}

} // namespace obelisk::simlowering
