//===- SimulationManagedLowering.cpp - Managed runtime lowering -------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

using namespace mlir;

namespace obelisk::detail {

namespace {

Value managedObjectPointer(OpBuilder &builder, Location location,
                           Value handle) {
  return LLVM::IntToPtrOp::create(
      builder, location, LLVM::LLVMPointerType::get(builder.getContext()),
      handle);
}

Value managedObjectHandle(OpBuilder &builder, Location location, Value object) {
  return LLVM::PtrToIntOp::create(builder, location, builder.getI64Type(),
                                  object);
}

std::pair<Value, Value> managedContextAndLane(OpBuilder &builder,
                                              Location location) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Value address = LLVM::AddressOfOp::create(builder, location, pointer,
                                            "__obelisk_current_context");
  Value context = LLVM::LoadOp::create(builder, location, pointer, address, 8);
  Value lane =
      LLVM::CallOp::create(builder, location, TypeRange{pointer},
                           SymbolRefAttr::get(builder.getContext(),
                                              "obelisk_rt_v1_gc_current_lane"),
                           context)
          .getResult();
  return {context, lane};
}

} // namespace

Operation *reportManagedStatus(OpBuilder &builder, Location location,
                               Value context, Value status) {
  (void)context;
  Type statusType = runtime::StatusType::get(builder.getContext());
  Value runtimeStatus = runtime::RTStatusFromBitsOp::create(builder, location,
                                                            statusType, status);
  return sim::SimStatusCheckOp::create(builder, location, runtimeStatus);
}

namespace {

class ClassNullConversion final
    : public OpConversionPattern<sim::SimClassNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassNullOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, arith::ConstantOp::create(
                               rewriter, op.getLoc(), rewriter.getI64Type(),
                               rewriter.getI64IntegerAttr(0)));
    return success();
  }
};

class ManagedNullConversion final
    : public OpConversionPattern<sim::SimManagedNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimManagedNullOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, arith::ConstantOp::create(
                               rewriter, op.getLoc(), rewriter.getI64Type(),
                               rewriter.getI64IntegerAttr(0)));
    return success();
  }
};

class ManagedIsNullConversion final
    : public OpConversionPattern<sim::SimManagedIsNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimManagedIsNullOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Value zero = llvmConstant(rewriter, op.getLoc(),
                              adaptor.getInput().front().getType(), 0);
    rewriter.replaceOpWithNewOp<arith::CmpIOp>(
        op, arith::CmpIPredicate::eq, adaptor.getInput().front(), zero);
    return success();
  }
};

class EventNullConversion final
    : public OpConversionPattern<sim::SimEventNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimEventNullOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, arith::ConstantOp::create(
                               rewriter, op.getLoc(), rewriter.getI64Type(),
                               rewriter.getI64IntegerAttr(-1)));
    return success();
  }
};

static Value zeroNativeValue(OpBuilder &builder, Location location, Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getIntegerAttr(integer, 0));
  if (auto floating = dyn_cast<FloatType>(type))
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getFloatAttr(floating, 0.0));
  return {};
}

class ContainerSizeConversion final
    : public OpConversionPattern<sim::SimContainerSizeOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerSizeOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1)
      return failure();
    rewriter.replaceOp(
        op, LLVM::CallOp::create(
                rewriter, op.getLoc(), TypeRange{rewriter.getI64Type()},
                SymbolRefAttr::get(rewriter.getContext(),
                                   "obelisk_rt_v1_container_size"),
                managedObjectPointer(rewriter, op.getLoc(),
                                     adaptor.getContainer().front()))
                .getResult());
    return success();
  }
};

class ContainerCreateLikeConversion final
    : public OpConversionPattern<sim::SimContainerCreateLikeOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerCreateLikeOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getPreferred().size() != 1 ||
        adaptor.getFallback().size() != 1 || adaptor.getSize().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_create_like"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getPreferred().front()),
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getFallback().front()),
                       adaptor.getSize().front(), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class ContainerCreateConversion final
    : public OpConversionPattern<sim::SimContainerCreateOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerCreateOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getSize().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    auto c32 = [&](uint32_t value) {
      return llvmConstant(rewriter, op.getLoc(), i32, value);
    };
    auto c64 = [&](uint64_t value) {
      return llvmConstant(rewriter, op.getLoc(), i64, value);
    };
    ArrayRef<int64_t> traceOffsets = op.getTraceOffsets();
    ArrayRef<int32_t> traceKinds = op.getTraceKinds();
    Value traceSlots = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!traceOffsets.empty()) {
      std::string bytes(
          traceOffsets.size() * sizeof(obelisk_rt_element_trace_slot_v1), '\0');
      for (auto [index, offset, kind] :
           llvm::enumerate(traceOffsets, traceKinds)) {
        obelisk_rt_element_trace_slot_v1 slot{
            static_cast<uint64_t>(offset),
            static_cast<obelisk_rt_managed_slot_kind_v1>(kind), 0};
        std::memcpy(bytes.data() +
                        index * sizeof(obelisk_rt_element_trace_slot_v1),
                    &slot, sizeof(slot));
      }
      ModuleOp module = op->getParentOfType<ModuleOp>();
      std::string name =
          "__obelisk_element_trace_" + std::to_string(op.getTypeId());
      LLVM::GlobalOp global = module.lookupSymbol<LLVM::GlobalOp>(name);
      if (!global)
        global = makeByteArrayGlobal(module, op.getLoc(), name, bytes);
      traceSlots = LLVM::AddressOfOp::create(rewriter, op.getLoc(), global);
    }
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_create_typed"),
            ValueRange{lane, c32(op.getContainerKind()), c64(op.getTypeId()),
                       c32(op.getElementKind()), c32(op.getElementFlags()),
                       c64(op.getValueSize()), c64(op.getAlignment()),
                       c64(op.getBitWidth()), traceSlots,
                       c64(traceOffsets.size()), adaptor.getSize().front(),
                       c64(op.getBound()), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class ContainerCloneConversion final
    : public OpConversionPattern<sim::SimContainerCloneOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerCloneOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_clone"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getInput().front()),
                       output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class ContainerDeleteConversion final
    : public OpConversionPattern<sim::SimContainerDeleteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerDeleteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_container_delete"),
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getContainer().front()))
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class QueueDeleteConversion final
    : public OpConversionPattern<sim::SimQueueDeleteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimQueueDeleteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getQueue().size() != 1 || adaptor.getIndex().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_queue_delete_index"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getQueue().front()),
                       adaptor.getIndex().front()})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class QueueInsertConversion final
    : public OpConversionPattern<sim::SimQueueInsertOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimQueueInsertOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getQueue().size() != 1 || adaptor.getIndex().size() != 1 ||
        adaptor.getValue().empty() || adaptor.getValue().size() > 2)
      return failure();
    SmallVector<Value> storage;
    for (Value value : adaptor.getValue()) {
      Value slot = entryAlloca(rewriter, op.getLoc(), value.getType(), 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), value, slot, 8);
      storage.push_back(slot);
    }
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown =
        storage.size() == 2
            ? storage[1]
            : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_queue_insert"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getQueue().front()),
                       adaptor.getIndex().front(), storage.front(), unknown})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class RandomNextConversion final
    : public OpConversionPattern<sim::SimRandomNextOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimRandomNextOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContext().size() != 1)
      return failure();
    Value context = adaptor.getContext().front();
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_random_next"),
                       ValueRange{context, output})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(op,
                       LLVM::LoadOp::create(rewriter, op.getLoc(),
                                            rewriter.getI64Type(), output, 8));
    return success();
  }
};

