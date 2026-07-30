//===- SimulationSuspensionTypeLowering.cpp - Suspension type rewrites ----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

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
      .Case<sim::SimSuspendEventOp>([&](auto op) {
        watched.push_back(op.getEvent());
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
    if (auto reference = dyn_cast<sim::RefType>(type))
      type = reference.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(type))
      type = net.getElementType();
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
    for (Value dependency : operation.getDependencies()) {
      if (isa<sim::EventType>(dependency.getType())) {
        dependencyKinds.push_back(OBELISK_RT_OBSERVER_DEPENDENCY_EVENT);
        dependencyWidths.push_back(1);
        continue;
      }
      Type type =
          isa<sim::RefType>(dependency.getType())
              ? cast<sim::RefType>(dependency.getType()).getElementType()
              : cast<sim::NetType>(dependency.getType()).getElementType();
      std::optional<unsigned> width =
          isa<FloatType>(type)
              ? std::optional<unsigned>(cast<FloatType>(type).getWidth())
              : sim::getPackedWidth(type);
      if (!width)
        return operation.emitOpError(
            "observer signal dependency must have a packed width");
      dependencyKinds.push_back(OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL);
      dependencyWidths.push_back(static_cast<int32_t>(*width));
    }

    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addTypes(results);
    state.addAttributes(operation->getAttrs());
    state.addAttribute("obelisk.coro.observer_id",
                       rewriter.getI64IntegerAttr(
                           static_cast<uint64_t>(*evaluator.getCodeUnitId())));
    state.addAttribute("obelisk.coro.observer_width",
                       rewriter.getI32IntegerAttr(*resultWidth));
    state.addAttribute("obelisk.coro.observer_four_state",
                       rewriter.getBoolAttr(isa<sim::LogicType>(observerType)));
    state.addAttribute("obelisk.coro.dependency_kinds",
                       rewriter.getDenseI32ArrayAttr(dependencyKinds));
    state.addAttribute("obelisk.coro.dependency_widths",
                       rewriter.getDenseI32ArrayAttr(dependencyWidths));
    rewriter.replaceOp(operation, rewriter.create(state)->getResults());
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

    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(operands);
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    state.addAttribute("obelisk.coro.initial_plane_counts",
                       rewriter.getDenseI32ArrayAttr(initialPlaneCounts));
    state.addAttribute("obelisk.coro.condition_operand_begin",
                       rewriter.getI64IntegerAttr(conditionBegin));
    state.addAttribute("obelisk.coro.continuation_operand_begin",
                       rewriter.getI64IntegerAttr(continuationBegin));
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

template <typename Op>
class SimSuspendTypeConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(Op operation,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    SmallVector<int32_t> waitWidths = suspensionWaitWidths(operation);
    if (!waitWidths.empty())
      state.addAttribute("obelisk.coro.wait_widths",
                         rewriter.getDenseI32ArrayAttr(waitWidths));
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

} // namespace

void populateSuspensionTypeConversionPatterns(RewritePatternSet &patterns,
                                              TypeConverter &converter) {
  patterns.add<SimObserverBindTypeConversion, SimSuspendObserveTypeConversion,
               SimSuspendTypeConversion<sim::SimSuspendDelayOp>,
               SimSuspendTypeConversion<sim::SimSuspendChangeOp>,
               SimSuspendTypeConversion<sim::SimSuspendEdgeOp>,
               SimSuspendTypeConversion<sim::SimSuspendEdgeIffOp>,
               SimSuspendTypeConversion<sim::SimSuspendLevelOp>,
               SimSuspendTypeConversion<sim::SimSuspendAnyOp>,
               SimSuspendTypeConversion<sim::SimSuspendEventOp>,
               SimSuspendTypeConversion<sim::SimSuspendForeverOp>,
               SimSuspendTypeConversion<sim::SimSuspendAwaitOp>,
               SimSuspendTypeConversion<sim::SimSuspendJoinOp>,
               SimSuspendTypeConversion<sim::SimSuspendChildrenOp>>(
      converter, patterns.getContext());
}

} // namespace obelisk::detail
