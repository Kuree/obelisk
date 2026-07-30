//===- SimulationOverrideLowering.cpp - Override rewrite patterns -------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

class OverrideConversion final
    : public OpConversionPattern<sim::SimOverrideOp> {
public:
  OverrideConversion(const TypeConverter &converter, MLIRContext *context,
                     uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimOverrideOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
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
};

class ReleaseOverrideConversion final
    : public OpConversionPattern<sim::SimReleaseOverrideOp> {
public:
  ReleaseOverrideConversion(const TypeConverter &converter,
                            MLIRContext *context, uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimReleaseOverrideOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
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
};

} // namespace

void populateOverrideToLLVMConversionPatterns(RewritePatternSet &patterns,
                                              TypeConverter &converter,
                                              uint64_t stateBitCount) {
  patterns.add<OverrideConversion, ReleaseOverrideConversion>(
      converter, patterns.getContext(), stateBitCount);
}

} // namespace obelisk::detail