class RandomSeedConversion final
    : public OpConversionPattern<sim::SimRandomSeedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimRandomSeedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContext().size() != 1 || adaptor.getSeed().size() != 1)
      return failure();
    Value context = adaptor.getContext().front();
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_random_seed"),
                       ValueRange{context, adaptor.getSeed().front()})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class RandomBoundedConversion final
    : public OpConversionPattern<sim::SimRandomBoundedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimRandomBoundedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContext().size() != 1 || adaptor.getBound().size() != 1)
      return failure();
    Value context = adaptor.getContext().front();
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_random_bounded"),
                       ValueRange{context, adaptor.getBound().front(), output})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(op,
                       LLVM::LoadOp::create(rewriter, op.getLoc(),
                                            rewriter.getI64Type(), output, 8));
    return success();
  }
};

class ContainerReadConversion final
    : public OpConversionPattern<sim::SimContainerReadOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1 || adaptor.getIndex().size() != 1)
      return failure();
    SmallVector<Type> types;
    if (failed(
            getTypeConverter()->convertType(op.getResult().getType(), types)) ||
        types.empty() || types.size() > 2)
      return failure();
    SmallVector<Value> storage;
    for (Type type : types) {
      Value zero = zeroNativeValue(rewriter, op.getLoc(), type);
      if (!zero)
        return failure();
      Value slot = entryAlloca(rewriter, op.getLoc(), type, 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), zero, slot, 8);
      storage.push_back(slot);
    }
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown =
        storage.size() == 2
            ? storage[1]
            : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_read"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getContainer().front()),
                       adaptor.getIndex().front(), storage.front(), unknown})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> values;
    for (auto [type, slot] : llvm::zip_equal(types, storage))
      values.push_back(
          LLVM::LoadOp::create(rewriter, op.getLoc(), type, slot, 8));
    rewriter.replaceOpWithMultiple(op, {ValueRange(values)});
    return success();
  }
};

class ContainerWriteConversion final
    : public OpConversionPattern<sim::SimContainerWriteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimContainerWriteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getContainer().size() != 1 || adaptor.getIndex().size() != 1 ||
        adaptor.getValue().empty() || adaptor.getValue().size() > 2)
      return failure();
    SmallVector<Value> storage;
    for (Value value : adaptor.getValue()) {
      Value slot = entryAlloca(rewriter, op.getLoc(), value.getType(), 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), value, slot, 8);
      storage.push_back(slot);
    }
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown =
        storage.size() == 2
            ? storage[1]
            : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_container_write"),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getContainer().front()),
                       adaptor.getIndex().front(), storage.front(), unknown})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

static Value makeNativeAssocKey(OpBuilder &builder, Location location,
                                sim::AssocArrayType array, ValueRange values) {
  Type i8 = builder.getI8Type();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Value storage =
      entryAlloca(builder, location, i8, sizeof(obelisk_rt_assoc_key_v1), 8);
  auto store32 = [&](uint64_t offset, uint32_t value) {
    LLVM::StoreOp::create(builder, location,
                          llvmConstant(builder, location, i32, value),
                          byteGEP(builder, location, storage, offset), 4);
  };
  auto store64 = [&](uint64_t offset, Value value) {
    LLVM::StoreOp::create(builder, location,
                          castIntegerWidth(builder, location, value, i64),
                          byteGEP(builder, location, storage, offset), 8);
  };
  bool stringKey = isa<sim::StringType>(array.getKeyType());
  uint32_t keyKind =
      stringKey ? OBELISK_RT_ASSOC_KEY_STRING
                : (array.getSignedKey() ? OBELISK_RT_ASSOC_KEY_SIGNED
                                        : OBELISK_RT_ASSOC_KEY_UNSIGNED);
  uint64_t keyWidth = stringKey ? 0 : *sim::getPackedWidth(array.getKeyType());
  store32(offsetof(obelisk_rt_assoc_key_v1, kind), keyKind);
  store32(offsetof(obelisk_rt_assoc_key_v1, reserved), 0);
  LLVM::StoreOp::create(builder, location,
                        llvmConstant(builder, location, i64, keyWidth),
                        byteGEP(builder, location, storage,
                                offsetof(obelisk_rt_assoc_key_v1, width)),
                        8);
  Value zero = llvmConstant(builder, location, i64, 0);
  if (stringKey) {
    LLVM::StoreOp::create(builder, location, zero,
                          byteGEP(builder, location, storage,
                                  offsetof(obelisk_rt_assoc_key_v1, value)),
                          8);
    LLVM::StoreOp::create(builder, location, zero,
                          byteGEP(builder, location, storage,
                                  offsetof(obelisk_rt_assoc_key_v1, unknown)),
                          8);
    store64(offsetof(obelisk_rt_assoc_key_v1, string), values.front());
  } else {
    store64(offsetof(obelisk_rt_assoc_key_v1, value), values.front());
    if (values.size() == 2)
      store64(offsetof(obelisk_rt_assoc_key_v1, unknown), values[1]);
    else
      LLVM::StoreOp::create(builder, location, zero,
                            byteGEP(builder, location, storage,
                                    offsetof(obelisk_rt_assoc_key_v1, unknown)),
                            8);
    LLVM::StoreOp::create(builder, location, zero,
                          byteGEP(builder, location, storage,
                                  offsetof(obelisk_rt_assoc_key_v1, string)),
                          8);
  }
  return storage;
}

static SmallVector<Value> makeNativeValueStorage(OpBuilder &builder,
                                                 Location location,
                                                 ValueRange values) {
  SmallVector<Value> storage;
  for (Value value : values) {
    Value slot = entryAlloca(builder, location, value.getType(), 1, 8);
    LLVM::StoreOp::create(builder, location, value, slot, 8);
    storage.push_back(slot);
  }
  return storage;
}

static Value makeNativeAssocTrace(OpBuilder &builder, Location location,
                                  Operation *operation, uint64_t typeID,
                                  ArrayRef<int64_t> offsets,
                                  ArrayRef<int32_t> kinds) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  if (offsets.empty())
    return LLVM::ZeroOp::create(builder, location, pointer);
  std::string bytes(offsets.size() * sizeof(obelisk_rt_element_trace_slot_v1),
                    '\0');
  for (auto [index, offset, kind] : llvm::enumerate(offsets, kinds)) {
    obelisk_rt_element_trace_slot_v1 slot{
        static_cast<uint64_t>(offset),
        static_cast<obelisk_rt_managed_slot_kind_v1>(kind), 0};
    std::memcpy(bytes.data() + index * sizeof(obelisk_rt_element_trace_slot_v1),
                &slot, sizeof(slot));
  }
  ModuleOp module = operation->getParentOfType<ModuleOp>();
  std::string name = "__obelisk_element_trace_" + std::to_string(typeID);
  LLVM::GlobalOp global = module.lookupSymbol<LLVM::GlobalOp>(name);
  if (!global)
    global = makeByteArrayGlobal(module, location, name, bytes);
  return LLVM::AddressOfOp::create(builder, location, global);
}

