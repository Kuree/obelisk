//===- SimulationEvalNBAQueue.h - Generated ordered NBA storage -*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_EVAL_NBA_QUEUE_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_EVAL_NBA_QUEUE_H

#include "SimulationToLLVMCoroutinePrivate.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

namespace obelisk::detail {

inline constexpr llvm::StringLiteral evalNBAQueueName =
    "__obelisk_eval_ordered_nba_queue_v1";

// Use the target's struct layout, not host pointer offsets. These fields match
// runtime::EvalNBAQueue; its records contain four consecutive i64 fields.
inline mlir::Value evalNBAQueueField(mlir::OpBuilder &builder,
                                     mlir::Location loc, unsigned field) {
  auto pointer = mlir::LLVM::LLVMPointerType::get(builder.getContext());
  auto type = mlir::LLVM::LLVMStructType::getLiteral(
      builder.getContext(), {pointer, builder.getI32Type(),
                             builder.getI32Type(), builder.getI32Type()});
  auto base =
      mlir::LLVM::AddressOfOp::create(builder, loc, pointer, evalNBAQueueName);
  return mlir::LLVM::GEPOp::create(
      builder, loc, pointer, type, base,
      llvm::ArrayRef<mlir::LLVM::GEPArg>{0, static_cast<int32_t>(field)});
}

inline mlir::Value evalNBAQueueSize(mlir::OpBuilder &builder,
                                    mlir::Location loc) {
  return mlir::LLVM::LoadOp::create(builder, loc, builder.getI32Type(),
                                    evalNBAQueueField(builder, loc, 1), 4);
}

} // namespace obelisk::detail

#endif
