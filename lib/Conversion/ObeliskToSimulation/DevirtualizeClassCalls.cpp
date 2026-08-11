//===- DevirtualizeClassCalls.cpp - Resolve exact class dispatches -------===//

#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMDEVIRTUALIZECLASSCALLSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

namespace sim = ::obelisk::sim;

bool isDirectTarget(sim::SimClassMethodDeclOp method,
                    sim::SimClassVirtualCallOp call) {
  return method && method.getSignatureIdAttr() &&
         method.getSignatureId() == call.getSignatureId() &&
         !method.getIsPure() && !method.getIsTask() &&
         method.getImplementationAttr();
}

bool isDirectTarget(sim::SimClassMethodDeclOp method,
                    sim::SimClassVirtualTaskCallOp call) {
  return method && method.getSignatureIdAttr() &&
         method.getSignatureId() == call.getSignatureId() &&
         !method.getIsPure() && method.getIsTask() &&
         method.getImplementationAttr();
}

sim::SimClassMethodDeclOp resolveMonomorphic(
    const analysis::ClassDispatchAnalysis &dispatch,
    sim::SimClassDeclOp staticClass, sim::SimClassVirtualCallOp call) {
  sim::SimClassMethodDeclOp selected;
  for (sim::SimClassDeclOp candidate :
       dispatch.compatibleConcreteClasses(staticClass)) {
    sim::SimClassMethodDeclOp method = dispatch.resolve(
        candidate, call.getSlot(), call.getSignatureId());
    if (!isDirectTarget(method, call))
      return {};
    if (!selected)
      selected = method;
    else if (selected.getImplementationAttr() !=
             method.getImplementationAttr())
      return {};
  }
  return selected;
}

sim::SimClassMethodDeclOp resolveMonomorphic(
    const analysis::ClassDispatchAnalysis &dispatch,
    sim::SimClassDeclOp staticClass, sim::SimClassVirtualTaskCallOp call) {
  sim::SimClassMethodDeclOp selected;
  for (sim::SimClassDeclOp candidate :
       dispatch.compatibleConcreteClasses(staticClass)) {
    sim::SimClassMethodDeclOp method = dispatch.resolve(
        candidate, call.getSlot(), call.getSignatureId());
    if (!isDirectTarget(method, call))
      return {};
    if (!selected)
      selected = method;
    else if (selected.getImplementationAttr() !=
             method.getImplementationAttr())
      return {};
  }
  return selected;
}

class ExactClassResolver {
public:
  explicit ExactClassResolver(const analysis::ClassDispatchAnalysis &dispatch)
      : dispatch(dispatch) {}

  sim::SimClassDeclOp resolve(Value value) {
    if (auto found = exact.find(value); found != exact.end())
      return found->second;
    if (unknown.contains(value) || !visiting.insert(value).second)
      return {};

    sim::SimClassDeclOp result;
    if (auto allocation = value.getDefiningOp<sim::SimClassAllocOp>()) {
      result = dispatch.lookup(
          cast<sim::ClassHandleType>(allocation.getResult().getType()));
    } else if (auto copy = value.getDefiningOp<sim::SimClassCopyOp>()) {
      result = resolve(copy.getSource());
    } else if (auto castOp = value.getDefiningOp<sim::SimClassCastOp>()) {
      sim::SimClassDeclOp dynamicClass = resolve(castOp.getObject());
      sim::SimClassDeclOp target = dispatch.lookup(
          cast<sim::ClassHandleType>(castOp.getResult().getType()));
      if (dispatch.isInstanceOf(dynamicClass, target))
        result = dynamicClass;
    }

    visiting.erase(value);
    if (result)
      exact.try_emplace(value, result);
    else
      unknown.insert(value);
    return result;
  }

private:
  const analysis::ClassDispatchAnalysis &dispatch;
  DenseMap<Value, sim::SimClassDeclOp> exact;
  DenseSet<Value> unknown;
  DenseSet<Value> visiting;
};

