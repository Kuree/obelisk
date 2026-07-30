//===- SimulationRuntimeStatusThreading.cpp - Thread runtime statuses ---===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Analysis/CallGraph.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"

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

  CallGraph callGraph(module);
  DenseMap<CallGraphNode *, sim::SimFuncOp> functionsByNode;
  for (sim::SimFuncOp function : orderedFunctions)
    if (CallGraphNode *node = callGraph.lookupNode(&function.getBody()))
      functionsByNode.try_emplace(node, function);

  DenseMap<CallGraphNode *, SmallVector<CallGraphNode *>> callers;
  for (CallGraphNode *caller : callGraph)
    for (const CallGraphNode::Edge &edge : *caller)
      if (edge.isCall())
        callers[edge.getTarget()].push_back(caller);

  llvm::SetVector<CallGraphNode *> mayFail;
  for (auto [node, function] : functionsByNode) {
    bool hasCheck = false;
    function.walk([&](sim::SimStatusCheckOp) { hasCheck = true; });
    if (hasCheck)
      mayFail.insert(node);
  }
  for (size_t index = 0; index != mayFail.size(); ++index)
    for (CallGraphNode *caller : callers[mayFail[index]])
      if (functionsByNode.contains(caller))
        mayFail.insert(caller);

  llvm::DenseSet<CallGraphNode *> statusReturning;
  OpBuilder builder(context);
  Type statusBitsType = builder.getI32Type();
  for (sim::SimFuncOp function : orderedFunctions) {
    CallGraphNode *node = callGraph.lookupNode(&function.getBody());
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

  SmallVector<sim::SimCallOp> calls;
  module.walk([&](sim::SimCallOp call) { calls.push_back(call); });
  SymbolTableCollection symbolTables;
  IRRewriter rewriter(context);
  for (sim::SimCallOp call : calls) {
    CallGraphNode *callee =
        callGraph.resolveCallable(cast<CallOpInterface>(call.getOperation()),
                                  symbolTables);
    if (!statusReturning.contains(callee))
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

  for (sim::SimFuncOp function : orderedFunctions) {
    if (!statusReturning.contains(callGraph.lookupNode(&function.getBody())))
      continue;
    SmallVector<sim::SimReturnOp> returns;
    function.walk(
        [&](sim::SimReturnOp operation) { returns.push_back(operation); });
    for (sim::SimReturnOp operation : returns) {
      rewriter.setInsertionPoint(operation);
      Value zero = arith::ConstantOp::create(rewriter, operation.getLoc(),
                                             rewriter.getI32Type(),
                                             rewriter.getI32IntegerAttr(0));
      SmallVector<Value> operands(operation.getOperands());
      operands.push_back(zero);
      sim::SimReturnOp::create(rewriter, operation.getLoc(), operands);
      rewriter.eraseOp(operation);
    }

    SmallVector<sim::SimStatusCheckOp> checks;
    function.walk(
        [&](sim::SimStatusCheckOp check) { checks.push_back(check); });
    for (sim::SimStatusCheckOp check : checks) {
      Block *source = check->getBlock();
      Block *continuation = source->splitBlock(std::next(check->getIterator()));
      Block *failure = new Block;
      function.getBody().push_back(failure);
      rewriter.setInsertionPoint(check);
      Value ok = runtime::RTStatusIsOp::create(
          rewriter, check.getLoc(), rewriter.getI1Type(), check.getStatus(), 0);
      cf::CondBranchOp::create(rewriter, check.getLoc(), ok, continuation,
                               ValueRange{}, failure, ValueRange{});
      Value status = check.getStatus();
      bool pushFailure = check->hasAttr(managedRootRangePushCheckAttr);
      rewriter.eraseOp(check);

      rewriter.setInsertionPointToStart(failure);
      SmallVector<Value> values;
      for (Type type : function.getResultTypes().drop_back()) {
        auto integer = dyn_cast<IntegerType>(type);
        if (!integer)
          return function.emitError()
                 << "cannot materialize a failure result for " << type;
        values.push_back(
            arith::ConstantOp::create(rewriter, function.getLoc(), integer,
                                      rewriter.getIntegerAttr(integer, 0)));
      }
      values.push_back(runtime::RTStatusToBitsOp::create(
          rewriter, function.getLoc(), rewriter.getI32Type(), status));
      if (!pushFailure)
        emitManagedRootRangePop(rewriter, function.getLoc(), function);
      sim::SimReturnOp::create(rewriter, function.getLoc(), values);
    }
  }
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
