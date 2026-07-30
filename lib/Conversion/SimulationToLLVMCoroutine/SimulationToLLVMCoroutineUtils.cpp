//===- SimulationToLLVMCoroutineUtils.cpp - Shared lowering support -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

#include "llvm/ADT/SmallVector.h"

#include <cassert>

using namespace mlir;

namespace obelisk::detail {

Value llvmConstant(OpBuilder &builder, Location location, Type type,
                   uint64_t value) {
  return LLVM::ConstantOp::create(builder, location, type,
                                  builder.getIntegerAttr(type, value));
}

Value entryAlloca(OpBuilder &builder, Location location, Type elementType,
                  uint64_t count, unsigned alignment) {
  Block *insertionBlock = builder.getInsertionBlock();
  assert(insertionBlock && insertionBlock->getParent() &&
         !insertionBlock->getParent()->empty() &&
         "entry allocation requires a function body");
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(&insertionBlock->getParent()->front());
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Value countValue =
      llvmConstant(builder, location, builder.getI64Type(), count);
  return LLVM::AllocaOp::create(builder, location, pointer, elementType,
                                countValue, alignment);
}

Value byteGEP(OpBuilder &builder, Location location, Value base,
              uint64_t offset) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i8 = builder.getI8Type();
  constexpr uint64_t maxConstantIndex =
      (uint64_t{1} << (LLVM::kGEPConstantBitWidth - 1)) - 1;
  SmallVector<LLVM::GEPArg> indices;
  if (offset <= maxConstantIndex)
    indices.emplace_back(static_cast<int32_t>(offset));
  else
    indices.emplace_back(
        llvmConstant(builder, location, builder.getI64Type(), offset));
  return LLVM::GEPOp::create(builder, location, pointer, i8, base, indices);
}

Value loadAt(OpBuilder &builder, Location location, Value base, uint64_t offset,
             Type type, unsigned alignment) {
  return LLVM::LoadOp::create(builder, location, type,
                              byteGEP(builder, location, base, offset),
                              alignment);
}

void storeAt(OpBuilder &builder, Location location, Value base, uint64_t offset,
             Value value, unsigned alignment) {
  LLVM::StoreOp::create(builder, location, value,
                        byteGEP(builder, location, base, offset), alignment);
}

Value castIntegerWidth(OpBuilder &builder, Location location, Value value,
                       Type target) {
  auto source = cast<IntegerType>(value.getType());
  auto destination = cast<IntegerType>(target);
  if (source.getWidth() == destination.getWidth())
    return value;
  if (source.getWidth() < destination.getWidth())
    return arith::ExtUIOp::create(builder, location, target, value);
  return arith::TruncIOp::create(builder, location, target, value);
}

LLVM::LLVMFuncOp getOrDeclareLLVMFunction(ModuleOp module, StringRef name,
                                          Type result,
                                          ArrayRef<Type> arguments) {
  if (auto existing = module.lookupSymbol<LLVM::LLVMFuncOp>(name))
    return existing;
  OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  return LLVM::LLVMFuncOp::create(
      builder, module.getLoc(), name,
      LLVM::LLVMFunctionType::get(result, arguments, false));
}

} // namespace obelisk::detail