class AssocCreateConversion final
    : public OpConversionPattern<sim::SimAssocCreateOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocCreateOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value trace =
        makeNativeAssocTrace(rewriter, op.getLoc(), op, op.getTypeId(),
                             op.getTraceOffsets(), op.getTraceKinds());
    auto c32 = [&](uint32_t value) {
      return llvmConstant(rewriter, op.getLoc(), i32, value);
    };
    auto c64 = [&](uint64_t value) {
      return llvmConstant(rewriter, op.getLoc(), i64, value);
    };
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_create_typed"),
            ValueRange{lane, c64(op.getTypeId()), c32(op.getElementKind()),
                       c32(op.getElementFlags()), c64(op.getValueSize()),
                       c64(op.getAlignment()), c64(op.getBitWidth()), trace,
                       c64(op.getTraceOffsets().size()), c32(op.getKeyKind()),
                       c64(op.getKeyWidth()), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class AssocReadConversion final
    : public OpConversionPattern<sim::SimAssocReadOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    SmallVector<Type> types;
    if (failed(
            getTypeConverter()->convertType(op.getResult().getType(), types)) ||
        types.empty() || types.size() > 2)
      return failure();
    SmallVector<Value> storage;
    for (Type type : types) {
      Value zero = zeroNativeValue(rewriter, op.getLoc(), type);
      if (!zero)
        return failure();
      Value slot = entryAlloca(rewriter, op.getLoc(), type, 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), zero, slot, 8);
      storage.push_back(slot);
    }
    sim::AssocArrayType array = op.getArray().getType();
    Value key =
        makeNativeAssocKey(rewriter, op.getLoc(), array, adaptor.getKey());
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown =
        storage.size() == 2
            ? storage[1]
            : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
    Value present =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    Value planeSize =
        llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                     (sim::getPackedWidth(op.getResult().getType())
                          .value_or(static_cast<unsigned>(
                              types.front().getIntOrFloatBitWidth())) +
                      7) /
                         8);
    if (sim::isManagedHandleType(op.getResult().getType()))
      planeSize = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                               sizeof(void *));
    Value unknownSize =
        storage.size() == 2
            ? planeSize
            : llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_read_checked"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key, storage.front(), planeSize, unknown, unknownSize,
                       present})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> values;
    for (auto [type, slot] : llvm::zip_equal(types, storage))
      values.push_back(
          LLVM::LoadOp::create(rewriter, op.getLoc(), type, slot, 8));
    rewriter.replaceOpWithMultiple(op, {ValueRange(values)});
    return success();
  }
};

template <typename Op, typename Adaptor>
static LogicalResult
lowerAssocValueMutation(Op op, Adaptor adaptor,
                        ConversionPatternRewriter &rewriter, StringRef callee) {
  if (adaptor.getArray().size() != 1 || adaptor.getValue().empty() ||
      adaptor.getValue().size() > 2)
    return failure();
  SmallVector<Value> storage =
      makeNativeValueStorage(rewriter, op.getLoc(), adaptor.getValue());
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Value unknown =
      storage.size() == 2
          ? storage[1]
          : Value(LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer));
  uint64_t bytes = 0;
  if (auto width = sim::getPackedWidth(op.getValue().getType()))
    bytes = (*width + 7) / 8;
  else if (sim::isManagedHandleType(op.getValue().getType()))
    bytes = sizeof(void *);
  else if (isa<sim::EventType>(op.getValue().getType()))
    bytes = sizeof(uint64_t);
  else if (auto floating = dyn_cast<FloatType>(op.getValue().getType()))
    bytes = floating.getWidth() / 8;
  else if (auto span = sim::getProvenanceSpan(op.getValue().getType()))
    bytes = (*span + 7) / 8;
  if (bytes == 0)
    return failure();
  Value size =
      llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), bytes);
  Value unknownSize =
      storage.size() == 2
          ? size
          : llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
  SmallVector<Value> arguments{
      managedContextAndLane(rewriter, op.getLoc()).second,
      managedObjectPointer(rewriter, op.getLoc(), adaptor.getArray().front())};
  if constexpr (std::is_same_v<Op, sim::SimAssocWriteOp>) {
    if (adaptor.getKey().empty() || adaptor.getKey().size() > 2)
      return failure();
    arguments.push_back(makeNativeAssocKey(
        rewriter, op.getLoc(), op.getArray().getType(), adaptor.getKey()));
  }
  arguments.append({storage.front(), size, unknown, unknownSize});
  auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
  (void)lane;
  Value status =
      LLVM::CallOp::create(
          rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
          SymbolRefAttr::get(rewriter.getContext(), callee), arguments)
          .getResult();
  reportManagedStatus(rewriter, op.getLoc(), context, status);
  rewriter.eraseOp(op);
  return success();
}

class AssocWriteConversion final
    : public OpConversionPattern<sim::SimAssocWriteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocWriteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    return lowerAssocValueMutation(op, adaptor, rewriter,
                                   "obelisk_rt_v1_assoc_write_checked");
  }
};

class AssocDefaultConversion final
    : public OpConversionPattern<sim::SimAssocSetDefaultOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocSetDefaultOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    return lowerAssocValueMutation(op, adaptor, rewriter,
                                   "obelisk_rt_v1_assoc_set_default_checked");
  }
};

class AssocExistsConversion final
    : public OpConversionPattern<sim::SimAssocExistsOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocExistsOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    Value key = makeNativeAssocKey(rewriter, op.getLoc(),
                                   op.getArray().getType(), adaptor.getKey());
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_exists"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key, output})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value loaded = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                        rewriter.getI32Type(), output, 4);
    rewriter.replaceOpWithNewOp<arith::TruncIOp>(op, rewriter.getI1Type(),
                                                 loaded);
    return success();
  }
};

