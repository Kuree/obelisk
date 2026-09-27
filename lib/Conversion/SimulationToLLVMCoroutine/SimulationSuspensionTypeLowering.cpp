//===- SimulationSuspensionTypeLowering.cpp - Suspension type rewrites ----===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

SmallVector<int32_t> suspensionWaitWidths(Operation *operation) {
  SmallVector<Value> watched;
  SmallVector<bool> scalarEdge;
  TypeSwitch<Operation *>(operation)
      .Case<sim::SimSuspendChangeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendLevelOp>([&](auto op) {
        watched.push_back(op.getWatched());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendEdgeOp>([&](auto op) {
        watched.push_back(op.getWatched());
        scalarEdge.push_back(true);
      })
      .Case<sim::SimSuspendEdgeIffOp>([&](auto op) {
        watched.push_back(op.getWatched());
        scalarEdge.push_back(true);
        watched.push_back(op.getCondition());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendAnyOp>([&](auto op) {
        llvm::append_range(watched, op.getWatched());
        for (int32_t edge : op.getEdges())
          scalarEdge.push_back(edge !=
                               static_cast<int32_t>(sim::EdgeKind::Change));
      })
      .Case<sim::SimSuspendClockSetOp>([&](auto op) {
        llvm::append_range(watched, op.getPrimaries());
        for (int32_t edge : op.getEdges()) {
          uint32_t encoded = static_cast<uint32_t>(edge);
          bool customTransition =
              (encoded & ~OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) ==
                  OBELISK_RT_WAIT_EDGE_TRANSITION_MASK &&
              (encoded & OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) != 0;
          // IEEE 1800-2017 31.8 applies an edge-control descriptor to every
          // bit of a timing-check vector and coalesces one packed publication.
          // Canonical pos/negedge remains the historical scalar-LSB wait.
          scalarEdge.push_back(
              encoded != static_cast<uint32_t>(sim::EdgeKind::Change) &&
              !customTransition);
        }
        llvm::append_range(watched, op.getConditions());
        scalarEdge.append(op.getConditions().size(), false);
      })
      .Case<sim::SimSuspendEventOp>([&](auto op) {
        watched.push_back(op.getEvent());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendEventOrderOp>([&](auto op) {
        llvm::append_range(watched, op.getEvents());
        scalarEdge.append(op.getEvents().size(), false);
      })
      .Case<sim::SimSuspendMailboxOp>([&](auto op) {
        watched.push_back(op.getMailbox());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendSemaphoreOp>([&](auto op) {
        watched.push_back(op.getSemaphore());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendAwaitOp>([&](auto op) {
        watched.push_back(op.getProcess());
        scalarEdge.push_back(false);
      })
      .Case<sim::SimSuspendJoinOp>([&](auto op) {
        llvm::append_range(watched, op.getProcesses());
        scalarEdge.append(op.getProcesses().size(), false);
      });

  SmallVector<int32_t> widths;
  widths.reserve(watched.size());
  for (auto [index, value] : llvm::enumerate(watched)) {
    if (scalarEdge[index]) {
      widths.push_back(1);
      continue;
    }
    Type type = value.getType();
    if (isa<sim::ManagedWatchType>(type)) {
      widths.push_back(static_cast<int32_t>(OBELISK_RT_WAIT_WIDTH_MANAGED));
      continue;
    }
    if (auto reference = dyn_cast<sim::RefType>(type))
      type = reference.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(type))
      type = net.getElementType();
    else if (auto driver = dyn_cast<sim::DriverType>(type))
      type = driver.getElementType();
    else
      type = {};
    std::optional<unsigned> width =
        type ? nativeStateWidth(type) : std::nullopt;
    widths.push_back(width ? static_cast<int32_t>(*width) : 0);
  }
  return widths;
}

class SimObserverBindTypeConversion final
    : public OpConversionPattern<sim::SimObserverBindOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimObserverBindOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Type> results;
    if (failed(getTypeConverter()->convertType(operation.getResult().getType(),
                                               results)) ||
        results.size() != 1)
      return rewriter.notifyMatchFailure(
          operation, "observer token must convert to one physical value");
    Type observerType = operation.getResult().getType().getResultType();
    std::optional<unsigned> resultWidth =
        isa<FloatType>(observerType)
            ? std::optional<unsigned>(cast<FloatType>(observerType).getWidth())
            : sim::getPackedWidth(observerType);
    if (!resultWidth)
      return operation.emitOpError("observer result must remain packed");
    auto evaluator = SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(
        operation, operation.getEvaluatorAttr());
    if (!evaluator || !evaluator.getCodeUnitId())
      return operation.emitOpError(
          "observer evaluator is missing its stable code-unit ID");

    SmallVector<int32_t> dependencyKinds;
    SmallVector<int32_t> dependencyWidths;
    SmallVector<int32_t> dependencyCaptureIndices;
    for (Value dependency : operation.getDependencies()) {
      if (isa<sim::ManagedWatchType>(dependency.getType())) {
        dependencyKinds.push_back(OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED);
        dependencyWidths.push_back(1);
        dependencyCaptureIndices.push_back(-1);
        continue;
      }
      if (isa<sim::EventType>(dependency.getType())) {
        dependencyKinds.push_back(OBELISK_RT_OBSERVER_DEPENDENCY_EVENT);
        dependencyWidths.push_back(1);
        dependencyCaptureIndices.push_back(-1);
        continue;
      }
      if (auto reference =
              dyn_cast<sim::ArgumentRefType>(dependency.getType())) {
        auto capture = llvm::find(operation.getCaptures(), dependency);
        if (capture == operation.getCaptures().end())
          return operation.emitOpError(
              "argument-ref dependency must also be an observer capture");
        std::optional<unsigned> width =
            nativeStateWidth(reference.getElementType());
        if (!width)
          return operation.emitOpError(
              "argument-ref dependency must have a simulation storage width");
        dependencyKinds.push_back(OBELISK_RT_OBSERVER_DEPENDENCY_ARGUMENT_REF);
        dependencyWidths.push_back(static_cast<int32_t>(*width));
        dependencyCaptureIndices.push_back(static_cast<int32_t>(
            std::distance(operation.getCaptures().begin(), capture)));
        continue;
      }
      Type type =
          isa<sim::RefType>(dependency.getType())
              ? cast<sim::RefType>(dependency.getType()).getElementType()
              : cast<sim::NetType>(dependency.getType()).getElementType();
      std::optional<unsigned> width =
          isa<FloatType>(type)
              ? std::optional<unsigned>(cast<FloatType>(type).getWidth())
              : nativeStateWidth(type);
      if (!width)
        return operation.emitOpError(
            "observer signal dependency must have a simulation storage "
            "width");
      dependencyKinds.push_back(OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL);
      dependencyWidths.push_back(static_cast<int32_t>(*width));
      dependencyCaptureIndices.push_back(-1);
    }

    auto bridge = schedule::NativeObserverOp::create(
        rewriter, operation.getLoc(), results.front(),
        flatten(adaptor.getOperands()),
        rewriter.getI64IntegerAttr(operation.getCaptureCount()));
    for (NamedAttribute attribute : operation->getAttrs())
      if (auto field = schedule::symbolizeField(attribute.getName().getValue()))
        schedule::set(bridge, *field, attribute.getValue());
    ::obelisk::schedule::set<::obelisk::schedule::Field::NativeObserverId>(
        bridge, rewriter.getI64IntegerAttr(
                    static_cast<uint64_t>(*evaluator.getCodeUnitId())));
    ::obelisk::schedule::set<::obelisk::schedule::Field::NativeObserverWidth>(
        bridge, rewriter.getI32IntegerAttr(*resultWidth));
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::NativeObserverFourState>(
        bridge, rewriter.getBoolAttr(isa<sim::LogicType>(observerType)));
    ::obelisk::schedule::set<::obelisk::schedule::Field::NativeDependencyKinds>(
        bridge, rewriter.getDenseI32ArrayAttr(dependencyKinds));
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::NativeDependencyWidths>(
        bridge, rewriter.getDenseI32ArrayAttr(dependencyWidths));
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::NativeDependencyCaptureIndices>(
        bridge, rewriter.getDenseI32ArrayAttr(dependencyCaptureIndices));
    rewriter.replaceOp(operation, bridge.getResult());
    return success();
  }
};

class SimSuspendObserveTypeConversion final
    : public OpConversionPattern<sim::SimSuspendObserveOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimSuspendObserveOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    size_t primaryCount = operation.getEdges().size();
    size_t conditionCount = operation.getConditionCount();
    if (operation.getNumOperands() < primaryCount * 2 + conditionCount)
      return operation.emitOpError("has a truncated observer inventory");
    ArrayRef<ValueRange> converted = adaptor.getOperands();
    SmallVector<Value> operands;
    SmallVector<int32_t> initialPlaneCounts;
    for (size_t index = 0; index != primaryCount; ++index)
      llvm::append_range(operands, converted[index]);
    for (size_t index = 0; index != primaryCount; ++index) {
      ValueRange planes = converted[primaryCount + index];
      if (planes.empty() || planes.size() > 2)
        return operation.emitOpError(
            "observer initial value must lower to one or two planes");
      initialPlaneCounts.push_back(static_cast<int32_t>(planes.size()));
      llvm::append_range(operands, planes);
    }
    size_t conditionBegin = operands.size();
    for (size_t index = 0; index != conditionCount; ++index) {
      ValueRange token = converted[primaryCount * 2 + index];
      if (token.size() != 1)
        return operation.emitOpError(
            "condition observer token must lower to one value");
      llvm::append_range(operands, token);
    }
    size_t continuationBegin = operands.size();
    for (size_t index = primaryCount * 2 + conditionCount;
         index != converted.size(); ++index)
      llvm::append_range(operands, converted[index]);

    OperationState state(operation.getLoc(),
                         schedule::NativeSuspendObserveOp::getOperationName());
    state.addOperands(operands);
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    state.addAttribute(
        ::obelisk::schedule::getFieldName(
            ::obelisk::schedule::Field::NativeInitialPlaneCounts),
        rewriter.getDenseI32ArrayAttr(initialPlaneCounts));
    state.addAttribute(
        ::obelisk::schedule::getFieldName(
            ::obelisk::schedule::Field::NativeConditionOperandBegin),
        rewriter.getI64IntegerAttr(conditionBegin));
    state.addAttribute(
        ::obelisk::schedule::getFieldName(
            ::obelisk::schedule::Field::NativeContinuationOperandBegin),
        rewriter.getI64IntegerAttr(continuationBegin));
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

class SimControlBoundaryTypeConversion final
    : public OpConversionPattern<sim::SimControlBoundaryOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimControlBoundaryOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Value> activation = flatten(adaptor.getActivation());
    SmallVector<Value> resume = flatten(adaptor.getResumeOperands());
    if (activation.size() != 1)
      return operation.emitOpError(
          "control activation must lower to one value");
    OperationState state(operation.getLoc(),
                         schedule::NativeControlBoundaryOp::getOperationName());
    state.addOperands(activation);
    state.addOperands(resume);
    state.addSuccessors(operation->getSuccessors());
    for (NamedAttribute attribute : operation->getAttrs())
      state.addAttribute(attribute.getName(), attribute.getValue());
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

template <typename Op, typename NativeOp>
class SimSuspendTypeConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(Op operation,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    OperationState state(operation.getLoc(), NativeOp::getOperationName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    SmallVector<int32_t> waitWidths = suspensionWaitWidths(operation);
    if (!waitWidths.empty())
      state.addAttribute(::obelisk::schedule::getFieldName(
                             ::obelisk::schedule::Field::NativeWaitWidths),
                         rewriter.getDenseI32ArrayAttr(waitWidths));
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

} // namespace

void populateSuspensionTypeConversionPatterns(RewritePatternSet &patterns,
                                              TypeConverter &converter) {
  patterns.add<SimObserverBindTypeConversion, SimSuspendObserveTypeConversion,
               SimControlBoundaryTypeConversion,
               SimSuspendTypeConversion<sim::SimSuspendDelayOp,
                                        schedule::NativeSuspendDelayOp>,
               SimSuspendTypeConversion<sim::SimSuspendChangeOp,
                                        schedule::NativeSuspendChangeOp>,
               SimSuspendTypeConversion<sim::SimSuspendEdgeOp,
                                        schedule::NativeSuspendEdgeOp>,
               SimSuspendTypeConversion<sim::SimSuspendEdgeIffOp,
                                        schedule::NativeSuspendEdgeIffOp>,
               SimSuspendTypeConversion<sim::SimSuspendLevelOp,
                                        schedule::NativeSuspendLevelOp>,
               SimSuspendTypeConversion<sim::SimSuspendAnyOp,
                                        schedule::NativeSuspendAnyOp>,
               SimSuspendTypeConversion<sim::SimSuspendClockSetOp,
                                        schedule::NativeSuspendClockSetOp>,
               SimSuspendTypeConversion<sim::SimSuspendEventOp,
                                        schedule::NativeSuspendEventOp>,
               SimSuspendTypeConversion<sim::SimSuspendEventOrderOp,
                                        schedule::NativeSuspendEventOrderOp>,
               SimSuspendTypeConversion<sim::SimSuspendMailboxOp,
                                        schedule::NativeSuspendMailboxOp>,
               SimSuspendTypeConversion<sim::SimSuspendSemaphoreOp,
                                        schedule::NativeSuspendSemaphoreOp>,
               SimSuspendTypeConversion<sim::SimSuspendForeverOp,
                                        schedule::NativeSuspendForeverOp>,
               SimSuspendTypeConversion<sim::SimSuspendAwaitOp,
                                        schedule::NativeSuspendAwaitOp>,
               SimSuspendTypeConversion<sim::SimSuspendJoinOp,
                                        schedule::NativeSuspendJoinOp>,
               SimSuspendTypeConversion<sim::SimSuspendChildrenOp,
                                        schedule::NativeSuspendChildrenOp>,
               SimSuspendTypeConversion<sim::SimProcessControlOp,
                                        schedule::NativeProcessControlOp>>(
      converter, patterns.getContext());
}

} // namespace obelisk::detail
