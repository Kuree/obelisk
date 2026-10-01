//===- NativeGlobalInitializersTest.cpp - Table layout --------------------===//

#include "NativeGlobalInitializers.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

#include <functional>

using namespace mlir;

namespace {

TEST(NativeGlobalInitializers, PreservesOffsetsPointersZerosAndLastAssignment) {
  MLIRContext context;
  context.loadDialect<LLVM::LLVMDialect>();
  registerBuiltinDialectTranslation(context);
  registerLLVMDialectTranslation(context);
  OpBuilder builder(&context);
  auto location = builder.getUnknownLoc();
  OwningOpRef<ModuleOp> module = ModuleOp::create(location);
  auto i64 = builder.getI64Type();
  auto pointer = LLVM::LLVMPointerType::get(&context);
  auto element = LLVM::LLVMStructType::getLiteral(&context, {i64, pointer});
  constexpr unsigned count = 4097;
  auto array = LLVM::LLVMArrayType::get(element, count);
  builder.setInsertionPointToEnd(module->getBody());
  LLVM::LLVMFuncOp::create(builder, location, "callback",
                           LLVM::LLVMFunctionType::get(
                               LLVM::LLVMVoidType::get(&context), {}, false));
  auto global =
      LLVM::GlobalOp::create(builder, location, array, true,
                             LLVM::Linkage::Internal, "table", Attribute{}, 8);
  auto *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToEnd(block);
  Value value = LLVM::ZeroOp::create(builder, location, array);
  auto insert = [&](unsigned index, uint64_t number) {
    Value row = LLVM::ZeroOp::create(builder, location, element);
    Value numberValue = LLVM::ConstantOp::create(
        builder, location, i64, builder.getI64IntegerAttr(number));
    row = LLVM::InsertValueOp::create(builder, location, row, numberValue,
                                      ArrayRef<int64_t>{0});
    Value callback =
        LLVM::AddressOfOp::create(builder, location, pointer, "callback");
    row = LLVM::InsertValueOp::create(builder, location, row, callback,
                                      ArrayRef<int64_t>{1});
    value = LLVM::InsertValueOp::create(
        builder, location, value, row,
        ArrayRef<int64_t>{static_cast<int64_t>(index)});
  };
  for (unsigned index = 0; index != count; ++index)
    if (index % 7 != 0)
      insert(index, index * 17 + 3);
  insert(64, 999999);
  LLVM::ReturnOp::create(builder, location, value);
  llvm::DataLayout layout("e-p:64:64-i64:64-n8:16:32:64");
  ASSERT_EQ(
      obelisk::driver::detail::balanceNativeGlobalInitializers(*module, layout),
      1u);
  ASSERT_TRUE(succeeded(verify(*module)));
  llvm::LLVMContext llvmContext;
  auto translated = translateModuleToLLVMIR(*module, llvmContext);
  ASSERT_TRUE(translated);
  auto *record = llvm::StructType::get(
      llvmContext, {llvm::Type::getInt64Ty(llvmContext),
                    llvm::PointerType::getUnqual(llvmContext)});
  auto *table = translated->getNamedGlobal("table");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(layout.getTypeAllocSize(table->getValueType()),
            uint64_t(count) * layout.getTypeAllocSize(record));
  EXPECT_EQ(layout.getABITypeAlign(table->getValueType()),
            layout.getABITypeAlign(llvm::ArrayType::get(record, count)));
  unsigned visited = 0;
  std::function<void(llvm::Constant *, uint64_t)> check;
  check = [&](llvm::Constant *constant, uint64_t offset) {
    auto *type = constant->getType();
    if (type == record) {
      unsigned index = visited++;
      EXPECT_EQ(offset, uint64_t(index) * layout.getTypeAllocSize(record));
      auto *number =
          llvm::cast<llvm::ConstantInt>(constant->getAggregateElement(0u));
      uint64_t expected = index == 64      ? 999999
                          : index % 7 == 0 ? 0
                                           : index * 17 + 3;
      EXPECT_EQ(number->getZExtValue(), expected) << index;
      auto *address = constant->getAggregateElement(1u);
      if (index % 7 == 0)
        EXPECT_TRUE(address->isNullValue());
      else
        EXPECT_EQ(address, translated->getFunction("callback"));
      return;
    }
    if (auto *structure = llvm::dyn_cast<llvm::StructType>(type)) {
      auto *offsets = layout.getStructLayout(structure);
      for (unsigned index = 0; index != structure->getNumElements(); ++index)
        check(constant->getAggregateElement(index),
              offset + offsets->getElementOffset(index));
      return;
    }
    auto *arrayType = llvm::cast<llvm::ArrayType>(type);
    uint64_t stride = layout.getTypeAllocSize(arrayType->getElementType());
    for (unsigned index = 0; index != arrayType->getNumElements(); ++index)
      check(constant->getAggregateElement(index), offset + index * stride);
  };
  check(table->getInitializer(), 0);
  EXPECT_EQ(visited, count);
}

TEST(NativeGlobalInitializers, PreservesArrayWithStrongerStructAlignment) {
  MLIRContext context;
  context.loadDialect<LLVM::LLVMDialect>();
  OpBuilder builder(&context);
  auto location = builder.getUnknownLoc();
  OwningOpRef<ModuleOp> module = ModuleOp::create(location);
  auto array = LLVM::LLVMArrayType::get(builder.getI8Type(), 257);
  builder.setInsertionPointToEnd(module->getBody());
  auto global =
      LLVM::GlobalOp::create(builder, location, array, true,
                             LLVM::Linkage::Internal, "bytes", Attribute{});
  auto *block = new Block;
  global.getInitializerRegion().push_back(block);
  builder.setInsertionPointToEnd(block);
  Value value = LLVM::ZeroOp::create(builder, location, array);
  Value one = LLVM::ConstantOp::create(builder, location, builder.getI8Type(),
                                       builder.getI8IntegerAttr(1));
  for (int64_t index = 0; index != 257; ++index)
    value = LLVM::InsertValueOp::create(builder, location, value, one,
                                        ArrayRef<int64_t>{index});
  LLVM::ReturnOp::create(builder, location, value);
  llvm::DataLayout layout("e-p:64:64-i64:64-a:64:64");
  EXPECT_EQ(
      obelisk::driver::detail::balanceNativeGlobalInitializers(*module, layout),
      0u);
  EXPECT_EQ(global.getGlobalType(), array);
  EXPECT_TRUE(succeeded(verify(*module)));
}

} // namespace