class AssocDeleteConversion final
    : public OpConversionPattern<sim::SimAssocDeleteOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocDeleteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    Value key = makeNativeAssocKey(rewriter, op.getLoc(),
                                   op.getArray().getType(), adaptor.getKey());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_assoc_delete"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class AssocTraverseConversion final
    : public OpConversionPattern<sim::SimAssocTraverseOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimAssocTraverseOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getArray().size() != 1 || adaptor.getKey().empty() ||
        adaptor.getKey().size() > 2)
      return failure();
    sim::AssocArrayType array = op.getArray().getType();
    Value key =
        makeNativeAssocKey(rewriter, op.getLoc(), array, adaptor.getKey());
    Value successSlot =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    StringRef callee;
    int32_t direction = static_cast<int32_t>(op.getDirection());
    if (op.getEndpoint())
      callee = direction > 0 ? "obelisk_rt_v1_assoc_first"
                             : "obelisk_rt_v1_assoc_last";
    else
      callee = direction > 0 ? "obelisk_rt_v1_assoc_next"
                             : "obelisk_rt_v1_assoc_prev";
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), callee),
            ValueRange{lane,
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getArray().front()),
                       key, successSlot})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> keyValues;
    Type i64 = rewriter.getI64Type();
    if (isa<sim::StringType>(array.getKeyType())) {
      Value loaded = LLVM::LoadOp::create(
          rewriter, op.getLoc(), i64,
          byteGEP(rewriter, op.getLoc(), key,
                  offsetof(obelisk_rt_assoc_key_v1, string)),
          8);
      keyValues.push_back(loaded);
    } else {
      SmallVector<Type> converted;
      if (failed(
              getTypeConverter()->convertType(array.getKeyType(), converted)) ||
          converted.empty() || converted.size() > 2)
        return failure();
      for (auto [index, type] : llvm::enumerate(converted)) {
        uint64_t offset = index == 0
                              ? offsetof(obelisk_rt_assoc_key_v1, value)
                              : offsetof(obelisk_rt_assoc_key_v1, unknown);
        Value loaded = LLVM::LoadOp::create(
            rewriter, op.getLoc(), i64,
            byteGEP(rewriter, op.getLoc(), key, offset), 8);
        keyValues.push_back(
            castIntegerWidth(rewriter, op.getLoc(), loaded, type));
      }
    }
    Value success32 = LLVM::LoadOp::create(
        rewriter, op.getLoc(), rewriter.getI32Type(), successSlot, 4);
    Value success1 = arith::TruncIOp::create(rewriter, op.getLoc(),
                                             rewriter.getI1Type(), success32);
    SmallVector<ValueRange> replacements;
    replacements.push_back(keyValues);
    replacements.push_back(ValueRange{success1});
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

static Value finishStringAllocation(ConversionPatternRewriter &rewriter,
                                    Location location, Value context,
                                    Value status, Value output) {
  reportManagedStatus(rewriter, location, context, status);
  return LLVM::LoadOp::create(rewriter, location, rewriter.getI64Type(), output,
                              8);
}

class StringLiteralConversion final
    : public OpConversionPattern<sim::SimStringLiteralOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringLiteralOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ModuleOp module = op->getParentOfType<ModuleOp>();
    if (!module)
      return failure();
    std::string name;
    for (uint64_t suffix = 0;; ++suffix) {
      name = ("__obelisk_string_literal." + Twine(suffix)).str();
      if (!module.lookupSymbol(name))
        break;
    }
    StringRef bytes = op.getValue();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value data = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!bytes.empty()) {
      LLVM::GlobalOp global =
          makeByteArrayGlobal(module, op.getLoc(), name, bytes);
      data = LLVM::AddressOfOp::create(rewriter, op.getLoc(), global);
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()),
        output, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_create"),
            ValueRange{lane, data,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), bytes.size()),
                       output})
            .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }
};

class StringFromPackedConversion final
    : public OpConversionPattern<sim::SimStringFromPackedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringFromPackedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> width =
        sim::getPackedWidth(op.getInput().getType());
    if (!width ||
        (adaptor.getInput().size() != 1 && adaptor.getInput().size() != 2))
      return failure();
    Type planeType = adaptor.getInput().front().getType();
    Value value = entryAlloca(rewriter, op.getLoc(), planeType, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(), adaptor.getInput().front(),
                          value, 8);
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (adaptor.getInput().size() == 2) {
      unknown = entryAlloca(rewriter, op.getLoc(), planeType, 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), adaptor.getInput()[1],
                            unknown, 8);
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()),
        output, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_string_from_packed"),
                       ValueRange{lane, value, unknown,
                                  llvmConstant(rewriter, op.getLoc(),
                                               rewriter.getI64Type(), *width),
                                  output})
                       .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }
};

class StringToPackedConversion final
    : public OpConversionPattern<sim::SimStringToPackedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringToPackedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto resultType = dyn_cast<IntegerType>(
        getTypeConverter()->convertType(op.getResult().getType()));
    if (!resultType || adaptor.getInput().size() != 1)
      return failure();
    Value output = entryAlloca(rewriter, op.getLoc(), resultType, 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), resultType), output, 8);
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_to_packed"),
            ValueRange{adaptor.getInput().front(), output,
                       LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(),
                                    resultType.getWidth())})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), resultType, output, 8));
    return success();
  }
};

class StringConcatConversion final
    : public OpConversionPattern<sim::SimStringConcatOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringConcatOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type i64 = rewriter.getI64Type();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value spans = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!adaptor.getInputs().empty()) {
      spans = entryAlloca(rewriter, op.getLoc(), i64,
                          adaptor.getInputs().size(), 8);
      for (auto [index, input] : llvm::enumerate(adaptor.getInputs())) {
        if (input.size() != 1)
          return failure();
        LLVM::StoreOp::create(
            rewriter, op.getLoc(), input.front(),
            byteGEP(rewriter, op.getLoc(), spans, index * sizeof(uint64_t)), 8);
      }
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          output, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_string_concat_many"),
                       ValueRange{lane, spans,
                                  llvmConstant(rewriter, op.getLoc(), i64,
                                               adaptor.getInputs().size()),
                                  output})
                       .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }
};

template <typename Op>
class StringAllocatingCallConversion final : public OpConversionPattern<Op> {
public:
  StringAllocatingCallConversion(const TypeConverter &converter,
                                 MLIRContext *context, StringRef symbol)
      : OpConversionPattern<Op>(converter, context), symbol(symbol) {}

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Value> operands;
    for (ValueRange range : adaptor.getOperands()) {
      if (range.size() != 1)
        return failure();
      operands.push_back(range.front());
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    operands.insert(operands.begin(), lane);
    if constexpr (std::is_same_v<Op, sim::SimStringPutcOp>)
      operands.back() = arith::ExtUIOp::create(
          rewriter, op.getLoc(), rewriter.getI32Type(), operands.back());
    if constexpr (std::is_same_v<Op, sim::SimStringCaseConvertOp>)
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(),
                                      op.getToUpper() ? 1 : 0));
    if constexpr (std::is_same_v<Op, sim::SimStringFormatIntegerOp>) {
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(), op.getRadix()));
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(),
                                      op.getIsSigned() ? 1 : 0));
    }
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()),
        output, 8);
    operands.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), symbol), operands)
            .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }

private:
  std::string symbol;
};

class StringLengthConversion final
    : public OpConversionPattern<sim::SimStringLengthOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringLengthOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(
        op, LLVM::CallOp::create(
                rewriter, op.getLoc(), TypeRange{rewriter.getI64Type()},
                SymbolRefAttr::get(rewriter.getContext(),
                                   "obelisk_rt_v1_string_length"),
                adaptor.getInput().front())
                .getResult());
    return success();
  }
};

class StringGetcConversion final
    : public OpConversionPattern<sim::SimStringGetcOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringGetcOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Value result =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_getc"),
            ValueRange{adaptor.getInput().front(), adaptor.getIndex().front()})
            .getResult();
    rewriter.replaceOp(op,
                       arith::TruncIOp::create(rewriter, op.getLoc(),
                                               rewriter.getI8Type(), result));
    return success();
  }
};