FailureOr<sim::SimCallOp>
createDirectCall(IRRewriter &rewriter, Operation *operation,
                 FlatSymbolRefAttr callee, Value receiver, ValueRange arguments,
                 TypeRange resultTypes) {
  sim::SimFuncOp caller = operation->getParentOfType<sim::SimFuncOp>();
  auto implementation =
      SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(operation, callee);
  if (!caller || caller.getBody().empty() ||
      caller.getBody().front().getNumArguments() == 0 || !implementation ||
      implementation.getEntryKind() != sim::EntryKind::Function ||
      implementation.getFunctionType().getNumInputs() < 2)
    return failure();

  Type expectedReceiver = implementation.getFunctionType().getInput(1);
  Value adjusted = receiver;
  if (adjusted.getType() != expectedReceiver)
    adjusted = sim::SimClassCastOp::create(rewriter, operation->getLoc(),
                                           expectedReceiver, adjusted);

  SmallVector<Value> operands{caller.getBody().front().getArgument(0),
                              adjusted};
  llvm::append_range(operands, arguments);
  return sim::SimCallOp::create(rewriter, operation->getLoc(), resultTypes,
                                callee, operands, ArrayAttr{}, ArrayAttr{});
}

LogicalResult createGuardedDirectCall(IRRewriter &rewriter,
                                      sim::SimClassVirtualCallOp call,
                                      sim::SimClassMethodDeclOp method) {
  Location location = call.getLoc();
  Value receiver = call.getReceiver();
  SmallVector<Value> arguments(call.getArguments());
  SmallVector<Type> resultTypes(call.getResultTypes());
  Block *head = call->getBlock();

  rewriter.setInsertionPoint(call);
  Value isNull = sim::SimManagedIsNullOp::create(
      rewriter, location, rewriter.getI1Type(), receiver);
  Block *continuation = rewriter.splitBlock(head, call->getIterator());
  SmallVector<BlockArgument> mergedResults;
  for (Type type : resultTypes)
    mergedResults.push_back(continuation->addArgument(type, location));

  Region *region = head->getParent();
  Block *nullBlock =
      rewriter.createBlock(region, continuation->getIterator());
  Block *directBlock =
      rewriter.createBlock(region, continuation->getIterator());

  rewriter.setInsertionPointToEnd(head);
  cf::CondBranchOp::create(rewriter, location, isNull, nullBlock, ValueRange{},
                           directBlock, ValueRange{});

  rewriter.setInsertionPointToEnd(nullBlock);
  Value nullReceiver = sim::SimClassNullOp::create(
      rewriter, location, receiver.getType());
  auto fallback = sim::SimClassVirtualCallOp::create(
      rewriter, location, resultTypes, nullReceiver, call.getMethodAttr(),
      call.getSlotAttr(), call.getSignatureIdAttr(), arguments);
  cf::BranchOp::create(rewriter, location, continuation,
                       fallback.getResults());

  rewriter.setInsertionPointToEnd(directBlock);
  FailureOr<sim::SimCallOp> direct = createDirectCall(
      rewriter, call, method.getImplementationAttr(), receiver, arguments,
      resultTypes);
  if (failed(direct))
    return failure();
  cf::BranchOp::create(rewriter, location, continuation, direct->getResults());

  for (auto [result, merged] :
       llvm::zip_equal(call.getResults(), mergedResults))
    result.replaceAllUsesWith(merged);
  rewriter.eraseOp(call);
  return success();
}

FailureOr<sim::SimTaskCallOp>
createDirectTaskCall(IRRewriter &rewriter,
                     sim::SimClassVirtualTaskCallOp call,
                     sim::SimClassMethodDeclOp method) {
  sim::SimFuncOp caller = call->getParentOfType<sim::SimFuncOp>();
  auto implementation =
      SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(
          call, method.getImplementationAttr());
  if (!caller || caller.getBody().empty() ||
      caller.getBody().front().getNumArguments() == 0 || !implementation ||
      implementation.getEntryKind() != sim::EntryKind::Task ||
      implementation.getFunctionType().getNumInputs() < 2)
    return failure();

  Value receiver = call.getReceiver();
  Type expectedReceiver = implementation.getFunctionType().getInput(1);
  if (receiver.getType() != expectedReceiver)
    receiver = sim::SimClassCastOp::create(rewriter, call.getLoc(),
                                           expectedReceiver, receiver);

  SmallVector<Value> operands{caller.getBody().front().getArgument(0),
                              receiver};
  llvm::append_range(operands, call.getArguments());
  uint64_t argumentCount = operands.size();
  llvm::append_range(operands, call.getContinuationOperands());
  return sim::SimTaskCallOp::create(
      rewriter, call.getLoc(), method.getImplementationAttr(), operands,
      rewriter.getI64IntegerAttr(argumentCount), call.getSiteAttr(),
      call.getContinuation());
}

