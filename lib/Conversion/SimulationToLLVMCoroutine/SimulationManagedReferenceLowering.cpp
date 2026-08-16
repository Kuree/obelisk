//===- SimulationManagedReferenceLowering.cpp - Reference patterns ---===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/MathExtras.h"

#include <cstddef>
#include <limits>

using namespace mlir;

namespace obelisk::detail {

namespace {

Value packArgumentReference(OpBuilder &builder, Location location, Value owner,
                            Value payload, uint32_t managed) {
  Type i192 = builder.getIntegerType(192);
  Value packed = arith::ExtUIOp::create(builder, location, i192, owner);
  Value extendedPayload =
      arith::ExtUIOp::create(builder, location, i192, payload);
  Value payloadShift = llvmConstant(builder, location, i192, 64);
  extendedPayload =
      arith::ShLIOp::create(builder, location, extendedPayload, payloadShift);
  packed = arith::OrIOp::create(builder, location, packed, extendedPayload);
  if (managed) {
    APInt tag(192, managed);
    tag <<= 128;
    Value tagValue = arith::ConstantOp::create(
        builder, location, i192, builder.getIntegerAttr(i192, tag));
    packed = arith::OrIOp::create(builder, location, packed, tagValue);
  }
  return packed;
}

struct UnpackedArgumentReference {
  Value owner;
  Value payload;
  Value managed;
};

UnpackedArgumentReference unpackArgumentReference(OpBuilder &builder,
                                                  Location location,
                                                  Value reference) {
  Type i192 = builder.getIntegerType(192);
  Type i64 = builder.getI64Type();
  Value owner = arith::TruncIOp::create(builder, location, i64, reference);
  Value payloadShift = llvmConstant(builder, location, i192, 64);
  Value shiftedPayload =
      arith::ShRUIOp::create(builder, location, reference, payloadShift);
  Value payload =
      arith::TruncIOp::create(builder, location, i64, shiftedPayload);
  Value tagShift = llvmConstant(builder, location, i192, 128);
  Value shiftedTag =
      arith::ShRUIOp::create(builder, location, reference, tagShift);
  Value managed = arith::TruncIOp::create(builder, location,
                                          builder.getI32Type(), shiftedTag);
  return {owner, payload, managed};
}

class ArgumentRefFromRefConversion final
    : public OpConversionPattern<sim::SimArgumentRefFromRefOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimArgumentRefFromRefOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Value zero = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
    rewriter.replaceOp(op, packArgumentReference(rewriter, op.getLoc(), zero,
                                                 adaptor.getInput().front(),
                                                 false));
    return success();
  }
};

class ArgumentRefFromManagedConversion final
    : public OpConversionPattern<sim::SimArgumentRefFromManagedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimArgumentRefFromManagedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 2)
      return failure();
    rewriter.replaceOp(op, packArgumentReference(rewriter, op.getLoc(),
                                                 adaptor.getInput()[0],
                                                 adaptor.getInput()[1], true));
    return success();
  }
};

class ReferencePathIndexConversion final
    : public OpConversionPattern<sim::SimReferencePathIndexOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimReferencePathIndexOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1 || adaptor.getIndex().size() != 1 ||
        adaptor.getOwnerReference().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    UnpackedArgumentReference ownerReference = unpackArgumentReference(
        rewriter, op.getLoc(), adaptor.getOwnerReference().front());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_reference_path_index_create"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getContainer().front()),
                       adaptor.getIndex().front(),
                       managedObjectPointer(rewriter, op.getLoc(),
                                            ownerReference.owner),
                       ownerReference.payload, ownerReference.managed, output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value path =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), path));
    return success();
  }
};

class ReferencePathAssocConversion final
    : public OpConversionPattern<sim::SimReferencePathAssocOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimReferencePathAssocOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2 || adaptor.getOwnerReference().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value key = makeNativeAssocKey(rewriter, op.getLoc(),
                                   op.getArray().getType(), adaptor.getKey());
    UnpackedArgumentReference ownerReference = unpackArgumentReference(
        rewriter, op.getLoc(), adaptor.getOwnerReference().front());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_reference_path_assoc_create"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            ownerReference.owner),
                       ownerReference.payload, ownerReference.managed, output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value path =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), path));
    return success();
  }
};

