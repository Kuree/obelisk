//===- SimulationEvalNBAQueue.h - Generated ordered NBA storage -*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_EVAL_NBA_QUEUE_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_EVAL_NBA_QUEUE_H

#include "SimulationToLLVMCoroutinePrivate.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include <cassert>
#include <cstdint>

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

// A payload wider than one record is staged as consecutive records of at most
// 64 bits. Each chunk drains through its own switch case and so needs a site
// identity of its own. Source NBA site IDs are dense and far below 2^48; the
// top bit keeps chunk identities disjoint from them.
inline uint64_t evalNBAChunkSite(uint64_t site, uint64_t chunk) {
  assert(site < (uint64_t{1} << 48) && chunk < (uint64_t{1} << 15) &&
         "NBA site or chunk index exceeds the chunk-site encoding");
  return (uint64_t{1} << 63) | (chunk << 48) | site;
}

inline mlir::Value evalNBAQueueSize(mlir::OpBuilder &builder,
                                    mlir::Location loc) {
  return mlir::LLVM::LoadOp::create(builder, loc, builder.getI32Type(),
                                    evalNBAQueueField(builder, loc, 1), 4);
}

} // namespace obelisk::detail

#endif