LogicalResult createGuardedDirectTaskCall(
    IRRewriter &rewriter, sim::SimClassVirtualTaskCallOp call,
    sim::SimClassMethodDeclOp method) {
  Location location = call.getLoc();
  Value receiver = call.getReceiver();
  SmallVector<Value> arguments(call.getArguments());
  SmallVector<Value> continuationOperands(call.getContinuationOperands());
  Block *continuation = call.getContinuation();
  Block *head = call->getBlock();
  Region *region = head->getParent();

  rewriter.setInsertionPoint(call);
  Value isNull = sim::SimManagedIsNullOp::create(
      rewriter, location, rewriter.getI1Type(), receiver);
  Block *nullBlock =
      rewriter.createBlock(region, continuation->getIterator());
  Block *directBlock =
      rewriter.createBlock(region, continuation->getIterator());

  SmallVector<Value> fallbackValues(arguments);
  llvm::append_range(fallbackValues, continuationOperands);
  rewriter.setInsertionPointToEnd(nullBlock);
  Value nullReceiver = sim::SimClassNullOp::create(
      rewriter, location, receiver.getType());
  sim::SimClassVirtualTaskCallOp::create(
      rewriter, location, nullReceiver, call.getMethodAttr(),
      call.getSlotAttr(), call.getSignatureIdAttr(), fallbackValues,
      call.getArgumentCountAttr(), call.getSiteAttr(), continuation);

  rewriter.setInsertionPointToEnd(directBlock);
  if (failed(createDirectTaskCall(rewriter, call, method)))
    return failure();

  rewriter.setInsertionPoint(call);
  cf::CondBranchOp::create(rewriter, location, isNull, nullBlock, ValueRange{},
                           directBlock, ValueRange{});
  rewriter.eraseOp(call);
  return success();
}

class ObeliskSimDevirtualizeClassCallsPass final
    : public impl::ObeliskSimDevirtualizeClassCallsPassBase<
          ObeliskSimDevirtualizeClassCallsPass> {
public:
  using Base = impl::ObeliskSimDevirtualizeClassCallsPassBase<
      ObeliskSimDevirtualizeClassCallsPass>;
  using Base::Base;
  ObeliskSimDevirtualizeClassCallsPass(
      const ObeliskSimDevirtualizeClassCallsPass &other)
      : Base(other) {}

  void runOnOperation() override;

private:
  Statistic exactCalls{this, "exact-calls",
                       "virtual calls resolved from exact non-null classes"};
  Statistic monomorphicCalls{
      this, "monomorphic-calls",
      "virtual calls resolved from closed-world monomorphic hierarchies"};
  Statistic guardedCalls{
      this, "guarded-calls",
      "monomorphic calls guarded to preserve null-dispatch failure"};
  Statistic directCalls{this, "direct-calls",
                        "managed direct calls normalized to ordinary calls"};
  Statistic unresolvedCalls{this, "unresolved-calls",
                            "virtual calls retained conservatively"};
  Statistic exactTaskCalls{
      this, "exact-task-calls",
      "virtual task calls resolved from exact non-null classes"};
  Statistic monomorphicTaskCalls{
      this, "monomorphic-task-calls",
      "virtual task calls resolved from closed-world monomorphic hierarchies"};
  Statistic guardedTaskCalls{
      this, "guarded-task-calls",
      "monomorphic task calls guarded to preserve null-dispatch failure"};
  Statistic unresolvedTaskCalls{
      this, "unresolved-task-calls",
      "virtual task calls retained conservatively"};
};

void ObeliskSimDevirtualizeClassCallsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  analysis::ClassDispatchAnalysis dispatch(design);
  ExactClassResolver resolver(dispatch);
  IRRewriter rewriter(design.getContext());

  SmallVector<sim::SimClassVirtualCallOp> virtualCalls;
  design.walk([&](sim::SimClassVirtualCallOp call) {
    virtualCalls.push_back(call);
  });
  for (sim::SimClassVirtualCallOp call : virtualCalls) {
    sim::SimClassDeclOp dynamicClass = resolver.resolve(call.getReceiver());
    if (dynamicClass) {
      sim::SimClassMethodDeclOp method = dispatch.resolve(
          dynamicClass, call.getSlot(), call.getSignatureId());
      if (!isDirectTarget(method, call)) {
        ++unresolvedCalls;
        continue;
      }
      rewriter.setInsertionPoint(call);
      FailureOr<sim::SimCallOp> replacement = createDirectCall(
          rewriter, call, method.getImplementationAttr(), call.getReceiver(),
          call.getArguments(), call.getResultTypes());
      if (failed(replacement)) {
        call.emitError(
            "resolved virtual method has no valid function implementation");
        return signalPassFailure();
      }
      rewriter.replaceOp(call, replacement->getResults());
      ++exactCalls;
      continue;
    }

    if (call.getReceiver().getDefiningOp<sim::SimClassNullOp>()) {
      ++unresolvedCalls;
      continue;
    }

    auto staticType = cast<sim::ClassHandleType>(call.getReceiver().getType());
    sim::SimClassMethodDeclOp method =
        resolveMonomorphic(dispatch, dispatch.lookup(staticType), call);
    if (!method) {
      ++unresolvedCalls;
      continue;
    }
    if (failed(createGuardedDirectCall(rewriter, call, method))) {
      call.emitError(
          "monomorphic virtual method has no valid function implementation");
      return signalPassFailure();
    }
    ++monomorphicCalls;
    ++guardedCalls;
  }

  SmallVector<sim::SimClassVirtualTaskCallOp> virtualTaskCalls;
  design.walk([&](sim::SimClassVirtualTaskCallOp call) {
    virtualTaskCalls.push_back(call);
  });
  for (sim::SimClassVirtualTaskCallOp call : virtualTaskCalls) {
    sim::SimClassDeclOp dynamicClass = resolver.resolve(call.getReceiver());
    if (dynamicClass) {
      sim::SimClassMethodDeclOp method = dispatch.resolve(
          dynamicClass, call.getSlot(), call.getSignatureId());
      if (!isDirectTarget(method, call)) {
        ++unresolvedTaskCalls;
        continue;
      }
      rewriter.setInsertionPoint(call);
      if (failed(createDirectTaskCall(rewriter, call, method))) {
        call.emitError(
            "resolved virtual task has no valid task implementation");
        return signalPassFailure();
      }
      rewriter.eraseOp(call);
      ++exactTaskCalls;
      continue;
    }

    if (call.getReceiver().getDefiningOp<sim::SimClassNullOp>()) {
      ++unresolvedTaskCalls;
      continue;
    }

    auto staticType = cast<sim::ClassHandleType>(call.getReceiver().getType());
    sim::SimClassMethodDeclOp method =
        resolveMonomorphic(dispatch, dispatch.lookup(staticType), call);
    if (!method) {
      ++unresolvedTaskCalls;
      continue;
    }
    if (failed(createGuardedDirectTaskCall(rewriter, call, method))) {
      call.emitError(
          "monomorphic virtual task has no valid task implementation");
      return signalPassFailure();
    }
    ++monomorphicTaskCalls;
    ++guardedTaskCalls;
  }

  SmallVector<sim::SimClassDirectCallOp> directCallsToNormalize;
  design.walk([&](sim::SimClassDirectCallOp call) {
    directCallsToNormalize.push_back(call);
  });
  for (sim::SimClassDirectCallOp call : directCallsToNormalize) {
    rewriter.setInsertionPoint(call);
    FailureOr<sim::SimCallOp> replacement = createDirectCall(
        rewriter, call, call.getCalleeAttr(), call.getReceiver(),
        call.getArguments(), call.getResultTypes());
    if (failed(replacement)) {
      call.emitError("direct class call has no valid function implementation");
      return signalPassFailure();
    }
    rewriter.replaceOp(call, replacement->getResults());
    ++directCalls;
  }
}

} // namespace
} // namespace obelisk