class ArgumentRefFromPathConversion final
    : public OpConversionPattern<sim::SimArgumentRefFromPathOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimArgumentRefFromPathOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Value zero = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
    rewriter.replaceOp(op, packArgumentReference(rewriter, op.getLoc(),
                                                 adaptor.getInput().front(),
                                                 zero, 2));
    return success();
  }
};

class ArgumentRefLoadConversion final
    : public OpConversionPattern<sim::SimArgumentRefLoadOp> {
public:
  ArgumentRefLoadConversion(const TypeConverter &converter,
                            MLIRContext *context,
                            const llvm::DataLayout &dataLayout,
                            uint64_t stateBitCount)
      : OpConversionPattern(converter, context), dataLayout(dataLayout),
        stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimArgumentRefLoadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 1)
      return failure();
    SmallVector<Type> convertedTypes;
    if (failed(getTypeConverter()->convertType(op.getResult().getType(),
                                               convertedTypes)) ||
        convertedTypes.empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getResult().getType(),
                                                 local, llvmContext);
    std::optional<unsigned> bitWidth =
        nativeStateWidth(op.getResult().getType());
    if (failed(storage) || !bitWidth ||
        convertedTypes.size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    UnpackedArgumentReference reference = unpackArgumentReference(
        rewriter, location, adaptor.getReference().front());
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value valueOut = entryAlloca(rewriter, location, convertedTypes[0], 1,
                                 storage->alignment);
    LLVM::StoreOp::create(
        rewriter, location,
        LLVM::ZeroOp::create(rewriter, location, convertedTypes[0]), valueOut,
        storage->alignment);
    Value unknownOut = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (convertedTypes.size() == 2) {
      unknownOut = entryAlloca(rewriter, location, convertedTypes[1], 1,
                               storage->alignment);
      LLVM::StoreOp::create(
          rewriter, location,
          LLVM::ZeroOp::create(rewriter, location, convertedTypes[1]),
          unknownOut, storage->alignment);
    }
    Value valueState = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                 "__obelisk_state_value");
    Value unknownState = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                   "__obelisk_state_unknown");
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_argument_ref_load"),
            ValueRange{
                context, valueState, unknownState,
                llvmConstant(rewriter, location, i64, stateBitCount),
                managedObjectPointer(rewriter, location, reference.owner),
                reference.payload, reference.managed,
                llvmConstant(rewriter, location, i64, *bitWidth),
                llvmConstant(rewriter, location, i64, storage->size),
                llvmConstant(rewriter, location, i32, storage->fourState),
                llvmConstant(
                    rewriter, location, i32,
                    isa<sim::StringType>(op.getResult().getType())
                        ? OBELISK_RT_ARGUMENT_VALUE_STRING
                    : sim::isManagedHandleType(op.getResult().getType())
                        ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                        : OBELISK_RT_ARGUMENT_VALUE_BITS),
                valueOut, unknownOut})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    SmallVector<Value> results{LLVM::LoadOp::create(
        rewriter, location, convertedTypes[0], valueOut, storage->alignment)};
    if (convertedTypes.size() == 2)
      results.push_back(LLVM::LoadOp::create(rewriter, location,
                                             convertedTypes[1], unknownOut,
                                             storage->alignment));
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
  uint64_t stateBitCount;
};

class ArgumentRefStoreConversion final
    : public OpConversionPattern<sim::SimArgumentRefStoreOp> {
public:
  ArgumentRefStoreConversion(const TypeConverter &converter,
                             MLIRContext *context,
                             const llvm::DataLayout &dataLayout,
                             uint64_t stateBitCount)
      : OpConversionPattern(converter, context), dataLayout(dataLayout),
        stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimArgumentRefStoreOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 1 || adaptor.getValue().empty())
      return failure();
    Type valueType = op.getValue().getType();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(valueType, local, llvmContext);
    std::optional<unsigned> bitWidth = nativeStateWidth(valueType);
    if (failed(storage) || !bitWidth ||
        adaptor.getValue().size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    UnpackedArgumentReference reference = unpackArgumentReference(
        rewriter, location, adaptor.getReference().front());
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value valueState = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                 "__obelisk_state_value");
    Value unknownState = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                   "__obelisk_state_unknown");
    SmallVector<Value> commonArguments{
        context,
        valueState,
        unknownState,
        llvmConstant(rewriter, location, i64, stateBitCount),
        managedObjectPointer(rewriter, location, reference.owner),
        reference.payload,
        reference.managed,
        llvmConstant(rewriter, location, i64, *bitWidth),
        llvmConstant(rewriter, location, i64, storage->size),
        llvmConstant(rewriter, location, i32, storage->fourState),
        llvmConstant(rewriter, location, i32,
                     isa<sim::StringType>(valueType)
                         ? OBELISK_RT_ARGUMENT_VALUE_STRING
                     : sim::isManagedHandleType(valueType)
                         ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                         : OBELISK_RT_ARGUMENT_VALUE_BITS)};
    Value valueIn =
        entryAlloca(rewriter, location, adaptor.getValue().front().getType(), 1,
                    storage->alignment);
    LLVM::StoreOp::create(rewriter, location, adaptor.getValue().front(),
                          valueIn, storage->alignment);
    Value unknownIn = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknownIn =
          entryAlloca(rewriter, location, adaptor.getValue()[1].getType(), 1,
                      storage->alignment);
      LLVM::StoreOp::create(rewriter, location, adaptor.getValue()[1],
                            unknownIn, storage->alignment);
    }
    SmallVector<Value> storeArguments(commonArguments);
    storeArguments.push_back(valueIn);
    storeArguments.push_back(unknownIn);
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{i32},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_argument_ref_store"),
                       storeArguments)
                       .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
  uint64_t stateBitCount;
};

