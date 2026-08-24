//===- SimulationOverrideLowering.cpp - Override rewrite patterns -------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

struct ManagedOverrideValue {
  uint64_t planeSize;
  bool fourState;
  Value value;
  Value unknown;
};

FailureOr<ManagedOverrideValue> materializeManagedOverrideValue(
    Type valueType, ValueRange converted, ConversionPatternRewriter &rewriter,
    Location location, const llvm::DataLayout &dataLayout) {
  llvm::DataLayout local(dataLayout.getStringRepresentation());
  llvm::LLVMContext llvmContext;
  FailureOr<analysis::SimulationStorageProperties> storage =
      analysis::getSimulationStorageProperties(valueType, local, llvmContext);
  if (failed(storage) ||
      converted.size() != analysis::getSimulationPhysicalStorageCount(*storage))
    return failure();
  SmallVector<Value> addresses;
  for (Value value : converted) {
    Value address =
        entryAlloca(rewriter, location, value.getType(), 1, storage->alignment);
    LLVM::StoreOp::create(rewriter, location, value, address,
                          storage->alignment);
    addresses.push_back(address);
  }
  Value unknown = LLVM::ZeroOp::create(
      rewriter, location, LLVM::LLVMPointerType::get(rewriter.getContext()));
  if (addresses.size() == 2)
    unknown = addresses[1];
  return ManagedOverrideValue{storage->size, storage->fourState,
                              addresses.front(), unknown};
}

class OverrideConversion final
    : public OpConversionPattern<sim::SimOverrideOp> {
public:
  OverrideConversion(const TypeConverter &converter, MLIRContext *context,
                     uint64_t stateBitCount, const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimOverrideOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (isa<sim::ManagedRefType>(op.getTarget().getType())) {
      if (adaptor.getTarget().size() != 2 || adaptor.getValue().empty())
        return failure();
      FailureOr<ManagedOverrideValue> managed = materializeManagedOverrideValue(
          op.getValue().getType(), adaptor.getValue(), rewriter, op.getLoc(),
          dataLayout);
      if (failed(managed))
        return failure();
      Type i32 = rewriter.getI32Type();
      Type i64 = rewriter.getI64Type();
      Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, op.getLoc(), pointer, "__obelisk_current_context");
      Value context = LLVM::LoadOp::create(rewriter, op.getLoc(), pointer,
                                           contextAddress, 8);
      Value status =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_object_override"),
              ValueRange{
                  managedObjectPointer(rewriter, op.getLoc(),
                                       adaptor.getTarget()[0]),
                  adaptor.getTarget()[1],
                  llvmConstant(rewriter, op.getLoc(), i64, managed->planeSize),
                  llvmConstant(rewriter, op.getLoc(), i32,
                               managed->fourState ? 1 : 0),
                  llvmConstant(rewriter, op.getLoc(), i32,
                               op.getIsAssign() ? 1 : 0),
                  llvmConstant(rewriter, op.getLoc(), i32, 0),
                  llvmConstant(rewriter, op.getLoc(), i64, 0),
                  llvmConstant(rewriter, op.getLoc(), i32, 0), managed->value,
                  managed->unknown})
              .getResult();
      LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{context, status});
      rewriter.eraseOp(op);
      return success();
    }
    if (adaptor.getTarget().size() != 1 || adaptor.getValue().empty())
      return failure();
    Type valueType = op.getValue().getType();
    std::optional<unsigned> width = nativeStateWidth(valueType);
    if (!width)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    IntegerType plane = rewriter.getIntegerType(*width);
    IntegerType i32 = rewriter.getI32Type();
    IntegerType i64 = rewriter.getI64Type();
    Location location = op.getLoc();

    Value value = adaptor.getValue().front();
    if (isa<FloatType>(valueType))
      value = arith::BitcastOp::create(rewriter, location, plane, value);
    Value valueStorage = entryAlloca(rewriter, location, plane, 1, 1);
    LLVM::StoreOp::create(rewriter, location, value, valueStorage, 1);
    Value unknownStorage = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknownStorage = entryAlloca(rewriter, location, plane, 1, 1);
      LLVM::StoreOp::create(rewriter, location, adaptor.getValue()[1],
                            unknownStorage, 1);
    }
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    Value globalValue = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                  "__obelisk_state_value");
    Value globalUnknown = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                    "__obelisk_state_unknown");
    uint32_t descriptorKind = isa<sim::NetType>(op.getTarget().getType())
                                  ? OBELISK_RT_DESCRIPTOR_NET
                                  : OBELISK_RT_DESCRIPTOR_STORAGE;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_native_override"),
            ValueRange{
                context, globalValue, globalUnknown,
                llvmConstant(rewriter, location, i64, stateBitCount),
                adaptor.getTarget().front(),
                llvmConstant(rewriter, location, i64, *width),
                llvmConstant(rewriter, location, i32, descriptorKind),
                llvmConstant(rewriter, location, i32, op.getIsAssign() ? 1 : 0),
                valueStorage, unknownStorage})
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{context, status});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const llvm::DataLayout &dataLayout;
};

