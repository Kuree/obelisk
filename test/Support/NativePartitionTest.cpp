//===- NativePartitionTest.cpp - Physical definition cost boundaries ------===//

#include "NativeExecutionCounts.h"
#include "NativePartitionCost.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "gtest/gtest.h"

using obelisk::driver::detail::estimateNativeGlobalWeight;
using namespace llvm;

namespace {

TEST(NativeExecutionCounts, RequiresMainWithoutMutatingLibraryModule) {
  LLVMContext context;
  Module module("library", context);
  EXPECT_FALSE(obelisk::driver::detail::addNativeExecutionCounts(module));
  EXPECT_TRUE(module.empty());
  EXPECT_TRUE(module.global_empty());
}

TEST(NativeExecutionCounts, NoExecutorHasNoInstrumentationCost) {
  LLVMContext context;
  Module module("empty", context);
  IRBuilder<> builder(context);
  auto *main = Function::Create(FunctionType::get(builder.getInt32Ty(), false),
                                GlobalValue::ExternalLinkage, "main", module);
  builder.SetInsertPoint(BasicBlock::Create(context, "entry", main));
  builder.CreateRet(builder.getInt32(0));
  EXPECT_TRUE(obelisk::driver::detail::addNativeExecutionCounts(module));
  EXPECT_EQ(module.size(), 1u);
  EXPECT_EQ(main->getInstructionCount(), 1u);
  EXPECT_TRUE(module.global_empty());
}

TEST(NativeExecutionCounts, ReportsBothExitsAndKeepsDistinctDomainEntries) {
  LLVMContext context;
  Module module("entries", context);
  IRBuilder<> builder(context);
  auto *type = FunctionType::get(builder.getVoidTy(), false);
  for (StringRef name : {"__obelisk_eval_ranked_group_0",
                         "__obelisk_eval_ranked_group_0.dataflow",
                         "__obelisk_eval_dispatch_v1", "unrelated"}) {
    auto *function =
        Function::Create(type, GlobalValue::ExternalLinkage, name, module);
    function->addFnAttr(Attribute::Speculatable);
    builder.SetInsertPoint(BasicBlock::Create(context, "entry", function));
    builder.CreateRetVoid();
  }
  auto *main = Function::Create(
      FunctionType::get(builder.getInt32Ty(), {builder.getInt1Ty()}, false),
      GlobalValue::ExternalLinkage, "main", module);
  auto *entry = BasicBlock::Create(context, "entry", main);
  auto *success = BasicBlock::Create(context, "success", main);
  auto *failure = BasicBlock::Create(context, "failure", main);
  builder.SetInsertPoint(entry);
  builder.CreateCondBr(main->getArg(0), success, failure);
  builder.SetInsertPoint(success);
  auto *invoke =
      builder.CreateCall(module.getFunction("__obelisk_eval_ranked_group_0"));
  invoke->addFnAttr(Attribute::Speculatable);
  builder.CreateRet(builder.getInt32(0));
  builder.SetInsertPoint(failure);
  builder.CreateRet(builder.getInt32(1));
  ASSERT_TRUE(obelisk::driver::detail::addNativeExecutionCounts(module));
  EXPECT_FALSE(verifyModule(module, &errs()));
  auto *counts = module.getNamedGlobal("__obelisk_execution_counts");
  ASSERT_NE(counts, nullptr);
  EXPECT_EQ(cast<ArrayType>(counts->getValueType())->getNumElements(), 3u);
  EXPECT_TRUE(counts->getInitializer()->isNullValue());
  EXPECT_EQ(module.getFunction("unrelated")->getInstructionCount(), 1u);
  EXPECT_FALSE(module.getFunction("__obelisk_eval_ranked_group_0")
                   ->hasFnAttribute(Attribute::Speculatable));
  EXPECT_FALSE(invoke->hasFnAttr(Attribute::Speculatable));
  for (BasicBlock *block : {success, failure}) {
    auto *call = dyn_cast<CallInst>(block->getTerminator()->getPrevNode());
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->getCalledFunction()->getName(),
              "__obelisk_execution_counts_report");
  }
}

