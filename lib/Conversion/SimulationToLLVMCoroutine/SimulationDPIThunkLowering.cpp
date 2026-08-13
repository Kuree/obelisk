//===- SimulationDPIThunkLowering.cpp - Native DPI C thunks ------------===//

#include "SimulationDPILowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/PatternMatch.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>

using namespace mlir;

namespace obelisk::detail {

namespace {

Type dpiScalarType(MLIRContext *context, uint32_t category) {
  switch (category) {
  case 0:
  case 1:
  case 2:
    return IntegerType::get(context, 8);
  case 3:
    return IntegerType::get(context, 16);
  case 4:
    return IntegerType::get(context, 32);
  case 5:
    return IntegerType::get(context, 64);
  default:
    return {};
  }
}

bool isDPIVector(uint32_t category) { return category == 6 || category == 7; }

bool isDPIString(uint32_t category) {
  return category == static_cast<uint32_t>(sim::DPIABIKind::String);
}

bool isDPIChandle(uint32_t category) {
  return category == static_cast<uint32_t>(sim::DPIABIKind::Chandle);
}

uint8_t dpiDescriptorKind(const DPIOperandABI &abi) {
  if (isDPIString(abi.category))
    return OBELISK_RT_DBREG_STRING;
  return abi.fourState ? OBELISK_RT_DBREG_LOGIC : OBELISK_RT_DBREG_BITS;
}

struct DPIWriteback {
  DPIOperandABI abi;
  Value buffer;
  uint64_t outputIndex = 0;
};

struct DPIThunkSpec {
  Operation *operation;
  Location location;
  uint32_t importID;
  StringAttr cIdentifier;
  ArrayAttr abiSignature;
  uint64_t logicalInputs;
  bool isTask;
};

LogicalResult materializeDPIThunk(ModuleOp module, const DPIThunkSpec &spec) {
  MLIRContext *context = module.getContext();
  Location location = spec.location;
  OpBuilder builder(context);
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i8 = builder.getI8Type();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  uint64_t logicalInputs = spec.logicalInputs;
  if (logicalInputs > spec.abiSignature.size())
    return spec.operation->emitError("has invalid DPI thunk operand metadata");
  StringRef cIdentifier = spec.cIdentifier.getValue();
  if (cIdentifier == "main" || cIdentifier.starts_with("obelisk_rt_") ||
      cIdentifier.starts_with("__obelisk_"))
    return spec.operation->emitError()
           << "DPI C identifier '" << cIdentifier
           << "' collides with a reserved generated/runtime symbol";
  uint64_t logicalOutputs = spec.abiSignature.size() - logicalInputs;
  SmallVector<DPIOperandABI> abi;
  for (Attribute attribute : spec.abiSignature) {
    FailureOr<DPIOperandABI> parsed =
        parseDPIOperandABI(attribute, spec.operation);
    if (failed(parsed))
      return failure();
    abi.push_back(*parsed);
  }

  std::string thunkName = ("__obelisk_dpi_thunk_" + Twine(spec.importID)).str();
  if (auto existing = module.lookupSymbol<LLVM::LLVMFuncOp>(thunkName)) {
    auto cIdentifier =
        existing->getAttrOfType<StringAttr>("obelisk.dpi.c_identifier");
    auto signature =
        existing->getAttrOfType<ArrayAttr>("obelisk.dpi.abi_signature");
    if (!cIdentifier || cIdentifier != spec.cIdentifier ||
        signature != spec.abiSignature)
      return spec.operation->emitError()
             << "conflicts with another DPI import using ID " << spec.importID;
    return success();
  }

  builder.setInsertionPointToStart(module.getBody());
  auto callbackType = LLVM::LLVMFunctionType::get(
      i32, {pointer, i32, pointer, i32, pointer, i32, pointer}, false);
  auto thunk =
      LLVM::LLVMFuncOp::create(builder, location, thunkName, callbackType);
  thunk.setLinkage(LLVM::Linkage::Internal);
  thunk->setAttr("obelisk.dpi.import_id",
                 builder.getI32IntegerAttr(spec.importID));
  thunk->setAttr("obelisk.dpi.c_identifier", spec.cIdentifier);
  thunk->setAttr("obelisk.dpi.abi_signature", spec.abiSignature);
  thunk->setAttr("obelisk.dpi.abi_hash",
                 builder.getI64IntegerAttr(sim::getDPISignatureHash(
                     spec.abiSignature, logicalInputs)));
  Block *entry = thunk.addEntryBlock(builder);
  Block *validate = new Block;
  Block *invoke = new Block;
  Block *invalid = new Block;
  thunk.getBody().push_back(validate);
  thunk.getBody().push_back(invoke);
  thunk.getBody().push_back(invalid);
  builder.setInsertionPointToStart(entry);
  Value inputsMatch = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, entry->getArgument(3),
      llvmConstant(builder, location, i32, logicalInputs));
  Value outputsMatch = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, entry->getArgument(5),
      llvmConstant(builder, location, i32, logicalOutputs));
  Value countsMatch =
      arith::AndIOp::create(builder, location, inputsMatch, outputsMatch);
  LLVM::CondBrOp::create(builder, location, countsMatch, validate, invalid);
  builder.setInsertionPointToStart(invalid);
  LLVM::ReturnOp::create(
      builder, location,
      llvmConstant(builder, location, i32, OBELISK_RT_INVALID_ARGUMENT));

