//===- SimulationEvalReadySet.h - Word-wise generated owner sets -*- C++
//-*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_EVAL_READY_SET_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_EVAL_READY_SET_H

#include "SimulationToLLVMCoroutinePrivate.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

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

inline void updateOwnerWord(mlir::OpBuilder &builder, mlir::Location loc,
                            mlir::Value base, unsigned word, mlir::Value mask,
                            bool clear = false) {
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
}

inline void updateOwnerMask(mlir::OpBuilder &builder, mlir::Location loc,
                            mlir::Value base, const llvm::APInt &mask,
                            bool clear = false, mlir::Value condition = {}) {
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
    updateOwnerWord(builder, loc, base, word, selected, clear);
  }
}

} // namespace obelisk::detail
#endif
