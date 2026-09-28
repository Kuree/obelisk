//===- SimulationFunctionBodyLowering.cpp - Native body rewrites ----------===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/WalkPatternRewriteDriver.h"

using namespace mlir;

namespace obelisk::detail {

// Batch only unused, constant word captures. Automatic handles still have
// retain/failure calls between spawns, which are boundaries for this scan.
void materializeNativeSpawnBatches(Operation *root) {
  ModuleOp module = root->getParentOfType<ModuleOp>();
  MLIRContext *context = root->getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i64 = IntegerType::get(context, 64);
  auto rowType = LLVM::LLVMStructType::getLiteral(context, {pointer, pointer});
  SymbolTableCollection symbols;
  unsigned batchID = 0;
  struct Spawn {
    schedule::NativeSpawnOp operation;
    SmallVector<uint64_t> captures;
  };
  SmallVector<SmallVector<Spawn>> batches;
  for (Region &region : root->getRegions()) {
    for (Block &block : region) {
      SmallVector<Spawn> pending;
      auto flush = [&] {
        if (pending.size() > 1)
          batches.push_back(std::move(pending));
        pending.clear();
      };
      for (Operation &operation : block) {
        if (operation.hasTrait<OpTrait::ConstantLike>())
          continue;
        auto spawn = dyn_cast<schedule::NativeSpawnOp>(operation);
        auto helper =
            spawn ? symbols.lookupNearestSymbolFrom<LLVM::LLVMFuncOp>(
                        spawn,
                        StringAttr::get(
                            context,
                            (spawn.getCallee() + ".__obelisk_spawn").str()))
                  : LLVM::LLVMFuncOp{};
        auto offsets = helper ? helper->getAttrOfType<DenseI64ArrayAttr>(
                                    "obelisk.spawn_capture_offsets")
                              : DenseI64ArrayAttr{};
        auto size = helper ? helper->getAttrOfType<IntegerAttr>(
                                 "obelisk.spawn_capture_size")
                           : IntegerAttr{};
        if (!spawn || !spawn->use_empty() || !offsets || !size ||
            spawn->getNumOperands() != offsets.size() + 1 ||
            size.getInt() < 0 || size.getInt() % 8 != 0) {
          flush();
          continue;
        }
        Spawn row{spawn, SmallVector<uint64_t>(size.getInt() / 8, 0)};
        bool eligible = true;
        for (auto [operand, offset] : llvm::zip(
                 spawn->getOperands().drop_front(), offsets.asArrayRef())) {
          APInt value;
          if (!operand.getType().isInteger(64) || offset < 0 || offset % 8 ||
              uint64_t(offset / 8) >= row.captures.size() ||
              !matchPattern(operand, m_ConstantInt(&value))) {
            eligible = false;
            break;
          }
          row.captures[offset / 8] = value.getZExtValue();
        }
        if (!eligible) {
          flush();
          continue;
        }
        if (!pending.empty() &&
            pending.front().operation->getOperand(0) != spawn->getOperand(0))
          flush();
        pending.push_back(std::move(row));
      }
      flush();
    }
  }
  for (auto &batch : batches) {
    Location location = batch.front().operation.getLoc();
    std::string base = (SymbolTable::getSymbolName(root).getValue() +
                        ".__obelisk_spawn_batch_" + Twine(batchID++))
                           .str();
    SmallVector<std::string> captures;
    for (auto [index, spawn] : llvm::enumerate(batch)) {
      std::string name = base + "_captures_" + std::to_string(index);
      auto type = LLVM::LLVMArrayType::get(i64, spawn.captures.size());
      if (!spawn.captures.empty()) {
        auto global = makeConstantGlobal(
            module, location, type, name, LLVM::Linkage::Internal, 8,
            [&](OpBuilder &builder) {
              Value value = LLVM::ZeroOp::create(builder, location, type);
              for (auto [word, bits] : llvm::enumerate(spawn.captures))
                value = insertValue(builder, location, value,
                                    llvmConstant(builder, location, i64, bits),
                                    word);
              return value;
            });
        copyNativePartition(root, global);
      }
      captures.push_back(std::move(name));
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
            if (!spawn.captures.empty())
              row =
                  insertValue(builder, location, row,
                              LLVM::AddressOfOp::create(
                                  builder, location, pointer, captures[index]),
                              1);
            value = insertValue(builder, location, value, row, index);
          }
          return value;
        });
    copyNativePartition(root, table);
    OpBuilder builder(batch.front().operation);
    Value address = LLVM::AddressOfOp::create(builder, location, pointer, base);
    LLVM::CallOp::create(
        builder, location, TypeRange{},
        SymbolRefAttr::get(context, "obelisk_rt_v1_process_spawn_batch"),
        ValueRange{batch.front().operation->getOperand(0), address,
                   llvmConstant(builder, location, builder.getI32Type(),
                                batch.size())});
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