  builder.setInsertionPointToStart(validate);
  Value null = LLVM::ZeroOp::create(builder, location, pointer);
  Value inputs = entry->getArgument(2);
  Value outputs = entry->getArgument(4);
  auto descriptorPointer = [&](Value base, uint64_t index, uint64_t offset) {
    return byteGEP(builder, location, base,
                   index * sizeof(obelisk_rt_import_input_v1) + offset);
  };
  Value descriptorsMatch =
      llvmConstant(builder, location, builder.getI1Type(), 1);
  auto requireEqual = [&](Value actual, Value expected) {
    Value equal = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, actual, expected);
    descriptorsMatch =
        arith::AndIOp::create(builder, location, descriptorsMatch, equal);
  };
  auto validateDescriptor = [&](Value base, uint64_t index,
                                const DPIOperandABI &entryABI) {
    requireEqual(
        LLVM::LoadOp::create(
            builder, location, i8,
            descriptorPointer(base, index,
                              offsetof(obelisk_rt_import_input_v1, kind)),
            1),
        llvmConstant(builder, location, i8, dpiDescriptorKind(entryABI)));
    requireEqual(
        LLVM::LoadOp::create(
            builder, location, i8,
            descriptorPointer(base, index,
                              offsetof(obelisk_rt_import_input_v1, flags)),
            1),
        llvmConstant(builder, location, i8,
                     entryABI.isSigned ? OBELISK_RT_DBREG_SIGNED : 0));
    requireEqual(
        LLVM::LoadOp::create(
            builder, location, builder.getI16Type(),
            descriptorPointer(base, index,
                              offsetof(obelisk_rt_import_input_v1, reserved)),
            2),
        llvmConstant(builder, location, builder.getI16Type(), 0));
    requireEqual(
        LLVM::LoadOp::create(
            builder, location, i32,
            descriptorPointer(base, index,
                              offsetof(obelisk_rt_import_input_v1, bit_width)),
            4),
        llvmConstant(builder, location, i32, entryABI.width));
    requireEqual(
        LLVM::LoadOp::create(
            builder, location, i64,
            descriptorPointer(base, index,
                              offsetof(obelisk_rt_import_input_v1, limb_count)),
            8),
        llvmConstant(builder, location, i64,
                     (uint64_t{entryABI.width} + 63) / 64));
  };
  for (uint64_t index = 0; index != logicalInputs; ++index)
    validateDescriptor(inputs, index, abi[index]);
  for (uint64_t index = 0; index != logicalOutputs; ++index)
    validateDescriptor(outputs, index, abi[logicalInputs + index]);
  LLVM::CondBrOp::create(builder, location, descriptorsMatch, invoke, invalid);

  builder.setInsertionPointToStart(invoke);
  auto planePointer = [&](Value base, uint64_t index, bool unknown) -> Value {
    return LLVM::LoadOp::create(
        builder, location, pointer,
        descriptorPointer(base, index,
                          unknown
                              ? offsetof(obelisk_rt_import_input_v1, unknown)
                              : offsetof(obelisk_rt_import_input_v1, value)),
        alignof(const uint64_t *));
  };
  auto readWord = [&](Value plane, uint64_t word) -> Value {
    return LLVM::LoadOp::create(builder, location, i32,
                                byteGEP(builder, location, plane, word * 4), 4);
  };
  auto writeWord = [&](Value plane, uint64_t word, Value value) {
    LLVM::StoreOp::create(builder, location, value,
                          byteGEP(builder, location, plane, word * 4), 4);
  };
  auto readScalar = [&](uint64_t inputIndex,
                        const DPIOperandABI &entryABI) -> Value {
    Value value64 = LLVM::LoadOp::create(
        builder, location, i64, planePointer(inputs, inputIndex, false), 8);
    Type scalar = dpiScalarType(context, entryABI.category);
    Value value = castIntegerWidth(builder, location, value64, scalar);
    if (entryABI.category != 1)
      return value;
    Value unknown64 = LLVM::LoadOp::create(
        builder, location, i64, planePointer(inputs, inputIndex, true), 8);
    Value unknown = castIntegerWidth(builder, location, unknown64, i8);
    Value one = llvmConstant(builder, location, i8, 1);
    Value aval = arith::XOrIOp::create(builder, location, value, unknown);
    Value b = arith::AndIOp::create(builder, location, unknown, one);
    Value shifted = arith::ShLIOp::create(builder, location, b, one);
    return arith::OrIOp::create(
        builder, location, shifted,
        arith::AndIOp::create(builder, location, aval, one));
  };
  auto readOpaqueWord = [&](uint64_t inputIndex) -> Value {
    return LLVM::LoadOp::create(builder, location, i64,
                                planePointer(inputs, inputIndex, false), 8);
  };
  auto readString = [&](uint64_t inputIndex) -> Value {
    Value scratch = entryAlloca(builder, location, i8, 8, 1);
    Value bytes = entryAlloca(builder, location, pointer, 1, 8);
    Value size = entryAlloca(builder, location, i64, 1, 8);
    LLVM::CallOp::create(
        builder, location, TypeRange{i32},
        SymbolRefAttr::get(context, "obelisk_rt_v1_string_view"),
        ValueRange{readOpaqueWord(inputIndex), scratch, bytes, size});
    return LLVM::LoadOp::create(builder, location, pointer, bytes, 8);
  };
  auto readChandle = [&](uint64_t inputIndex) -> Value {
    return LLVM::IntToPtrOp::create(builder, location, pointer,
                                    readOpaqueWord(inputIndex));
  };
  auto makeScalarBuffer = [&](uint64_t inputIndex,
                              const DPIOperandABI &entryABI,
                              bool initialize) -> Value {
    Type scalar = dpiScalarType(context, entryABI.category);
    Value value = initialize ? readScalar(inputIndex, entryABI)
                             : LLVM::ZeroOp::create(builder, location, scalar);
    return makeDPIPlaneStorage(
        builder, location, value,
        std::min<unsigned>(8, scalar.getIntOrFloatBitWidth() / 8));
  };
  auto makeVectorBuffer = [&](uint64_t inputIndex,
                              const DPIOperandABI &entryABI,
                              bool initialize) -> Value {
    uint64_t words = (uint64_t{entryABI.width} + 31) / 32;
    Type element =
        entryABI.category == 7
            ? Type(LLVM::LLVMStructType::getLiteral(context, {i32, i32}))
            : Type(i32);
    Value buffer = entryAlloca(builder, location, element, words, 4);
    Value valuePlane =
        initialize ? planePointer(inputs, inputIndex, false) : null;
    Value unknownPlane = initialize && entryABI.category == 7
                             ? planePointer(inputs, inputIndex, true)
                             : null;
    for (uint64_t word = 0; word != words; ++word) {
      Value value = initialize ? readWord(valuePlane, word)
                               : llvmConstant(builder, location, i32, 0);
      if (entryABI.category == 6) {
        LLVM::StoreOp::create(builder, location, value,
                              byteGEP(builder, location, buffer, word * 4), 4);
        continue;
      }
      Value unknown = initialize ? readWord(unknownPlane, word)
                                 : llvmConstant(builder, location, i32, 0);
      Value aval = arith::XOrIOp::create(builder, location, value, unknown);
      LLVM::StoreOp::create(builder, location, aval,
                            byteGEP(builder, location, buffer, word * 8), 4);
      LLVM::StoreOp::create(builder, location, unknown,
                            byteGEP(builder, location, buffer, word * 8 + 4),
                            4);
    }
    return buffer;
  };
  auto makePointerBuffer = [&](uint64_t inputIndex,
                               const DPIOperandABI &entryABI,
                               bool initialize) -> Value {
    Value value = null;
    if (initialize)
      value = isDPIString(entryABI.category) ? readString(inputIndex)
                                             : readChandle(inputIndex);
    return makeDPIPlaneStorage(builder, location, value, 8);
  };

  SmallVector<Value> cArguments;
  SmallVector<DPIWriteback> writebacks;
  bool hasFunctionResult =
      !spec.isTask && logicalOutputs != 0 &&
      abi[logicalInputs].direction ==
          static_cast<uint32_t>(sim::DPIArgumentDirection::Result);
  uint64_t outputCursor = hasFunctionResult ? 1 : 0;
  Value packedFunctionResult;
  if (hasFunctionResult) {
    const DPIOperandABI &resultABI = abi[logicalInputs];
    if (isDPIVector(resultABI.category)) {
      packedFunctionResult = makeVectorBuffer(0, resultABI, false);
      cArguments.push_back(packedFunctionResult);
      writebacks.push_back({resultABI, packedFunctionResult, 0});
    }
  }
  for (uint64_t index = 0; index != logicalInputs; ++index) {
    const DPIOperandABI &entryABI = abi[index];
    if (entryABI.direction == 0) {
      if (isDPIString(entryABI.category))
        cArguments.push_back(readString(index));
      else if (isDPIChandle(entryABI.category))
        cArguments.push_back(readChandle(index));
      else
        cArguments.push_back(isDPIVector(entryABI.category)
                                 ? makeVectorBuffer(index, entryABI, true)
                                 : readScalar(index, entryABI));
      continue;
    }
    if (entryABI.direction != 1 && entryABI.direction != 2)
      return spec.operation->emitError(
          "DPI thunk has an unsupported formal direction");
    bool initialize = entryABI.direction == 2;
    Value buffer =
        isDPIString(entryABI.category) || isDPIChandle(entryABI.category)
            ? makePointerBuffer(index, entryABI, initialize)
        : isDPIVector(entryABI.category)
            ? makeVectorBuffer(index, entryABI, initialize)
            : makeScalarBuffer(index, entryABI, initialize);
    cArguments.push_back(buffer);
    if (outputCursor >= logicalOutputs)
      return spec.operation->emitError(
          "DPI thunk has too few copy-out results");
    writebacks.push_back({entryABI, buffer, outputCursor++});
  }
  if (outputCursor != logicalOutputs)
    return spec.operation->emitError("DPI thunk has excess copy-out results");

  Type cResultType = voidType;
  if (spec.isTask)
    cResultType = i32;
  else if (hasFunctionResult && (isDPIString(abi[logicalInputs].category) ||
                                 isDPIChandle(abi[logicalInputs].category)))
    cResultType = pointer;
  else if (hasFunctionResult && !isDPIVector(abi[logicalInputs].category)) {
    cResultType = dpiScalarType(context, abi[logicalInputs].category);
    if (!cResultType)
      return spec.operation->emitError(
          "DPI function has an invalid result category");
  }
  SmallVector<Type> cArgumentTypes;
  llvm::transform(cArguments, std::back_inserter(cArgumentTypes),
                  [](Value value) { return value.getType(); });
  auto expectedCType =
      LLVM::LLVMFunctionType::get(cResultType, cArgumentTypes, false);
  LLVM::LLVMFuncOp cFunction =
      module.lookupSymbol<LLVM::LLVMFuncOp>(spec.cIdentifier.getValue());
  if (cFunction && cFunction.getFunctionType() != expectedCType)
    return spec.operation->emitError()
           << "DPI C identifier '" << spec.cIdentifier.getValue()
           << "' collides with an LLVM declaration of incompatible type";
  if (!cFunction)
    cFunction = getOrDeclareLLVMFunction(module, spec.cIdentifier.getValue(),
                                         cResultType, cArgumentTypes);
  Value callbackStatus = llvmConstant(builder, location, i32, OBELISK_RT_OK);
  auto mergeStatus = [&](Value next) {
    Value succeeded = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, callbackStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    callbackStatus = arith::SelectOp::create(builder, location, succeeded, next,
                                             callbackStatus);
  };

  // String copies allocate managed objects. Keep every completed copy-out in
  // one precise root range until all string results have been materialized.
  // This matters when a call has multiple output/inout strings: a later copy
  // may collect after an earlier copy has produced its managed word.
  SmallVector<Value> stringOutputSlots(logicalOutputs);
  uint64_t stringOutputCount = 0;
  for (uint64_t index = 0; index != logicalOutputs; ++index)
    if (isDPIString(abi[logicalInputs + index].category))
      ++stringOutputCount;
  Value stringRootRecord;
  Value stringRootLane;
  if (stringOutputCount != 0) {
    Value slots = entryAlloca(builder, location, i64, stringOutputCount, 8);
    uint64_t cursor = 0;
    for (uint64_t index = 0; index != logicalOutputs; ++index) {
      if (!isDPIString(abi[logicalInputs + index].category))
        continue;
      Value slot = byteGEP(builder, location, slots, cursor++ * 8);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, 0), slot, 8);
      stringOutputSlots[index] = slot;
    }
    Type rootType =
        LLVM::LLVMStructType::getLiteral(context, {pointer, i64, pointer, i64});
    stringRootRecord = entryAlloca(builder, location, rootType, 1, 8);
    LLVM::StoreOp::create(builder, location,
                          LLVM::ZeroOp::create(builder, location, rootType),
                          stringRootRecord, 8);
    stringRootLane =
        LLVM::CallOp::create(
            builder, location, TypeRange{pointer},
            SymbolRefAttr::get(context, "obelisk_rt_v1_gc_current_lane"),
            entry->getArgument(0))
            .getResult();
    Value pushStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_gc_managed_root_range_push"),
            ValueRange{stringRootLane, stringRootRecord, slots,
                       llvmConstant(builder, location, i64, stringOutputCount)})
            .getResult();
    mergeStatus(pushStatus);
  }
  SmallVector<Type> callResults;
  if (!isa<LLVM::LLVMVoidType>(cResultType))
    callResults.push_back(cResultType);
  auto call = LLVM::CallOp::create(builder, location, callResults,
                                   SymbolRefAttr::get(cFunction), cArguments);

  if (hasFunctionResult && !isDPIVector(abi[logicalInputs].category)) {
    Value result = call.getResult();
    const DPIOperandABI &resultABI = abi[logicalInputs];
    Value outputValue = planePointer(outputs, 0, false);
    if (isDPIString(resultABI.category)) {
      Value status =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, "obelisk_rt_v1_dpi_string_copy"),
              ValueRange{entry->getArgument(0), result, stringOutputSlots[0]})
              .getResult();
      mergeStatus(status);
    } else if (isDPIChandle(resultABI.category)) {
      LLVM::StoreOp::create(
          builder, location,
          LLVM::PtrToIntOp::create(builder, location, i64, result), outputValue,
          8);
    } else if (resultABI.category == 1) {
      Value one = llvmConstant(builder, location, i8, 1);
      Value aval = arith::AndIOp::create(builder, location, result, one);
      Value shifted = arith::ShRUIOp::create(builder, location, result, one);
      Value bval = arith::AndIOp::create(builder, location, shifted, one);
      Value internal = arith::XOrIOp::create(builder, location, aval, bval);
      LLVM::StoreOp::create(builder, location,
                            castIntegerWidth(builder, location, internal, i64),
                            outputValue, 8);
      LLVM::StoreOp::create(builder, location,
                            castIntegerWidth(builder, location, bval, i64),
                            planePointer(outputs, 0, true), 8);
    } else {
      LLVM::StoreOp::create(builder, location,
                            castIntegerWidth(builder, location, result, i64),
                            outputValue, 8);
    }
  }

  for (const DPIWriteback &writeback : writebacks) {
    Value outputValue = planePointer(outputs, writeback.outputIndex, false);
    if (isDPIString(writeback.abi.category)) {
      Value string =
          LLVM::LoadOp::create(builder, location, pointer, writeback.buffer, 8);
      Value status =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, "obelisk_rt_v1_dpi_string_copy"),
              ValueRange{entry->getArgument(0), string,
                         stringOutputSlots[writeback.outputIndex]})
              .getResult();
      mergeStatus(status);
      continue;
    }
    if (isDPIChandle(writeback.abi.category)) {
      Value value =
          LLVM::LoadOp::create(builder, location, pointer, writeback.buffer, 8);
      LLVM::StoreOp::create(
          builder, location,
          LLVM::PtrToIntOp::create(builder, location, i64, value), outputValue,
          8);
      continue;
    }
    if (!isDPIVector(writeback.abi.category)) {
      Type scalar = dpiScalarType(context, writeback.abi.category);
      Value value = LLVM::LoadOp::create(
          builder, location, scalar, writeback.buffer,
          std::min<unsigned>(8, scalar.getIntOrFloatBitWidth() / 8));
      if (writeback.abi.category == 1) {
        Value one = llvmConstant(builder, location, i8, 1);
        Value aval = arith::AndIOp::create(builder, location, value, one);
        Value shifted = arith::ShRUIOp::create(builder, location, value, one);
        Value bval = arith::AndIOp::create(builder, location, shifted, one);
        Value internal = arith::XOrIOp::create(builder, location, aval, bval);
        LLVM::StoreOp::create(
            builder, location,
            castIntegerWidth(builder, location, internal, i64), outputValue, 8);
        LLVM::StoreOp::create(
            builder, location, castIntegerWidth(builder, location, bval, i64),
            planePointer(outputs, writeback.outputIndex, true), 8);
      } else {
        LLVM::StoreOp::create(builder, location,
                              castIntegerWidth(builder, location, value, i64),
                              outputValue, 8);
      }
      continue;
    }
    uint64_t words = (uint64_t{writeback.abi.width} + 31) / 32;
    Value outputUnknown =
        writeback.abi.category == 7
            ? planePointer(outputs, writeback.outputIndex, true)
            : null;
    for (uint64_t word = 0; word != words; ++word) {
      if (writeback.abi.category == 6) {
        Value value = LLVM::LoadOp::create(
            builder, location, i32,
            byteGEP(builder, location, writeback.buffer, word * 4), 4);
        writeWord(outputValue, word, value);
        continue;
      }
      Value aval = LLVM::LoadOp::create(
          builder, location, i32,
          byteGEP(builder, location, writeback.buffer, word * 8), 4);
      Value bval = LLVM::LoadOp::create(
          builder, location, i32,
          byteGEP(builder, location, writeback.buffer, word * 8 + 4), 4);
      writeWord(outputValue, word,
                arith::XOrIOp::create(builder, location, aval, bval));
      writeWord(outputUnknown, word, bval);
    }
  }

  if (spec.isTask) {
    Value nonzero = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne, call.getResult(),
        llvmConstant(builder, location, i32, 0));
    Value taskStatus = arith::SelectOp::create(
        builder, location, nonzero,
        llvmConstant(builder, location, i32,
                     OBELISK_RT_DPI_DISABLE_UNSUPPORTED),
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    mergeStatus(taskStatus);
  }
  if (stringOutputCount != 0) {
    for (uint64_t index = 0; index != logicalOutputs; ++index) {
      if (!stringOutputSlots[index])
        continue;
      Value value = LLVM::LoadOp::create(builder, location, i64,
                                         stringOutputSlots[index], 8);
      LLVM::StoreOp::create(builder, location, value,
                            planePointer(outputs, index, false), 8);
    }
    Value popStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_gc_managed_root_range_pop"),
            ValueRange{stringRootLane, stringRootRecord})
            .getResult();
    mergeStatus(popStatus);
  }
  LLVM::ReturnOp::create(builder, location, callbackStatus);
  if (llvm::any_of(abi, [](const DPIOperandABI &entry) {
        return isDPIString(entry.category);
      })) {
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_view", i32,
                             {i64, pointer, pointer, pointer});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_string_copy", i32,
                             {pointer, pointer, pointer});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_current_lane", pointer,
                             {pointer});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_managed_root_range_push",
                             i32, {pointer, pointer, pointer, i64});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_managed_root_range_pop",
                             i32, {pointer, pointer});
  }
  return success();
}

} // namespace

