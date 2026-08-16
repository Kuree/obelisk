//===- SimulationProcessFunctionLowering.cpp - Native function lowering ===//

#include "SimulationProcessFunctionLowering.h"
#include "SimulationProcessWrapperLowering.h"

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Conversion/SimulationTimeLowering.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

#include <limits>

using namespace mlir;

namespace obelisk::detail {

namespace {

// Process control in a zero-time callable completes synchronously when it
// targets another process. A dynamically selected current process would have
// to preserve and later reconstruct the native call stack; until that ABI is
// available, return a checked lifecycle failure instead of miscompiling the
// continuation. Bytecode retains the complete callable-control behavior.
LogicalResult lowerCallableProcessControls(func::FuncOp function) {
  SmallVector<sim::SimProcessControlOp> controls;
  function.walk(
      [&](sim::SimProcessControlOp control) { controls.push_back(control); });
  if (controls.empty())
    return success();
  if (function.getResultTypes().empty() ||
      function.getResultTypes().back() != IntegerType::get(function.getContext(), 32))
    return function.emitError(
        "callable process control is missing its threaded status result");

  IRRewriter rewriter(function.getContext());
  Type i32 = rewriter.getI32Type();
  auto makeReturn = [&](Location location, Value status) -> LogicalResult {
    SmallVector<Value> results;
    for (Type type : function.getResultTypes().drop_back()) {
      Value zero = zeroNativeValue(rewriter, location, type);
      if (!zero)
        return function.emitError()
               << "cannot materialize callable-control result for " << type;
      results.push_back(zero);
    }
    results.push_back(status);
    emitManagedRootRangePop(rewriter, location, function);
    func::ReturnOp::create(rewriter, location, results);
    return success();
  };

  for (sim::SimProcessControlOp control : controls) {
    Location location = control.getLoc();
    Value controlledProcess = control.getProcess();
    sim::ProcessControlKind controlKind = control.getKind();
    Block *continuation = control.getContinuation();
    SmallVector<Value> continuationOperands(control.getContinuationOperands());
    Region *region = control->getParentRegion();
    Block *invokeControl = new Block;
    Block *dispatch = new Block;
    Block *continueAction = new Block;
    Block *unsupportedAction = new Block;
    Block *failureBlock = new Block;
    failureBlock->addArgument(i32, location);
    region->push_back(invokeControl);
    region->push_back(dispatch);
    region->push_back(continueAction);
    region->push_back(unsupportedAction);
    region->push_back(failureBlock);

    rewriter.setInsertionPoint(control);
    Value disposition = entryAlloca(rewriter, location, i32, 1, 4);
    LLVM::StoreOp::create(
        rewriter, location,
        llvmConstant(rewriter, location, i32,
                     OBELISK_RT_PROCESS_CONTROL_CONTINUE),
        disposition, 4);
    Value context = managedContextAndLane(rewriter, location).first;
    Value current =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI64Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_process_current"),
            ValueRange{context})
            .getResult();
    Value targetsCurrent = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::eq, current,
        controlledProcess);
    cf::CondBranchOp::create(rewriter, location, targetsCurrent,
                             unsupportedAction, ValueRange{}, invokeControl,
                             ValueRange{});
    rewriter.eraseOp(control);

    rewriter.setInsertionPointToStart(invokeControl);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_process_control"),
            ValueRange{context, controlledProcess,
                       llvmConstant(rewriter, location, i32,
                                    static_cast<uint32_t>(controlKind)),
                       disposition})
            .getResult();
    Value succeeded = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::eq, status,
        llvmConstant(rewriter, location, i32, OBELISK_RT_OK));
    cf::CondBranchOp::create(rewriter, location, succeeded, dispatch,
                             ValueRange{}, failureBlock, ValueRange{status});

    rewriter.setInsertionPointToStart(dispatch);
    Value selected =
        LLVM::LoadOp::create(rewriter, location, i32, disposition, 4);
    Value canContinue = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::eq, selected,
        llvmConstant(rewriter, location, i32,
                     OBELISK_RT_PROCESS_CONTROL_CONTINUE));
    cf::CondBranchOp::create(rewriter, location, canContinue, continueAction,
                             ValueRange{}, unsupportedAction, ValueRange{});

    rewriter.setInsertionPointToStart(continueAction);
    cf::BranchOp::create(rewriter, location, continuation,
                         continuationOperands);

    rewriter.setInsertionPointToStart(unsupportedAction);
    if (failed(makeReturn(
            location,
            llvmConstant(rewriter, location, i32,
                         OBELISK_RT_INVALID_LIFECYCLE))))
      return failure();

    rewriter.setInsertionPointToStart(failureBlock);
    if (failed(makeReturn(location, failureBlock->getArgument(0))))
      return failure();
  }
  return success();
}

} // namespace

