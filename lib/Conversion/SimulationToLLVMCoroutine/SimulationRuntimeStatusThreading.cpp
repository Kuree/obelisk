//===- SimulationRuntimeStatusThreading.cpp - Thread runtime statuses ---===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Threading.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMTHREADRUNTIMESTATUSESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace detail {

LogicalResult threadRuntimeStatuses(ModuleOp module) {
  MLIRContext *context = module.getContext();
  context->getOrLoadDialect<cf::ControlFlowDialect>();
  SmallVector<sim::SimFuncOp> orderedFunctions;
  module.walk(
      [&](sim::SimFuncOp function) { orderedFunctions.push_back(function); });

  DenseMap<Operation *, llvm::StringMap<sim::SimFuncOp>> functionsByScope;
  for (sim::SimFuncOp function : orderedFunctions)
    functionsByScope[function->getParentOp()].try_emplace(function.getSymName(),
                                                          function);
  auto lookupFunction = [&](sim::SimFuncOp caller,
                            StringRef name) -> sim::SimFuncOp {
    auto scope = functionsByScope.find(caller->getParentOp());
    if (scope == functionsByScope.end())
      return {};
    auto found = scope->second.find(name);
    return found == scope->second.end() ? sim::SimFuncOp{} : found->second;
  };

  DenseMap<Operation *, SmallVector<Operation *>> callers;
  DenseMap<Operation *, Operation *> callees;
  SmallVector<sim::SimCallOp> calls;
  SmallVector<sim::SimClassDirectCallOp> directCalls;
  llvm::SetVector<Operation *> mayFail;
  for (sim::SimFuncOp function : orderedFunctions) {
    bool hasCheck = false;
    function.walk([&](Operation *operation) {
      hasCheck |=
          isa<sim::SimStatusCheckOp, sim::SimProcessControlOp>(operation);
      if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
        calls.push_back(call);
        if (sim::SimFuncOp callee =
                lookupFunction(function, call.getCallee())) {
          callees[call.getOperation()] = callee.getOperation();
          callers[callee.getOperation()].push_back(function.getOperation());
        }
      } else if (auto call = dyn_cast<sim::SimClassDirectCallOp>(operation)) {
        directCalls.push_back(call);
        if (sim::SimFuncOp callee =
                lookupFunction(function, call.getCallee())) {
          callees[call.getOperation()] = callee.getOperation();
          callers[callee.getOperation()].push_back(function.getOperation());
        }
      }
    });
    if (hasCheck)
      mayFail.insert(function.getOperation());
  }
  for (size_t index = 0; index != mayFail.size(); ++index)
    for (Operation *caller : callers[mayFail[index]])
      mayFail.insert(caller);

  llvm::DenseSet<Operation *> statusReturning;
  OpBuilder builder(context);
  Type statusBitsType = builder.getI32Type();
  for (sim::SimFuncOp function : orderedFunctions) {
    Operation *node = function.getOperation();
    if (!mayFail.contains(node) ||
        (function.getEntryKind() != sim::EntryKind::Function &&
         function.getEntryKind() != sim::EntryKind::Observer))
      continue;
    statusReturning.insert(node);
    SmallVector<Type> results(function.getResultTypes());
    results.push_back(statusBitsType);
    function.setType(
        FunctionType::get(context, function.getArgumentTypes(), results));
    SmallVector<Attribute> resultAttrs;
    if (auto attrs = function.getResAttrs())
      llvm::append_range(resultAttrs, *attrs);
    while (resultAttrs.size() != results.size())
      resultAttrs.push_back(builder.getDictionaryAttr({}));
    function.setResAttrsAttr(builder.getArrayAttr(resultAttrs));
  }

  IRRewriter rewriter(context);
  for (sim::SimCallOp call : calls) {
    sim::SimFuncOp callee =
        dyn_cast_or_null<sim::SimFuncOp>(callees.lookup(call.getOperation()));
    if (!callee || !statusReturning.contains(callee.getOperation()))
      continue;
    SmallVector<Type> results(call.getResultTypes());
    results.push_back(statusBitsType);
    SmallVector<Attribute> resultAttrs;
    if (auto attrs = call.getResAttrs())
      llvm::append_range(resultAttrs, *attrs);
    while (resultAttrs.size() != results.size())
      resultAttrs.push_back(rewriter.getDictionaryAttr({}));
    rewriter.setInsertionPoint(call);
    auto replacement = sim::SimCallOp::create(
        rewriter, call.getLoc(), results, call.getCalleeAttr(),
        call.getOperands(), call.getArgAttrsAttr(),
        rewriter.getArrayAttr(resultAttrs));
    for (auto [oldResult, newResult] : llvm::zip_equal(
             call.getResults(), replacement.getResults().drop_back()))
      oldResult.replaceAllUsesWith(newResult);
    rewriter.setInsertionPointAfter(replacement);
    Value status = runtime::RTStatusFromBitsOp::create(
        rewriter, call.getLoc(), runtime::StatusType::get(context),
        replacement.getResults().back());
    sim::SimStatusCheckOp::create(rewriter, call.getLoc(), status);
    rewriter.eraseOp(call);
  }
  for (sim::SimClassDirectCallOp call : directCalls) {
    sim::SimFuncOp callee =
        dyn_cast_or_null<sim::SimFuncOp>(callees.lookup(call.getOperation()));
    if (!callee || !statusReturning.contains(callee.getOperation()))
      continue;
    SmallVector<Type> results(call.getResultTypes());
    results.push_back(statusBitsType);
    rewriter.setInsertionPoint(call);
    auto replacement = sim::SimClassDirectCallOp::create(
        rewriter, call.getLoc(), results, call.getCalleeAttr(),
        call.getReceiver(), call.getArguments());
    for (auto [oldResult, newResult] : llvm::zip_equal(
             call.getResults(), replacement.getResults().drop_back()))
      oldResult.replaceAllUsesWith(newResult);
    rewriter.setInsertionPointAfter(replacement);
    Value status = runtime::RTStatusFromBitsOp::create(
        rewriter, call.getLoc(), runtime::StatusType::get(context),
        replacement.getResults().back());
    sim::SimStatusCheckOp::create(rewriter, call.getLoc(), status);
    rewriter.eraseOp(call);
  }

  SmallVector<SmallVector<sim::SimFuncOp>> statusFunctionChunks;
  constexpr size_t functionsPerChunk = 64;
  for (sim::SimFuncOp function : orderedFunctions)
    if (statusReturning.contains(function.getOperation())) {
      if (statusFunctionChunks.empty() ||
          statusFunctionChunks.back().size() == functionsPerChunk)
        statusFunctionChunks.emplace_back();
      statusFunctionChunks.back().push_back(function);
    }
  if (failed(failableParallelForEach(
          context, statusFunctionChunks,
          [&](ArrayRef<sim::SimFuncOp> functions) -> LogicalResult {
            for (sim::SimFuncOp function : functions) {
              IRRewriter localRewriter(context);
              SmallVector<sim::SimReturnOp> returns;
              SmallVector<sim::SimStatusCheckOp> checks;
              function.walk([&](Operation *operation) {
                if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation))
                  returns.push_back(returnOp);
                else if (auto check =
                             dyn_cast<sim::SimStatusCheckOp>(operation))
                  checks.push_back(check);
              });
              for (sim::SimReturnOp operation : returns) {
                localRewriter.setInsertionPoint(operation);
                Value zero = arith::ConstantOp::create(
                    localRewriter, operation.getLoc(),
                    localRewriter.getI32Type(),
                    localRewriter.getI32IntegerAttr(0));
                SmallVector<Value> operands(operation.getOperands());
                operands.push_back(zero);
                sim::SimReturnOp::create(localRewriter, operation.getLoc(),
                                         operands);
                localRewriter.eraseOp(operation);
              }

              for (sim::SimStatusCheckOp check : checks) {
                Block *source = check->getBlock();
                Block *continuation =
                    source->splitBlock(std::next(check->getIterator()));
                Block *failure = new Block;
                function.getBody().push_back(failure);
                localRewriter.setInsertionPoint(check);
                Value ok = runtime::RTStatusIsOp::create(
                    localRewriter, check.getLoc(), localRewriter.getI1Type(),
                    check.getStatus(), 0);
                cf::CondBranchOp::create(localRewriter, check.getLoc(), ok,
                                         continuation, ValueRange{}, failure,
                                         ValueRange{});
                Value status = check.getStatus();
                bool pushFailure =
                    check->hasAttr(managedRootRangePushCheckAttr);
                localRewriter.eraseOp(check);

                localRewriter.setInsertionPointToStart(failure);
                SmallVector<Value> values;
                for (Type type : function.getResultTypes().drop_back()) {
                  if (auto integer = dyn_cast<IntegerType>(type)) {
                    values.push_back(arith::ConstantOp::create(
                        localRewriter, function.getLoc(), integer,
                        localRewriter.getIntegerAttr(integer, 0)));
                    continue;
                  }
                  if (auto floating = dyn_cast<FloatType>(type)) {
                    values.push_back(arith::ConstantOp::create(
                        localRewriter, function.getLoc(), floating,
                        localRewriter.getFloatAttr(floating, 0.0)));
                    continue;
                  }
                  return function.emitError()
                         << "cannot materialize a failure result for " << type;
                }
                values.push_back(runtime::RTStatusToBitsOp::create(
                    localRewriter, function.getLoc(),
                    localRewriter.getI32Type(), status));
                if (!pushFailure)
                  emitManagedRootRangePop(localRewriter, function.getLoc(),
                                          function);
                sim::SimReturnOp::create(localRewriter, function.getLoc(),
                                         values);
              }
            }
            return success();
          })))
    return failure();
  return success();
}

} // namespace detail

namespace {

class ObeliskSimThreadRuntimeStatusesPass
    : public impl::ObeliskSimThreadRuntimeStatusesPassBase<
          ObeliskSimThreadRuntimeStatusesPass> {
public:
  void runOnOperation() override {
    if (failed(detail::threadRuntimeStatuses(getOperation())))
      signalPassFailure();
  }
};

} // namespace
} // namespace obelisk