class ManagedLoadConversion final
    : public OpConversionPattern<sim::SimManagedLoadOp> {
public:
  ManagedLoadConversion(const TypeConverter &converter, MLIRContext *context,
                        const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimManagedLoadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 2)
      return failure();
    SmallVector<Type> convertedTypes;
    if (failed(getTypeConverter()->convertType(op.getResult().getType(),
                                               convertedTypes)) ||
        convertedTypes.empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getResult().getType(),
                                                 local, llvmContext);
    if (failed(storage) ||
        convertedTypes.size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i64 = rewriter.getI64Type();
    Value object =
        managedObjectPointer(rewriter, op.getLoc(), adaptor.getReference()[0]);
    Value baseOffset = adaptor.getReference()[1];
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    SmallVector<Value> results;
    if (isa<sim::ClassHandleType>(op.getResult().getType())) {
      Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
      LLVM::StoreOp::create(
          rewriter, op.getLoc(),
          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer), output, 8);
      Value status =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_object_field_load"),
              ValueRange{object, baseOffset, output})
              .getResult();
      reportManagedStatus(rewriter, op.getLoc(), context, status);
      Value loaded =
          LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
      results.push_back(managedObjectHandle(rewriter, op.getLoc(), loaded));
    } else {
      SmallVector<Value> outputs;
      for (Type type : convertedTypes) {
        Value output =
            entryAlloca(rewriter, op.getLoc(), type, 1, storage->alignment);
        LLVM::StoreOp::create(rewriter, op.getLoc(),
                              LLVM::ZeroOp::create(rewriter, op.getLoc(), type),
                              output, storage->alignment);
        outputs.push_back(output);
      }
      SmallVector<Value> arguments{
          object, baseOffset, outputs.front(),
          llvmConstant(rewriter, op.getLoc(), i64, storage->size)};
      StringRef callee = "obelisk_rt_v1_object_read";
      if (outputs.size() == 2) {
        callee = "obelisk_rt_v1_object_read_planes";
        arguments.insert(arguments.end() - 1, outputs[1]);
      }
      Value status =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
              SymbolRefAttr::get(rewriter.getContext(), callee), arguments)
              .getResult();
      reportManagedStatus(rewriter, op.getLoc(), context, status);
      for (auto [type, output] : llvm::zip_equal(convertedTypes, outputs))
        results.push_back(LLVM::LoadOp::create(rewriter, op.getLoc(), type,
                                               output, storage->alignment));
    }
    SmallVector<ValueRange> replacements{ValueRange(results)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

class ManagedBitsDynStoreConversion final
    : public OpConversionPattern<sim::SimManagedBitsDynStoreOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimManagedBitsDynStoreOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 2 ||
        adaptor.getReplacement().size() != 1 || adaptor.getLowBit().size() != 1)
      return failure();
    std::optional<unsigned> inputWidth =
        sim::getPackedWidth(op.getReference().getType().getElementType());
    if (!inputWidth)
      return failure();
    auto replacementType = cast<IntegerType>(op.getReplacement().getType());
    auto lowType = cast<IntegerType>(op.getLowBit().getType());
    Value replacement = adaptor.getReplacement().front();
    Value low = adaptor.getLowBit().front();

    Location location = op.getLoc();
    Type i64 = rewriter.getI64Type();
    Type i32 = rewriter.getI32Type();
    auto signedConstant = [&](IntegerType type, int64_t value) {
      return arith::ConstantOp::create(
          rewriter, location, type,
          rewriter.getIntegerAttr(type, APInt(type.getWidth(),
                                              static_cast<uint64_t>(value),
                                              /*isSigned=*/true)));
    };
    auto resizeSigned = [&](Value value, unsigned width) -> Value {
      auto type = cast<IntegerType>(value.getType());
      if (type.getWidth() < width)
        return arith::ExtSIOp::create(rewriter, location,
                                      rewriter.getIntegerType(width), value);
      if (type.getWidth() > width)
        return arith::TruncIOp::create(rewriter, location,
                                       rewriter.getIntegerType(width), value);
      return value;
    };
    auto resizeUnsigned = [&](Value value, unsigned width) -> Value {
      auto type = cast<IntegerType>(value.getType());
      if (type.getWidth() < width)
        return arith::ExtUIOp::create(rewriter, location,
                                      rewriter.getIntegerType(width), value);
      if (type.getWidth() > width)
        return arith::TruncIOp::create(rewriter, location,
                                       rewriter.getIntegerType(width), value);
      return value;
    };
    if (lowType.getWidth() == std::numeric_limits<unsigned>::max())
      return failure();
    unsigned boundBits = std::max(
        2u, llvm::Log2_64_Ceil(static_cast<uint64_t>(std::max(
                                      *inputWidth,
                                      replacementType.getWidth())) +
                                  1) +
                2);
    unsigned checkWidth = std::max(lowType.getWidth() + 1, boundBits);
    Value checkedLow = resizeSigned(low, checkWidth);
    auto checkType = cast<IntegerType>(checkedLow.getType());
    Value atLeastPartial = arith::CmpIOp::create(
        rewriter, location, arith::CmpIPredicate::sge, checkedLow,
        signedConstant(checkType,
                       -static_cast<int64_t>(replacementType.getWidth() - 1)));
    Value belowField =
        arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::slt,
                              checkedLow,
                              signedConstant(checkType, *inputWidth));
    Value valid =
        arith::AndIOp::create(rewriter, location, atLeastPartial, belowField);

    Value lowI64 = resizeSigned(checkedLow, 64);
    Value replacementI64 = resizeUnsigned(replacement, 64);
    Value validI32 = arith::ExtUIOp::create(rewriter, location, i32, valid);
    Value object = managedObjectPointer(rewriter, location,
                                        adaptor.getReference().front());
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_object_bits_insert"),
            ValueRange{object, adaptor.getReference()[1],
                       llvmConstant(rewriter, location, i64, *inputWidth),
                       lowI64, validI32, replacementI64,
                       llvmConstant(rewriter, location, i32,
                                    replacementType.getWidth())})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);

    rewriter.eraseOp(op);
    return success();
  }
};

