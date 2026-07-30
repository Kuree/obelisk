//===- SimulationManagedContainerLowering.cpp - Container patterns ---===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"

#include <cstdint>

using namespace mlir;

namespace obelisk::detail {

Value zeroNativeValue(OpBuilder &builder, Location location, Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getIntegerAttr(integer, 0));
  if (auto floating = dyn_cast<FloatType>(type))
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getFloatAttr(floating, 0.0));
  return {};
}

namespace {

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

} // namespace


void populateManagedContainerToLLVMConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter) {
  MLIRContext *context = patterns.getContext();
  patterns.add<
      ContainerSizeConversion, ContainerCreateLikeConversion,
      ContainerCreateConversion, ContainerCloneConversion,
      ContainerDeleteConversion, QueueDeleteConversion,
      QueueInsertConversion, ContainerReadConversion,
      ContainerWriteConversion, RandomNextConversion, RandomSeedConversion,
      RandomBoundedConversion>(converter, context);
  populateManagedAssociativeToLLVMConversionPatterns(patterns, converter);
}

} // namespace obelisk::detail