class StringCompareConversion final
    : public OpConversionPattern<sim::SimStringCompareOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringCompareOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    StringRef symbol = op.getCaseInsensitive()
                           ? "obelisk_rt_v1_string_compare_insensitive"
                           : "obelisk_rt_v1_string_compare";
    rewriter.replaceOp(
        op, LLVM::CallOp::create(
                rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                SymbolRefAttr::get(rewriter.getContext(), symbol),
                ValueRange{adaptor.getLhs().front(), adaptor.getRhs().front()})
                .getResult());
    return success();
  }
};

template <typename Op>
class StringFileOpenConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI32Type()),
        output, 4);
    SmallVector<Value> operands{context, adaptor.getPath().front()};
    StringRef symbol = "obelisk_rt_v1_file_open_string_mcd";
    if constexpr (std::is_same_v<Op, sim::SimFileOpenStringOp>) {
      operands.push_back(adaptor.getMode().front());
      symbol = "obelisk_rt_v1_file_open_string";
    }
    operands.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), symbol), operands)
            .getResult();
    Value descriptor = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                            rewriter.getI32Type(), output, 4);
    Value ok = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, status,
        llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(), 0));
    rewriter.replaceOp(op, arith::SelectOp::create(
                               rewriter, op.getLoc(), ok, descriptor,
                               LLVM::ZeroOp::create(rewriter, op.getLoc(),
                                                    rewriter.getI32Type())));
    return success();
  }
};

class StringFileGetlineConversion final
    : public OpConversionPattern<sim::SimFileGetlineStringOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimFileGetlineStringOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Type i64 = rewriter.getI64Type();
    Type i32 = rewriter.getI32Type();
    Value stringOutput = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    Value countOutput = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          stringOutput, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i32),
                          countOutput, 4);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_file_getline_string"),
            ValueRange{context, lane, adaptor.getDescriptor().front(),
                       stringOutput, countOutput})
            .getResult();
    Value ok = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, status,
        llvmConstant(rewriter, op.getLoc(), i32, 0));
    Value string =
        LLVM::LoadOp::create(rewriter, op.getLoc(), i64, stringOutput, 8);
    Value count =
        LLVM::LoadOp::create(rewriter, op.getLoc(), i32, countOutput, 4);
    rewriter.replaceOp(
        op, ValueRange{arith::SelectOp::create(
                           rewriter, op.getLoc(), ok, string,
                           LLVM::ZeroOp::create(rewriter, op.getLoc(), i64)),
                       arith::SelectOp::create(
                           rewriter, op.getLoc(), ok, count,
                           LLVM::ZeroOp::create(rewriter, op.getLoc(), i32))});
    return success();
  }
};

template <typename Op>
class StringParseConversion final : public OpConversionPattern<Op> {
public:
  StringParseConversion(const TypeConverter &converter, MLIRContext *context,
                        StringRef symbol)
      : OpConversionPattern<Op>(converter, context), symbol(symbol) {}

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = std::is_same_v<Op, sim::SimStringParseIntegerOp>
                          ? Type(rewriter.getI64Type())
                          : Type(rewriter.getF64Type());
    Value output = entryAlloca(rewriter, op.getLoc(), resultType, 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), resultType), output, 8);
    SmallVector<Value> operands{adaptor.getInput().front()};
    if constexpr (std::is_same_v<Op, sim::SimStringParseIntegerOp>)
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(), op.getRadix()));
    operands.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), symbol), operands)
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), resultType, output, 8));
    return success();
  }

private:
  std::string symbol;
};

class ClassAllocConversion final
    : public OpConversionPattern<sim::SimClassAllocOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassAllocOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    auto classType = cast<sim::ClassHandleType>(op.getResult().getType());
    Value descriptor = LLVM::AddressOfOp::create(
        rewriter, op.getLoc(), pointer,
        managedClassDescriptorName(classType.getClassName()));
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_object_allocate"),
                       ValueRange{lane, descriptor, output})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value object =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), object));
    return success();
  }
};

class CovergroupNullConversion final
    : public OpConversionPattern<sim::SimCovergroupNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupNullOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(
        op, LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()));
    return success();
  }
};

class CovergroupCreateConversion final
    : public OpConversionPattern<sim::SimCovergroupCreateOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupCreateOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
            op, op.getDeclarationAttr());
    if (!declaration)
      return failure();
    Type i64 = rewriter.getI64Type();
    Value bins = entryAlloca(rewriter, op.getLoc(), i64,
                             declaration.getCoverpointBins().size(), 8);
    for (auto [index, count] : llvm::enumerate(declaration.getCoverpointBins()))
      LLVM::StoreOp::create(
          rewriter, op.getLoc(),
          llvmConstant(rewriter, op.getLoc(), i64,
                       static_cast<uint64_t>(count)),
          byteGEP(rewriter, op.getLoc(), bins, index * sizeof(uint64_t)), 8);
    Value output = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          output, 8);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_create"),
            ValueRange{
                context,
                llvmConstant(rewriter, op.getLoc(), i64, declaration.getId()),
                bins,
                llvmConstant(rewriter, op.getLoc(), i64,
                             declaration.getCoverpointBins().size()),
                output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), i64, output, 8));
    return success();
  }
};

template <typename Op, bool Enabled>
class CovergroupControlConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_set_enabled"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), Enabled)})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupEnabledConversion final
    : public OpConversionPattern<sim::SimCovergroupSampleEnabledOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSampleEnabledOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    Type i32 = rewriter.getI32Type();
    Value output = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_sample_enabled"),
            ValueRange{context, adaptor.getHandle().front(), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value enabled = LLVM::LoadOp::create(rewriter, op.getLoc(), i32, output, 4);
    rewriter.replaceOp(op,
                       LLVM::TruncOp::create(rewriter, op.getLoc(),
                                             rewriter.getI1Type(), enabled));
    return success();
  }
};

class CovergroupBinHitConversion final
    : public OpConversionPattern<sim::SimCovergroupBinHitOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupBinHitOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_bin_hit"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), op.getCoverpoint()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), op.getBin())})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupSampleConversion final
    : public OpConversionPattern<sim::SimCovergroupSampleOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSampleOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    SmallVector<Value> hitValues = flatten(adaptor.getHits());
    Type i8 = rewriter.getI8Type();
    Value hits = entryAlloca(rewriter, op.getLoc(), i8, hitValues.size(), 1);
    for (auto [index, hit] : llvm::enumerate(hitValues))
      LLVM::StoreOp::create(
          rewriter, op.getLoc(),
          LLVM::ZExtOp::create(rewriter, op.getLoc(), i8, hit),
          byteGEP(rewriter, op.getLoc(), hits, index), 1);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_sample"),
            ValueRange{context, adaptor.getHandle().front(), hits,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), hitValues.size())})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

