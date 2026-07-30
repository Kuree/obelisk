//===- SimulationDPILowering.cpp - Native DPI lowering ----------------===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "SimulationDPILowering.h"

#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/PatternMatch.h"

#include "llvm/ADT/Twine.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

using namespace mlir;

namespace obelisk::detail {

Value makeDPIPlaneStorage(OpBuilder &builder, Location location, Value value,
                          unsigned alignment) {
  Value address = entryAlloca(builder, location, value.getType(), 1, alignment);
  LLVM::StoreOp::create(builder, location, value, address, alignment);
  return address;
}

namespace {

static_assert(sizeof(obelisk_rt_import_input_v1) ==
              sizeof(obelisk_rt_import_output_v1));
static_assert(alignof(obelisk_rt_import_input_v1) ==
              alignof(obelisk_rt_import_output_v1));
static_assert(offsetof(obelisk_rt_import_input_v1, kind) ==
              offsetof(obelisk_rt_import_output_v1, kind));
static_assert(offsetof(obelisk_rt_import_input_v1, flags) ==
              offsetof(obelisk_rt_import_output_v1, flags));
static_assert(offsetof(obelisk_rt_import_input_v1, reserved) ==
              offsetof(obelisk_rt_import_output_v1, reserved));
static_assert(offsetof(obelisk_rt_import_input_v1, bit_width) ==
              offsetof(obelisk_rt_import_output_v1, bit_width));
static_assert(offsetof(obelisk_rt_import_input_v1, value) ==
              offsetof(obelisk_rt_import_output_v1, value));
static_assert(offsetof(obelisk_rt_import_input_v1, unknown) ==
              offsetof(obelisk_rt_import_output_v1, unknown));
static_assert(offsetof(obelisk_rt_import_input_v1, limb_count) ==
              offsetof(obelisk_rt_import_output_v1, limb_count));

uint64_t appendHash(uint64_t hash, uint64_t value, unsigned bytes) {
  for (unsigned index = 0; index != bytes; ++index) {
    hash ^= static_cast<uint8_t>(value >> (index * 8));
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

Value padDPIPlane(OpBuilder &builder, Location location, Value value,
                  uint32_t width) {
  uint64_t paddedWidth = ((uint64_t{width} + 63) / 64) * 64;
  Type paddedType = IntegerType::get(builder.getContext(),
                                     static_cast<unsigned>(paddedWidth));
  if (value.getType() == paddedType)
    return value;
  auto integer = dyn_cast<IntegerType>(value.getType());
  if (!integer || integer.getWidth() != width)
    return {};
  return arith::ExtUIOp::create(builder, location, paddedType, value);
}

Value makeZeroDPIPlaneStorage(OpBuilder &builder, Location location, Type type,
                              unsigned alignment = 8) {
  Value zero = LLVM::ZeroOp::create(builder, location, type);
  return makeDPIPlaneStorage(builder, location, zero, alignment);
}

LogicalResult lowerNativeDPICall(sim::SimDPICallOp operation,
                                 IRRewriter &rewriter) {
  ModuleOp module = operation->getParentOfType<ModuleOp>();
  if (!module)
    return operation.emitOpError("requires a containing module");
  Location location = operation.getLoc();
  MLIRContext *context = operation.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i8 = rewriter.getI8Type();
  Type i16 = rewriter.getI16Type();
  Type i32 = rewriter.getI32Type();
  Type i64 = rewriter.getI64Type();
  Value null = LLVM::ZeroOp::create(rewriter, location, pointer);
  auto logicalCountAttr = operation->getAttrOfType<IntegerAttr>(
      "obelisk.dpi.logical_operand_count");
  uint64_t logicalInputs = logicalCountAttr
                               ? logicalCountAttr.getValue().getZExtValue()
                               : operation.getArguments().size();
  ArrayAttr signature = operation.getAbiSignature();
  if (logicalInputs > signature.size())
    return operation.emitOpError("has an invalid logical operand count");
  uint64_t logicalOutputs = signature.size() - logicalInputs;

  SmallVector<DPIOperandABI> abi;
  abi.reserve(signature.size());
  for (Attribute attribute : signature) {
    FailureOr<DPIOperandABI> parsed = parseDPIOperandABI(attribute, operation);
    if (failed(parsed))
      return failure();
    abi.push_back(*parsed);
  }

  SmallVector<Value> physicalInputs(operation.getArguments());
  size_t physicalInput = 0;
  SmallVector<std::pair<Value, Value>> inputPlanes;
  inputPlanes.reserve(logicalInputs);
  for (uint64_t index = 0; index != logicalInputs; ++index) {
    if (physicalInput >= physicalInputs.size())
      return operation.emitOpError("is missing a physical DPI input plane");
    Value value = physicalInputs[physicalInput++];
    Value unknown;
    if (abi[index].fourState) {
      if (physicalInput >= physicalInputs.size())
        return operation.emitOpError("is missing a physical DPI unknown plane");
      unknown = physicalInputs[physicalInput++];
    }
    value = padDPIPlane(rewriter, location, value, abi[index].width);
    if (!value)
      return operation.emitOpError("has a malformed DPI value plane");
    if (unknown) {
      unknown = padDPIPlane(rewriter, location, unknown, abi[index].width);
      if (!unknown)
        return operation.emitOpError("has a malformed DPI unknown plane");
    }
    inputPlanes.emplace_back(
        makeDPIPlaneStorage(rewriter, location, value),
        unknown ? makeDPIPlaneStorage(rewriter, location, unknown) : null);
  }
  if (physicalInput != physicalInputs.size())
    return operation.emitOpError("has excess physical DPI input planes");

  SmallVector<std::pair<Value, Value>> outputPlanes;
  SmallVector<Value> physicalOutputValues;
  outputPlanes.reserve(logicalOutputs);
  for (uint64_t index = logicalInputs; index != abi.size(); ++index) {
    uint64_t paddedWidth = ((uint64_t{abi[index].width} + 63) / 64) * 64;
    Type valueType =
        IntegerType::get(context, static_cast<unsigned>(paddedWidth));
    Value value = makeZeroDPIPlaneStorage(rewriter, location, valueType);
    Value unknown = abi[index].fourState
                        ? makeZeroDPIPlaneStorage(rewriter, location, valueType)
                        : null;
    outputPlanes.emplace_back(value, unknown);
  }

  auto makeDescriptorArray = [&](uint64_t count) -> Value {
    if (count == 0)
      return null;
    Type descriptor = LLVM::LLVMStructType::getLiteral(
        context, {i8, i8, i16, i32, pointer, pointer, i64});
    return entryAlloca(rewriter, location, descriptor, count,
                       alignof(obelisk_rt_import_input_v1));
  };
  Value inputs = makeDescriptorArray(logicalInputs);
  Value outputs = makeDescriptorArray(logicalOutputs);
  auto writeDescriptor = [&](Value base, uint64_t index,
                             const DPIOperandABI &entry,
                             std::pair<Value, Value> planes) {
    Value address = byteGEP(rewriter, location, base,
                            index * sizeof(obelisk_rt_import_input_v1));
    uint32_t kind =
        entry.fourState ? OBELISK_RT_DBREG_LOGIC : OBELISK_RT_DBREG_BITS;
    storeAt(rewriter, location, address,
            offsetof(obelisk_rt_import_input_v1, kind),
            llvmConstant(rewriter, location, i8, kind), 1);
    storeAt(rewriter, location, address,
            offsetof(obelisk_rt_import_input_v1, flags),
            llvmConstant(rewriter, location, i8,
                         entry.isSigned ? OBELISK_RT_DBREG_SIGNED : 0),
            1);
    storeAt(rewriter, location, address,
            offsetof(obelisk_rt_import_input_v1, reserved),
            llvmConstant(rewriter, location, i16, 0), 2);
    storeAt(rewriter, location, address,
            offsetof(obelisk_rt_import_input_v1, bit_width),
            llvmConstant(rewriter, location, i32, entry.width), 4);
    storeAt(rewriter, location, address,
            offsetof(obelisk_rt_import_input_v1, value), planes.first,
            alignof(const uint64_t *));
    storeAt(rewriter, location, address,
            offsetof(obelisk_rt_import_input_v1, unknown), planes.second,
            alignof(const uint64_t *));
    storeAt(rewriter, location, address,
            offsetof(obelisk_rt_import_input_v1, limb_count),
            llvmConstant(rewriter, location, i64,
                         (uint64_t{entry.width} + 63) / 64),
            alignof(uint64_t));
  };
  for (uint64_t index = 0; index != logicalInputs; ++index)
    writeDescriptor(inputs, index, abi[index], inputPlanes[index]);
  for (uint64_t index = 0; index != logicalOutputs; ++index)
    writeDescriptor(outputs, index, abi[logicalInputs + index],
                    outputPlanes[index]);

  Type siteType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i64, pointer, i64, i32, i32, i64});
  Value site = entryAlloca(rewriter, location, siteType, 1,
                           alignof(obelisk_rt_import_site_v1));
  uint32_t flags =
      (operation.getIsPure() ? OBELISK_RT_IMPORT_PURE : 0u) |
      (operation.getIsContext() ? OBELISK_RT_IMPORT_CONTEXT : 0u) |
      (operation.getIsTask() ? OBELISK_RT_IMPORT_TASK : 0u);
  Value source = null;
  if (!operation.getSourceFile().empty()) {
    uint64_t fileHash = UINT64_C(14695981039346656037);
    for (unsigned char byte : operation.getSourceFile().bytes())
      fileHash = appendHash(fileHash, byte, 1);
    std::string base =
        ("__obelisk_dpi_source_" + Twine(operation.getImportId()) + "_" +
         Twine(operation.getSourceLine()) + "_" +
         Twine(operation.getSourceColumn()) + "_" + Twine(fileHash))
            .str();
    LLVM::GlobalOp global = module.lookupSymbol<LLVM::GlobalOp>(base);
    if (!global) {
      OpBuilder::InsertionGuard guard(rewriter);
      rewriter.setInsertionPointToStart(module.getBody());
      Type array =
          LLVM::LLVMArrayType::get(i8, operation.getSourceFile().size());
      global = LLVM::GlobalOp::create(
          rewriter, location, array, true, LLVM::Linkage::Internal, base,
          rewriter.getStringAttr(operation.getSourceFile()), 1);
    }
    rewriter.setInsertionPoint(operation);
    source = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                       global.getSymName());
  }
  storeAt(rewriter, location, site,
          offsetof(obelisk_rt_import_site_v1, version),
          llvmConstant(rewriter, location, i32, OBELISK_RT_VERSION),
          alignof(uint32_t));
  storeAt(rewriter, location, site,
          offsetof(obelisk_rt_import_site_v1, flags),
          llvmConstant(rewriter, location, i32, flags), 4);
  storeAt(rewriter, location, site,
          offsetof(obelisk_rt_import_site_v1, import_id),
          llvmConstant(rewriter, location, i32, operation.getImportId()), 4);
  storeAt(rewriter, location, site,
          offsetof(obelisk_rt_import_site_v1, reserved),
          llvmConstant(rewriter, location, i32, 0), 4);
  storeAt(rewriter, location, site,
          offsetof(obelisk_rt_import_site_v1, scope_id),
          llvmConstant(rewriter, location, i64, operation.getScopeId()), 8);
  storeAt(rewriter, location, site,
          offsetof(obelisk_rt_import_site_v1, source_file), source,
          alignof(const char *));
  storeAt(
      rewriter, location, site,
      offsetof(obelisk_rt_import_site_v1, source_file_size),
      llvmConstant(rewriter, location, i64, operation.getSourceFile().size()),
      8);
  storeAt(rewriter, location, site,
          offsetof(obelisk_rt_import_site_v1, source_line),
          llvmConstant(rewriter, location, i32, operation.getSourceLine()), 4);
  storeAt(rewriter, location, site,
          offsetof(obelisk_rt_import_site_v1, source_column),
          llvmConstant(rewriter, location, i32, operation.getSourceColumn()),
          4);
  storeAt(rewriter, location, site,
          offsetof(obelisk_rt_import_site_v1, abi_signature),
          llvmConstant(rewriter, location, i64,
                       sim::getDPISignatureHash(signature, logicalInputs)),
          8);

