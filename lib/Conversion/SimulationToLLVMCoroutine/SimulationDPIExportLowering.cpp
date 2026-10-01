//===- SimulationDPIExportLowering.cpp - Generated DPI export thunks ----===//

#include "SimulationDPILowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <algorithm>
#include <cstdint>

using namespace mlir;

namespace obelisk::detail {
namespace {

bool isVector(const DPIOperandABI &abi) {
  return abi.category == static_cast<uint32_t>(sim::DPIABIKind::BitVector) ||
         abi.category == static_cast<uint32_t>(sim::DPIABIKind::LogicVector);
}

bool isString(const DPIOperandABI &abi) {
  return abi.category == static_cast<uint32_t>(sim::DPIABIKind::String);
}

bool isChandle(const DPIOperandABI &abi) {
  return abi.category == static_cast<uint32_t>(sim::DPIABIKind::Chandle);
}

bool isReal(const DPIOperandABI &abi) {
  return abi.category == static_cast<uint32_t>(sim::DPIABIKind::ShortReal) ||
         abi.category == static_cast<uint32_t>(sim::DPIABIKind::Real);
}

bool isAggregate(const DPIOperandABI &abi) {
  return abi.category ==
         static_cast<uint32_t>(sim::DPIABIKind::UnpackedAggregate);
}

Type cScalarType(MLIRContext *context, const DPIOperandABI &abi) {
  switch (abi.category) {
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
  case 10:
    return Float32Type::get(context);
  case 11:
    return Float64Type::get(context);
  default:
    return {};
  }
}

uint8_t descriptorKind(const DPIOperandABI &abi) {
  if (isString(abi))
    return OBELISK_RT_DBREG_STRING;
  if (isAggregate(abi))
    return OBELISK_RT_DBREG_AGGREGATE;
  if (abi.category == static_cast<uint32_t>(sim::DPIABIKind::ShortReal))
    return OBELISK_RT_DBREG_REAL32;
  if (abi.category == static_cast<uint32_t>(sim::DPIABIKind::Real))
    return OBELISK_RT_DBREG_REAL64;
  return abi.fourState ? OBELISK_RT_DBREG_LOGIC : OBELISK_RT_DBREG_BITS;
}

Type planeType(MLIRContext *context, const DPIOperandABI &abi) {
  if (isReal(abi))
    return cScalarType(context, abi);
  uint64_t width = ((uint64_t{abi.width} + 63) / 64) * 64;
  return IntegerType::get(context, static_cast<unsigned>(width));
}

uint64_t planeBytes(const DPIOperandABI &abi) {
  return isReal(abi) ? abi.width / 8 : ((uint64_t{abi.width} + 63) / 64) * 8;
}

Value zero(OpBuilder &builder, Location location, Type type) {
  return LLVM::ZeroOp::create(builder, location, type);
}

struct ExportSpec {
  Operation *operation;
  Location location;
  std::string symbol;
  StringAttr cIdentifier;
  uint32_t exportID;
  uint64_t abiHash;
  uint32_t inputCount;
  ArrayAttr signature;
  ArrayAttr aggregateLayouts;
  SmallVector<DPIOperandABI> abi;
  llvm::BitVector elidedInputs;
  bool isTask;
};

FailureOr<ExportSpec> getExportSpec(Operation *operation, StringRef symbol) {
  auto identifier =
      operation->getAttrOfType<StringAttr>("simulation.dpi_c_identifier");
  auto exportID =
      operation->getAttrOfType<IntegerAttr>("simulation.dpi_export_id");
  auto signature =
      operation->getAttrOfType<ArrayAttr>("simulation.dpi_abi_signature");
  auto inputs =
      operation->getAttrOfType<IntegerAttr>("simulation.dpi_logical_inputs");
  auto aggregateLayouts =
      operation->getAttrOfType<ArrayAttr>("simulation.dpi_aggregate_layouts");
  if (!identifier || !exportID || !signature || !inputs ||
      exportID.getValue().getActiveBits() > 32 ||
      inputs.getValue().getActiveBits() > 32 ||
      inputs.getValue().getZExtValue() > signature.size())
    return operation->emitError("has invalid DPI export lowering metadata"),
           failure();
  if (!aggregateLayouts) {
    SmallVector<Attribute> empty(signature.size(),
                                 UnitAttr::get(operation->getContext()));
    aggregateLayouts = ArrayAttr::get(operation->getContext(), empty);
  }
  if (aggregateLayouts.size() != signature.size())
    return operation->emitError("has invalid DPI export aggregate metadata"),
           failure();
  SmallVector<DPIOperandABI> abi;
  for (Attribute attribute : signature) {
    FailureOr<DPIOperandABI> parsed = parseDPIOperandABI(attribute, operation);
    if (failed(parsed))
      return failure();
    abi.push_back(*parsed);
  }
  uint32_t inputCount = static_cast<uint32_t>(inputs.getValue().getZExtValue());
  llvm::BitVector elidedInputs(inputCount);
  if (auto elided = operation->getAttrOfType<DenseI64ArrayAttr>(
          sim::metadata::dpiElidedInputs)) {
    for (int64_t index : elided.asArrayRef()) {
      if (index < 0 || static_cast<uint64_t>(index) >= inputCount ||
          elidedInputs.test(index) ||
          abi[index].direction !=
              static_cast<uint32_t>(sim::DPIArgumentDirection::Output))
        return operation->emitError(
                   "has invalid elided DPI output-input metadata"),
               failure();
      elidedInputs.set(index);
    }
  }
  return ExportSpec{operation,
                    operation->getLoc(),
                    symbol.str(),
                    identifier,
                    static_cast<uint32_t>(exportID.getValue().getZExtValue()),
                    sim::getDPISignatureHash(signature, inputCount),
                    inputCount,
                    signature,
                    aggregateLayouts,
                    std::move(abi),
                    std::move(elidedInputs),
                    operation->hasAttr("simulation.dpi_task")};
}

Value descriptorField(OpBuilder &builder, Location location, Value base,
                      Type descriptorType, uint64_t index, uint32_t field) {
  return fieldGEP(builder, location,
                  elementGEP(builder, location, base, descriptorType, index),
                  descriptorType, field);
}

void writeDescriptor(OpBuilder &builder, Location location, Value base,
                     Type descriptorType, uint64_t index,
                     const DPIOperandABI &abi, Value value, Value unknown) {
  Type i8 = builder.getI8Type();
  Type i16 = builder.getI16Type();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  auto store = [&](uint32_t field, Value fieldValue, unsigned alignment) {
    LLVM::StoreOp::create(
        builder, location, fieldValue,
        descriptorField(builder, location, base, descriptorType, index, field),
        alignment);
  };
  store(0, llvmConstant(builder, location, i8, descriptorKind(abi)), 1);
  store(1,
        llvmConstant(builder, location, i8,
                     abi.isSigned ? OBELISK_RT_DBREG_SIGNED : 0),
        1);
  store(2, llvmConstant(builder, location, i16, 0), 2);
  store(3, llvmConstant(builder, location, i32, abi.width), 4);
  store(4, value, 0);
  store(5, unknown, 0);
  store(6,
        llvmConstant(builder, location, i64, (uint64_t{abi.width} + 63) / 64),
        8);
}

Value loadCScalar(OpBuilder &builder, Location location, Value address,
                  const DPIOperandABI &abi) {
  Type type = cScalarType(builder.getContext(), abi);
  // Foreign C pointers are not guaranteed to have the alignment of our
  // internal allocas (and pointer alignment is only four on wasm32).
  return LLVM::LoadOp::create(builder, location, type, address, 1);
}

void zeroCVector(OpBuilder &builder, Location location, Value address,
                 const DPIOperandABI &abi) {
  uint64_t words = (uint64_t{abi.width} + 31) / 32;
  uint64_t bytes = words * (abi.fourState ? 8 : 4);
  LLVM::MemsetOp::create(
      builder, location, address,
      llvmConstant(builder, location, builder.getI8Type(), 0),
      llvmConstant(builder, location, builder.getI64Type(), bytes), false);
}

LogicalResult
materializeCWrapper(ModuleOp module, const ExportSpec &spec,
                    llvm::StringMap<LLVM::LLVMFuncOp> &functions) {
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = spec.location;
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i8 = builder.getI8Type();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  uint32_t outputCount = spec.abi.size() - spec.inputCount;
  StringRef cIdentifier = spec.cIdentifier.getValue();
  if (cIdentifier == "main" || cIdentifier.starts_with("obelisk_rt_") ||
      cIdentifier.starts_with("__obelisk_"))
    return spec.operation->emitError()
           << "DPI export C identifier '" << cIdentifier
           << "' collides with a reserved generated/runtime symbol";
  bool hasResult = outputCount != 0 &&
                   spec.abi[spec.inputCount].direction ==
                       static_cast<uint32_t>(sim::DPIArgumentDirection::Result);
  const DPIOperandABI *resultABI =
      hasResult ? &spec.abi[spec.inputCount] : nullptr;

  Type cResultType = voidType;
  if (spec.isTask)
    cResultType = i32;
  else if (resultABI && !isVector(*resultABI))
    cResultType = isString(*resultABI) || isChandle(*resultABI)
                      ? pointer
                      : cScalarType(context, *resultABI);
  SmallVector<Type> cArguments;
  if (resultABI && isVector(*resultABI))
    cArguments.push_back(pointer);
  for (uint32_t index = 0; index != spec.inputCount; ++index) {
    const DPIOperandABI &abi = spec.abi[index];
    bool byPointer = abi.direction != static_cast<uint32_t>(
                                          sim::DPIArgumentDirection::Input) ||
                     isVector(abi) || isAggregate(abi);
    cArguments.push_back(byPointer || isString(abi) || isChandle(abi)
                             ? pointer
                             : cScalarType(context, abi));
  }
  if (!cResultType)
    return spec.operation->emitError("has an unsupported DPI C result type");
  for (Type type : cArguments)
    if (!type)
      return spec.operation->emitError(
          "has an unsupported DPI C argument type");
  auto cType = LLVM::LLVMFunctionType::get(cResultType, cArguments, false);
  if (auto found = functions.find(spec.cIdentifier.getValue());
      found != functions.end()) {
    LLVM::LLVMFuncOp existing = found->second;
    auto marker = existing->getAttrOfType<IntegerAttr>("obelisk.dpi.export_id");
    auto hash = existing->getAttrOfType<IntegerAttr>("obelisk.dpi.abi_hash");
    auto signature =
        existing->getAttrOfType<ArrayAttr>("obelisk.dpi.abi_signature");
    if (!marker || !hash ||
        static_cast<uint32_t>(marker.getValue().getZExtValue()) !=
            spec.exportID ||
        hash.getValue().getZExtValue() != spec.abiHash ||
        signature != spec.signature || existing.getFunctionType() != cType)
      return spec.operation->emitError()
             << "DPI export C identifier '" << spec.cIdentifier.getValue()
             << "' collides with an incompatible LLVM symbol";
    return success();
  }

  builder.setInsertionPointToStart(module.getBody());
  auto wrapper = LLVM::LLVMFuncOp::create(builder, location,
                                          spec.cIdentifier.getValue(), cType);
  functions.try_emplace(spec.cIdentifier.getValue(), wrapper);
  wrapper->setAttr("obelisk.dpi.export_id",
                   builder.getI32IntegerAttr(spec.exportID));
  wrapper->setAttr("obelisk.dpi.abi_hash",
                   builder.getI64IntegerAttr(spec.abiHash));
  wrapper->setAttr("obelisk.dpi.abi_signature", spec.signature);
  auto setNarrowExtension = [&](bool result, uint32_t index,
                                const DPIOperandABI &abi) {
    Type type = cScalarType(context, abi);
    if (!type || !type.isIntOrIndex() || type.getIntOrFloatBitWidth() >= 32)
      return;
    // svBit and svLogic are unsigned C typedefs regardless of the source SV
    // expression's signedness.  Only byte/shortint use signed C spellings.
    bool cSigned =
        (abi.category == static_cast<uint32_t>(sim::DPIABIKind::Byte) ||
         abi.category == static_cast<uint32_t>(sim::DPIABIKind::ShortInt)) &&
        abi.isSigned;
    StringRef name = cSigned ? LLVM::LLVMDialect::getSExtAttrName()
                             : LLVM::LLVMDialect::getZExtAttrName();
    if (result)
      wrapper.setResultAttr(index, name, builder.getUnitAttr());
    else
      wrapper.setArgAttr(index, name, builder.getUnitAttr());
  };
  if (resultABI && !isVector(*resultABI) && !isString(*resultABI) &&
      !isChandle(*resultABI) && !isReal(*resultABI))
    setNarrowExtension(true, 0, *resultABI);
  uint32_t attributeCursor = resultABI && isVector(*resultABI) ? 1 : 0;
  for (uint32_t index = 0; index != spec.inputCount; ++index) {
    const DPIOperandABI &abi = spec.abi[index];
    if (abi.direction ==
            static_cast<uint32_t>(sim::DPIArgumentDirection::Input) &&
        !isVector(abi) && !isAggregate(abi) && !isString(abi) &&
        !isChandle(abi) && !isReal(abi))
      setNarrowExtension(false, attributeCursor, abi);
    ++attributeCursor;
  }
  Block *entry = wrapper.addEntryBlock(builder);
  Block *copy = new Block;
  Block *cleanup = new Block;
  Block *done = new Block;
  wrapper.getBody().push_back(copy);
  wrapper.getBody().push_back(cleanup);
  wrapper.getBody().push_back(done);
  cleanup->addArgument(i32, location);
  done->addArgument(i32, location);
  builder.setInsertionPointToStart(entry);
  Value null = zero(builder, location, pointer);
  Type descriptorType = LLVM::LLVMStructType::getLiteral(
      context, {i8, i8, builder.getI16Type(), i32, pointer, pointer, i64});
  auto descriptorArray = [&](uint32_t count) -> Value {
    return count == 0
               ? null
               : entryAlloca(builder, location, descriptorType, count, 8);
  };
  Value inputs = descriptorArray(spec.inputCount);
  Value outputs = descriptorArray(outputCount);
  SmallVector<std::pair<Value, Value>> inputPlanes;
  SmallVector<std::pair<Value, Value>> outputPlanes;
  SmallVector<Value> formalArguments;
  SmallVector<Value> aggregatePlans(spec.abi.size());
  SmallVector<Value> aggregateRootHandles;
  Value marshallingStatus = llvmConstant(builder, location, i32, OBELISK_RT_OK);
  auto mergeMarshallingStatus = [&](Value next) {
    Value succeeded = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, marshallingStatus,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    marshallingStatus = arith::SelectOp::create(builder, location, succeeded,
                                                next, marshallingStatus);
  };
  uint32_t cCursor = resultABI && isVector(*resultABI) ? 1 : 0;
  for (uint32_t index = 0; index != spec.inputCount; ++index)
    formalArguments.push_back(entry->getArgument(cCursor++));

  auto aggregateLayout = [&](uint32_t index) {
    return dyn_cast<sim::DPIAggregateABIAttr>(spec.aggregateLayouts[index]);
  };
  auto aggregatePlan = [&](uint32_t index) -> Value {
    if (aggregatePlans[index])
      return aggregatePlans[index];
    sim::DPIAggregateABIAttr layout = aggregateLayout(index);
    ArrayRef<int64_t> words = layout.getLeaves().asArrayRef();
    Value plan = entryAlloca(builder, location, i64, words.size(), 8);
    for (auto [word, constant] : llvm::enumerate(words))
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, i64, static_cast<uint64_t>(constant)),
          byteGEP(builder, location, plan, word * sizeof(uint64_t)), 8);
    aggregatePlans[index] = plan;
    return plan;
  };

