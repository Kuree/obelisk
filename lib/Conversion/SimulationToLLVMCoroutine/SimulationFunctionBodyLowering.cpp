//===- SimulationFunctionBodyLowering.cpp - Native body rewrites ----------===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/WalkPatternRewriteDriver.h"

#include <map>
#include <optional>

using namespace mlir;

namespace obelisk::detail {

void foldNativeSpawnInitializationScopes(ModuleOp module) {
  SmallVector<sim::SimRandomInitializationLeaveOp> leaves;
  module.walk([&](sim::SimRandomInitializationLeaveOp leave) {
    leaves.push_back(leave);
  });
  PatternRewriter rewriter(module.getContext());
  for (sim::SimRandomInitializationLeaveOp leave : leaves) {
    // Fusion removes absorbed spawns but leaves their bindings behind.
    if (succeeded(
            sim::SimRandomInitializationLeaveOp::canonicalize(leave, rewriter)))
      continue;
    auto enter =
        leave.getActivation().getDefiningOp<sim::SimRandomInitializationEnterOp>();
    if (!enter || enter->getBlock() != leave->getBlock() || !enter->hasOneUse())
      continue;
    sim::SimSpawnOp spawn;
    bool single = true;
    for (Operation *between = enter->getNextNode(); single && between != leave;
         between = between->getNextNode()) {
      if (isMemoryEffectFree(between))
        continue;
      auto candidate = dyn_cast<sim::SimSpawnOp>(between);
      single = candidate && !spawn;
      spawn = candidate;
    }
    if (!single || !spawn || spawn.getOperands().empty() ||
        spawn.getOperands().front() != enter.getContext() ||
        native::has<native::InitializationScope>(spawn))
      continue;
    native::set<native::InitializationScope>(
        spawn, rewriter.getI64IntegerAttr(enter.getScope()));
    leave.erase();
    enter.erase();
  }
}

// Batch spawns whose captures are constant words into one spawn table. A row
// carries the initialization scope that the spawn binds, if any: IEEE
// 1800-2023 18.14.1 seeds each static process from the initialization RNG of
// its containing instance, in creation order, so rows keep program order.
// Automatic handles still have retain/failure calls between spawns, which are
// boundaries for this scan.
void materializeNativeSpawnBatches(Operation *root) {
  ModuleOp module = root->getParentOfType<ModuleOp>();
  MLIRContext *context = root->getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  auto rowType =
      LLVM::LLVMStructType::getLiteral(context, {pointer, i64, i64});
  SymbolTableCollection symbols;
  unsigned batchID = 0;
  struct Spawn {
    schedule::NativeSpawnOp operation;
    SmallVector<uint64_t> captures;
    uint64_t scope = 0;
  };
  auto scopeOf = [](schedule::NativeSpawnOp spawn) -> uint64_t {
    auto scope = native::get<native::InitializationScope>(spawn);
    return scope ? scope.getValue().getZExtValue() : 0;
  };
  auto makeRow = [&](Operation &operation) -> std::optional<Spawn> {
    auto spawn = dyn_cast<schedule::NativeSpawnOp>(operation);
    auto helper =
        spawn ? symbols.lookupNearestSymbolFrom<LLVM::LLVMFuncOp>(
                    spawn, StringAttr::get(
                               context,
                               (spawn.getCallee() + ".__obelisk_spawn").str()))
              : LLVM::LLVMFuncOp{};
    auto offsets = helper ? native::get<native::SpawnCaptureOffsets>(helper)
                          : DenseI64ArrayAttr{};
    auto size =
        helper ? native::get<native::SpawnCaptureSize>(helper) : IntegerAttr{};
    if (!spawn || !spawn->use_empty() || !offsets || !size ||
        spawn->getNumOperands() != offsets.size() + 1 || size.getInt() < 0 ||
        size.getInt() % 8 != 0)
      return std::nullopt;
    Spawn row{spawn, SmallVector<uint64_t>(size.getInt() / 8, 0),
              scopeOf(spawn)};
    for (auto [operand, offset] :
         llvm::zip(spawn->getOperands().drop_front(), offsets.asArrayRef())) {
      APInt value;
      if (!operand.getType().isInteger(64) || offset < 0 || offset % 8 ||
          uint64_t(offset / 8) >= row.captures.size() ||
          !matchPattern(operand, m_ConstantInt(&value)))
        return std::nullopt;
      row.captures[offset / 8] = value.getZExtValue();
    }
    return row;
  };
  SmallVector<SmallVector<Spawn>> batches;
  SmallVector<schedule::NativeSpawnOp> unbatchedScopes;
  for (Region &region : root->getRegions()) {
    for (Block &block : region) {
      SmallVector<Spawn> pending;
      auto flush = [&] {
        if (pending.size() > 1 || (pending.size() == 1 && pending[0].scope))
          batches.push_back(std::move(pending));
        pending.clear();
      };
      for (Operation &operation : block) {
        if (operation.hasTrait<OpTrait::ConstantLike>())
          continue;
        std::optional<Spawn> row = makeRow(operation);
        if (!row) {
          auto spawn = dyn_cast<schedule::NativeSpawnOp>(operation);
          if (spawn && scopeOf(spawn))
            unbatchedScopes.push_back(spawn);
          flush();
          continue;
        }
        if (!pending.empty() && pending.front().operation->getOperand(0) !=
                                    row->operation->getOperand(0))
          flush();
        pending.push_back(std::move(*row));
      }
      flush();
    }
  }
  // A spawn the table cannot hold binds its scope around the ordinary call.
  for (schedule::NativeSpawnOp spawn : unbatchedScopes) {
    OpBuilder builder(spawn);
    Value spawnContext = spawn->getOperand(0);
    Value activation = emitRandomInitializationEnter(
        builder, spawn.getLoc(), spawnContext, scopeOf(spawn));
    builder.setInsertionPointAfter(spawn);
    emitRandomInitializationLeave(builder, spawn.getLoc(), spawnContext,
                                  activation);
    native::remove<native::InitializationScope>(spawn);
  }
  for (auto &batch : batches) {
    Location location = batch.front().operation.getLoc();
    std::string base = (SymbolTable::getSymbolName(root).getValue() +
                        ".__obelisk_spawn_batch_" + Twine(batchID++))
                           .str();
    // Rows with equal captures share one blob slice.
    SmallVector<uint64_t> words;
    std::map<SmallVector<uint64_t>, uint64_t> slices;
    SmallVector<uint64_t> offsets;
    for (const Spawn &spawn : batch) {
      auto [slice, inserted] = slices.try_emplace(spawn.captures, words.size());
      if (inserted)
        llvm::append_range(words, spawn.captures);
      offsets.push_back(slice->second * 8);
    }
    std::string capturesName = base + "_captures";
    if (!words.empty()) {
      auto type = LLVM::LLVMArrayType::get(i64, words.size());
      auto global = makeConstantGlobal(
          module, location, type, capturesName, LLVM::Linkage::Internal, 8,
          [&](OpBuilder &builder) {
            Value value = LLVM::ZeroOp::create(builder, location, type);
            for (auto [index, bits] : llvm::enumerate(words))
              value = insertValue(builder, location, value,
                                  llvmConstant(builder, location, i64, bits),
                                  index);
            return value;
          });
      copyNativePartition(root, global);
    }
    auto tableType = LLVM::LLVMArrayType::get(rowType, batch.size());
    auto table = makeConstantGlobal(
        module, location, tableType, base, LLVM::Linkage::Internal, 8,
        [&](OpBuilder &builder) {
          Value value = LLVM::ZeroOp::create(builder, location, tableType);
          for (auto [index, spawn] : llvm::enumerate(batch)) {
            Value row = LLVM::ZeroOp::create(builder, location, rowType);
            row = insertValue(
                builder, location, row,
                LLVM::AddressOfOp::create(
                    builder, location, pointer,
                    (spawn.operation.getCallee() + ".__obelisk_spawn_plan")
                        .str()),
                0);
            row = insertValue(
                builder, location, row,
                llvmConstant(builder, location, i64, offsets[index]), 1);
            row = insertValue(builder, location, row,
                              llvmConstant(builder, location, i64, spawn.scope),
                              2);
            value = insertValue(builder, location, value, row, index);
          }
          return value;
        });
    copyNativePartition(root, table);
    OpBuilder builder(batch.front().operation);
    Value spawnContext = batch.front().operation->getOperand(0);
    Value address = LLVM::AddressOfOp::create(builder, location, pointer, base);
    Value captures =
        words.empty()
            ? Value(LLVM::ZeroOp::create(builder, location, pointer))
            : Value(LLVM::AddressOfOp::create(builder, location, pointer,
                                              capturesName));
    Value status =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_process_spawn_batch"),
            ValueRange{spawnContext, address, captures,
                       llvmConstant(builder, location, i32, batch.size())})
            .getResult();
    // Only an initialization binding can fail the table itself; spawn
    // failures are reported through scheduler_fail as for separate spawns.
    if (llvm::any_of(batch, [](const Spawn &spawn) { return spawn.scope; }))
      reportManagedStatus(builder, location, spawnContext, status);
    for (auto &spawn : batch)
      spawn.operation.erase();
  }
}

