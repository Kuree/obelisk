//===- SimulationDPILowering.cpp - Native DPI lowering ----------------===//

#include "SimulationDPILowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

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

uint64_t appendHash(uint64_t hash, uint64_t value, unsigned bytes) {
  return obelisk_stable_hash_append_uint_le(hash, value, bytes);
}

bool isDPIReal(const DPIOperandABI &abi) {
  return abi.category == static_cast<uint32_t>(sim::DPIABIKind::ShortReal) ||
         abi.category == static_cast<uint32_t>(sim::DPIABIKind::Real);
}

bool isDPIOpenArray(const DPIOperandABI &abi) {
  return abi.category == static_cast<uint32_t>(sim::DPIABIKind::OpenArray);
}

bool isDPIAggregate(const DPIOperandABI &abi) {
  return abi.category ==
         static_cast<uint32_t>(sim::DPIABIKind::UnpackedAggregate);
}

uint64_t getDPIOpenArrayElementSize(sim::DPIOpenArrayABIAttr layout) {
  return layout.getElementCSize();
}

Type getDPIRealType(MLIRContext *context, const DPIOperandABI &abi) {
  if (abi.category == static_cast<uint32_t>(sim::DPIABIKind::ShortReal))
    return Float32Type::get(context);
  if (abi.category == static_cast<uint32_t>(sim::DPIABIKind::Real))
    return Float64Type::get(context);
  return {};
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
  ArrayAttr openLayouts =
      operation->getAttrOfType<ArrayAttr>("obelisk.dpi.open_array_layouts");
  if (!openLayouts) {
    SmallVector<Attribute> empty(signature.size(), rewriter.getUnitAttr());
    openLayouts = rewriter.getArrayAttr(empty);
  }
  if (openLayouts.size() != signature.size())
    return operation.emitOpError("has no complete DPI open-array inventory");
  ArrayAttr aggregateLayouts =
      operation->getAttrOfType<ArrayAttr>("obelisk.dpi.aggregate_layouts");
  if (!aggregateLayouts) {
    SmallVector<Attribute> empty(signature.size(), rewriter.getUnitAttr());
    aggregateLayouts = rewriter.getArrayAttr(empty);
  }
  if (aggregateLayouts.size() != signature.size())
    return operation.emitOpError("has no complete DPI aggregate inventory");

  struct OpenArrayStorage {
    sim::DPIOpenArrayABIAttr layout;
    Value descriptor;
    Value data;
    Value plan;
    Value unpackedRanges;
    Value flatValue;
    Value flatUnknown;
    Value managedHandle;
    Value container;
    Value elementCountValue;
    Value dataSizeValue;
    Value capacityValue;
    Value planeSizeValue;
    Value totalWidthValue;
    uint64_t elementCount = 0;
    uint64_t elementSize = 0;
    uint64_t dataSize = 0;
    uint32_t unpackedDimensions = 0;
    bool dynamic = false;
    bool recursive = false;
    Value recursiveStorage;
    Value shape;
  };
  struct AggregateStorage {
    sim::DPIAggregateABIAttr layout;
    Value data;
    Value plan;
    Value value;
    Value unknown;
    uint64_t capacity = 0;
  };
  SmallVector<std::optional<OpenArrayStorage>> openInputs(logicalInputs);
  SmallVector<std::optional<AggregateStorage>> aggregateInputs(logicalInputs);

  SmallVector<Value> physicalInputs(operation.getArguments());
  size_t physicalInput = 0;
  SmallVector<std::pair<Value, Value>> inputPlanes;
  inputPlanes.reserve(logicalInputs);
  Value marshallingStatus = llvmConstant(rewriter, location, i32, 0);
  auto preserveFirstStatus = [&](Value next) {
    Value ok = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::eq, marshallingStatus,
        llvmConstant(rewriter, location, i32, 0));
    marshallingStatus = arith::SelectOp::create(rewriter, location, ok, next,
                                                marshallingStatus);
  };
  for (uint64_t index = 0; index != logicalInputs; ++index) {
    if (physicalInput >= physicalInputs.size())
      return operation.emitOpError("is missing a physical DPI input plane");
    Value value = physicalInputs[physicalInput++];
    Value unknown;
    auto openLayout =
        isDPIOpenArray(abi[index])
            ? dyn_cast<sim::DPIOpenArrayABIAttr>(openLayouts[index])
            : sim::DPIOpenArrayABIAttr{};
    bool inputFourState =
        openLayout ? openLayout.getTransportFourState() : abi[index].fourState;
    if (inputFourState) {
      if (physicalInput >= physicalInputs.size())
        return operation.emitOpError("is missing a physical DPI unknown plane");
      unknown = physicalInputs[physicalInput++];
    }
    if (isDPIOpenArray(abi[index])) {
      auto layout = openLayout;
      if (!layout)
        return operation.emitOpError("has malformed open-array metadata");
      bool dynamic = layout.getStorage() == 1;
      bool recursive = layout.getStorage() == 2;
      if (dynamic && unknown)
        return operation.emitOpError(
            "has an unexpected dynamic open-array unknown plane");
      Value managedHandle;
      Value container;
      Value valueStorage;
      Value unknownStorage = null;
      uint64_t elementCount = 1;
      ArrayRef<int64_t> ranges = layout.getRanges().asArrayRef();
      ArrayRef<int64_t> sourceRanges = layout.getSourceRanges().asArrayRef();
      if (!dynamic) {
        value = padDPIPlane(rewriter, location, value,
                            static_cast<uint32_t>(layout.getTransportWidth()));
        if (!value)
          return operation.emitOpError("has malformed open-array value plane");
        if (unknown) {
          unknown =
              padDPIPlane(rewriter, location, unknown,
                          static_cast<uint32_t>(layout.getTransportWidth()));
          if (!unknown)
            return operation.emitOpError(
                "has malformed open-array unknown plane");
        }
        valueStorage = makeDPIPlaneStorage(rewriter, location, value);
        unknownStorage =
            unknown ? makeDPIPlaneStorage(rewriter, location, unknown) : null;
        for (size_t range = 0; !recursive && range != ranges.size();
             range += 2) {
          uint64_t extent = static_cast<uint64_t>(
                                std::max(ranges[range], ranges[range + 1]) -
                                std::min(ranges[range], ranges[range + 1])) +
                            1;
          if (extent == 0 || elementCount > UINT64_MAX / extent)
            return operation.emitOpError("open-array extent overflows");
          elementCount *= extent;
        }
      }
      uint64_t elementSize = getDPIOpenArrayElementSize(layout);
      if (elementSize == 0 ||
          (!dynamic && !recursive && elementCount > UINT64_MAX / elementSize) ||
          (!dynamic && !recursive && layout.getElementStringCount() != 0 &&
           elementCount > UINT64_MAX / layout.getElementStringCount()))
        return operation.emitOpError(
            "open-array canonical storage is not representable");
      auto c32 = [&](uint32_t constant) {
        return llvmConstant(rewriter, location, i32, constant);
      };
      auto c64 = [&](uint64_t constant) {
        return llvmConstant(rewriter, location, i64, constant);
      };
      uint64_t dataSize = dynamic || recursive ? 0 : elementCount * elementSize;
      uint64_t stringCount =
          dynamic || recursive ? 0
                               : elementCount * layout.getElementStringCount();
      if (!dynamic && !recursive && stringCount > (UINT64_MAX - dataSize) / 8)
        return operation.emitOpError(
            "open-array string scratch storage is not representable");
      uint64_t capacity = dataSize + stringCount * 8;
      ArrayRef<int64_t> planWords = layout.getElementLeaves().asArrayRef();
      Value plan = entryAlloca(rewriter, location, i64, planWords.size(), 8);
      for (auto [word, constant] : llvm::enumerate(planWords))
        LLVM::StoreOp::create(
            rewriter, location,
            llvmConstant(rewriter, location, i64,
                         static_cast<uint64_t>(constant)),
            byteGEP(rewriter, location, plan, word * sizeof(uint64_t)), 8);
      if (recursive) {
        if (abi[index].direction ==
                static_cast<uint32_t>(sim::DPIArgumentDirection::Output) ||
            layout.getShapePlan().empty())
          return operation.emitOpError(
              "recursive DPI open arrays cannot be output-only");
        ArrayRef<int64_t> shapeWords = layout.getShapePlan().asArrayRef();
        Value shape =
            entryAlloca(rewriter, location, i64, shapeWords.size(), 8);
        for (auto [word, constant] : llvm::enumerate(shapeWords))
          LLVM::StoreOp::create(
              rewriter, location,
              llvmConstant(rewriter, location, i64,
                           static_cast<uint64_t>(constant)),
              byteGEP(rewriter, location, shape, word * sizeof(uint64_t)), 8);
        Type openType = LLVM::LLVMStructType::getLiteral(
            context,
            {i32, i32, i32, i32, i32, i32, i64, pointer, i64, pointer, i64});
        Type storageType =
            LLVM::LLVMStructType::getLiteral(context, {openType, pointer});
        Value prepared = entryAlloca(rewriter, location, storageType, 1, 8);
        LLVM::MemsetOp::create(rewriter, location, prepared,
                               llvmConstant(rewriter, location, i8, 0), c64(72),
                               false);
        Value prepareStatus =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i32},
                SymbolRefAttr::get(
                    context, "obelisk_rt_v1_dpi_open_array_prepare_recursive"),
                ValueRange{
                    valueStorage, unknownStorage,
                    c64((layout.getTransportWidth() + 7) / 8),
                    c64(layout.getTransportWidth()),
                    c32(layout.getTransportFourState()),
                    c32(abi[index].direction !=
                        static_cast<uint32_t>(
                            sim::DPIArgumentDirection::Input)),
                    c32(static_cast<uint32_t>(layout.getElementKind())),
                    c32(layout.getElementWidth()), c32(layout.getFourState()),
                    c32(static_cast<uint32_t>(layout.getPackedLeft())),
                    c32(static_cast<uint32_t>(layout.getPackedRight())),
                    c64(layout.getElementCSize()),
                    c32(layout.getElementCAlignment()),
                    c64(layout.getElementStringCount()), plan,
                    c64(planWords.size()), shape,
                    c32(static_cast<uint32_t>(ranges.size() / 2)), prepared})
                .getResult();
        preserveFirstStatus(prepareStatus);
        Value descriptor =
            fieldGEP(rewriter, location, prepared, storageType, 0);
        inputPlanes.emplace_back(descriptor, null);
        OpenArrayStorage storage;
        storage.layout = layout;
        storage.descriptor = descriptor;
        storage.plan = plan;
        storage.shape = shape;
        storage.flatValue = valueStorage;
        storage.flatUnknown = unknownStorage;
        storage.recursive = true;
        storage.recursiveStorage = prepared;
        openInputs[index] = storage;
        continue;
      }
      uint32_t unpackedDimensions = dynamic ? 0 : ranges.size() / 2;
      Value unpackedRanges =
          unpackedDimensions
              ? entryAlloca(rewriter, location, i64, sourceRanges.size(), 8)
              : null;
      if (!dynamic)
        for (auto [word, constant] : llvm::enumerate(sourceRanges))
          LLVM::StoreOp::create(rewriter, location,
                                llvmConstant(rewriter, location, i64,
                                             static_cast<uint64_t>(constant)),
                                byteGEP(rewriter, location, unpackedRanges,
                                        word * sizeof(uint64_t)),
                                8);
      Value elementCountValue;
      Value totalWidthValue;
      Value planeSizeValue;
      Value dataSizeValue;
      Value capacityValue;
      Value data;
      if (dynamic) {
        managedHandle = value;
        container = managedObjectPointer(rewriter, location, managedHandle);
        elementCountValue =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i64},
                SymbolRefAttr::get(context, "obelisk_rt_v1_container_size"),
                container)
                .getResult();
        totalWidthValue =
            arith::MulIOp::create(rewriter, location, elementCountValue,
                                  c64(layout.getElementWidth()));
        planeSizeValue = arith::DivUIOp::create(
            rewriter, location,
            arith::AddIOp::create(rewriter, location, totalWidthValue, c64(7)),
            c64(8));
        dataSizeValue = arith::MulIOp::create(
            rewriter, location, elementCountValue, c64(elementSize));
        capacityValue = arith::AddIOp::create(
            rewriter, location, dataSizeValue,
            arith::MulIOp::create(
                rewriter, location, elementCountValue,
                c64(layout.getElementStringCount() * uint64_t{8})));
        valueStorage = LLVM::AllocaOp::create(
            rewriter, location, pointer, i8,
            arith::AddIOp::create(rewriter, location, planeSizeValue, c64(1)),
            8);
        if (layout.getFourState())
          unknownStorage = LLVM::AllocaOp::create(
              rewriter, location, pointer, i8,
              arith::AddIOp::create(rewriter, location, planeSizeValue, c64(1)),
              8);
        data = LLVM::AllocaOp::create(
            rewriter, location, pointer, i8,
            arith::AddIOp::create(rewriter, location, capacityValue, c64(1)),
            8);
      } else {
        elementCountValue = c64(elementCount);
        totalWidthValue = c64(layout.getTransportWidth());
        planeSizeValue = c64((layout.getTransportWidth() + 7) / 8);
        dataSizeValue = c64(dataSize);
        capacityValue = c64(capacity);
        data =
            entryAlloca(rewriter, location, i8, std::max<uint64_t>(capacity, 1),
                        layout.getElementCAlignment());
      }
      if (abi[index].direction !=
          static_cast<uint32_t>(sim::DPIArgumentDirection::Output)) {
        if (dynamic) {
          Value exportStatus =
              LLVM::CallOp::create(
                  rewriter, location, TypeRange{i32},
                  SymbolRefAttr::get(context,
                                     "obelisk_rt_v1_container_export_fixed"),
                  ValueRange{container, valueStorage, unknownStorage,
                             planeSizeValue, totalWidthValue,
                             c32(layout.getFourState()),
                             c64(layout.getElementWidth()), elementCountValue})
                  .getResult();
          preserveFirstStatus(exportStatus);
        }
        Value packStatus =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i32},
                SymbolRefAttr::get(
                    context, "obelisk_rt_v1_dpi_open_array_aggregate_pack"),
                ValueRange{valueStorage, unknownStorage, planeSizeValue,
                           totalWidthValue, c32(layout.getFourState()),
                           c32(layout.getElementWidth()), elementCountValue,
                           c32(0), unpackedRanges, c32(unpackedDimensions),
                           data, dataSizeValue, capacityValue,
                           c64(layout.getElementCSize()),
                           c64(layout.getElementStringCount()), plan,
                           c64(planWords.size())})
                .getResult();
        preserveFirstStatus(packStatus);
      } else {
        LLVM::MemsetOp::create(rewriter, location, data,
                               llvmConstant(rewriter, location, i8, 0),
                               capacityValue, false);
      }

      Type rangeType =
          LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64, i32, i32});
      uint64_t dimensions = dynamic ? 1 : ranges.size() / 2;
      Value rangeStorage =
          dimensions ? entryAlloca(rewriter, location, rangeType, dimensions, 8)
                     : null;
      uint64_t stride = elementSize;
      for (uint64_t dimension = dimensions; dimension-- != 0;) {
        Value address =
            elementGEP(rewriter, location, rangeStorage, rangeType, dimension);
        LLVM::StoreOp::create(
            rewriter, location,
            dynamic ? c32(0)
                    : c32(static_cast<uint32_t>(ranges[dimension * 2])),
            fieldGEP(rewriter, location, address, rangeType, 0), 4);
        Value right =
            dynamic ? Value(arith::TruncIOp::create(
                          rewriter, location, i32,
                          arith::SubIOp::create(rewriter, location,
                                                elementCountValue, c64(1))))
                    : c32(static_cast<uint32_t>(ranges[dimension * 2 + 1]));
        LLVM::StoreOp::create(
            rewriter, location, right,
            fieldGEP(rewriter, location, address, rangeType, 1), 4);
        LLVM::StoreOp::create(
            rewriter, location, c64(stride),
            fieldGEP(rewriter, location, address, rangeType, 2), 8);
        Value dimensionFlags = c32(0);
        if (dynamic) {
          Value empty = arith::CmpIOp::create(rewriter, location,
                                              arith::CmpIPredicate::eq,
                                              elementCountValue, c64(0));
          dimensionFlags =
              arith::SelectOp::create(rewriter, location, empty,
                                      c32(OBELISK_RT_DPI_DIMENSION_RUNTIME |
                                          OBELISK_RT_DPI_DIMENSION_EMPTY),
                                      c32(OBELISK_RT_DPI_DIMENSION_RUNTIME));
        }
        LLVM::StoreOp::create(
            rewriter, location, dimensionFlags,
            fieldGEP(rewriter, location, address, rangeType, 3), 4);
        LLVM::StoreOp::create(
            rewriter, location, c32(0),
            fieldGEP(rewriter, location, address, rangeType, 4), 4);
        if (!dynamic) {
          uint64_t extent =
              static_cast<uint64_t>(
                  std::max(ranges[dimension * 2], ranges[dimension * 2 + 1]) -
                  std::min(ranges[dimension * 2], ranges[dimension * 2 + 1])) +
              1;
          stride *= extent;
        }
      }
      Type openType = LLVM::LLVMStructType::getLiteral(
          context,
          {i32, i32, i32, i32, i32, i32, i64, pointer, i64, pointer, i64});
      Value descriptor = entryAlloca(rewriter, location, openType, 1, 8);
      auto storeOpen = [&](uint32_t field, Value fieldValue,
                           unsigned alignment) {
        LLVM::StoreOp::create(
            rewriter, location, fieldValue,
            fieldGEP(rewriter, location, descriptor, openType, field),
            alignment);
      };
      uint32_t elementKind = static_cast<uint32_t>(layout.getElementKind());
      bool packed =
          elementKind <= static_cast<uint32_t>(sim::DPIABIKind::LogicVector);
      bool cLayout = true;
      uint32_t flags =
          (abi[index].direction != 0 ? OBELISK_RT_DPI_OPEN_ARRAY_WRITABLE : 0) |
          (cLayout ? OBELISK_RT_DPI_OPEN_ARRAY_C_LAYOUT : 0) |
          (packed ? OBELISK_RT_DPI_OPEN_ARRAY_PACKED : 0) |
          (layout.getFourState() ? OBELISK_RT_DPI_OPEN_ARRAY_FOUR_STATE : 0);
      Value flagValue = c32(flags);
      if (dynamic) {
        Value empty =
            arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::eq,
                                  elementCountValue, c64(0));
        flagValue = arith::SelectOp::create(
            rewriter, location, empty,
            c32(flags | OBELISK_RT_DPI_OPEN_ARRAY_EMPTY), flagValue);
      }
      storeOpen(0, c32(OBELISK_RT_DPI_OPEN_ARRAY_MAGIC), 4);
      storeOpen(1, flagValue, 4);
      storeOpen(2, c32(dimensions), 4);
      storeOpen(3, c32(layout.getElementWidth()), 4);
      storeOpen(4, c32(static_cast<uint32_t>(layout.getPackedLeft())), 4);
      storeOpen(5, c32(static_cast<uint32_t>(layout.getPackedRight())), 4);
      storeOpen(6, c64(elementSize), 8);
      Value descriptorData = data;
      if (dynamic) {
        Value empty =
            arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::eq,
                                  elementCountValue, c64(0));
        descriptorData =
            arith::SelectOp::create(rewriter, location, empty, null, data);
      }
      storeOpen(7, descriptorData, 8);
      storeOpen(8, dataSizeValue, 8);
      storeOpen(9, rangeStorage, 8);
      storeOpen(10, c64(0), 8);
      inputPlanes.emplace_back(descriptor, null);
      OpenArrayStorage storage;
      storage.layout = layout;
      storage.descriptor = descriptor;
      storage.data = data;
      storage.plan = plan;
      storage.unpackedRanges = unpackedRanges;
      storage.flatValue = valueStorage;
      storage.flatUnknown = unknownStorage;
      storage.managedHandle = managedHandle;
      storage.container = container;
      storage.elementCountValue = elementCountValue;
      storage.dataSizeValue = dataSizeValue;
      storage.capacityValue = capacityValue;
      storage.planeSizeValue = planeSizeValue;
      storage.totalWidthValue = totalWidthValue;
      storage.elementCount = elementCount;
      storage.elementSize = elementSize;
      storage.dataSize = dataSize;
      storage.unpackedDimensions = unpackedDimensions;
      storage.dynamic = dynamic;
      openInputs[index] = storage;
      continue;
    }
    if (isDPIAggregate(abi[index])) {
      auto layout = dyn_cast<sim::DPIAggregateABIAttr>(aggregateLayouts[index]);
      if (!layout)
        return operation.emitOpError("has malformed aggregate metadata");
      value = padDPIPlane(rewriter, location, value, abi[index].width);
      if (!value)
        return operation.emitOpError("has malformed aggregate value plane");
      if (unknown) {
        unknown = padDPIPlane(rewriter, location, unknown, abi[index].width);
        if (!unknown)
          return operation.emitOpError("has malformed aggregate unknown plane");
      }
      if (layout.getStringCount() >
          (UINT64_MAX - layout.getCSize()) / uint64_t{8})
        return operation.emitOpError(
            "has unrepresentable aggregate string scratch storage");
      uint64_t capacity =
          layout.getCSize() + layout.getStringCount() * uint64_t{8};
      Value data =
          entryAlloca(rewriter, location, i8, capacity, layout.getCAlignment());
      ArrayRef<int64_t> planWords = layout.getLeaves().asArrayRef();
      Value plan = entryAlloca(rewriter, location, i64, planWords.size(), 8);
      for (auto [word, constant] : llvm::enumerate(planWords))
        LLVM::StoreOp::create(
            rewriter, location,
            llvmConstant(rewriter, location, i64,
                         static_cast<uint64_t>(constant)),
            byteGEP(rewriter, location, plan, word * sizeof(uint64_t)), 8);
      Value valueStorage = makeDPIPlaneStorage(rewriter, location, value);
      Value unknownStorage =
          unknown ? makeDPIPlaneStorage(rewriter, location, unknown) : null;
      auto c32 = [&](uint32_t constant) {
        return llvmConstant(rewriter, location, i32, constant);
      };
      auto c64 = [&](uint64_t constant) {
        return llvmConstant(rewriter, location, i64, constant);
      };
      if (abi[index].direction !=
          static_cast<uint32_t>(sim::DPIArgumentDirection::Output)) {
        Value packStatus =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i32},
                SymbolRefAttr::get(context, "obelisk_rt_v1_dpi_aggregate_pack"),
                ValueRange{valueStorage, unknownStorage,
                           c64((uint64_t{abi[index].width} + 7) / 8),
                           c64(abi[index].width), c32(abi[index].fourState),
                           data, c64(layout.getCSize()), c64(capacity), plan,
                           c64(planWords.size())})
                .getResult();
        preserveFirstStatus(packStatus);
      } else {
        LLVM::MemsetOp::create(rewriter, location, data,
                               llvmConstant(rewriter, location, i8, 0),
                               c64(capacity), false);
      }
      inputPlanes.emplace_back(data, null);
      aggregateInputs[index] = AggregateStorage{
          layout, data, plan, valueStorage, unknownStorage, capacity};
      continue;
    }
    if (isDPIReal(abi[index])) {
      Type realType = getDPIRealType(context, abi[index]);
      if (value.getType() != realType || unknown)
        return operation.emitOpError("has a malformed DPI floating input");
      inputPlanes.emplace_back(
          makeDPIPlaneStorage(rewriter, location, value, 8), null);
      continue;
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
  SmallVector<int64_t> openOutputSources(logicalOutputs, -1);
  SmallVector<int64_t> aggregateOutputSources(logicalOutputs, -1);
  uint64_t copyOutCursor =
      !operation.getIsTask() && logicalOutputs != 0 &&
              abi[logicalInputs].direction ==
                  static_cast<uint32_t>(sim::DPIArgumentDirection::Result)
          ? 1
          : 0;
  for (uint64_t input = 0; input != logicalInputs; ++input) {
    if (abi[input].direction ==
        static_cast<uint32_t>(sim::DPIArgumentDirection::Input))
      continue;
    if (copyOutCursor >= logicalOutputs)
      return operation.emitOpError("has malformed DPI copy-out ordering");
    if (isDPIOpenArray(abi[input]))
      openOutputSources[copyOutCursor] = static_cast<int64_t>(input);
    if (isDPIAggregate(abi[input]))
      aggregateOutputSources[copyOutCursor] = static_cast<int64_t>(input);
    ++copyOutCursor;
  }
  for (uint64_t index = logicalInputs; index != abi.size(); ++index) {
    uint64_t outputIndex = index - logicalInputs;
    if (isDPIOpenArray(abi[index])) {
      int64_t source = openOutputSources[outputIndex];
      if (source < 0 || !openInputs[source])
        return operation.emitOpError(
            "open-array copy-out has no matching formal descriptor");
      outputPlanes.emplace_back(openInputs[source]->descriptor, null);
      continue;
    }
    if (isDPIAggregate(abi[index])) {
      int64_t source = aggregateOutputSources[outputIndex];
      if (source < 0 || !aggregateInputs[source])
        return operation.emitOpError(
            "aggregate copy-out has no matching formal storage");
      outputPlanes.emplace_back(aggregateInputs[source]->data, null);
      continue;
    }
    if (isDPIReal(abi[index])) {
      Type realType = getDPIRealType(context, abi[index]);
      outputPlanes.emplace_back(
          makeZeroDPIPlaneStorage(rewriter, location, realType, 8), null);
      continue;
    }
    uint64_t paddedWidth = ((uint64_t{abi[index].width} + 63) / 64) * 64;
    Type valueType =
        IntegerType::get(context, static_cast<unsigned>(paddedWidth));
    Value value = makeZeroDPIPlaneStorage(rewriter, location, valueType);
    Value unknown = abi[index].fourState
                        ? makeZeroDPIPlaneStorage(rewriter, location, valueType)
                        : null;
    outputPlanes.emplace_back(value, unknown);
  }

  Type descriptorType = LLVM::LLVMStructType::getLiteral(
      context, {i8, i8, i16, i32, pointer, pointer, i64});
  auto makeDescriptorArray = [&](uint64_t count) -> Value {
    if (count == 0)
      return null;
    return entryAlloca(rewriter, location, descriptorType, count, 8);
  };
  Value inputs = makeDescriptorArray(logicalInputs);
  Value outputs = makeDescriptorArray(logicalOutputs);
  auto writeDescriptor = [&](Value base, uint64_t index,
                             const DPIOperandABI &entry,
                             std::pair<Value, Value> planes) {
    Value address = elementGEP(rewriter, location, base, descriptorType, index);
    auto storeField = [&](uint32_t field, Value value, unsigned alignment) {
      LLVM::StoreOp::create(
          rewriter, location, value,
          fieldGEP(rewriter, location, address, descriptorType, field),
          alignment);
    };
    uint32_t kind =
        isDPIOpenArray(entry)   ? OBELISK_RT_DBREG_OPEN_ARRAY
        : isDPIAggregate(entry) ? OBELISK_RT_DBREG_AGGREGATE
        : entry.category == static_cast<uint32_t>(sim::DPIABIKind::String)
            ? OBELISK_RT_DBREG_STRING
        : entry.category == static_cast<uint32_t>(sim::DPIABIKind::ShortReal)
            ? OBELISK_RT_DBREG_REAL32
        : entry.category == static_cast<uint32_t>(sim::DPIABIKind::Real)
            ? OBELISK_RT_DBREG_REAL64
        : entry.fourState ? OBELISK_RT_DBREG_LOGIC
                          : OBELISK_RT_DBREG_BITS;
    storeField(0, llvmConstant(rewriter, location, i8, kind), 1);
    storeField(1,
               llvmConstant(rewriter, location, i8,
                            entry.isSigned ? OBELISK_RT_DBREG_SIGNED : 0),
               1);
    storeField(2, llvmConstant(rewriter, location, i16, 0), 2);
    uint32_t descriptorWidth =
        (isDPIOpenArray(entry) || isDPIAggregate(entry)) ? 64 : entry.width;
    storeField(3, llvmConstant(rewriter, location, i32, descriptorWidth), 4);
    storeField(4, planes.first, 0);
    storeField(5, planes.second, 0);
    storeField(6,
               llvmConstant(rewriter, location, i64,
                            (uint64_t{descriptorWidth} + 63) / 64),
               8);
  };
  for (uint64_t index = 0; index != logicalInputs; ++index)
    writeDescriptor(inputs, index, abi[index], inputPlanes[index]);
  for (uint64_t index = 0; index != logicalOutputs; ++index)
    writeDescriptor(outputs, index, abi[logicalInputs + index],
                    outputPlanes[index]);

  Type siteType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i64, pointer, i64, i32, i32, i64});
  Value site = entryAlloca(rewriter, location, siteType, 1, 8);
  uint32_t flags = (operation.getIsPure() ? OBELISK_RT_IMPORT_PURE : 0u) |
                   (operation.getIsContext() ? OBELISK_RT_IMPORT_CONTEXT : 0u) |
                   (operation.getIsTask() ? OBELISK_RT_IMPORT_TASK : 0u);
  Value source = null;
  if (operation.getIsContext() && !operation.getSourceFile().empty()) {
    uint64_t fileHash = OBELISK_STABLE_HASH_OFFSET_BASIS;
    for (unsigned char byte : operation.getSourceFile().bytes())
      fileHash = appendHash(fileHash, byte, 1);
    std::string base =
        ("__obelisk_dpi_source_" + Twine(operation.getImportId()) + "_" +
         Twine(operation.getSourceLine()) + "_" +
         Twine(operation.getSourceColumn()) + "_" + Twine(fileHash))
            .str();
    rewriter.setInsertionPoint(operation);
    source = LLVM::AddressOfOp::create(rewriter, location, pointer, base);
  }
  auto storeSiteField = [&](uint32_t field, Value value, unsigned alignment) {
    LLVM::StoreOp::create(rewriter, location, value,
                          fieldGEP(rewriter, location, site, siteType, field),
                          alignment);
  };
  storeSiteField(0, llvmConstant(rewriter, location, i32, OBELISK_RT_VERSION),
                 4);
  storeSiteField(1, llvmConstant(rewriter, location, i32, flags), 4);
  storeSiteField(
      2, llvmConstant(rewriter, location, i32, operation.getImportId()), 4);
  storeSiteField(3, llvmConstant(rewriter, location, i32, 0), 4);
  storeSiteField(
      4,
      llvmConstant(rewriter, location, i64,
                   operation.getIsContext() ? operation.getScopeId() : 0),
      8);
  storeSiteField(5, source, 8);
  storeSiteField(6,
                 llvmConstant(rewriter, location, i64,
                              operation.getIsContext()
                                  ? operation.getSourceFile().size()
                                  : uint64_t{0}),
                 8);
  storeSiteField(
      7,
      llvmConstant(rewriter, location, i32,
                   operation.getIsContext() ? operation.getSourceLine() : 0),
      4);
  storeSiteField(
      8,
      llvmConstant(rewriter, location, i32,
                   operation.getIsContext() ? operation.getSourceColumn() : 0),
      4);
  storeSiteField(
      9,
      llvmConstant(rewriter, location, i64,
                   llvm::any_of(abi, isDPIOpenArray)
                       ? 0
                       : sim::getDPISignatureHash(signature, logicalInputs)),
      8);

  Value statusBits =
      LLVM::CallOp::create(
          rewriter, location, TypeRange{i32},
          SymbolRefAttr::get(
              context, operation.getIsContext()
                           ? "obelisk_rt_v1_import_call_guarded"
                           : "obelisk_rt_v1_import_call_noncontext_guarded"),
          ValueRange{
              marshallingStatus, operation.getRuntimeContext(), site, inputs,
              llvmConstant(rewriter, location, i32, logicalInputs), outputs,
              llvmConstant(rewriter, location, i32, logicalOutputs)})
          .getResult();
  marshallingStatus = statusBits;

  for (uint64_t index = 0; index != logicalOutputs; ++index) {
    const DPIOperandABI &entry = abi[logicalInputs + index];
    if (isDPIOpenArray(entry)) {
      int64_t source = openOutputSources[index];
      if (source < 0 || !openInputs[source])
        return operation.emitOpError(
            "open-array result has no matching canonical storage");
      const OpenArrayStorage &storage = *openInputs[source];
      auto c32 = [&](uint32_t constant) {
        return llvmConstant(rewriter, location, i32, constant);
      };
      auto c64 = [&](uint64_t constant) {
        return llvmConstant(rewriter, location, i64, constant);
      };
      if (storage.recursive) {
        marshallingStatus =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i32},
                SymbolRefAttr::get(
                    context, "obelisk_rt_v1_dpi_open_array_finish_recursive"),
                ValueRange{marshallingStatus, operation.getRuntimeContext(),
                           storage.recursiveStorage, storage.flatValue,
                           storage.flatUnknown,
                           c64((storage.layout.getTransportWidth() + 7) / 8),
                           c64(storage.layout.getTransportWidth()),
                           c32(storage.layout.getTransportFourState()),
                           c32(storage.layout.getElementWidth()),
                           c32(storage.layout.getFourState()),
                           c64(storage.layout.getElementCSize()), storage.plan,
                           c64(storage.layout.getElementLeaves().size()),
                           storage.shape,
                           c32(static_cast<uint32_t>(
                               storage.layout.getRanges().size() / 2))})
                .getResult();
        uint64_t paddedWidth =
            ((storage.layout.getTransportWidth() + 63) / 64) * 64;
        Type paddedType =
            IntegerType::get(context, static_cast<unsigned>(paddedWidth));
        Type resultType = IntegerType::get(
            context, static_cast<unsigned>(storage.layout.getTransportWidth()));
        auto loadPlane = [&](Value address) -> Value {
          Value loaded =
              LLVM::LoadOp::create(rewriter, location, paddedType, address, 8);
          if (paddedType != resultType)
            loaded =
                arith::TruncIOp::create(rewriter, location, resultType, loaded);
          return loaded;
        };
        physicalOutputValues.push_back(loadPlane(storage.flatValue));
        if (storage.layout.getTransportFourState())
          physicalOutputValues.push_back(loadPlane(storage.flatUnknown));
        continue;
      }
      if (storage.dynamic) {
        Value unpackStatus =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i32},
                SymbolRefAttr::get(
                    context, "obelisk_rt_v1_dpi_open_array_aggregate_unpack"),
                ValueRange{
                    operation.getRuntimeContext(), storage.data,
                    storage.dataSizeValue,
                    c64(storage.layout.getElementCSize()), storage.plan,
                    c64(storage.layout.getElementLeaves().size()),
                    storage.elementCountValue, c32(0), storage.unpackedRanges,
                    c32(storage.unpackedDimensions), storage.flatValue,
                    storage.flatUnknown, storage.planeSizeValue,
                    storage.totalWidthValue, c32(storage.layout.getFourState()),
                    c32(storage.layout.getElementWidth())})
                .getResult();
        preserveFirstStatus(unpackStatus);
        Value aggregateRootSlot;
        if (storage.layout.getElementStringCount() != 0) {
          aggregateRootSlot = entryAlloca(rewriter, location, pointer, 1, 8);
          LLVM::StoreOp::create(rewriter, location, null, aggregateRootSlot, 8);
          Value canRoot = arith::CmpIOp::create(rewriter, location,
                                                arith::CmpIPredicate::eq,
                                                marshallingStatus, c32(0));
          Value guardedContext = arith::SelectOp::create(
              rewriter, location, canRoot, operation.getRuntimeContext(), null);
          Value rootStatus =
              LLVM::CallOp::create(
                  rewriter, location, TypeRange{i32},
                  SymbolRefAttr::get(
                      context,
                      "obelisk_rt_v1_dpi_open_array_aggregate_roots_push"),
                  ValueRange{
                      guardedContext, storage.flatValue, storage.planeSizeValue,
                      storage.totalWidthValue,
                      c32(storage.layout.getElementWidth()),
                      storage.elementCountValue, c32(0), storage.unpackedRanges,
                      c32(storage.unpackedDimensions),
                      c64(storage.layout.getElementCSize()), storage.plan,
                      c64(storage.layout.getElementLeaves().size()),
                      aggregateRootSlot})
                  .getResult();
          preserveFirstStatus(rootStatus);
        }
        Value canImport =
            arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::eq,
                                  marshallingStatus, c32(0));
        Value guardedContainer = arith::SelectOp::create(
            rewriter, location, canImport, storage.container, null);
        Value lane = managedContextAndLane(rewriter, location).second;
        Value importContainerStatus =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i32},
                SymbolRefAttr::get(context,
                                   "obelisk_rt_v1_container_import_fixed"),
                ValueRange{lane, guardedContainer, storage.flatValue,
                           storage.flatUnknown, storage.planeSizeValue,
                           storage.totalWidthValue,
                           c32(storage.layout.getFourState()),
                           c64(storage.layout.getElementWidth()),
                           storage.elementCountValue})
                .getResult();
        preserveFirstStatus(importContainerStatus);
        if (aggregateRootSlot) {
          Value rootHandle = LLVM::LoadOp::create(rewriter, location, pointer,
                                                  aggregateRootSlot, 8);
          Value popStatus =
              LLVM::CallOp::create(
                  rewriter, location, TypeRange{i32},
                  SymbolRefAttr::get(context,
                                     "obelisk_rt_v1_dpi_aggregate_roots_pop"),
                  ValueRange{operation.getRuntimeContext(), rootHandle})
                  .getResult();
          preserveFirstStatus(popStatus);
        }
        physicalOutputValues.push_back(storage.managedHandle);
        continue;
      }
      uint64_t paddedWidth =
          ((storage.layout.getTransportWidth() + 63) / 64) * 64;
      Type paddedType =
          IntegerType::get(context, static_cast<unsigned>(paddedWidth));
      Value valueStorage =
          makeZeroDPIPlaneStorage(rewriter, location, paddedType);
      Value unknownStorage =
          storage.layout.getFourState()
              ? makeZeroDPIPlaneStorage(rewriter, location, paddedType)
              : null;
      Value unpackStatus =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(
                  context, "obelisk_rt_v1_dpi_open_array_aggregate_unpack"),
              ValueRange{
                  operation.getRuntimeContext(), storage.data,
                  c64(storage.dataSize), c64(storage.layout.getElementCSize()),
                  storage.plan, c64(storage.layout.getElementLeaves().size()),
                  c64(storage.elementCount), c32(0), storage.unpackedRanges,
                  c32(storage.unpackedDimensions), valueStorage, unknownStorage,
                  c64((storage.layout.getTransportWidth() + 7) / 8),
                  c64(storage.layout.getTransportWidth()),
                  c32(storage.layout.getFourState()),
                  c32(storage.layout.getElementWidth())})
              .getResult();
      preserveFirstStatus(unpackStatus);
      Type resultType = IntegerType::get(
          context, static_cast<unsigned>(storage.layout.getTransportWidth()));
      auto loadPlane = [&](Value address) -> Value {
        Value loaded =
            LLVM::LoadOp::create(rewriter, location, paddedType, address, 8);
        if (paddedType != resultType)
          loaded =
              arith::TruncIOp::create(rewriter, location, resultType, loaded);
        return loaded;
      };
      physicalOutputValues.push_back(loadPlane(valueStorage));
      if (storage.layout.getFourState())
        physicalOutputValues.push_back(loadPlane(unknownStorage));
      continue;
    }
    if (isDPIAggregate(entry)) {
      int64_t source = aggregateOutputSources[index];
      if (source < 0 || !aggregateInputs[source])
        return operation.emitOpError(
            "aggregate result has no matching C storage");
      const AggregateStorage &storage = *aggregateInputs[source];
      uint64_t paddedWidth = ((uint64_t{entry.width} + 63) / 64) * 64;
      Type paddedType =
          IntegerType::get(context, static_cast<unsigned>(paddedWidth));
      Value valueStorage =
          makeZeroDPIPlaneStorage(rewriter, location, paddedType);
      Value unknownStorage =
          entry.fourState
              ? makeZeroDPIPlaneStorage(rewriter, location, paddedType)
              : null;
      auto c32 = [&](uint32_t constant) {
        return llvmConstant(rewriter, location, i32, constant);
      };
      auto c64 = [&](uint64_t constant) {
        return llvmConstant(rewriter, location, i64, constant);
      };
      Value unpackStatus =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(context, "obelisk_rt_v1_dpi_aggregate_unpack"),
              ValueRange{operation.getRuntimeContext(), storage.data,
                         c64(storage.layout.getCSize()), storage.plan,
                         c64(storage.layout.getLeaves().size()), valueStorage,
                         unknownStorage, c64((uint64_t{entry.width} + 7) / 8),
                         c64(entry.width), c32(entry.fourState)})
              .getResult();
      preserveFirstStatus(unpackStatus);
      Type resultType = IntegerType::get(context, entry.width);
      auto loadPlane = [&](Value address) -> Value {
        Value loaded =
            LLVM::LoadOp::create(rewriter, location, paddedType, address, 8);
        if (paddedType != resultType)
          loaded =
              arith::TruncIOp::create(rewriter, location, resultType, loaded);
        return loaded;
      };
      physicalOutputValues.push_back(loadPlane(valueStorage));
      if (entry.fourState)
        physicalOutputValues.push_back(loadPlane(unknownStorage));
      continue;
    }
    if (isDPIReal(entry)) {
      Type resultType = getDPIRealType(context, entry);
      physicalOutputValues.push_back(
          LLVM::LoadOp::create(rewriter, location, resultType,
                               outputPlanes[index].first, entry.width / 8));
      continue;
    }
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
  for (const std::optional<OpenArrayStorage> &storage : openInputs)
    if (storage && storage->recursive)
      LLVM::CallOp::create(
          rewriter, location, TypeRange{},
          SymbolRefAttr::get(context,
                             "obelisk_rt_v1_dpi_open_array_release_recursive"),
          storage->recursiveStorage);
  Value status = runtime::RTStatusFromBitsOp::create(
      rewriter, location, runtime::StatusType::get(context), marshallingStatus);
  physicalOutputValues.push_back(status);
  if (physicalOutputValues.size() != operation.getNumResults())
    return operation.emitOpError("has inconsistent physical DPI results");
  rewriter.replaceOp(operation, physicalOutputValues);

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
