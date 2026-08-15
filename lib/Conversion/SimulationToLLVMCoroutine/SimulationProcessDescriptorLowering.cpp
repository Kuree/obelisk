//===- SimulationProcessDescriptorLowering.cpp - Process ABI globals -----===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {

uint64_t stableProcessID(StringRef name) {
  return obelisk_stable_hash(name.data(), name.size());
}

LogicalResult
makeProcessDescriptor(ModuleOp module, Location location, StringRef baseName,
                      uint64_t stableID,
                      const SimulationProcessFrameAnalysis &analysis) {
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  auto fieldType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, i64, i32, i32});
  auto fieldsType =
      LLVM::LLVMArrayType::get(fieldType, analysis.getFields().size());
  auto continuationsType =
      LLVM::LLVMArrayType::get(i32, analysis.getContinuations().size());
  auto layoutType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, pointer, i32, i32, pointer, i64});
  auto handleType = LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64});
  auto descriptorType = LLVM::LLVMStructType::getLiteral(
      context, {handleType, i32, i32, i32, i32, pointer, pointer, pointer,
                pointer, pointer, pointer, pointer});

  std::string fieldsName = (baseName + ".__obelisk_frame_fields").str();
  std::string continuationsName = (baseName + ".__obelisk_continuations").str();
  std::string layoutName = (baseName + ".__obelisk_frame_layout").str();
  std::string descriptorName =
      (baseName + ".__obelisk_process_descriptor").str();
  std::string designBytecodeName =
      (baseName + ".__obelisk_bytecode_entry").str();
  constexpr StringLiteral executionName = "__obelisk_execution_descriptor_v1";
  bool hasExecution = module.lookupSymbol(executionName) != nullptr;
  bool hasDesignBytecode = module.lookupSymbol(designBytecodeName) != nullptr;
  auto executionFlags =
      module->getAttrOfType<IntegerAttr>("obelisk.execution.flags");
  bool bytecodeOnly =
      executionFlags && (executionFlags.getValue().getZExtValue() &
                         OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0;
  if (bytecodeOnly && !hasDesignBytecode)
    return module.emitError(
        "bytecode-only process descriptor has no encoded entry");

  makeConstantGlobal(
      module, location, fieldsType, fieldsName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &builder) {
        Value array = LLVM::ZeroOp::create(builder, location, fieldsType);
        for (auto [index, field] : llvm::enumerate(analysis.getFields())) {
          Value value = LLVM::ZeroOp::create(builder, location, fieldType);
          value = insertValue(builder, location, value,
                              llvmConstant(builder, location, i32,
                                           static_cast<uint32_t>(field.kind)),
                              0);
          value = insertValue(builder, location, value,
                              llvmConstant(builder, location, i32,
                                           static_cast<uint32_t>(field.flags)),
                              1);
          value = insertValue(
              builder, location, value,
              llvmConstant(builder, location, i64, field.offset), 2);
          value =
              insertValue(builder, location, value,
                          llvmConstant(builder, location, i64, field.size), 3);
          value = insertValue(
              builder, location, value,
              llvmConstant(builder, location, i32, field.alignment), 4);
          value = insertValue(
              builder, location, value,
              llvmConstant(builder, location, i32, field.reserved), 5);
          array = LLVM::InsertValueOp::create(
              builder, location, array, value,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return array;
      });
  makeConstantGlobal(module, location, continuationsType, continuationsName,
                     LLVM::Linkage::Internal, 4, [&](OpBuilder &builder) {
                       Value array = LLVM::ZeroOp::create(builder, location,
                                                          continuationsType);
                       for (auto [index, continuation] :
                            llvm::enumerate(analysis.getContinuations()))
                         array = LLVM::InsertValueOp::create(
                             builder, location, array,
                             llvmConstant(builder, location, i32, continuation),
                             ArrayRef<int64_t>{static_cast<int64_t>(index)});
                       return array;
                     });
  makeConstantGlobal(
      module, location, layoutType, layoutName, LLVM::Linkage::Internal, 8,
      [&](OpBuilder &builder) {
        Value layout = LLVM::ZeroOp::create(builder, location, layoutType);
        layout = insertValue(builder, location, layout,
                             llvmConstant(builder, location, i32, 1), 0);
        layout = insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i64, analysis.getFrameSize()), 2);
        layout = insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i64, analysis.getFrameAlignment()),
            3);
        layout = insertValue(
            builder, location, layout,
            LLVM::AddressOfOp::create(builder, location, pointer, fieldsName),
            4);
        layout = insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i32, analysis.getFields().size()),
            5);
        layout = insertValue(builder, location, layout,
                             llvmConstant(builder, location, i32,
                                          analysis.getContinuations().size()),
                             6);
        layout = insertValue(builder, location, layout,
                             LLVM::AddressOfOp::create(
                                 builder, location, pointer, continuationsName),
                             7);
        return insertValue(
            builder, location, layout,
            llvmConstant(builder, location, i64, analysis.getChecksum()), 8);
      });
  makeConstantGlobal(
      module, location, descriptorType, descriptorName, LLVM::Linkage::External,
      8, [&](OpBuilder &builder) {
        Value handle = LLVM::ZeroOp::create(builder, location, handleType);
        handle = insertValue(builder, location, handle,
                             llvmConstant(builder, location, i32, 6), 0);
        handle = insertValue(builder, location, handle,
                             llvmConstant(builder, location, i64, stableID), 2);
        Value descriptor =
            LLVM::ZeroOp::create(builder, location, descriptorType);
        descriptor = insertValue(builder, location, descriptor, handle, 0);
        descriptor = insertValue(
            builder, location, descriptor,
            llvmConstant(builder, location, i32, OBELISK_RT_VERSION), 1);
        uint32_t availableTiers =
            bytecodeOnly
                ? OBELISK_RT_TIER_MASK_BYTECODE
                : (hasDesignBytecode ? OBELISK_RT_TIER_MASK_NATIVE |
                                           OBELISK_RT_TIER_MASK_BYTECODE
                                     : OBELISK_RT_TIER_MASK_NATIVE);
        descriptor = insertValue(
            builder, location, descriptor,
            llvmConstant(builder, location, i32, availableTiers), 3);
        descriptor = insertValue(
            builder, location, descriptor,
            LLVM::AddressOfOp::create(builder, location, pointer, layoutName),
            5);
        if (!bytecodeOnly) {
          descriptor = insertValue(
              builder, location, descriptor,
              LLVM::AddressOfOp::create(
                  builder, location, pointer,
                  (baseName + ".__obelisk_native_requirements").str()),
              6);
          descriptor =
              insertValue(builder, location, descriptor,
                          LLVM::AddressOfOp::create(
                              builder, location, pointer,
                              (baseName + ".__obelisk_native_execute").str()),
                          7);
          descriptor =
              insertValue(builder, location, descriptor,
                          LLVM::AddressOfOp::create(
                              builder, location, pointer,
                              (baseName + ".__obelisk_native_destroy").str()),
                          8);
        }
        if (hasExecution)
          descriptor =
              insertValue(builder, location, descriptor,
                          LLVM::AddressOfOp::create(builder, location, pointer,
                                                    executionName),
                          10);
        if (hasDesignBytecode)
          descriptor =
              insertValue(builder, location, descriptor,
                          LLVM::AddressOfOp::create(builder, location, pointer,
                                                    designBytecodeName),
                          11);
        return descriptor;
      });
  return success();
}

} // namespace obelisk::detail
