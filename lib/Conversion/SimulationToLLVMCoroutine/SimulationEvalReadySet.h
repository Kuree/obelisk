//===- SimulationEvalReadySet.h - Word-wise generated owner sets -*- C++
//-*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_EVAL_READY_SET_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_EVAL_READY_SET_H

#include "SimulationToLLVMCoroutinePrivate.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Runtime/ClockKernelReadySet.h"

namespace obelisk::detail {

// APInt is only a compiler-side constant set. Generated storage and operations
// are always machine words, never wide LLVM integers.
inline uint64_t ownerMaskWord(const llvm::APInt &mask, unsigned word) {
  return mask.getRawData()[word];
}

inline mlir::Value ownerWordAddress(mlir::OpBuilder &builder,
                                    mlir::Location loc, mlir::Value base,
                                    unsigned word) {
  if (word == 0)
    return base;
  return byteGEP(builder, loc, base, uint64_t{word} * sizeof(uint64_t));
}

inline mlir::Value loadOwnerWord(mlir::OpBuilder &builder, mlir::Location loc,
                                 mlir::Value base, unsigned word) {
  return mlir::LLVM::LoadOp::create(builder, loc, builder.getI64Type(),
                                    ownerWordAddress(builder, loc, base, word),
                                    8);
}

inline mlir::Value maskedOwnerWords(mlir::OpBuilder &builder,
                                    mlir::Location loc, mlir::Value base,
                                    const llvm::APInt &mask) {
  auto i64 = builder.getI64Type();
  mlir::Value any;
  for (unsigned word = 0; word != mask.getNumWords(); ++word) {
    uint64_t bits = ownerMaskWord(mask, word);
    if (!bits)
      continue;
    mlir::Value selected = loadOwnerWord(builder, loc, base, word);
    if (bits != UINT64_MAX)
      selected = mlir::arith::AndIOp::create(
          builder, loc, selected, llvmConstant(builder, loc, i64, bits));
    any = any ? mlir::arith::OrIOp::create(builder, loc, any, selected)
              : selected;
  }
  return any ? any : llvmConstant(builder, loc, i64, 0);
}

inline void storeOwnerMask(mlir::OpBuilder &builder, mlir::Location loc,
                           mlir::Value base, const llvm::APInt &mask) {
  for (unsigned word = 0; word != mask.getNumWords(); ++word)
    mlir::LLVM::StoreOp::create(builder, loc,
                                llvmConstant(builder, loc, builder.getI64Type(),
                                             ownerMaskWord(mask, word)),
                                ownerWordAddress(builder, loc, base, word), 8);
}

inline mlir::Value updateOwnerWord(mlir::OpBuilder &builder, mlir::Location loc,
                                   mlir::Value base, unsigned word,
                                   mlir::Value mask, bool clear = false) {
  mlir::Value previous = loadOwnerWord(builder, loc, base, word);
  mlir::Value next;
  if (clear) {
    mlir::Value inverse = mlir::arith::XOrIOp::create(
        builder, loc, mask,
        llvmConstant(builder, loc, builder.getI64Type(), UINT64_MAX));
    next = mlir::arith::AndIOp::create(builder, loc, previous, inverse);
  } else {
    next = mlir::arith::OrIOp::create(builder, loc, previous, mask);
  }
  mlir::LLVM::StoreOp::create(builder, loc, next,
                              ownerWordAddress(builder, loc, base, word), 8);
  return next;
}

// Mutate authoritative ingress and its derived index as one serialized
// operation. Clears may leave the minimum cache stale-low, never stale-high.
inline void materializeEvalReadyIndexes(mlir::OpBuilder &builder,
                                        mlir::Location loc, mlir::Value base,
                                        const runtime::ReadySetLayout &layout,
                                        unsigned word, mlir::Value next,
                                        mlir::Value added) {
  using namespace mlir;
  auto i64 = builder.getI64Type();
  if (!layout.hasCache())
    return;
  Value zero = llvmConstant(builder, loc, i64, 0);
  if (added) {
    Value cache = loadOwnerWord(builder, loc, base, layout.cacheOffset());
    Value published = arith::CmpIOp::create(
        builder, loc, arith::CmpIPredicate::ne, added, zero);
    Value candidate = arith::SelectOp::create(
        builder, loc, published, llvmConstant(builder, loc, i64, word), cache);
    Value minimum = arith::MinUIOp::create(builder, loc, cache, candidate);
    LLVM::StoreOp::create(
        builder, loc, minimum,
        ownerWordAddress(builder, loc, base, layout.cacheOffset()), 8);
  }
  unsigned child = word;
  for (unsigned level = 1; level != layout.levels; ++level) {
    Value nonempty = arith::CmpIOp::create(
        builder, loc, arith::CmpIPredicate::ne, next, zero);
    unsigned parent = layout.offsets[level] + child / 64;
    uint64_t bit = uint64_t{1} << (child % 64);
    Value previous = loadOwnerWord(builder, loc, base, parent);
    Value without = arith::AndIOp::create(
        builder, loc, previous, llvmConstant(builder, loc, i64, ~bit));
    Value with = arith::OrIOp::create(builder, loc, previous,
                                      llvmConstant(builder, loc, i64, bit));
    next = arith::SelectOp::create(builder, loc, nonempty, with, without);
    LLVM::StoreOp::create(builder, loc, next,
                          ownerWordAddress(builder, loc, base, parent), 8);
    child /= 64;
  }
}

inline void materializeEvalReadyWord(mlir::OpBuilder &builder,
                                     mlir::Location loc, mlir::Value base,
                                     const runtime::ReadySetLayout &layout,
                                     unsigned word, mlir::Value mask,
                                     bool clear = false) {
  mlir::Value next = updateOwnerWord(builder, loc, base, word, mask, clear);
  materializeEvalReadyIndexes(builder, loc, base, layout, word, next,
                              clear ? mlir::Value{} : mask);
}

inline void updateEvalReadyWord(mlir::OpBuilder &builder, mlir::Location loc,
                                mlir::Value base,
                                const runtime::ReadySetLayout &layout,
                                unsigned word, mlir::Value mask,
                                bool clear = false) {
  schedule::NativeReadyUpdateOp::create(
      builder, loc, base, mask, builder.getI64IntegerAttr(layout.capacity),
      builder.getI64IntegerAttr(word), builder.getBoolAttr(clear));
}

inline void
updateOwnerMask(mlir::OpBuilder &builder, mlir::Location loc, mlir::Value base,
                const llvm::APInt &mask, bool clear = false,
                mlir::Value condition = {},
                const runtime::ReadySetLayout *readyLayout = nullptr) {
  auto i64 = builder.getI64Type();
  for (unsigned word = 0; word != mask.getNumWords(); ++word) {
    uint64_t bits = ownerMaskWord(mask, word);
    if (!bits)
      continue;
    mlir::Value selected = llvmConstant(builder, loc, i64, bits);
    if (condition)
      selected =
          mlir::arith::SelectOp::create(builder, loc, condition, selected,
                                        llvmConstant(builder, loc, i64, 0));
    if (readyLayout)
      updateEvalReadyWord(builder, loc, base, *readyLayout, word, selected,
                          clear);
    else
      updateOwnerWord(builder, loc, base, word, selected, clear);
  }
}

// Emit a cached minimum lookup using the ADT's physical layout. Moderate sets
// scan only from the last lower bound; large sets descend exact summaries on a
// cache miss. No runtime helper or integer wider than i64 enters the hot path.
inline std::pair<mlir::Value, mlir::Value>
findEvalReadyWord(mlir::OpBuilder &builder, mlir::Location loc,
                  mlir::Value base, const runtime::ReadySetLayout &layout) {
  using namespace mlir;
  auto i64 = builder.getI64Type();
  Value zero = llvmConstant(builder, loc, i64, 0);
  if (!layout.hasCache())
    return {zero, loadOwnerWord(builder, loc, base, 0)};
  Region *region = builder.getInsertionBlock()->getParent();
  auto block = [&](unsigned args = 0) {
    auto *result = new Block;
    region->push_back(result);
    for (unsigned i = 0; i != args; ++i)
      result->addArgument(i64, loc);
    return result;
  };
  Block *check = block(1);
  Block *load = block(1);
  Block *miss = block(1);
  Block *empty = block();
  Block *found = block(2);
  Block *join = block(2);
  Value count = llvmConstant(builder, loc, i64, layout.counts[0]);
  Value cached = loadOwnerWord(builder, loc, base, layout.cacheOffset());
  cf::BranchOp::create(builder, loc, check, ValueRange{cached});
  builder.setInsertionPointToStart(check);
  Value inRange = arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ult,
                                        check->getArgument(0), count);
  cf::CondBranchOp::create(builder, loc, inRange, load,
                           ValueRange{check->getArgument(0)}, empty,
                           ValueRange{});
  auto loadIndexed = [&](Value index, unsigned offset = 0) -> Value {
    if (offset)
      index = arith::AddIOp::create(builder, loc, index,
                                    llvmConstant(builder, loc, i64, offset));
    Value address = LLVM::GEPOp::create(builder, loc, base.getType(), i64, base,
                                        ValueRange{index});
    return LLVM::LoadOp::create(builder, loc, i64, address, 8);
  };
  builder.setInsertionPointToStart(load);
  Value index = load->getArgument(0);
  Value bits = loadIndexed(index);
  Value nonempty =
      arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne, bits, zero);
  cf::CondBranchOp::create(builder, loc, nonempty, found,
                           ValueRange{index, bits}, miss, ValueRange{index});
  builder.setInsertionPointToStart(miss);
  if (!layout.hasSummaries()) {
    Value next = arith::AddIOp::create(builder, loc, miss->getArgument(0),
                                       llvmConstant(builder, loc, i64, 1));
    cf::BranchOp::create(builder, loc, check, ValueRange{next});
  } else {
    Value summary =
        loadOwnerWord(builder, loc, base, layout.offsets[layout.levels - 1]);
    Value any = arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::ne,
                                      summary, zero);
    Block *descend = block();
    cf::CondBranchOp::create(builder, loc, any, descend, ValueRange{}, empty,
                             ValueRange{});
    builder.setInsertionPointToStart(descend);
    Value child =
        LLVM::CountTrailingZerosOp::create(builder, loc, i64, summary, true);
    for (unsigned level = layout.levels - 1; --level != 0;) {
      Value word = loadIndexed(child, layout.offsets[level]);
      Value bit =
          LLVM::CountTrailingZerosOp::create(builder, loc, i64, word, true);
      child = arith::AddIOp::create(
          builder, loc, bit,
          arith::MulIOp::create(builder, loc, child,
                                llvmConstant(builder, loc, i64, 64)));
    }
    cf::BranchOp::create(builder, loc, found,
                         ValueRange{child, loadIndexed(child)});
  }
  builder.setInsertionPointToStart(found);
  LLVM::StoreOp::create(
      builder, loc, found->getArgument(0),
      ownerWordAddress(builder, loc, base, layout.cacheOffset()), 8);
  cf::BranchOp::create(builder, loc, join, found->getArguments());
  builder.setInsertionPointToStart(empty);
  LLVM::StoreOp::create(
      builder, loc, count,
      ownerWordAddress(builder, loc, base, layout.cacheOffset()), 8);
  cf::BranchOp::create(builder, loc, join, ValueRange{count, zero});
  builder.setInsertionPointToStart(join);
  return {join->getArgument(0), join->getArgument(1)};
}

} // namespace obelisk::detail
#endif