TEST(NativeExecutionCounts, CountsTakenLoopIterationsAcrossInvocations) {
  LLVMContext context;
  auto module = std::make_unique<Module>("executed-counts", context);
  module->setDataLayout("e-p:64:64-i64:64-n8:16:32:64-S128");
  IRBuilder<> builder(context);
  auto *leaf = Function::Create(FunctionType::get(builder.getVoidTy(), false),
                                GlobalValue::ExternalLinkage,
                                "__obelisk_eval_ranked_group_0", *module);
  builder.SetInsertPoint(BasicBlock::Create(context, "entry", leaf));
  builder.CreateRetVoid();
  auto *dispatch = Function::Create(
      FunctionType::get(builder.getVoidTy(), {builder.getInt64Ty()}, false),
      GlobalValue::ExternalLinkage, "__obelisk_eval_dispatch_v1", *module);
  auto *entry = BasicBlock::Create(context, "entry", dispatch);
  auto *loop = BasicBlock::Create(context, "loop", dispatch);
  auto *body = BasicBlock::Create(context, "body", dispatch);
  auto *done = BasicBlock::Create(context, "done", dispatch);
  builder.SetInsertPoint(entry);
  builder.CreateBr(loop);
  builder.SetInsertPoint(loop);
  auto *remaining = builder.CreatePHI(builder.getInt64Ty(), 2);
  remaining->addIncoming(dispatch->getArg(0), entry);
  builder.CreateCondBr(builder.CreateICmpEQ(remaining, builder.getInt64(0)),
                       done, body);
  builder.SetInsertPoint(body);
  builder.CreateCall(leaf);
  remaining->addIncoming(builder.CreateSub(remaining, builder.getInt64(1)),
                         body);
  builder.CreateBr(loop);
  builder.SetInsertPoint(done);
  builder.CreateRetVoid();
  auto *main = Function::Create(FunctionType::get(builder.getInt32Ty(), false),
                                GlobalValue::ExternalLinkage, "main", *module);
  builder.SetInsertPoint(BasicBlock::Create(context, "entry", main));
  builder.CreateRet(builder.getInt32(0));
  ASSERT_TRUE(obelisk::driver::detail::addNativeExecutionCounts(*module));
  auto *counts = module->getNamedGlobal("__obelisk_execution_counts");
  auto *read = Function::Create(
      FunctionType::get(builder.getInt64Ty(), {builder.getInt64Ty()}, false),
      GlobalValue::ExternalLinkage, "read_count", *module);
  builder.SetInsertPoint(BasicBlock::Create(context, "entry", read));
  builder.CreateRet(builder.CreateLoad(
      builder.getInt64Ty(),
      builder.CreateInBoundsGEP(counts->getValueType(), counts,
                                {builder.getInt32(0), read->getArg(0)})));
  ASSERT_FALSE(verifyModule(*module, &errs()));
  std::string error;
  std::unique_ptr<ExecutionEngine> engine(
      EngineBuilder(std::move(module))
          .setEngineKind(EngineKind::Interpreter)
          .setErrorStr(&error)
          .create());
  ASSERT_NE(engine, nullptr) << error;
  GenericValue argument;
  for (unsigned iterations : {0, 3, 5}) {
    argument.IntVal = APInt(64, iterations);
    engine->runFunction(dispatch, {argument});
  }
  argument.IntVal = APInt(64, 0);
  EXPECT_EQ(engine->runFunction(read, {argument}).IntVal.getZExtValue(), 3u);
  argument.IntVal = APInt(64, 1);
  EXPECT_EQ(engine->runFunction(read, {argument}).IntVal.getZExtValue(), 8u);
}

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
