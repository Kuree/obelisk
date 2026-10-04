#include "SimulationEvalReadySet.h"
#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/Transforms/NativeTransforms.h"

using namespace mlir;

namespace obelisk {
namespace detail {
void lowerSpecializedNBABatch(schedule::NativeNBABatchOp op);
void lowerNativeReadyRegion(schedule::NativeReadyRegionOp op,
                            SymbolTableCollection &symbols);
}
void materializeNativeScheduleActions(Operation *operation, bool coalesce) {
  SmallVector<schedule::NativeNBABatchOp> batches;
  operation->walk(
      [&](schedule::NativeNBABatchOp op) { batches.push_back(op); });
  for (auto batch : batches) {
    if (batch.getLayoutAttr()) {
      detail::lowerSpecializedNBABatch(batch);
      continue;
    }
    auto module = batch->getParentOfType<ModuleOp>();
    OpBuilder builder(batch);
    auto loc = batch.getLoc();
    auto ptr = LLVM::LLVMPointerType::get(builder.getContext());
    auto i32 = builder.getI32Type();
    detail::getOrDeclareLLVMFunction(module,
                                     "obelisk_rt_v1_static_nba_commit_batch",
                                     i32, {ptr, ptr, i32, i32, ptr});
    SmallVector<uint32_t> roots;
    for (auto commit : batch.getBody().getOps<schedule::NativeNBACommitOp>())
      roots.push_back(commit.getRoot());
    LLVM::GlobalOp table;
    {
      SymbolTable symbols(module);
      OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(module.getBody());
      auto tensor = RankedTensorType::get({int64_t(roots.size())}, i32);
      table = LLVM::GlobalOp::create(
          builder, loc, LLVM::LLVMArrayType::get(i32, roots.size()), true,
          LLVM::Linkage::Internal, "__obelisk_nba_batch_roots",
          DenseIntElementsAttr::get(tensor, ArrayRef<uint32_t>(roots)), 4);
      symbols.insert(table);
    }
    Value address = LLVM::AddressOfOp::create(builder, loc, table);
    auto call = LLVM::CallOp::create(
        builder, loc, TypeRange{i32},
        SymbolRefAttr::get(builder.getContext(),
                           "obelisk_rt_v1_static_nba_commit_batch"),
        ValueRange{batch.getContext(), address,
                   detail::llvmConstant(builder, loc, i32, roots.size()),
                   batch.getBarrier(), batch.getChanged()});
    batch.replaceAllUsesWith(call.getResult());
    batch.erase();
  }
  if (coalesce)
    schedule::coalesceNativeReadyUpdates(operation);
  SmallVector<schedule::NativeReadyRegionOp> readyRegions;
  operation->walk(
      [&](schedule::NativeReadyRegionOp op) { readyRegions.push_back(op); });
  SymbolTableCollection symbols;
  for (auto region : readyRegions)
    detail::lowerNativeReadyRegion(region, symbols);
  SmallVector<schedule::NativeReadyUpdateOp> updates;
  SmallVector<schedule::NativeTransitionOp> transitions;
  SmallVector<schedule::NativeReadyCommitOp> commits;
  operation->walk(
      [&](schedule::NativeReadyUpdateOp op) { updates.push_back(op); });
  operation->walk(
      [&](schedule::NativeTransitionOp op) { transitions.push_back(op); });
  operation->walk(
      [&](schedule::NativeReadyCommitOp op) { commits.push_back(op); });
  if (!transitions.empty()) {
    auto module = dyn_cast<ModuleOp>(operation);
    if (!module)
      module = operation->getParentOfType<ModuleOp>();
    OpBuilder builder(operation->getContext());
    auto ptr = LLVM::LLVMPointerType::get(builder.getContext());
    detail::getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_scheduler_static_transition",
        LLVM::LLVMVoidType::get(builder.getContext()),
        {ptr, builder.getI32Type(), builder.getI64Type(), builder.getI64Type(),
         builder.getI64Type(), builder.getI64Type(), builder.getI64Type(),
         builder.getI64Type()});
  }
  for (auto update : updates) {
    OpBuilder builder(update);
    runtime::ReadySetLayout layout(update.getCapacity());
    detail::materializeEvalReadyWord(builder, update.getLoc(), update.getBase(),
                                     layout, update.getWord(), update.getMask(),
                                     update.getConsume());
    update.erase();
  }
  for (auto commit : commits) {
    OpBuilder builder(commit);
    auto loc = commit.getLoc();
    runtime::ReadySetLayout layout(commit.getCapacity());
    Value previous =
        detail::loadOwnerWord(builder, loc, commit.getBase(), commit.getWord());
    Value inverse = builder.createOrFold<arith::XOrIOp>(
        loc, commit.getRemoveMask(),
        detail::llvmConstant(builder, loc, builder.getI64Type(), UINT64_MAX));
    Value retained =
        builder.createOrFold<arith::AndIOp>(loc, previous, inverse);
    Value next =
        builder.createOrFold<arith::OrIOp>(loc, retained, commit.getAddMask());
    LLVM::StoreOp::create(builder, loc, next,
                          detail::ownerWordAddress(
                              builder, loc, commit.getBase(), commit.getWord()),
                          8);
    detail::materializeEvalReadyIndexes(builder, loc, commit.getBase(), layout,
                                        commit.getWord(), next,
                                        commit.getAddMask());
    commit.erase();
  }
  for (auto transition : transitions) {
    OpBuilder builder(transition);
    auto call = LLVM::CallOp::create(
        builder, transition.getLoc(), TypeRange{},
        SymbolRefAttr::get(builder.getContext(),
                           "obelisk_rt_v1_scheduler_static_transition"),
        ValueRange{transition.getContext(),
                   detail::llvmConstant(builder, transition.getLoc(),
                                        builder.getI32Type(),
                                        transition.getStaticState()),
                   transition.getOffset(),
                   detail::llvmConstant(builder, transition.getLoc(),
                                        builder.getI64Type(),
                                        transition.getWidth()),
                   transition.getOldValue(), transition.getOldUnknown(),
                   transition.getNewValue(), transition.getNewUnknown()});
    if (auto owner = transition.getSourceOwnerAttr())
      schedule::set<schedule::Field::EvalSourceOwner>(call, owner);
    transition.erase();
  }
}

#define GEN_PASS_DEF_CONVERTNATIVESCHEDULEACTIONSTOLLVMPASS
#include "obelisk/Conversion/Passes.h.inc"
namespace {
struct ConvertNativeScheduleActionsToLLVMPass
    : impl::ConvertNativeScheduleActionsToLLVMPassBase<
          ConvertNativeScheduleActionsToLLVMPass> {
  using ConvertNativeScheduleActionsToLLVMPassBase::
      ConvertNativeScheduleActionsToLLVMPassBase;
  void runOnOperation() override {
    materializeNativeScheduleActions(getOperation(), coalesce);
  }
};
} // namespace
} // namespace obelisk