template <typename Op, bool IsType>
class CovergroupQueryConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type f64 = rewriter.getF64Type();
    Type i32 = rewriter.getI32Type();
    Value percentage = entryAlloca(rewriter, op.getLoc(), f64, 1, 8);
    Value covered = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    Value total = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value key;
    StringRef symbol;
    SmallVector<Value> arguments{context};
    if constexpr (IsType) {
      auto declaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
              op, op.getDeclarationAttr());
      if (!declaration)
        return failure();
      key = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                         declaration.getId());
      symbol = "obelisk_rt_v1_covergroup_type_query";
      arguments.push_back(key);
      Type i64 = rewriter.getI64Type();
      Value bins = entryAlloca(rewriter, op.getLoc(), i64,
                               declaration.getCoverpointBins().size(), 8);
      for (auto [index, count] :
           llvm::enumerate(declaration.getCoverpointBins()))
        LLVM::StoreOp::create(
            rewriter, op.getLoc(),
            llvmConstant(rewriter, op.getLoc(), i64,
                         static_cast<uint64_t>(count)),
            byteGEP(rewriter, op.getLoc(), bins, index * sizeof(uint64_t)), 8);
      arguments.push_back(bins);
      arguments.push_back(llvmConstant(rewriter, op.getLoc(), i64,
                                       declaration.getCoverpointBins().size()));
    } else {
      if (adaptor.getHandle().size() != 1)
        return failure();
      key = adaptor.getHandle().front();
      symbol = "obelisk_rt_v1_covergroup_instance_query";
      arguments.push_back(key);
    }
    arguments.append({percentage, covered, total});
    Value status =
        LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{i32},
                             SymbolRefAttr::get(rewriter.getContext(), symbol),
                             arguments)
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, ValueRange{
                LLVM::LoadOp::create(rewriter, op.getLoc(), f64, percentage, 8),
                LLVM::LoadOp::create(rewriter, op.getLoc(), i32, covered, 4),
                LLVM::LoadOp::create(rewriter, op.getLoc(), i32, total, 4)});
    return success();
  }
};

class ClassCopyConversion final
    : public OpConversionPattern<sim::SimClassCopyOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassCopyOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getSource().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    auto classType = cast<sim::ClassHandleType>(op.getResult().getType());
    Value descriptor = LLVM::AddressOfOp::create(
        rewriter, op.getLoc(), pointer,
        managedClassDescriptorName(classType.getClassName()));
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    Value source = managedObjectPointer(rewriter, op.getLoc(),
                                        adaptor.getSource().front());
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_object_shallow_copy"),
                       ValueRange{lane, descriptor, source, output})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value object =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), object));
    return success();
  }
};

class ClassIsInstanceConversion final
    : public OpConversionPattern<sim::SimClassIsInstanceOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassIsInstanceOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getObject().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value object = managedObjectPointer(rewriter, op.getLoc(),
                                        adaptor.getObject().front());
    Value descriptor = LLVM::AddressOfOp::create(
        rewriter, op.getLoc(), pointer,
        managedClassDescriptorName(op.getTargetAttr()));
    Value result = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_object_is_instance"),
                       ValueRange{object, descriptor})
                       .getResult();
    rewriter.replaceOp(op, LLVM::TruncOp::create(rewriter, op.getLoc(),
                                                 rewriter.getI1Type(), result));
    return success();
  }
};

class ClassIdConversion final : public OpConversionPattern<sim::SimClassIdOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassIdOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getObject().size() != 1)
      return failure();
    Value object = managedObjectPointer(rewriter, op.getLoc(),
                                        adaptor.getObject().front());
    rewriter.replaceOp(
        op, LLVM::CallOp::create(rewriter, op.getLoc(),
                                 TypeRange{rewriter.getI64Type()},
                                 SymbolRefAttr::get(rewriter.getContext(),
                                                    "obelisk_rt_v1_object_id"),
                                 object)
                .getResults());
    return success();
  }
};

class ClassCastConversion final
    : public OpConversionPattern<sim::SimClassCastOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassCastOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getObject().size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    auto targetType = cast<sim::ClassHandleType>(op.getResult().getType());
    Value descriptor = LLVM::AddressOfOp::create(
        rewriter, op.getLoc(), pointer,
        managedClassDescriptorName(targetType.getClassName()));
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_object_cast"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getObject().front()),
                       descriptor, output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value result =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), result));
    return success();
  }
};

class ClassFieldRefConversion final
    : public OpConversionPattern<sim::SimClassFieldRefOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassFieldRefOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getObject().size() != 1)
      return failure();
    auto field = SymbolTable::lookupNearestSymbolFrom<sim::SimClassFieldDeclOp>(
        op, op.getFieldAttr());
    if (!field || !field.getOffset())
      return op.emitError("managed class field has no native offset");
    SmallVector<Value> values{
        adaptor.getObject().front(),
        arith::ConstantOp::create(
            rewriter, op.getLoc(), rewriter.getI64Type(),
            rewriter.getI64IntegerAttr(*field.getOffset()))};
    SmallVector<ValueRange> replacements{ValueRange(values)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }
};

class ClassRootBindConversion final
    : public OpConversionPattern<sim::SimClassRootBindOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimClassRootBindOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if ((adaptor.getObject().size() != 1 && adaptor.getObject().size() != 2) ||
        adaptor.getSlot().size() != 1)
      return failure();
    Value owner = adaptor.getObject().front();
    Type sourceType = op.getObject().getType();
    uint64_t bitOffset = op.getBitOffset();
    if (isa<sim::ArgumentRefType>(sourceType)) {
      owner = arith::TruncIOp::create(rewriter, op.getLoc(),
                                      rewriter.getI64Type(), owner);
    } else if (!isa<sim::ManagedRefType>(sourceType)) {
      auto integer = dyn_cast<IntegerType>(owner.getType());
      if (!integer || integer.getWidth() < 64 ||
          bitOffset > integer.getWidth() - 64)
        return failure();
      if (bitOffset != 0)
        owner = arith::ShRUIOp::create(
            rewriter, op.getLoc(), owner,
            llvmConstant(rewriter, op.getLoc(), integer, bitOffset));
      if (integer.getWidth() != 64)
        owner = arith::TruncIOp::create(rewriter, op.getLoc(),
                                        rewriter.getI64Type(), owner);
    }
    LLVM::StoreOp::create(rewriter, op.getLoc(), owner,
                          adaptor.getSlot().front(), 8);
    rewriter.eraseOp(op);
    return success();
  }
};

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

template <typename Op>
class ManagedObjectOutputConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange input;
    if constexpr (std::is_same_v<Op, sim::SimWeakCreateOp>)
      input = adaptor.getReferent();
    else
      input = adaptor.getWeak();
    if (input.size() != 1)
      return failure();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), pointer, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                          output, 8);
    SmallVector<Value> arguments;
    if constexpr (std::is_same_v<Op, sim::SimWeakCreateOp>) {
      arguments.push_back(lane);
      auto wrapperType = cast<sim::ClassHandleType>(op.getResult().getType());
      arguments.push_back(LLVM::AddressOfOp::create(
          rewriter, op.getLoc(), pointer,
          managedClassDescriptorName(wrapperType.getClassName())));
    }
    arguments.push_back(
        managedObjectPointer(rewriter, op.getLoc(), input.front()));
    arguments.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               std::is_same_v<Op, sim::SimWeakCreateOp>
                                   ? "obelisk_rt_v1_weak_create"
                                   : "obelisk_rt_v1_weak_get"),
            arguments)
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value object =
        LLVM::LoadOp::create(rewriter, op.getLoc(), pointer, output, 8);
    rewriter.replaceOp(op, managedObjectHandle(rewriter, op.getLoc(), object));
    return success();
  }
};