class ManagedStoreConversion final
    : public OpConversionPattern<sim::SimManagedStoreOp> {
public:
  ManagedStoreConversion(const TypeConverter &converter, MLIRContext *context,
                         const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimManagedStoreOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 2 || adaptor.getValue().empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getValue().getType(), local,
                                                 llvmContext);
    if (failed(storage) ||
        adaptor.getValue().size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();
    Type i64 = rewriter.getI64Type();
    Value object =
        managedObjectPointer(rewriter, op.getLoc(), adaptor.getReference()[0]);
    Value baseOffset = adaptor.getReference()[1];
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status;
    if (isa<sim::ClassHandleType>(op.getValue().getType())) {
      status = LLVM::CallOp::create(
                   rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                   SymbolRefAttr::get(rewriter.getContext(),
                                      "obelisk_rt_v1_object_field_store"),
                   ValueRange{object, baseOffset,
                              managedObjectPointer(rewriter, op.getLoc(),
                                                   adaptor.getValue().front())})
                   .getResult();
    } else {
      SmallVector<Value> inputs;
      for (Value value : adaptor.getValue()) {
        Value input = entryAlloca(rewriter, op.getLoc(), value.getType(), 1,
                                  storage->alignment);
        LLVM::StoreOp::create(rewriter, op.getLoc(), value, input,
                              storage->alignment);
        inputs.push_back(input);
      }
      SmallVector<Value> arguments{
          object, baseOffset, inputs.front(),
          llvmConstant(rewriter, op.getLoc(), i64, storage->size)};
      StringRef callee = "obelisk_rt_v1_object_write";
      if (inputs.size() == 2) {
        callee = "obelisk_rt_v1_object_write_planes";
        arguments.insert(arguments.end() - 1, inputs[1]);
      }
      status = LLVM::CallOp::create(
                   rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                   SymbolRefAttr::get(rewriter.getContext(), callee), arguments)
                   .getResult();
    }
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

class ManagedNBAConversion final
    : public OpConversionPattern<sim::SimManagedNBAEnqueueOp> {
public:
  ManagedNBAConversion(const TypeConverter &converter, MLIRContext *context,
                       const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimManagedNBAEnqueueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDestination().size() != 2 || adaptor.getValue().empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getValue().getType(), local,
                                                 llvmContext);
    if (failed(storage) ||
        adaptor.getValue().size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i64 = rewriter.getI64Type();
    auto savePlane = [&](Value value, uint32_t alignment) {
      Value address =
          entryAlloca(rewriter, location, value.getType(), 1, alignment);
      LLVM::StoreOp::create(rewriter, location, value, address, alignment);
      return address;
    };

    Value value;
    if (isa<sim::ClassHandleType>(op.getValue().getType()))
      value = savePlane(
          managedObjectPointer(rewriter, location, adaptor.getValue().front()),
          storage->alignment);
    else
      value = savePlane(adaptor.getValue().front(), storage->alignment);
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (storage->fourState)
      unknown = savePlane(adaptor.getValue()[1], storage->alignment);
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, location, i64, 0)
                      : adaptor.getDelay().front();
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_managed_nba"),
            ValueRange{context,
                       managedObjectPointer(rewriter, location,
                                            adaptor.getDestination()[0]),
                       adaptor.getDestination()[1], value, unknown,
                       llvmConstant(rewriter, location, i64, storage->size),
                       delay})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

class ReferencePathNBAConversion final
    : public OpConversionPattern<sim::SimReferencePathNBAEnqueueOp> {
public:
  ReferencePathNBAConversion(const TypeConverter &converter,
                             MLIRContext *context,
                             const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimReferencePathNBAEnqueueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDestination().size() != 1 || adaptor.getValue().empty())
      return failure();
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(op.getValue().getType(), local,
                                                 llvmContext);
    if (failed(storage) ||
        adaptor.getValue().size() !=
            analysis::getSimulationPhysicalStorageCount(*storage))
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i64 = rewriter.getI64Type();
    auto savePlane = [&](Value value, uint32_t alignment) {
      Value address =
          entryAlloca(rewriter, location, value.getType(), 1, alignment);
      LLVM::StoreOp::create(rewriter, location, value, address, alignment);
      return address;
    };

    Value value;
    if (isa<sim::ClassHandleType>(op.getValue().getType()))
      value = savePlane(
          managedObjectPointer(rewriter, location, adaptor.getValue().front()),
          storage->alignment);
    else
      value = savePlane(adaptor.getValue().front(), storage->alignment);
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (storage->fourState)
      unknown = savePlane(adaptor.getValue()[1], storage->alignment);
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, location, i64, 0)
                      : adaptor.getDelay().front();
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_managed_nba"),
            ValueRange{context,
                       managedObjectPointer(rewriter, location,
                                            adaptor.getDestination().front()),
                       llvmConstant(rewriter, location, i64, UINT64_MAX), value,
                       unknown,
                       llvmConstant(rewriter, location, i64, storage->size),
                       delay})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

} // namespace

void populateManagedReferenceToLLVMConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter,
    const llvm::DataLayout &dataLayout, uint64_t stateBitCount) {
  MLIRContext *context = patterns.getContext();
  patterns.add<ArgumentRefFromRefConversion, ArgumentRefFromManagedConversion,
               ReferencePathIndexConversion, ReferencePathAssocConversion,
               ArgumentRefFromPathConversion>(converter, context);
  patterns.add<ArgumentRefLoadConversion, ArgumentRefStoreConversion>(
      converter, context, dataLayout, stateBitCount);
  patterns.add<ManagedBitsDynStoreConversion>(converter, context);
  patterns.add<ManagedLoadConversion, ManagedStoreConversion,
               ManagedNBAConversion, ReferencePathNBAConversion>(
      converter, context, dataLayout);
}

} // namespace obelisk::detail