  auto makePlanes = [&](const DPIOperandABI &abi) {
    uint64_t bytes = planeBytes(abi);
    bool byteBacked = bytes > sizeof(uint64_t);
    Type type =
        byteBacked
            ? Type(LLVM::LLVMArrayType::get(i64, bytes / sizeof(uint64_t)))
            : planeType(context, abi);
    Value value = entryAlloca(builder, location, type, 1, 8);
    if (byteBacked)
      LLVM::MemsetOp::create(
          builder, location, value, llvmConstant(builder, location, i8, 0),
          llvmConstant(builder, location, i64, bytes), false);
    else
      LLVM::StoreOp::create(builder, location, zero(builder, location, type),
                            value, 8);
    Value unknown = null;
    if (abi.fourState) {
      unknown = entryAlloca(builder, location, type, 1, 8);
      if (byteBacked)
        LLVM::MemsetOp::create(
            builder, location, unknown, llvmConstant(builder, location, i8, 0),
            llvmConstant(builder, location, i64, bytes), false);
      else
        LLVM::StoreOp::create(builder, location, zero(builder, location, type),
                              unknown, 8);
    }
    return std::pair<Value, Value>{value, unknown};
  };
  auto storeInteger = [&](Value address, Value value,
                          const DPIOperandABI &abi) {
    Type type = planeType(context, abi);
    value = castIntegerWidth(builder, location, value, type);
    LLVM::StoreOp::create(builder, location, value, address, 8);
  };