namespace {

class NativeCallPattern final : public OpRewritePattern<sim::SimCallOp> {
public:
  NativeCallPattern(MLIRContext *context, NativeCallResultLowering lowering)
      : OpRewritePattern(context), lowering(lowering) {}

  LogicalResult matchAndRewrite(sim::SimCallOp operation,
                                PatternRewriter &rewriter) const override {
    SmallVector<Type> resultTypes;
    for (Type type : operation.getResultTypes())
      resultTypes.push_back(
          lowering == NativeCallResultLowering::ConvertProcessTypes
              ? convertProcessType(type, rewriter.getContext())
              : type);
    auto call = func::CallOp::create(rewriter, operation.getLoc(),
                                     operation.getCallee(), resultTypes,
                                     operation.getOperands());
    rewriter.replaceOp(operation, call.getResults());
    return success();
  }

private:
  NativeCallResultLowering lowering;
};

class NativeSpawnPattern final
    : public OpRewritePattern<schedule::NativeSpawnOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(schedule::NativeSpawnOp operation,
                                PatternRewriter &rewriter) const override {
    // Spawn-table materialization owns initialization bindings.
    if (native::has<native::InitializationScope>(operation))
      return rewriter.notifyMatchFailure(operation, "unbound spawn scope");
    auto call = LLVM::CallOp::create(
        rewriter, operation.getLoc(), TypeRange{rewriter.getI64Type()},
        SymbolRefAttr::get(rewriter.getContext(),
                           (operation.getCallee() + ".__obelisk_spawn").str()),
        operation.getOperands());
    rewriter.replaceOp(operation, call.getResults());
    return success();
  }
};

