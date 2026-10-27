//===- SimulationProcessDescriptorLowering.cpp - Process ABI globals -----===//

#include "SimulationProcessWrapperLowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "talos/Conversion/RuntimeToLLVM.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "talos/Analysis/SimulationProcessFrameAnalysis.h"
#include "talos/Runtime/Runtime.h"
#include "talos/Runtime/StableHash.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Interfaces/CallInterfaces.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"

using namespace mlir;

namespace talos::detail {

uint64_t stableProcessID(StringRef name) {
  return talos_stable_hash(name.data(), name.size());
}

namespace {
bool isUnmanagedNativeType(Type type) {
  if (auto ref = dyn_cast<sim::RefType>(type))
    type = ref.getElementType();
  else if (auto net = dyn_cast<sim::NetType>(type))
    type = net.getElementType();
  else if (auto driver = dyn_cast<sim::DriverType>(type))
    type = driver.getElementType();
  // A fixed unpacked array of scalar elements is plain data, whether held as
  // a value or addressed through a reference, net or driver. It owns no
  // managed allocation, so it needs no GC lane. Arrays of handles stay
  // conservative.
  for (unsigned depth = 0; depth != 16; ++depth) {
    auto array = dyn_cast<sim::UnpackedArrayType>(type);
    if (!array)
      break;
    type = array.getElementType();
  }
  return isa<IntegerType, FloatType, sim::LogicType, sim::ContextType,
             sim::ProcessType, sim::TimeType, sim::BytesType, sim::ControlType,
             sim::PackedArrayType, sim::PackedStructType, sim::PackedUnionType>(
      type);
}

/// A body is unmanaged when it holds only scalar values and each of its
/// calls satisfies `unmanagedCall`.
template <typename CallVerdict>
bool scanUnmanagedNativeBody(sim::SimFuncOp function,
                             CallVerdict &&unmanagedCall) {
  return !function.isExternal() &&
         !function
              .walk([&](Operation *operation) {
                if (operation == function.getOperation())
                  return WalkResult::advance();
                if (!llvm::all_of(operation->getOperandTypes(),
                                  isUnmanagedNativeType) ||
                    !llvm::all_of(operation->getResultTypes(),
                                  isUnmanagedNativeType))
                  return WalkResult::interrupt();
                if (auto call = dyn_cast<sim::SimCallOp>(operation))
                  return unmanagedCall(call) ? WalkResult::advance()
                                             : WalkResult::interrupt();
                return isa<CallOpInterface, sim::SimGCSafepointOp,
                           sim::SimDPICallOp>(operation)
                           ? WalkResult::interrupt()
                           : WalkResult::advance();
              })
              .wasInterrupted();
}

/// Calls must reach only unmanaged bodies. Recursion and unresolved or
/// external callees stay conservative.
bool isUnmanagedNativeBody(sim::SimFuncOp function,
                           UnmanagedNativeCache &cache) {
  if (auto found = cache.bodies.find(function.getOperation());
      found != cache.bodies.end())
    return found->second;
  // A cycle back to this body while it is analyzed is conservative.
  cache.bodies[function.getOperation()] = false;
  bool unmanaged = scanUnmanagedNativeBody(function, [&](sim::SimCallOp call) {
    auto callee = cache.symbols.lookupNearestSymbolFrom<sim::SimFuncOp>(
        call, call.getCalleeAttr());
    return callee && callee.getEntryKind() == sim::EntryKind::Function &&
           isUnmanagedNativeBody(callee, cache);
  });
  cache.bodies[function.getOperation()] = unmanaged;
  return unmanaged;
}
} // namespace

bool isUnmanagedNativeProcess(sim::SimFuncOp function,
                              UnmanagedNativeCache &cache) {
  return isUnmanagedNativeBody(function, cache);
}

bool isCallFreeUnmanagedNativeProcess(sim::SimFuncOp function) {
  return scanUnmanagedNativeBody(function,
                                 [](sim::SimCallOp) { return false; });
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

  std::string continuationsName = (baseName + ".__talos_continuations").str();
  std::string layoutName = (baseName + ".__talos_frame_layout").str();
  std::string descriptorName =
      (baseName + ".__talos_process_descriptor").str();
  std::string designBytecodeName =
      (baseName + ".__talos_bytecode_entry").str();
  constexpr StringLiteral executionName = "__talos_execution_descriptor_v1";
  // These embedded-image symbols are frozen before process finalization.
  // Generated wrappers and frame globals do not change their presence.
  bool hasExecution = embeddedSymbols.lookup(executionName) != nullptr;
  bool hasDesignBytecode =
      embeddedSymbols.lookup(designBytecodeName) != nullptr;
  bool bytecodeOnly =
      runtime::bitEnumContainsAll(runtime::getExecutionFlags(module),
                                  runtime::ExecutionFlags::RequireBytecode);
  if (bytecodeOnly && !hasDesignBytecode)
    return module.emitError(
        "bytecode-only process descriptor has no encoded entry");

  // Frame field tables depend only on the frame shape, so processes with
  // equal shapes share one. The runtime reads them only through
  // frame_layout.fields and never compares their addresses. Attributes are
  // uniqued, so equal tables have equal attributes; the name is keyed by a
  // stable hash of the contents.
  SmallVector<Attribute> fieldRows;
  SmallVector<uint64_t> fieldWords;
  fieldRows.reserve(analysis.getFields().size());
  for (const auto &field : analysis.getFields()) {
    uint64_t row[] = {static_cast<uint32_t>(field.kind),
                      static_cast<uint32_t>(field.flags),
                      field.offset,
                      field.size,
                      field.alignment,
                      field.reserved};
    fieldRows.push_back(integerStructAttr(fieldType, row));
    llvm::append_range(fieldWords, row);
  }
  ArrayAttr fields = ArrayAttr::get(context, fieldRows);
  uint64_t fieldsHash = talos_stable_hash(
      fieldWords.data(), fieldWords.size() * sizeof(uint64_t));
  std::string sharedName =
      "__talos_frame_fields_" + llvm::utohexstr(fieldsHash, true);
  std::string fieldsName = sharedName;
  for (unsigned probe = 1;; ++probe) {
    auto shared = embeddedSymbols.lookup<LLVM::GlobalOp>(fieldsName);
    if (!shared) {
      embeddedSymbols.insert(
          makeConstantGlobal(module, location, fieldsType, fieldsName,
                             LLVM::Linkage::Internal, 8, fields));
      break;
    }
    if (shared.getGlobalType() == fieldsType &&
        shared.getValueOrNull() == fields)
      break;
    fieldsName = (sharedName + "_" + Twine(probe)).str();
  }
  SmallVector<uint64_t> continuations(analysis.getContinuations().begin(),
                                      analysis.getContinuations().end());
  makeConstantGlobal(module, location, continuationsType, continuationsName,
                     LLVM::Linkage::Internal, 4,
                     integerArrayAttr(cast<IntegerType>(i32), continuations));
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
            llvmConstant(builder, location, i32, TALOS_RT_VERSION), 1);
        uint32_t flags = analysis.isBytecodeReadOnly()
                             ? TALOS_RT_PROCESS_BYTECODE_READ_ONLY
                             : 0;
        if (unmanagedNative && !bytecodeOnly)
          flags |= TALOS_RT_PROCESS_UNMANAGED_NATIVE |
                   (tableProcess ? TALOS_RT_PROCESS_TABLE_NATIVE : 0);
        if (flags)
          descriptor =
              insertValue(builder, location, descriptor,
                          llvmConstant(builder, location, i32, flags), 2);
        uint32_t availableTiers =
            bytecodeOnly
                ? TALOS_RT_TIER_MASK_BYTECODE
                : (hasDesignBytecode ? TALOS_RT_TIER_MASK_NATIVE |
                                           TALOS_RT_TIER_MASK_BYTECODE
                                     : TALOS_RT_TIER_MASK_NATIVE);
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
                      ? (baseName + ".__talos_native_requirements").str()
                      : nativeZeroRequirementsName.str()),
              6);
          descriptor = insertValue(
              builder, location, descriptor,
              LLVM::AddressOfOp::create(
                  builder, location, pointer,
                  tableProcess
                      ? "talos_rt_v1_table_process_execute"
                      : (baseName + ".__talos_native_execute").str()),
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
                               (baseName + ".__talos_table.plan").str()),
                           1);
      });
  return success();
}

} // namespace talos::detail