  Value activeContext;
  auto rootAggregate = [&](uint32_t signatureIndex,
                           std::pair<Value, Value> planes) {
    sim::DPIAggregateABIAttr layout = aggregateLayout(signatureIndex);
    if (!layout || layout.getStringCount() == 0)
      return;
    if (!activeContext)
      activeContext =
          LLVM::CallOp::create(
              builder, location, TypeRange{pointer},
              SymbolRefAttr::get(context, "obelisk_rt_v1_dpi_current_context"),
              ValueRange{})
              .getResult();
    Value handle = entryAlloca(builder, location, pointer, 1, 8);
    LLVM::StoreOp::create(builder, location, null, handle, 8);
    Value rootStatus =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_dpi_aggregate_roots_push"),
            ValueRange{
                activeContext, planes.first,
                llvmConstant(builder, location, i64,
                             planeBytes(spec.abi[signatureIndex])),
                llvmConstant(builder, location, i64,
                             spec.abi[signatureIndex].width),
                aggregatePlan(signatureIndex),
                llvmConstant(builder, location, i64, layout.getLeaves().size()),
                handle})
            .getResult();
    mergeMarshallingStatus(rootStatus);
    aggregateRootHandles.push_back(handle);
  };

  // Snapshot every inout before deterministic output initialization.
  for (uint32_t index = 0; index != spec.inputCount; ++index) {
    const DPIOperandABI &abi = spec.abi[index];
    auto planes = makePlanes(abi);
    inputPlanes.push_back(planes);
    bool initialize = abi.direction !=
                      static_cast<uint32_t>(sim::DPIArgumentDirection::Output);
    Value argument = formalArguments[index];
    if (initialize) {
      if (isAggregate(abi)) {
        sim::DPIAggregateABIAttr layout = aggregateLayout(index);
        if (!layout)
          return spec.operation->emitError(
              "has no concrete exported aggregate layout");
        Value activeContext =
            LLVM::CallOp::create(
                builder, location, TypeRange{pointer},
                SymbolRefAttr::get(context,
                                   "obelisk_rt_v1_dpi_current_context"),
                ValueRange{})
                .getResult();
        Value unpackStatus =
            LLVM::CallOp::create(
                builder, location, TypeRange{i32},
                SymbolRefAttr::get(context,
                                   "obelisk_rt_v1_dpi_aggregate_unpack"),
                ValueRange{
                    activeContext, argument,
                    llvmConstant(builder, location, i64, layout.getCSize()),
                    aggregatePlan(index),
                    llvmConstant(builder, location, i64,
                                 layout.getLeaves().size()),
                    planes.first, planes.second,
                    llvmConstant(builder, location, i64, planeBytes(abi)),
                    llvmConstant(builder, location, i64, abi.width),
                    llvmConstant(builder, location, i32, abi.fourState)})
                .getResult();
        mergeMarshallingStatus(unpackStatus);
        rootAggregate(index, planes);
      } else if (isReal(abi)) {
        Value value = abi.direction == static_cast<uint32_t>(
                                           sim::DPIArgumentDirection::Input)
                          ? argument
                          : loadCScalar(builder, location, argument, abi);
        LLVM::StoreOp::create(builder, location, value, planes.first,
                              abi.width / 8);
      } else if (isString(abi) || isChandle(abi)) {
        Value value = argument;
        if (abi.direction !=
            static_cast<uint32_t>(sim::DPIArgumentDirection::Input))
          value = LLVM::LoadOp::create(builder, location, pointer, argument, 1);
        storeInteger(planes.first,
                     LLVM::PtrToIntOp::create(builder, location, i64, value),
                     abi);
      } else if (isVector(abi)) {
        LLVM::CallOp::create(
            builder, location, TypeRange{},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_dpi_export_unpack_vector"),
            ValueRange{argument, planes.first, planes.second,
                       llvmConstant(builder, location, i32, abi.width),
                       llvmConstant(builder, location, i32, abi.fourState)});
      } else {
        Value value = abi.direction == static_cast<uint32_t>(
                                           sim::DPIArgumentDirection::Input)
                          ? argument
                          : loadCScalar(builder, location, argument, abi);
        if (abi.category == static_cast<uint32_t>(sim::DPIABIKind::Bit)) {
          value = arith::AndIOp::create(builder, location, value,
                                        llvmConstant(builder, location, i8, 1));
          storeInteger(planes.first, value, abi);
        } else if (abi.category ==
                   static_cast<uint32_t>(sim::DPIABIKind::Logic)) {
          Value one = llvmConstant(builder, location, i8, 1);
          Value aval = arith::AndIOp::create(builder, location, value, one);
          Value bval = arith::AndIOp::create(
              builder, location,
              arith::ShRUIOp::create(builder, location, value, one), one);
          storeInteger(planes.first,
                       arith::XOrIOp::create(builder, location, aval, bval),
                       abi);
          storeInteger(planes.second, bval, abi);
        } else {
          storeInteger(planes.first, value, abi);
        }
      }
    }
    writeDescriptor(builder, location, inputs, descriptorType, index, abi,
                    planes.first, planes.second);
  }
  for (uint32_t index = 0; index != outputCount; ++index) {
    auto planes = makePlanes(spec.abi[spec.inputCount + index]);
    outputPlanes.push_back(planes);
    writeDescriptor(builder, location, outputs, descriptorType, index,
                    spec.abi[spec.inputCount + index], planes.first,
                    planes.second);
  }

  for (uint32_t index = 0; index != outputCount; ++index)
    if (isAggregate(spec.abi[spec.inputCount + index]))
      rootAggregate(spec.inputCount + index, outputPlanes[index]);

  Value resultSlot;
  Value vectorResult =
      resultABI && isVector(*resultABI) ? entry->getArgument(0) : Value{};
  if (resultABI && !isVector(*resultABI)) {
    resultSlot = entryAlloca(builder, location, cResultType, 1, 8);
    LLVM::StoreOp::create(builder, location,
                          zero(builder, location, cResultType), resultSlot, 8);
  }
  if (vectorResult)
    zeroCVector(builder, location, vectorResult, *resultABI);
  for (uint32_t index = 0; index != spec.inputCount; ++index) {
    const DPIOperandABI &abi = spec.abi[index];
    if (abi.direction ==
        static_cast<uint32_t>(sim::DPIArgumentDirection::Input))
      continue;
    Value target = formalArguments[index];
    if (isVector(abi))
      zeroCVector(builder, location, target, abi);
    else if (isAggregate(abi)) {
      sim::DPIAggregateABIAttr layout = aggregateLayout(index);
      LLVM::MemsetOp::create(
          builder, location, target, llvmConstant(builder, location, i8, 0),
          llvmConstant(builder, location, i64, layout.getCSize()), false);
    } else
      LLVM::StoreOp::create(builder, location,
                            zero(builder, location,
                                 isString(abi) || isChandle(abi)
                                     ? pointer
                                     : cScalarType(context, abi)),
                            target, 1);
  }

  auto status = LLVM::CallOp::create(
      builder, location, TypeRange{i32},
      SymbolRefAttr::get(context, "obelisk_rt_v1_export_call_guarded"),
      ValueRange{marshallingStatus,
                 llvmConstant(builder, location, i32, spec.exportID),
                 llvmConstant(builder, location, i64, spec.abiHash), inputs,
                 llvmConstant(builder, location, i32, spec.inputCount), outputs,
                 llvmConstant(builder, location, i32, outputCount)});
  Value succeeded = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, status.getResult(),
      llvmConstant(builder, location, i32, OBELISK_RT_OK));
  LLVM::CondBrOp::create(builder, location, succeeded, copy, ValueRange{},
                         cleanup, ValueRange{status.getResult()});

  builder.setInsertionPointToStart(copy);
  auto writeCValue = [&](uint32_t outputIndex, const DPIOperandABI &abi,
                         Value target) {
    if (isAggregate(abi)) {
      uint32_t signatureIndex = spec.inputCount + outputIndex;
      sim::DPIAggregateABIAttr layout = aggregateLayout(signatureIndex);
      if (!layout)
        return;
      SmallVector<Value> arguments{
          outputPlanes[outputIndex].first,
          outputPlanes[outputIndex].second,
          llvmConstant(builder, location, i64, planeBytes(abi)),
          llvmConstant(builder, location, i64, abi.width),
          llvmConstant(builder, location, i32, abi.fourState),
          target,
          llvmConstant(builder, location, i64, layout.getCSize())};
      StringRef function;
      if (layout.getStringCount() != 0) {
        function = "obelisk_rt_v1_dpi_aggregate_export_pack";
        arguments.append(
            {aggregatePlan(signatureIndex),
             llvmConstant(builder, location, i64, layout.getLeaves().size()),
             llvmConstant(builder, location, i32, outputIndex)});
      } else {
        function = "obelisk_rt_v1_dpi_aggregate_pack";
        arguments.append(
            {llvmConstant(builder, location, i64, layout.getCSize()),
             aggregatePlan(signatureIndex),
             llvmConstant(builder, location, i64, layout.getLeaves().size())});
      }
      Value packStatus =
          LLVM::CallOp::create(builder, location, TypeRange{i32},
                               SymbolRefAttr::get(context, function), arguments)
              .getResult();
      mergeMarshallingStatus(packStatus);
      return;
    }
    if (isVector(abi)) {
      LLVM::CallOp::create(
          builder, location, TypeRange{},
          SymbolRefAttr::get(context, "obelisk_rt_v1_dpi_export_pack_vector"),
          ValueRange{target, outputPlanes[outputIndex].first,
                     outputPlanes[outputIndex].second,
                     llvmConstant(builder, location, i32, abi.width),
                     llvmConstant(builder, location, i32, abi.fourState)});
      return;
    }
    if (isString(abi)) {
      Value text =
          LLVM::CallOp::create(
              builder, location, TypeRange{pointer},
              SymbolRefAttr::get(context, "obelisk_rt_v1_export_string"),
              llvmConstant(builder, location, i32, outputIndex))
              .getResult();
      LLVM::StoreOp::create(builder, location, text, target, 1);
      return;
    }
    Value value =
        LLVM::LoadOp::create(builder, location, planeType(context, abi),
                             outputPlanes[outputIndex].first, 8);
    if (isChandle(abi)) {
      LLVM::StoreOp::create(
          builder, location,
          LLVM::IntToPtrOp::create(
              builder, location, pointer,
              castIntegerWidth(builder, location, value, i64)),
          target, 1);
      return;
    }
    if (isReal(abi)) {
      LLVM::StoreOp::create(builder, location, value, target, 1);
      return;
    }
    Type scalar = cScalarType(context, abi);
    Value scalarValue = castIntegerWidth(builder, location, value, scalar);
    if (abi.category == 1) {
      Value unknown =
          LLVM::LoadOp::create(builder, location, planeType(context, abi),
                               outputPlanes[outputIndex].second, 8);
      Value one = llvmConstant(builder, location, i8, 1);
      Value aval = arith::XOrIOp::create(
          builder, location, scalarValue,
          castIntegerWidth(builder, location, unknown, scalar));
      Value bval = arith::AndIOp::create(
          builder, location, castIntegerWidth(builder, location, unknown, i8),
          one);
      scalarValue = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, aval, one),
          arith::ShLIOp::create(builder, location, bval, one));
    }
    LLVM::StoreOp::create(builder, location, scalarValue, target, 1);
  };

  uint32_t outputCursor = 0;
  if (resultABI) {
    Value target = vectorResult ? vectorResult : resultSlot;
    writeCValue(outputCursor++, *resultABI, target);
  }
  for (uint32_t index = 0; index != spec.inputCount; ++index) {
    const DPIOperandABI &abi = spec.abi[index];
    if (abi.direction ==
        static_cast<uint32_t>(sim::DPIArgumentDirection::Input))
      continue;
    const DPIOperandABI &outputABI = spec.abi[spec.inputCount + outputCursor];
    writeCValue(outputCursor, outputABI, formalArguments[index]);
    ++outputCursor;
  }
  LLVM::BrOp::create(builder, location, ValueRange{marshallingStatus}, cleanup);

  builder.setInsertionPointToStart(cleanup);
  marshallingStatus = cleanup->getArgument(0);
  for (Value handleSlot : llvm::reverse(aggregateRootHandles)) {
    Value handle =
        LLVM::LoadOp::create(builder, location, pointer, handleSlot, 8);
    Value popStatus = LLVM::CallOp::create(
                          builder, location, TypeRange{i32},
                          SymbolRefAttr::get(
                              context, "obelisk_rt_v1_dpi_aggregate_roots_pop"),
                          ValueRange{activeContext, handle})
                          .getResult();
    mergeMarshallingStatus(popStatus);
  }
  marshallingStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_dpi_export_status"),
          ValueRange{marshallingStatus})
          .getResult();
  LLVM::BrOp::create(builder, location, ValueRange{marshallingStatus}, done);

  builder.setInsertionPointToStart(done);
  Value finalStatus = done->getArgument(0);
  if (spec.isTask) {
    Value disabled = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, finalStatus,
        llvmConstant(builder, location, i32,
                     OBELISK_RT_DPI_DISABLE_UNSUPPORTED));
    LLVM::ReturnOp::create(
        builder, location,
        arith::SelectOp::create(builder, location, disabled,
                                llvmConstant(builder, location, i32, 1),
                                llvmConstant(builder, location, i32, 0)));
  } else if (resultSlot)
    LLVM::ReturnOp::create(
        builder, location,
        LLVM::LoadOp::create(builder, location, cResultType, resultSlot, 8));
  else
    LLVM::ReturnOp::create(builder, location, ValueRange{});

  return success();
}

