//===- SimulationProcessFunctionLowering.cpp - Native function lowering ===//

#include "SimulationProcessFunctionLowering.h"
#include "SimulationProcessWrapperLowering.h"

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Conversion/SimulationTimeLowering.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

#include <limits>

using namespace mlir;

namespace obelisk::detail {

LogicalResult
lowerPlainNativeProcess(sim::SimFuncOp function,
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
  body.getBody().takeBody(function.getBody());
  function.erase();
  for (Block &block : body.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(convertProcessType(argument.getType(), context));
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

  if (failed(makePlainNativeWrappers(module, body, baseName, analysis)))
    return failure();
  return makeProcessDescriptor(module, location, baseName, stableID, analysis);
}

LogicalResult lowerOrdinaryFunction(sim::SimFuncOp function) {
  if (failed(lowerSimulationTimeOperations(function)))
    return failure();
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
  function.getContext()->getOrLoadDialect<func::FuncDialect>();
  OpBuilder builder(function.getContext());
  builder.setInsertionPoint(function);
  auto replacement =
      func::FuncOp::create(builder, location, builder.getStringAttr(symbolName),
                           TypeAttr::get(FunctionType::get(
                               function.getContext(), inputTypes, resultTypes)),
                           StringAttr{}, ArrayAttr{}, ArrayAttr{}, UnitAttr{});
  replacement->setAttr("obelisk.entry_kind",
                       builder.getI32IntegerAttr(entryKind));
  replacement->setAttr("obelisk.native_scratch_size",
                       builder.getI64IntegerAttr(0));
  replacement.getBody().takeBody(function.getBody());
  function.erase();
  for (Block &block : replacement.getBody())
    for (BlockArgument argument : block.getArguments())
      argument.setType(
          convertProcessType(argument.getType(), replacement.getContext()));
  if (failed(lowerNativeDPICalls(replacement)))
    return failure();
  if (failed(lowerNativeFunctionBody(
          replacement, NativeReturnLowering::Preserve,
          NativeCallResultLowering::ConvertProcessTypes)))
    return failure();
  if (observer) {
    if (!observerWidth || !observerFourState)
      return replacement.emitError(
          "observer entry is missing native descriptor metadata");
    replacement->setAttr(
        "obelisk.observer_width",
        builder.getI32IntegerAttr(observerWidth.getValue().getZExtValue()));
    replacement->setAttr("obelisk.observer_four_state",
                         builder.getBoolAttr(observerFourState.getValue()));
  }
  return success();
}

} // namespace obelisk::detail