LogicalResult materializeDPIThunks(ModuleOp module) {
  SmallVector<sim::SimCodeUnitDeclOp> declarations;
  module.walk([&](sim::SimCodeUnitDeclOp declaration) {
    if (declaration->hasAttr("obelisk_sim.dpi_import"))
      declarations.push_back(declaration);
  });
  for (sim::SimCodeUnitDeclOp declaration : declarations) {
    auto importID =
        declaration->getAttrOfType<IntegerAttr>("obelisk_sim.dpi_import_id");
    auto cIdentifier =
        declaration->getAttrOfType<StringAttr>("obelisk_sim.dpi_c_identifier");
    auto signature =
        declaration->getAttrOfType<ArrayAttr>("obelisk_sim.dpi_abi_signature");
    auto logicalInputs = declaration->getAttrOfType<IntegerAttr>(
        "obelisk_sim.dpi_logical_inputs");
    if (!importID || !cIdentifier || !signature || !logicalInputs)
      return declaration.emitOpError(
          "has incomplete DPI import declaration metadata");
    DPIThunkSpec spec{
        declaration.getOperation(),
        declaration.getLoc(),
        static_cast<uint32_t>(importID.getValue().getZExtValue()),
        cIdentifier,
        signature,
        logicalInputs.getValue().getZExtValue(),
        declaration->hasAttr("obelisk_sim.dpi_task"),
    };
    if (failed(materializeDPIThunk(module, spec)))
      return failure();
  }

  SmallVector<sim::SimDPICallOp> sites;
  module.walk([&](sim::SimDPICallOp site) { sites.push_back(site); });
  for (sim::SimDPICallOp site : sites) {
    auto logicalCountAttr =
        site->getAttrOfType<IntegerAttr>("obelisk.dpi.logical_operand_count");
    DPIThunkSpec spec{
        site.getOperation(),
        site.getLoc(),
        site.getImportId(),
        site.getCIdentifierAttr(),
        site.getAbiSignature(),
        logicalCountAttr ? logicalCountAttr.getValue().getZExtValue()
                         : site.getArguments().size(),
        site.getIsTask(),
    };
    if (failed(materializeDPIThunk(module, spec)))
      return failure();
  }
  return success();
}

} // namespace obelisk::detail