struct InlinedCall {
  Block *continuation;
  SmallVector<Value> results;
};

Value resolveAggregateElement(Value aggregate, uint32_t index);

Value resolvePhysicalValue(Value value) {
  if (auto extract = value.getDefiningOp<LLVM::ExtractValueOp>()) {
    ArrayRef<int64_t> position = extract.getPosition();
    if (position.size() == 1 && position.front() >= 0)
      if (Value resolved = resolveAggregateElement(
              extract.getContainer(), static_cast<uint32_t>(position.front())))
        return resolved;
  }
  return value;
}

Value resolveAggregateElement(Value aggregate, uint32_t index) {
  while (auto insert = aggregate.getDefiningOp<LLVM::InsertValueOp>()) {
    ArrayRef<int64_t> position = insert.getPosition();
    if (position.size() == 1 && position.front() == index)
      return resolvePhysicalValue(insert.getValue());
    aggregate = insert.getContainer();
  }
  return {};
}

FailureOr<InlinedCall> inlineLLVMCall(OpBuilder &builder, LLVM::CallOp call,
                                      LLVM::LLVMFuncOp callee) {
  if (callee.getBody().empty() ||
      callee.getBody().front().getNumArguments() != call.getNumOperands())
    return call.emitError("cannot inline malformed DPI export body"), failure();

  Block *source = call->getBlock();
  Block *continuation = source->splitBlock(std::next(call->getIterator()));
  unsigned returnCount = 0;
  callee.walk([&](LLVM::ReturnOp) { ++returnCount; });
  if (returnCount == 0)
    return call.emitError("DPI export body has no return"), failure();
  SmallVector<Value> results;
  if (returnCount != 1) {
    for (OpResult result : call.getResults()) {
      BlockArgument replacement =
          continuation->addArgument(result.getType(), result.getLoc());
      result.replaceAllUsesWith(replacement);
      results.push_back(replacement);
    }
  }

  Region &caller = *source->getParent();
  IRMapping mapping;
  for (Block &block : callee.getBody()) {
    Block *clone = new Block;
    caller.push_back(clone);
    mapping.map(&block, clone);
    if (&block == &callee.getBody().front()) {
      for (auto [argument, operand] :
           llvm::zip_equal(block.getArguments(), call.getOperands()))
        mapping.map(argument, operand);
    } else {
      for (BlockArgument argument : block.getArguments())
        mapping.map(argument,
                    clone->addArgument(argument.getType(), argument.getLoc()));
    }
  }
  SmallVector<LLVM::ReturnOp> returns;
  for (Block &block : callee.getBody()) {
    Block *clone = mapping.lookup(&block);
    builder.setInsertionPointToEnd(clone);
    for (Operation &operation : block) {
      Operation *copy = builder.clone(operation, mapping);
      if (auto ret = dyn_cast<LLVM::ReturnOp>(copy))
        returns.push_back(ret);
    }
  }
  if (returns.size() != returnCount)
    return call.emitError("failed to clone every DPI export return"), failure();
  if (returnCount == 1) {
    if (returns.front().getNumOperands() != call.getNumResults())
      return returns.front().emitError(
                 "DPI export body return type does not match its call"),
             failure();
    for (auto [result, replacement] :
         llvm::zip_equal(call.getResults(), returns.front().getOperands())) {
      result.replaceAllUsesWith(replacement);
      results.push_back(replacement);
    }
  }
  for (LLVM::ReturnOp ret : returns) {
    if (ret.getNumOperands() != results.size())
      return ret.emitError(
                 "DPI export body return type does not match its call"),
             failure();
    builder.setInsertionPoint(ret);
    LLVM::BrOp::create(builder, ret.getLoc(),
                       returnCount == 1 ? ValueRange{} : ret.getOperands(),
                       continuation);
    ret.erase();
  }

  builder.setInsertionPoint(call);
  LLVM::BrOp::create(builder, call.getLoc(), ValueRange{},
                     mapping.lookup(&callee.getBody().front()));
  call.erase();
  builder.setInsertionPointToStart(continuation);
  return InlinedCall{continuation, std::move(results)};
}

