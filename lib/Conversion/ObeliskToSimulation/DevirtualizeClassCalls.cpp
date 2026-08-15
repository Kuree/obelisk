//===- DevirtualizeClassCalls.cpp - Resolve exact class dispatches -------===//

#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Builders.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMDEVIRTUALIZECLASSCALLSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

namespace sim = ::obelisk::sim;

bool isDirectTarget(sim::SimClassMethodDeclOp method, uint64_t signatureId,
                    bool isTask) {
  return method && method.getSignatureIdAttr() &&
         method.getSignatureId() == signatureId && !method.getIsPure() &&
         method.getIsTask() == isTask && method.getImplementationAttr();
}

struct CachedMethod {
  bool computed = false;
  sim::SimClassMethodDeclOp method;
};

/// Cache immutable closed-world dispatch facts shared by thousands of UVM
/// call sites with the same receiver type and method slot.
class MonomorphicResolver {
public:
  explicit MonomorphicResolver(const analysis::ClassDispatchAnalysis &dispatch)
      : dispatch(dispatch) {}

  sim::SimClassMethodDeclOp resolveEffective(sim::SimClassDeclOp dynamicClass,
                                             uint64_t slot,
                                             uint64_t signatureId) {
    CachedMethod &cached = effective[dynamicClass.getOperation()]
                                    [std::make_pair(slot, signatureId)];
    if (!cached.computed) {
      cached.computed = true;
      cached.method = dispatch.resolve(dynamicClass, slot, signatureId);
    }
    return cached.method;
  }

  sim::SimClassMethodDeclOp resolveMonomorphic(sim::SimClassDeclOp staticClass,
                                               uint64_t slot,
                                               uint64_t signatureId,
                                               bool isTask) {
    auto key = std::make_pair(slot, signatureId);
    auto &byKind = monomorphic[staticClass.getOperation()][key];
    CachedMethod &cached = isTask ? byKind.task : byKind.function;
    if (cached.computed)
      return cached.method;
    cached.computed = true;

    auto compatible = compatibleClasses.find(staticClass.getOperation());
    if (compatible == compatibleClasses.end())
      compatible =
          compatibleClasses
              .try_emplace(staticClass.getOperation(),
                           dispatch.compatibleConcreteClasses(staticClass))
              .first;
    for (sim::SimClassDeclOp candidate : compatible->second) {
      sim::SimClassMethodDeclOp method =
          resolveEffective(candidate, slot, signatureId);
      if (!isDirectTarget(method, signatureId, isTask)) {
        cached.method = {};
        return {};
      }
      if (!cached.method)
        cached.method = method;
      else if (cached.method.getImplementationAttr() !=
               method.getImplementationAttr()) {
        cached.method = {};
        return {};
      }
    }
    return cached.method;
  }

private:
  struct CachedByKind {
    CachedMethod function;
    CachedMethod task;
  };
  using Slot = std::pair<uint64_t, uint64_t>;

  const analysis::ClassDispatchAnalysis &dispatch;
  DenseMap<Operation *, SmallVector<sim::SimClassDeclOp>> compatibleClasses;
  DenseMap<Operation *, DenseMap<Slot, CachedMethod>> effective;
  DenseMap<Operation *, DenseMap<Slot, CachedByKind>> monomorphic;
};

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

FailureOr<sim::SimCallOp> createDirectCall(IRRewriter &rewriter,
                                           Operation *operation,
                                           sim::SimFuncOp implementation,
                                           Value receiver, ValueRange arguments,
                                           TypeRange resultTypes) {
  sim::SimFuncOp caller = operation->getParentOfType<sim::SimFuncOp>();
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
  auto callee = FlatSymbolRefAttr::get(rewriter.getContext(),
                                       implementation.getSymName());
  return sim::SimCallOp::create(rewriter, operation->getLoc(), resultTypes,
                                callee, operands, ArrayAttr{}, ArrayAttr{});
}

