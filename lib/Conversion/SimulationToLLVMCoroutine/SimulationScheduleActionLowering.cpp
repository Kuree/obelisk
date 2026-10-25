#include "SimulationEvalReadySet.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/DialectConversion.h"
#include "obelisk/Analysis/ParallelWalk.h"
#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/Transforms/NativeTransforms.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <atomic>

using namespace mlir;

namespace obelisk {
namespace detail {
void lowerSpecializedNBABatch(schedule::NativeNBABatchOp op);
void lowerNativeReadyRegion(schedule::NativeReadyRegionOp op,
                            const schedule::NativeGlobalLookup &globals);
} // namespace detail
namespace {
// Preserve the existing generated pending-set representation and publication
// boundary (IEEE 1800-2023 4.3, 4.5--4.7): no actor or observer executes inside
// this operation, and it does not coalesce source events. Preserve ascending
// read/add order even when source aliases the ready set. In particular, do not
// defer cache or summary writes past a later source read.
void materializeReadyMerge(schedule::NativeReadyMergeOp merge) {
  OpBuilder builder(merge);
  Location loc = merge.getLoc();
  runtime::ReadySetLayout layout(merge.getCapacity());
  auto i64 = builder.getI64Type();
  auto constant = [&](uint64_t value) {
    return detail::llvmConstant(builder, loc, i64, value);
  };
  auto address = [&](Value base, Value word) -> Value {
    return LLVM::GEPOp::create(builder, loc, base.getType(), i64, base,
                               ValueRange{word});
  };
  auto load = [&](Value pointer) -> Value {
    return LLVM::LoadOp::create(builder, loc, i64, pointer, 8);
  };
  auto empty = [&](Value value) -> Value {
    return arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq, value,
                                 constant(0));
  };
  Block *entry = merge->getBlock();
  Block *done = entry->splitBlock(Block::iterator(merge));
  auto block = [&]() {
    auto *result = new Block;
    done->getParent()->getBlocks().insert(done->getIterator(), result);
    return result;
  };
  Block *head = block(), *update = block(), *advance = block();
  head->addArgument(i64, loc);
  builder.setInsertionPointToEnd(entry);
  cf::BranchOp::create(builder, loc, head, ValueRange{constant(0)});

  builder.setInsertionPointToStart(head);
  Value word = head->getArgument(0);
  Value selected = load(address(merge.getSource(), word));
  if (layout.lastWordMask() != UINT64_MAX) {
    Value last = arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq,
                                       word, constant(layout.counts[0] - 1));
    Value bounded = arith::AndIOp::create(builder, loc, selected,
                                          constant(layout.lastWordMask()));
    selected = arith::SelectOp::create(builder, loc, last, bounded, selected);
  }
  cf::CondBranchOp::create(builder, loc, empty(selected), advance, update);

  builder.setInsertionPointToStart(update);
  Value leaf = address(merge.getBase(), word);
  Value previous = load(leaf);
  Value next = arith::OrIOp::create(builder, loc, previous, selected);
  LLVM::StoreOp::create(builder, loc, next, leaf, 8);
  if (layout.hasCache() || layout.hasSummaries()) {
    Block *indexes = block();
    // A previously nonempty leaf already has exact ancestor bits and a cache
    // no greater than its index. Adding bits cannot change either property.
    cf::CondBranchOp::create(builder, loc, empty(previous), indexes, advance);
    builder.setInsertionPointToStart(indexes);
    if (layout.hasCache()) {
      Value slot = address(merge.getBase(), constant(layout.cacheOffset()));
      Value minimum = arith::MinUIOp::create(builder, loc, load(slot), word);
      LLVM::StoreOp::create(builder, loc, minimum, slot, 8);
    }
    Value child = word;
    for (unsigned level = 1; level != layout.levels; ++level) {
      Value parent = arith::ShRUIOp::create(builder, loc, child, constant(6));
      Value index = arith::AddIOp::create(builder, loc, parent,
                                          constant(layout.offsets[level]));
      Value slot = address(merge.getBase(), index);
      Value old = load(slot);
      Value bit = arith::ShLIOp::create(
          builder, loc, constant(1),
          arith::AndIOp::create(builder, loc, child, constant(63)));
      LLVM::StoreOp::create(
          builder, loc, arith::OrIOp::create(builder, loc, old, bit), slot, 8);
      child = parent;
      if (level + 1 != layout.levels) {
        Block *propagate = block();
        // A nonempty parent already has its bit in the next level.
        cf::CondBranchOp::create(builder, loc, empty(old), propagate, advance);
        builder.setInsertionPointToStart(propagate);
      }
    }
  }
  cf::BranchOp::create(builder, loc, advance);
  builder.setInsertionPointToStart(advance);
  Value following = arith::AddIOp::create(builder, loc, word, constant(1));
  Value more = arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ult,
                                     following, constant(layout.counts[0]));
  cf::CondBranchOp::create(builder, loc, more, head, ValueRange{following},
                           done, ValueRange{});
  merge.erase();
}