LogicalResult materializeNativeTaskThunk(ModuleOp module,
                                         LLVM::LLVMFuncOp thunk,
                                         LLVM::LLVMFuncOp activation,
                                         const ExportSpec &spec) {
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = spec.location;
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i8 = builder.getI8Type();
  Type i16 = builder.getI16Type();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  if (!thunk.getBody().empty())
    return thunk.emitError("native DPI exported-task thunk is defined twice");
  if (!spec.isTask)
    return thunk.emitError("task thunk has function export metadata");
  uint32_t outputCount = spec.abi.size() - spec.inputCount;
  Block *entry = thunk.addEntryBlock(builder);
  Block *invoke = new Block;
  Block *invalid = new Block;
  Block *copy = new Block;
  Block *cleanup = new Block;
  thunk.getBody().push_back(invoke);
  thunk.getBody().push_back(invalid);
  thunk.getBody().push_back(copy);
  thunk.getBody().push_back(cleanup);
  builder.setInsertionPointToStart(entry);
  Type descriptorType = LLVM::LLVMStructType::getLiteral(
      context, {i8, i8, i16, i32, pointer, pointer, i64});
  Value inputs = entry->getArgument(1);
  Value outputs = entry->getArgument(3);
  SmallVector<Value> aggregatePlans(spec.abi.size());
  auto aggregateLayout = [&](uint32_t index) {
    return dyn_cast<sim::DPIAggregateABIAttr>(spec.aggregateLayouts[index]);
  };
  auto aggregatePlan = [&](uint32_t index) -> Value {
    if (aggregatePlans[index])
      return aggregatePlans[index];
    sim::DPIAggregateABIAttr layout = aggregateLayout(index);
    if (!layout)
      return {};
    ArrayRef<int64_t> words = layout.getLeaves().asArrayRef();
    Value plan = entryAlloca(builder, location, i64, words.size(), 8);
    for (auto [word, constant] : llvm::enumerate(words))
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, i64, static_cast<uint64_t>(constant)),
          byteGEP(builder, location, plan, word * sizeof(uint64_t)), 8);
    aggregatePlans[index] = plan;
    return plan;
  };
  Value descriptorsMatch = LLVM::AndOp::create(
      builder, location,
      LLVM::ICmpOp::create(
          builder, location, LLVM::ICmpPredicate::eq, entry->getArgument(2),
          llvmConstant(builder, location, i32, spec.inputCount)),
      LLVM::ICmpOp::create(builder, location, LLVM::ICmpPredicate::eq,
                           entry->getArgument(4),
                           llvmConstant(builder, location, i32, outputCount)));
  auto requireEqual = [&](Value actual, Value expected) {
    descriptorsMatch = LLVM::AndOp::create(
        builder, location, descriptorsMatch,
        LLVM::ICmpOp::create(builder, location, LLVM::ICmpPredicate::eq, actual,
                             expected));
  };
  auto validate = [&](Value base, uint32_t index, const DPIOperandABI &abi) {
    requireEqual(LLVM::LoadOp::create(builder, location, i8,
                                      descriptorField(builder, location, base,
                                                      descriptorType, index, 0),
                                      1),
                 llvmConstant(builder, location, i8, descriptorKind(abi)));
    requireEqual(LLVM::LoadOp::create(builder, location, i8,
                                      descriptorField(builder, location, base,
                                                      descriptorType, index, 1),
                                      1),
                 llvmConstant(builder, location, i8,
                              abi.isSigned ? OBELISK_RT_DBREG_SIGNED : 0));
    requireEqual(LLVM::LoadOp::create(builder, location, i16,
                                      descriptorField(builder, location, base,
                                                      descriptorType, index, 2),
                                      2),
                 llvmConstant(builder, location, i16, 0));
    requireEqual(LLVM::LoadOp::create(builder, location, i32,
                                      descriptorField(builder, location, base,
                                                      descriptorType, index, 3),
                                      4),
                 llvmConstant(builder, location, i32, abi.width));
    requireEqual(
        LLVM::LoadOp::create(
            builder, location, i64,
            descriptorField(builder, location, base, descriptorType, index, 6),
            8),
        llvmConstant(builder, location, i64, (uint64_t{abi.width} + 63) / 64));
  };
  for (uint32_t index = 0; index != spec.inputCount; ++index)
    validate(inputs, index, spec.abi[index]);
  for (uint32_t index = 0; index != outputCount; ++index)
    validate(outputs, index, spec.abi[spec.inputCount + index]);
  LLVM::CondBrOp::create(builder, location, descriptorsMatch, invoke, invalid);
  builder.setInsertionPointToStart(invalid);
  LLVM::ReturnOp::create(
      builder, location,
      llvmConstant(builder, location, i32, OBELISK_RT_INVALID_ARGUMENT));

  builder.setInsertionPointToStart(invoke);
  auto plane = [&](Value base, uint32_t index, bool unknown) {
    return LLVM::LoadOp::create(builder, location, pointer,
                                descriptorField(builder, location, base,
                                                descriptorType, index,
                                                unknown ? 5 : 4));
  };
  Value statusSlot = entryAlloca(builder, location, i32, 1, 4);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i32, OBELISK_RT_OK),
                        statusSlot, 4);
  SmallVector<Value> handleSlots;
  for (uint32_t index = 0; index != spec.inputCount; ++index)
    if (spec.abi[index].direction !=
        static_cast<uint32_t>(sim::DPIArgumentDirection::Input)) {
      Value slot = entryAlloca(builder, location, i64, 1, 8);
      LLVM::StoreOp::create(builder, location,
                            llvmConstant(builder, location, i64, UINT64_MAX),
                            slot, 8);
      handleSlots.push_back(slot);
    }
  auto failToCleanup = [&](Value status) {
    Block *next = new Block;
    Block *failed = new Block;
    thunk.getBody().push_back(next);
    thunk.getBody().push_back(failed);
    Value ok = LLVM::ICmpOp::create(
        builder, location, LLVM::ICmpPredicate::eq, status,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    LLVM::CondBrOp::create(builder, location, ok, next, failed);
    builder.setInsertionPointToStart(failed);
    LLVM::StoreOp::create(builder, location, status, statusSlot, 4);
    LLVM::BrOp::create(builder, location, cleanup);
    builder.setInsertionPointToStart(next);
  };
  auto loadFormalPlane = [&](uint32_t inputIndex, const DPIOperandABI &abi,
                             bool unknown, Type expected) {
    Type stored = planeType(context, abi);
    Value value = LLVM::LoadOp::create(builder, location, stored,
                                       plane(inputs, inputIndex, unknown),
                                       isReal(abi) ? abi.width / 8 : 8);
    if (value.getType() != expected)
      value = LLVM::TruncOp::create(builder, location, expected, value);
    return value;
  };
  ArrayRef<Type> activationParameters =
      activation.getFunctionType().getParams();
  SmallVector<Value> arguments{entry->getArgument(0)};
  size_t parameter = 1;
  size_t handle = 0;
  for (uint32_t index = 0; index != spec.inputCount; ++index) {
    const DPIOperandABI &abi = spec.abi[index];
    if (!spec.elidedInputs.test(index)) {
      if (parameter >= activationParameters.size())
        return activation.emitError("has too few exported-task arguments");
      arguments.push_back(loadFormalPlane(index, abi, false,
                                          activationParameters[parameter++]));
      if (abi.fourState) {
        if (parameter >= activationParameters.size())
          return activation.emitError(
              "is missing an exported-task unknown argument");
        arguments.push_back(loadFormalPlane(index, abi, true,
                                            activationParameters[parameter++]));
      }
    }
    if (abi.direction ==
        static_cast<uint32_t>(sim::DPIArgumentDirection::Input))
      continue;
    if (parameter >= activationParameters.size() ||
        activationParameters[parameter] != i64)
      return activation.emitError(
          "is missing an exported-task destination reference");
    Value allocationStatus;
    sim::DPIAggregateABIAttr aggregate =
        isAggregate(abi) ? aggregateLayout(index) : sim::DPIAggregateABIAttr{};
    if (aggregate && aggregate.getStringCount() != 0) {
      allocationStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context,
                                 "obelisk_rt_v1_dpi_aggregate_state_alloc"),
              ValueRange{entry->getArgument(0),
                         llvmConstant(builder, location, i64, abi.width),
                         plane(inputs, index, false),
                         plane(inputs, index, true), aggregatePlan(index),
                         llvmConstant(builder, location, i64,
                                      aggregate.getLeaves().size()),
                         handleSlots[handle]})
              .getResult();
    } else if (isString(abi)) {
      Type slotType =
          LLVM::LLVMStructType::getLiteral(context, {i64, i32, i32});
      Value root = entryAlloca(builder, location, slotType, 1, 8);
      Value record = LLVM::ZeroOp::create(builder, location, slotType);
      record = LLVM::InsertValueOp::create(
          builder, location, record,
          llvmConstant(builder, location, i32,
                       OBELISK_RT_MANAGED_ROOT_KIND_STRING),
          ArrayRef<int64_t>{1});
      LLVM::StoreOp::create(builder, location, record, root, 8);
      allocationStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(
                  context, "obelisk_rt_v1_native_state_alloc_with_typed_roots"),
              ValueRange{
                  entry->getArgument(0),
                  llvmConstant(builder, location, i64, abi.width),
                  plane(inputs, index, false), plane(inputs, index, true), root,
                  llvmConstant(builder, location, i64, 1), handleSlots[handle]})
              .getResult();
    } else {
      allocationStatus =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context, "obelisk_rt_v1_native_state_alloc"),
              ValueRange{entry->getArgument(0),
                         llvmConstant(builder, location, i64, abi.width),
                         plane(inputs, index, false),
                         plane(inputs, index, true), handleSlots[handle]})
              .getResult();
    }
    failToCleanup(allocationStatus);
    Value reference =
        LLVM::LoadOp::create(builder, location, i64, handleSlots[handle], 8);
    Value retain =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_native_state_retain"),
            ValueRange{entry->getArgument(0), reference})
            .getResult();
    failToCleanup(retain);
    arguments.push_back(reference);
    ++parameter;
    ++handle;
  }
  if (parameter + 1 != activationParameters.size() ||
      activationParameters.back() != pointer)
    return activation.emitError(
        "exported-task activation signature disagrees with its DPI ABI");
  Value activationSlot = entryAlloca(builder, location, i64, 1, 8);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, 0), activationSlot,
                        8);
  arguments.push_back(activationSlot);
  Value activated =
      LLVM::CallOp::create(builder, location, TypeRange{i32},
                           SymbolRefAttr::get(context, activation.getSymName()),
                           arguments)
          .getResult();
  failToCleanup(activated);
  Value activationWord =
      LLVM::LoadOp::create(builder, location, i64, activationSlot, 8);
  Value instance =
      LLVM::IntToPtrOp::create(builder, location, pointer, activationWord);
  Value taskStatus =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context, "obelisk_rt_v1_dpi_export_task_run"),
          ValueRange{entry->getArgument(0), instance})
          .getResult();
  LLVM::StoreOp::create(builder, location, taskStatus, statusSlot, 4);
  Value completed = LLVM::ICmpOp::create(
      builder, location, LLVM::ICmpPredicate::eq, taskStatus,
      llvmConstant(builder, location, i32, OBELISK_RT_OK));
  LLVM::CondBrOp::create(builder, location, completed, copy, cleanup);

  builder.setInsertionPointToStart(copy);
  uint32_t output = 0;
  handle = 0;
  auto mergeStatus = [&](Value next) {
    Value current = LLVM::LoadOp::create(builder, location, i32, statusSlot, 4);
    Value ok = LLVM::ICmpOp::create(
        builder, location, LLVM::ICmpPredicate::eq, current,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    LLVM::StoreOp::create(
        builder, location,
        LLVM::SelectOp::create(builder, location, ok, next, current),
        statusSlot, 4);
  };
  for (uint32_t index = 0; index != spec.inputCount; ++index) {
    const DPIOperandABI &abi = spec.abi[index];
    if (abi.direction ==
        static_cast<uint32_t>(sim::DPIArgumentDirection::Input))
      continue;
    Value reference =
        LLVM::LoadOp::create(builder, location, i64, handleSlots[handle++], 8);
    Value valueDestination = plane(outputs, output, false);
    Value loadValue =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context,
                               "obelisk_rt_v1_native_state_load_plane"),
            ValueRange{entry->getArgument(0), valueDestination,
                       llvmConstant(builder, location, i64, 0), reference,
                       llvmConstant(builder, location, i64, abi.width),
                       llvmConstant(builder, location, i32, 0),
                       llvmConstant(builder, location, i32, 0),
                       valueDestination})
            .getResult();
    mergeStatus(loadValue);
    if (abi.fourState) {
      Value unknownDestination = plane(outputs, output, true);
      Value loadUnknown =
          LLVM::CallOp::create(
              builder, location, TypeRange{i32},
              SymbolRefAttr::get(context,
                                 "obelisk_rt_v1_native_state_load_plane"),
              ValueRange{entry->getArgument(0), unknownDestination,
                         llvmConstant(builder, location, i64, 0), reference,
                         llvmConstant(builder, location, i64, abi.width),
                         llvmConstant(builder, location, i32, 1),
                         llvmConstant(builder, location, i32, 0),
                         unknownDestination})
              .getResult();
      mergeStatus(loadUnknown);
    }
    ++output;
  }
  LLVM::BrOp::create(builder, location, cleanup);

  builder.setInsertionPointToStart(cleanup);
  for (Value slot : handleSlots) {
    Block *release = new Block;
    Block *next = new Block;
    thunk.getBody().push_back(release);
    thunk.getBody().push_back(next);
    Value reference = LLVM::LoadOp::create(builder, location, i64, slot, 8);
    Value valid = LLVM::ICmpOp::create(
        builder, location, LLVM::ICmpPredicate::ne, reference,
        llvmConstant(builder, location, i64, UINT64_MAX));
    LLVM::CondBrOp::create(builder, location, valid, release, next);
    builder.setInsertionPointToStart(release);
    Value released =
        LLVM::CallOp::create(
            builder, location, TypeRange{i32},
            SymbolRefAttr::get(context, "obelisk_rt_v1_native_state_release"),
            ValueRange{entry->getArgument(0), reference,
                       llvmConstant(builder, location, i32, 1)})
            .getResult();
    mergeStatus(released);
    LLVM::BrOp::create(builder, location, next);
    builder.setInsertionPointToStart(next);
  }
  LLVM::ReturnOp::create(
      builder, location,
      LLVM::LoadOp::create(builder, location, i32, statusSlot, 4));
  return success();
}

