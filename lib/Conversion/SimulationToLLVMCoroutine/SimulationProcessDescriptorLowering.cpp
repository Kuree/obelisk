//===- SimulationProcessDescriptorLowering.cpp - Process ABI globals -----===//

#include "SimulationProcessWrapperLowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Interfaces/CallInterfaces.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {

uint64_t stableProcessID(StringRef name) {
  return obelisk_stable_hash(name.data(), name.size());
}

bool isUnmanagedNativeProcess(sim::SimFuncOp function) {
  auto scalar = [](Type type) {
    if (auto ref = dyn_cast<sim::RefType>(type)) {
      type = ref.getElementType();
      // A fixed array reference is an address, not an aggregate value or a
      // managed allocation. Scalar element accesses need no GC lane. Keep
      // whole-array values, arrays of handles, and actual calls conservative.
      for (unsigned depth = 0; depth != 16; ++depth) {
        auto array = dyn_cast<sim::UnpackedArrayType>(type);
        if (!array)
          break;
        type = array.getElementType();
      }
    }
    if (auto net = dyn_cast<sim::NetType>(type))
      type = net.getElementType();
    if (auto driver = dyn_cast<sim::DriverType>(type))
      type = driver.getElementType();
    return isa<IntegerType, FloatType, sim::LogicType, sim::ContextType,
               sim::ProcessType, sim::TimeType, sim::BytesType, sim::ControlType,
               sim::PackedArrayType, sim::PackedStructType,
               sim::PackedUnionType>(type);
  };
  // Deliberately conservative: calls and non-scalar values need the ordinary
  // scope, even if a future interprocedural analysis could prove otherwise.
  return !function
              .walk([&](Operation *operation) {
                if (operation == function.getOperation())
                  return WalkResult::advance();
                if (isa<CallOpInterface, sim::SimGCSafepointOp,
                        sim::SimDPICallOp>(operation) ||
                    !llvm::all_of(operation->getOperandTypes(), scalar) ||
                    !llvm::all_of(operation->getResultTypes(), scalar))
                  return WalkResult::interrupt();
                return WalkResult::advance();
              })
              .wasInterrupted();
}

LogicalResult
makeProcessDescriptor(ModuleOp module, SymbolTable &embeddedSymbols,
                      Location location, StringRef baseName, uint64_t stableID,
                      const SimulationProcessFrameAnalysis &analysis,
                      bool unmanagedNative, bool usesCoroutine,
                      bool tableProcess) {
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
  Type descriptorType = getNativeProcessDescriptorType(context);
  Type globalType =
      tableProcess
          ? LLVM::LLVMStructType::getLiteral(context, {descriptorType, pointer})
          : descriptorType;

  std::string fieldsName = (baseName + ".__obelisk_frame_fields").str();
  std::string continuationsName = (baseName + ".__obelisk_continuations").str();
  std::string layoutName = (baseName + ".__obelisk_frame_layout").str();
  std::string descriptorName =
      (baseName + ".__obelisk_process_descriptor").str();
  std::string designBytecodeName =
      (baseName + ".__obelisk_bytecode_entry").str();
  constexpr StringLiteral executionName = "__obelisk_execution_descriptor_v1";
  // These embedded-image symbols are frozen before process finalization.
  // Generated wrappers and frame globals do not change their presence.
  bool hasExecution = embeddedSymbols.lookup(executionName) != nullptr;
  bool hasDesignBytecode = embeddedSymbols.lookup(designBytecodeName) != nullptr;
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
  // The embedded design declares every activation descriptor before process
  // finalization, so the snapshot already holds the declaration. Erase it
  // through the table: a linear module lookup here made finalization
  // quadratic in the number of processes.
  if (auto declaration =
          embeddedSymbols.lookup<LLVM::GlobalOp>(descriptorName)) {
    if (!declaration.getInitializerRegion().empty() || declaration.getValue() ||
        declaration.getGlobalType() != descriptorType)
      return declaration.emitError(
          "incompatible native process descriptor definition");
    embeddedSymbols.erase(declaration);
  }
  makeConstantGlobal(
      module, location, globalType, descriptorName, LLVM::Linkage::External, 8,
      [&](OpBuilder &builder) {
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
        uint32_t flags = analysis.isBytecodeReadOnly()
                             ? OBELISK_RT_PROCESS_BYTECODE_READ_ONLY
                             : 0;
        if (unmanagedNative && !bytecodeOnly)
          flags |= OBELISK_RT_PROCESS_UNMANAGED_NATIVE |
                   (tableProcess ? OBELISK_RT_PROCESS_TABLE_NATIVE : 0);
        if (flags)
          descriptor =
              insertValue(builder, location, descriptor,
                          llvmConstant(builder, location, i32, flags), 2);
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
                  usesCoroutine
                      ? (baseName + ".__obelisk_native_requirements").str()
                      : nativeZeroRequirementsName.str()),
              6);
          descriptor = insertValue(
              builder, location, descriptor,
              LLVM::AddressOfOp::create(
                  builder, location, pointer,
                  tableProcess
                      ? "obelisk_rt_v1_table_process_execute"
                      : (baseName + ".__obelisk_native_execute").str()),
              7);
          descriptor =
              insertValue(builder, location, descriptor,
                          LLVM::AddressOfOp::create(
                              builder, location, pointer,
                              usesCoroutine ? nativeCoroutineDestroyName
                                            : nativeNoopDestroyName),
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
        if (!tableProcess)
          return descriptor;
        Value extended = LLVM::ZeroOp::create(builder, location, globalType);
        extended = insertValue(builder, location, extended, descriptor, 0);
        return insertValue(builder, location, extended,
                           LLVM::AddressOfOp::create(
                               builder, location, pointer,
                               (baseName + ".__obelisk_table.plan").str()),
                           1);
      });
  return success();
}

} // namespace obelisk::detail
