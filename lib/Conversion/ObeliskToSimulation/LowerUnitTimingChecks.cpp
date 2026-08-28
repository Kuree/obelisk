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
  auto edges = function->getAttrOfType<ArrayAttr>("timing_check_arg_edges");
  auto ticks = function->getAttrOfType<DenseI64ArrayAttr>(
      "obelisk_sim.timing_check_arg_ticks");
  if (!kindAttr || !expressionChildren || !edges || !ticks ||
      static_cast<size_t>(expressionChildren.size()) != edges.size() ||
      expressionChildren.size() != ticks.size() ||
      expressionChildren.size() < 3)
    return function.emitError("basic timing check has a malformed frozen ABI");

  int32_t kind = static_cast<int32_t>(kindAttr.getInt());
  if (kind != 1 && kind != 2 && kind != 4 && kind != 5)
    return function.emitError("unsupported basic timing-check kind");
  auto childFor = [&](size_t index) -> Operation * {
    int64_t child = expressionChildren[index];
    return child >= 0 && static_cast<size_t>(child) < roots.size()
               ? roots[child]
               : nullptr;
  };
  Operation *event0 = childFor(0);
  Operation *event1 = childFor(1);
  if (!event0 || !event1 || ticks[2] < 0)
    return function.emitError(
        "basic timing check has no direct events or limit");

  SmallVector<Value, 2> handles;
  SmallVector<int32_t, 2> eventEdges;
  std::array<Operation *, 2> events{event0, event1};
  for (auto [index, event] : llvm::enumerate(events)) {
    FailureOr<Value> handle = lowerExpression(event, true);
    if (failed(handle) ||
        !isa<sim::RefType, sim::NetType, sim::DriverType>(
            succeeded(handle) ? (*handle).getType() : Type{}))
      return function.emitError(
          "basic timing-check event is not a direct signal handle");
    auto edge = dyn_cast<semantic::EdgeKindAttr>(edges[index]);
    if (!edge)
      return function.emitError(
          "basic timing-check event has an invalid edge");
    handles.push_back(*handle);
    eventEdges.push_back(static_cast<int32_t>(edge.getValue()));
  }

  std::optional<CapturedLValue> notifier;
  if (expressionChildren.size() > 3 && expressionChildren[3] >= 0) {
    Operation *notifierExpression = childFor(3);
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
  Value zero64 = arith::ConstantOp::create(
      builder, location, i64, builder.getI64IntegerAttr(0));
  Value falseValue = arith::ConstantOp::create(
      builder, location, i1, builder.getBoolAttr(false));
  Value timestamp = sim::SimRefAllocOp::create(
      builder, location, sim::RefType::get(function.getContext(), i64),
      zero64);
  Value timestampValid = sim::SimRefAllocOp::create(
      builder, location, sim::RefType::get(function.getContext(), i1),
      falseValue);

  auto codeUnit = function->getAttrOfType<IntegerAttr>("code_unit_id");
  uint32_t occurrenceSite =
      codeUnit ? static_cast<uint32_t>(codeUnit.getValue().getZExtValue()) : 0;
  if (occurrenceSite == 0)
    occurrenceSite = 1;

  Block *wait = addBlock();
  Block *drain = addBlock();
  Block *process = addBlock();
  Block *violation = addBlock();
  emitBranch(wait);

  setCurrent(wait);
  sim::SimSuspendClockSetOp::create(
      builder, location, handles, builder.getI32IntegerAttr(0),
      builder.getDenseI32ArrayAttr(eventEdges),
      builder.getDenseI32ArrayAttr({-1, -1}),
      builder.getI64IntegerAttr(occurrenceSite), sim::ContinuationSiteAttr{},
      sim::EventRegionAttr::get(function.getContext(),
                                sim::EventRegion::Active),
      drain);

  setCurrent(drain);
  Value context = function.getBody().front().getArgument(0);
  Value cohort = sim::SimClockOccurrenceConsumeOp::create(
      builder, location, i64, context,
      builder.getI64IntegerAttr(occurrenceSite));
  Value hasCohort = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ne, cohort, zero64);
  cf::CondBranchOp::create(builder, location, hasCohort, process,
                           ValueRange{}, wait, ValueRange{});

  setCurrent(process);
  Value one64 = arith::ConstantOp::create(
      builder, location, i64, builder.getI64IntegerAttr(1));
  Value two64 = arith::ConstantOp::create(
      builder, location, i64, builder.getI64IntegerAttr(2));
  auto occurred = [&](Value mask) {
    Value selected = arith::AndIOp::create(builder, location, cohort, mask);
    return Value(arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, selected, zero64));
  };
  Value event0Occurred = occurred(one64);
  Value event1Occurred = occurred(two64);
  Value now = sim::SimTimeNowOp::create(builder, location, i64, context);
  Value previous = sim::SimRefLoadOp::create(builder, location, i64, timestamp);
  Value valid =
      sim::SimRefLoadOp::create(builder, location, i1, timestampValid);

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
  Value delta = arith::SubIOp::create(builder, location, now,
                                      effectiveTimestamp);
  Value limit = arith::ConstantOp::create(
      builder, location, i64, builder.getI64IntegerAttr(ticks[2]));
  Value positiveLimit = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ne, limit, zero64);
  Value inWindow = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::ult, delta, limit);
  Value qualified = arith::AndIOp::create(builder, location, checkOccurred,
                                          effectiveValid);
  qualified = arith::AndIOp::create(builder, location, qualified,
                                    positiveLimit);
  qualified = arith::AndIOp::create(builder, location, qualified, inWindow);
  if (setupStyle) {
    Value notSimultaneous = arith::XOrIOp::create(
        builder, location, timestampOccurred,
        arith::ConstantOp::create(builder, location, i1,
                                  builder.getBoolAttr(true)));
    Value nonzeroDelta = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, delta, zero64);
    qualified = arith::AndIOp::create(builder, location, qualified,
                                      notSimultaneous);
    qualified = arith::AndIOp::create(builder, location, qualified,
                                      nonzeroDelta);
  }
  Value nextTimestamp = arith::SelectOp::create(
      builder, location, timestampOccurred, now, previous);
  Value nextValid = arith::OrIOp::create(builder, location, valid,
                                         timestampOccurred);
  sim::SimRefStoreOp::create(builder, location, nextTimestamp, timestamp);
  sim::SimRefStoreOp::create(builder, location, nextValid, timestampValid);
  cf::CondBranchOp::create(builder, location, qualified, violation,
                           ValueRange{}, drain, ValueRange{});

  setCurrent(violation);
  if (notifier) {
    FailureOr<Value> old = loadCapturedLValue(*notifier, location);
    if (failed(old))
      return failure();
    FailureOr<Value> logic = toLogic(*old, location);
    if (failed(logic) || cast<sim::LogicType>((*logic).getType()).getWidth() != 1)
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
        builder, location, i1, sim::CompareKind::CaseEq, *logic,
        highImpedance);
    // IEEE 1800-2017 31.6 toggles 0/1, preserves Z, and permits either
    // known result for X. Choose X->0 deterministically across all tiers.
    Value toggled = arith::SelectOp::create(builder, location, isZero, one,
                                            zero);
    Value next = arith::SelectOp::create(builder, location, isHighImpedance,
                                         highImpedance, toggled);
    if (failed(writeCapturedLValue(*notifier, next, false, false, location)))
      return failure();
  } else {
    // IEEE 1800-2017 31.3 reports the violation even when the optional
    // Clause 31.6 notifier is omitted. Keep this cold path nonfatal.
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
  cf::BranchOp::create(builder, location, drain, ValueRange{});
  return success();
}

} // namespace obelisk::simlowering