LogicalResult materializeBytecodeTaskThunk(LLVM::LLVMFuncOp thunk,
                                           const ExportSpec &spec) {
  MLIRContext *context = thunk.getContext();
  OpBuilder builder(context);
  Location location = spec.location;
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i8 = builder.getI8Type();
  Type i32 = builder.getI32Type();
  if (!thunk.getBody().empty())
    return thunk.emitError("bytecode DPI exported-task thunk is defined twice");
  Block *entry = thunk.addEntryBlock(builder);
  builder.setInsertionPointToStart(entry);
  Value directions = LLVM::ZeroOp::create(builder, location, pointer);
  Value aggregatePlans = LLVM::ZeroOp::create(builder, location, pointer);
  Value aggregatePlanWords = LLVM::ZeroOp::create(builder, location, pointer);
  if (spec.inputCount != 0) {
    directions = entryAlloca(builder, location, i8, spec.inputCount, 1);
    aggregatePlans =
        entryAlloca(builder, location, pointer, spec.inputCount, 8);
    aggregatePlanWords = entryAlloca(builder, location, builder.getI64Type(),
                                     spec.inputCount, 8);
    for (uint32_t index = 0; index != spec.inputCount; ++index)
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, i8,
                       spec.abi[index].direction |
                           (spec.elidedInputs.test(index) ? 0x80u : 0u)),
          elementGEP(builder, location, directions, i8, index), 1);
    for (uint32_t index = 0; index != spec.inputCount; ++index) {
      Value plan = LLVM::ZeroOp::create(builder, location, pointer);
      uint64_t wordCount = 0;
      auto layout =
          dyn_cast<sim::DPIAggregateABIAttr>(spec.aggregateLayouts[index]);
      if (layout && layout.getStringCount() != 0) {
        ArrayRef<int64_t> words = layout.getLeaves().asArrayRef();
        plan = entryAlloca(builder, location, builder.getI64Type(),
                           words.size(), 8);
        for (auto [word, constant] : llvm::enumerate(words))
          LLVM::StoreOp::create(
              builder, location,
              llvmConstant(builder, location, builder.getI64Type(),
                           static_cast<uint64_t>(constant)),
              byteGEP(builder, location, plan, word * sizeof(uint64_t)), 8);
        wordCount = words.size();
      }
      LLVM::StoreOp::create(
          builder, location, plan,
          elementGEP(builder, location, aggregatePlans, pointer, index), 8);
      LLVM::StoreOp::create(
          builder, location,
          llvmConstant(builder, location, builder.getI64Type(), wordCount),
          elementGEP(builder, location, aggregatePlanWords,
                     builder.getI64Type(), index),
          8);
    }
  }
  Value status =
      LLVM::CallOp::create(
          builder, location, TypeRange{i32},
          SymbolRefAttr::get(context,
                             "obelisk_rt_v1_dpi_export_task_bytecode_run"),
          ValueRange{entry->getArgument(0),
                     llvmConstant(builder, location, i32, spec.exportID),
                     entry->getArgument(1), entry->getArgument(2),
                     entry->getArgument(3), entry->getArgument(4), directions,
                     aggregatePlans, aggregatePlanWords})
          .getResult();
  LLVM::ReturnOp::create(builder, location, status);
  return success();
}

