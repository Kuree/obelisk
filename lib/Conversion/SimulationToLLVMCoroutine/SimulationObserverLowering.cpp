//===- SimulationObserverLowering.cpp - Native observer lowering ----------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

LogicalResult makeNativeObserverThunk(ModuleOp module,
                                      LLVM::LLVMFuncOp evaluator,
                                      uint32_t resultWidth, bool fourState) {
  MLIRContext *context = module.getContext();
  Location location = evaluator.getLoc();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  ArrayRef<Type> inputs = evaluator.getFunctionType().getParams();
  if (inputs.empty() || inputs.front() != pointer || resultWidth == 0)
    return evaluator.emitError("observer evaluator has an invalid native ABI");
  uint32_t captureCount = inputs.size() - 1;
  if (!llvm::all_of(inputs.drop_front(),
                    [&](Type type) { return type == i64; }))
    return evaluator.emitError(
        "observer captures must lower to stable 64-bit handles");
  unsigned expectedResults = fourState ? 2 : 1;
  SmallVector<Type> results;
  Type returnType = evaluator.getFunctionType().getReturnType();
  if (auto aggregate = dyn_cast<LLVM::LLVMStructType>(returnType))
    llvm::append_range(results, aggregate.getBody());
  else if (!isa<LLVM::LLVMVoidType>(returnType))
    results.push_back(returnType);
  unsigned resultCount = results.size();
  bool returnsStatus = resultCount == expectedResults + 1;
  if (resultCount != expectedResults && !returnsStatus)
    return evaluator.emitError(
        "observer result plane count does not match its descriptor");
  for (Type type : ArrayRef<Type>(results).take_front(expectedResults)) {
    auto integer = dyn_cast<IntegerType>(type);
    auto floating = dyn_cast<FloatType>(type);
    if ((!integer && !floating) ||
        (integer ? integer.getWidth() : floating.getWidth()) != resultWidth)
      return evaluator.emitError(
          "observer result plane width does not match its descriptor");
  }

  OpBuilder builder(module.getContext());
  std::string thunkName =
      (evaluator.getSymName() + ".__obelisk_observer").str();
  auto thunk = module.lookupSymbol<LLVM::LLVMFuncOp>(thunkName);
  if (!thunk)
    return evaluator.emitError(
        "native observer thunk declaration was not materialized");
  if (!thunk.getBody().empty())
    return evaluator.emitError("native observer thunk is defined twice");
  Block *entry = thunk.addEntryBlock(builder);
  builder.setInsertionPointToStart(entry);
  SmallVector<Value> arguments{entry->getArgument(0)};
  Value captures = entry->getArgument(1);
  for (uint32_t index = 0; index != captureCount; ++index)
    arguments.push_back(LLVM::LoadOp::create(
        builder, location, i64,
        byteGEP(builder, location, captures, uint64_t{index} * 8), 8));
  auto call = LLVM::CallOp::create(
      builder, location, TypeRange{returnType},
      SymbolRefAttr::get(context, evaluator.getSymName()), arguments);
  auto resultAt = [&](unsigned index) -> Value {
    if (resultCount == 1)
      return call.getResult();
    return LLVM::ExtractValueOp::create(
        builder, location, results[index], call.getResult(),
        ArrayRef<int64_t>{static_cast<int64_t>(index)});
  };
  uint32_t limbs = (resultWidth + 63) / 64;
  Value zero = llvmConstant(builder, location, i64, 0);
  for (uint32_t index = 0; index != limbs; ++index) {
    LLVM::StoreOp::create(
        builder, location, zero,
        byteGEP(builder, location, entry->getArgument(3), uint64_t{index} * 8),
        8);
    LLVM::StoreOp::create(
        builder, location, zero,
        byteGEP(builder, location, entry->getArgument(4), uint64_t{index} * 8),
        8);
  }
  LLVM::StoreOp::create(builder, location, resultAt(0), entry->getArgument(3),
                        1);
  if (fourState)
    LLVM::StoreOp::create(builder, location, resultAt(1), entry->getArgument(4),
                          1);
  Value status = returnsStatus ? resultAt(expectedResults)
                               : llvmConstant(builder, location, i32, 0);
  LLVM::ReturnOp::create(builder, location, status);
  return success();
}

} // namespace

LogicalResult materializeNativeObserverThunks(ModuleOp module) {
  SmallVector<LLVM::LLVMFuncOp> evaluators;
  module.walk([&](LLVM::LLVMFuncOp function) {
    if (function->hasAttr("obelisk.observer_width"))
      evaluators.push_back(function);
  });
  for (LLVM::LLVMFuncOp evaluator : evaluators) {
    auto width =
        evaluator->getAttrOfType<IntegerAttr>("obelisk.observer_width");
    auto fourState =
        evaluator->getAttrOfType<BoolAttr>("obelisk.observer_four_state");
    if (!width || !fourState ||
        failed(makeNativeObserverThunk(
            module, evaluator,
            static_cast<uint32_t>(width.getValue().getZExtValue()),
            fourState.getValue())))
      return failure();
  }
  return success();
}

} // namespace obelisk::detail
