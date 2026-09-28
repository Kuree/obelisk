//===- SimulationPromotionReadiness.cpp - Shared knownness scan ----------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {

LogicalResult materializeNativeKernelPromotionReadiness(
    ModuleOp module, uint64_t stateBits,
    ArrayRef<SmallVector<NativePromotionRange>> ranges,
    ArrayRef<std::string> twoStateExecutors,
    ArrayRef<obelisk_rt_native_merged_fragment> fragments) {
  if (llvm::all_of(twoStateExecutors,
                   [](const std::string &name) { return name.empty(); }))
    return success();

  // IEEE 1800-2023 6.3.1, 6.11.2: every certified bit must be known before
  // using a two-state body. Merge overlapping intervals without filling gaps
  // or admitting neighboring bits in either partial boundary byte.
  SmallVector<uint64_t> owners, scans;
  for (auto [index, executor] : llvm::enumerate(twoStateExecutors)) {
    SmallVector<std::pair<uint64_t, uint64_t>> intervals;
    if (!executor.empty())
      for (const NativePromotionRange &range : ranges[index]) {
        if (!range.bitWidth || range.bitOffset > stateBits ||
            range.bitWidth > stateBits - range.bitOffset)
          return module.emitError(
              "kernel promotion range exceeds native state");
        intervals.emplace_back(range.bitOffset,
                               range.bitOffset + range.bitWidth);
      }
    llvm::sort(intervals);
    SmallVector<std::pair<uint64_t, uint64_t>> merged;
    for (auto [begin, end] : intervals) {
      if (!merged.empty() && begin <= merged.back().second)
        merged.back().second = std::max(merged.back().second, end);
      else
        merged.emplace_back(begin, end);
    }
    uint64_t first = scans.size() / 4;
    for (auto [begin, end] : merged) {
      uint64_t firstMask = (UINT64_C(255) << (begin % 8)) & 255;
      uint64_t lastMask = end % 8 ? (uint64_t{1} << (end % 8)) - 1 : 255;
      llvm::append_range(scans,
                         ArrayRef<uint64_t>{begin / 8, end / 8 + (end % 8 != 0),
                                            firstMask, lastMask});
    }
    uint64_t bit = fragments[index].bit;
    llvm::append_range(owners,
                       ArrayRef<uint64_t>{first, scans.size() / 4, bit / 64,
                                          uint64_t{1} << (bit % 64)});
  }

  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location loc = module.getLoc();
  Type i1 = builder.getI1Type(), i8 = builder.getI8Type(),
       i64 = builder.getI64Type();
  Type pointer = LLVM::LLVMPointerType::get(context);
  auto table = [&](StringRef name, ArrayRef<uint64_t> words) {
    builder.setInsertionPointToStart(module.getBody());
    auto tensor =
        RankedTensorType::get({static_cast<int64_t>(words.size())}, i64);
    return LLVM::GlobalOp::create(builder, loc,
                                  LLVM::LLVMArrayType::get(i64, words.size()),
                                  true, LLVM::Linkage::Internal, name,
                                  DenseIntElementsAttr::get(tensor, words), 8);
  };
  auto ownerTable = table("__obelisk_eval_kernel_promotion_owners_v1", owners);
  auto rangeTable = table("__obelisk_eval_kernel_promotion_ranges_v1", scans);
  auto constant = [&](uint64_t value) {
    return llvmConstant(builder, loc, i64, value);
  };
  auto address = [&](StringRef name) -> Value {
    return LLVM::AddressOfOp::create(builder, loc, pointer, name);
  };
  auto gep = [&](Value base, Type type, Value index) -> Value {
    return LLVM::GEPOp::create(builder, loc, pointer, type, base,
                               ArrayRef<LLVM::GEPArg>{index});
  };
  auto field = [&](Value base, Value record, uint64_t index) -> Value {
    Value offset = arith::AddIOp::create(
        builder, loc, arith::MulIOp::create(builder, loc, record, constant(4)),
        constant(index));
    return LLVM::LoadOp::create(builder, loc, i64, gep(base, i64, offset), 8);
  };
  constexpr StringLiteral latchName =
      "__obelisk_eval_kernel_promotion_latched_v1";
  constexpr StringLiteral pendingName =
      "__obelisk_eval_promotion_pending_mask_v1";
  constexpr StringLiteral scanName = "__obelisk_eval_kernel_promotion_scan_v1";
  builder.setInsertionPointToEnd(module.getBody());
  auto scan = LLVM::LLVMFuncOp::create(
      builder, loc, scanName, LLVM::LLVMFunctionType::get(i1, {i64}, false));
  scan->setAttr("passthrough",
                builder.getArrayAttr({builder.getStringAttr("noinline"),
                                      builder.getStringAttr("cold")}));
  Block *entry = scan.addEntryBlock(builder);
  auto block = [&](bool argument = false) {
    auto *result = new Block;
    if (argument)
      result->addArgument(i64, loc);
    scan.getBody().push_back(result);
    return result;
  };
  Block *nextRange = block(true), *readRange = block();
  Block *readByte = block(true), *advanceByte = block(),
        *advanceRange = block();
  Block *known = block(), *unknown = block();
  builder.setInsertionPointToStart(entry);
  Value owner = entry->getArgument(0);
  Value ownerBase = address(ownerTable.getSymName());
  Value rangeBase = address(rangeTable.getSymName());
  Value state = address("__obelisk_state_unknown");
  Value first = field(ownerBase, owner, 0), end = field(ownerBase, owner, 1);
  cf::BranchOp::create(builder, loc, nextRange, ValueRange{first});

  builder.setInsertionPointToStart(nextRange);
  Value range = nextRange->getArgument(0);
  Value done =
      arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq, range, end);
  cf::CondBranchOp::create(builder, loc, done, known, ValueRange{}, readRange,
                           ValueRange{});
  builder.setInsertionPointToStart(readRange);
  Value beginByte = field(rangeBase, range, 0),
        endByte = field(rangeBase, range, 1);
  Value firstMask =
      arith::TruncIOp::create(builder, loc, i8, field(rangeBase, range, 2));
  Value lastMask =
      arith::TruncIOp::create(builder, loc, i8, field(rangeBase, range, 3));
  cf::BranchOp::create(builder, loc, readByte, ValueRange{beginByte});

  builder.setInsertionPointToStart(readByte);
  Value byte = readByte->getArgument(0);
  Value nextByte = arith::AddIOp::create(builder, loc, byte, constant(1));
  Value isFirst = arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq,
                                        byte, beginByte);
  Value isLast = arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq,
                                       nextByte, endByte);
  Value fullMask = llvmConstant(builder, loc, i8, 255);
  Value mask = arith::AndIOp::create(
      builder, loc,
      arith::SelectOp::create(builder, loc, isFirst, firstMask, fullMask),
      arith::SelectOp::create(builder, loc, isLast, lastMask, fullMask));
  Value bits = LLVM::LoadOp::create(builder, loc, i8, gep(state, i8, byte), 1);
  Value hasUnknown =
      arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne,
                            arith::AndIOp::create(builder, loc, bits, mask),
                            llvmConstant(builder, loc, i8, 0));
  cf::CondBranchOp::create(builder, loc, hasUnknown, unknown, ValueRange{},
                           advanceByte, ValueRange{});
  builder.setInsertionPointToStart(advanceByte);
  cf::CondBranchOp::create(builder, loc, isLast, advanceRange, ValueRange{},
                           readByte, ValueRange{nextByte});
  builder.setInsertionPointToStart(advanceRange);
  cf::BranchOp::create(
      builder, loc, nextRange,
      ValueRange{arith::AddIOp::create(builder, loc, range, constant(1))});

  builder.setInsertionPointToStart(known);
  LLVM::StoreOp::create(builder, loc, llvmConstant(builder, loc, i8, 1),
                        gep(address(latchName), i8, owner), 1);
  Value pendingAddress =
      gep(address(pendingName), i64, field(ownerBase, owner, 2));
  Value pending = LLVM::LoadOp::create(builder, loc, i64, pendingAddress, 8);
  Value keep = arith::XOrIOp::create(builder, loc, field(ownerBase, owner, 3),
                                     constant(UINT64_MAX));
  LLVM::StoreOp::create(builder, loc,
                        arith::AndIOp::create(builder, loc, pending, keep),
                        pendingAddress, 8);
  LLVM::ReturnOp::create(builder, loc, llvmConstant(builder, loc, i1, 1));
  builder.setInsertionPointToStart(unknown);
  LLVM::ReturnOp::create(builder, loc, llvmConstant(builder, loc, i1, 0));

  // IEEE 1800-2023 4.5/4.6: keep each existing readiness boundary and its
  // invalidation ordering. A valid latch needs no scan or additional call.
  builder.setInsertionPointToEnd(module.getBody());
  auto ready =
      LLVM::LLVMFuncOp::create(builder, loc, kernelPromotionReadyName,
                               LLVM::LLVMFunctionType::get(i1, {i64}, false));
  ready->setAttr("passthrough",
                 builder.getArrayAttr({builder.getStringAttr("alwaysinline")}));
  Block *readyEntry = ready.addEntryBlock(builder);
  Block *latched = new Block, *check = new Block;
  ready.getBody().push_back(latched);
  ready.getBody().push_back(check);
  builder.setInsertionPointToStart(readyEntry);
  Value latch = LLVM::LoadOp::create(
      builder, loc, i8, gep(address(latchName), i8, readyEntry->getArgument(0)),
      1);
  Value isLatched =
      arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne, latch,
                            llvmConstant(builder, loc, i8, 0));
  cf::CondBranchOp::create(builder, loc, isLatched, latched, ValueRange{},
                           check, ValueRange{});
  builder.setInsertionPointToStart(latched);
  LLVM::ReturnOp::create(builder, loc, llvmConstant(builder, loc, i1, 1));
  builder.setInsertionPointToStart(check);
  Value result = LLVM::CallOp::create(builder, loc, scan,
                                      ValueRange{readyEntry->getArgument(0)})
                     .getResult();
  LLVM::ReturnOp::create(builder, loc, result);
  return success();
}

} // namespace obelisk::detail