FailureOr<PreparedPlainNativeProcess>
preparePlainNativeProcess(sim::SimFuncOp function,
                          const SimulationProcessFrameAnalysis &analysis) {
  if (!function.getResultTypes().empty())
    return function.emitError("simulation process cannot return values");
  if (failed(lowerSimulationTimeOperations(function)))
    return failure();
  ModuleOp module = function->getParentOfType<ModuleOp>();
  Location location = function.getLoc();
  MLIRContext *context = function.getContext();
  std::string baseName = function.getSymName().str();
  uint64_t stableID = function.getCodeUnitId().value_or(
      stableProcessID(baseName) &
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  context->getOrLoadDialect<func::FuncDialect>();
  SmallVector<Type> inputTypes;
  for (BlockArgument argument : function.getBody().front().getArguments())
    inputTypes.push_back(convertProcessType(argument.getType(), context));
  OpBuilder builder(function);
  auto body = func::FuncOp::create(
      builder, location, baseName,
      FunctionType::get(context, inputTypes, TypeRange{builder.getI32Type()}));
  body->setAttr("obelisk.entry_kind",
                builder.getI32IntegerAttr(
                    static_cast<uint32_t>(function.getEntryKind())));
  body->setAttr("obelisk.native_scratch_size", builder.getI64IntegerAttr(0));
  copyNativePartition(function, body);
  body.getBody().takeBody(function.getBody());
  function.erase();
  for (Block &block : body.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(convertProcessType(argument.getType(), context));
  return PreparedPlainNativeProcess{module, body, location, std::move(baseName),
                                    stableID, &analysis};
}

LogicalResult
lowerPreparedPlainNativeProcess(PreparedPlainNativeProcess &process) {
  func::FuncOp body = process.body;
  MLIRContext *context = body.getContext();
  if (failed(lowerNativeDPICalls(body)))
    return failure();

  IRRewriter rewriter(context);
  if (failed(lowerNativeFunctionBody(
          body, NativeReturnLowering::SuccessStatus,
          NativeCallResultLowering::Preserve)))
    return failure();

  SmallVector<sim::SimStatusCheckOp> checks;
  body.walk([&](sim::SimStatusCheckOp check) { checks.push_back(check); });
  for (sim::SimStatusCheckOp check : checks) {
    Block *source = check->getBlock();
    Block *continuation = source->splitBlock(std::next(check->getIterator()));
    Block *failure = new Block;
    body.getBody().push_back(failure);
    rewriter.setInsertionPoint(check);
    Value ok = runtime::RTStatusIsOp::create(
        rewriter, check.getLoc(), rewriter.getI1Type(), check.getStatus(), 0);
    cf::CondBranchOp::create(rewriter, check.getLoc(), ok, continuation,
                             ValueRange{}, failure, ValueRange{});
    Value status = check.getStatus();
    bool pushFailure = check->hasAttr(managedRootRangePushCheckAttr);
    rewriter.eraseOp(check);
    rewriter.setInsertionPointToStart(failure);
    Value bits = runtime::RTStatusToBitsOp::create(
        rewriter, body.getLoc(), rewriter.getI32Type(), status);
    if (!pushFailure)
      emitManagedRootRangePop(rewriter, body.getLoc(), body);
    func::ReturnOp::create(rewriter, body.getLoc(), bits);
  }

  return success();
}

LogicalResult
finishPreparedPlainNativeProcess(PreparedPlainNativeProcess &process) {
  if (failed(makePlainNativeWrappers(process.module, process.body,
                                     process.baseName, *process.analysis)))
    return failure();
  return makeProcessDescriptor(process.module, process.location,
                               process.baseName, process.stableID,
                               *process.analysis);
}

LogicalResult
lowerPlainNativeProcess(sim::SimFuncOp function,
                        const SimulationProcessFrameAnalysis &analysis) {
  FailureOr<PreparedPlainNativeProcess> prepared =
      preparePlainNativeProcess(function, analysis);
  if (failed(prepared) || failed(lowerPreparedPlainNativeProcess(*prepared)))
    return failure();
  return finishPreparedPlainNativeProcess(*prepared);
}

FailureOr<PreparedOrdinaryNativeFunction>
prepareOrdinaryFunction(sim::SimFuncOp function) {
  if (failed(lowerSimulationTimeOperations(function)))
    return failure();
  bool privateSymbol =
      SymbolTable::getSymbolVisibility(function) ==
      SymbolTable::Visibility::Private;
  Location location = function.getLoc();
  std::string symbolName = function.getSymName().str();
  FunctionType functionType = function.getFunctionType();
  SmallVector<Type> inputTypes;
  SmallVector<Type> resultTypes;
  for (Type type : functionType.getInputs())
    inputTypes.push_back(convertProcessType(type, function.getContext()));
  for (Type type : functionType.getResults())
    resultTypes.push_back(convertProcessType(type, function.getContext()));
  uint32_t entryKind = static_cast<uint32_t>(function.getEntryKind());
  bool observer = function.getEntryKind() == sim::EntryKind::Observer;
  auto observerWidth =
      function->getAttrOfType<IntegerAttr>("obelisk_sim.observer_width");
  auto observerFourState =
      function->getAttrOfType<BoolAttr>("obelisk_sim.observer_four_state");
  Attribute evalFourStateSource =
      function->getAttr("obelisk.eval.four_state_source");
  Attribute evalPromotionRanges =
      function->getAttr("obelisk.eval.local_promotion_ranges");
  Attribute evalConditionallyTwoState =
      function->getAttr("obelisk.eval.conditionally_two_state");
  Attribute evalPathKnownProbe =
      function->getAttr("obelisk.eval.path_known_probe");
  Attribute evalPathKnownPredicate =
      function->getAttr("obelisk.eval.path_known_predicate");
  function.getContext()->getOrLoadDialect<func::FuncDialect>();
  OpBuilder builder(function.getContext());
  builder.setInsertionPoint(function);
  auto replacement =
      func::FuncOp::create(builder, location, builder.getStringAttr(symbolName),
                           TypeAttr::get(FunctionType::get(
                               function.getContext(), inputTypes, resultTypes)),
                           StringAttr{}, ArrayAttr{}, ArrayAttr{}, UnitAttr{});
  if (privateSymbol)
    replacement.setPrivate();
  replacement->setAttr("obelisk.entry_kind",
                       builder.getI32IntegerAttr(entryKind));
  replacement->setAttr("obelisk.native_scratch_size",
                       builder.getI64IntegerAttr(0));
  copyNativePartition(function, replacement);
  if (evalFourStateSource)
    replacement->setAttr("obelisk.eval.four_state_source",
                         evalFourStateSource);
  if (evalPromotionRanges)
    replacement->setAttr("obelisk.eval.local_promotion_ranges",
                         evalPromotionRanges);
  if (evalConditionallyTwoState)
    replacement->setAttr("obelisk.eval.conditionally_two_state",
                         evalConditionallyTwoState);
  if (evalPathKnownProbe)
    replacement->setAttr("obelisk.eval.path_known_probe",
                         evalPathKnownProbe);
  if (evalPathKnownPredicate)
    replacement->setAttr("obelisk.eval.path_known_predicate",
                         evalPathKnownPredicate);
  replacement.getBody().takeBody(function.getBody());
  function.erase();
  for (Block &block : replacement.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(
          convertProcessType(argument.getType(), replacement.getContext()));
  return PreparedOrdinaryNativeFunction{replacement, observer, observerWidth,
                                        observerFourState};
}

LogicalResult lowerPreparedOrdinaryFunction(
    PreparedOrdinaryNativeFunction &function) {
  func::FuncOp replacement = function.body;
  if (failed(lowerNativeDPICalls(replacement)))
    return failure();
  if (failed(lowerNativeFunctionBody(
          replacement, NativeReturnLowering::Preserve,
          NativeCallResultLowering::ConvertProcessTypes)))
    return failure();
  if (failed(lowerCallableProcessControls(replacement)))
    return failure();
  if (function.observer) {
    if (!function.observerWidth || !function.observerFourState)
      return replacement.emitError(
          "observer entry is missing native descriptor metadata");
    OpBuilder builder(replacement.getContext());
    replacement->setAttr(
        "obelisk.observer_width",
        builder.getI32IntegerAttr(
            function.observerWidth.getValue().getZExtValue()));
    replacement->setAttr("obelisk.observer_four_state",
                         builder.getBoolAttr(
                             function.observerFourState.getValue()));
  }
  return success();
}

LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function) {
  FailureOr<PreparedOrdinaryNativeFunction> prepared =
      prepareOrdinaryFunction(function);
  if (failed(prepared))
    return failure();
  return lowerPreparedOrdinaryFunction(*prepared);
}

} // namespace obelisk::detail
