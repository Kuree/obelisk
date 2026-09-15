//===- NativePartitionTest.cpp - Physical definition cost boundaries ------===//

#include "NativePartitionCost.h"

#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "gtest/gtest.h"

using obelisk::driver::detail::estimateNativeGlobalWeight;
using namespace llvm;

namespace {

TEST(NativePartitionCost, ReferencedInitializerIsChargedOnlyToItsDefinition) {
  LLVMContext context;
  Module module("references", context);
  auto *type = ArrayType::get(Type::getInt32Ty(context), 1024);
  auto *payload = new GlobalVariable(module, type, true,
      GlobalValue::InternalLinkage, ConstantAggregateZero::get(type), "payload");
  auto *reference = new GlobalVariable(module, payload->getType(), true,
      GlobalValue::InternalLinkage, payload, "reference");
  uint64_t referenceWeight = estimateNativeGlobalWeight(*reference);
  uint64_t payloadWeight = estimateNativeGlobalWeight(*payload);
  SmallVector<uint32_t> values(1024, 7);
  payload->setInitializer(ConstantDataArray::get(context, values));
  EXPECT_GT(estimateNativeGlobalWeight(*payload), payloadWeight);
  EXPECT_EQ(estimateNativeGlobalWeight(*reference), referenceWeight);
}

TEST(NativePartitionCost, CyclicReferenceGraphDoesNotChangeLocalWeights) {
  LLVMContext context;
  Module module("cycle", context);
  auto *pointer = PointerType::getUnqual(context);
  SmallVector<GlobalVariable *> globals;
  for (unsigned index = 0; index != 257; ++index)
    globals.push_back(new GlobalVariable(module, pointer, true,
        GlobalValue::InternalLinkage, ConstantPointerNull::get(pointer),
        "node" + Twine(index)));
  uint64_t localWeight = estimateNativeGlobalWeight(*globals.front());
  // Multiplication by 17 permutes this prime-sized graph. Every node has one
  // initializer address regardless of whether the target is itself, another
  // node, or part of a long cycle.
  for (unsigned index = 0; index != globals.size(); ++index)
    globals[index]->setInitializer(globals[(index * 17 + 1) % globals.size()]);
  for (GlobalVariable *global : globals)
    EXPECT_EQ(estimateNativeGlobalWeight(*global), localWeight);
}

TEST(NativePartitionCost, AliasReferenceDoesNotTraverseAliasee) {
  LLVMContext context;
  Module module("alias", context);
  auto *type = ArrayType::get(Type::getInt32Ty(context), 4096);
  auto *payload = new GlobalVariable(module, type, true,
      GlobalValue::InternalLinkage, ConstantAggregateZero::get(type), "payload");
  auto *alias = GlobalAlias::create(GlobalValue::InternalLinkage, "alias", payload);
  auto *reference = new GlobalVariable(module, alias->getType(), true,
      GlobalValue::InternalLinkage, alias, "reference");
  uint64_t weight = estimateNativeGlobalWeight(*reference);
  SmallVector<uint32_t> values(4096, 9);
  payload->setInitializer(ConstantDataArray::get(context, values));
  EXPECT_EQ(estimateNativeGlobalWeight(*reference), weight);
}

TEST(NativePartitionCost, InlineAggregateConstantsStillContribute) {
  LLVMContext context;
  Module module("inline", context);
  auto *array = ArrayType::get(Type::getInt32Ty(context), 1024);
  auto *type = StructType::get(context, {array, Type::getInt64Ty(context)});
  auto *global = new GlobalVariable(module, type, true,
      GlobalValue::InternalLinkage, ConstantAggregateZero::get(type), "data");
  uint64_t zeroWeight = estimateNativeGlobalWeight(*global);
  SmallVector<uint32_t> values(1024, 3);
  global->setInitializer(ConstantStruct::get(type,
      {ConstantDataArray::get(context, values),
       ConstantInt::get(Type::getInt64Ty(context), 1)}));
  EXPECT_GT(estimateNativeGlobalWeight(*global), zeroWeight);
}

} // namespace