// Keep readiness semantic until conversion. Only speculatable, effect-free
// operations may separate actions sharing one proven base and ready universe.
// In particular a ready read, ordinary load, transition, call or alias action
// ends a batch, even when its result is unused.
void materializeReadyBatches(Operation *operation,
                             const schedule::NativeGlobalLookup &globals) {
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
    return lhs && rhs && globals(lhs) && globals(lhs) == globals(rhs);
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

void declareStaticTransition(ModuleOp module) {
  OpBuilder builder(module.getContext());
  auto ptr = LLVM::LLVMPointerType::get(builder.getContext());
  detail::getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_static_transition",
      LLVM::LLVMVoidType::get(builder.getContext()),
      {ptr, builder.getI32Type(), builder.getI64Type(), builder.getI64Type(),
       builder.getI64Type(), builder.getI64Type(), builder.getI64Type(),
       builder.getI64Type()});
}

/// Lowers NBA batches. An unspecialized batch adds a module global and a
/// runtime declaration; `cache`, if given, stays coherent with the global.
void lowerNBABatches(Operation *operation, SymbolTableCollection *cache) {
  SmallVector<schedule::NativeNBABatchOp> batches =
      analysis::collectInWalkOrder<schedule::NativeNBABatchOp>(operation);
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
      SymbolTable unique(module);
      OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(module.getBody());
      auto tensor = RankedTensorType::get({int64_t(roots.size())}, i32);
      table = LLVM::GlobalOp::create(
          builder, loc, LLVM::LLVMArrayType::get(i32, roots.size()), true,
          LLVM::Linkage::Internal, "__obelisk_nba_batch_roots",
          DenseIntElementsAttr::get(tensor, ArrayRef<uint32_t>(roots)), 4);
      unique.insert(table);
      // Keep a caller's cache coherent; the name is already unique.
      if (cache)
        cache->getSymbolTable(module).insert(table);
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
}

/// Lowers the remaining actions in place. The module is touched only to
/// declare the static-transition entry, and only if `declareRuntime`.
void materializeLocalScheduleActions(
    Operation *operation, bool coalesce, bool preserveReadiness,
    const schedule::NativeGlobalLookup &globals, bool declareRuntime) {
  if (coalesce && !preserveReadiness)
    schedule::coalesceNativeReadyUpdates(operation, globals);
  SmallVector<schedule::NativeReadyRegionOp> readyRegions;
  if (!preserveReadiness)
    readyRegions =
        analysis::collectInWalkOrder<schedule::NativeReadyRegionOp>(operation);
  for (auto region : readyRegions)
    detail::lowerNativeReadyRegion(region, globals);
  if (coalesce && !preserveReadiness)
    materializeReadyBatches(operation, globals);
  SmallVector<schedule::NativeReadyUpdateOp> updates;
  SmallVector<schedule::NativeTransitionOp> transitions;
  SmallVector<schedule::NativeReadyCommitOp> commits;
  SmallVector<schedule::NativeReadyReadOp> reads;
  SmallVector<schedule::NativeReadyMergeOp> merges;
  // One walk of the whole module collects every kind in its own walk order.
  for (Operation *op :
       analysis::collectInWalkOrder<Operation *>(operation, [&](Operation *op) {
         return isa<schedule::NativeTransitionOp>(op) ||
                (!preserveReadiness &&
                 isa<schedule::NativeReadyUpdateOp,
                     schedule::NativeReadyCommitOp, schedule::NativeReadyReadOp,
                     schedule::NativeReadyMergeOp>(op));
       })) {
    if (auto transition = dyn_cast<schedule::NativeTransitionOp>(op))
      transitions.push_back(transition);
    else if (auto update = dyn_cast<schedule::NativeReadyUpdateOp>(op))
      updates.push_back(update);
    else if (auto commit = dyn_cast<schedule::NativeReadyCommitOp>(op))
      commits.push_back(commit);
    else if (auto merge = dyn_cast<schedule::NativeReadyMergeOp>(op))
      merges.push_back(merge);
    else
      reads.push_back(cast<schedule::NativeReadyReadOp>(op));
  }
  if (declareRuntime && !transitions.empty()) {
    auto module = dyn_cast<ModuleOp>(operation);
    if (!module)
      module = operation->getParentOfType<ModuleOp>();
    declareStaticTransition(module);
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
  for (auto merge : merges)
    materializeReadyMerge(merge);
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
    if (auto routed =
            schedule::get<schedule::Field::EvalRuntimeRoutedPublication>(
                transition))
      schedule::set<schedule::Field::EvalRuntimeRoutedPublication>(call,
                                                                   routed);
    transition.erase();
  }
}


