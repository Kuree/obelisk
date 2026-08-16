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

class ManagedWatchConversion final
    : public OpConversionPattern<sim::SimManagedWatchOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimManagedWatchOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange input = adaptor.getInput();
    Value object;
    Value selector;
    uint32_t runtimeKind;
    switch (op.getKind()) {
    case sim::ManagedWatchKind::Field:
      if (input.size() != 2)
        return failure();
      object = input[0];
      selector = input[1];
      runtimeKind = OBELISK_RT_MANAGED_WATCH_FIELD;
      break;
    case sim::ManagedWatchKind::ContainerSize:
      if (input.size() != 1)
        return failure();
      object = input[0];
      selector = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), 0);
      runtimeKind = OBELISK_RT_MANAGED_WATCH_CONTAINER_SIZE;
      break;
    default:
      return rewriter.notifyMatchFailure(op, "unknown managed-watch kind");
    }
    Value kind = llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(),
                              runtimeKind);
    rewriter.replaceOp(
        op,
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI64Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_managed_watch"),
            ValueRange{managedObjectPointer(rewriter, op.getLoc(), object),
                       kind, selector})
            .getResults());
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
    if (op.getMode() == sim::ManagedRootMode::Candidate) {
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, op.getLoc(), LLVM::LLVMPointerType::get(rewriter.getContext()),
          "__obelisk_current_context");
      Value context = LLVM::LoadOp::create(
          rewriter, op.getLoc(), LLVM::LLVMPointerType::get(rewriter.getContext()),
          contextAddress, 8);
      owner = LLVM::CallOp::create(
                  rewriter, op.getLoc(), TypeRange{rewriter.getI64Type()},
                  SymbolRefAttr::get(rewriter.getContext(),
                                     "obelisk_rt_v1_gc_candidate_root"),
                  ValueRange{context, owner,
                             llvmConstant(rewriter, op.getLoc(),
                                          rewriter.getI32Type(),
                                          op.getKindMask())})
                  .getResult();
    }
    LLVM::StoreOp::create(rewriter, op.getLoc(), owner,
                          adaptor.getSlot().front(), 8);
    rewriter.eraseOp(op);
    return success();
  }
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
    SmallVector<Value> invokeArguments{
        lane, managedObjectPointer(rewriter, op.getLoc(),
                                   adaptor.getReceiver().front())};
    StringRef invokeName = "obelisk_rt_v1_method_invoke";
    auto method =
        SymbolTable::lookupNearestSymbolFrom<sim::SimClassMethodDeclOp>(
            op, op.getMethodAttr());
    auto owner =
        method ? SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                     method, method.getOwnerAttr())
               : sim::SimClassDeclOp{};
    if (!method || !owner)
      return op.emitError("virtual method descriptor is missing");
    if (owner.getIsInterface()) {
      if (!method.getInterfaceOrdinalAttr())
        return op.emitError("interface method has no dispatch ordinal");
      invokeName = "obelisk_rt_v1_interface_method_invoke";
      invokeArguments.push_back(
          llvmConstant(rewriter, op.getLoc(), i64, owner.getId()));
      invokeArguments.push_back(llvmConstant(rewriter, op.getLoc(), i64,
                                             *method.getInterfaceOrdinal()));
    } else {
      invokeArguments.push_back(
          llvmConstant(rewriter, op.getLoc(), i64, op.getSlot()));
    }
    invokeArguments.push_back(
        llvmConstant(rewriter, op.getLoc(), i64, op.getSignatureId()));
    invokeArguments.push_back(argumentArray);
    invokeArguments.push_back(
        llvmConstant(rewriter, op.getLoc(), i32, physicalArguments.size()));
    invokeArguments.push_back(resultStorage);
    invokeArguments.push_back(
        llvmConstant(rewriter, op.getLoc(), i64, resultSize));
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{i32},
                       SymbolRefAttr::get(rewriter.getContext(), invokeName),
                       invokeArguments)
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
      auto implementation = SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(
          method, method.getImplementationAttr());
      if (!implementation)
        return method.emitError(
            "managed task implementation was not preserved as a process");
      std::string helperName =
          (implementation.getSymName() + ".__obelisk_activate_checked").str();
      auto helper = SymbolTable::lookupNearestSymbolFrom<LLVM::LLVMFuncOp>(
          method, FlatSymbolRefAttr::get(context, helperName));
      if (!helper)
        return method.emitError(
            "managed task implementation has no checked activation helper");
      ArrayRef<Type> helperInputs = helper.getFunctionType().getParams();
      if (helperInputs.size() < 3 || helperInputs.front() != pointer ||
          helperInputs[1] != i64 || helperInputs.back() != pointer)
        return method.emitError(
            "managed task activation helper has an incompatible ABI");
      unsigned physicalArgumentCount = helperInputs.size() - 3;
      FunctionType semanticType = cast<FunctionType>(method.getFunctionType());
      SmallVector<uint64_t> expectedSizes;
      llvm::DataLayout local(dataLayout.getStringRepresentation());
      llvm::LLVMContext llvmContext;
      for (Type argument : semanticType.getInputs().drop_front(2)) {
        FailureOr<analysis::SimulationStorageProperties> storage =
            analysis::getSimulationStorageProperties(argument, local,
                                                     llvmContext);
        if (failed(storage))
          return method.emitError("managed task argument has no native ABI");
        unsigned planes =
            analysis::getSimulationPhysicalStorageCount(*storage);
        for (unsigned plane = 0; plane != planes; ++plane)
          expectedSizes.push_back(storage->size);
      }
      if (expectedSizes.size() != physicalArgumentCount)
        return method.emitError(
            "managed task argument flattening is inconsistent");
      builder.setInsertionPointToStart(module.getBody());
      auto thunk = LLVM::LLVMFuncOp::create(
          builder, method.getLoc(), thunkName,
          LLVM::LLVMFunctionType::get(
              i32, {pointer, pointer, pointer, pointer, i32, pointer, i64},
              false),
          LLVM::Linkage::Internal);
      copyNativePartition(helper, thunk);
      Block *entry = thunk.addEntryBlock(builder);
      Block *invoke = new Block;
      Block *invalid = new Block;
      SmallVector<Block *> validations(physicalArgumentCount);
      for (Block *&block : validations) {
        block = new Block;
        thunk.getBody().push_back(block);
      }
      thunk.getBody().push_back(invoke);
      thunk.getBody().push_back(invalid);
      builder.setInsertionPointToStart(entry);
      Value countMatches = arith::CmpIOp::create(
          builder, method.getLoc(), arith::CmpIPredicate::eq,
          entry->getArgument(4),
          llvmConstant(builder, method.getLoc(), i32, physicalArgumentCount));
      Value resultSizeMatches = arith::CmpIOp::create(
          builder, method.getLoc(), arith::CmpIPredicate::eq,
          entry->getArgument(6),
          llvmConstant(builder, method.getLoc(), i64, sizeof(uint64_t)));
      Value validABI = arith::AndIOp::create(builder, method.getLoc(),
                                             countMatches, resultSizeMatches);
      LLVM::CondBrOp::create(
          builder, method.getLoc(), validABI,
          validations.empty() ? invoke : validations.front(), invalid);

      builder.setInsertionPointToStart(invalid);
      LLVM::ReturnOp::create(builder, method.getLoc(),
                             llvmConstant(builder, method.getLoc(), i32,
                                          OBELISK_RT_ARGUMENT_MISMATCH));

      SmallVector<Value> argumentData;
      argumentData.reserve(physicalArgumentCount);
      for (unsigned index = 0; index != physicalArgumentCount; ++index) {
        builder.setInsertionPointToStart(validations[index]);
        Value record = byteGEP(builder, method.getLoc(), entry->getArgument(3),
                               index * 16);
        Value data =
            LLVM::LoadOp::create(builder, method.getLoc(), pointer, record, 8);
        Value size = LLVM::LoadOp::create(
            builder, method.getLoc(), i64,
            byteGEP(builder, method.getLoc(), record, 8), 8);
        Value hasData = LLVM::ICmpOp::create(
            builder, method.getLoc(), LLVM::ICmpPredicate::ne, data,
            LLVM::ZeroOp::create(builder, method.getLoc(), pointer));
        Value sizeMatches = arith::CmpIOp::create(
            builder, method.getLoc(), arith::CmpIPredicate::eq, size,
            llvmConstant(builder, method.getLoc(), i64, expectedSizes[index]));
        Value validRecord = arith::AndIOp::create(builder, method.getLoc(),
                                                  hasData, sizeMatches);
        LLVM::CondBrOp::create(
            builder, method.getLoc(), validRecord,
            index + 1 == physicalArgumentCount ? invoke
                                               : validations[index + 1],
            invalid);
        argumentData.push_back(data);
      }

      builder.setInsertionPointToStart(invoke);
      SmallVector<Value> callArguments{
          entry->getArgument(0),
          managedObjectHandle(builder, method.getLoc(),
                              entry->getArgument(2))};
      for (unsigned index = 0; index != physicalArgumentCount; ++index) {
        callArguments.push_back(LLVM::LoadOp::create(
            builder, method.getLoc(), helperInputs[index + 2],
            argumentData[index], 1));
      }
      callArguments.push_back(entry->getArgument(5));
      Value status =
          LLVM::CallOp::create(builder, method.getLoc(), TypeRange{i32},
                               SymbolRefAttr::get(context, helperName),
                               callArguments)
              .getResult();
      LLVM::ReturnOp::create(builder, method.getLoc(), status);
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
    copyNativePartition(implementation, thunk);
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
      ClassNullConversion, ManagedNullConversion, ManagedIsNullConversion,
      EventNullConversion,
      ClassAllocConversion, ClassCopyConversion,
      ClassIsInstanceConversion, ClassIdConversion, ClassCastConversion,
      ClassFieldRefConversion, ManagedWatchConversion, ClassRootBindConversion,
      ManagedObjectOutputConversion<sim::SimWeakCreateOp>,
      ManagedObjectOutputConversion<sim::SimWeakGetOp>, WeakClearConversion,
      GCSafepointConversion>(converter, context);
  populateManagedContainerToLLVMConversionPatterns(patterns, converter);
  populateManagedCoverageToLLVMConversionPatterns(patterns, converter);
  populateManagedReferenceToLLVMConversionPatterns(
      patterns, converter, dataLayout, stateBitCount);
  populateManagedStringToLLVMConversionPatterns(patterns, converter);
  patterns.add<ClassVirtualCallConversion>(converter, context, dataLayout);
}

} // namespace obelisk::detail