class WeakClearConversion final
    : public OpConversionPattern<sim::SimWeakClearOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimWeakClearOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getWeak().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_weak_clear"),
                       managedObjectPointer(rewriter, op.getLoc(),
                                            adaptor.getWeak().front()))
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class GCSafepointConversion final
    : public OpConversionPattern<sim::SimGCSafepointOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimGCSafepointOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_gc_safepoint"),
                       lane)
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class ClassVirtualCallConversion final
    : public OpConversionPattern<sim::SimClassVirtualCallOp> {
public:
  ClassVirtualCallConversion(const TypeConverter &converter,
                             MLIRContext *context,
                             const llvm::DataLayout &dataLayout)
      : OpConversionPattern(converter, context), dataLayout(dataLayout) {}

  LogicalResult
  matchAndRewrite(sim::SimClassVirtualCallOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReceiver().size() != 1 ||
        adaptor.getArguments().size() != op.getArguments().size())
      return failure();
    for (auto [argument, converted] :
         llvm::zip_equal(op.getArguments(), adaptor.getArguments()))
      if (isa<sim::RefType>(argument.getType()) && converted.size() == 1)
        emitNativeStateRetain(rewriter, op.getLoc(), converted.front());
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Type argumentType =
        LLVM::LLVMStructType::getLiteral(rewriter.getContext(), {pointer, i64});
    SmallVector<std::pair<Value, uint64_t>> physicalArguments;
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    for (auto [original, converted] :
         llvm::zip_equal(op.getArguments(), adaptor.getArguments())) {
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(original.getType(), local,
                                                   llvmContext);
      if (failed(storage) ||
          converted.size() !=
              analysis::getSimulationPhysicalStorageCount(*storage))
        return failure();
      for (Value value : converted)
        physicalArguments.push_back({value, storage->size});
    }

    Value argumentArray = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!physicalArguments.empty()) {
      argumentArray = entryAlloca(rewriter, op.getLoc(), argumentType,
                                  physicalArguments.size(), 8);
      for (auto [index, argument] : llvm::enumerate(physicalArguments)) {
        Value data =
            entryAlloca(rewriter, op.getLoc(), argument.first.getType(), 1, 8);
        LLVM::StoreOp::create(rewriter, op.getLoc(), argument.first, data, 1);
        Value record =
            LLVM::ZeroOp::create(rewriter, op.getLoc(), argumentType);
        record = insertValue(rewriter, op.getLoc(), record, data, 0);
        record = insertValue(
            rewriter, op.getLoc(), record,
            llvmConstant(rewriter, op.getLoc(), i64, argument.second), 1);
        LLVM::StoreOp::create(
            rewriter, op.getLoc(), record,
            byteGEP(rewriter, op.getLoc(), argumentArray, index * 16), 8);
      }
    }

    SmallVector<Type> convertedResults;
    SmallVector<uint64_t> resultOffsets;
    uint64_t resultSize = 0;
    uint32_t resultAlignment = 1;
    for (Type original : op.getResultTypes()) {
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(original, local,
                                                   llvmContext);
      SmallVector<Type> converted;
      if (failed(storage) ||
          failed(getTypeConverter()->convertType(original, converted)) ||
          converted.size() !=
              analysis::getSimulationPhysicalStorageCount(*storage))
        return failure();
      for (Type type : converted) {
        uint64_t offset;
        if (!alignUp(resultSize, storage->alignment, offset) ||
            storage->size > std::numeric_limits<uint64_t>::max() - offset)
          return op.emitError("managed method result layout overflow");
        resultOffsets.push_back(offset);
        resultSize = offset + storage->size;
        resultAlignment =
            std::max<uint32_t>(resultAlignment, storage->alignment);
        convertedResults.push_back(type);
      }
    }
    if (!alignUp(resultSize, resultAlignment, resultSize))
      return op.emitError("managed method result layout overflow");
    Value resultStorage = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (resultSize != 0)
      resultStorage = entryAlloca(rewriter, op.getLoc(), rewriter.getI8Type(),
                                  resultSize, resultAlignment);

    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_method_invoke"),
            ValueRange{
                lane,
                managedObjectPointer(rewriter, op.getLoc(),
                                     adaptor.getReceiver().front()),
                llvmConstant(rewriter, op.getLoc(), i64, op.getSlot()),
                llvmConstant(rewriter, op.getLoc(), i64, op.getSignatureId()),
                argumentArray,
                llvmConstant(rewriter, op.getLoc(), i32,
                             physicalArguments.size()),
                resultStorage,
                llvmConstant(rewriter, op.getLoc(), i64, resultSize)})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> results;
    for (auto [type, offset] : llvm::zip_equal(convertedResults, resultOffsets))
      results.push_back(LLVM::LoadOp::create(
          rewriter, op.getLoc(), type,
          byteGEP(rewriter, op.getLoc(), resultStorage, offset), 1));

    SmallVector<SmallVector<Value>> replacements;
    size_t physical = 0;
    for (Type original : op.getResultTypes()) {
      SmallVector<Type> converted;
      if (failed(getTypeConverter()->convertType(original, converted)))
        return failure();
      replacements.emplace_back(results.begin() + physical,
                                results.begin() + physical + converted.size());
      physical += converted.size();
    }
    rewriter.replaceOpWithMultiple(op, std::move(replacements));
    return success();
  }

private:
  const llvm::DataLayout &dataLayout;
};

} // namespace