LogicalResult convertActionArithmetic(Operation *operation) {
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

/// Lowers each function of `module` independently, as the serial module-wide
/// lowering would: only batches and runtime declarations touch the module, so
/// they run first; each worker then rewrites and converts its own function.
/// Returns nothing, before any change, if an operation outside the functions
/// is not already LLVM IR.
std::optional<LogicalResult> lowerModuleActionsInParallel(ModuleOp module) {
  MLIRContext *context = module.getContext();
  SmallVector<LLVM::LLVMFuncOp> functions;
  SmallVector<Operation *> others;
  for (Operation &operation : *module.getBody()) {
    if (auto function = dyn_cast<LLVM::LLVMFuncOp>(operation)) {
      if (!function.isExternal())
        functions.push_back(function);
    } else {
      others.push_back(&operation);
    }
  }
  std::atomic<bool> foreign = false;
  analysis::parallelForChunked(context, others.size(), [&](size_t index) {
    others[index]->walk([&](Operation *nested) {
      if (isa_and_nonnull<LLVM::LLVMDialect>(nested->getDialect()))
        return WalkResult::advance();
      foreign = true;
      return WalkResult::interrupt();
    });
  });
  if (foreign)
    return std::nullopt;
  lowerNBABatches(module, /*cache=*/nullptr);
  std::atomic<bool> transitions = false;
  analysis::parallelForChunked(context, functions.size(), [&](size_t index) {
    if (functions[index]
            .walk([](schedule::NativeTransitionOp) {
              return WalkResult::interrupt();
            })
            .wasInterrupted())
      transitions = true;
  });
  if (transitions)
    declareStaticTransition(module);
  // Workers add or remove no symbols.
  SymbolTable symbols(module);
  schedule::NativeGlobalLookup globals(symbols);
  std::atomic<bool> converted = true;
  analysis::parallelForChunked(context, functions.size(), [&](size_t index) {
    LLVM::LLVMFuncOp function = functions[index];
    materializeLocalScheduleActions(function, /*coalesce=*/true,
                                    /*preserveReadiness=*/false, globals,
                                    /*declareRuntime=*/false);
    bool converts = function
                        .walk([](Operation *operation) {
                          return isa_and_nonnull<arith::ArithDialect,
                                                 cf::ControlFlowDialect>(
                                     operation->getDialect())
                                     ? WalkResult::interrupt()
                                     : WalkResult::advance();
                        })
                        .wasInterrupted();
    if (converts && failed(convertActionArithmetic(function)))
      converted = false;
  });
  return success(converted);
}
} // namespace

void materializeNativeScheduleActions(Operation *operation, bool coalesce,
                                      bool preserveReadiness,
                                      SymbolTableCollection *cache) {
  SymbolTableCollection local;
  lowerNBABatches(operation, cache);
  materializeLocalScheduleActions(operation, coalesce, preserveReadiness,
                                  cache ? *cache : local,
                                  /*declareRuntime=*/true);
}

LogicalResult lowerNativeScheduleActionsToLLVM(Operation *operation,
                                               SymbolTableCollection *symbols) {
  if (auto module = dyn_cast<ModuleOp>(operation); module && !symbols)
    if (std::optional<LogicalResult> lowered =
            lowerModuleActionsInParallel(module))
      return *lowered;
  materializeNativeScheduleActions(operation, /*coalesce=*/true,
                                   /*preserveReadiness=*/false, symbols);
  return convertActionArithmetic(operation);
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