LogicalResult createGuardedDirectCall(IRRewriter &rewriter,
                                      sim::SimClassVirtualCallOp call,
                                      sim::SimClassMethodDeclOp method,
                                      sim::SimFuncOp implementation) {
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
  Block *nullBlock = rewriter.createBlock(region, continuation->getIterator());
  Block *directBlock =
      rewriter.createBlock(region, continuation->getIterator());

  rewriter.setInsertionPointToEnd(head);
  cf::CondBranchOp::create(rewriter, location, isNull, nullBlock, ValueRange{},
                           directBlock, ValueRange{});

  rewriter.setInsertionPointToEnd(nullBlock);
  Value nullReceiver =
      sim::SimClassNullOp::create(rewriter, location, receiver.getType());
  auto fallback = sim::SimClassVirtualCallOp::create(
      rewriter, location, resultTypes, nullReceiver, call.getMethodAttr(),
      call.getSlotAttr(), call.getSignatureIdAttr(), arguments);
  cf::BranchOp::create(rewriter, location, continuation, fallback.getResults());

  rewriter.setInsertionPointToEnd(directBlock);
  FailureOr<sim::SimCallOp> direct = createDirectCall(
      rewriter, call, implementation, receiver, arguments, resultTypes);
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
createDirectTaskCall(IRRewriter &rewriter, sim::SimClassVirtualTaskCallOp call,
                     sim::SimClassMethodDeclOp method,
                     sim::SimFuncOp implementation) {
  sim::SimFuncOp caller = call->getParentOfType<sim::SimFuncOp>();
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
  auto callee = FlatSymbolRefAttr::get(rewriter.getContext(),
                                       implementation.getSymName());
  return sim::SimTaskCallOp::create(rewriter, call.getLoc(), callee, operands,
                                    rewriter.getI64IntegerAttr(argumentCount),
                                    call.getSiteAttr(), call.getContinuation());
}

LogicalResult createGuardedDirectTaskCall(IRRewriter &rewriter,
                                          sim::SimClassVirtualTaskCallOp call,
                                          sim::SimClassMethodDeclOp method,
                                          sim::SimFuncOp implementation) {
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
  Block *nullBlock = rewriter.createBlock(region, continuation->getIterator());
  Block *directBlock =
      rewriter.createBlock(region, continuation->getIterator());

  SmallVector<Value> fallbackValues(arguments);
  llvm::append_range(fallbackValues, continuationOperands);
  rewriter.setInsertionPointToEnd(nullBlock);
  Value nullReceiver =
      sim::SimClassNullOp::create(rewriter, location, receiver.getType());
  sim::SimClassVirtualTaskCallOp::create(
      rewriter, location, nullReceiver, call.getMethodAttr(),
      call.getSlotAttr(), call.getSignatureIdAttr(), fallbackValues,
      call.getArgumentCountAttr(), call.getSiteAttr(), continuation);

  rewriter.setInsertionPointToEnd(directBlock);
  if (failed(createDirectTaskCall(rewriter, call, method, implementation)))
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
  Statistic unresolvedTaskCalls{this, "unresolved-task-calls",
                                "virtual task calls retained conservatively"};
};

void ObeliskSimDevirtualizeClassCallsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  analysis::ClassDispatchAnalysis dispatch(design);
  MonomorphicResolver monomorphic(dispatch);
  ExactClassResolver resolver(dispatch);
  IRRewriter rewriter(design.getContext());
  llvm::StringMap<sim::SimFuncOp> functions;
  for (sim::SimFuncOp function : design.getBody().getOps<sim::SimFuncOp>())
    functions[function.getSymName()] = function;
  auto lookupImplementation = [&](sim::SimClassMethodDeclOp method) {
    if (!method || !method.getImplementationAttr())
      return sim::SimFuncOp{};
    auto found = functions.find(*method.getImplementation());
    return found == functions.end() ? sim::SimFuncOp{} : found->second;
  };

  SmallVector<sim::SimClassVirtualCallOp> virtualCalls;
  SmallVector<sim::SimClassVirtualTaskCallOp> virtualTaskCalls;
  design.walk([&](Operation *operation) {
    if (auto call = dyn_cast<sim::SimClassVirtualCallOp>(operation))
      virtualCalls.push_back(call);
    else if (auto call = dyn_cast<sim::SimClassVirtualTaskCallOp>(operation))
      virtualTaskCalls.push_back(call);
  });
  for (sim::SimClassVirtualCallOp call : virtualCalls) {
    sim::SimClassDeclOp dynamicClass = resolver.resolve(call.getReceiver());
    if (dynamicClass) {
      sim::SimClassMethodDeclOp method = monomorphic.resolveEffective(
          dynamicClass, call.getSlot(), call.getSignatureId());
      if (!isDirectTarget(method, call.getSignatureId(), /*isTask=*/false)) {
        ++unresolvedCalls;
        continue;
      }
      rewriter.setInsertionPoint(call);
      sim::SimFuncOp implementation = lookupImplementation(method);
      FailureOr<sim::SimCallOp> replacement =
          createDirectCall(rewriter, call, implementation, call.getReceiver(),
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
    sim::SimClassMethodDeclOp method = monomorphic.resolveMonomorphic(
        dispatch.lookup(staticType), call.getSlot(), call.getSignatureId(),
        /*isTask=*/false);
    if (!method) {
      ++unresolvedCalls;
      continue;
    }
    if (failed(createGuardedDirectCall(rewriter, call, method,
                                       lookupImplementation(method)))) {
      call.emitError(
          "monomorphic virtual method has no valid function implementation");
      return signalPassFailure();
    }
    ++monomorphicCalls;
    ++guardedCalls;
  }

  for (sim::SimClassVirtualTaskCallOp call : virtualTaskCalls) {
    sim::SimClassDeclOp dynamicClass = resolver.resolve(call.getReceiver());
    if (dynamicClass) {
      sim::SimClassMethodDeclOp method = monomorphic.resolveEffective(
          dynamicClass, call.getSlot(), call.getSignatureId());
      if (!isDirectTarget(method, call.getSignatureId(), /*isTask=*/true)) {
        ++unresolvedTaskCalls;
        continue;
      }
      rewriter.setInsertionPoint(call);
      if (failed(createDirectTaskCall(rewriter, call, method,
                                      lookupImplementation(method)))) {
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
    sim::SimClassMethodDeclOp method = monomorphic.resolveMonomorphic(
        dispatch.lookup(staticType), call.getSlot(), call.getSignatureId(),
        /*isTask=*/true);
    if (!method) {
      ++unresolvedTaskCalls;
      continue;
    }
    if (failed(createGuardedDirectTaskCall(rewriter, call, method,
                                           lookupImplementation(method)))) {
      call.emitError(
          "monomorphic virtual task has no valid task implementation");
      return signalPassFailure();
    }
    ++monomorphicTaskCalls;
    ++guardedTaskCalls;
  }

  uint64_t normalizedDirectCalls = 0;
  if (failed(sim::normalizeClassDirectCalls(design, &normalizedDirectCalls)))
    return signalPassFailure();
  directCalls += normalizedDirectCalls;
}

} // namespace
} // namespace obelisk
