//===- LowerUnitTiming.cpp - Lower timing and event controls -----------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/SetVector.h"

#include <cmath>
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
  if (isa<semantic::SVRealLiteralOp, semantic::SVTimeLiteralOp>(realLiteral)) {
    auto spelling = realLiteral->getAttrOfType<StringAttr>("constant_value");
    auto quantumAttr =
        function->getAttrOfType<IntegerAttr>(delayQuantumAttrName);
    if (!spelling || !quantumAttr) {
      function.emitError("code unit has incomplete real-delay metadata");
      return failure();
    }
    double amount = 0;
    if (spelling.getValue().getAsDouble(amount) || !std::isfinite(amount)) {
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
  if (integer.getWidth() > 64) {
    emitError(location) << "dynamic delay wider than 64 bits is not executable";
    return failure();
  }
  FailureOr<Value> normalized64 =
      convert(normalized, builder.getI64Type(), false, location);
  if (failed(normalized64))
    return failure();

  // Keep the multiplication in the supported nonnegative signed-time range
  // on every backend. The source language's X/Z and negative rules have
  // already mapped those values to zero above.
  uint64_t scale = scaleAttr.getValue().getZExtValue();
  uint64_t maximumInput =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / scale;
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

LogicalResult UnitLowering::emitEventSuspend(Operation *control,
                                             Block *continuation,
                                             ValueRange continuationOperands) {
  Location location = getSemanticLocation(control);
  auto emitDirect = [&](Value watched, sim::EdgeKind edge, Block *successor,
                        ValueRange operands) {
    if (isa<sim::EventType>(watched.getType()))
      sim::SimSuspendEventOp::create(builder, location, watched, operands,
                                     sim::ContinuationSiteAttr{},
                                     sim::EventRegionAttr{}, successor);
    else if (edge == sim::EdgeKind::Change)
      sim::SimSuspendChangeOp::create(builder, location, watched, operands,
                                      sim::ContinuationSiteAttr{},
                                      sim::EventRegionAttr{}, successor);
    else
      sim::SimSuspendEdgeOp::create(builder, location, edge, watched, operands,
                                    sim::ContinuationSiteAttr{},
                                    sim::EventRegionAttr{}, successor);
  };
  auto evaluateInitial = [&](Operation *expression) -> FailureOr<Value> {
    FailureOr<Value> value = lowerExpression(expression);
    if (failed(value))
      return failure();
    if (isa<sim::EventType>((*value).getType()))
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
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
      FailureOr<Value> initial = evaluateInitial(children.front());
      FailureOr<Value> primary = bindObserver(children.front());
      if (failed(initial) || failed(primary))
        return failure();
      primaries.push_back(*primary);
      initials.push_back(*initial);
      auto edge = static_cast<int32_t>(event.getEdgeKind());
      FailureOr<Type> primaryType = getNormalizedSemanticType(children.front());
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
    FailureOr<Type> watchedType = getNormalizedSemanticType(children.front());
    if (failed(watchedType))
      return failure();
    bool computed =
        !isAddressableExpression(children.front()) ||
        (event.getHasIff() && (!isAddressableExpression(children[1]) ||
                               isa<sim::EventType>(*watchedType)));
    if (computed)
      return emitObserved(ArrayRef<semantic::SVSignalEventControlOp>(event));
    FailureOr<Value> handle =
        lowerExpression(children.front(), !isa<sim::EventType>(*watchedType));
    if (failed(handle))
      return failure();
    auto edge = static_cast<sim::EdgeKind>(event.getEdgeKind());
    if (!event.getHasIff()) {
      emitDirect(*handle, edge, continuation, continuationOperands);
      return success();
    }

    FailureOr<Value> condition = lowerExpression(children[1], true);
    if (failed(condition))
      return failure();
    if (!isa<sim::RefType, sim::NetType>((*handle).getType()) ||
        !isa<sim::RefType, sim::NetType>((*condition).getType())) {
      unsupported(event) << " (iff requires signal handles)";
      return failure();
    }
    sim::SimSuspendEdgeIffOp::create(
        builder, location, edge, *handle, *condition, continuationOperands,
        sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
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
    computed |=
        event.getHasIff() || !isAddressableExpression(eventChildren.front());
    FailureOr<Type> watchedType =
        getNormalizedSemanticType(eventChildren.front());
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
  sim::SimSuspendAnyOp::create(
      builder, location, values, builder.getDenseI32ArrayAttr(edges),
      sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
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
  FailureOr<Value> count = lowerExpression(children[0]);
  if (failed(count))
    return failure();
  FailureOr<Value> scalar = toPackedScalar(*count, location);
  if (failed(scalar))
    return failure();
  Type countType = builder.getI64Type();
  FailureOr<Value> normalized =
      convert(*scalar, countType, isSignedNode(children[0]), location);
  if (failed(normalized))
    return failure();
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

LogicalResult UnitLowering::lowerTiming(Operation *control,
                                        Operation *statement) {
  Location location = getSemanticLocation(control);
  SmallVector<Operation *> children = getChildren(control);
  auto lowerControlledStatement =
      [&](Operation *sampledClock) -> LogicalResult {
    Operation *savedClock = activeSampledClock;
    activeSampledClock = sampledClock;
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
    Block *statementEnd = current;
    if (dependencies.empty()) {
      unsupported(control)
          << " (@* controlled statement has no readable dependency)";
      return failure();
    }
    setCurrent(waitBlock);
    SmallVector<int32_t> edges(dependencies.size(),
                               static_cast<int32_t>(sim::EdgeKind::Change));
    if (dependencies.size() == 1) {
      auto suspend = sim::SimSuspendChangeOp::create(
          builder, location, dependencies.front(), ValueRange{},
          sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, continuation);
      if (control == topLevelWildcardControl)
        suspend->setAttr(sim::metadata::topLevelWildcardWait,
                         builder.getUnitAttr());
    } else {
      auto suspend = sim::SimSuspendAnyOp::create(
          builder, location, dependencies.getArrayRef(),
          builder.getDenseI32ArrayAttr(edges), sim::ContinuationSiteAttr{},
          sim::EventRegionAttr{}, continuation);
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
  if (dependencies.size() != 1 || !isAddressableExpression(children[0])) {
    if (!children[0]->hasAttr("obelisk_sim.observer")) {
      unsupported(op) << " (computed wait condition requires an observer)";
      return failure();
    }
    cf::CondBranchOp::create(builder, location, *condition, bodyBlock,
                             ValueRange{}, suspendBlock, ValueRange{});
    setCurrent(suspendBlock);
    FailureOr<Value> observer = bindObserver(children[0]);
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
                                 sim::EventSiteAttr{});
  return success();
}

} // namespace obelisk::simlowering
