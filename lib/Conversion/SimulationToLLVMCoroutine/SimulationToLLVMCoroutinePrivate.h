//===- SimulationToLLVMCoroutinePrivate.h - Shared lowering support ------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
class DataLayout;
}

namespace mlir {
class RewritePatternSet;
class TypeConverter;
} // namespace mlir

namespace obelisk::detail {

inline constexpr llvm::StringLiteral nativeTwoStateBlockUnknownsAttr =
    "obelisk.native.two_state_block_unknowns";

struct SignedI64Index {
  mlir::Value value;
  mlir::Value representable;
};

bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result);
bool containsLogic(mlir::Type type);
std::optional<unsigned> nativeStateWidth(mlir::Type type);
mlir::SmallVector<mlir::Value> flatten(mlir::ArrayRef<mlir::ValueRange> ranges);
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
mlir::Value resizeNativeInteger(mlir::OpBuilder &builder,
                                mlir::Location location, mlir::Value value,
                                mlir::IntegerType result,
                                bool isSigned = false);
SignedI64Index resizeSignedIndexToI64(mlir::OpBuilder &builder,
                                      mlir::Location location,
                                      mlir::Value source);
mlir::Value insertValue(mlir::OpBuilder &builder, mlir::Location location,
                        mlir::Value aggregate, mlir::Value element,
                        int64_t index);
void emitNativeStateRetain(mlir::OpBuilder &builder, mlir::Location location,
                           mlir::Value handle);
mlir::Operation *reportManagedStatus(mlir::OpBuilder &builder,
                                     mlir::Location location,
                                     mlir::Value context, mlir::Value status);

std::string managedClassDescriptorName(mlir::SymbolRefAttr className);
std::string managedMethodThunkName(llvm::StringRef methodName);
mlir::LLVM::GlobalOp makeByteArrayGlobal(mlir::ModuleOp module,
                                         mlir::Location location,
                                         llvm::StringRef name,
                                         llvm::StringRef bytes);
mlir::LLVM::GlobalOp makeConstantGlobal(
    mlir::ModuleOp module, mlir::Location location, mlir::Type type,
    llvm::StringRef name, mlir::LLVM::Linkage linkage, uint64_t alignment,
    llvm::function_ref<mlir::Value(mlir::OpBuilder &)> initializer);

mlir::LLVM::LLVMFuncOp
getOrDeclareLLVMFunction(mlir::ModuleOp module, llvm::StringRef name,
                         mlir::Type result,
                         mlir::ArrayRef<mlir::Type> arguments);

mlir::LogicalResult lowerNativeDPICalls(mlir::Operation *root);
mlir::LogicalResult materializeDPIThunks(mlir::ModuleOp module);
void populateManagedToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                             mlir::TypeConverter &converter,
                                             const llvm::DataLayout &dataLayout,
                                             uint64_t stateBitCount);
void populateAggregateToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateControlToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                             mlir::TypeConverter &converter);
void populateFunctionTypeConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    const llvm::DenseSet<mlir::Value> &twoStateValues);
void populateEventToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                           mlir::TypeConverter &converter);
void populateSuspensionTypeConversionPatterns(mlir::RewritePatternSet &patterns,
                                              mlir::TypeConverter &converter);
void populateContextRuntimeToLLVMConversionPattern(
    mlir::RewritePatternSet &patterns, const mlir::TypeConverter &converter);
mlir::LogicalResult
materializeManagedMethodThunks(mlir::ModuleOp module,
                               const llvm::DataLayout &dataLayout);
mlir::LogicalResult materializeNativeObserverThunks(mlir::ModuleOp module);
mlir::LogicalResult prepareManagedLowering(mlir::ModuleOp module,
                                           const llvm::DataLayout &dataLayout);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H
