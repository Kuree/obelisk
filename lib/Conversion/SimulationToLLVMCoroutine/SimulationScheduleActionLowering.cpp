#include "SimulationEvalReadySet.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/DialectConversion.h"
#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/Transforms/NativeTransforms.h"
#include "llvm/ADT/SmallPtrSet.h"

using namespace mlir;

namespace obelisk {
namespace detail {
void lowerSpecializedNBABatch(schedule::NativeNBABatchOp op);
void lowerNativeReadyRegion(schedule::NativeReadyRegionOp op,
                            SymbolTableCollection &symbols);
} // namespace detail
namespace {
// Keep readiness semantic until conversion. Only speculatable, effect-free
// operations may separate actions sharing one proven base and ready universe.
// In particular a ready read, ordinary load, transition, call or alias action
// ends a batch, even when its result is unused.
void materializeReadyBatches(Operation *operation,
                             SymbolTableCollection &symbols) {
  SmallVector<SmallVector<Operation *>> batches;
  SmallVector<Block *> blocks;
  llvm::SmallPtrSet<Block *, 16> seen;
  operation->walk([&](Operation *op) {
    if (isa<schedule::NativeReadyUpdateOp, schedule::NativeReadyCommitOp>(op) &&
        seen.insert(op->getBlock()).second)
      blocks.push_back(op->getBlock());
  });
  auto baseAndCapacity = [](Operation *op) -> std::pair<Value, uint64_t> {
    if (auto update = dyn_cast<schedule::NativeReadyUpdateOp>(op))
      return {update.getBase(), update.getCapacity()};
    if (auto commit = dyn_cast<schedule::NativeReadyCommitOp>(op))
      return {commit.getBase(), commit.getCapacity()};
    return {};
  };
  auto sameBase = [&](Value a, Value b) {
    if (a == b)
      return true;
    auto lhs = a.getDefiningOp<LLVM::AddressOfOp>();
    auto rhs = b.getDefiningOp<LLVM::AddressOfOp>();
    // Distinct aliases or opaque pointers do not prove a shared base.
    return lhs && rhs && lhs.getGlobal(symbols) &&
           lhs.getGlobal(symbols) == rhs.getGlobal(symbols);
  };
  for (Block *block : blocks) {
    SmallVector<Operation *> pending;
    Value base;
    uint64_t capacity = 0;
    auto flush = [&]() {
      if (pending.size() > 1)
        batches.push_back(std::move(pending));
      pending.clear();
      base = {};
    };
    for (Operation &op : *block) {
      auto [current, count] = baseAndCapacity(&op);
      if (current) {
        if (base && (capacity != count || !sameBase(base, current)))
          flush();
        base = current;
        capacity = count;
        pending.push_back(&op);
      } else if (!isMemoryEffectFree(&op) || !isSpeculatable(&op)) {
        flush();
      }
    }
    flush();
  }
  for (const auto &batch : batches) {
    OpBuilder builder(batch.back());
    auto loc = batch.back()->getLoc();
    auto [base, capacity] = baseAndCapacity(batch.back());
    Value zero = detail::llvmConstant(builder, loc, builder.getI64Type(), 0);
    SmallVector<detail::EvalReadyWordChange> changes;
    for (Operation *op : batch) {
      if (auto update = dyn_cast<schedule::NativeReadyUpdateOp>(op))
        changes.push_back({unsigned(update.getWord()),
                           update.getConsume() ? update.getMask() : zero,
                           update.getConsume() ? zero : update.getMask()});
      else {
        auto commit = cast<schedule::NativeReadyCommitOp>(op);
        changes.push_back({unsigned(commit.getWord()), commit.getRemoveMask(),
                           commit.getAddMask()});
      }
    }
    detail::materializeEvalReadyWords(
        builder, loc, base, runtime::ReadySetLayout(capacity), changes);
    for (Operation *op : batch)
      op->erase();
  }
}
} // namespace
void materializeNativeScheduleActions(Operation *operation, bool coalesce,
                                      bool preserveReadiness) {
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
  if (coalesce && !preserveReadiness)
    schedule::coalesceNativeReadyUpdates(operation);
  SmallVector<schedule::NativeReadyRegionOp> readyRegions;
  if (!preserveReadiness)
    operation->walk(
        [&](schedule::NativeReadyRegionOp op) { readyRegions.push_back(op); });
  SymbolTableCollection symbols;
  for (auto region : readyRegions)
    detail::lowerNativeReadyRegion(region, symbols);
  if (coalesce && !preserveReadiness)
    materializeReadyBatches(operation, symbols);
  SmallVector<schedule::NativeReadyUpdateOp> updates;
  SmallVector<schedule::NativeTransitionOp> transitions;
  SmallVector<schedule::NativeReadyCommitOp> commits;
  SmallVector<schedule::NativeReadyReadOp> reads;
  // One walk of the whole module collects every kind in its own walk order.
  operation->walk([&](Operation *op) {
    if (auto transition = dyn_cast<schedule::NativeTransitionOp>(op)) {
      transitions.push_back(transition);
      return;
    }
    if (preserveReadiness)
      return;
    if (auto update = dyn_cast<schedule::NativeReadyUpdateOp>(op))
      updates.push_back(update);
    else if (auto commit = dyn_cast<schedule::NativeReadyCommitOp>(op))
      commits.push_back(commit);
    else if (auto read = dyn_cast<schedule::NativeReadyReadOp>(op))
      reads.push_back(read);
  });
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
  for (auto read : reads) {
    OpBuilder builder(read);
    Value value = detail::loadOwnerWord(builder, read.getLoc(), read.getBase(),
                                        read.getWord());
    if (auto owner = schedule::get<schedule::Field::EvalActivationEntry>(read))
      schedule::set<schedule::Field::EvalActivationEntry>(value.getDefiningOp(),
                                                          owner);
    read.replaceAllUsesWith(value);
    read.erase();
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

LogicalResult lowerNativeScheduleActionsToLLVM(Operation *operation) {
  materializeNativeScheduleActions(operation);
  LLVMTypeConverter converter(operation->getContext());
  RewritePatternSet patterns(operation->getContext());
  arith::populateArithToLLVMConversionPatterns(converter, patterns);
  cf::populateControlFlowToLLVMConversionPatterns(converter, patterns);
  ConversionTarget target(*operation->getContext());
  target.addLegalDialect<LLVM::LLVMDialect>();
  target.addIllegalDialect<arith::ArithDialect, cf::ControlFlowDialect>();
  target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });
  return applyPartialConversion(operation, target, std::move(patterns));
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
