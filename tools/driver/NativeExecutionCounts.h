//===- NativeExecutionCounts.h - Opt-in generated execution diagnostics
//----===//

#ifndef OBELISK_DRIVER_NATIVEEXECUTIONCOUNTS_H
#define OBELISK_DRIVER_NATIVEEXECUTIONCOUNTS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"

namespace obelisk::driver::detail {

/// Counts generated function entries before native optimization/partitioning.
/// Coroutine ramps (before splitting) also count their entries and resumes,
/// each split by whether simulation time is zero, as `<ramp>#entry@0`,
/// `#entry@later`, `#resume@0` and `#resume@later`.
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
         name.contains(".__obelisk_eval_body_") ||
         name.ends_with(".__obelisk_coro_ramp")))
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
  Type *pointer = builder.getPtrTy();
  // Each ramp has four counters: entry and resume, at time zero and later.
  SmallVector<std::string> counterNames;
  SmallVector<unsigned> firstCounter;
  for (Function *function : functions) {
    firstCounter.push_back(counterNames.size());
    if (!function->getName().ends_with(".__obelisk_coro_ramp")) {
      counterNames.push_back(function->getName().str());
      continue;
    }
    for (StringRef suffix :
         {"#entry@0", "#entry@later", "#resume@0", "#resume@later"})
      counterNames.push_back((function->getName() + suffix).str());
  }
  auto *countsType = ArrayType::get(i64, counterNames.size());
  auto *counts = new GlobalVariable(
      module, countsType, false, GlobalValue::InternalLinkage,
      ConstantAggregateZero::get(countsType), "__obelisk_execution_counts");
  counts->setAlignment(Align(8));
  // 0 at simulation time zero (or before a context exists), else 1.
  Function *phase = nullptr;
  if (GlobalVariable *current =
          module.getGlobalVariable("__obelisk_current_context", true)) {
    phase = Function::Create(FunctionType::get(i64, false),
                             GlobalValue::InternalLinkage,
                             "__obelisk_execution_phase", module);
    phase->addFnAttr(Attribute::NoInline);
    auto *entry = BasicBlock::Create(context, "entry", phase);
    auto *query = BasicBlock::Create(context, "query", phase);
    auto *zero = BasicBlock::Create(context, "zero", phase);
    builder.SetInsertPoint(entry);
    Value *runtime = builder.CreateLoad(pointer, current);
    builder.CreateCondBr(builder.CreateIsNull(runtime), zero, query);
    builder.SetInsertPoint(query);
    FunctionCallee time =
        module.getOrInsertFunction("obelisk_rt_v1_scheduler_time",
                                   FunctionType::get(i64, {pointer}, false));
    Value *now = builder.CreateCall(time, {runtime});
    builder.CreateRet(builder.CreateZExt(
        builder.CreateICmpNE(now, builder.getInt64(0)), i64));
    builder.SetInsertPoint(zero);
    builder.CreateRet(builder.getInt64(0));
  }
  // Volatile preserves each observation across inlining and loop transforms.
  // Simulation is single threaded; these are not atomics.
  auto count = [&](Instruction *before, unsigned counter, bool phased) {
    builder.SetInsertPoint(before);
    Value *index = builder.getInt64(counter);
    if (phased && phase)
      index = builder.CreateAdd(index, builder.CreateCall(phase));
    Value *address = builder.CreateInBoundsGEP(countsType, counts,
                                               {builder.getInt64(0), index});
    auto *old = builder.CreateLoad(i64, address);
    old->setVolatile(true);
    auto *store = builder.CreateStore(
        builder.CreateAdd(old, builder.getInt64(1)), address);
    store->setVolatile(true);
  };
  for (auto [function, first] : llvm::zip_equal(functions, firstCounter)) {
    bool ramp = function->getName().ends_with(".__obelisk_coro_ramp");
    count(&*function->getEntryBlock().getFirstInsertionPt(), first, ramp);
    if (!ramp)
      continue;
    // A non-final suspend's result 0 resumes the coroutine. Count on a new
    // block on that edge so blocks reached otherwise are not counted.
    SmallVector<std::pair<SwitchInst *, unsigned>> resumes;
    for (BasicBlock &block : *function)
      for (Instruction &instruction : block)
        if (auto *suspend = dyn_cast<IntrinsicInst>(&instruction);
            suspend && suspend->getIntrinsicID() == Intrinsic::coro_suspend &&
            !cast<ConstantInt>(suspend->getArgOperand(1))->isOne())
          for (User *user : suspend->users())
            if (auto *dispatch = dyn_cast<SwitchInst>(user))
              for (auto handle : dispatch->cases())
                if (handle.getCaseValue()->isZero())
                  resumes.emplace_back(dispatch, handle.getSuccessorIndex());
    for (auto [dispatch, successor] : resumes) {
      BasicBlock *target = dispatch->getSuccessor(successor);
      auto *edge =
          BasicBlock::Create(context, "resume.count", function, target);
      BranchInst::Create(target, edge);
      dispatch->setSuccessor(successor, edge);
      for (PHINode &node : target->phis())
        node.setIncomingBlock(node.getBasicBlockIndex(dispatch->getParent()),
                              edge);
      count(edge->getTerminator(), first + 2, true);
    }
  }
  SmallVector<Constant *> names;
  for (const std::string &name : counterNames)
    names.push_back(
        builder.CreateGlobalString(name, "__obelisk_execution_name"));
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
      builder.CreateICmpULT(incremented, builder.getInt64(counterNames.size())),
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
