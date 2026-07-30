//===- SimulationToLLVMCoroutineUtils.cpp - Shared lowering support -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationAnalysis.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MathExtras.h"

#include <cassert>
#include <limits>

using namespace mlir;

namespace obelisk::detail {

bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result) {
  if (value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
    return false;
  result = llvm::alignTo(value, alignment);
  return true;
}

std::optional<unsigned> nativeStateWidth(Type type) {
  return analysis::getSimulationStorageBitWidth(type);
}

SmallVector<Value> flatten(ArrayRef<ValueRange> ranges) {
  SmallVector<Value> values;
  for (ValueRange range : ranges)
    llvm::append_range(values, range);
  return values;
}

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

Value insertValue(OpBuilder &builder, Location location, Value aggregate,
                  Value element, int64_t index) {
  return LLVM::InsertValueOp::create(builder, location, aggregate, element,
                                     ArrayRef<int64_t>{index});
}

void emitNativeStateRetain(OpBuilder &builder, Location location,
                           Value handle) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Value contextAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value status = LLVM::CallOp::create(
                     builder, location, TypeRange{i32},
                     SymbolRefAttr::get(builder.getContext(),
                                        "obelisk_rt_v1_native_state_retain"),
                     ValueRange{context, handle})
                     .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(), "obelisk_rt_v1_scheduler_fail"),
      ValueRange{context, status});
}

std::string managedClassDescriptorName(SymbolRefAttr className) {
  return (className.getRootReference().getValue() +
          ".__obelisk_class_descriptor")
      .str();
}

std::string managedMethodThunkName(StringRef methodName) {
  return (methodName + ".__obelisk_native_thunk").str();
}

LLVM::GlobalOp makeByteArrayGlobal(ModuleOp module, Location location,
                                   StringRef name, StringRef bytes) {
  MLIRContext *context = module.getContext();
  Type i8 = IntegerType::get(context, 8);
  Type type = LLVM::LLVMArrayType::get(i8, bytes.size());
  OpBuilder builder(context);
  builder.setInsertionPointToStart(module.getBody());
  auto global =
      LLVM::GlobalOp::create(builder, location, type, true,
                             LLVM::Linkage::Internal, name, Attribute{}, 1);
  Block *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToStart(block);
  Value value = LLVM::ZeroOp::create(builder, location, type);
  for (auto [index, byte] : llvm::enumerate(bytes.bytes()))
    value = LLVM::InsertValueOp::create(
        builder, location, value, llvmConstant(builder, location, i8, byte),
        ArrayRef<int64_t>{static_cast<int64_t>(index)});
  LLVM::ReturnOp::create(builder, location, value);
  return global;
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