class DynamicOverrideConversion final
    : public OpConversionPattern<sim::SimDynamicOverrideOp> {
public:
  DynamicOverrideConversion(const TypeConverter &converter,
                            MLIRContext *context, uint64_t stateBitCount,
                            const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimDynamicOverrideOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (isa<sim::ManagedRefType>(op.getTarget().getType())) {
      if (adaptor.getTarget().size() != 2 || adaptor.getValue().empty() ||
          adaptor.getOwner().size() != 1)
        return failure();
      FailureOr<ManagedOverrideValue> managed = materializeManagedOverrideValue(
          op.getValue().getType(), adaptor.getValue(), rewriter, op.getLoc(),
          dataLayout);
      if (failed(managed))
        return failure();
      Type i32 = rewriter.getI32Type();
      Type i64 = rewriter.getI64Type();
      Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, op.getLoc(), pointer, "__obelisk_current_context");
      Value context = LLVM::LoadOp::create(rewriter, op.getLoc(), pointer,
                                           contextAddress, 8);
      Value status =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_object_override"),
              ValueRange{
                  managedObjectPointer(rewriter, op.getLoc(),
                                       adaptor.getTarget()[0]),
                  adaptor.getTarget()[1],
                  llvmConstant(rewriter, op.getLoc(), i64, managed->planeSize),
                  llvmConstant(rewriter, op.getLoc(), i32,
                               managed->fourState ? 1 : 0),
                  llvmConstant(rewriter, op.getLoc(), i32,
                               op.getIsAssign() ? 1 : 0),
                  llvmConstant(rewriter, op.getLoc(), i32, 1),
                  adaptor.getOwner().front(),
                  llvmConstant(rewriter, op.getLoc(), i32,
                               op.getClaim() ? 1 : 0),
                  managed->value, managed->unknown})
              .getResult();
      LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{context, status});
      rewriter.eraseOp(op);
      return success();
    }
    if (adaptor.getTarget().size() != 1 || adaptor.getValue().empty() ||
        adaptor.getOwner().size() != 1)
      return failure();
    Type valueType = op.getValue().getType();
    std::optional<unsigned> width = nativeStateWidth(valueType);
    if (!width)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    IntegerType plane = rewriter.getIntegerType(*width);
    IntegerType i32 = rewriter.getI32Type();
    IntegerType i64 = rewriter.getI64Type();
    Location location = op.getLoc();
    Value value = adaptor.getValue().front();
    if (isa<FloatType>(valueType))
      value = arith::BitcastOp::create(rewriter, location, plane, value);
    Value valueStorage = entryAlloca(rewriter, location, plane, 1, 1);
    LLVM::StoreOp::create(rewriter, location, value, valueStorage, 1);
    Value unknownStorage = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknownStorage = entryAlloca(rewriter, location, plane, 1, 1);
      LLVM::StoreOp::create(rewriter, location, adaptor.getValue()[1],
                            unknownStorage, 1);
    }
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    Value globalValue = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                  "__obelisk_state_value");
    Value globalUnknown = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                    "__obelisk_state_unknown");
    uint32_t descriptorKind = isa<sim::NetType>(op.getTarget().getType())
                                  ? OBELISK_RT_DESCRIPTOR_NET
                                  : OBELISK_RT_DESCRIPTOR_STORAGE;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_native_dynamic_override"),
            ValueRange{
                context, globalValue, globalUnknown,
                llvmConstant(rewriter, location, i64, stateBitCount),
                adaptor.getTarget().front(),
                llvmConstant(rewriter, location, i64, *width),
                llvmConstant(rewriter, location, i32, descriptorKind),
                llvmConstant(rewriter, location, i32, op.getIsAssign() ? 1 : 0),
                adaptor.getOwner().front(),
                llvmConstant(rewriter, location, i32, op.getClaim() ? 1 : 0),
                valueStorage, unknownStorage})
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{context, status});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const llvm::DataLayout &dataLayout;
};