LogicalResult materializeNativeThunk(ModuleOp module, LLVM::LLVMFuncOp thunk,
                                     LLVM::LLVMFuncOp bridge,
                                     LLVM::LLVMFuncOp body,
                                     const ExportSpec &spec) {
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = spec.location;
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i8 = builder.getI8Type();
  Type i16 = builder.getI16Type();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  if (!thunk.getBody().empty())
    return bridge.emitError("native DPI export thunk is defined twice");
  uint32_t outputCount = spec.abi.size() - spec.inputCount;
  ArrayRef<Type> parameters = bridge.getFunctionType().getParams();
  if (parameters.empty() || parameters.front() != pointer)
    return bridge.emitError("native DPI export bridge has no context argument");
  SmallVector<Type> expectedParameters{pointer};
  for (const DPIOperandABI &abi :
       ArrayRef<DPIOperandABI>(spec.abi).take_front(spec.inputCount)) {
    Type type = isReal(abi) ? cScalarType(context, abi)
                            : Type(IntegerType::get(context, abi.width));
    expectedParameters.push_back(type);
    if (abi.fourState)
      expectedParameters.push_back(type);
  }
  if (parameters != ArrayRef<Type>(expectedParameters))
    return bridge.emitError(
        "native DPI export bridge parameters do not match its ABI");
  SmallVector<Type> results;
  Type returnType = bridge.getFunctionType().getReturnType();
  if (auto aggregate = dyn_cast<LLVM::LLVMStructType>(returnType))
    llvm::append_range(results, aggregate.getBody());
  else if (!isa<LLVM::LLVMVoidType>(returnType))
    results.push_back(returnType);
  SmallVector<Type> expectedResults;
  for (const DPIOperandABI &abi :
       ArrayRef<DPIOperandABI>(spec.abi).drop_front(spec.inputCount)) {
    Type type = isReal(abi) ? cScalarType(context, abi)
                            : Type(IntegerType::get(context, abi.width));
    expectedResults.push_back(type);
    if (abi.fourState)
      expectedResults.push_back(type);
  }
  bool returnsStatus =
      results.size() == expectedResults.size() + 1 && results.back() == i32 &&
      ArrayRef<Type>(results).drop_back() == ArrayRef<Type>(expectedResults);
  if (results != expectedResults && !returnsStatus)
    return bridge.emitError(
        "native DPI export bridge results do not match its ABI");

  Block *entry = thunk.addEntryBlock(builder);
  Block *invoke = new Block;
  Block *invalid = new Block;
  thunk.getBody().push_back(invoke);
  thunk.getBody().push_back(invalid);
  builder.setInsertionPointToStart(entry);
  Value inputCountMatches = LLVM::ICmpOp::create(
      builder, location, LLVM::ICmpPredicate::eq, entry->getArgument(2),
      llvmConstant(builder, location, i32, spec.inputCount));
  Value outputCountMatches = LLVM::ICmpOp::create(
      builder, location, LLVM::ICmpPredicate::eq, entry->getArgument(4),
      llvmConstant(builder, location, i32, outputCount));
  Value descriptorsMatch = LLVM::AndOp::create(
      builder, location, inputCountMatches, outputCountMatches);
  Type descriptorType = LLVM::LLVMStructType::getLiteral(
      context, {i8, i8, i16, i32, pointer, pointer, i64});
  Value inputs = entry->getArgument(1);
  Value outputs = entry->getArgument(3);
  auto requireEqual = [&](Value actual, Value expected) {
    Value equal = LLVM::ICmpOp::create(
        builder, location, LLVM::ICmpPredicate::eq, actual, expected);
    descriptorsMatch =
        LLVM::AndOp::create(builder, location, descriptorsMatch, equal);
  };
  auto validate = [&](Value base, uint32_t index, const DPIOperandABI &abi) {
    requireEqual(LLVM::LoadOp::create(builder, location, i8,
                                      descriptorField(builder, location, base,
                                                      descriptorType, index, 0),
                                      1),
                 llvmConstant(builder, location, i8, descriptorKind(abi)));
    requireEqual(LLVM::LoadOp::create(builder, location, i8,
                                      descriptorField(builder, location, base,
                                                      descriptorType, index, 1),
                                      1),
                 llvmConstant(builder, location, i8,
                              abi.isSigned ? OBELISK_RT_DBREG_SIGNED : 0));
    requireEqual(LLVM::LoadOp::create(builder, location, i16,
                                      descriptorField(builder, location, base,
                                                      descriptorType, index, 2),
                                      2),
                 llvmConstant(builder, location, i16, 0));
    requireEqual(LLVM::LoadOp::create(builder, location, i32,
                                      descriptorField(builder, location, base,
                                                      descriptorType, index, 3),
                                      4),
                 llvmConstant(builder, location, i32, abi.width));
    requireEqual(
        LLVM::LoadOp::create(
            builder, location, i64,
            descriptorField(builder, location, base, descriptorType, index, 6),
            8),
        llvmConstant(builder, location, i64, (uint64_t{abi.width} + 63) / 64));
  };
  for (uint32_t index = 0; index != spec.inputCount; ++index)
    validate(inputs, index, spec.abi[index]);
  for (uint32_t index = 0; index != outputCount; ++index)
    validate(outputs, index, spec.abi[spec.inputCount + index]);
  LLVM::CondBrOp::create(builder, location, descriptorsMatch, invoke, invalid);
  builder.setInsertionPointToStart(invalid);
  LLVM::ReturnOp::create(
      builder, location,
      llvmConstant(builder, location, i32, OBELISK_RT_INVALID_ARGUMENT));

  builder.setInsertionPointToStart(invoke);
  auto plane = [&](Value base, uint32_t index, bool unknown) {
    return LLVM::LoadOp::create(builder, location, pointer,
                                descriptorField(builder, location, base,
                                                descriptorType, index,
                                                unknown ? 5 : 4));
  };
  SmallVector<Value> arguments{entry->getArgument(0)};
  SmallVector<std::pair<Value, Value>> inputValues;
  for (uint32_t index = 0; index != spec.inputCount; ++index) {
    const DPIOperandABI &abi = spec.abi[index];
    Type padded = planeType(context, abi);
    Value value = LLVM::LoadOp::create(builder, location, padded,
                                       plane(inputs, index, false), 8);
    Type physical = expectedParameters[arguments.size()];
    if (value.getType() != physical)
      value = LLVM::TruncOp::create(builder, location, physical, value);
    arguments.push_back(value);
    Value unknownValue;
    if (abi.fourState) {
      Value unknown = LLVM::LoadOp::create(builder, location, padded,
                                           plane(inputs, index, true), 8);
      if (unknown.getType() != physical)
        unknown = LLVM::TruncOp::create(builder, location, physical, unknown);
      arguments.push_back(unknown);
      unknownValue = unknown;
    }
    inputValues.push_back({value, unknownValue});
  }
  auto call = LLVM::CallOp::create(
      builder, location,
      isa<LLVM::LLVMVoidType>(returnType) ? TypeRange{} : TypeRange{returnType},
      SymbolRefAttr::get(context, bridge.getSymName()), arguments);
  FailureOr<InlinedCall> inlined = inlineLLVMCall(builder, call, bridge);
  if (failed(inlined))
    return failure();
  SmallVector<LLVM::CallOp> bodyCalls;
  thunk.walk([&](LLVM::CallOp nested) {
    if (nested->getAttrOfType<FlatSymbolRefAttr>("callee") ==
        FlatSymbolRefAttr::get(context, body.getSymName()))
      bodyCalls.push_back(nested);
  });
  if (bodyCalls.size() != 1)
    return bridge.emitError(
        "native DPI export bridge does not call its body exactly once");
  SmallVector<std::pair<unsigned, unsigned>> bodyResultUses;
  for (auto [outerIndex, outerResult] : llvm::enumerate(inlined->results))
    for (auto [bodyIndex, bodyResult] :
         llvm::enumerate(bodyCalls.front().getResults()))
      if (outerResult == bodyResult)
        bodyResultUses.push_back({outerIndex, bodyIndex});
  FailureOr<InlinedCall> bodyInlined =
      inlineLLVMCall(builder, bodyCalls.front(), body);
  if (failed(bodyInlined))
    return failure();
  for (auto [outerIndex, bodyIndex] : bodyResultUses)
    inlined->results[outerIndex] = bodyInlined->results[bodyIndex];
  builder.setInsertionPointToStart(inlined->continuation);
  uint32_t physicalResult = 0;
  auto resultAt = [&](uint32_t index) -> Value {
    if (results.size() == 1)
      return inlined->results[0];
    if (Value inserted = resolveAggregateElement(inlined->results[0], index))
      return inserted;
    return LLVM::ExtractValueOp::create(
        builder, location, results[index], inlined->results[0],
        ArrayRef<int64_t>{static_cast<int64_t>(index)});
  };
  if (returnsStatus) {
    Block *copyResults = new Block;
    Block *bodyFailed = new Block;
    thunk.getBody().push_back(copyResults);
    thunk.getBody().push_back(bodyFailed);
    Value status = resultAt(expectedResults.size());
    Value succeeded = LLVM::ICmpOp::create(
        builder, location, LLVM::ICmpPredicate::eq, status,
        llvmConstant(builder, location, i32, OBELISK_RT_OK));
    LLVM::CondBrOp::create(builder, location, succeeded, copyResults,
                           bodyFailed);
    builder.setInsertionPointToStart(bodyFailed);
    LLVM::ReturnOp::create(builder, location, status);
    builder.setInsertionPointToStart(copyResults);
  }
  auto storePlane = [&](Value value, Value destination,
                        const DPIOperandABI &abi, bool unknown) {
    value = resolvePhysicalValue(value);
    for (uint32_t input = 0; input != spec.inputCount; ++input) {
      const DPIOperandABI &inputABI = spec.abi[input];
      Value candidate =
          unknown ? inputValues[input].second : inputValues[input].first;
      if (!isVector(abi) || !isVector(inputABI) || candidate != value ||
          inputABI.width != abi.width || inputABI.fourState != abi.fourState ||
          inputABI.category != abi.category)
        continue;
      LLVM::MemcpyOp::create(
          builder, location, destination, plane(inputs, input, unknown),
          llvmConstant(builder, location, i64, planeBytes(abi)), false);
      return;
    }
    if (isVector(abi) && value.getDefiningOp<LLVM::ZeroOp>()) {
      LLVM::MemsetOp::create(
          builder, location, destination,
          llvmConstant(builder, location, builder.getI8Type(), 0),
          llvmConstant(builder, location, i64, planeBytes(abi)), false);
      return;
    }
    LLVM::StoreOp::create(builder, location, value, destination, 1);
  };
  for (uint32_t index = 0; index != outputCount; ++index) {
    const DPIOperandABI &abi = spec.abi[spec.inputCount + index];
    Value value = resultAt(physicalResult++);
    storePlane(value, plane(outputs, index, false), abi, false);
    if (abi.fourState)
      storePlane(resultAt(physicalResult++), plane(outputs, index, true), abi,
                 true);
  }
  bool erased;
  do {
    erased = false;
    SmallVector<Operation *> dead;
    thunk.walk([&](Operation *operation) {
      if (operation->use_empty() &&
          isa<LLVM::InsertValueOp, LLVM::ExtractValueOp, LLVM::PoisonOp,
              LLVM::ZeroOp>(operation))
        dead.push_back(operation);
    });
    for (Operation *operation : llvm::reverse(dead)) {
      operation->erase();
      erased = true;
    }
  } while (erased);
  auto eraseDeadPlaneLoad = [](Value value) {
    while (Operation *operation = value.getDefiningOp()) {
      if (!operation->use_empty() ||
          !isa<LLVM::LoadOp, LLVM::TruncOp>(operation))
        break;
      Value source = operation->getOperand(0);
      operation->erase();
      value = source;
    }
  };
  for (auto [value, unknown] : inputValues) {
    eraseDeadPlaneLoad(value);
    if (unknown)
      eraseDeadPlaneLoad(unknown);
  }
  LLVM::ReturnOp::create(builder, location,
                         llvmConstant(builder, location, i32, OBELISK_RT_OK));
  bridge.erase();
  return success();
}

} // namespace