  Value statusBits =
      LLVM::CallOp::create(
          rewriter, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_import_call"),
          ValueRange{operation.getRuntimeContext(), site, inputs,
                     llvmConstant(rewriter, location, i32, logicalInputs),
                     outputs,
                     llvmConstant(rewriter, location, i32, logicalOutputs)})
          .getResult();
  Value status = runtime::RTStatusFromBitsOp::create(
      rewriter, location, runtime::StatusType::get(context), statusBits);

  for (uint64_t index = 0; index != logicalOutputs; ++index) {
    const DPIOperandABI &entry = abi[logicalInputs + index];
    uint64_t paddedWidth = ((uint64_t{entry.width} + 63) / 64) * 64;
    Type paddedType =
        IntegerType::get(context, static_cast<unsigned>(paddedWidth));
    Type resultType = IntegerType::get(context, entry.width);
    auto loadPlane = [&](Value address) -> Value {
      Value value =
          LLVM::LoadOp::create(rewriter, location, paddedType, address, 8);
      if (paddedType != resultType)
        value = arith::TruncIOp::create(rewriter, location, resultType, value);
      return value;
    };
    physicalOutputValues.push_back(loadPlane(outputPlanes[index].first));
    if (entry.fourState)
      physicalOutputValues.push_back(loadPlane(outputPlanes[index].second));
  }
  physicalOutputValues.push_back(status);
  if (physicalOutputValues.size() != operation.getNumResults())
    return operation.emitOpError("has inconsistent physical DPI results");
  rewriter.replaceOp(operation, physicalOutputValues);

  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_import_call", i32,
                           {pointer, pointer, pointer, i32, pointer, i32});
  return success();
}

} // namespace

LogicalResult lowerNativeDPICalls(Operation *root) {
  SmallVector<sim::SimDPICallOp> calls;
  root->walk([&](sim::SimDPICallOp call) { calls.push_back(call); });
  IRRewriter rewriter(root->getContext());
  for (sim::SimDPICallOp call : calls) {
    rewriter.setInsertionPoint(call);
    if (failed(lowerNativeDPICall(call, rewriter)))
      return failure();
  }
  return success();
}

} // namespace obelisk::detail