class ReleaseOverrideConversion final
    : public OpConversionPattern<sim::SimReleaseOverrideOp> {
public:
  ReleaseOverrideConversion(const TypeConverter &converter,
                            MLIRContext *context, uint64_t stateBitCount,
                            const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimReleaseOverrideOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (auto reference =
            dyn_cast<sim::ManagedRefType>(op.getTarget().getType())) {
      if (adaptor.getTarget().size() != 2)
        return failure();
      llvm::DataLayout local(dataLayout.getStringRepresentation());
      llvm::LLVMContext llvmContext;
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(reference.getElementType(),
                                                   local, llvmContext);
      if (failed(storage))
        return failure();
      Type i32 = rewriter.getI32Type();
      Type i64 = rewriter.getI64Type();
      Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, op.getLoc(), pointer, "__obelisk_current_context");
      Value context = LLVM::LoadOp::create(rewriter, op.getLoc(), pointer,
                                           contextAddress, 8);
      Value status =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_object_release_override"),
              ValueRange{
                  managedObjectPointer(rewriter, op.getLoc(),
                                       adaptor.getTarget()[0]),
                  adaptor.getTarget()[1],
                  llvmConstant(rewriter, op.getLoc(), i64, storage->size),
                  llvmConstant(rewriter, op.getLoc(), i32,
                               storage->fourState ? 1 : 0),
                  llvmConstant(rewriter, op.getLoc(), i32,
                               op.getIsAssign() ? 1 : 0)})
              .getResult();
      LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{context, status});
      rewriter.eraseOp(op);
      return success();
    }
    if (adaptor.getTarget().size() != 1)
      return failure();
    Type elementType;
    if (auto ref = dyn_cast<sim::RefType>(op.getTarget().getType()))
      elementType = ref.getElementType();
    else if (auto net = dyn_cast<sim::NetType>(op.getTarget().getType()))
      elementType = net.getElementType();
    std::optional<unsigned> width = nativeStateWidth(elementType);
    if (!width)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    IntegerType i32 = rewriter.getI32Type();
    IntegerType i64 = rewriter.getI64Type();
    Location location = op.getLoc();
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    Value globalValue = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                  "__obelisk_state_value");
    Value globalUnknown = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                    "__obelisk_state_unknown");
    uint32_t descriptorKind = isa<sim::NetType>(op.getTarget().getType())
                                  ? OBELISK_RT_DESCRIPTOR_NET
                                  : OBELISK_RT_DESCRIPTOR_STORAGE;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_native_release_override"),
            ValueRange{context, globalValue, globalUnknown,
                       llvmConstant(rewriter, location, i64, stateBitCount),
                       adaptor.getTarget().front(),
                       llvmConstant(rewriter, location, i64, *width),
                       llvmConstant(rewriter, location, i32, descriptorKind),
                       llvmConstant(rewriter, location, i32,
                                    op.getIsAssign() ? 1 : 0)})
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{context, status});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const llvm::DataLayout &dataLayout;
};

} // namespace

void populateOverrideToLLVMConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter,
    uint64_t stateBitCount, const llvm::DataLayout &dataLayout) {
  patterns.add<OverrideConversion, DynamicOverrideConversion,
               ReleaseOverrideConversion>(converter, patterns.getContext(),
                                          stateBitCount, dataLayout);
}

} // namespace obelisk::detail
