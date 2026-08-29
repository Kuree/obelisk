//===- LowerUnitTimingChecks.cpp - Lower system timing checks ------------===//

#include "LowerUnit.h"

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
  auto edges = function->getAttrOfType<DenseI32ArrayAttr>(
      "timing_check_arg_effective_edges");
  auto ticks = function->getAttrOfType<DenseI64ArrayAttr>(
      "obelisk_sim.timing_check_arg_ticks");
  if (!kindAttr || !expressionChildren || !conditionChildren || !edges ||
      !ticks || expressionChildren.size() != conditionChildren.size() ||
      static_cast<size_t>(expressionChildren.size()) !=
          static_cast<size_t>(edges.size()) ||
      expressionChildren.size() != ticks.size())
    return function.emitError("basic timing check has a malformed frozen ABI");

  int32_t kind = static_cast<int32_t>(kindAttr.getInt());
  bool combined = kind == 3 || kind == 6;
  bool skew = kind == 7;
  bool period = kind == 10;
  bool width = kind == 11;
  bool slotFinal = kind >= 1 && kind <= 7;
  if (kind != 1 && kind != 2 && kind != 3 && kind != 4 && kind != 5 &&
      kind != 6 && !skew && !period && !width)
    return function.emitError("unsupported basic timing-check kind");
  size_t minimumArguments = combined ? 4 : period || width ? 2 : 3;
  if (static_cast<size_t>(expressionChildren.size()) < minimumArguments)
    return function.emitError("basic timing check has a malformed frozen ABI");
  auto childFor = [&](size_t index) -> Operation * {
    int64_t child = expressionChildren[index];
    return child >= 0 && static_cast<size_t>(child) < roots.size()
               ? roots[child]
               : nullptr;
  };
  Operation *event0 = childFor(0);
  Operation *event1 = period || width ? nullptr : childFor(1);
  size_t firstLimit = period || width ? 1 : 2;
  if (!event0 || (!(period || width) && !event1) || ticks[firstLimit] < 0 ||
      (combined && ticks[3] < 0) || (width && ticks.size() > 2 && ticks[2] < 0))
    return function.emitError(
        "basic timing check has no direct events or limit");

  SmallVector<Value, 2> handles;
  SmallVector<Value, 2> conditions;
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
    int32_t edge = edges[index];
    if (edge < static_cast<int32_t>(sim::EdgeKind::Change) ||
        edge > static_cast<int32_t>(sim::EdgeKind::Both))
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
    FailureOr<Value> condition = lowerExpression(roots[conditionChild], true);
    if (failed(condition) ||
        !isa<sim::RefType, sim::NetType, sim::DriverType>(
            succeeded(condition) ? (*condition).getType() : Type{}))
      return function.emitError(
          "basic timing-check condition is not a direct signal handle");

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
    if (*conditionWidth != 1) {
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
    // IEEE 1800-2017 31.7 samples only the condition's LSB at the event.
    // A bare condition enables only on a known one, exactly matching the
    // existing publication-time clock-condition handle semantics.
    conditionIndices[index] = static_cast<int32_t>(conditions.size());
    conditions.push_back(*condition);
    eventConditions[index] = *condition;
  }

  if (width) {
    int32_t edge = eventEdges.front();
    if (edge != static_cast<int32_t>(sim::EdgeKind::Posedge) &&
        edge != static_cast<int32_t>(sim::EdgeKind::Negedge))
      return function.emitError(
          "$width requires a canonical posedge or negedge event");
    handles.push_back(handles.front());
    eventEdges.push_back(edge == static_cast<int32_t>(sim::EdgeKind::Posedge)
                             ? static_cast<int32_t>(sim::EdgeKind::Negedge)
                             : static_cast<int32_t>(sim::EdgeKind::Posedge));
    conditionIndices.push_back(-1);
    if (eventConditions.front()) {
      // Each derived $width event samples the same Clause 31.7 condition at
      // publication time. Keep two ABI slots because a clock-set condition
      // belongs to exactly one primary, even when both slots name one handle.
      conditionIndices.back() = static_cast<int32_t>(conditions.size());
      conditions.push_back(eventConditions.front());
    }
  }

  std::optional<CapturedLValue> notifier;
  size_t notifierIndex = combined ? 4 : period ? 2 : 3;
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
  Value timestamp = sim::SimRefAllocOp::create(
      builder, location, sim::RefType::get(function.getContext(), i64), zero64);
  Value timestampValid = sim::SimRefAllocOp::create(
      builder, location, sim::RefType::get(function.getContext(), i1),
      falseValue);
  Value slotEvent0Count;
  Value slotEvent1Count;
  if (slotFinal) {
    slotEvent0Count = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i64),
        zero64);
    slotEvent1Count = sim::SimRefAllocOp::create(
        builder, location, sim::RefType::get(function.getContext(), i64),
        zero64);
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

  auto codeUnit = function->getAttrOfType<IntegerAttr>("code_unit_id");
  uint32_t occurrenceSite =
      codeUnit ? static_cast<uint32_t>(codeUnit.getValue().getZExtValue()) : 0;
  if (occurrenceSite == 0)
    occurrenceSite = 1;

  Block *wait = addBlock();
  Block *drain = addBlock();
  Block *process = addBlock();
  Block *slotFinalize = slotFinal ? addBlock() : nullptr;
  Block *slotReset = slotFinal ? addBlock() : nullptr;
  Block *violation = addBlock();
  violation->addArgument(i64, location);
  emitBranch(wait);

  setCurrent(wait);
  SmallVector<Value, 4> waitValues;
  llvm::append_range(waitValues, handles);
  llvm::append_range(waitValues, conditions);
  sim::SimSuspendClockSetOp::create(
      builder, location, waitValues,
      builder.getI32IntegerAttr(conditions.size()),
      builder.getDenseI32ArrayAttr(eventEdges),
      builder.getDenseI32ArrayAttr(conditionIndices),
      builder.getI64IntegerAttr(occurrenceSite),
      slotFinal ? builder.getUnitAttr() : UnitAttr{},
      sim::ContinuationSiteAttr{},
      sim::EventRegionAttr::get(function.getContext(),
                                sim::EventRegion::Observed),
      drain);

  setCurrent(drain);
  Value context = function.getBody().front().getArgument(0);
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
  Value event0Count =
      arith::SelectOp::create(builder, location, event0Occurred, one64, zero64);
  Value event1Count =
      arith::SelectOp::create(builder, location, event1Occurred, one64, zero64);
  if (slotFinal) {
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
  Value now = sim::SimTimeNowOp::create(builder, location, i64, context);
  Value previous = sim::SimRefLoadOp::create(builder, location, i64, timestamp);
  Value valid =
      sim::SimRefLoadOp::create(builder, location, i1, timestampValid);

  Value qualified;
  Value baseReportCount = one64;
  if (combined) {
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
    sim::SimRefStoreOp::create(builder, location, zero64, slotEvent0Count);
    sim::SimRefStoreOp::create(builder, location, zero64, slotEvent1Count);
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
        builder, location, context, descriptor, ValueRange{message}, true, 10,
        builder.getDenseI32ArrayAttr({0}),
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