LogicalResult materializeDPIExportWrappers(ModuleOp module) {
  if (!module->hasAttr("simulation.has_dpi_exports"))
    return success();
  SmallVector<sim::SimFuncOp> bridges;
  llvm::StringMap<LLVM::LLVMFuncOp> functions;
  ::obelisk::detail::walkNativeFunctions<sim::SimFuncOp>(
      module, [&](sim::SimFuncOp function) {
        if (function->hasAttr("simulation.dpi_export_bridge"))
          bridges.push_back(function);
      });
  ::obelisk::detail::walkNativeFunctions<LLVM::LLVMFuncOp>(
      module, [&](LLVM::LLVMFuncOp function) {
        functions.try_emplace(function.getSymName(), function);
      });
  llvm::sort(bridges, [](sim::SimFuncOp lhs, sim::SimFuncOp rhs) {
    return lhs.getSymName() < rhs.getSymName();
  });
  bool needsString = false;
  bool needsVector = false;
  bool needsTask = false;
  bool needsAggregate = false;
  for (sim::SimFuncOp bridge : bridges) {
    FailureOr<ExportSpec> spec = getExportSpec(bridge, bridge.getSymName());
    if (failed(spec))
      return failure();
    needsString |= llvm::any_of(spec->abi, isString);
    needsVector |= llvm::any_of(spec->abi, isVector);
    needsAggregate |= llvm::any_of(spec->abi, isAggregate);
    needsTask |= spec->isTask;
    if (failed(materializeCWrapper(module, *spec, functions)))
      return failure();
  }
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = builder.getI32Type();
  Type voidType = LLVM::LLVMVoidType::get(context);
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_export_call_guarded", i32,
      {i32, i32, builder.getI64Type(), pointer, i32, pointer, i32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_export_status", i32,
                           {i32});
  if (needsTask)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_export_task_run", i32,
                             {pointer, pointer});
  if (needsTask)
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_dpi_export_task_bytecode_run", i32,
        {pointer, i32, pointer, i32, pointer, i32, pointer, pointer, pointer});
  if (needsString)
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_export_string", pointer,
                             {i32});
  if (needsVector) {
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_export_unpack_vector",
                             voidType, {pointer, pointer, pointer, i32, i32});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_export_pack_vector",
                             voidType, {pointer, pointer, pointer, i32, i32});
  }
  if (needsAggregate) {
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_current_context",
                             pointer, {});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_aggregate_pack", i32,
                             {pointer, pointer, builder.getI64Type(),
                              builder.getI64Type(), i32, pointer,
                              builder.getI64Type(), builder.getI64Type(),
                              pointer, builder.getI64Type()});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_aggregate_unpack", i32,
                             {pointer, pointer, builder.getI64Type(), pointer,
                              builder.getI64Type(), pointer, pointer,
                              builder.getI64Type(), builder.getI64Type(), i32});
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_dpi_aggregate_roots_push", i32,
        {pointer, pointer, builder.getI64Type(), builder.getI64Type(), pointer,
         builder.getI64Type(), pointer});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_aggregate_roots_pop",
                             i32, {pointer, pointer});
    getOrDeclareLLVMFunction(
        module, "obelisk_rt_v1_dpi_aggregate_export_pack", i32,
        {pointer, pointer, builder.getI64Type(), builder.getI64Type(), i32,
         pointer, builder.getI64Type(), pointer, builder.getI64Type(), i32});
    getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dpi_aggregate_state_alloc",
                             i32,
                             {pointer, builder.getI64Type(), pointer, pointer,
                              pointer, builder.getI64Type(), pointer});
  }
  return success();
}

LogicalResult materializeNativeDPIExportThunks(ModuleOp module) {
  if (!module->hasAttr("simulation.has_dpi_exports"))
    return success();
  SmallVector<LLVM::LLVMFuncOp> thunks;
  SmallVector<LLVM::LLVMFuncOp> bodies;
  llvm::StringMap<LLVM::LLVMFuncOp> functions;
  ::obelisk::detail::walkNativeFunctions<LLVM::LLVMFuncOp>(
      module, [&](LLVM::LLVMFuncOp function) {
        functions.try_emplace(function.getSymName(), function);
        if (function->hasAttr("obelisk.dpi.export_bridge"))
          thunks.push_back(function);
      });
  for (LLVM::LLVMFuncOp thunk : thunks) {
    auto bridgeName =
        thunk->getAttrOfType<StringAttr>("obelisk.dpi.export_bridge");
    auto bodyName = thunk->getAttrOfType<StringAttr>("obelisk.dpi.export_body");
    if (!bridgeName)
      return thunk.emitError("native DPI export has no bridge symbol");
    FailureOr<ExportSpec> spec = getExportSpec(thunk, bridgeName.getValue());
    if (failed(spec))
      return failure();
    if (spec->isTask) {
      std::string activationName =
          (bridgeName.getValue() + ".__obelisk_activate_checked").str();
      auto found = functions.find(activationName);
      if (found == functions.end()) {
        if (failed(materializeBytecodeTaskThunk(thunk, *spec)))
          return failure();
        continue;
      }
      if (failed(
              materializeNativeTaskThunk(module, thunk, found->second, *spec)))
        return failure();
      continue;
    }
    auto bridgeFound =
        bridgeName ? functions.find(bridgeName.getValue()) : functions.end();
    auto bridge = bridgeFound == functions.end() ? LLVM::LLVMFuncOp{}
                                                 : bridgeFound->second;
    if (!bridge)
      return thunk.emitError("native DPI export bridge definition is missing");
    auto bodyFound =
        bodyName ? functions.find(bodyName.getValue()) : functions.end();
    auto body =
        bodyFound == functions.end() ? LLVM::LLVMFuncOp{} : bodyFound->second;
    if (!body)
      return thunk.emitError("native DPI export body definition is missing");
    if (failed(materializeNativeThunk(module, thunk, bridge, body, *spec)))
      return failure();
    bodies.push_back(body);
  }
  llvm::sort(bodies, [](LLVM::LLVMFuncOp lhs, LLVM::LLVMFuncOp rhs) {
    return lhs.getSymName() < rhs.getSymName();
  });
  bodies.erase(std::unique(bodies.begin(), bodies.end(),
                           [](LLVM::LLVMFuncOp lhs, LLVM::LLVMFuncOp rhs) {
                             return lhs.getSymName() == rhs.getSymName();
                           }),
               bodies.end());
  std::optional<SymbolTable::UseRange> uses =
      SymbolTable::getSymbolUses(module);
  if (!uses)
    return success();
  llvm::StringSet<> usedSymbols;
  for (const SymbolTable::SymbolUse &use : *uses)
    usedSymbols.insert(use.getSymbolRef().getRootReference().getValue());
  for (LLVM::LLVMFuncOp body : bodies)
    if (!usedSymbols.contains(body.getSymName()))
      body.erase();
  return success();
}

} // namespace obelisk::detail