class NativeReturnPattern final : public OpRewritePattern<sim::SimReturnOp> {
public:
  NativeReturnPattern(MLIRContext *context, NativeReturnLowering lowering)
      : OpRewritePattern(context), lowering(lowering) {}

  LogicalResult matchAndRewrite(sim::SimReturnOp operation,
                                PatternRewriter &rewriter) const override {
    if (lowering == NativeReturnLowering::SuccessStatus) {
      if (!operation.getOperands().empty())
        return rewriter.notifyMatchFailure(
            operation, "process success return must not carry values");
      Value zero = arith::ConstantOp::create(rewriter, operation.getLoc(),
                                             rewriter.getI32Type(),
                                             rewriter.getI32IntegerAttr(0));
      rewriter.replaceOpWithNewOp<func::ReturnOp>(operation, zero);
      return success();
    }
    rewriter.replaceOpWithNewOp<func::ReturnOp>(operation,
                                                operation.getOperands());
    return success();
  }

private:
  NativeReturnLowering lowering;
};

} // namespace

LogicalResult
lowerNativeFunctionBody(Operation *root, NativeReturnLowering returnLowering,
                        NativeCallResultLowering callResultLowering) {
  RewritePatternSet patterns(root->getContext());
  patterns.add<NativeCallPattern>(root->getContext(), callResultLowering);
  patterns.add<NativeSpawnPattern>(root->getContext());
  if (returnLowering != NativeReturnLowering::None)
    patterns.add<NativeReturnPattern>(root->getContext(), returnLowering);
  FrozenRewritePatternSet frozenPatterns(std::move(patterns));
  if (::obelisk::schedule::has<
          ::obelisk::schedule::Field::EvalPathKnownPredicate>(root)) {
    if (failed(applyPatternsGreedily(root, frozenPatterns)))
      return root->emitError("native function-body rewrite failed");
  } else {
    // The native process frame analysis refers to the original CFG. Avoid the
    // region simplification performed by the greedy driver for those bodies.
    walkAndApplyPatterns(root, frozenPatterns);
  }
  Operation *illegalOperation = nullptr;
  WalkResult leftovers = root->walk([&](Operation *operation) {
    bool illegal = isa<sim::SimCallOp, schedule::NativeSpawnOp>(operation) ||
                   (returnLowering != NativeReturnLowering::None &&
                    isa<sim::SimReturnOp>(operation));
    if (illegal)
      illegalOperation = operation;
    return illegal ? WalkResult::interrupt() : WalkResult::advance();
  });
  if (leftovers.wasInterrupted())
    return illegalOperation->emitError()
           << "native function-body rewrite left illegal operation '"
           << illegalOperation->getName() << "' in '"
           << SymbolTable::getSymbolName(root).getValue() << "'";
  return success();
}

} // namespace obelisk::detail