LogicalResult
materializeManagedMethodThunks(ModuleOp module,
                               const llvm::DataLayout &dataLayout) {
  SmallVector<sim::SimClassMethodDeclOp> methods;
  module.walk([&](sim::SimClassMethodDeclOp method) {
    if (method.getImplementation() && method.getIsVirtual())
      methods.push_back(method);
  });
  MLIRContext *context = module.getContext();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type i32 = IntegerType::get(context, 32);
  Type i64 = IntegerType::get(context, 64);
  for (sim::SimClassMethodDeclOp method : methods) {
    std::string thunkName = managedMethodThunkName(method.getSymName());
    if (module.lookupSymbol(thunkName))
      continue;
    OpBuilder builder(context);
    if (method.getIsTask()) {
      builder.setInsertionPointToStart(module.getBody());
      auto thunk = LLVM::LLVMFuncOp::create(
          builder, method.getLoc(), thunkName,
          LLVM::LLVMFunctionType::get(
              i32, {pointer, pointer, pointer, pointer, i32, pointer, i64},
              false),
          LLVM::Linkage::Internal);
      Block *entry = thunk.addEntryBlock(builder);
      builder.setInsertionPointToStart(entry);
      LLVM::ReturnOp::create(builder, method.getLoc(),
                             llvmConstant(builder, method.getLoc(), i32,
                                          OBELISK_RT_TIER_UNAVAILABLE));
      continue;
    }
    auto implementation = SymbolTable::lookupNearestSymbolFrom<func::FuncOp>(
        method, method.getImplementationAttr());
    if (!implementation)
      return method.emitError(
          "managed method implementation was not lowered to func");
    builder.setInsertionPoint(implementation);
    auto thunk = LLVM::LLVMFuncOp::create(
        builder, method.getLoc(), thunkName,
        LLVM::LLVMFunctionType::get(
            i32, {pointer, pointer, pointer, pointer, i32, pointer, i64},
            false),
        LLVM::Linkage::Internal);
    Block *entry = thunk.addEntryBlock(builder);
    builder.setInsertionPointToStart(entry);

    SmallVector<Value> callArguments{
        entry->getArgument(0),
        managedObjectHandle(builder, method.getLoc(), entry->getArgument(2))};
    FunctionType implementationType = implementation.getFunctionType();
    if (implementationType.getNumInputs() < 2)
      return method.emitError("managed method implementation has no receiver");
    unsigned physicalArgumentCount = implementationType.getNumInputs() - 2;
    for (unsigned index = 0; index != physicalArgumentCount; ++index) {
      Type type = implementationType.getInput(index + 2);
      Value record =
          byteGEP(builder, method.getLoc(), entry->getArgument(3), index * 16);
      Value data =
          LLVM::LoadOp::create(builder, method.getLoc(), pointer, record, 8);
      callArguments.push_back(
          LLVM::LoadOp::create(builder, method.getLoc(), type, data, 1));
    }
    auto call = func::CallOp::create(
        builder, method.getLoc(), implementation.getSymName(),
        implementationType.getResults(), callArguments);

    FunctionType semanticType = cast<FunctionType>(method.getFunctionType());
    llvm::DataLayout local(dataLayout.getStringRepresentation());
    llvm::LLVMContext llvmContext;
    uint64_t resultOffset = 0;
    unsigned physicalResult = 0;
    for (Type resultType : semanticType.getResults()) {
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(resultType, local,
                                                   llvmContext);
      if (failed(storage))
        return method.emitError("managed method result has no native layout");
      unsigned planes = analysis::getSimulationPhysicalStorageCount(*storage);
      for (unsigned plane = 0; plane != planes; ++plane) {
        if (physicalResult >= call.getNumResults())
          return method.emitError(
              "managed method result flattening is inconsistent");
        uint64_t aligned;
        if (!alignUp(resultOffset, storage->alignment, aligned))
          return method.emitError("managed method result layout overflow");
        resultOffset = aligned;
        LLVM::StoreOp::create(builder, method.getLoc(),
                              call.getResult(physicalResult++),
                              byteGEP(builder, method.getLoc(),
                                      entry->getArgument(5), resultOffset),
                              1);
        if (storage->size > std::numeric_limits<uint64_t>::max() - resultOffset)
          return method.emitError("managed method result layout overflow");
        resultOffset += storage->size;
      }
    }
    Value returnedStatus =
        llvmConstant(builder, method.getLoc(), i32, OBELISK_RT_OK);
    if (physicalResult + 1 == call.getNumResults()) {
      Value status = call.getResult(physicalResult);
      if (status.getType() == i32) {
        returnedStatus = status;
        ++physicalResult;
      } else if (isa<runtime::StatusType>(status.getType())) {
        returnedStatus = runtime::RTStatusToBitsOp::create(
            builder, method.getLoc(), i32, status);
        ++physicalResult;
      }
    }
    if (physicalResult != call.getNumResults())
      return method.emitError(
          "managed method result flattening is inconsistent");
    LLVM::ReturnOp::create(builder, method.getLoc(), returnedStatus);
  }
  return success();
}

void populateManagedToLLVMConversionPatterns(RewritePatternSet &patterns,
                                             TypeConverter &converter,
                                             const llvm::DataLayout &dataLayout,
                                             uint64_t stateBitCount) {
  MLIRContext *context = patterns.getContext();
  patterns.add<
      ClassNullConversion, CovergroupNullConversion, CovergroupCreateConversion,
      CovergroupEnabledConversion, CovergroupBinHitConversion,
      CovergroupSampleConversion,
      CovergroupControlConversion<sim::SimCovergroupStartOp, true>,
      CovergroupControlConversion<sim::SimCovergroupStopOp, false>,
      CovergroupQueryConversion<sim::SimCovergroupInstanceQueryOp, false>,
      CovergroupQueryConversion<sim::SimCovergroupTypeQueryOp, true>,
      ManagedNullConversion, ManagedIsNullConversion, EventNullConversion,
      ContainerSizeConversion, ContainerCreateLikeConversion,
      ContainerCreateConversion, ContainerCloneConversion,
      ContainerDeleteConversion, QueueDeleteConversion, QueueInsertConversion,
      ContainerReadConversion, ContainerWriteConversion, AssocCreateConversion,
      AssocReadConversion, AssocWriteConversion, AssocDefaultConversion,
      AssocExistsConversion, AssocDeleteConversion, AssocTraverseConversion,
      RandomNextConversion, RandomSeedConversion, RandomBoundedConversion,
      StringLiteralConversion, StringFromPackedConversion,
      StringToPackedConversion, StringConcatConversion, StringLengthConversion,
      StringGetcConversion, StringCompareConversion, ClassAllocConversion,
      StringFileOpenConversion<sim::SimFileOpenStringMCDOp>,
      StringFileOpenConversion<sim::SimFileOpenStringOp>,
      StringFileGetlineConversion, ClassCopyConversion,
      ClassIsInstanceConversion, ClassIdConversion, ClassCastConversion,
      ClassFieldRefConversion, ClassRootBindConversion,
      ArgumentRefFromRefConversion, ArgumentRefFromManagedConversion,
      ReferencePathIndexConversion, ReferencePathAssocConversion,
      ArgumentRefFromPathConversion,
      ManagedObjectOutputConversion<sim::SimWeakCreateOp>,
      ManagedObjectOutputConversion<sim::SimWeakGetOp>, WeakClearConversion,
      GCSafepointConversion>(converter, context);
  patterns.add<StringAllocatingCallConversion<sim::SimStringRepeatOp>>(
      converter, context, "obelisk_rt_v1_string_repeat");
  patterns.add<StringAllocatingCallConversion<sim::SimStringPutcOp>>(
      converter, context, "obelisk_rt_v1_string_putc");
  patterns.add<StringAllocatingCallConversion<sim::SimStringSubstrOp>>(
      converter, context, "obelisk_rt_v1_string_substr");
  patterns.add<StringAllocatingCallConversion<sim::SimStringCaseConvertOp>>(
      converter, context, "obelisk_rt_v1_string_case_convert");
  patterns.add<StringAllocatingCallConversion<sim::SimStringFormatIntegerOp>>(
      converter, context, "obelisk_rt_v1_string_format_integer");
  patterns.add<StringAllocatingCallConversion<sim::SimStringFormatRealOp>>(
      converter, context, "obelisk_rt_v1_string_format_real");
  patterns.add<StringParseConversion<sim::SimStringParseIntegerOp>>(
      converter, context, "obelisk_rt_v1_string_parse_integer");
  patterns.add<StringParseConversion<sim::SimStringParseRealOp>>(
      converter, context, "obelisk_rt_v1_string_parse_real");
  patterns.add<ArgumentRefLoadConversion, ArgumentRefStoreConversion>(
      converter, context, dataLayout, stateBitCount);
  patterns
      .add<ManagedLoadConversion, ManagedStoreConversion, ManagedNBAConversion,
           ReferencePathNBAConversion, ClassVirtualCallConversion>(
          converter, context, dataLayout);
}

} // namespace obelisk::detail
