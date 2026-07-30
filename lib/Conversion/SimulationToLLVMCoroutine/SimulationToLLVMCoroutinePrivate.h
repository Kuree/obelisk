//===- SimulationToLLVMCoroutinePrivate.h - Shared lowering support ------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include <cstdint>

namespace obelisk::detail {

mlir::Value llvmConstant(mlir::OpBuilder &builder, mlir::Location location,
                         mlir::Type type, uint64_t value);
mlir::Value entryAlloca(mlir::OpBuilder &builder, mlir::Location location,
                        mlir::Type elementType, uint64_t count,
                        unsigned alignment);
mlir::Value byteGEP(mlir::OpBuilder &builder, mlir::Location location,
                    mlir::Value base, uint64_t offset);
mlir::Value loadAt(mlir::OpBuilder &builder, mlir::Location location,
                   mlir::Value base, uint64_t offset, mlir::Type type,
                   unsigned alignment);
void storeAt(mlir::OpBuilder &builder, mlir::Location location,
             mlir::Value base, uint64_t offset, mlir::Value value,
             unsigned alignment);
mlir::Value castIntegerWidth(mlir::OpBuilder &builder, mlir::Location location,
                             mlir::Value value, mlir::Type target);

mlir::LLVM::LLVMFuncOp
getOrDeclareLLVMFunction(mlir::ModuleOp module, llvm::StringRef name,
                         mlir::Type result,
                         mlir::ArrayRef<mlir::Type> arguments);

mlir::LogicalResult lowerNativeDPICalls(mlir::Operation *root);
mlir::LogicalResult materializeDPIThunks(mlir::ModuleOp module);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H
