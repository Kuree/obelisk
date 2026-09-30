//===- NativeExecutionCounts.h - Opt-in generated execution diagnostics
//----===//

#ifndef OBELISK_DRIVER_NATIVEEXECUTIONCOUNTS_H
#define OBELISK_DRIVER_NATIVEEXECUTIONCOUNTS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

namespace obelisk::driver::detail {

/// Counts generated function entries before native optimization/partitioning.
/// This is deliberately opt-in: ordinary modules gain no counters, branches,
/// runtime hooks or report strings. Counts describe the instrumented execution,
/// not timing or instruction costs of an uninstrumented binary.
inline bool addNativeExecutionCounts(llvm::Module &module) {
  using namespace llvm;
  Function *main = module.getFunction("main");
  if (!main || main->isDeclaration())
    return false;
  SmallVector<Function *> functions;
  for (Function &function : module) {
    StringRef name = function.getName();
    if (!function.isDeclaration() &&
        (name.starts_with("__obelisk_eval_ranked_group_") ||
         name.starts_with("__obelisk_direct_fragment_") ||
         name == "__obelisk_eval_dispatch_v1" ||
         name.starts_with("__obelisk_aot_static_nba_commit") ||
         name.contains(".__obelisk_eval_body_")))
      functions.push_back(&function);
  }
  if (functions.empty())
    return true;
  llvm::sort(functions, [](Function *a, Function *b) {
    return a->getName() < b->getName();
  });
  LLVMContext &context = module.getContext();
  IRBuilder<> builder(context);
  Type *i64 = builder.getInt64Ty();
  auto *countsType = ArrayType::get(i64, functions.size());
  auto *counts = new GlobalVariable(
      module, countsType, false, GlobalValue::InternalLinkage,
      ConstantAggregateZero::get(countsType), "__obelisk_execution_counts");
  counts->setAlignment(Align(8));
  SmallVector<Constant *> names;
  for (auto [index, function] : llvm::enumerate(functions)) {
    builder.SetInsertPoint(&*function->getEntryBlock().getFirstInsertionPt());
    Value *address = builder.CreateInBoundsGEP(
        countsType, counts, {builder.getInt32(0), builder.getInt32(index)});
    // Volatile preserves the entry-count observation across inlining and
    // loop transforms. Simulation is single threaded; these are not atomics.
    auto *old = builder.CreateLoad(i64, address);
    old->setVolatile(true);
    auto *store = builder.CreateStore(
        builder.CreateAdd(old, builder.getInt64(1)), address);
    store->setVolatile(true);
    names.push_back(builder.CreateGlobalString(function->getName(),
                                               "__obelisk_execution_name"));
  }
  auto *namesType = ArrayType::get(builder.getPtrTy(), names.size());
  auto *nameTable = new GlobalVariable(
      module, namesType, true, GlobalValue::InternalLinkage,
      ConstantArray::get(namesType, names), "__obelisk_execution_names");
  auto *report = Function::Create(FunctionType::get(builder.getVoidTy(), false),
                                  GlobalValue::InternalLinkage,
                                  "__obelisk_execution_counts_report", module);
  report->addFnAttr(Attribute::NoInline);
  auto *entry = BasicBlock::Create(context, "entry", report);
  auto *loop = BasicBlock::Create(context, "loop", report);
  auto *print = BasicBlock::Create(context, "print", report);
  auto *next = BasicBlock::Create(context, "next", report);
  auto *done = BasicBlock::Create(context, "done", report);
  builder.SetInsertPoint(entry);
  Constant *format = builder.CreateGlobalString(
      "obelisk-execution-count\t%s\t%llu\n", "__obelisk_execution_format");
  auto printf = module.getOrInsertFunction(
      "printf",
      FunctionType::get(builder.getInt32Ty(), {builder.getPtrTy()}, true));
  builder.CreateBr(loop);
  builder.SetInsertPoint(loop);
  auto *index = builder.CreatePHI(i64, 2);
  index->addIncoming(builder.getInt64(0), entry);
  Value *value = builder.CreateLoad(
      i64, builder.CreateInBoundsGEP(countsType, counts,
                                     {builder.getInt32(0), index}));
  builder.CreateCondBr(builder.CreateICmpNE(value, builder.getInt64(0)), print,
                       next);
  builder.SetInsertPoint(print);
  Value *name = builder.CreateLoad(
      builder.getPtrTy(),
      builder.CreateInBoundsGEP(namesType, nameTable,
                                {builder.getInt32(0), index}));
  builder.CreateCall(printf, {format, name, value});
  builder.CreateBr(next);
  builder.SetInsertPoint(next);
  Value *incremented = builder.CreateAdd(index, builder.getInt64(1));
  index->addIncoming(incremented, next);
  builder.CreateCondBr(
      builder.CreateICmpULT(incremented, builder.getInt64(functions.size())),
      loop, done);
  builder.SetInsertPoint(done);
  builder.CreateRetVoid();
  // Report on every ordinary generated-main exit, after simulator output.
  // No destructor, foreign callback or second scheduler is installed.
  for (BasicBlock &block : *main)
    if (auto *ret = dyn_cast<ReturnInst>(block.getTerminator())) {
      builder.SetInsertPoint(ret);
      builder.CreateCall(report);
    }
  // Existing memory/speculation promises can also live on callers or call
  // sites. They describe the uninstrumented graph. Drop them before LLVM
  // recomputes effects, so an originally pure call cannot discard its count.
  for (Function &function : module) {
    if (function.isDeclaration())
      continue;
    function.removeFnAttr(Attribute::Memory);
    function.removeFnAttr(Attribute::Speculatable);
    for (BasicBlock &block : function)
      for (Instruction &instruction : block)
        if (auto *call = dyn_cast<CallBase>(&instruction)) {
          Function *callee = call->getCalledFunction();
          if (callee && callee->isIntrinsic())
            continue;
          call->removeFnAttr(Attribute::Memory);
          call->removeFnAttr(Attribute::Speculatable);
        }
  }
  return true;
}

} // namespace obelisk::driver::detail

#endif
